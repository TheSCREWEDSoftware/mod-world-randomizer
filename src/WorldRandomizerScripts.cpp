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

    // Each race's capital (the game_tele spots): map, x, y, z, orientation, zone
    struct Capital { uint8 race; uint32 map; float x, y, z, o; uint32 zone; char const* name; };

    static bool StartInCapital(Player* player)
    {
        static Capital const CAPITALS[] =
        {
            { RACE_HUMAN,         0, -8833.38f,   628.628f,   94.0066f, 1.06535f,  1519, "Stormwind" },
            { RACE_ORC,           1,  1629.85f, -4373.64f,    31.5573f, 3.69762f,  1637, "Orgrimmar" },
            { RACE_DWARF,         0, -4918.88f,  -940.406f,  501.564f,  5.42347f,  1537, "Ironforge" },
            { RACE_NIGHTELF,      1,  9949.56f,  2284.21f,  1341.4f,    1.59587f,  1657, "Darnassus" },
            { RACE_UNDEAD_PLAYER, 0,  1584.14f,   240.308f,  -52.1534f, 0.041793f, 1497, "Undercity" },
            { RACE_TAUREN,        1, -1277.37f,   124.804f,  131.287f,  5.22274f,  1638, "Thunder Bluff" },
            { RACE_GNOME,         0, -4918.88f,  -940.406f,  501.564f,  5.42347f,  1537, "Ironforge" },
            { RACE_TROLL,         1,  1629.85f, -4373.64f,    31.5573f, 3.69762f,  1637, "Orgrimmar" },
            { RACE_BLOODELF,    530,  9487.69f, -7279.2f,     14.2866f, 6.16478f,  3487, "Silvermoon City" },
            { RACE_DRAENEI,     530, -3965.7f, -11653.6f,  -138.844f,  0.852154f, 3557, "The Exodar" },
        };

        for (Capital const& c : CAPITALS)
        {
            if (c.race != player->getRace())
                continue;

            WorldLocation home(c.map, c.x, c.y, c.z, c.o);
            RandomizerSetHomebind(player, home, c.zone);
            player->TeleportTo(c.map, c.x, c.y, c.z, c.o);
            std::vector<Bracket> const& brackets = sWorldRandomizer->Config().brackets; // lowest first
            Bracket first = brackets.empty() ? Bracket() : brackets.front();
            ChatHandler(player->GetSession()).PSendSysMessage("|cffFF8000[World Randomizer]|r You start in {}, safe "
                "among its guards, with your hearthstone there. Pick where your journey starts in the "
                "RandomizerAchievements addon's Group tab (or .coop alone / .coop create), or travel to a level {}-{} "
                "zone (the addon's World tab lists them).", c.name, first.levelMin, first.levelMax);
            return true;
        }
        return false;
    }

    void OnPlayerFirstLogin(Player* player) override
    {
        RandomizerConfig const& config = sWorldRandomizer->Config();
        if (!config.enable || !config.playerStart)
            return;

        if (config.playerStartSkipDK && player->IsClass(CLASS_DEATH_KNIGHT))
            return;

        // Where = 2: the race's capital, safe among its guards (the race's own start may sit in a zone shuffled to
        // high levels). The Group tab or .coop then takes the character to a bracket 1 zone.
        if (config.playerStartWhere == 2 && StartInCapital(player))
            return;

        StartLocation location;
        if (!sWorldRandomizer->PickStartLocation(player, location))
            return;

        WorldLocation home(location.mapId, location.x, location.y, location.z, player->GetOrientation());
        RandomizerSetHomebind(player, home, location.zoneId);
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
