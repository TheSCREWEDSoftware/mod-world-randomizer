/*
 * mod-world-randomizer: items that follow the shuffle.
 *
 * A 3.3.5 client shows one level per item, so an item can't be scaled for one player: instead it is swapped for a
 * similar item of the right level (same class and subclass, same slot, same quality), taken from items that really
 * drop or are rewarded somewhere.
 *   - Quest rewards of shifted quests (Randomizer.Quests.SwapRewards), once at startup, the same for everyone.
 *   - Loot of creatures whose level moved (kept creatures of moved zones, and creatures mod-dynamic-scaling scaled):
 *     weapons and armor far from the creature's level are swapped when it dies (Randomizer.Loot.Swap).
 */

#include "WorldRandomizer.h"

#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace
{
    // Weapons and armor by class, subclass, slot (robes count as chest) and quality
    using ItemKey = std::tuple<uint32, uint32, uint32, uint32>;

    struct PoolItem
    {
        uint32 entry;
        uint32 level; // required level, or the item level's rough equivalent when it has none
    };

    uint32 SlotOf(ItemTemplate const* proto)
    {
        return proto->InventoryType == INVTYPE_ROBE ? uint32(INVTYPE_CHEST) : proto->InventoryType;
    }

    ItemKey KeyOf(ItemTemplate const* proto)
    {
        return { proto->Class, proto->SubClass, SlotOf(proto), proto->Quality };
    }

    uint32 LevelOf(ItemTemplate const* proto)
    {
        if (proto->RequiredLevel)
            return proto->RequiredLevel;
        // No requirement (most low level items): about item level - 5 below 60
        return proto->ItemLevel > 5 ? std::min<uint32>(proto->ItemLevel - 5, 80) : 1;
    }

    bool Swappable(ItemTemplate const* proto)
    {
        if (!proto || (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR))
            return false;
        if (proto->Quality > ITEM_QUALITY_EPIC || !proto->ItemLevel || proto->InventoryType == INVTYPE_NON_EQUIP)
            return false;
        if (proto->StartQuest || proto->HasFlag(ITEM_FLAG_UNIQUE_EQUIPPABLE))
            return false;
        // Shirts, tabards and the like carry no level
        if (proto->InventoryType == INVTYPE_BODY || proto->InventoryType == INVTYPE_TABARD)
            return false;
        return true;
    }

    bool LooksReal(ItemTemplate const* proto)
    {
        std::string const& name = proto->Name1;
        // Anywhere in the name
        static char const* const BAD[] = { "Test ", "TEST", "Deprecated", "DEPRECATED", "Monster -", "[PH]",
            "Unused", "UNUSED" };
        for (char const* bad : BAD)
            if (name.find(bad) != std::string::npos)
                return false;
        // At the start only ("zzOLD...", "OLD ..."), so "Grizzly" or "of the BOLD" stay
        static char const* const PREFIXES[] = { "zz", "ZZ", "OLD", "QA ", "Test" };
        for (char const* prefix : PREFIXES)
            if (name.compare(0, std::strlen(prefix), prefix) == 0)
                return false;
        return true;
    }

    uint32 Mix(uint32 a, uint32 b)
    {
        uint32 h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        return h;
    }
}

class ItemSwapMgr
{
public:
    static ItemSwapMgr* instance()
    {
        static ItemSwapMgr mgr;
        return &mgr;
    }

    // Two pools: quest rewards (for quest rewards) and items that drop (for loot)
    void Load()
    {
        _rewards.clear();
        _drops.clear();

        std::unordered_set<uint32> rewardItems;
        for (auto const& [id, quest] : sObjectMgr->GetQuestTemplates())
        {
            for (uint32 item : quest->RewardItemId)
                if (item)
                    rewardItems.insert(item);
            for (uint32 item : quest->RewardChoiceItemId)
                if (item)
                    rewardItems.insert(item);
        }

        std::unordered_set<uint32> dropItems;
        for (char const* table : { "creature_loot_template", "reference_loot_template", "gameobject_loot_template" })
            if (QueryResult result = WorldDatabase.Query("SELECT DISTINCT `Item` FROM `{}` WHERE `Reference` = 0",
                table))
                do
                    dropItems.insert(result->Fetch()[0].Get<uint32>());
                while (result->NextRow());

        auto fill = [](std::unordered_set<uint32> const& items, std::map<ItemKey, std::vector<PoolItem>>& pool)
        {
            for (uint32 entry : items)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
                if (Swappable(proto) && LooksReal(proto))
                    pool[KeyOf(proto)].push_back({ entry, LevelOf(proto) });
            }
            for (auto& [key, list] : pool)
                std::sort(list.begin(), list.end(), [](PoolItem const& a, PoolItem const& b)
                    { return a.level != b.level ? a.level < b.level : a.entry < b.entry; });
        };
        fill(rewardItems, _rewards);
        fill(dropItems, _drops);

        LOG_INFO("module", "mod-world-randomizer: item swaps from {} quest reward and {} dropped weapons and armor.",
            rewardItems.size(), dropItems.size());
    }

    // A similar item near targetLevel; the same item when nothing fits better. seed picks among equally close ones.
    uint32 Swap(uint32 entry, uint32 targetLevel, uint32 seed, bool reward) const
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
        if (!Swappable(proto))
            return entry;

        auto const& pool = reward ? _rewards : _drops;
        auto itr = pool.find(KeyOf(proto));
        if (itr == pool.end())
            return entry;

        uint32 bestDistance = UINT32_MAX;
        std::vector<uint32> best;
        for (PoolItem const& item : itr->second)
        {
            // Class restricted items (a paladin-only belt) only stand in for items with the same restriction
            ItemTemplate const* candidate = sObjectMgr->GetItemTemplate(item.entry);
            if (candidate->AllowableClass != proto->AllowableClass && int32(candidate->AllowableClass) != -1)
                continue;
            // Nor race or faction restricted ones, unless the original had the same restriction
            if (candidate->AllowableRace != proto->AllowableRace && int32(candidate->AllowableRace) != -1)
                continue;
            uint32 factionFlags = ITEM_FLAG2_FACTION_HORDE | ITEM_FLAG2_FACTION_ALLIANCE;
            uint32 candidateFaction = candidate->Flags2 & factionFlags;
            if (candidateFaction && candidateFaction != (proto->Flags2 & factionFlags))
                continue;

            uint32 distance = uint32(std::abs(int32(item.level) - int32(targetLevel)));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best.clear();
            }
            if (distance == bestDistance)
                best.push_back(item.entry);
        }

        uint32 ownDistance = uint32(std::abs(int32(LevelOf(proto)) - int32(targetLevel)));
        if (best.empty() || ownDistance <= bestDistance)
            return entry;

        return best[Mix(seed, KeyHash(proto)) % best.size()];
    }

private:
    static uint32 KeyHash(ItemTemplate const* proto)
    {
        return Mix(Mix(proto->Class, proto->SubClass), Mix(SlotOf(proto), proto->Quality));
    }

    std::map<ItemKey, std::vector<PoolItem>> _rewards;
    std::map<ItemKey, std::vector<PoolItem>> _drops;
};

#define sItemSwap ItemSwapMgr::instance()

void LoadRandomizerItemSwaps()
{
    sItemSwap->Load();
}

// A quest reward of a shifted quest: its level moves the way the quest's zone moved
uint32 RandomizerSwapRewardItem(uint32 entry, uint8 fromMin, uint8 fromMax, uint8 toMin, uint8 toMax, uint32 seed)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
    if (!Swappable(proto))
        return entry;

    uint32 target = WorldRandomizerMgr::RemapLevel(uint8(std::min<uint32>(LevelOf(proto), MAX_LEVEL)), fromMin,
        fromMax, toMin, toMax);
    return sItemSwap->Swap(entry, target, seed, true);
}

class RandomizerItemUnitScript : public UnitScript
{
public:
    RandomizerItemUnitScript() : UnitScript("RandomizerItemUnitScript", true, { UNITHOOK_ON_UNIT_DEATH }) { }

    // The loot was just filled (Unit::Kill), before anyone opens it or a group roll starts
    void OnUnitDeath(Unit* unit, Unit* /*killer*/) override
    {
        Creature* creature = unit ? unit->ToCreature() : nullptr;
        RandomizerConfig const& config = sWorldRandomizer->Config();
        if (!creature || !config.lootSwap || creature->loot.items.empty() || !creature->GetMap() ||
            creature->GetMap()->Instanceable())
            return;

        // Only creatures whose level moved: kept creatures of moved zones, or scaled by mod-dynamic-scaling
        CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
        uint8 level = creature->GetLevel();
        bool moved = sWorldRandomizer->IsShiftedSpawn(creature->GetSpawnId()) || level < cInfo->minlevel ||
            level > cInfo->maxlevel;
        if (!moved)
            return;

        uint32 index = 0;
        for (LootItem& item : creature->loot.items)
        {
            ++index;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item.itemid);
            if (!proto || (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR))
                continue;

            uint32 itemLevel = LevelOf(proto);
            if (uint32(std::abs(int32(itemLevel) - int32(level))) <= config.lootSwapMargin)
                continue;

            uint32 swapped = sItemSwap->Swap(item.itemid, level, Mix(creature->GetGUID().GetCounter(), index), false);
            if (swapped == item.itemid)
                continue;

            item.itemid = swapped;
            item.randomPropertyId = Item::GenerateItemRandomPropertyId(swapped);
            item.randomSuffix = GenerateEnchSuffixFactor(swapped);
        }
    }
};

void AddWorldRandomizerItemScripts()
{
    new RandomizerItemUnitScript();
}
