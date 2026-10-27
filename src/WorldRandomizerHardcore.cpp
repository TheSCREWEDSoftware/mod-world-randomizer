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
 * A death caused by a bug can be undone with .hardcore revive <name> [reason]: the character is linked back to its
 * account (if it was soft deleted), comes back alive on its next login, and the revive is logged with who did it
 * and why. Restoring with the core's .character deleted restore also counts as a revive on the next login.
 */

#include "WorldRandomizer.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "Timer.h"
#include "World.h"
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

        //                                                       0       1          2       3       4        5
        QueryResult result = CharacterDatabase.Query("SELECT `guid`, `account`, `name`, `race`, `class`, `gender`, "
            //   6        7         8               9                 10         11
            "`level`, `status`, `soft_deleted`, `revive_pending`, `revives`, `died_at` FROM `world_randomizer_hardcore`");
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
            "`class`, `gender`, `level`, `status`, `started_at`, `seed`) VALUES ({}, {}, '{}', {}, {}, {}, {}, 0, {}, {})",
            player->GetGUID().GetCounter(), entry.account, entry.name, entry.race, entry.cls, entry.gender, entry.level,
            GameTime::GetGameTime().count(), sWorldRandomizer->GetSeed());

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
        PLAYERHOOK_CAN_RESURRECT
    }) { }

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
            { "revive",  HandleRestoreCommand, SEC_GAMEMASTER, Console::Yes }
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
        else if (config.hardcoreMode == 1 && player->GetLevel() <= config.hardcoreOptInMaxLevel)
            handler->PSendSysMessage("You are not hardcore. Type .hardcore on confirm to go hardcore (only up to "
                "level {}, and it can't be undone).", config.hardcoreOptInMaxLevel);
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

        if (player->GetLevel() > config.hardcoreOptInMaxLevel)
        {
            handler->SendErrorMessage("Hardcore can only be chosen up to level {}.", config.hardcoreOptInMaxLevel);
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

void AddWorldRandomizerHardcoreScripts()
{
    new HardcoreWorldScript();
    new HardcorePlayerScript();
    new HardcoreUnitScript();
    new HardcoreCommandScript();
}
