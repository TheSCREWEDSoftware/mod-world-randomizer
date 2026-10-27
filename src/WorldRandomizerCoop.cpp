/*
 * mod-world-randomizer: Co-op starts.
 *
 * One player types .coop create and gets a short code; friends type .coop join <code> and start in the same place
 * (a random bracket 1 zone when the randomizer runs), with their hearthstone there and, if the leader is online,
 * in the leader's group. Up to Randomizer.Coop.MaxSize players per code, only while at most Randomizer.Coop.MaxLevel.
 */

#include "WorldRandomizer.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "CommandScript.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
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

        LOG_INFO("module", "mod-world-randomizer: {} co-op parties loaded.", _parties.size());
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

    std::string Create(Player* player)
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
        if (config.enable && sWorldRandomizer->PickStartLocation(player, location))
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

        WorldLocation home(party.mapId, party.x, party.y, party.z, party.o);
        player->SetHomebind(home, party.zoneId);
        player->TeleportTo(party.mapId, party.x, party.y, party.z, party.o);
    }

    std::unordered_map<std::string, Party> _parties;
    std::unordered_map<ObjectGuid::LowType, std::string> _memberOf;
};

#define sCoop CoopMgr::instance()

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
            { "join",   HandleJoinCommand,   SEC_PLAYER, Console::No }
        };

        static ChatCommandTable commandTable =
        {
            { "coop", coopTable }
        };

        return commandTable;
    }

    static bool CheckAllowed(ChatHandler* handler, Player* player)
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        if (!sCoop->Enabled())
        {
            handler->SendErrorMessage("Co-op starts are off on this server.");
            return false;
        }

        if (sCoop->CodeOf(player->GetGUID().GetCounter()))
        {
            handler->SendErrorMessage("You are already in a co-op party. Type .coop to see it.");
            return false;
        }

        if (player->GetLevel() > config.coopMaxLevel)
        {
            handler->SendErrorMessage("Co-op parties can only be made or joined up to level {}.", config.coopMaxLevel);
            return false;
        }

        return true;
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
        if (!code)
        {
            handler->PSendSysMessage("You are not in a co-op party. Up to level {}: .coop create makes one and gives "
                "you a code; friends type .coop join <code> (up to {} players).", config.coopMaxLevel,
                config.coopMaxSize);
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

    static bool HandleCreateCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!CheckAllowed(handler, player))
            return false;

        std::string code = sCoop->Create(player);
        RandomizerConfig const& config = sWorldRandomizer->Config();
        handler->PSendSysMessage("|cffFF8000[Co-op]|r Your party code is |cffFFFF00{}|r. Share it: friends type "
            "|cffFFFF00.coop join {}|r to start here with you (up to {} players, level {} or lower).", code, code,
            config.coopMaxSize, config.coopMaxLevel);
        return true;
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

void AddWorldRandomizerCoopScripts()
{
    new CoopWorldScript();
    new CoopCommandScript();
}
