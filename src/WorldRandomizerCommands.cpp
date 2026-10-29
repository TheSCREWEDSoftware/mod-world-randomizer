/*
 * mod-world-randomizer: commands.
 *
 *   .randomizer info          seed, stats era, and your zone's old and new bracket
 *   .randomizer zones         every zone and its bracket
 *   .randomizer reroll [seed] store a new seed (random if none given); takes effect after a restart
 *   .randomizer start         teleport to a random bracket 1 start point for your faction
 *   .randomizer quests [id]   quest levels now and before the shift: one quest, the selected NPC's, or your zone's
 *   .randomizer roll [code] [n]  tie this character to a roll from the web roller (WorldRandomizerRoll.cpp)
 *   .randomizer roll clear <name> (GM) untie it
 */

#include "WorldRandomizer.h"

#include "Chat.h"
#include "CommandScript.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "Random.h"

using namespace Acore::ChatCommands;

bool HandleRandomizerRollCommand(ChatHandler* handler, Optional<std::string> code, Optional<uint32> number);
bool HandleRandomizerRollClearCommand(ChatHandler* handler, PlayerIdentifier target);

namespace
{
    char const* EraName(StatEra era)
    {
        switch (era)
        {
            case StatEra::Vanilla:
                return "Vanilla (vMaNGOS)";
            case StatEra::TBC:
                return "TBC (cMaNGOS-TBC)";
            case StatEra::Wrath:
                return "Wrath (AzerothCore)";
            default:
                return "unchanged";
        }
    }

    std::string BracketText(uint8 index)
    {
        auto const& brackets = sWorldRandomizer->Config().brackets;
        if (index >= brackets.size())
            return "?";

        return Acore::StringFormat("B{} ({}-{})", index + 1, brackets[index].levelMin, brackets[index].levelMax);
    }

    std::string MoneyText(int32 copper)
    {
        if (copper <= 0)
            return "none";

        std::string text;
        if (copper >= 10000)
            text += Acore::StringFormat("{}g ", copper / 10000);
        if (copper % 10000 >= 100)
            text += Acore::StringFormat("{}s ", (copper % 10000) / 100);
        if (copper % 100 || text.empty())
            text += Acore::StringFormat("{}c ", copper % 100);
        text.pop_back();
        return text;
    }

    void QuestLine(ChatHandler* handler, Player* player, Quest const* quest)
    {
        bool canTake = player && player->SatisfyQuestLevel(quest, false);
        std::string level = quest->GetQuestLevel() < 0 ? "your level" : std::to_string(quest->GetQuestLevel());
        std::string head = Acore::StringFormat("|cff{}[{}] {}|r: level {}, needs {}, money {}",
            canTake ? "4CFF00" : "FF6060", quest->GetQuestId(), quest->GetTitle(), level, quest->GetMinLevel(),
            MoneyText(quest->GetRewOrReqMoney(0)));

        if (QuestOriginal const* original = sWorldRandomizer->GetQuestOriginal(quest->GetQuestId()))
            handler->PSendSysMessage("{}  (was level {}, needs {}, money {})", head,
                original->level < 0 ? "your level" : std::to_string(original->level), original->minLevel,
                MoneyText(original->money));
        else
            handler->PSendSysMessage("{}  (not shifted)", head);
    }
}

class WorldRandomizerCommandScript : public CommandScript
{
public:
    WorldRandomizerCommandScript() : CommandScript("WorldRandomizerCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable rollTable =
        {
            { "clear", HandleRandomizerRollClearCommand, SEC_GAMEMASTER, Console::Yes },
            { "",      HandleRandomizerRollCommand,      SEC_PLAYER,     Console::No }
        };

        static ChatCommandTable randomizerTable =
        {
            { "roll",   rollTable },
            { "info",   HandleInfoCommand,   SEC_PLAYER,        Console::No },
            { "zones",  HandleZonesCommand,  SEC_GAMEMASTER,    Console::Yes },
            { "reroll", HandleRerollCommand, SEC_ADMINISTRATOR, Console::Yes },
            { "start",  HandleStartCommand,  SEC_GAMEMASTER,    Console::No },
            { "quests", HandleQuestsCommand, SEC_GAMEMASTER,    Console::No }
        };

        static ChatCommandTable commandTable =
        {
            { "randomizer", randomizerTable }
        };

        return commandTable;
    }

    static bool HandleInfoCommand(ChatHandler* handler)
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        handler->PSendSysMessage("World Randomizer: {}, seed {}, {} brackets, stats {}, XP x{:.2f}",
            config.enable ? "on" : "off", sWorldRandomizer->GetSeed(), config.brackets.size(), EraName(config.era),
            sWorldRandomizer->GetXPRate());

        if (!config.enable)
            return true;

        Player* player = handler->GetSession()->GetPlayer();
        if (RandomizerZone const* zone = sWorldRandomizer->FindZone(player->GetZoneId()))
            handler->PSendSysMessage("{}: was {}-{} ({}), now {}", zone->name, zone->levelMin, zone->levelMax,
                BracketText(zone->homeBracket), BracketText(zone->bracket));
        else
            handler->SendSysMessage("This zone is not part of the shuffle.");

        return true;
    }

    static bool HandleZonesCommand(ChatHandler* handler)
    {
        if (!sWorldRandomizer->Config().enable)
        {
            handler->SendErrorMessage("The World Randomizer is off.");
            return false;
        }

        for (RandomizerZone const& zone : sWorldRandomizer->GetZones())
            handler->PSendSysMessage("{} ({}): {}-{} -> {}", zone.name, zone.zoneId, zone.levelMin, zone.levelMax,
                BracketText(zone.bracket));

        handler->PSendSysMessage("{} creatures replaced, {} kept and level shifted.", sWorldRandomizer->ReplacedCount(),
            sWorldRandomizer->ShiftedCount());
        return true;
    }

    static bool HandleRerollCommand(ChatHandler* handler, Optional<uint32> seed)
    {
        uint32 newSeed = seed && *seed ? *seed : std::max<uint32>(1, rand32());
        sWorldRandomizer->StoreSeed(newSeed);
        handler->PSendSysMessage("New world seed {} stored. Restart the worldserver to shuffle the world. "
            "(Randomizer.Seed in the config overrides it when set.)", newSeed);
        return true;
    }

    static bool HandleStartCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        StartLocation location;
        if (!sWorldRandomizer->PickStartLocation(player, location))
        {
            handler->SendErrorMessage("No bracket 1 start point for your faction.");
            return false;
        }

        player->TeleportTo(location.mapId, location.x, location.y, location.z, player->GetOrientation());
        return true;
    }

    // Green: you can take it at your level; red: not yet (or no longer)
    static bool HandleQuestsCommand(ChatHandler* handler, Optional<uint32> questId)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (questId)
        {
            Quest const* quest = sObjectMgr->GetQuestTemplate(*questId);
            if (!quest)
            {
                handler->SendErrorMessage("No quest {}.", *questId);
                return false;
            }
            QuestLine(handler, player, quest);
            return true;
        }

        constexpr uint32 MAX_LINES = 30;
        uint32 shown = 0;
        uint32 total = 0;
        if (Creature* creature = handler->getSelectedCreature())
        {
            handler->PSendSysMessage("Quests {} gives:", creature->GetName());
            auto bounds = sObjectMgr->GetCreatureQuestRelationBounds(creature->GetEntry());
            for (auto itr = bounds.first; itr != bounds.second; ++itr)
                if (Quest const* quest = sObjectMgr->GetQuestTemplate(itr->second))
                {
                    ++total;
                    if (shown < MAX_LINES)
                    {
                        QuestLine(handler, player, quest);
                        ++shown;
                    }
                }
        }
        else
        {
            uint32 zoneId = player->GetZoneId();
            handler->PSendSysMessage("Quests of this zone ({}):", zoneId);
            for (auto const& [id, quest] : sObjectMgr->GetQuestTemplates())
                if (quest->GetZoneOrSort() == int32(zoneId))
                {
                    ++total;
                    if (shown < MAX_LINES)
                    {
                        QuestLine(handler, player, quest);
                        ++shown;
                    }
                }
        }

        if (!total)
            handler->SendSysMessage("  none");
        else if (total > shown)
            handler->PSendSysMessage("  ... and {} more. Select a quest giver, or .randomizer quests <id>.",
                total - shown);
        return true;
    }
};

void AddWorldRandomizerCommandScripts()
{
    new WorldRandomizerCommandScript();
}
