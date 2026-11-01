/*
 * mod-world-randomizer
 *
 * Shuffles every outdoor zone into a random level bracket, then:
 *   - replaces outdoor combat creatures with random creatures from the zone's new bracket,
 *   - moves the levels of creatures it keeps (quest targets, scripted spawns) into the new bracket,
 *   - moves dungeon creature levels by the same amount as their entrance zone moved,
 *   - starts new characters in a bracket 1 zone of their own faction.
 * Also: player base stats from Wrath (AzerothCore), TBC (cMaNGOS-TBC) or Vanilla (vMaNGOS),
 * with an XP rate per choice.
 *
 * Nothing is written to the creature tables: replacements are made in memory at startup,
 * so turning the module off brings the normal world back.
 */

#ifndef MOD_WORLD_RANDOMIZER_H
#define MOD_WORLD_RANDOMIZER_H

#include "Define.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"

#include <string>
#include <unordered_map>
#include <vector>

class Map;
class Player;
class WorldLocation;

// Mulberry32. The web map (web/randomizer.js) uses the same generator, so a seed gives the same shuffle there.
class RandomizerRng
{
public:
    explicit RandomizerRng(uint32 seed) : _state(seed) { }

    uint32 Next()
    {
        _state += 0x6D2B79F5u;
        uint32 t = _state;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return t ^ (t >> 14);
    }

    uint32 Below(uint32 count) { return count ? Next() % count : 0; }

private:
    uint32 _state;
};

enum class StatEra : uint8
{
    Unchanged = 0, // leave AzerothCore's stats and XP alone
    Vanilla   = 1, // vMaNGOS
    TBC       = 2, // cMaNGOS-TBC
    Wrath     = 3  // AzerothCore
};

enum class ShuffleScope : uint8
{
    Global    = 0,
    Continent = 1,
    Expansion = 2
};

struct Bracket
{
    uint8 levelMin = 1;
    uint8 levelMax = 1;
};

// One row of `world_randomizer_zone`
struct RandomizerZone
{
    uint32 zoneId = 0;
    uint32 mapId = 0;
    uint8 continent = 0;  // 0 Eastern Kingdoms, 1 Kalimdor, 2 Outland, 3 Northrend
    uint8 expansion = 0;
    uint8 levelMin = 0;   // original range
    uint8 levelMax = 0;
    uint8 territory = 2;  // 0 alliance, 1 horde, 2 contested
    bool shuffle = true;  // false = stays in its home bracket
    std::string name;
    std::vector<uint32> neighbours; // zone ids that border this one

    // Filled by the shuffle
    uint8 homeBracket = 0;
    uint8 bracket = 0;
};

struct RandomizerConfig
{
    bool moduleEnable = true; // Randomizer.Enable: master switch for every feature below
    bool announce = true;     // Randomizer.Announce: login message
    bool enable = false;      // Randomizer.World.Enable: the world shuffle
    uint32 seed = 0;
    std::vector<Bracket> brackets;
    ShuffleScope scope = ShuffleScope::Global;
    bool balanced = false;
    bool adjacent = true; // neighbouring zones get neighbouring brackets
    bool factionStarts = true; // each faction gets one of its own zones in bracket 1

    bool replaceCreatures = true;
    bool sameEntrySameReplacement = true;
    bool replaceElites = true;
    bool replaceRares = false;
    bool keepQuestCreatures = true;
    bool shiftKeptLevels = true;
    bool computeMissingZones = true;
    bool shiftQuests = true;      // quests of moved zones follow their creatures (level, level requirement, XP)
    bool shiftQuestMoney = true;  // and their reward money
    bool questMinLevelFromBracket = true; // level requirement = the bottom of the zone's new bracket
    bool questSwapRewards = true; // shifted quests reward similar items of their new level
    bool lootSwap = true;         // creatures whose level moved drop weapons and armor of their level
    uint32 lootSwapMargin = 3;    // levels an item may be off before it is swapped
    std::vector<uint32> excludeEntries;

    uint8 dungeonMode = 2; // 0 unchanged, 1 shift by as much as the entrance zone moved, 2 scale to the group
                           // inside the entrance zone's bracket

    // Collective with mod-dynamic-scaling: it scales dungeons (Simple) inside their bracket, so this module
    // publishes the brackets and stops shifting dungeon levels itself. Needs both modules' collective switches on.
    bool collectiveSetting = true;
    bool collective = false;
    // And mod-dynamic-scaling scales the open world inside each zone's bracket (Collective.WorldScaling there):
    // this module then leaves open world creature levels to it
    bool collectiveWorld = false;

    bool playerStart = true;
    uint8 playerStartWhere = 2;    // 1 a graveyard in a random bracket 1 zone, 2 the race's capital city
    bool playerStartSkipDK = true;
    bool playerStartFactionLock = true;
    bool playerStartPreferLow = true; // zones that were already low level (their home bracket is bracket 1) first

    // Co-op: friends share a code and start together
    bool coopEnable = true;
    uint8 coopMaxSize = 5;
    uint8 coopMaxLevel = 1;
    bool coopSameFaction = true;
    bool coopAutoGroup = true;

    // Hardcore: one life. A dead hardcore character is never deleted for good (see WorldRandomizerHardcore.cpp)
    uint8 hardcoreMode = 0;      // 0 off, 1 players opt in with .hardcore on, 2 every new character
    uint8 hardcoreOnDeath = 2;   // 0 ghost forever, 1 locked (can't log in), 2 soft deleted (core unlink)
    uint32 hardcoreKickDelay = 10;
    bool hardcoreIgnoreBattlegrounds = true;
    bool hardcoreCountPvP = true;
    bool hardcoreAnnounce = true;

    StatEra era = StatEra::Unchanged;
    float xpRateVanilla = 1.0f;
    float xpRateTBC = 0.75f;
    float xpRateWrath = 0.5f;

    uint8 talentStartLevel = 10; // first talent point at this level (10 = as normal)
    bool talentKeepTotal = true; // same points at max level, spread from talentStartLevel
};

// A shifted quest's values before the shift (.randomizer quests)
struct QuestOriginal
{
    int32 level = 0;
    uint32 minLevel = 0;
    int32 money = 0;
    uint8 rewardsSwapped = 0;
};

struct StartLocation
{
    uint32 mapId = 0;
    uint32 zoneId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    TeamId team = TEAM_NEUTRAL;
};

// Sets the hearthstone and tells the client, so the hearthstone shows the new place right away (the core's
// SetHomebind only saves it; the client would show the old one until the next login)
void RandomizerSetHomebind(Player* player, WorldLocation const& location, uint32 areaId);

class WorldRandomizerMgr
{
public:
    static WorldRandomizerMgr* instance();

    void LoadConfig();
    void Initialize();        // at OnBeforeWorldInitialized: after all data, before any grid loads
    void ApplyStatEra();

    RandomizerConfig const& Config() const { return _config; }
    uint32 GetSeed() const { return _seed; }
    std::vector<RandomizerZone> const& GetZones() const { return _zones; }
    RandomizerZone const* FindZone(uint32 zoneId) const;
    std::size_t ReplacedCount() const { return _replaced; }
    std::size_t ShiftedCount() const { return _spawnZone.size(); }
    QuestOriginal const* GetQuestOriginal(uint32 questId) const;
    bool IsShiftedSpawn(ObjectGuid::LowType spawnId) const { return _spawnZone.count(spawnId) > 0; }

    // Level hooks
    bool AdjustLevel(Map* map, ObjectGuid::LowType spawnId, uint8& level) const;
    Bracket const* GetDungeonBracket(uint32 mapId) const;

    // Players
    // zoneId: start in that zone (it must be one of the player's start zones); 0 = pick one
    bool PickStartLocation(Player* player, StartLocation& out, uint32 zoneId = 0) const;
    // The zones PickStartLocation would choose from for this player (the preferred ones only)
    std::vector<uint32> StartZones(Player* player) const;
    float GetXPRate() const;

    void StoreSeed(uint32 seed) const;

    static std::vector<Bracket> ParseBrackets(std::string const& text);
    static uint8 HomeBracket(std::vector<Bracket> const& brackets, uint8 levelMin, uint8 levelMax);
    static uint8 RemapLevel(uint8 level, uint8 fromMin, uint8 fromMax, uint8 toMin, uint8 toMax);

private:
    void LoadZones();
    void Shuffle();
    void EnsureFactionStarts();
    uint32 GroupKey(RandomizerZone const& zone) const;
    void LoadSpawnZones(std::unordered_map<ObjectGuid::LowType, uint32>& out);
    void ReplaceCreatures();
    void ShiftQuests();
    void ComputeDungeonShifts();
    void PublishDungeonBrackets();
    void PublishZoneBrackets();
    void LoadStartLocations();
    uint32 ResolveSeed();

    RandomizerConfig _config;
    uint32 _seed = 0;
    std::vector<RandomizerZone> _zones;
    std::unordered_map<uint32, std::size_t> _zoneIndex;

    std::unordered_map<ObjectGuid::LowType, uint16> _spawnZone; // kept spawn -> zone index, for level remap
    std::unordered_map<uint32, int32> _dungeonShift;            // map -> level delta
    std::unordered_map<uint32, uint32> _dungeonZone;            // map -> entrance zone
    std::unordered_map<uint32, uint8> _dungeonBase;             // map -> level the dungeon was made for
    std::vector<StartLocation> _startLocations;
    std::size_t _replaced = 0;
    std::size_t _questsShifted = 0;
    std::unordered_map<uint32, QuestOriginal> _questOriginal;

    bool _statsApplied = false;
};

#define sWorldRandomizer WorldRandomizerMgr::instance()

#endif // MOD_WORLD_RANDOMIZER_H
