/*
 * mod-world-randomizer: Hardcore (one life).
 *
 * A hardcore character that dies is never deleted for good. Depending on Randomizer.Hardcore.OnDeath it:
 *   0 - stays a ghost forever (can still log in, can't be resurrected),
 *   1 - is locked (kicked, and kicked again on every login),
 *   2 - is soft deleted with the core's own unlink: the character, items and all stay in the database, only
 *       name and account move to deleteInfos_Name / deleteInfos_Account.
 * `world_randomizer_hardcore` keeps every hardcore character with its owner account and how it died.
 *
 * Going hardcore (.hardcore on confirm, Mode 1) is only for a fresh character: level 1, nothing but its starting gear
 * and starting money, and no mail received or trade made, ever. Mail and trades are remembered in
 * `world_randomizer_hardcore_taint` from the moment they happen, and any failed check is saved too: once a character
 * is ruled out it stays ruled out, unless an admin overrides it with .hardcore allow <name or GUID>.
 *
 * A death caused by a bug can be undone with .hardcore revive <name> [reason]: the character is linked back to its
 * account (if it was soft deleted), comes back alive on its next login, and the revive is logged with who did it
 * and why. Restoring with the core's .character deleted restore also counts as a revive on the next login.
 */

#include "WorldRandomizer.h"
#include "WorldRandomizerAchievements.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Bag.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Item.h"
#include "Mail.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "Timer.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    enum HardcoreStatus : uint8
    {
        HARDCORE_ALIVE = 0,
        HARDCORE_DEAD  = 1
    };

    enum HardcoreOnDeath : uint8
    {
        ON_DEATH_GHOST       = 0,
        ON_DEATH_LOCK        = 1,
        ON_DEATH_SOFT_DELETE = 2
    };

    enum KillerType : uint8
    {
        KILLER_UNKNOWN  = 0, // falling, drowning, fatigue, lava...
        KILLER_CREATURE = 1,
        KILLER_PLAYER   = 2
    };

    constexpr uint32 RECORD_DELAY = 1000;  // ms, so the killer is known before the death is written
    constexpr uint32 UNLINK_DELAY = 10000; // ms after the kick, so the logout save is done first

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
        return text;
    }
}

class HardcoreMgr
{
public:
    struct Entry
    {
        uint32 account = 0;
        std::string name;
        uint8 race = 0;
        uint8 cls = 0;
        uint8 gender = 0;
        uint8 level = 1;
        uint8 status = HARDCORE_ALIVE;
        bool softDeleted = false;
        bool revivePending = false;
        uint32 revives = 0;
        uint32 diedAt = 0;
    };

    struct PendingDeath
    {
        uint32 elapsed = 0;
        uint32 mapId = 0;
        uint32 zoneId = 0;
        uint8 level = 1;
        uint8 killerType = KILLER_UNKNOWN;
        uint32 killerEntry = 0;
        std::string killerName;
        bool recorded = false;
        bool kicked = false;
    };

    static HardcoreMgr* instance()
    {
        static HardcoreMgr instance;
        return &instance;
    }

    bool Enabled() const { return sWorldRandomizer->Config().hardcoreMode != 0; }

    void Load()
    {
        _entries.clear();
        _taints.clear();

        if (QueryResult taints = CharacterDatabase.Query(
            "SELECT `guid`, `reason`, `allowed` FROM `world_randomizer_hardcore_taint`"))
        {
            do
            {
                Taint& taint = _taints[(*taints)[0].Get<uint32>()];
                taint.reason = (*taints)[1].Get<std::string>();
                taint.allowed = (*taints)[2].Get<bool>();
            } while (taints->NextRow());
        }

        //                                                       0       1          2       3       4        5
        QueryResult result = CharacterDatabase.Query("SELECT `guid`, `account`, `name`, `race`, `class`, `gender`, "
            //   6        7         8               9                 10         11
            "`level`, `status`, `soft_deleted`, `revive_pending`, `revives`, `died_at` "
            "FROM `world_randomizer_hardcore`");
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            Entry entry;
            entry.account = fields[1].Get<uint32>();
            entry.name = fields[2].Get<std::string>();
            entry.race = fields[3].Get<uint8>();
            entry.cls = fields[4].Get<uint8>();
            entry.gender = fields[5].Get<uint8>();
            entry.level = fields[6].Get<uint8>();
            entry.status = fields[7].Get<uint8>();
            entry.softDeleted = fields[8].Get<bool>();
            entry.revivePending = fields[9].Get<bool>();
            entry.revives = fields[10].Get<uint32>();
            entry.diedAt = fields[11].Get<uint32>();
            _entries[fields[0].Get<uint32>()] = entry;
        } while (result->NextRow());

        // A crash between the death and the unlink: finish the unlink now
        uint32 unlinked = 0;
        if (sWorldRandomizer->Config().hardcoreOnDeath == ON_DEATH_SOFT_DELETE)
            for (auto const& [guid, entry] : _entries)
                if (entry.status == HARDCORE_DEAD && !entry.softDeleted)
                {
                    _unlinkQueue[guid] = UNLINK_DELAY;
                    ++unlinked;
                }

        LOG_INFO("module", "mod-world-randomizer: {} hardcore characters loaded{}.", _entries.size(),
            unlinked ? Acore::StringFormat(", {} soft deletes to finish", unlinked) : "");
    }

    Entry* Find(ObjectGuid::LowType guid)
    {
        auto itr = _entries.find(guid);
        return itr != _entries.end() ? &itr->second : nullptr;
    }

    // Hardcore characters matching a name (case-insensitive) or a character GUID, dead ones only unless allowAlive
    std::vector<ObjectGuid::LowType> FindMatches(std::string const& nameOrGuid, bool allowAlive = false) const
    {
        std::vector<ObjectGuid::LowType> matches;
        if (Optional<uint32> guid = Acore::StringTo<uint32>(nameOrGuid))
        {
            auto itr = _entries.find(*guid);
            if (itr != _entries.end() && (allowAlive || itr->second.status == HARDCORE_DEAD))
                matches.push_back(*guid);
            return matches;
        }

        std::string wanted = Lower(nameOrGuid);
        for (auto const& [guid, entry] : _entries)
            if (Lower(entry.name) == wanted && (allowAlive || entry.status == HARDCORE_DEAD))
                matches.push_back(guid);

        std::sort(matches.begin(), matches.end());
        return matches;
    }

    // "Hardxammy / GUID 741158 / Account 54394 / Hardcore / died on 2026-10-20 at level 25"
    std::string Describe(ObjectGuid::LowType guid) const
    {
        auto itr = _entries.find(guid);
        if (itr == _entries.end())
            return Acore::StringFormat("GUID {}", guid);

        Entry const& entry = itr->second;
        std::string state = entry.status == HARDCORE_DEAD ?
            Acore::StringFormat("died on {} at level {}", entry.diedAt ?
                Acore::Time::TimeToTimestampStr(Seconds(entry.diedAt), "%Y-%m-%d") : "?", entry.level) :
            Acore::StringFormat("alive, level {}", entry.level);

        return Acore::StringFormat("{} / GUID {} / Account {} / Hardcore{} / {}", entry.name, guid, entry.account,
            entry.softDeleted ? " (soft deleted)" : "", state);
    }

    // Rules a character out of hardcore for good (saved), unless an admin allowed it. The first reason is kept.
    void MarkIneligible(ObjectGuid::LowType guid, std::string reason)
    {
        if (!guid || _entries.count(guid) || _taints.count(guid))
            return;

        if (reason.size() > 255)
            reason.resize(255);

        _taints[guid].reason = reason;
        CharacterDatabase.EscapeString(reason);
        CharacterDatabase.Execute("INSERT IGNORE INTO `world_randomizer_hardcore_taint` (`guid`, `reason`, `at`) "
            "VALUES ({}, '{}', {})", guid, reason, GameTime::GetGameTime().count());
    }

    // Admin override: this character may go hardcore whatever the checks say (or no longer may)
    void SetAllowed(ObjectGuid::LowType guid, bool allowed, std::string const& by)
    {
        Taint& taint = _taints[guid];
        taint.allowed = allowed;
        if (taint.reason.empty())
            taint.reason = allowed ? "allowed by an admin" : "blocked by an admin";

        std::string reason = taint.reason;
        std::string escapedBy = by;
        CharacterDatabase.EscapeString(reason);
        CharacterDatabase.EscapeString(escapedBy);
        CharacterDatabase.Execute("INSERT INTO `world_randomizer_hardcore_taint` (`guid`, `reason`, `at`, `allowed`, "
            "`allowed_by`, `allowed_at`) VALUES ({0}, '{1}', {2}, {3}, '{4}', {2}) ON DUPLICATE KEY UPDATE "
            "`allowed` = {3}, `allowed_by` = '{4}', `allowed_at` = {2}", guid, reason, GameTime::GetGameTime().count(),
            allowed ? 1 : 0, escapedBy);
    }

    // "allowed by an admin", "ruled out: <reason>" or "" (nothing saved)
    std::string EligibilityNote(ObjectGuid::LowType guid) const
    {
        auto itr = _taints.find(guid);
        if (itr == _taints.end())
            return "";
        return itr->second.allowed ? "allowed by an admin" : "ruled out: " + itr->second.reason;
    }

    // Everything that stops this character from going hardcore; empty = allowed. A failure is saved for good.
    std::vector<std::string> CheckFreshStart(Player* player)
    {
        std::vector<std::string> problems;
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();

        auto saved = _taints.find(guid);
        if (saved != _taints.end() && saved->second.allowed)
            return problems;

        if (player->GetLevel() != 1)
            problems.push_back(Acore::StringFormat("you are level {}, a hardcore run starts at level 1",
                player->GetLevel()));

        // What the character was created with: CharStartOutfit.dbc (as Player::Create counts it) and
        // `playercreateinfo_item`
        std::unordered_map<uint32, uint32> allowed;
        if (CharStartOutfitEntry const* outfit = GetCharStartOutfitEntry(player->getRace(), player->getClass(),
            player->getGender()))
        {
            for (int32 itemId : outfit->ItemId)
            {
                if (itemId <= 0)
                    continue;

                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(uint32(itemId));
                if (!proto)
                    continue;

                uint32 count = proto->BuyCount;
                if (proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_FOOD)
                {
                    if (proto->Spells[0].SpellCategory == SPELL_CATEGORY_FOOD)
                        count = player->IsClass(CLASS_DEATH_KNIGHT, CLASS_CONTEXT_INIT) ? 10 : 4;
                    else if (proto->Spells[0].SpellCategory == SPELL_CATEGORY_DRINK)
                        count = 2;
                    count = std::min<uint32>(count, proto->GetMaxStackSize());
                }

                allowed[uint32(itemId)] += count;
            }
        }

        if (PlayerInfo const* info = sObjectMgr->GetPlayerInfo(player->getRace(), player->getClass()))
            for (PlayerCreateInfoItem const& item : info->item)
                allowed[item.item_id] += item.item_amount;

        // Achievement start rewards are starting gear too
        uint32 accountId = player->GetSession()->GetAccountId();
        for (auto const& [item, amount] : WorldRandomizerAchievements::GetStartItems(accountId))
            allowed[item] += amount;

        // Everything carried, worn, in bags, the bank, the keyring and the currency tab
        std::unordered_map<uint32, uint32> carried;
        auto count = [&carried](Item const* item)
        {
            if (item)
                carried[item->GetEntry()] += item->GetCount();
        };

        for (uint8 slot = EQUIPMENT_SLOT_START; slot < CURRENCYTOKEN_SLOT_END; ++slot)
        {
            if (slot >= BUYBACK_SLOT_START && slot < BUYBACK_SLOT_END)
                continue;

            count(player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
            if (Bag* bag = player->GetBagByPos(slot))
                for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                    count(bag->GetItemByPos(uint8(i)));
        }

        for (auto const& [entry, amount] : carried)
        {
            auto itr = allowed.find(entry);
            if (itr != allowed.end() && amount <= itr->second)
                continue;

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
            problems.push_back(Acore::StringFormat("you carry {}{} that is not starting gear",
                proto ? proto->Name1 : Acore::StringFormat("item {}", entry),
                itr != allowed.end() ? " (more than you started with)" : ""));
        }

        if (player->GetMoney() > sWorld->getIntConfig(CONFIG_START_PLAYER_MONEY) +
            WorldRandomizerAchievements::GetStartMoney(accountId))
            problems.push_back("you have more money than you started with");

        if (player->GetMailSize() ||
            CharacterDatabase.Query("SELECT 1 FROM `mail` WHERE `receiver` = {} LIMIT 1", guid))
            problems.push_back("this character has received mail");

        // Already ruled out earlier (a trade, a mail, a failed check): that stays, even if the cause is gone now
        if (saved != _taints.end())
            problems.insert(problems.begin(), "ruled out earlier: " + saved->second.reason);
        else if (!problems.empty())
        {
            std::string reason;
            for (std::string const& problem : problems)
                reason += (reason.empty() ? "" : "; ") + problem;
            MarkIneligible(guid, reason);
        }

        return problems;
    }

    bool IsHardcore(Player const* player) const
    {
        return _entries.count(player->GetGUID().GetCounter()) != 0;
    }

    bool IsDead(Player const* player) const
    {
        auto itr = _entries.find(player->GetGUID().GetCounter());
        return itr != _entries.end() && itr->second.status == HARDCORE_DEAD;
    }

    void Register(Player* player)
    {
        Entry entry;
        entry.account = player->GetSession()->GetAccountId();
        entry.name = player->GetName();
        entry.race = player->getRace();
        entry.cls = player->getClass();
        entry.gender = player->getGender();
        entry.level = player->GetLevel();
        _entries[player->GetGUID().GetCounter()] = entry;

        CharacterDatabase.Execute("REPLACE INTO `world_randomizer_hardcore` (`guid`, `account`, `name`, `race`, "
            "`class`, `gender`, `level`, `status`, `started_at`, `seed`) "
            "VALUES ({}, {}, '{}', {}, {}, {}, {}, 0, {}, {})",
            player->GetGUID().GetCounter(), entry.account, entry.name, entry.race, entry.cls, entry.gender, entry.level,
            GameTime::GetGameTime().count(), sWorldRandomizer->GetSeed());
        WorldRandomizerAchievements::ChallengeStarted(player);

        ChatHandler(player->GetSession()).SendSysMessage("|cffFF2020[Hardcore]|r You have one life. If you die, this "
            "character is gone.");

        if (sWorldRandomizer->Config().hardcoreAnnounce)
            ChatHandler(nullptr).SendWorldText(Acore::StringFormat("|cffFF2020[Hardcore]|r {} has started a "
                "hardcore run.", entry.name));
    }

    void OnDeath(Player* player)
    {
        Entry* entry = Find(player->GetGUID().GetCounter());
        if (!entry || entry->status != HARDCORE_ALIVE)
            return;

        RandomizerConfig const& config = sWorldRandomizer->Config();
        if (config.hardcoreIgnoreBattlegrounds && (player->InBattleground() || player->InArena()))
            return;

        entry->status = HARDCORE_DEAD;
        entry->level = player->GetLevel();
        entry->diedAt = uint32(GameTime::GetGameTime().count());

        PendingDeath& death = _pending[player->GetGUID().GetCounter()];
        death = PendingDeath();
        death.mapId = player->GetMapId();
        death.zoneId = player->GetZoneId();
        death.level = player->GetLevel();
    }

    void OnKilledBy(Player* victim, Unit* killer)
    {
        auto itr = _pending.find(victim->GetGUID().GetCounter());
        if (itr == _pending.end() || itr->second.recorded || !killer || killer == victim)
            return;

        PendingDeath& death = itr->second;
        if (Player* player = killer->ToPlayer())
        {
            if (!sWorldRandomizer->Config().hardcoreCountPvP)
            {
                // PvP deaths don't count: this life goes on
                _pending.erase(itr);
                if (Entry* entry = Find(victim->GetGUID().GetCounter()))
                    entry->status = HARDCORE_ALIVE;
                return;
            }

            death.killerType = KILLER_PLAYER;
            death.killerName = player->GetName();
        }
        else if (Creature* creature = killer->ToCreature())
        {
            death.killerType = KILLER_CREATURE;
            death.killerEntry = creature->GetEntry();
            death.killerName = creature->GetName();
        }
    }

    void Update(uint32 diff)
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();

        for (auto itr = _pending.begin(); itr != _pending.end();)
        {
            ObjectGuid::LowType guid = itr->first;
            PendingDeath& death = itr->second;
            death.elapsed += diff;

            if (!death.recorded && death.elapsed >= RECORD_DELAY)
            {
                RecordDeath(guid, death);
                death.recorded = true;
            }

            if (death.recorded && config.hardcoreOnDeath == ON_DEATH_GHOST)
            {
                itr = _pending.erase(itr);
                continue;
            }

            if (death.recorded && !death.kicked && death.elapsed >= config.hardcoreKickDelay * IN_MILLISECONDS)
            {
                if (Player* player = ObjectAccessor::FindPlayerByLowGUID(guid))
                    player->GetSession()->KickPlayer("Hardcore: character died");

                death.kicked = true;
                if (config.hardcoreOnDeath == ON_DEATH_SOFT_DELETE)
                    _unlinkQueue[guid] = 0;

                itr = _pending.erase(itr);
                continue;
            }

            ++itr;
        }

        for (auto itr = _unlinkQueue.begin(); itr != _unlinkQueue.end();)
        {
            itr->second += diff;
            if (itr->second < UNLINK_DELAY || ObjectAccessor::FindPlayerByLowGUID(itr->first))
            {
                ++itr;
                continue;
            }

            SoftDelete(itr->first);
            itr = _unlinkQueue.erase(itr);
        }
    }

    void OnLogin(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        Entry* entry = Find(guid);
        if (!entry)
            return;

        RandomizerConfig const& config = sWorldRandomizer->Config();
        ChatHandler handler(player->GetSession());

        // Logging in although soft deleted: someone used .character deleted restore
        if (entry->status == HARDCORE_DEAD && entry->softDeleted)
            Revive(guid, "restored with .character deleted restore", "core", false);

        if (entry->status == HARDCORE_DEAD)
        {
            if (config.hardcoreOnDeath == ON_DEATH_GHOST)
            {
                handler.SendSysMessage("|cffFF2020[Hardcore]|r This character died. It stays a ghost forever.");
                return;
            }

            handler.SendSysMessage("|cffFF2020[Hardcore]|r This character died and can't be played any more.");
            PendingDeath& death = _pending[guid];
            death.recorded = true;
            death.elapsed = config.hardcoreKickDelay > 3 ? (config.hardcoreKickDelay - 3) * IN_MILLISECONDS : 0;
            return;
        }

        if (entry->revivePending)
        {
            if (player->isDead())
            {
                player->ResurrectPlayer(1.0f, false);
                player->SpawnCorpseBones();
            }

            entry->revivePending = false;
            CharacterDatabase.Execute("UPDATE `world_randomizer_hardcore` SET `revive_pending` = 0 WHERE `guid` = {}",
                guid);
            handler.SendSysMessage("|cffFF2020[Hardcore]|r A GM revived you. Your one life continues.");
            return;
        }

        handler.SendSysMessage("|cffFF2020[Hardcore]|r You are on your one life. Be careful.");
    }

    // Returns an error message, or an empty string on success
    std::string Revive(ObjectGuid::LowType guid, std::string const& reason, std::string const& by, bool relink)
    {
        Entry* entry = Find(guid);
        if (!entry)
            return "That character is not hardcore.";

        if (entry->status != HARDCORE_DEAD)
            return "That character is not dead.";

        if (relink && entry->softDeleted)
        {
            std::string name = entry->name;
            CharacterDatabase.EscapeString(name);
            if (CharacterDatabase.Query("SELECT 1 FROM `characters` WHERE `name` = '{}' AND `guid` <> {}", name, guid))
                return Acore::StringFormat("The name {} is taken now. Use .character deleted restore {} <new name> to "
                    "bring it back under another name; it counts as a revive on its next login.", entry->name, guid);

            // The reverse of the core's unlink (CHAR_UPD_DELETE_INFO)
            CharacterDatabase.DirectExecute("UPDATE `characters` SET `name` = `deleteInfos_Name`, `account` = "
                "`deleteInfos_Account`, `deleteDate` = NULL, `deleteInfos_Name` = NULL, `deleteInfos_Account` = NULL "
                "WHERE `guid` = {} AND `deleteInfos_Account` IS NOT NULL", guid);

            sCharacterCache->AddCharacterCacheEntry(ObjectGuid::Create<HighGuid::Player>(guid), entry->account,
                entry->name, entry->gender, entry->race, entry->cls, entry->level);
            sWorld->UpdateRealmCharCount(entry->account);
        }

        _pending.erase(guid);
        _unlinkQueue.erase(guid);

        entry->status = HARDCORE_ALIVE;
        entry->softDeleted = false;
        entry->revivePending = true;
        ++entry->revives;

        std::string escapedReason = reason;
        std::string escapedBy = by;
        CharacterDatabase.EscapeString(escapedReason);
        CharacterDatabase.EscapeString(escapedBy);
        CharacterDatabase.Execute("UPDATE `world_randomizer_hardcore` SET `status` = 0, `soft_deleted` = 0, "
            "`revive_pending` = 1, `revives` = `revives` + 1 WHERE `guid` = {}", guid);
        CharacterDatabase.Execute("INSERT INTO `world_randomizer_hardcore_revive` (`guid`, `revived_at`, `revived_by`, "
            "`reason`) VALUES ({}, {}, '{}', '{}')", guid, GameTime::GetGameTime().count(), escapedBy, escapedReason);

        // Online (a ghost): bring them back right away
        if (Player* player = ObjectAccessor::FindPlayerByLowGUID(guid))
            OnLogin(player);

        if (sWorldRandomizer->Config().hardcoreAnnounce)
            ChatHandler(nullptr).SendWorldText(Acore::StringFormat("|cffFF2020[Hardcore]|r {} was revived ({}).",
                entry->name, reason.empty() ? "no reason given" : reason));

        return "";
    }

private:
    void RecordDeath(ObjectGuid::LowType guid, PendingDeath const& death)
    {
        Entry* entry = Find(guid);
        if (!entry)
            return;

        std::string killer = death.killerName;
        CharacterDatabase.EscapeString(killer);
        CharacterDatabase.Execute("UPDATE `world_randomizer_hardcore` SET `status` = 1, `level` = {}, `died_at` = {}, "
            "`death_map` = {}, `death_zone` = {}, `killer_type` = {}, `killer_entry` = {}, `killer_name` = '{}', "
            "`revive_pending` = 0 WHERE `guid` = {}", death.level, GameTime::GetGameTime().count(), death.mapId,
            death.zoneId, death.killerType, death.killerEntry, killer, guid);
        entry->revivePending = false;

        if (!sWorldRandomizer->Config().hardcoreAnnounce)
            return;

        std::string cause = death.killerName.empty() ? "the world" : death.killerName;
        ChatHandler(nullptr).SendWorldText(Acore::StringFormat("|cffFF2020[Hardcore]|r {} (level {}) has fallen to {}.",
            entry->name, death.level, cause));
    }

    void SoftDelete(ObjectGuid::LowType guid)
    {
        Entry* entry = Find(guid);
        if (!entry || entry->status != HARDCORE_DEAD || entry->softDeleted)
            return;

        // The core's own unlink: data stays, name / account move to deleteInfos_*
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_DELETE_INFO);
        stmt->SetData(0, guid);
        CharacterDatabase.Execute(stmt);

        sCharacterCache->DeleteCharacterCacheEntry(ObjectGuid::Create<HighGuid::Player>(guid), entry->name);
        sWorld->UpdateRealmCharCount(entry->account);

        entry->softDeleted = true;
        CharacterDatabase.Execute("UPDATE `world_randomizer_hardcore` SET `soft_deleted` = 1 WHERE `guid` = {}", guid);
        LOG_INFO("module", "mod-world-randomizer: hardcore character {} ({}) soft deleted.", entry->name, guid);
    }

    std::unordered_map<ObjectGuid::LowType, Entry> _entries;
    struct Taint
    {
        std::string reason;
        bool allowed = false; // admin override
    };
    std::unordered_map<ObjectGuid::LowType, Taint> _taints;
    std::unordered_map<ObjectGuid::LowType, PendingDeath> _pending;
    std::unordered_map<ObjectGuid::LowType, uint32> _unlinkQueue;
};

#define sHardcore HardcoreMgr::instance()

class HardcoreWorldScript : public WorldScript
{
public:
    HardcoreWorldScript() : WorldScript("HardcoreWorldScript",
        { WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED, WORLDHOOK_ON_UPDATE }) { }

    void OnBeforeWorldInitialized() override
    {
        if (sHardcore->Enabled())
            sHardcore->Load();
    }

    void OnUpdate(uint32 diff) override
    {
        if (sHardcore->Enabled())
            sHardcore->Update(diff);
    }
};

class HardcorePlayerScript : public PlayerScript
{
public:
    HardcorePlayerScript() : PlayerScript("HardcorePlayerScript", {
        PLAYERHOOK_ON_FIRST_LOGIN,
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_PLAYER_JUST_DIED,
        PLAYERHOOK_CAN_RESURRECT,
        PLAYERHOOK_CAN_INIT_TRADE,
        PLAYERHOOK_ON_LEVEL_CHANGED
    }) { }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (sWorldRandomizer->Config().hardcoreMode == 1 && player->GetLevel() > 1)
            sHardcore->MarkIneligible(player->GetGUID().GetCounter(), Acore::StringFormat("reached level {}",
                player->GetLevel()));
    }

    bool OnPlayerCanInitTrade(Player* player, Player* target) override
    {
        if (sWorldRandomizer->Config().hardcoreMode == 1)
        {
            sHardcore->MarkIneligible(player->GetGUID().GetCounter(), Acore::StringFormat("traded with {}",
                target->GetName()));
            sHardcore->MarkIneligible(target->GetGUID().GetCounter(), Acore::StringFormat("traded with {}",
                player->GetName()));
        }
        return true;
    }

    void OnPlayerFirstLogin(Player* player) override
    {
        if (sWorldRandomizer->Config().hardcoreMode == 2)
            sHardcore->Register(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (sHardcore->Enabled())
            sHardcore->OnLogin(player);
    }

    void OnPlayerJustDied(Player* player) override
    {
        if (sHardcore->Enabled())
            sHardcore->OnDeath(player);
    }

    bool OnPlayerCanResurrect(Player* player) override
    {
        return !sHardcore->Enabled() || !sHardcore->IsDead(player);
    }
};

class HardcoreMailScript : public MailScript
{
public:
    HardcoreMailScript() : MailScript("HardcoreMailScript", { MAILHOOK_ON_BEFORE_MAIL_DRAFT_SEND_MAIL_TO }) { }

    void OnBeforeMailDraftSendMailTo(MailDraft* /*mailDraft*/, MailReceiver const& receiver,
        MailSender const& /*sender*/, MailCheckMask& /*checked*/, uint32& /*deliverDelay*/,
        uint32& /*customExpiration*/, bool& /*deleteMailItemsFromDB*/, bool& /*sendMail*/) override
    {
        if (sWorldRandomizer->Config().hardcoreMode == 1)
            sHardcore->MarkIneligible(receiver.GetPlayerGUIDLow(), "received mail");
    }
};

class HardcoreUnitScript : public UnitScript
{
public:
    HardcoreUnitScript() : UnitScript("HardcoreUnitScript", true, { UNITHOOK_ON_UNIT_DEATH }) { }

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        if (sHardcore->Enabled())
            if (Player* player = unit->ToPlayer())
                sHardcore->OnKilledBy(player, killer);
    }
};

class HardcoreCommandScript : public CommandScript
{
public:
    HardcoreCommandScript() : CommandScript("HardcoreCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable hardcoreTable =
        {
            { "",       HandleStatusCommand, SEC_PLAYER,     Console::No },
            { "on",     HandleOnCommand,     SEC_PLAYER,     Console::No },
            { "fallen", HandleFallenCommand, SEC_PLAYER,     Console::Yes },
            { "restore", HandleRestoreCommand, SEC_GAMEMASTER, Console::Yes },
            { "revive",  HandleRestoreCommand, SEC_GAMEMASTER, Console::Yes },
            { "allow",   HandleAllowCommand,   SEC_ADMINISTRATOR, Console::Yes },
            { "block",   HandleBlockCommand,   SEC_ADMINISTRATOR, Console::Yes },
            { "check",   HandleCheckCommand,   SEC_GAMEMASTER, Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "hardcore", hardcoreTable }
        };

        return commandTable;
    }

    static bool HandleStatusCommand(ChatHandler* handler)
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        if (!config.hardcoreMode)
        {
            handler->SendSysMessage("Hardcore is off on this server.");
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();
        if (sHardcore->IsHardcore(player))
            handler->SendSysMessage("You are hardcore: one life.");
        else if (config.hardcoreMode == 1)
        {
            std::vector<std::string> problems = sHardcore->CheckFreshStart(player);
            if (problems.empty())
                handler->SendSysMessage("You are not hardcore. Type .hardcore on confirm to go hardcore (it can't be "
                    "undone).");
            else
            {
                handler->SendSysMessage("You are not hardcore, and this character can't go hardcore:");
                for (std::string const& problem : problems)
                    handler->PSendSysMessage("  - {}", problem);
            }
        }
        else
            handler->SendSysMessage("You are not hardcore.");

        return true;
    }

    static bool HandleOnCommand(ChatHandler* handler, Optional<std::string> confirm)
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        Player* player = handler->GetSession()->GetPlayer();

        if (config.hardcoreMode != 1)
        {
            handler->SendErrorMessage(config.hardcoreMode == 2 ? "Every new character is hardcore on this server." :
                "Hardcore is off on this server.", false);
            return false;
        }

        if (sHardcore->IsHardcore(player))
        {
            handler->SendErrorMessage("You are already hardcore.");
            return false;
        }

        // Only a fresh character: level 1, starting gear and money, never mailed or traded
        std::vector<std::string> problems = sHardcore->CheckFreshStart(player);
        if (!problems.empty())
        {
            handler->SendSysMessage("A hardcore run can only start on a fresh level 1 character with just its starting "
                "gear, and no mail or trades ever. Not this one:");
            for (std::string const& problem : problems)
                handler->PSendSysMessage("  - {}", problem);
            return false;
        }

        if (!confirm || Lower(*confirm) != "confirm")
        {
            handler->SendSysMessage("Hardcore means one life: if you die, this character can't be played again. "
                "Type .hardcore on confirm to go ahead.");
            return true;
        }

        sHardcore->Register(player);
        return true;
    }

    static bool HandleFallenCommand(ChatHandler* handler)
    {
        QueryResult result = CharacterDatabase.Query("SELECT `name`, `level`, `race`, `class`, `killer_name`, "
            "`died_at` FROM `world_randomizer_hardcore` WHERE `status` = 1 ORDER BY `died_at` DESC LIMIT 10");
        if (!result)
        {
            handler->SendSysMessage("No hardcore character has fallen yet.");
            return true;
        }

        handler->SendSysMessage("The fallen (latest first):");
        do
        {
            Field* fields = result->Fetch();
            std::string killer = fields[4].Get<std::string>();
            handler->PSendSysMessage("  {} - level {}, killed by {}", fields[0].Get<std::string>(),
                fields[1].Get<uint8>(), killer.empty() ? "the world" : killer);
        } while (result->NextRow());

        return true;
    }

    static ObjectGuid::LowType ResolveCharacter(ChatHandler* handler, std::string const& nameOrGuid)
    {
        if (Optional<uint32> guid = Acore::StringTo<uint32>(nameOrGuid))
            if (sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(*guid)))
                return *guid;

        std::string name = nameOrGuid;
        if (normalizePlayerName(name))
            if (ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(name))
                return guid.GetCounter();

        handler->SendErrorMessage(Acore::StringFormat("No character called {}.", nameOrGuid), false);
        return 0;
    }

    static std::string GmName(ChatHandler* handler)
    {
        return handler->GetSession() ? handler->GetSession()->GetPlayerName() : "console";
    }

    // .hardcore allow <name or GUID>: an admin lets a ruled out character go hardcore anyway
    static bool HandleAllowCommand(ChatHandler* handler, std::string nameOrGuid)
    {
        ObjectGuid::LowType guid = ResolveCharacter(handler, nameOrGuid);
        if (!guid)
            return false;

        std::string before = sHardcore->EligibilityNote(guid);
        sHardcore->SetAllowed(guid, true, GmName(handler));
        handler->PSendSysMessage("{} (GUID {}) may now go hardcore with .hardcore on confirm, whatever the checks "
            "say.{}", nameOrGuid, guid, before.empty() ? "" : " Before: " + before + ".");
        return true;
    }

    // .hardcore block <name or GUID>: take an override back (or rule a character out by hand)
    static bool HandleBlockCommand(ChatHandler* handler, std::string nameOrGuid)
    {
        ObjectGuid::LowType guid = ResolveCharacter(handler, nameOrGuid);
        if (!guid)
            return false;

        sHardcore->SetAllowed(guid, false, GmName(handler));
        handler->PSendSysMessage("{} (GUID {}) can't go hardcore: {}", nameOrGuid, guid,
            sHardcore->EligibilityNote(guid));
        return true;
    }

    // .hardcore check <name or GUID>: what is saved about a character's eligibility
    static bool HandleCheckCommand(ChatHandler* handler, std::string nameOrGuid)
    {
        ObjectGuid::LowType guid = ResolveCharacter(handler, nameOrGuid);
        if (!guid)
            return false;

        std::string note = sHardcore->EligibilityNote(guid);
        handler->PSendSysMessage("{} (GUID {}): {}", nameOrGuid, guid, note.empty() ?
            "nothing saved; the checks run when it tries .hardcore on" : note);
        return true;
    }

    // .hardcore restore <name or GUID> [reason]: undo a death (a bug, a disconnect...)
    static bool HandleRestoreCommand(ChatHandler* handler, std::string nameOrGuid, Tail reason)
    {
        std::vector<ObjectGuid::LowType> matches = sHardcore->FindMatches(nameOrGuid);
        if (matches.empty())
        {
            std::vector<ObjectGuid::LowType> alive = sHardcore->FindMatches(nameOrGuid, true);
            handler->SendErrorMessage(alive.empty() ?
                Acore::StringFormat("No hardcore character called {}.", nameOrGuid) :
                Acore::StringFormat("{} is not dead: {}", nameOrGuid, sHardcore->Describe(alive.front())), false);
            return false;
        }

        if (matches.size() > 1)
        {
            handler->SendSysMessage("Found multiple characters with this name:");
            for (ObjectGuid::LowType guid : matches)
                handler->PSendSysMessage("- {}", sHardcore->Describe(guid));
            handler->SendSysMessage("Run it again with the GUID: .hardcore restore <GUID> [reason]");
            return true;
        }

        ObjectGuid::LowType guid = matches.front();
        std::string description = sHardcore->Describe(guid);
        std::string by = handler->GetSession() ? handler->GetSession()->GetPlayerName() : "console";
        std::string error = sHardcore->Revive(guid, std::string(reason), by, true);
        if (!error.empty())
        {
            handler->SendErrorMessage(error, false);
            return false;
        }

        handler->PSendSysMessage("Restored: {}", description);
        handler->SendSysMessage("It is back on its account and comes back alive on its next login.");
        return true;
    }
};

uint8 RandomizerHardcoreState(Player const* player)
{
    if (!player || !sWorldRandomizer->Config().hardcoreMode || !sHardcore->IsHardcore(player))
        return 0;
    return sHardcore->IsDead(player) ? 2 : 1;
}

// "HARDCORE": the addon's "Go hardcore" button, the same as .hardcore on confirm. Answers "X\t<why not>" when it
// can't; the addon asks for the Group tab (START) again either way, which shows the new state.
bool HandleHardcoreAddonMessage(Player* player, std::string const& body)
{
    if (body != "HARDCORE")
        return false;

    auto reply = [player](std::string text)
    {
        std::replace(text.begin(), text.end(), '\t', ' ');
        text = std::string(RANDOMIZER_ADDON_PREFIX) + "\tX\t" + text;
        if (text.size() > 254)
            text.resize(254);
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, text);
        player->SendDirectMessage(&data);
    };

    RandomizerConfig const& config = sWorldRandomizer->Config();
    if (config.hardcoreMode != 1)
    {
        reply(config.hardcoreMode == 2 ? "Every new character is hardcore on this server." :
            "Hardcore is off on this server.");
        return true;
    }
    if (sHardcore->IsHardcore(player))
    {
        reply("You are already hardcore.");
        return true;
    }

    std::vector<std::string> problems = sHardcore->CheckFreshStart(player);
    if (!problems.empty())
    {
        std::string why;
        for (std::string const& problem : problems)
            why += (why.empty() ? "" : "; ") + problem;
        reply("This character can't go hardcore: " + why + ".");
        return true;
    }

    sHardcore->Register(player);
    return true;
}

void AddWorldRandomizerHardcoreScripts()
{
    new HardcoreWorldScript();
    new HardcorePlayerScript();
    new HardcoreMailScript();
    new HardcoreUnitScript();
    new HardcoreCommandScript();
}
