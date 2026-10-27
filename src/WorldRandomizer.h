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

class Player;

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

    // Filled by the shuffle
    uint8 homeBracket = 0;
    uint8 bracket = 0;
};

struct RandomizerConfig
{
    bool enable = false;
    uint32 seed = 0;
    std::vector<Bracket> brackets;
    ShuffleScope scope = ShuffleScope::Global;
    bool balanced = false;

    bool replaceCreatures = true;
    bool sameEntrySameReplacement = true;
    bool replaceElites = true;
    bool replaceRares = false;
    bool keepQuestCreatures = true;
    bool shiftKeptLevels = true;
    bool computeMissingZones = true;
    std::vector<uint32> excludeEntries;

    bool dungeonShift = true;

    bool playerStart = true;
    bool playerStartSkipDK = true;
    bool playerStartFactionLock = true;

    StatEra era = StatEra::Unchanged;
    float xpRateVanilla = 1.0f;
    float xpRateTBC = 0.75f;
    float xpRateWrath = 0.5f;
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

    // Level hooks
    bool AdjustLevel(uint32 mapId, ObjectGuid::LowType spawnId, bool isDungeon, uint8& level) const;

    // Players
    bool PickStartLocation(Player* player, StartLocation& out) const;
    float GetXPRate() const;

    void StoreSeed(uint32 seed) const;

    static std::vector<Bracket> ParseBrackets(std::string const& text);
    static uint8 HomeBracket(std::vector<Bracket> const& brackets, uint8 levelMin, uint8 levelMax);
    static uint8 RemapLevel(uint8 level, uint8 fromMin, uint8 fromMax, uint8 toMin, uint8 toMax);

private:
    void LoadZones();
    void Shuffle();
    void LoadSpawnZones(std::unordered_map<ObjectGuid::LowType, uint32>& out);
    void ReplaceCreatures();
    void ComputeDungeonShifts();
    void LoadStartLocations();
    uint32 ResolveSeed();

    RandomizerConfig _config;
    uint32 _seed = 0;
    std::vector<RandomizerZone> _zones;
    std::unordered_map<uint32, std::size_t> _zoneIndex;

    std::unordered_map<ObjectGuid::LowType, uint16> _spawnZone; // kept spawn -> zone index, for level remap
    std::unordered_map<uint32, int32> _dungeonShift;            // map -> level delta
    std::vector<StartLocation> _startLocations;
    std::size_t _replaced = 0;

    bool _statsApplied = false;
};

#define sWorldRandomizer WorldRandomizerMgr::instance()

#endif // MOD_WORLD_RANDOMIZER_H
