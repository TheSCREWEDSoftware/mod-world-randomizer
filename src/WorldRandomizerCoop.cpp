/*
 * mod-world-randomizer: Co-op starts.
 *
 * One player types .coop create and gets a short code; friends type .coop join <code> and start in the same place
 * (a random bracket 1 zone when the randomizer runs), with their hearthstone there and, if the leader is online,
 * in the leader's group. Up to Randomizer.Coop.MaxSize players per code, only while at most Randomizer.Coop.MaxLevel.
 * .coop alone [zone] (or the RandomizerAchievements addon's Group tab, which does all of this with buttons) starts a
 * character alone, for good: it can't make or join a party later. A zone given to create or alone must be one of the
 * player's bracket 1 start zones (the web roller's picker shows them).
 */

#include "WorldRandomizer.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "CommandScript.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldRandomizerAchievements.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    // No 0/O or 1/I, so codes are easy to read out to friends
    constexpr char const CODE_ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    constexpr uint8 CODE_LENGTH = 6;

    std::string Upper(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::toupper(c); });
        return text;
    }
}

class CoopMgr
{
public:
    struct Party
    {
        ObjectGuid::LowType leader = 0;
        uint32 mapId = 0;
        uint32 zoneId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float o = 0.0f;
        uint8 team = TEAM_NEUTRAL;
        std::vector<ObjectGuid::LowType> members; // leader included
    };

    static CoopMgr* instance()
    {
        static CoopMgr instance;
        return &instance;
    }

    bool Enabled() const { return sWorldRandomizer->Config().coopEnable; }

    void Load()
    {
        _parties.clear();
        _memberOf.clear();
        _alone.clear();

        if (QueryResult result = CharacterDatabase.Query("SELECT `guid` FROM `world_randomizer_start_choice`"))
            do
                _alone.insert((*result)[0].Get<uint32>());
            while (result->NextRow());

        if (QueryResult result = CharacterDatabase.Query("SELECT `code`, `leader`, `map`, `zone`, `x`, `y`, `z`, `o`, "
            "`team` FROM `world_randomizer_coop`"))
        {
            do
            {
                Field* fields = result->Fetch();
                Party party;
                party.leader = fields[1].Get<uint32>();
                party.mapId = fields[2].Get<uint16>();
                party.zoneId = fields[3].Get<uint32>();
                party.x = fields[4].Get<float>();
                party.y = fields[5].Get<float>();
                party.z = fields[6].Get<float>();
                party.o = fields[7].Get<float>();
                party.team = fields[8].Get<uint8>();
                _parties[fields[0].Get<std::string>()] = party;
            } while (result->NextRow());
        }

        if (QueryResult result = CharacterDatabase.Query("SELECT `guid`, `code` FROM `world_randomizer_coop_member` "
            "ORDER BY `joined_at`"))
        {
            do
            {
                ObjectGuid::LowType guid = (*result)[0].Get<uint32>();
                std::string code = (*result)[1].Get<std::string>();
                auto itr = _parties.find(code);
                if (itr == _parties.end())
                    continue;

                itr->second.members.push_back(guid);
                _memberOf[guid] = code;
            } while (result->NextRow());
        }

        LOG_INFO("module", "mod-world-randomizer: {} co-op parties loaded, {} characters started alone.",
            _parties.size(), _alone.size());
    }

    bool StartedAlone(ObjectGuid::LowType guid) const { return _alone.count(guid) != 0; }

    // Why this player can't make or join a party now; empty when they can
    std::string CantStart(Player* player) const
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        if (!Enabled())
            return "Co-op starts are off on this server.";
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        if (CodeOf(guid))
            return "You are already in a co-op party. Type .coop to see it.";
        if (StartedAlone(guid))
            return "This character started alone; that can't be changed.";
        if (player->GetLevel() > config.coopMaxLevel)
            return Acore::StringFormat("Co-op parties can only be made or joined up to level {}.",
                config.coopMaxLevel);
        return "";
    }

    // For good: this character won't make or join a party
    void StartAlone(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        _alone.insert(guid);
        CharacterDatabase.Execute("REPLACE INTO `world_randomizer_start_choice` (`guid`, `chosen_at`) VALUES ({}, {})",
            guid, GameTime::GetGameTime().count());
        WorldRandomizerAchievements::ChallengeStarted(player);
    }

    std::string const* CodeOf(ObjectGuid::LowType guid) const
    {
        auto itr = _memberOf.find(guid);
        return itr != _memberOf.end() ? &itr->second : nullptr;
    }

    Party const* Find(std::string const& code) const
    {
        auto itr = _parties.find(code);
        return itr != _parties.end() ? &itr->second : nullptr;
    }

    std::string Create(Player* player, uint32 startZone)
    {
        std::string code;
        do
        {
            code.clear();
            for (uint8 i = 0; i < CODE_LENGTH; ++i)
                code += CODE_ALPHABET[urand(0, sizeof(CODE_ALPHABET) - 2)];
        } while (_parties.count(code));

        Party party;
        party.leader = player->GetGUID().GetCounter();
        party.team = uint8(player->GetTeamId());

        // Where the party starts: a random bracket 1 zone for the leader's faction, or right here
        StartLocation location;
        RandomizerConfig const& config = sWorldRandomizer->Config();
        if (config.enable && sWorldRandomizer->PickStartLocation(player, location, startZone))
        {
            party.mapId = location.mapId;
            party.zoneId = location.zoneId;
            party.x = location.x;
            party.y = location.y;
            party.z = location.z;
            party.o = player->GetOrientation();
        }
        else
        {
            party.mapId = player->GetMapId();
            party.zoneId = player->GetZoneId();
            party.x = player->GetPositionX();
            party.y = player->GetPositionY();
            party.z = player->GetPositionZ();
            party.o = player->GetOrientation();
        }

        _parties[code] = party;
        CharacterDatabase.Execute("INSERT INTO `world_randomizer_coop` (`code`, `leader`, `map`, `zone`, `x`, `y`, "
            "`z`, `o`, `team`, `created_at`) VALUES ('{}', {}, {}, {}, {}, {}, {}, {}, {}, {})", code, party.leader,
            party.mapId, party.zoneId, party.x, party.y, party.z, party.o, party.team, GameTime::GetGameTime().count());

        AddMember(code, player);
        return code;
    }

    // Returns an error message, or an empty string on success
    std::string Join(std::string const& code, Player* player)
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        auto itr = _parties.find(code);
        if (itr == _parties.end())
            return "No co-op party with that code.";

        Party& party = itr->second;
        if (party.members.size() >= config.coopMaxSize)
            return Acore::StringFormat("That party is full ({} players).", config.coopMaxSize);

        if (config.coopSameFaction && party.team != uint8(player->GetTeamId()))
            return "That party is for the other faction.";

        AddMember(code, player);

        if (!config.coopAutoGroup)
            return "";

        Player* leader = ObjectAccessor::FindPlayerByLowGUID(party.leader);
        if (!leader || leader == player || player->GetGroup())
            return "";

        Group* group = leader->GetGroup();
        if (!group)
        {
            group = new Group();
            if (!group->Create(leader))
            {
                delete group;
                return "";
            }
            sGroupMgr->AddGroup(group);
        }

        if (!group->IsFull())
            group->AddMember(player);

        return "";
    }

private:
    void AddMember(std::string const& code, Player* player)
    {
        Party& party = _parties[code];
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        party.members.push_back(guid);
        _memberOf[guid] = code;

        CharacterDatabase.Execute("REPLACE INTO `world_randomizer_coop_member` (`guid`, `code`, `joined_at`) VALUES "
            "({}, '{}', {})", guid, code, GameTime::GetGameTime().count());
        WorldRandomizerAchievements::ChallengeStarted(player);

        WorldLocation home(party.mapId, party.x, party.y, party.z, party.o);
        RandomizerSetHomebind(player, home, party.zoneId);
        player->TeleportTo(party.mapId, party.x, party.y, party.z, party.o);
    }

    std::unordered_map<std::string, Party> _parties;
    std::unordered_map<ObjectGuid::LowType, std::string> _memberOf;
    std::unordered_set<ObjectGuid::LowType> _alone;
};

#define sCoop CoopMgr::instance()

namespace
{
    // Why the party can't start in this zone (it must be one of the leader's start zones); empty when it can
    std::string BadStartZone(Player* player, uint32 zoneId)
    {
        if (!zoneId)
            return "";
        std::vector<uint32> zones = sWorldRandomizer->StartZones(player);
        if (sWorldRandomizer->Config().enable && std::find(zones.begin(), zones.end(), zoneId) != zones.end())
            return "";
        std::string list;
        for (uint32 id : zones)
            if (RandomizerZone const* zone = sWorldRandomizer->FindZone(id))
                list += Acore::StringFormat("{}{} ({})", list.empty() ? "" : ", ", zone->name, id);
        return Acore::StringFormat("You can't start in zone {}. Your start zones: {}.", zoneId,
            list.empty() ? "none" : list);
    }

    // Start alone, for good, optionally moved to one of your start zones (only while a party could still be made, so
    // it can't be used to travel later); returns an error message, or an empty string on success
    std::string StartAloneIn(Player* player, uint32 zoneId)
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        if (!sCoop->Enabled())
            return "Co-op starts are off on this server.";
        if (sCoop->CodeOf(guid))
            return "You are already in a co-op party. Type .coop to see it.";
        if (sCoop->StartedAlone(guid))
            return "This character already started alone.";

        StartLocation location;
        if (zoneId)
        {
            if (player->GetLevel() > config.coopMaxLevel)
                return Acore::StringFormat("A start zone can only be picked up to level {}.", config.coopMaxLevel);
            std::string error = BadStartZone(player, zoneId);
            if (!error.empty())
                return error;
            if (!sWorldRandomizer->PickStartLocation(player, location, zoneId))
                return "No start point in that zone for your faction.";
        }

        sCoop->StartAlone(player);
        ChatHandler chat(player->GetSession());
        if (!zoneId)
        {
            chat.SendSysMessage("|cffFF8000[Co-op]|r This character starts alone.");
            return "";
        }

        WorldLocation home(location.mapId, location.x, location.y, location.z, player->GetOrientation());
        RandomizerSetHomebind(player, home, location.zoneId);
        player->TeleportTo(location.mapId, location.x, location.y, location.z, player->GetOrientation());
        RandomizerZone const* zone = sWorldRandomizer->FindZone(zoneId);
        chat.PSendSysMessage("|cffFF8000[Co-op]|r This character starts alone in {}. Your hearthstone is set there.",
            zone ? zone->name : std::to_string(zoneId));
        return "";
    }

    void AnnounceCreated(Player* player, std::string const& code)
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        ChatHandler(player->GetSession()).PSendSysMessage("|cffFF8000[Co-op]|r Your party code is |cffFFFF00{}|r. "
            "Share it: friends type |cffFFFF00.coop join {}|r (or use the Group tab of the RandomizerAchievements "
            "addon) to start here with you (up to {} players, level {} or lower).", code, code, config.coopMaxSize,
            config.coopMaxLevel);
    }
}

class CoopWorldScript : public WorldScript
{
public:
    CoopWorldScript() : WorldScript("CoopWorldScript", { WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED }) { }

    void OnBeforeWorldInitialized() override
    {
        if (sCoop->Enabled())
            sCoop->Load();
    }
};

class CoopCommandScript : public CommandScript
{
public:
    CoopCommandScript() : CommandScript("CoopCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable coopTable =
        {
            { "",       HandleInfoCommand,   SEC_PLAYER, Console::No },
            { "create", HandleCreateCommand, SEC_PLAYER, Console::No },
            { "join",   HandleJoinCommand,   SEC_PLAYER, Console::No },
            { "alone",  HandleAloneCommand,  SEC_PLAYER, Console::No }
        };

        static ChatCommandTable commandTable =
        {
            { "coop", coopTable }
        };

        return commandTable;
    }

    static bool CheckAllowed(ChatHandler* handler, Player* player)
    {
        std::string error = sCoop->CantStart(player);
        if (error.empty())
            return true;
        handler->SendErrorMessage(error, false);
        return false;
    }

    static bool HandleInfoCommand(ChatHandler* handler)
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        if (!sCoop->Enabled())
        {
            handler->SendSysMessage("Co-op starts are off on this server.");
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();
        std::string const* code = sCoop->CodeOf(player->GetGUID().GetCounter());
        if (!code && sCoop->StartedAlone(player->GetGUID().GetCounter()))
        {
            handler->SendSysMessage("This character started alone: it can't make or join a co-op party.");
            return true;
        }
        if (!code)
        {
            handler->PSendSysMessage("You are not in a co-op party. Up to level {}: .coop create makes one and gives "
                "you a code; friends type .coop join <code> (up to {} players). .coop alone [zone id] starts alone "
                "for good.", config.coopMaxLevel, config.coopMaxSize);
            return true;
        }

        CoopMgr::Party const* party = sCoop->Find(*code);
        handler->PSendSysMessage("Co-op code |cffFFFF00{}|r - {}/{} players:", *code, party->members.size(),
            config.coopMaxSize);
        for (ObjectGuid::LowType guid : party->members)
        {
            std::string name;
            if (CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByGuid(
                ObjectGuid::Create<HighGuid::Player>(guid)))
                name = entry->Name;
            handler->PSendSysMessage("  {}{}", name.empty() ? Acore::StringFormat("GUID {}", guid) : name,
                guid == party->leader ? " (leader)" : "");
        }

        return true;
    }

    // .coop create [zone id]: the zone must be one of the leader's start zones (the web page's "Starts in" gives it)
    static bool HandleCreateCommand(ChatHandler* handler, Optional<uint32> zoneId)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!CheckAllowed(handler, player))
            return false;

        std::string error = BadStartZone(player, zoneId ? *zoneId : 0);
        if (!error.empty())
        {
            handler->SendErrorMessage(error, false);
            return false;
        }

        AnnounceCreated(player, sCoop->Create(player, zoneId ? *zoneId : 0));
        return true;
    }

    // .coop alone [zone id]: start alone for good (no party later), in one of your start zones if given
    static bool HandleAloneCommand(ChatHandler* handler, Optional<uint32> zoneId)
    {
        std::string error = StartAloneIn(handler->GetSession()->GetPlayer(), zoneId ? *zoneId : 0);
        if (error.empty())
            return true;
        handler->SendErrorMessage(error, false);
        return false;
    }

    static bool HandleJoinCommand(ChatHandler* handler, std::string code)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!CheckAllowed(handler, player))
            return false;

        code = Upper(code);
        std::string error = sCoop->Join(code, player);
        if (!error.empty())
        {
            handler->SendErrorMessage(error, false);
            return false;
        }

        handler->PSendSysMessage("|cffFF8000[Co-op]|r You joined party {}. Your hearthstone is set here.", code);
        return true;
    }
};

namespace
{
    void SendAddon(Player* player, std::string const& body)
    {
        std::string text = std::string(RANDOMIZER_ADDON_PREFIX) + "\t" + body;
        if (text.size() > 254)
            text.resize(254);

        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, text);
        player->SendDirectMessage(&data);
    }

    std::string NoTabs(std::string text)
    {
        std::replace(text.begin(), text.end(), '\t', ' ');
        return text;
    }

    // "G\t<state>\t<code>\t<members>\t<max size>\t<max level>\t<can choose 0/1>\t<why not>",
    // state 0 co-op off, 1 not chosen yet, 2 started alone, 3 in a party;
    // then "Z\t<zone id>\t<name>" per start zone the party could pick, and "E\tSTART"
    void SendStart(Player* player)
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        std::string const* code = sCoop->Enabled() ? sCoop->CodeOf(guid) : nullptr;
        uint32 state = !sCoop->Enabled() ? 0 : code ? 3 : sCoop->StartedAlone(guid) ? 2 : 1;
        std::size_t members = code ? sCoop->Find(*code)->members.size() : 0;
        std::string whyNot = state == 1 ? sCoop->CantStart(player) : "";

        SendAddon(player, Acore::StringFormat("G\t{}\t{}\t{}\t{}\t{}\t{}\t{}", state, code ? *code : "", members,
            config.coopMaxSize, config.coopMaxLevel, state == 1 && whyNot.empty() ? 1 : 0, NoTabs(whyNot)));

        if (state == 1 && whyNot.empty() && config.enable)
            for (uint32 id : sWorldRandomizer->StartZones(player))
                if (RandomizerZone const* zone = sWorldRandomizer->FindZone(id))
                    SendAddon(player, Acore::StringFormat("Z\t{}\t{}", id, NoTabs(zone->name)));

        SendAddon(player, "E\tSTART");
    }

    // One "M" line per member of your co-op party and of your group:
    // "M\t<name>\t<online 0/1>\t<level>\t<race>\t<class>\t<zone>\t<flags>\t<roll>"
    // flags: 1 co-op leader, 2 in your co-op party, 4 in your group, 8 you; then "E\tPARTY\t<count>\t<code>"
    void SendParty(Player* player)
    {
        std::vector<ObjectGuid::LowType> guids;
        std::unordered_map<ObjectGuid::LowType, uint32> flags;
        auto add = [&](ObjectGuid::LowType guid, uint32 flag)
        {
            if (!flags.count(guid))
                guids.push_back(guid);
            flags[guid] |= flag;
        };

        add(player->GetGUID().GetCounter(), 8);
        std::string const* code = sCoop->Enabled() ? sCoop->CodeOf(player->GetGUID().GetCounter()) : nullptr;
        CoopMgr::Party const* party = code ? sCoop->Find(*code) : nullptr;
        if (party)
            for (ObjectGuid::LowType guid : party->members)
                add(guid, 2 | (guid == party->leader ? 1 : 0));
        if (Group* group = player->GetGroup())
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
                if (Player* member = itr->GetSource())
                    add(member->GetGUID().GetCounter(), 4);

        LocaleConstant locale = player->GetSession()->GetSessionDbcLocale();
        for (ObjectGuid::LowType guid : guids)
        {
            ObjectGuid fullGuid = ObjectGuid::Create<HighGuid::Player>(guid);
            std::string name, zone, roll;
            uint32 level = 0, race = 0, cls = 0;
            bool online = false;
            if (Player* member = ObjectAccessor::FindConnectedPlayer(fullGuid))
            {
                online = true;
                name = member->GetName();
                level = member->GetLevel();
                race = member->getRace();
                cls = member->getClass();
                if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(member->GetZoneId()))
                    zone = area->area_name[locale] && *area->area_name[locale] ? area->area_name[locale] :
                        area->area_name[0];
                roll = RandomizerRollText(member);
            }
            else if (CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByGuid(fullGuid))
            {
                name = entry->Name;
                level = entry->Level;
                race = entry->Race;
                cls = entry->Class;
            }
            else
                continue;

            SendAddon(player, Acore::StringFormat("M\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", name, online ? 1 : 0, level,
                race, cls, NoTabs(zone), flags[guid], NoTabs(roll)));
        }
        SendAddon(player, Acore::StringFormat("E\tPARTY\t{}\t{}", guids.size(), code ? *code : ""));
    }
}

// Addon requests (prefix RANDACH) of the Group tab, answered whether achievements are on or not:
//   "START"                  how this character starts                  -> G, Z..., E START
//   "STARTSOLO\t<zone>"      start alone, for good (zone 0: stay here)  -> G..., or X\t<error>
//   "STARTCREATE\t<zone>"    make a co-op party (zone 0: a random one)  -> G..., or X\t<error>
//   "STARTJOIN\t<code>"      join one                                   -> G..., or X\t<error>
//   "PARTY"                  your co-op party and group                 -> M..., E PARTY
bool HandleCoopAddonMessage(Player* player, std::string const& body)
{
    if (body == "START")
    {
        SendStart(player);
        return true;
    }

    if (body == "PARTY")
    {
        SendParty(player);
        return true;
    }

    bool solo = body == "STARTSOLO" || body.compare(0, 10, "STARTSOLO\t") == 0;
    bool create = body.compare(0, 12, "STARTCREATE\t") == 0;
    bool join = body.compare(0, 10, "STARTJOIN\t") == 0;
    if (!solo && !create && !join)
        return false;

    ChatHandler chat(player->GetSession());
    std::string error;
    if (solo)
        error = StartAloneIn(player, body.size() > 10 ? uint32(std::max(0, std::atoi(body.c_str() + 10))) : 0);
    else
        error = sCoop->CantStart(player);

    if (!solo && error.empty() && create)
    {
        uint32 zoneId = uint32(std::max(0, std::atoi(body.c_str() + 12)));
        error = BadStartZone(player, zoneId);
        if (error.empty())
            AnnounceCreated(player, sCoop->Create(player, zoneId));
    }
    else if (!solo && error.empty() && join)
    {
        std::string code = Upper(body.substr(10));
        code.erase(std::remove_if(code.begin(), code.end(), [](unsigned char c) { return std::isspace(c); }),
            code.end());
        error = sCoop->Join(code, player);
        if (error.empty())
            chat.PSendSysMessage("|cffFF8000[Co-op]|r You joined party {}. Your hearthstone is set here.", code);
    }

    if (!error.empty())
        SendAddon(player, "X\t" + NoTabs(error));
    SendStart(player);
    return true;
}

void AddWorldRandomizerCoopScripts()
{
    new CoopWorldScript();
    new CoopCommandScript();
}
