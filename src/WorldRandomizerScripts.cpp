/*
 * mod-world-randomizer: script hooks.
 */

#include "WorldRandomizer.h"

#include "Chat.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"

class WorldRandomizerWorldScript : public WorldScript
{
public:
    WorldRandomizerWorldScript() : WorldScript("WorldRandomizerWorldScript",
        { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        // The shuffle and the stats are built once at startup, so config changes need a restart
        if (!reload)
            sWorldRandomizer->LoadConfig();
    }

    void OnBeforeWorldInitialized() override
    {
        sWorldRandomizer->ApplyStatEra();
        sWorldRandomizer->Initialize();
    }
};

class WorldRandomizerCreatureScript : public AllCreatureScript
{
public:
    WorldRandomizerCreatureScript() : AllCreatureScript("WorldRandomizerCreatureScript") { }

    void OnBeforeCreatureSelectLevel(CreatureTemplate const* /*cinfo*/, Creature* creature, uint8& level) override
    {
        Map* map = creature->GetMap();
        if (!map || creature->GetCharmerOrOwnerGUID().IsPlayer() || creature->IsPet() || creature->IsTotem())
            return;

        sWorldRandomizer->AdjustLevel(map, creature->GetSpawnId(), level);
    }
};

class WorldRandomizerPlayerScript : public PlayerScript
{
public:
    WorldRandomizerPlayerScript() : PlayerScript("WorldRandomizerPlayerScript", {
        PLAYERHOOK_ON_FIRST_LOGIN,
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_CALCULATE_TALENTS_POINTS
    }) { }

    // Talents before level 10: the first point at Talents.StartLevel. KeepTotal spreads the usual points at max level
    // (71 at 80) from there, so nobody ends up with more; otherwise every level from StartLevel gives one.
    void OnPlayerCalculateTalentsPoints(Player const* player, uint32& talentPointsForLevel) override
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        if (config.talentStartLevel >= 10 || player->IsClass(CLASS_DEATH_KNIGHT, CLASS_CONTEXT_TALENT_POINT_CALC))
            return;

        uint32 level = player->GetLevel();
        uint32 start = config.talentStartLevel;
        uint32 normal = level < 10 ? 0 : level - 9;
        uint32 wanted = 0;
        if (level >= start)
        {
            uint32 maxLevel = std::max<uint32>(10, sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
            if (config.talentKeepTotal)
            {
                uint32 total = maxLevel - 9;
                uint32 span = maxLevel - start + 1;
                uint32 steps = std::min(level, maxLevel) - start + 1;
                wanted = (steps * total + span - 1) / span + (level > maxLevel ? level - maxLevel : 0);
            }
            else
                wanted = level - start + 1;
        }

        int64 points = int64(talentPointsForLevel) + int64(wanted) - int64(normal);
        talentPointsForLevel = uint32(std::max<int64>(0, points));
    }

    void OnPlayerFirstLogin(Player* player) override
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        if (!config.enable || !config.playerStart)
            return;

        if (config.playerStartSkipDK && player->IsClass(CLASS_DEATH_KNIGHT))
            return;

        StartLocation location;
        if (!sWorldRandomizer->PickStartLocation(player, location))
            return;

        WorldLocation home(location.mapId, location.x, location.y, location.z, player->GetOrientation());
        player->SetHomebind(home, location.zoneId);
        player->TeleportTo(location.mapId, location.x, location.y, location.z, player->GetOrientation());

        if (RandomizerZone const* zone = sWorldRandomizer->FindZone(location.zoneId))
            ChatHandler(player->GetSession()).PSendSysMessage("|cffFF8000[World Randomizer]|r Your journey starts in "
                "{}. Your hearthstone is set there.", zone->name);
    }

    void OnPlayerLogin(Player* player) override
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        if (!config.moduleEnable || !config.announce)
            return;

        static char const* const ERA_NAMES[] = { "", "Vanilla", "TBC", "Wrath" };
        static char const* const HARDCORE_NAMES[] = { "", "you choose (.hardcore)", "every new character" };

        ChatHandler handler(player->GetSession());
        handler.SendSysMessage("This server is running the |cffFF8000World Randomizer|r module.");
        if (config.enable)
            handler.PSendSysMessage("  World shuffle: |cff4CFF00on|r, seed {}. .randomizer info shows your zone.",
                sWorldRandomizer->GetSeed());
        if (config.era != StatEra::Unchanged)
            handler.PSendSysMessage("  Player stats: {}, XP x{:.2f}", ERA_NAMES[uint8(config.era)],
                sWorldRandomizer->GetXPRate());
        if (config.hardcoreMode)
            handler.PSendSysMessage("  Hardcore: |cffFF2020{}|r", HARDCORE_NAMES[config.hardcoreMode]);
        if (config.coopEnable)
            handler.PSendSysMessage("  Co-op starts: up to {} players (.coop)", config.coopMaxSize);
    }

    void OnPlayerGiveXP(Player* /*player*/, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
    {
        float rate = sWorldRandomizer->GetXPRate();
        if (rate != 1.0f)
            amount = uint32(float(amount) * rate);
    }
};

void AddWorldRandomizerScripts()
{
    new WorldRandomizerWorldScript();
    new WorldRandomizerCreatureScript();
    new WorldRandomizerPlayerScript();
}
