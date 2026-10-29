/*
 * mod-world-randomizer: the shuffle, creature replacement, start locations and stat eras.
 */

#include "WorldRandomizer.h"

#include "Config.h"
#include "Containers.h"
#include "CreatureData.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameGraveyard.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Player.h"
#include "RaceMgr.h"
#include "Random.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include "World.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <set>
#include <unordered_set>

namespace
{
    bool IsOpenWorldMap(uint32 mapId)
    {
        return mapId == 0 || mapId == 1 || mapId == 530 || mapId == 571;
    }

    // An outdoor creature that fights players: what the shuffle may replace or draw from
    bool IsCombatTemplate(CreatureTemplate const* cInfo, FactionTemplateEntry const* alliance,
        FactionTemplateEntry const* horde)
    {
        if (!cInfo || cInfo->npcflag || cInfo->VehicleId || cInfo->ScriptID)
            return false;

        if (cInfo->unit_flags & (UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PC))
            return false;

        switch (cInfo->type)
        {
            case CREATURE_TYPE_CRITTER:
            case CREATURE_TYPE_TOTEM:
            case CREATURE_TYPE_NON_COMBAT_PET:
            case CREATURE_TYPE_GAS_CLOUD:
                return false;
            default:
                break;
        }

        if (cInfo->HasFlagsExtra(CREATURE_FLAG_EXTRA_TRIGGER | CREATURE_FLAG_EXTRA_CIVILIAN |
            CREATURE_FLAG_EXTRA_DUNGEON_BOSS))
            return false;

        if (cInfo->rank == CREATURE_ELITE_WORLDBOSS)
            return false;

        FactionTemplateEntry const* faction = sFactionTemplateStore.LookupEntry(cInfo->faction);
        if (!faction)
            return false;

        // Guards and town folk of either side are not "mobs"
        if ((alliance && faction->IsFriendlyTo(*alliance)) || (horde && faction->IsFriendlyTo(*horde)))
            return false;

        return true;
    }

    bool IsElite(CreatureTemplate const* cInfo)
    {
        return cInfo->rank == CREATURE_ELITE_ELITE || cInfo->rank == CREATURE_ELITE_RAREELITE;
    }

    bool IsRare(CreatureTemplate const* cInfo)
    {
        return cInfo->rank == CREATURE_ELITE_RARE || cInfo->rank == CREATURE_ELITE_RAREELITE;
    }

    bool IsAquatic(CreatureTemplate const* cInfo)
    {
        return cInfo->Movement.IsSwimAllowed() && !cInfo->Movement.IsGroundAllowed();
    }

    uint32 PoolKey(uint8 bracket, bool aquatic, bool elite)
    {
        return (uint32(bracket) << 2) | (aquatic ? 2u : 0u) | (elite ? 1u : 0u);
    }

    uint32 StatKey(uint8 race, uint8 cls, uint8 level)
    {
        return (uint32(race) << 16) | (uint32(cls) << 8) | level;
    }

    constexpr std::array<uint8, 10> RACE_ORDER = { 1, 2, 3, 4, 5, 6, 7, 8, 10, 11 };
}

WorldRandomizerMgr* WorldRandomizerMgr::instance()
{
    static WorldRandomizerMgr instance;
    return &instance;
}

std::vector<Bracket> WorldRandomizerMgr::ParseBrackets(std::string const& text)
{
    // Spaces are ignored, like in web/randomizer.js
    std::string compact;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c)))
            compact += c;

    std::vector<Bracket> brackets;
    for (std::string_view token : Acore::Tokenize(compact, ',', false))
    {
        std::vector<std::string_view> parts = Acore::Tokenize(token, '-', false);
        if (parts.size() != 2)
            continue;

        Optional<uint32> low = Acore::StringTo<uint32>(parts[0]);
        Optional<uint32> high = Acore::StringTo<uint32>(parts[1]);
        if (!low || !high || !*low || *low > *high || *high > MAX_LEVEL)
            continue;

        brackets.push_back({ uint8(*low), uint8(*high) });
    }

    // Lowest first, whatever order they were typed in
    std::stable_sort(brackets.begin(), brackets.end(), [](Bracket const& a, Bracket const& b)
    {
        return a.levelMin != b.levelMin ? a.levelMin < b.levelMin : a.levelMax < b.levelMax;
    });

    return brackets;
}

uint8 WorldRandomizerMgr::HomeBracket(std::vector<Bracket> const& brackets, uint8 levelMin, uint8 levelMax)
{
    int32 mid = (int32(levelMin) + int32(levelMax)) / 2;

    for (std::size_t i = 0; i < brackets.size(); ++i)
        if (mid >= brackets[i].levelMin && mid <= brackets[i].levelMax)
            return uint8(i);

    uint8 best = 0;
    int32 bestDistance = std::numeric_limits<int32>::max();
    for (std::size_t i = 0; i < brackets.size(); ++i)
    {
        int32 center = (int32(brackets[i].levelMin) + int32(brackets[i].levelMax)) / 2;
        int32 distance = std::abs(center - mid);
        if (distance < bestDistance)
        {
            best = uint8(i);
            bestDistance = distance;
        }
    }

    return best;
}

uint8 WorldRandomizerMgr::RemapLevel(uint8 level, uint8 fromMin, uint8 fromMax, uint8 toMin, uint8 toMax)
{
    int32 result;
    if (fromMax <= fromMin)
        result = int32(toMin) + std::clamp<int32>(int32(level) - int32(fromMin), 0, int32(toMax) - int32(toMin));
    else
        result = int32(toMin) + (int32(level) - int32(fromMin)) * (int32(toMax) - int32(toMin)) /
            (int32(fromMax) - int32(fromMin));

    return uint8(std::clamp<int32>(result, 1, MAX_LEVEL));
}

void WorldRandomizerMgr::LoadConfig()
{
    RandomizerConfig c;

    c.moduleEnable = sConfigMgr->GetOption<bool>("Randomizer.Enable", true);
    c.announce = sConfigMgr->GetOption<bool>("Randomizer.Announce", true);
    c.enable = sConfigMgr->GetOption<bool>("Randomizer.World.Enable", false);
    c.seed = sConfigMgr->GetOption<uint32>("Randomizer.Seed", 0);
    c.brackets = ParseBrackets(sConfigMgr->GetOption<std::string>("Randomizer.Brackets",
        "1-10,11-20,21-30,31-40,41-50,51-60,61-70,71-80"));
    if (c.brackets.empty())
    {
        LOG_ERROR("module", "mod-world-randomizer: Randomizer.Brackets has no valid bracket, using 1-80.");
        c.brackets.push_back({ 1, 80 });
    }

    // Brackets should follow on from each other: a gap means some levels have no zone at all
    for (std::size_t i = 1; i < c.brackets.size(); ++i)
        if (c.brackets[i].levelMin > c.brackets[i - 1].levelMax + 1)
            LOG_WARN("module", "mod-world-randomizer: Randomizer.Brackets has a gap between {}-{} and {}-{}.",
                c.brackets[i - 1].levelMin, c.brackets[i - 1].levelMax, c.brackets[i].levelMin,
                c.brackets[i].levelMax);

    uint32 scope = sConfigMgr->GetOption<uint32>("Randomizer.Scope", 0);
    c.scope = scope <= uint32(ShuffleScope::Expansion) ? ShuffleScope(scope) : ShuffleScope::Global;
    c.balanced = sConfigMgr->GetOption<bool>("Randomizer.Brackets.Balanced", false);
    c.adjacent = sConfigMgr->GetOption<bool>("Randomizer.Brackets.Adjacent", true);
    c.factionStarts = sConfigMgr->GetOption<bool>("Randomizer.Brackets.FactionStarts", true);

    c.replaceCreatures = sConfigMgr->GetOption<bool>("Randomizer.Creatures.Replace", true);
    c.sameEntrySameReplacement = sConfigMgr->GetOption<bool>("Randomizer.Creatures.SameEntrySameReplacement", true);
    c.replaceElites = sConfigMgr->GetOption<bool>("Randomizer.Creatures.ReplaceElites", true);
    c.replaceRares = sConfigMgr->GetOption<bool>("Randomizer.Creatures.ReplaceRares", false);
    c.keepQuestCreatures = sConfigMgr->GetOption<bool>("Randomizer.Creatures.KeepQuestCreatures", true);
    c.shiftKeptLevels = sConfigMgr->GetOption<bool>("Randomizer.Creatures.ShiftKeptLevels", true);
    c.computeMissingZones = sConfigMgr->GetOption<bool>("Randomizer.Creatures.ComputeMissingZones", true);
    std::string excluded = sConfigMgr->GetOption<std::string>("Randomizer.Creatures.ExcludeEntries", "");
    for (std::string_view token : Acore::Tokenize(excluded, ',', false))
        if (Optional<uint32> entry = Acore::StringTo<uint32>(token))
            c.excludeEntries.push_back(*entry);

    c.dungeonMode = std::min<uint8>(2, sConfigMgr->GetOption<uint8>("Randomizer.Dungeons.Mode", 2));

    // The other module's settings live in the same config manager; quiet lookups, since it may not be installed
    c.collectiveSetting = sConfigMgr->GetOption<bool>("Randomizer.Collective.DynamicScaling", true);
    c.collective = c.collectiveSetting && c.moduleEnable && c.enable &&
        sConfigMgr->GetOption<bool>("DynamicScaling.Enable", false, false) &&
        sConfigMgr->GetOption<bool>("DynamicScaling.Instance.Enable", true, false) &&
        sConfigMgr->GetOption<bool>("DynamicScaling.Collective.WorldRandomizer", false, false);

    c.shiftQuests = sConfigMgr->GetOption<bool>("Randomizer.Quests.Shift", true);
    c.shiftQuestMoney = sConfigMgr->GetOption<bool>("Randomizer.Quests.ShiftMoney", true);
    c.questMinLevelFromBracket = sConfigMgr->GetOption<bool>("Randomizer.Quests.MinLevelFromBracket", true);

    c.playerStart = sConfigMgr->GetOption<bool>("Randomizer.PlayerStart.Enable", true);
    c.playerStartSkipDK = sConfigMgr->GetOption<bool>("Randomizer.PlayerStart.SkipDeathKnights", true);
    c.playerStartFactionLock = sConfigMgr->GetOption<bool>("Randomizer.PlayerStart.FactionLock", true);

    c.coopEnable = sConfigMgr->GetOption<bool>("Randomizer.Coop.Enable", true);
    c.coopMaxSize = std::max<uint8>(2, sConfigMgr->GetOption<uint8>("Randomizer.Coop.MaxSize", 5));
    c.coopMaxLevel = sConfigMgr->GetOption<uint8>("Randomizer.Coop.MaxLevel", 1);
    c.coopSameFaction = sConfigMgr->GetOption<bool>("Randomizer.Coop.SameFaction", true);
    c.coopAutoGroup = sConfigMgr->GetOption<bool>("Randomizer.Coop.AutoGroup", true);

    c.hardcoreMode = std::min<uint8>(2, sConfigMgr->GetOption<uint8>("Randomizer.Hardcore.Mode", 0));
    c.hardcoreOnDeath = std::min<uint8>(2, sConfigMgr->GetOption<uint8>("Randomizer.Hardcore.OnDeath", 2));
    c.hardcoreKickDelay = sConfigMgr->GetOption<uint32>("Randomizer.Hardcore.KickDelay", 10);
    c.hardcoreIgnoreBattlegrounds = sConfigMgr->GetOption<bool>("Randomizer.Hardcore.IgnoreBattlegrounds", true);
    c.hardcoreCountPvP = sConfigMgr->GetOption<bool>("Randomizer.Hardcore.CountPvPDeaths", true);
    c.hardcoreAnnounce = sConfigMgr->GetOption<bool>("Randomizer.Hardcore.Announce", true);

    uint32 era = sConfigMgr->GetOption<uint32>("Randomizer.Stats.Era", 0);
    c.era = era <= uint32(StatEra::Wrath) ? StatEra(era) : StatEra::Unchanged;
    c.xpRateVanilla = sConfigMgr->GetOption<float>("Randomizer.Stats.XPRate.Vanilla", 1.0f);
    c.xpRateTBC = sConfigMgr->GetOption<float>("Randomizer.Stats.XPRate.TBC", 0.75f);
    c.xpRateWrath = sConfigMgr->GetOption<float>("Randomizer.Stats.XPRate.Wrath", 0.5f);

    c.talentStartLevel = std::clamp<uint8>(sConfigMgr->GetOption<uint8>("Randomizer.Talents.StartLevel", 10), 1, 10);
    c.talentKeepTotal = sConfigMgr->GetOption<bool>("Randomizer.Talents.KeepTotal", true);

    // The master switch turns every feature off
    if (!c.moduleEnable)
    {
        c.enable = false;
        c.announce = false;
        c.hardcoreAnnounce = false;
        c.hardcoreMode = 0;
        c.era = StatEra::Unchanged;
        c.coopEnable = false;
        c.talentStartLevel = 10;
    }

    _config = c;
}

uint32 WorldRandomizerMgr::ResolveSeed()
{
    if (_config.seed)
        return _config.seed;

    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `value` FROM `world_randomizer_state` WHERE `key` = 'seed'"))
        if (Optional<uint32> seed = Acore::StringTo<uint32>((*result)[0].Get<std::string>()))
            if (*seed)
                return *seed;

    uint32 seed = std::max<uint32>(1, rand32());
    StoreSeed(seed);
    LOG_INFO("module", "mod-world-randomizer: rolled a new world seed {}.", seed);
    return seed;
}

void WorldRandomizerMgr::StoreSeed(uint32 seed) const
{
    CharacterDatabase.Execute("REPLACE INTO `world_randomizer_state` (`key`, `value`) VALUES ('seed', '{}')", seed);
}

void WorldRandomizerMgr::Initialize()
{
    if (!_config.enable)
    {
        PublishDungeonBrackets(); // clears what an earlier run left
        return;
    }

    _seed = ResolveSeed();
    LoadZones();
    Shuffle();

    if (_config.replaceCreatures || _config.shiftKeptLevels)
        ReplaceCreatures();

    if (_config.shiftQuests)
        ShiftQuests();

    if (_config.dungeonMode || _config.collective)
        ComputeDungeonShifts();

    PublishDungeonBrackets();

    if (_config.playerStart)
        LoadStartLocations();

    LOG_INFO("module", "mod-world-randomizer: seed {}, {} zones in {} brackets, {} creatures replaced, {} kept and "
        "level shifted, {} quests shifted, {} dungeons {}, {} start points.", _seed, _zones.size(),
        _config.brackets.size(), _replaced, _spawnZone.size(), _questsShifted,
        _config.collective ? _dungeonZone.size() : _dungeonShift.size(),
        _config.collective ? "handed to mod-dynamic-scaling" :
            (_config.dungeonMode == 2 ? "scaling inside their bracket" : "shifted"), _startLocations.size());
}

void WorldRandomizerMgr::LoadZones()
{
    _zones.clear();
    _zoneIndex.clear();

    //                                                   0          1         2            3            4
    QueryResult result = WorldDatabase.Query("SELECT `zone_id`, `map_id`, `continent`, `expansion`, `level_min`, "
        //   5            6            7          8
        "`level_max`, `territory`, `shuffle`, `name` FROM `world_randomizer_zone` WHERE `enabled` = 1 "
        "ORDER BY `zone_id`");
    if (!result)
    {
        LOG_ERROR("module", "mod-world-randomizer: `world_randomizer_zone` is empty, nothing to shuffle.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        RandomizerZone zone;
        zone.zoneId = fields[0].Get<uint32>();
        zone.mapId = fields[1].Get<uint32>();
        zone.continent = fields[2].Get<uint8>();
        zone.expansion = fields[3].Get<uint8>();
        zone.levelMin = fields[4].Get<uint8>();
        zone.levelMax = fields[5].Get<uint8>();
        zone.territory = fields[6].Get<uint8>();
        zone.shuffle = fields[7].Get<bool>();
        zone.name = fields[8].Get<std::string>();

        if (!zone.levelMin || zone.levelMax < zone.levelMin)
            continue;

        _zoneIndex[zone.zoneId] = _zones.size();
        _zones.push_back(zone);
    } while (result->NextRow());

    if (QueryResult links = WorldDatabase.Query("SELECT `zone_id`, `neighbour_id` FROM `world_randomizer_zone_link` "
        "ORDER BY `zone_id`, `neighbour_id`"))
    {
        do
        {
            auto itr = _zoneIndex.find((*links)[0].Get<uint32>());
            if (itr != _zoneIndex.end())
                _zones[itr->second].neighbours.push_back((*links)[1].Get<uint32>());
        } while (links->NextRow());
    }
}

void WorldRandomizerMgr::Shuffle()
{
    // Keep this in step with shuffleZones() in web/randomizer.js
    RandomizerRng rng(_seed);
    uint8 bracketCount = uint8(_config.brackets.size());

    std::map<uint32, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < _zones.size(); ++i)
    {
        RandomizerZone& zone = _zones[i];
        zone.homeBracket = HomeBracket(_config.brackets, zone.levelMin, zone.levelMax);
        zone.bracket = zone.homeBracket;

        if (!zone.shuffle)
            continue;

        uint32 key = 0;
        if (_config.scope == ShuffleScope::Continent)
            key = zone.continent;
        else if (_config.scope == ShuffleScope::Expansion)
            key = zone.expansion;

        groups[key].push_back(i);
    }

    for (auto& [key, members] : groups)
    {
        std::vector<uint8> pool;
        pool.reserve(members.size());
        for (std::size_t n = 0; n < members.size(); ++n)
            pool.push_back(_config.balanced ? uint8(n % bracketCount) : _zones[members[n]].homeBracket);

        if (!_config.adjacent)
        {
            for (std::size_t n = pool.size(); n > 1; --n)
                std::swap(pool[n - 1], pool[rng.Below(uint32(n))]);

            for (std::size_t n = 0; n < members.size(); ++n)
                _zones[members[n]].bracket = pool[n];
            continue;
        }

        // Adjacent: start at a random zone and grow outwards through bordering zones, handing out the brackets
        // lowest first. Neighbours end up with neighbouring brackets; where the zones run out (an island, the end
        // of a continent) it jumps to a random zone that is still free.
        std::sort(pool.begin(), pool.end());
        std::vector<bool> assigned(_zones.size(), false);
        std::vector<std::size_t> candidates;
        for (std::size_t k = 0; k < pool.size(); ++k)
        {
            candidates.clear();
            if (k)
                for (std::size_t index : members)
                {
                    if (assigned[index])
                        continue;

                    for (uint32 neighbour : _zones[index].neighbours)
                    {
                        auto itr = _zoneIndex.find(neighbour);
                        if (itr != _zoneIndex.end() && assigned[itr->second])
                        {
                            candidates.push_back(index);
                            break;
                        }
                    }
                }

            if (candidates.empty())
                for (std::size_t index : members)
                    if (!assigned[index])
                        candidates.push_back(index);

            std::size_t chosen = candidates[rng.Below(uint32(candidates.size()))];
            assigned[chosen] = true;
            _zones[chosen].bracket = pool[k];
        }
    }

    if (_config.factionStarts)
        EnsureFactionStarts();
}

uint32 WorldRandomizerMgr::GroupKey(RandomizerZone const& zone) const
{
    if (_config.scope == ShuffleScope::Continent)
        return zone.continent;
    if (_config.scope == ShuffleScope::Expansion)
        return zone.expansion;
    return 0;
}

void WorldRandomizerMgr::EnsureFactionStarts()
{
    // Keep this in step with ensureFactionStarts() in web/randomizer.js. No random numbers: it only swaps.
    // Brackets are sorted, so bracket 0 is the lowest (where new characters start).
    for (uint8 team = 0; team < 2; ++team)
    {
        bool present = std::any_of(_zones.begin(), _zones.end(), [team](RandomizerZone const& zone)
        {
            return zone.bracket == 0 && zone.territory == team;
        });
        if (present)
            continue;

        // The faction's own zone in the lowest bracket (lowest id on a tie)...
        RandomizerZone* own = nullptr;
        for (RandomizerZone& zone : _zones)
            if (zone.shuffle && zone.territory == team && (!own || zone.bracket < own->bracket))
                own = &zone;

        if (!own)
            continue;

        // ...swaps brackets with the first contested bracket 0 zone of the same shuffle group
        for (RandomizerZone& zone : _zones)
        {
            if (zone.shuffle && zone.bracket == 0 && zone.territory == 2 && GroupKey(zone) == GroupKey(*own))
            {
                std::swap(zone.bracket, own->bracket);
                break;
            }
        }
    }
}

RandomizerZone const* WorldRandomizerMgr::FindZone(uint32 zoneId) const
{
    auto itr = _zoneIndex.find(zoneId);
    return itr != _zoneIndex.end() ? &_zones[itr->second] : nullptr;
}

void WorldRandomizerMgr::LoadSpawnZones(std::unordered_map<ObjectGuid::LowType, uint32>& out)
{
    if (QueryResult result = WorldDatabase.Query("SELECT `guid`, `zoneId` FROM `creature` "
        "WHERE `map` IN (0, 1, 530, 571) AND `zoneId` <> 0"))
    {
        do
        {
            out[(*result)[0].Get<uint32>()] = (*result)[1].Get<uint16>();
        } while (result->NextRow());
    }

    if (QueryResult result = WorldDatabase.Query("SELECT `guid`, `zone_id` FROM `world_randomizer_spawn_zone`"))
    {
        do
        {
            out.emplace((*result)[0].Get<uint32>(), (*result)[1].Get<uint32>());
        } while (result->NextRow());
    }

    if (!_config.computeMissingZones)
        return;

    // Spawns with no zone yet: ask the terrain once and remember the answer
    std::vector<std::pair<ObjectGuid::LowType, uint32>> computed;
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        if (!IsOpenWorldMap(data.mapid) || out.find(spawnId) != out.end())
            continue;

        uint32 zoneId = sMapMgr->GetZoneId(PHASEMASK_NORMAL, data.mapid, data.posX, data.posY, data.posZ);
        out[spawnId] = zoneId;
        computed.emplace_back(spawnId, zoneId);
    }

    if (computed.empty())
        return;

    LOG_INFO("module", "mod-world-randomizer: worked out the zone of {} creature spawns, saved for next time.",
        computed.size());

    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();
    for (std::size_t i = 0; i < computed.size(); i += 500)
    {
        std::string values;
        for (std::size_t n = i; n < std::min(computed.size(), i + 500); ++n)
        {
            if (!values.empty())
                values += ',';
            values += Acore::StringFormat("({},{})", computed[n].first, computed[n].second);
        }

        trans->Append("REPLACE INTO `world_randomizer_spawn_zone` (`guid`, `zone_id`) VALUES " + values);
    }
    WorldDatabase.CommitTransaction(trans);
}

void WorldRandomizerMgr::ReplaceCreatures()
{
    _spawnZone.clear();
    _replaced = 0;

    if (_zones.empty())
        return;

    FactionTemplateEntry const* alliance = sFactionTemplateStore.LookupEntry(1); // Human
    FactionTemplateEntry const* horde = sFactionTemplateStore.LookupEntry(2);    // Orc

    std::unordered_set<uint32> questCreatures;
    if (_config.keepQuestCreatures)
    {
        if (QueryResult result = WorldDatabase.Query("SELECT `RequiredNpcOrGo1`, `RequiredNpcOrGo2`, "
            "`RequiredNpcOrGo3`, `RequiredNpcOrGo4` FROM `quest_template`"))
        {
            do
            {
                for (uint8 i = 0; i < 4; ++i)
                {
                    int32 id = (*result)[i].Get<int32>();
                    if (id > 0)
                        questCreatures.insert(uint32(id));
                }
            } while (result->NextRow());
        }

        // Kill credit stand-ins count as the quest creature
        for (auto const& [entry, cInfo] : *sObjectMgr->GetCreatureTemplates())
            for (uint32 credit : cInfo.KillCredit)
                if (credit && questCreatures.count(credit))
                    questCreatures.insert(entry);
    }

    std::unordered_set<ObjectGuid::LowType> scriptedSpawns;
    if (QueryResult result = WorldDatabase.Query("SELECT DISTINCT `entryorguid` FROM `smart_scripts` "
        "WHERE `entryorguid` < 0 AND `source_type` = 0"))
    {
        do
        {
            scriptedSpawns.insert(ObjectGuid::LowType(-(*result)[0].Get<int32>()));
        } while (result->NextRow());
    }

    // Every creature that spawns somewhere in the open world can be drawn
    std::set<uint32> worldEntries;
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        if (IsOpenWorldMap(data.mapid))
            worldEntries.insert(data.id);

    std::unordered_map<uint32, std::vector<uint32>> pools;
    for (uint32 entry : worldEntries)
    {
        CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(entry);
        if (!IsCombatTemplate(cInfo, alliance, horde) || IsRare(cInfo))
            continue;

        uint8 center = uint8((uint32(cInfo->minlevel) + uint32(cInfo->maxlevel)) / 2);
        for (std::size_t b = 0; b < _config.brackets.size(); ++b)
            if (center >= _config.brackets[b].levelMin && center <= _config.brackets[b].levelMax)
                pools[PoolKey(uint8(b), IsAquatic(cInfo), IsElite(cInfo))].push_back(entry);
    }

    std::unordered_map<ObjectGuid::LowType, uint32> spawnZones;
    LoadSpawnZones(spawnZones);

    for (auto const& [spawnId, constData] : sObjectMgr->GetAllCreatureData())
    {
        if (!IsOpenWorldMap(constData.mapid))
            continue;

        auto zoneItr = spawnZones.find(spawnId);
        if (zoneItr == spawnZones.end())
            continue;

        auto indexItr = _zoneIndex.find(zoneItr->second);
        if (indexItr == _zoneIndex.end())
            continue;

        RandomizerZone const& zone = _zones[indexItr->second];
        CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(constData.id);
        if (!IsCombatTemplate(cInfo, alliance, horde))
            continue;

        bool moved = zone.bracket != zone.homeBracket;
        bool keep = !_config.replaceCreatures || constData.ScriptId || scriptedSpawns.count(spawnId) ||
            questCreatures.count(constData.id) || (IsElite(cInfo) && !_config.replaceElites) ||
            (IsRare(cInfo) && !_config.replaceRares) ||
            std::find(_config.excludeEntries.begin(), _config.excludeEntries.end(), constData.id) !=
                _config.excludeEntries.end();

        std::vector<uint32> const* pool = nullptr;
        if (!keep)
        {
            bool aquatic = IsAquatic(cInfo);
            bool elite = IsElite(cInfo);
            for (bool tryElite : { elite, false })
            {
                auto poolItr = pools.find(PoolKey(zone.bracket, aquatic, tryElite));
                if (poolItr != pools.end() && !poolItr->second.empty())
                {
                    pool = &poolItr->second;
                    break;
                }
            }
        }

        if (!pool)
        {
            if (moved && _config.shiftKeptLevels)
                _spawnZone[spawnId] = uint16(indexItr->second);
            continue;
        }

        // The same original creature becomes the same random creature in a zone, so packs stay packs
        uint32 key = _seed ^ (zone.zoneId * 0x9E3779B1u) ^
            (_config.sameEntrySameReplacement ? constData.id * 0x85EBCA77u : spawnId * 0xC2B2AE3Du);
        RandomizerRng rng(key);
        rng.Next();
        uint32 replacement = (*pool)[rng.Below(uint32(pool->size()))];

        CreatureData& data = const_cast<CreatureData&>(constData);
        data.id = replacement;
        data.id2 = 0;
        data.id3 = 0;
        data.displayid = 0;      // the new template's model
        data.equipmentId = 1;    // the new template's equipment, if it has any
        data.npcflag = 0;
        data.unit_flags = 0;
        data.dynamicflags = 0;
        data.curhealth = 0;
        data.curmana = 0;
        ++_replaced;
    }
}

void WorldRandomizerMgr::ComputeDungeonShifts()
{
    _dungeonShift.clear();
    _dungeonZone.clear();
    _dungeonBase.clear();

    // The level each dungeon was made for: its LFG recommended level
    for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
        if (LFGDungeonEntry const* entry = sLFGDungeonStore.LookupEntry(i))
        {
            uint32 level = entry->TargetLevel ? entry->TargetLevel : (entry->MinLevel + entry->MaxLevel) / 2;
            if (level && !_dungeonBase.count(entry->MapID))
                _dungeonBase[entry->MapID] = uint8(level);
        }

    std::unordered_map<uint32, uint32> entranceZones;
    if (QueryResult result = WorldDatabase.Query("SELECT `map_id`, `zone_id` FROM `world_randomizer_instance`"))
    {
        do
        {
            entranceZones[(*result)[0].Get<uint32>()] = (*result)[1].Get<uint32>();
        } while (result->NextRow());
    }

    // Instance entrances: the area trigger that teleports into the instance, and the zone it stands in
    if (QueryResult result = WorldDatabase.Query("SELECT t.`target_map`, a.`map`, a.`x`, a.`y`, a.`z` "
        "FROM `areatrigger_teleport` t JOIN `areatrigger` a ON a.`entry` = t.`ID` ORDER BY t.`ID`"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 targetMap = fields[0].Get<uint16>();
            uint32 sourceMap = fields[1].Get<uint32>();

            MapEntry const* entry = sMapStore.LookupEntry(targetMap);
            if (!entry || !entry->IsDungeon() || !IsOpenWorldMap(sourceMap) || entranceZones.count(targetMap))
                continue;

            entranceZones[targetMap] = sMapMgr->GetZoneId(PHASEMASK_NORMAL, sourceMap, fields[2].Get<float>(),
                fields[3].Get<float>(), fields[4].Get<float>());
        } while (result->NextRow());
    }

    for (auto const& [mapId, zoneId] : entranceZones)
    {
        RandomizerZone const* zone = FindZone(zoneId);
        if (!zone)
            continue;

        _dungeonZone[mapId] = zoneId;
        if (zone->bracket == zone->homeBracket)
            continue;

        Bracket const& from = _config.brackets[zone->homeBracket];
        Bracket const& to = _config.brackets[zone->bracket];
        int32 delta = (int32(to.levelMin) + int32(to.levelMax)) / 2 - (int32(from.levelMin) + int32(from.levelMax)) / 2;
        if (delta)
            _dungeonShift[mapId] = delta;
    }
}

Bracket const* WorldRandomizerMgr::GetDungeonBracket(uint32 mapId) const
{
    auto itr = _dungeonZone.find(mapId);
    if (itr == _dungeonZone.end())
        return nullptr;

    RandomizerZone const* zone = FindZone(itr->second);
    return zone ? &_config.brackets[zone->bracket] : nullptr;
}

bool WorldRandomizerMgr::AdjustLevel(Map* map, ObjectGuid::LowType spawnId, uint8& level) const
{
    if (!_config.enable || !map)
        return false;

    if (map->IsDungeon())
    {
        // Collective: mod-dynamic-scaling scales dungeons inside their bracket instead
        if (_config.collective || !_config.dungeonMode)
            return false;

        if (_config.dungeonMode == 1)
        {
            auto itr = _dungeonShift.find(map->GetId());
            if (itr == _dungeonShift.end())
                return false;

            level = uint8(std::clamp<int32>(int32(level) + itr->second, 1, MAX_LEVEL));
            return true;
        }

        // Mode 2: scale to the group, inside the entrance zone's bracket. Every creature moves by the same amount,
        // so bosses stay above their trash.
        Bracket const* bracket = GetDungeonBracket(map->GetId());
        if (!bracket)
            return false;

        auto baseItr = _dungeonBase.find(map->GetId());
        int32 base = baseItr != _dungeonBase.end() ? baseItr->second : level;

        int32 reference = 0;
        for (auto const& ref : map->GetPlayers())
            if (Player* player = ref.GetSource())
                if (!player->IsGameMaster())
                    reference = std::max<int32>(reference, player->GetLevel());

        if (!reference) // nobody inside yet: the middle of the bracket
            reference = (int32(bracket->levelMin) + int32(bracket->levelMax)) / 2;

        int32 aboveBase = std::max<int32>(0, int32(level) - base);
        int32 target = int32(level) + reference - base;
        target = std::clamp<int32>(target, int32(bracket->levelMin) + aboveBase, int32(bracket->levelMax) + aboveBase);
        level = uint8(std::clamp<int32>(target, 1, MAX_LEVEL));
        return true;
    }

    if (!spawnId)
        return false;

    auto itr = _spawnZone.find(spawnId);
    if (itr == _spawnZone.end())
        return false;

    RandomizerZone const& zone = _zones[itr->second];
    Bracket const& to = _config.brackets[zone.bracket];
    level = RemapLevel(level, zone.levelMin, zone.levelMax, to.levelMin, to.levelMax);
    return true;
}

namespace
{
    // Quest's level fields are protected; member pointers taken through a derived class are the legal way in
    struct QuestFieldAccess : Quest
    {
        static constexpr int32 Quest::* Level = &QuestFieldAccess::Level;
        static constexpr uint32 Quest::* MinLevel = &QuestFieldAccess::MinLevel;
        static constexpr int32 Quest::* RewardMoney = &QuestFieldAccess::RewardMoney;
    };
}

// Quests of a zone that moved to another bracket move with its creatures: the quest level (so the XP, the reward
// money at max level and the colour in the quest log follow), the level requirement and, optionally, the reward
// money. Changed in memory only, the same for everyone; a restart with World.Enable = 0 puts them back.
void WorldRandomizerMgr::ShiftQuests()
{
    _questsShifted = 0;
    _questOriginal.clear();
    for (auto const& [questId, quest] : sObjectMgr->GetQuestTemplates())
    {
        if (quest->GetZoneOrSort() <= 0)
            continue; // class, profession and holiday quests (sorts) keep their levels

        uint32 zoneId = uint32(quest->GetZoneOrSort());
        RandomizerZone const* zone = FindZone(zoneId);
        if (!zone)
            if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId))
                if (area->zone)
                    zone = FindZone(area->zone);

        if (!zone || zone->bracket == zone->homeBracket)
            continue;

        Bracket const& to = _config.brackets[zone->bracket];
        _questOriginal[questId] = { quest->GetQuestLevel(), quest->GetMinLevel(), quest->GetRewOrReqMoney(0) };
        int32 oldLevel = quest->GetQuestLevel();
        int32 newLevel = oldLevel;
        if (oldLevel > 0) // -1 = the player's own level
        {
            newLevel = RemapLevel(uint8(std::min<int32>(oldLevel, MAX_LEVEL)), zone->levelMin, zone->levelMax,
                to.levelMin, to.levelMax);
            quest->*QuestFieldAccess::Level = newLevel;
        }

        if (quest->GetMinLevel() > 0)
        {
            // The whole zone is meant for its bracket: by default any quest there can be taken from its bottom level
            uint32 minLevel = _config.questMinLevelFromBracket ? to.levelMin :
                RemapLevel(uint8(std::min<uint32>(quest->GetMinLevel(), MAX_LEVEL)), zone->levelMin, zone->levelMax,
                    to.levelMin, to.levelMax);
            if (newLevel > 0)
                minLevel = std::min<uint32>(minLevel, uint32(newLevel));
            quest->*QuestFieldAccess::MinLevel = minLevel;
        }

        // Reward money grows with the level about as quest XP does, so it moves by the same ratio
        if (_config.shiftQuestMoney && oldLevel > 0 && newLevel != oldLevel && quest->GetRewOrReqMoney(0) > 0)
        {
            uint32 xpId = std::min<uint32>(quest->GetXPId(), 9);
            QuestXPEntry const* from = sQuestXPStore.LookupEntry(oldLevel);
            QuestXPEntry const* toXP = sQuestXPStore.LookupEntry(newLevel);
            if (from && toXP && from->Exp[xpId] > 0)
                quest->*QuestFieldAccess::RewardMoney = std::max<int32>(1, int32(int64(quest->*QuestFieldAccess::
                    RewardMoney) * toXP->Exp[xpId] / from->Exp[xpId]));
        }

        ++_questsShifted;
    }
}

QuestOriginal const* WorldRandomizerMgr::GetQuestOriginal(uint32 questId) const
{
    auto itr = _questOriginal.find(questId);
    return itr != _questOriginal.end() ? &itr->second : nullptr;
}

void WorldRandomizerMgr::PublishDungeonBrackets()
{
    // Read by mod-dynamic-scaling at OnStartup. Synchronous, so it is there before that runs.
    WorldDatabase.DirectExecute("DELETE FROM `world_randomizer_dungeon_bracket`");
    if (!_config.collective || _dungeonZone.empty())
        return;

    std::string values;
    for (auto const& [mapId, zoneId] : _dungeonZone)
    {
        RandomizerZone const* zone = FindZone(zoneId);
        Bracket const& bracket = _config.brackets[zone->bracket];
        if (!values.empty())
            values += ',';
        values += Acore::StringFormat("({},{},{},{})", mapId, zoneId, bracket.levelMin, bracket.levelMax);
    }

    WorldDatabase.DirectExecute("INSERT INTO `world_randomizer_dungeon_bracket` (`map_id`, `zone_id`, `level_min`, "
        "`level_max`) VALUES " + values);
}

void WorldRandomizerMgr::LoadStartLocations()
{
    _startLocations.clear();

    uint8 lowest = 0;
    for (std::size_t b = 1; b < _config.brackets.size(); ++b)
        if (_config.brackets[b].levelMin < _config.brackets[lowest].levelMin)
            lowest = uint8(b);

    QueryResult result = WorldDatabase.Query("SELECT `ID`, `GhostZone`, `Faction` FROM `graveyard_zone`");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        RandomizerZone const* zone = FindZone(fields[1].Get<uint32>());
        if (!zone || zone->bracket != lowest)
            continue;

        GraveyardStruct const* graveyard = sGraveyard->GetGraveyard(fields[0].Get<uint32>());
        if (!graveyard || !IsOpenWorldMap(graveyard->Map))
            continue;

        StartLocation location;
        location.mapId = graveyard->Map;
        location.zoneId = zone->zoneId;
        location.x = graveyard->x;
        location.y = graveyard->y;
        location.z = graveyard->z;

        uint32 faction = fields[2].Get<uint16>();
        location.team = faction == ALLIANCE ? TEAM_ALLIANCE : (faction == HORDE ? TEAM_HORDE : TEAM_NEUTRAL);
        _startLocations.push_back(location);
    } while (result->NextRow());
}

bool WorldRandomizerMgr::PickStartLocation(Player* player, StartLocation& out) const
{
    TeamId team = player->GetTeamId();

    auto usable = [team](StartLocation const& location)
    {
        return location.team == TEAM_NEUTRAL || location.team == team;
    };

    // 1. Zones of the player's own faction, 2. contested zones, 3. anything in bracket 1
    std::vector<StartLocation const*> tiers[3];
    for (StartLocation const& location : _startLocations)
    {
        if (!usable(location))
            continue;

        RandomizerZone const* zone = FindZone(location.zoneId);
        if (!zone)
            continue;

        if (zone->territory == uint8(team))
            tiers[0].push_back(&location);
        else if (zone->territory == 2)
            tiers[1].push_back(&location);
        else
            tiers[2].push_back(&location);
    }

    uint8 lastTier = _config.playerStartFactionLock ? 1 : 2;
    for (uint8 tier = 0; tier <= lastTier; ++tier)
    {
        if (tiers[tier].empty())
            continue;

        out = *Acore::Containers::SelectRandomContainerElement(tiers[tier]);
        return true;
    }

    return false;
}

float WorldRandomizerMgr::GetXPRate() const
{
    switch (_config.era)
    {
        case StatEra::Vanilla:
            return _config.xpRateVanilla;
        case StatEra::TBC:
            return _config.xpRateTBC;
        case StatEra::Wrath:
            return _config.xpRateWrath;
        default:
            return 1.0f;
    }
}

void WorldRandomizerMgr::ApplyStatEra()
{
    if (_statsApplied || (_config.era != StatEra::Vanilla && _config.era != StatEra::TBC))
        return;

    _statsApplied = true;
    uint8 era = uint8(_config.era);
    uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    uint8 maxRaces = sRaceMgr->GetMaxRaces();

    using Stats = std::array<int32, MAX_STATS>;
    std::unordered_map<uint32, Stats> eraStats;
    std::unordered_map<uint32, uint8> eraCap; // race/class -> highest level
    std::unordered_map<uint32, std::pair<uint32, uint32>> eraClass;
    std::unordered_map<uint8, uint8> eraClassCap;

    if (QueryResult result = WorldDatabase.Query("SELECT `race`, `class`, `level`, `str`, `agi`, `sta`, `inte`, `spi` "
        "FROM `world_randomizer_levelstats` WHERE `era` = {}", era))
    {
        do
        {
            Field* fields = result->Fetch();
            uint8 race = fields[0].Get<uint8>();
            uint8 cls = fields[1].Get<uint8>();
            uint8 level = fields[2].Get<uint8>();
            Stats stats;
            for (uint8 i = 0; i < MAX_STATS; ++i)
                stats[i] = fields[3 + i].Get<int32>();

            eraStats[StatKey(race, cls, level)] = stats;
            uint8& cap = eraCap[StatKey(race, cls, 0)];
            cap = std::max(cap, level);
        } while (result->NextRow());
    }

    if (QueryResult result = WorldDatabase.Query("SELECT `class`, `level`, `basehp`, `basemana` "
        "FROM `world_randomizer_classlevelstats` WHERE `era` = {}", era))
    {
        do
        {
            Field* fields = result->Fetch();
            uint8 cls = fields[0].Get<uint8>();
            uint8 level = fields[1].Get<uint8>();
            eraClass[StatKey(0, cls, level)] = { fields[2].Get<uint32>(), fields[3].Get<uint32>() };
            uint8& cap = eraClassCap[cls];
            cap = std::max(cap, level);
        } while (result->NextRow());
    }

    if (eraStats.empty() || eraClass.empty())
    {
        LOG_ERROR("module", "mod-world-randomizer: no stats for Randomizer.Stats.Era = {} in "
            "`world_randomizer_levelstats` / `world_randomizer_classlevelstats`.", era);
        return;
    }

    // Snapshot AzerothCore's values first: they are the reference for races / classes / levels the era lacks
    std::unordered_map<uint32, Stats> core;
    for (uint8 race = 1; race < maxRaces; ++race)
        for (uint8 cls = 1; cls < MAX_CLASSES; ++cls)
            if (PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, cls))
                if (info->levelInfo)
                    for (uint32 level = 1; level <= maxLevel; ++level)
                    {
                        Stats stats;
                        for (uint8 i = 0; i < MAX_STATS; ++i)
                            stats[i] = int32(info->levelInfo[level - 1].stats[i]);
                        core[StatKey(race, cls, uint8(level))] = stats;
                    }

    auto coreStat = [&core](uint8 race, uint8 cls, uint32 level, uint8 stat, bool& found) -> int32
    {
        auto itr = core.find(StatKey(race, cls, uint8(level)));
        found = itr != core.end();
        return found ? itr->second[stat] : 0;
    };

    uint32 changed = 0;
    for (uint8 race = 1; race < maxRaces; ++race)
    {
        for (uint8 cls = 1; cls < MAX_CLASSES; ++cls)
        {
            PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, cls);
            if (!info || !info->levelInfo)
                continue;

            // Death Knights did not exist: they borrow the Warrior's numbers
            uint8 sourceClass = cls == CLASS_DEATH_KNIGHT ? uint8(CLASS_WARRIOR) : cls;
            uint8 sourceRace = 0;
            if (eraCap.count(StatKey(race, sourceClass, 0)))
                sourceRace = race;
            else
                for (uint8 candidate : RACE_ORDER)
                    if (eraCap.count(StatKey(candidate, sourceClass, 0)))
                    {
                        sourceRace = candidate;
                        break;
                    }

            if (!sourceRace)
                continue;

            uint8 cap = eraCap[StatKey(sourceRace, sourceClass, 0)];
            PlayerLevelInfo* levels = const_cast<PlayerLevelInfo*>(info->levelInfo);

            for (uint32 level = 1; level <= maxLevel; ++level)
            {
                uint8 eraLevel = uint8(std::min<uint32>(level, cap));
                auto eraItr = eraStats.find(StatKey(sourceRace, sourceClass, eraLevel));
                if (eraItr == eraStats.end())
                    continue;

                for (uint8 i = 0; i < MAX_STATS; ++i)
                {
                    int32 value = eraItr->second[i];
                    bool found = false;

                    // A race the era lacks (or lacks for this class): add AzerothCore's race difference
                    if (sourceRace != race)
                    {
                        int32 own = coreStat(race, cls, eraLevel, i, found);
                        bool foundSource = false;
                        int32 source = coreStat(sourceRace, cls, eraLevel, i, foundSource);
                        if (found && foundSource)
                            value += own - source;
                    }

                    // Past the era's level cap: keep AzerothCore's growth
                    if (level > cap)
                    {
                        int32 now = coreStat(race, cls, level, i, found);
                        bool foundCap = false;
                        int32 atCap = coreStat(race, cls, cap, i, foundCap);
                        if (found && foundCap)
                            value += now - atCap;
                    }

                    levels[level - 1].stats[i] = uint32(std::max<int32>(1, value));
                }
            }

            ++changed;
        }
    }

    for (uint8 cls = 1; cls < MAX_CLASSES; ++cls)
    {
        PlayerClassInfo const* classInfo = sObjectMgr->GetPlayerClassInfo(cls);
        if (!classInfo || !classInfo->levelInfo)
            continue;

        uint8 sourceClass = cls == CLASS_DEATH_KNIGHT ? uint8(CLASS_WARRIOR) : cls;
        auto capItr = eraClassCap.find(sourceClass);
        if (capItr == eraClassCap.end())
            continue;

        uint8 cap = uint8(std::min<uint32>(capItr->second, maxLevel));
        PlayerClassLevelInfo* levels = const_cast<PlayerClassLevelInfo*>(classInfo->levelInfo);
        uint32 coreHealthAtCap = levels[cap - 1].basehealth;
        uint32 coreManaAtCap = levels[cap - 1].basemana;

        std::vector<PlayerClassLevelInfo> original(levels, levels + maxLevel);
        for (uint32 level = 1; level <= maxLevel; ++level)
        {
            auto itr = eraClass.find(StatKey(0, sourceClass, uint8(std::min<uint32>(level, cap))));
            if (itr == eraClass.end())
                continue;

            int64 health = itr->second.first;
            int64 mana = itr->second.second;
            if (level > cap)
            {
                health += int64(original[level - 1].basehealth) - int64(coreHealthAtCap);
                mana += int64(original[level - 1].basemana) - int64(coreManaAtCap);
            }

            levels[level - 1].basehealth = uint32(std::max<int64>(1, health));
            if (cls != CLASS_DEATH_KNIGHT && original[level - 1].basemana)
                levels[level - 1].basemana = uint32(std::max<int64>(1, mana));
        }
    }

    LOG_INFO("module", "mod-world-randomizer: player base stats set to the {} era for {} race/class pairs.",
        _config.era == StatEra::Vanilla ? "Vanilla (vMaNGOS)" : "TBC (cMaNGOS-TBC)", changed);
}
