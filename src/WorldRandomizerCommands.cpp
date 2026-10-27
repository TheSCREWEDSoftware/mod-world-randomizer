/*
 * mod-world-randomizer: commands.
 *
 *   .randomizer info          seed, stats era, and your zone's old and new bracket
 *   .randomizer zones         every zone and its bracket
 *   .randomizer reroll [seed] store a new seed (random if none given); takes effect after a restart
 *   .randomizer start         teleport to a random bracket 1 start point for your faction
 */

#include "WorldRandomizer.h"

#include "Chat.h"
#include "CommandScript.h"
#include "Player.h"
#include "Random.h"

using namespace Acore::ChatCommands;

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
}

class WorldRandomizerCommandScript : public CommandScript
{
public:
    WorldRandomizerCommandScript() : CommandScript("WorldRandomizerCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable randomizerTable =
        {
            { "info",   HandleInfoCommand,   SEC_PLAYER,        Console::No },
            { "zones",  HandleZonesCommand,  SEC_GAMEMASTER,    Console::Yes },
            { "reroll", HandleRerollCommand, SEC_ADMINISTRATOR, Console::Yes },
            { "start",  HandleStartCommand,  SEC_GAMEMASTER,    Console::No }
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
};

void AddWorldRandomizerCommandScripts()
{
    new WorldRandomizerCommandScript();
}
