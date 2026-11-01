/*
 * mod-world-randomizer: account-wide achievements. See WorldRandomizerAchievements.h for the criteria and rewards.
 */

#include "WorldRandomizerAchievements.h"
#include "WorldRandomizer.h"

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Group.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{

    struct RandomizerRewardDef
    {
        uint8 type = ACH_REWARD_NONE;
        float value = 0.0f;
        uint32 arg = 0;
        uint32 arg2 = 0;
        std::string text;
    };

    struct RandomizerAchDef
    {
        uint32 id = 0;
        uint8 category = 0;
        std::string categoryName;
        std::string name;
        std::string description;
        std::string icon;
        uint8 expansionMask = 0; // 1 Vanilla, 2 TBC, 4 Wrath, 0 = every expansion
        uint32 parentId = 0;     // versions of the same achievement share a parent (0 / own id = the base)
        bool active = true;      // made for the server's current content
        uint8 criteria = 0;
        uint32 value = 0;
        uint32 arg = 0;
        std::vector<RandomizerRewardDef> rewards;
    };

    // Everything an account's achievements add up to
    struct RandomizerPerks
    {
        float xpPct = 0.0f;
        uint32 levelCap = 0;
        float talentGain = 0.0f;
        uint32 bonusTalents = 0;
        uint32 startGold = 0;
        float goldDropPct = 0.0f;
        float healthRegenPct = 0.0f;
        float manaRegenPct = 0.0f;
        std::unordered_map<uint32, float> skillRate; // skill 0 = every profession
        std::vector<uint32> proficiencies;
        std::vector<std::pair<uint32, uint32>> learnSpells;   // spell, required skill
        uint32 professionSlots = 0;
        std::vector<uint32> startProfessions;
        std::vector<uint32> professionChoices;
        uint32 startSkillBonus = 0;
        std::unordered_map<uint32, float> gatherMultiplier;
        std::vector<std::pair<uint32, uint32>> levelSpells;   // spell, level
        std::vector<std::pair<uint32, uint32>> startItems;    // item, count
        std::vector<uint32> titles;
    };

    struct RandomizerAccountState
    {
        std::unordered_set<uint32> done;
        std::unordered_map<std::string, uint32> counters;
        std::unordered_set<uint32> zones;
        uint32 chosenProfession = 0;
        RandomizerPerks perks;
    };

    struct RandomizerRunState
    {
        uint32 quests = 0;
        uint32 maxMoney = 0;
        std::unordered_set<uint32> rares;
    };

    struct Profession
    {
        uint32 skill;
        uint32 spell; // Apprentice
        char const* name;
    };

    constexpr Profession PROFESSIONS[] =
    {
        { 171, 2259, "Alchemy" },       { 164, 2018, "Blacksmithing" }, { 333, 7411, "Enchanting" },
        { 202, 4036, "Engineering" },   { 182, 2366, "Herbalism" },     { 186, 2575, "Mining" },
        { 393, 8613, "Skinning" },      { 197, 3908, "Tailoring" },     { 165, 2108, "Leatherworking" },
        { 755, 25229, "Jewelcrafting" }, { 773, 45357, "Inscription" }, { 129, 3273, "First Aid" },
        { 185, 2550, "Cooking" },       { 356, 7620, "Fishing" }
    };

    Profession const* FindProfession(uint32 skill)
    {
        for (Profession const& profession : PROFESSIONS)
            if (profession.skill == skill)
                return &profession;
        return nullptr;
    }

    // Armor / weapon skill -> item class and subclass, for "usable by any class"
    bool ProficiencyFor(uint32 skill, ItemClass& itemClass, uint32& subclass)
    {
        static std::unordered_map<uint32, std::pair<ItemClass, uint32>> const map =
        {
            { SKILL_PLATE_MAIL, { ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_PLATE } },
            { SKILL_MAIL, { ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_MAIL } },
            { SKILL_LEATHER, { ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_LEATHER } },
            { SKILL_SHIELD, { ITEM_CLASS_ARMOR, ITEM_SUBCLASS_ARMOR_SHIELD } },
            { SKILL_AXES, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_AXE } },
            { SKILL_2H_AXES, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_AXE2 } },
            { SKILL_BOWS, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_BOW } },
            { SKILL_GUNS, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_GUN } },
            { SKILL_MACES, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_MACE } },
            { SKILL_2H_MACES, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_MACE2 } },
            { SKILL_POLEARMS, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_POLEARM } },
            { SKILL_SWORDS, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_SWORD } },
            { SKILL_2H_SWORDS, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_SWORD2 } },
            { SKILL_STAVES, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_STAFF } },
            { SKILL_FIST_WEAPONS, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_FIST } },
            { SKILL_DAGGERS, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_DAGGER } },
            { SKILL_THROWN, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_THROWN } },
            { SKILL_CROSSBOWS, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_CROSSBOW } },
            { SKILL_WANDS, { ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_WAND } }
        };

        auto itr = map.find(skill);
        if (itr == map.end())
            return false;

        itemClass = itr->second.first;
        subclass = itr->second.second;
        return true;
    }

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
        return text;
    }
}

class RandomizerAchievementMgr
{
public:
    static RandomizerAchievementMgr* instance()
    {
        static RandomizerAchievementMgr instance;
        return &instance;
    }

    bool enable = false;
    bool announce = true;
    uint32 baseLevelCap = 0;
    uint32 deathMinLevel = 10;
    bool groupCredit = true;
    uint32 healthRegenSpell = 150110;
    uint32 manaRegenSpell = 150111;
    uint8 expansionSetting = 0;
    uint8 expansion = 3; // 1 Vanilla, 2 TBC, 3 Wrath
    bool challengeOnly = true;

    void LoadConfig()
    {
        enable = sWorldRandomizer->Config().moduleEnable &&
            sConfigMgr->GetOption<bool>("Randomizer.Achievements.Enable", true);
        announce = enable && sConfigMgr->GetOption<bool>("Randomizer.Achievements.Announce", true);
        baseLevelCap = sConfigMgr->GetOption<uint32>("Randomizer.Achievements.BaseLevelCap", 0);
        deathMinLevel = sConfigMgr->GetOption<uint32>("Randomizer.Achievements.DeathMinLevel", 10);
        groupCredit = sConfigMgr->GetOption<bool>("Randomizer.Achievements.GroupCredit", true);
        healthRegenSpell = sConfigMgr->GetOption<uint32>("Randomizer.Achievements.HealthRegenSpell", 150110);
        manaRegenSpell = sConfigMgr->GetOption<uint32>("Randomizer.Achievements.ManaRegenSpell", 150111);

        expansionSetting = std::min<uint8>(3, sConfigMgr->GetOption<uint8>("Randomizer.Achievements.Expansion", 0));
        challengeOnly = sConfigMgr->GetOption<bool>("Randomizer.Achievements.ChallengeOnly", true);
        std::lock_guard lock(_challengeLock);
        _challenge.clear();
    }

    // Only characters doing the challenge make progress (ChallengeOnly): ones that chose how to start (alone or in
    // a co-op party), tied a roll, or are on a hardcore run. With none of those features on, no character could
    // start, so then every character counts. Looked up once per character and kept; ChallengeStarted() turns it on.
    bool InChallenge(Player* player)
    {
        if (!challengeOnly)
            return true;

        RandomizerConfig const& config = sWorldRandomizer->Config();
        bool rollEnable = sConfigMgr->GetOption<bool>("Randomizer.Roll.Enable", true);
        if (!config.coopEnable && !rollEnable && !config.hardcoreMode)
            return true;

        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        {
            std::lock_guard lock(_challengeLock);
            auto itr = _challenge.find(guid);
            if (itr != _challenge.end())
                return itr->second;
        }

        bool yes = !!CharacterDatabase.Query(
            "SELECT 1 FROM `world_randomizer_start_choice` WHERE `guid` = {0} UNION ALL "
            "SELECT 1 FROM `world_randomizer_coop_member` WHERE `guid` = {0} UNION ALL "
            "SELECT 1 FROM `world_randomizer_roll` WHERE `guid` = {0} UNION ALL "
            "SELECT 1 FROM `world_randomizer_hardcore` WHERE `guid` = {0} LIMIT 1", guid);

        std::lock_guard lock(_challengeLock);
        _challenge[guid] = yes;
        return yes;
    }

    void SetInChallenge(Player* player)
    {
        std::lock_guard lock(_challengeLock);
        _challenge[player->GetGUID().GetCounter()] = true;
    }

    void Load()
    {
        // The content achievements are made for: its own setting, or the stats era, or Wrath. Worked out here, once
        // every module's config is loaded.
        expansion = expansionSetting;
        if (!expansion)
        {
            StatEra era = sWorldRandomizer->Config().era;
            expansion = era == StatEra::Unchanged ? 3 : uint8(era);
        }

        _defs.clear();
        _order.clear();
        _byCriteria.clear();
        _zoneList.clear();

        //                                                   0     1             2                3       4
        QueryResult result = WorldDatabase.Query("SELECT `id`, `category`, `category_name`, `name`, `criteria`, "
            //   5        6      7              8       9            10
            "`value`, `arg`, `description`, `icon`, `expansion`, `parent_id` FROM `world_randomizer_achievement` "
            "WHERE `enabled` = 1 ORDER BY `category`, `id`");
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                RandomizerAchDef def;
                def.id = fields[0].Get<uint32>();
                def.category = fields[1].Get<uint8>();
                def.categoryName = fields[2].Get<std::string>();
                def.name = fields[3].Get<std::string>();
                def.criteria = fields[4].Get<uint8>();
                def.value = fields[5].Get<uint32>();
                def.arg = fields[6].Get<uint32>();
                def.description = fields[7].Get<std::string>();
                def.icon = fields[8].Get<std::string>();
                def.expansionMask = fields[9].Get<uint8>();
                def.parentId = fields[10].Get<uint32>();
                def.active = !def.expansionMask || (def.expansionMask & (1 << (expansion - 1)));
                _order.push_back(def.id);
                _defs[def.id] = def;
            } while (result->NextRow());
        }

        if (QueryResult rewards = WorldDatabase.Query("SELECT `achievement_id`, `type`, `value`, `arg`, `arg2`, `text` "
            "FROM `world_randomizer_achievement_reward` ORDER BY `achievement_id`, `idx`"))
        {
            do
            {
                Field* fields = rewards->Fetch();
                auto itr = _defs.find(fields[0].Get<uint32>());
                if (itr == _defs.end())
                    continue;

                RandomizerRewardDef reward;
                reward.type = fields[1].Get<uint8>();
                reward.value = fields[2].Get<float>();
                reward.arg = fields[3].Get<uint32>();
                reward.arg2 = fields[4].Get<uint32>();
                reward.text = fields[5].Get<std::string>();
                itr->second.rewards.push_back(reward);
            } while (rewards->NextRow());
        }

        _activeCount = 0;
        for (auto const& [id, def] : _defs)
        {
            if (!def.active)
                continue;

            _byCriteria[def.criteria].push_back(id);
            ++_activeCount;
        }

        if (QueryResult zones = WorldDatabase.Query(
            "SELECT `zone_id` FROM `world_randomizer_zone` WHERE `enabled` = 1"))
        {
            do
            {
                _zoneList.insert(zones->Fetch()[0].Get<uint32>());
            } while (zones->NextRow());
        }

        for (uint32 spell : { healthRegenSpell, manaRegenSpell })
            if (!sSpellMgr->GetSpellInfo(spell))
                LOG_ERROR("module", "mod-world-randomizer: achievement aura spell {} is missing from `spell_dbc`.",
                    spell);

        static char const* const EXPANSIONS[] = { "", "Vanilla", "TBC", "Wrath" };
        LOG_INFO("module", "mod-world-randomizer: {} achievements loaded, {} for {}.", _defs.size(), _activeCount,
            EXPANSIONS[expansion]);
    }

    // ---- State ----

    RandomizerAccountState& Account(uint32 accountId)
    {
        auto itr = _accounts.find(accountId);
        if (itr != _accounts.end())
            return itr->second;

        RandomizerAccountState& state = _accounts[accountId];
        if (QueryResult result = CharacterDatabase.Query("SELECT `achievement_id` FROM `world_randomizer_ach_done` "
            "WHERE `account` = {}", accountId))
            do
            {
                state.done.insert(result->Fetch()[0].Get<uint32>());
            } while (result->NextRow());

        if (QueryResult result = CharacterDatabase.Query("SELECT `counter`, `value` "
            "FROM `world_randomizer_ach_counter` WHERE `account` = {}", accountId))
            do
            {
                state.counters[result->Fetch()[0].Get<std::string>()] = result->Fetch()[1].Get<uint32>();
            } while (result->NextRow());

        if (QueryResult result = CharacterDatabase.Query("SELECT `zone_id` FROM `world_randomizer_ach_zone` "
            "WHERE `account` = {}", accountId))
            do
            {
                state.zones.insert(result->Fetch()[0].Get<uint32>());
            } while (result->NextRow());

        if (QueryResult result = CharacterDatabase.Query("SELECT `start_profession` FROM `world_randomizer_ach_choice` "
            "WHERE `account` = {}", accountId))
            state.chosenProfession = result->Fetch()[0].Get<uint32>();

        RecomputePerks(state);
        return state;
    }

    RandomizerAccountState& Account(Player const* player)
    {
        return Account(player->GetSession()->GetAccountId());
    }

    RandomizerRunState& Run(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        auto itr = _runs.find(guid);
        if (itr != _runs.end())
            return itr->second;

        RandomizerRunState& run = _runs[guid];
        if (QueryResult result = CharacterDatabase.Query("SELECT `counter`, `value` FROM `world_randomizer_ach_run` "
            "WHERE `guid` = {}", guid))
            do
            {
                std::string counter = result->Fetch()[0].Get<std::string>();
                uint32 value = result->Fetch()[1].Get<uint32>();
                if (counter == "quests")
                    run.quests = value;
                else if (counter == "money")
                    run.maxMoney = value;
            } while (result->NextRow());

        if (QueryResult result = CharacterDatabase.Query("SELECT `entry` FROM `world_randomizer_ach_rare` "
            "WHERE `guid` = {}", guid))
            do
            {
                run.rares.insert(result->Fetch()[0].Get<uint32>());
            } while (result->NextRow());

        return run;
    }

    void Forget(Player* player)
    {
        _runs.erase(player->GetGUID().GetCounter());
        std::lock_guard lock(_challengeLock);
        _challenge.erase(player->GetGUID().GetCounter());
    }

    void AddCounter(Player* player, std::string const& counter, uint32 amount = 1)
    {
        if (!InChallenge(player))
            return;

        RandomizerAccountState& state = Account(player);
        uint32& value = state.counters[counter];
        value += amount;
        CharacterDatabase.Execute("REPLACE INTO `world_randomizer_ach_counter` (`account`, `counter`, `value`) VALUES "
            "({}, '{}', {})", player->GetSession()->GetAccountId(), counter, value);
    }

    void SetRun(Player* player, char const* counter, uint32 value)
    {
        if (!InChallenge(player))
            return;

        CharacterDatabase.Execute("REPLACE INTO `world_randomizer_ach_run` (`guid`, `counter`, `value`) VALUES "
            "({}, '{}', {})", player->GetGUID().GetCounter(), counter, value);
    }

    bool AddZone(Player* player, uint32 zoneId)
    {
        if (!_zoneList.count(zoneId) || !InChallenge(player))
            return false;

        RandomizerAccountState& state = Account(player);
        if (!state.zones.insert(zoneId).second)
            return false;

        CharacterDatabase.Execute("INSERT IGNORE INTO `world_randomizer_ach_zone` (`account`, `zone_id`) "
            "VALUES ({}, {})", player->GetSession()->GetAccountId(), zoneId);
        return true;
    }

    // ---- Achievements ----

    uint32 Progress(Player* player, RandomizerAchDef const& def, uint32 extra)
    {
        RandomizerAccountState& state = Account(player);
        switch (def.criteria)
        {
            case ACH_CRITERIA_LEVEL:
                return player->GetLevel();
            case ACH_CRITERIA_ZONES:
                return uint32(state.zones.size());
            case ACH_CRITERIA_QUESTS_IN_RUN:
                return Run(player).quests;
            case ACH_CRITERIA_GOLD_IN_RUN:
                return Run(player).maxMoney / GOLD;
            case ACH_CRITERIA_MAX_HEALTH:
                return player->GetMaxHealth();
            case ACH_CRITERIA_DEATHS:
                return state.counters["deaths"];
            case ACH_CRITERIA_RARES_IN_RUN:
                return uint32(Run(player).rares.size());
            case ACH_CRITERIA_KILL_HIGHER:
                return extra;
            case ACH_CRITERIA_SKILL:
                return player->GetPureSkillValue(def.arg);
            case ACH_CRITERIA_GATHER:
                return state.counters[Acore::StringFormat("gather_{}", def.arg)];
            case ACH_CRITERIA_CRAFT:
                return state.counters["craft"];
            case ACH_CRITERIA_SPELL:
                return player->HasSpell(def.arg) ? def.value : 0;
            case ACH_CRITERIA_CATEGORY:
            {
                uint32 total = 0;
                uint32 done = 0;
                for (auto const& [id, other] : _defs)
                {
                    if (!other.active || other.category != def.arg || other.criteria == ACH_CRITERIA_CATEGORY)
                        continue;
                    ++total;
                    done += state.done.count(id);
                }
                return total && done == total ? def.value : 0;
            }
            default:
                return 0;
        }
    }

    uint32 Required(RandomizerAchDef const& def) const
    {
        if (def.criteria == ACH_CRITERIA_ZONES && !def.value)
            return uint32(_zoneList.size());
        return std::max<uint32>(1, def.value);
    }

    void Evaluate(Player* player, uint8 criteria, uint32 arg = 0, uint32 extra = 0)
    {
        // Not while the character loads (the core sets skills and spells then): login runs EvaluateAll instead
        if (!enable || !player || !player->GetSession() || !player->IsInWorld() || !InChallenge(player))
            return;

        auto itr = _byCriteria.find(criteria);
        if (itr == _byCriteria.end())
            return;

        RandomizerAccountState& state = Account(player);
        for (uint32 id : itr->second)
        {
            RandomizerAchDef const& def = _defs[id];
            if (state.done.count(id) || (arg && def.arg && def.arg != arg))
                continue;

            if (Progress(player, def, extra) >= Required(def))
                Complete(player, def);
        }
    }

    void EvaluateAll(Player* player)
    {
        for (uint8 criteria : { ACH_CRITERIA_LEVEL, ACH_CRITERIA_ZONES, ACH_CRITERIA_QUESTS_IN_RUN,
            ACH_CRITERIA_GOLD_IN_RUN, ACH_CRITERIA_MAX_HEALTH, ACH_CRITERIA_DEATHS, ACH_CRITERIA_RARES_IN_RUN,
            ACH_CRITERIA_SKILL, ACH_CRITERIA_GATHER, ACH_CRITERIA_CRAFT, ACH_CRITERIA_SPELL })
            Evaluate(player, criteria);
    }

    void Complete(Player* player, RandomizerAchDef const& def)
    {
        uint32 accountId = player->GetSession()->GetAccountId();
        RandomizerAccountState& state = Account(accountId);
        if (!state.done.insert(def.id).second)
            return;

        CharacterDatabase.Execute("INSERT IGNORE INTO `world_randomizer_ach_done` (`account`, `achievement_id`, "
            "`guid`, `done_at`) VALUES ({}, {}, {}, {})", accountId, def.id, player->GetGUID().GetCounter(),
            GameTime::GetGameTime().count());

        RecomputePerks(state);

        // The addon shows its achievement popup (it ignores this when the player turned popups off)
        std::string rewards;
        for (RandomizerRewardDef const& reward : def.rewards)
            rewards += (rewards.empty() ? "" : ", ") + reward.text;
        SendAddon(player, Acore::StringFormat("A\t{}\t{}\t{}\t{}", def.id, Clean(def.icon), Clean(def.name),
            Clean(rewards)));

        if (announce)
        {
            ChatHandler handler(player->GetSession());
            handler.PSendSysMessage("|cffFFD100[Achievement]|r {}", def.name);
            for (RandomizerRewardDef const& reward : def.rewards)
                handler.PSendSysMessage("  |cff4CFF00{}|r{}", reward.text,
                    reward.type == ACH_REWARD_NONE ? " (not active yet)" : "");
        }

        ApplyLivePerks(player);

        // Finishing this may finish a whole category
        Evaluate(player, ACH_CRITERIA_CATEGORY, def.category);
    }

    void RecomputePerks(RandomizerAccountState& state) const
    {
        RandomizerPerks perks;
        for (uint32 id : state.done)
        {
            auto itr = _defs.find(id);
            if (itr == _defs.end() || !itr->second.active)
                continue;

            for (RandomizerRewardDef const& reward : itr->second.rewards)
            {
                switch (reward.type)
                {
                    case ACH_REWARD_XP_PCT:            perks.xpPct += reward.value; break;
                    case ACH_REWARD_LEVEL_CAP:         perks.levelCap += uint32(reward.value); break;
                    case ACH_REWARD_TALENT_GAIN:       perks.talentGain += reward.value; break;
                    case ACH_REWARD_BONUS_TALENTS:     perks.bonusTalents += uint32(reward.value); break;
                    case ACH_REWARD_START_GOLD:        perks.startGold += uint32(reward.value); break;
                    case ACH_REWARD_START_ITEM:
                        perks.startItems.emplace_back(reward.arg, std::max<uint32>(1, reward.arg2));
                        break;
                    case ACH_REWARD_GOLD_DROP_PCT:     perks.goldDropPct += reward.value; break;
                    case ACH_REWARD_HEALTH_REGEN_PCT:  perks.healthRegenPct += reward.value; break;
                    case ACH_REWARD_MANA_REGEN_PCT:    perks.manaRegenPct += reward.value; break;
                    case ACH_REWARD_SKILL_RATE:        perks.skillRate[reward.arg] += reward.value; break;
                    case ACH_REWARD_PROFICIENCY:       perks.proficiencies.push_back(reward.arg); break;
                    case ACH_REWARD_LEARN_SPELL:       perks.learnSpells.emplace_back(reward.arg, reward.arg2); break;
                    case ACH_REWARD_PROFESSION_SLOT:   perks.professionSlots += uint32(reward.value); break;
                    case ACH_REWARD_START_PROFESSION:  perks.startProfessions.push_back(reward.arg); break;
                    case ACH_REWARD_PROFESSION_CHOICE: perks.professionChoices.push_back(reward.arg); break;
                    case ACH_REWARD_START_SKILL_BONUS: perks.startSkillBonus += uint32(reward.value); break;
                    case ACH_REWARD_GATHER_MULTIPLIER:
                    {
                        float& multiplier = perks.gatherMultiplier[reward.arg];
                        multiplier = std::max(multiplier, reward.value);
                        break;
                    }
                    case ACH_REWARD_SPELL_AT_LEVEL:    perks.levelSpells.emplace_back(reward.arg, reward.arg2); break;
                    case ACH_REWARD_TITLE:             perks.titles.push_back(reward.arg); break;
                    default: break;
                }
            }
        }

        const_cast<RandomizerAccountState&>(state).perks = perks;
    }

    // ---- Applying perks ----

    void ApplyLivePerks(Player* player)
    {
        RandomizerPerks const& perks = Account(player).perks;

        ApplyAura(player, healthRegenSpell, perks.healthRegenPct);
        ApplyAura(player, manaRegenSpell, perks.manaRegenPct);

        for (uint32 skill : perks.proficiencies)
            GrantProficiency(player, skill);

        for (auto const& [spell, skill] : perks.learnSpells)
            if ((!skill || player->HasSkill(skill)) && !player->HasSpell(spell) && sSpellMgr->GetSpellInfo(spell))
                player->learnSpell(spell);

        for (auto const& [spell, level] : perks.levelSpells)
            if (player->GetLevel() >= level && !player->HasSpell(spell) && sSpellMgr->GetSpellInfo(spell))
                player->learnSpell(spell);

        for (uint32 titleId : perks.titles)
            if (CharTitlesEntry const* title = sCharTitlesStore.LookupEntry(titleId))
                if (!player->HasTitle(title))
                    player->SetTitle(title);

        player->InitTalentForLevel();

        // Bonus talent points can come before level 10: tell the addon to open the talent frame
        if (player->IsInWorld())
            SendAddon(player, Acore::StringFormat("L\t{}", TalentFrameLevel(player)));
    }

    // The level the addon opens the talent frame from: 1 with bonus talent points from achievements, else
    // Randomizer.Talents.StartLevel (10 = the client's own)
    uint32 TalentFrameLevel(Player* player)
    {
        uint32 level = sWorldRandomizer->Config().talentStartLevel;
        if (enable && Account(player).perks.bonusTalents)
            level = 1;
        return level;
    }

    void ApplyAura(Player* player, uint32 spell, float amount)
    {
        if (!spell || !sSpellMgr->GetSpellInfo(spell))
            return;

        if (amount <= 0.0f)
        {
            player->RemoveAurasDueToSpell(spell);
            return;
        }

        if (!player->HasAura(spell))
            player->AddAura(spell, player);

        if (AuraEffect* effect = player->GetAuraEffect(spell, EFFECT_0))
            effect->ChangeAmount(int32(amount));
    }

    void RemoveAuras(Player* player)
    {
        player->RemoveAurasDueToSpell(healthRegenSpell);
        player->RemoveAurasDueToSpell(manaRegenSpell);
    }

    // The core drops skills a race / class may not have (SkillRaceClassInfo.dbc) when it loads a character, so
    // they are given here on every login, with their saved value
    void GrantProficiency(Player* player, uint32 skill)
    {
        ItemClass itemClass;
        uint32 subclass;
        if (!ProficiencyFor(skill, itemClass, subclass))
            return;

        if (!player->HasSkill(skill))
        {
            uint16 value = 1;
            uint16 max = itemClass == ITEM_CLASS_WEAPON ? uint16(player->GetLevel() * 5) : 1;
            auto saved = _savedSkills.find({ player->GetGUID().GetCounter(), skill });
            if (saved != _savedSkills.end())
                value = std::min<uint16>(saved->second, max);
            player->SetSkill(uint16(skill), 0, value, max);
        }

        if (itemClass == ITEM_CLASS_WEAPON)
        {
            player->AddWeaponProficiency(1 << subclass);
            player->SendProficiency(ITEM_CLASS_WEAPON, player->GetWeaponProficiency());
        }
        else
        {
            player->AddArmorProficiency(1 << subclass);
            player->SendProficiency(ITEM_CLASS_ARMOR, player->GetArmorProficiency());
        }
    }

    void LoadSavedSkills(Player* player)
    {
        if (QueryResult result = CharacterDatabase.Query("SELECT `skill`, `value` FROM `world_randomizer_ach_skill` "
            "WHERE `guid` = {}", player->GetGUID().GetCounter()))
            do
            {
                _savedSkills[{ player->GetGUID().GetCounter(), result->Fetch()[0].Get<uint32>() }] =
                    result->Fetch()[1].Get<uint16>();
            } while (result->NextRow());
    }

    void SaveOffClassSkills(Player* player)
    {
        for (uint32 skill : Account(player).perks.proficiencies)
        {
            if (!player->HasSkill(skill) || GetSkillRaceClassInfo(skill, player->getRace(), player->getClass()))
                continue;

            uint16 value = player->GetPureSkillValue(skill);
            CharacterDatabase.Execute("REPLACE INTO `world_randomizer_ach_skill` (`guid`, `skill`, `value`) VALUES "
                "({}, {}, {})", player->GetGUID().GetCounter(), skill, value);
            _savedSkills[{ player->GetGUID().GetCounter(), skill }] = value;
        }
    }

    bool HasShieldPerk(Player const* player)
    {
        RandomizerPerks const& perks = Account(player).perks;
        return std::find(perks.proficiencies.begin(), perks.proficiencies.end(), uint32(SKILL_SHIELD)) !=
            perks.proficiencies.end();
    }

    void GiveStartRewards(Player* player)
    {
        RandomizerAccountState& state = Account(player);
        RandomizerPerks const& perks = state.perks;
        ChatHandler handler(player->GetSession());

        std::vector<std::string> given; // said in chat, so it's clear what the achievements gave (or didn't)
        if (perks.startGold)
        {
            player->ModifyMoney(int32(perks.startGold * GOLD));
            given.push_back(Acore::StringFormat("{} gold", perks.startGold));
        }

        for (auto const& [item, count] : perks.startItems)
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item))
            {
                if (player->AddItem(item, count))
                    given.push_back(count > 1 ? Acore::StringFormat("{} x{}", proto->Name1, count) : proto->Name1);
                else
                    given.push_back(proto->Name1 + " (no room in your bags)");
            }

        std::vector<uint32> professions = perks.startProfessions;
        if (state.chosenProfession && std::find(perks.professionChoices.begin(), perks.professionChoices.end(),
            state.chosenProfession) != perks.professionChoices.end())
            professions.push_back(state.chosenProfession);

        for (uint32 skill : professions)
        {
            Profession const* profession = FindProfession(skill);
            if (!profession || player->HasSkill(skill))
                continue;

            player->learnSpell(profession->spell);
            if (player->HasSkill(skill))
                given.push_back(profession->name);
            if (skill == state.chosenProfession && perks.startSkillBonus && player->HasSkill(skill))
            {
                uint16 max = player->GetMaxSkillValue(skill);
                player->SetSkill(uint16(skill), player->GetSkillStep(uint16(skill)),
                    uint16(std::min<uint32>(1 + perks.startSkillBonus, max)), max);
            }
        }

        if (state.done.empty())
            return;

        std::string list;
        for (std::string const& what : given)
            list += (list.empty() ? "" : ", ") + what;
        if (list.empty())
            handler.PSendSysMessage("|cffFFD100[Achievements]|r {} achievements done on this account; none of them "
                "gives a new character anything to start with (gold, items or a profession).", DoneCount(state));
        else
            handler.PSendSysMessage("|cffFFD100[Achievements]|r From your account's achievements, this character "
                "starts with: {}.", list);
    }

    void SetChoice(Player* player, uint32 skill)
    {
        RandomizerAccountState& state = Account(player);
        state.chosenProfession = skill;
        CharacterDatabase.Execute("REPLACE INTO `world_randomizer_ach_choice` (`account`, `start_profession`) VALUES "
            "({}, {})", player->GetSession()->GetAccountId(), skill);
    }

    std::vector<uint32> const& Order() const { return _order; }
    RandomizerAchDef const* Find(uint32 id) const
    {
        auto itr = _defs.find(id);
        return itr != _defs.end() ? &itr->second : nullptr;
    }

    // Achievements for the current content, and how many of them an account has done
    std::size_t Count() const { return _activeCount; }
    std::size_t DoneCount(RandomizerAccountState const& state) const
    {
        std::size_t done = 0;
        for (uint32 id : state.done)
        {
            auto itr = _defs.find(id);
            done += itr != _defs.end() && itr->second.active;
        }
        return done;
    }

    // ---- Addon ----

    static void SendAddon(Player* player, std::string const& body)
    {
        std::string text = std::string(RANDOMIZER_ADDON_PREFIX) + "\t" + body;
        if (text.size() > 254)
            text.resize(254);

        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, text);
        player->SendDirectMessage(&data);
    }

    static std::string Clean(std::string text)
    {
        std::replace(text.begin(), text.end(), '\t', ' ');
        return text;
    }

    void SendDefinitions(Player* player)
    {
        uint8 lastCategory = 0;
        for (uint32 id : _order)
        {
            RandomizerAchDef const& def = _defs[id];
            if (def.category != lastCategory)
            {
                SendAddon(player, Acore::StringFormat("C\t{}\t{}", def.category, Clean(def.categoryName)));
                lastCategory = def.category;
            }

            std::string rewards;
            for (RandomizerRewardDef const& reward : def.rewards)
                rewards += (rewards.empty() ? "" : ", ") + reward.text;

            SendAddon(player, Acore::StringFormat("D\t{}\t{}\t{}\t{}\t{}\t{}\t{}", def.id, def.category,
                Clean(def.icon), Clean(def.name), Clean(rewards), def.expansionMask, def.parentId));
            if (!def.description.empty())
                SendAddon(player, Acore::StringFormat("T\t{}\t{}", def.id, Clean(def.description)));
        }

        SendAddon(player, Acore::StringFormat("E\tDEF\t{}\t{}", _order.size(), expansion));
    }

    void SendProgress(Player* player, Player* target)
    {
        RandomizerAccountState& state = Account(target);
        RandomizerRunState& run = Run(target);
        uint32 cap = baseLevelCap ? baseLevelCap + state.perks.levelCap : 0;

        SendAddon(player, Acore::StringFormat("S\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", target->GetName(),
            DoneCount(state), _activeCount, state.counters["deaths"], state.zones.size(), run.quests,
            run.rares.size(), run.maxMoney / GOLD, cap));

        std::string chunk;
        for (uint32 id : _order)
        {
            RandomizerAchDef const& def = _defs[id];
            if (!def.active)
                continue;

            bool done = state.done.count(id) != 0;
            uint32 required = Required(def);
            uint32 progress = done ? required : std::min(Progress(target, def, 0), required);
            std::string entry = Acore::StringFormat("{}:{}:{}:{}", id, done ? 1 : 0, progress, required);

            if (chunk.size() + entry.size() + 1 > 230)
            {
                SendAddon(player, "P\t" + chunk);
                chunk.clear();
            }
            chunk += (chunk.empty() ? "" : ",") + entry;
        }

        if (!chunk.empty())
            SendAddon(player, "P\t" + chunk);

        SendAddon(player, "E\tGET\t" + target->GetName());
    }

    // Returns true when the message was for this module (and must not be passed on)
    bool HandleAddonMessage(Player* player, std::string const& msg)
    {
        std::string prefix = std::string(RANDOMIZER_ADDON_PREFIX) + "\t";
        if (msg.compare(0, prefix.size(), prefix) != 0)
            return false;

        // Module off: the addon turns itself off on this message
        if (!sWorldRandomizer->Config().moduleEnable)
        {
            SendAddon(player, "X\tThe World Randomizer is off on this server.");
            return true;
        }

        // Rolls from the web roller (WorldRandomizerRoll.cpp) and the Group tab (WorldRandomizerCoop.cpp) work whether
        // achievements are on or not
        if (HandleRollAddonMessage(player, msg.substr(prefix.size())) ||
            HandleCoopAddonMessage(player, msg.substr(prefix.size())) ||
            HandleHardcoreAddonMessage(player, msg.substr(prefix.size())))
            return true;

        // The level of the first talent point, so the addon can open the talent frame before level 10, and the
        // addon version this module expects (older addons send "DEF" with no version)
        if (msg.compare(prefix.size(), 3, "DEF") == 0)
        {
            SendAddon(player, Acore::StringFormat("L\t{}", TalentFrameLevel(player)));
            SendAddon(player, Acore::StringFormat("V\t{}", RANDOMIZER_ADDON_VERSION));
            uint32 version = msg.size() > prefix.size() + 4 ?
                uint32(std::max(0, std::atoi(msg.c_str() + prefix.size() + 4))) : 1;
            if (version != RANDOMIZER_ADDON_VERSION)
                ChatHandler(player->GetSession()).PSendSysMessage("|cffFF8000[World Randomizer]|r Your "
                    "RandomizerAchievements addon is version {}, this server expects version {}: {} it from "
                    "https://github.com/TheSCREWEDSoftware/mod-world-randomizer/releases/tag/v{}", version,
                    RANDOMIZER_ADDON_VERSION,
                    version < RANDOMIZER_ADDON_VERSION ? "update" : "use the matching version of",
                    RANDOMIZER_ADDON_VERSION);
        }

        if (!enable)
        {
            SendAddon(player, "X\tAchievements are off on this server.");
            return true;
        }

        std::string body = msg.substr(prefix.size());
        if (body == "DEF" || body.compare(0, 4, "DEF\t") == 0)
        {
            SendDefinitions(player);
            return true;
        }

        if (body.compare(0, 3, "GET") == 0)
        {
            Player* target = player;
            if (body.size() > 4)
            {
                // A party / raid member, by name
                std::string name = body.substr(4);
                target = nullptr;
                if (Group* group = player->GetGroup())
                    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
                        if (Player* member = itr->GetSource())
                            if (Lower(member->GetName()) == Lower(name))
                                target = member;

                if (!target)
                {
                    SendAddon(player, "X\t" + name + " is not in your group or not online.");
                    return true;
                }
            }

            SendProgress(player, target);
            return true;
        }

        SendAddon(player, "X\tUnknown request.");
        return true;
    }

    // Gathering loot already multiplied, so reopening the window doesn't multiply again
    bool MarkLoot(ObjectGuid guid)
    {
        uint32 now = uint32(GameTime::GetGameTime().count());
        auto itr = _multipliedLoot.find(guid);
        if (itr != _multipliedLoot.end() && now - itr->second < 120)
            return false;

        _multipliedLoot[guid] = now;
        if (_multipliedLoot.size() > 4096)
            _multipliedLoot.clear();
        return true;
    }

private:
    std::unordered_map<uint32, RandomizerAchDef> _defs;
    std::vector<uint32> _order;
    std::size_t _activeCount = 0;
    std::unordered_map<uint8, std::vector<uint32>> _byCriteria;
    std::unordered_set<uint32> _zoneList;
    std::unordered_map<uint32, RandomizerAccountState> _accounts;
    std::unordered_map<ObjectGuid::LowType, RandomizerRunState> _runs;
    std::unordered_map<ObjectGuid::LowType, bool> _challenge; // doing the challenge, per character (InChallenge)
    std::mutex _challengeLock;
    std::map<std::pair<ObjectGuid::LowType, uint32>, uint16> _savedSkills;
    std::unordered_map<ObjectGuid, uint32> _multipliedLoot;
};

#define sRandomizerAch RandomizerAchievementMgr::instance()

std::vector<std::pair<uint32, uint32>> WorldRandomizerAchievements::GetStartItems(uint32 accountId)
{
    if (!sRandomizerAch->enable)
        return {};
    return sRandomizerAch->Account(accountId).perks.startItems;
}

uint32 WorldRandomizerAchievements::GetStartMoney(uint32 accountId)
{
    if (!sRandomizerAch->enable)
        return 0;
    return sRandomizerAch->Account(accountId).perks.startGold * GOLD;
}

bool WorldRandomizerAchievements::CountsForAchievements(Player* player)
{
    return player && sRandomizerAch->InChallenge(player);
}

void WorldRandomizerAchievements::ChallengeStarted(Player* player)
{
    if (!player)
        return;

    sRandomizerAch->SetInChallenge(player);
    if (sRandomizerAch->enable)
        sRandomizerAch->EvaluateAll(player);
}

class AchievementWorldScript : public WorldScript
{
public:
    AchievementWorldScript() : WorldScript("AchievementWorldScript",
        { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        if (!reload)
            sRandomizerAch->LoadConfig();
    }

    void OnBeforeWorldInitialized() override
    {
        if (sRandomizerAch->enable)
            sRandomizerAch->Load();
    }
};

class AchievementPlayerScript : public PlayerScript
{
public:
    AchievementPlayerScript() : PlayerScript("AchievementPlayerScript", {
        PLAYERHOOK_ON_FIRST_LOGIN,
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_BEFORE_LOGOUT,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_LEVEL_CHANGED,
        PLAYERHOOK_ON_UPDATE_ZONE,
        PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
        PLAYERHOOK_ON_MONEY_CHANGED,
        PLAYERHOOK_ON_PLAYER_JUST_DIED,
        PLAYERHOOK_ON_CREATURE_KILL,
        PLAYERHOOK_ON_CREATURE_KILLED_BY_PET,
        PLAYERHOOK_ON_UPDATE_SKILL,
        PLAYERHOOK_ON_SET_SKILL,
        PLAYERHOOK_ON_UPDATE_GATHERING_SKILL,
        PLAYERHOOK_ON_UPDATE_CRAFTING_SKILL,
        PLAYERHOOK_ON_LEARN_SPELL,
        PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_CALCULATE_TALENTS_POINTS,
        PLAYERHOOK_ON_BEFORE_LOOT_MONEY,
        PLAYERHOOK_ON_BEFORE_SEND_LOOT,
        PLAYERHOOK_ON_PLAYER_IS_CLASS,
        PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT
    }) { }

    using PlayerScript::OnPlayerCanUseChat;

    // The addon whispers its requests to the player themself
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || language != LANG_ADDON || receiver != player)
            return true;

        return !sRandomizerAch->HandleAddonMessage(player, msg);
    }

    void OnPlayerFirstLogin(Player* player) override
    {
        if (sRandomizerAch->enable)
            sRandomizerAch->GiveStartRewards(player);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!sRandomizerAch->enable)
            return;

        sRandomizerAch->LoadSavedSkills(player);

        // The core resets free profession slots at load: add the achievement ones back
        if (uint32 slots = sRandomizerAch->Account(player).perks.professionSlots)
            player->SetFreePrimaryProfessions(uint16(player->GetFreePrimaryProfessionPoints() + slots));

        sRandomizerAch->ApplyLivePerks(player);
        sRandomizerAch->AddZone(player, player->GetZoneId());
        sRandomizerAch->EvaluateAll(player);

        if (sRandomizerAch->announce && sWorldRandomizer->Config().announce)
        {
            RandomizerAccountState& state = sRandomizerAch->Account(player);
            ChatHandler(player->GetSession()).PSendSysMessage("  Achievements: {}/{} done on this account (.rach)",
                sRandomizerAch->DoneCount(state), sRandomizerAch->Count());
        }
    }

    void OnPlayerBeforeLogout(Player* player) override
    {
        if (!sRandomizerAch->enable)
            return;

        sRandomizerAch->SaveOffClassSkills(player);
        sRandomizerAch->RemoveAuras(player); // before the save, so the auras are never stored
    }

    void OnPlayerLogout(Player* player) override
    {
        sRandomizerAch->Forget(player);
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (!sRandomizerAch->enable)
            return;

        sRandomizerAch->Evaluate(player, ACH_CRITERIA_LEVEL);
        sRandomizerAch->Evaluate(player, ACH_CRITERIA_MAX_HEALTH);
        sRandomizerAch->ApplyLivePerks(player); // spells at a level, talent points
    }

    void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 /*newArea*/) override
    {
        if (!sRandomizerAch->enable)
            return;

        if (sRandomizerAch->AddZone(player, newZone))
            sRandomizerAch->Evaluate(player, ACH_CRITERIA_ZONES);
        sRandomizerAch->Evaluate(player, ACH_CRITERIA_MAX_HEALTH);
    }

    void OnPlayerCompleteQuest(Player* player, Quest const* /*quest*/) override
    {
        if (!sRandomizerAch->enable || !sRandomizerAch->InChallenge(player))
            return;

        RandomizerRunState& run = sRandomizerAch->Run(player);
        sRandomizerAch->SetRun(player, "quests", ++run.quests);
        sRandomizerAch->Evaluate(player, ACH_CRITERIA_QUESTS_IN_RUN);
    }

    void OnPlayerMoneyChanged(Player* player, int32& amount) override
    {
        if (!sRandomizerAch->enable || amount <= 0 || !sRandomizerAch->InChallenge(player))
            return;

        RandomizerRunState& run = sRandomizerAch->Run(player);
        uint32 money = player->GetMoney() + uint32(amount);
        if (money <= run.maxMoney)
            return;

        // Only write when a whole gold more is held
        bool newGold = money / GOLD > run.maxMoney / GOLD;
        run.maxMoney = money;
        if (newGold)
        {
            sRandomizerAch->SetRun(player, "money", money);
            sRandomizerAch->Evaluate(player, ACH_CRITERIA_GOLD_IN_RUN);
        }
    }

    void OnPlayerJustDied(Player* player) override
    {
        if (!sRandomizerAch->enable || player->GetLevel() < sRandomizerAch->deathMinLevel)
            return;

        sRandomizerAch->AddCounter(player, "deaths");
        sRandomizerAch->Evaluate(player, ACH_CRITERIA_DEATHS);
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        OnKill(killer, killed);
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        OnKill(owner, killed);
    }

    void OnPlayerUpdateSkill(Player* player, uint32 skillId, uint32 /*value*/, uint32 max, uint32 step,
        uint32 newValue) override
    {
        if (!sRandomizerAch->enable)
            return;

        // Skill rate: a chance at one more point (a rate of 1 or more always gives the whole part)
        RandomizerPerks const& perks = sRandomizerAch->Account(player).perks;
        float rate = 0.0f;
        auto itr = perks.skillRate.find(skillId);
        if (itr != perks.skillRate.end())
            rate += itr->second;

        SkillLineEntry const* line = sSkillLineStore.LookupEntry(skillId);
        if (line && line->categoryId == SKILL_CATEGORY_PROFESSION)
        {
            auto all = perks.skillRate.find(0);
            if (all != perks.skillRate.end())
                rate += all->second;
        }

        uint32 extra = uint32(rate);
        if (roll_chance_f((rate - float(extra)) * 100.0f))
            ++extra;

        if (extra && newValue < max)
            player->SetSkill(uint16(skillId), uint16(step), uint16(std::min<uint32>(newValue + extra, max)),
                uint16(max));

        sRandomizerAch->Evaluate(player, ACH_CRITERIA_SKILL, skillId);
    }

    void OnPlayerSetSkill(Player* player, uint32 skillId, uint32 /*value*/, uint32 /*max*/, uint32 /*step*/,
        uint32 /*newValue*/) override
    {
        if (!sRandomizerAch->enable)
            return;

        sRandomizerAch->Evaluate(player, ACH_CRITERIA_SKILL, skillId);
    }

    void OnPlayerUpdateGatheringSkill(Player* player, uint32 skillId, uint32 /*current*/, uint32 /*gray*/,
        uint32 /*green*/, uint32 /*yellow*/, uint32& /*gain*/) override
    {
        if (!sRandomizerAch->enable)
            return;

        sRandomizerAch->AddCounter(player, Acore::StringFormat("gather_{}", skillId));
        sRandomizerAch->Evaluate(player, ACH_CRITERIA_GATHER, skillId);
    }

    void OnPlayerUpdateCraftingSkill(Player* player, SkillLineAbilityEntry const* /*skill*/, uint32 /*currentLevel*/,
        uint32& /*gain*/) override
    {
        if (!sRandomizerAch->enable)
            return;

        sRandomizerAch->AddCounter(player, "craft");
        sRandomizerAch->Evaluate(player, ACH_CRITERIA_CRAFT);
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        if (!sRandomizerAch->enable || !player->IsInWorld())
            return;

        sRandomizerAch->Evaluate(player, ACH_CRITERIA_SPELL, spellId);

        // A profession was learned: specializations that needed it can come in now
        for (Profession const& profession : PROFESSIONS)
            if (profession.spell == spellId)
            {
                sRandomizerAch->ApplyLivePerks(player);
                break;
            }
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
    {
        if (!sRandomizerAch->enable || !amount)
            return;

        RandomizerPerks const& perks = sRandomizerAch->Account(player).perks;

        // Level cap: base cap plus the achievement levels
        if (sRandomizerAch->baseLevelCap)
        {
            uint32 cap = std::min<uint32>(sRandomizerAch->baseLevelCap + perks.levelCap,
                sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
            if (player->GetLevel() >= cap)
            {
                amount = 0;
                return;
            }
        }

        if (perks.xpPct > 0.0f)
            amount = uint32(float(amount) * (1.0f + perks.xpPct / 100.0f));
    }

    void OnPlayerCalculateTalentsPoints(Player const* player, uint32& talentPointsForLevel) override
    {
        if (!sRandomizerAch->enable || !player->GetSession())
            return;

        RandomizerPerks const& perks = sRandomizerAch->Account(player).perks;
        uint32 levels = player->GetLevel() > 9 ? player->GetLevel() - 9 : 0;
        talentPointsForLevel += uint32(float(levels) * perks.talentGain) + perks.bonusTalents;
    }

    void OnPlayerBeforeLootMoney(Player* player, Loot* loot) override
    {
        if (!sRandomizerAch->enable || !loot || !loot->gold)
            return;

        float pct = sRandomizerAch->Account(player).perks.goldDropPct;
        if (pct > 0.0f)
            loot->gold = uint32(float(loot->gold) * (1.0f + pct / 100.0f));
    }

    void OnPlayerBeforeSendLoot(Player* player, ObjectGuid lootGuid, Loot* loot) override
    {
        if (!sRandomizerAch->enable || !loot)
            return;

        RandomizerPerks const& perks = sRandomizerAch->Account(player).perks;
        if (perks.gatherMultiplier.empty())
            return;

        uint32 skill = 0;
        if (loot->loot_type == LOOT_SKINNING)
            skill = SKILL_SKINNING;
        else if (lootGuid.IsGameObject())
        {
            if (GameObject* go = player->GetMap()->GetGameObject(lootGuid))
                if (LockEntry const* lock = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId()))
                    for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
                        if (lock->Type[i] == LOCK_KEY_SKILL)
                        {
                            if (lock->Index[i] == LOCKTYPE_MINING)
                                skill = SKILL_MINING;
                            else if (lock->Index[i] == LOCKTYPE_HERBALISM)
                                skill = SKILL_HERBALISM;
                        }
        }

        auto itr = perks.gatherMultiplier.find(skill);
        if (!skill || itr == perks.gatherMultiplier.end() || itr->second <= 1.0f || !sRandomizerAch->MarkLoot(lootGuid))
            return;

        for (LootItem& item : loot->items)
        {
            uint32 maxStack = 1;
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item.itemid))
                maxStack = std::max<uint32>(1, proto->GetMaxStackSize());
            item.count = std::min<uint32>(uint32(float(item.count) * itr->second), maxStack);
        }
    }

    Optional<bool> OnPlayerIsClass(Player const* player, Classes /*playerClass*/, ClassContext context) override
    {
        // "Shields usable by any class"
        if (sRandomizerAch->enable && context == CLASS_CONTEXT_EQUIP_SHIELDS && player->GetSession() &&
            sRandomizerAch->HasShieldPerk(player))
            return true;
        return std::nullopt;
    }

private:
    // The killer, and with GroupCredit everyone in the group close enough to share the kill's XP and loot
    void OnKill(Player* player, Creature* killed)
    {
        if (!sRandomizerAch->enable || !player || !killed)
            return;

        Group* group = sRandomizerAch->groupCredit ? player->GetGroup() : nullptr;
        if (!group)
        {
            CreditKill(player, killed);
            return;
        }

        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            if (Player* member = itr->GetSource())
                if (member == player || (member->IsInMap(killed) && member->IsAtGroupRewardDistance(killed)))
                    CreditKill(member, killed);
    }

    void CreditKill(Player* player, Creature* killed)
    {
        if (!sRandomizerAch->InChallenge(player))
            return;

        CreatureTemplate const* cInfo = killed->GetCreatureTemplate();
        if (cInfo && (cInfo->rank == CREATURE_ELITE_RARE || cInfo->rank == CREATURE_ELITE_RAREELITE))
        {
            RandomizerRunState& run = sRandomizerAch->Run(player);
            if (run.rares.insert(killed->GetEntry()).second)
            {
                CharacterDatabase.Execute("INSERT IGNORE INTO `world_randomizer_ach_rare` (`guid`, `entry`) VALUES "
                    "({}, {})", player->GetGUID().GetCounter(), killed->GetEntry());
                sRandomizerAch->Evaluate(player, ACH_CRITERIA_RARES_IN_RUN);
            }
        }

        if (killed->GetLevel() > player->GetLevel())
            sRandomizerAch->Evaluate(player, ACH_CRITERIA_KILL_HIGHER, 0, killed->GetLevel() - player->GetLevel());
    }
};

class AchievementCommandScript : public CommandScript
{
public:
    AchievementCommandScript() : CommandScript("AchievementCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable rachTable =
        {
            { "",           HandleSummaryCommand,    SEC_PLAYER, Console::No },
            { "list",       HandleListCommand,       SEC_PLAYER, Console::No },
            { "profession", HandleProfessionCommand, SEC_PLAYER, Console::No }
        };

        static ChatCommandTable commandTable =
        {
            { "rach", rachTable }
        };

        return commandTable;
    }

    static bool CheckEnabled(ChatHandler* handler)
    {
        if (sRandomizerAch->enable)
            return true;

        handler->SendErrorMessage("Achievements are off on this server.");
        return false;
    }

    static bool HandleSummaryCommand(ChatHandler* handler)
    {
        if (!CheckEnabled(handler))
            return false;

        Player* player = handler->GetSession()->GetPlayer();
        RandomizerAccountState& state = sRandomizerAch->Account(player);
        RandomizerPerks const& p = state.perks;

        handler->PSendSysMessage("|cffFFD100Achievements|r: {}/{} done on this account. .rach list [category] shows "
            "them.", sRandomizerAch->DoneCount(state), sRandomizerAch->Count());
        handler->SendSysMessage("Your account's rewards, on every character:");
        if (p.xpPct > 0.0f)
            handler->PSendSysMessage("  XP +{:.0f}%", p.xpPct);
        if (sRandomizerAch->baseLevelCap)
            handler->PSendSysMessage("  Level cap {} ({} + {})", sRandomizerAch->baseLevelCap + p.levelCap,
                sRandomizerAch->baseLevelCap, p.levelCap);
        if (p.talentGain > 0.0f || p.bonusTalents)
            handler->PSendSysMessage("  Talent points +{:.1f} per level from 10, +{} extra", p.talentGain,
                p.bonusTalents);
        if (p.startGold)
            handler->PSendSysMessage("  New characters start with {} gold", p.startGold);
        if (!p.startItems.empty())
            handler->PSendSysMessage("  New characters start with {} reward item stacks", p.startItems.size());
        if (p.goldDropPct > 0.0f)
            handler->PSendSysMessage("  Looted money +{:.0f}%", p.goldDropPct);
        if (p.healthRegenPct > 0.0f || p.manaRegenPct > 0.0f)
            handler->PSendSysMessage("  Health regen +{:.0f}%, mana regen +{:.0f}%", p.healthRegenPct,
                p.manaRegenPct);
        if (!p.proficiencies.empty())
            handler->PSendSysMessage("  {} armor / weapon types usable by any class", p.proficiencies.size());
        if (p.professionSlots)
            handler->PSendSysMessage("  +{} profession slots", p.professionSlots);
        if (!p.professionChoices.empty())
        {
            Profession const* chosen = FindProfession(state.chosenProfession);
            handler->PSendSysMessage("  Starting profession: {} (.rach profession to choose){}",
                chosen ? chosen->name : "none chosen",
                p.startSkillBonus ? Acore::StringFormat(", +{} starting skill", p.startSkillBonus) : "");
        }
        return true;
    }

    static bool HandleListCommand(ChatHandler* handler, Optional<uint32> category)
    {
        if (!CheckEnabled(handler))
            return false;

        RandomizerAccountState& state = sRandomizerAch->Account(handler->GetSession()->GetPlayer());
        uint8 lastCategory = 0;
        for (uint32 id : sRandomizerAch->Order())
        {
            RandomizerAchDef const* def = sRandomizerAch->Find(id);
            if (!def || !def->active || (category && def->category != *category))
                continue;

            if (def->category != lastCategory)
            {
                handler->PSendSysMessage("|cffFFD100{} ({})|r", def->categoryName, def->category);
                lastCategory = def->category;
            }

            std::string rewards;
            for (RandomizerRewardDef const& reward : def->rewards)
                rewards += (rewards.empty() ? "" : ", ") + reward.text;

            handler->PSendSysMessage("  {} {} - {}", state.done.count(id) ? "|cff4CFF00[x]|r" : "[ ]", def->name,
                rewards);
        }

        if (!category)
            handler->SendSysMessage("Tip: .rach list <category number> shows one category.");
        return true;
    }

    static bool HandleProfessionCommand(ChatHandler* handler, Optional<std::string> name)
    {
        if (!CheckEnabled(handler))
            return false;

        Player* player = handler->GetSession()->GetPlayer();
        RandomizerAccountState& state = sRandomizerAch->Account(player);
        std::vector<uint32> const& choices = state.perks.professionChoices;
        if (choices.empty())
        {
            handler->SendSysMessage("No starting profession unlocked yet (reach skill 150 in a profession).");
            return true;
        }

        if (!name)
        {
            std::string list;
            for (uint32 skill : choices)
                if (Profession const* profession = FindProfession(skill))
                    list += (list.empty() ? "" : ", ") + std::string(profession->name);
            Profession const* chosen = FindProfession(state.chosenProfession);
            handler->PSendSysMessage("Unlocked starting professions: {}. Chosen: {}. New characters start with it. "
                "Use .rach profession <name>.", list, chosen ? chosen->name : "none");
            return true;
        }

        for (uint32 skill : choices)
        {
            Profession const* profession = FindProfession(skill);
            if (profession && Lower(profession->name) == Lower(*name))
            {
                sRandomizerAch->SetChoice(player, skill);
                handler->PSendSysMessage("New characters on this account will start with {}.", profession->name);
                return true;
            }
        }

        handler->SendErrorMessage("That profession is not unlocked as a starting profession.");
        return false;
    }
};

void AddWorldRandomizerAchievementScripts()
{
    new AchievementWorldScript();
    new AchievementPlayerScript();
    new AchievementCommandScript();
}
