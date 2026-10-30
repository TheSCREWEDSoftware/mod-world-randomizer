/*
 * mod-world-randomizer: account-wide achievements with rewards for every character on the account.
 *
 * A "run" is one character's life; "lifetime" counts every character of the account. Achievements and their rewards
 * are rows in `world_randomizer_achievement` / `world_randomizer_achievement_reward` (world DB), so they can be
 * changed without touching code. Progress is kept per account in the characters DB.
 */

#ifndef MOD_WORLD_RANDOMIZER_ACHIEVEMENTS_H
#define MOD_WORLD_RANDOMIZER_ACHIEVEMENTS_H

#include "Define.h"
#include <string>

#include <unordered_map>
#include <utility>
#include <vector>

class Player;

enum RandomizerAchCriteria : uint8
{
    ACH_CRITERIA_LEVEL             = 1,  // value: level, on any character
    ACH_CRITERIA_ZONES             = 2,  // value: different zones discovered (lifetime), 0 = every zone in the list
    ACH_CRITERIA_QUESTS_IN_RUN     = 3,  // value: quests completed by one character
    ACH_CRITERIA_GOLD_IN_RUN       = 4,  // value: gold held at once by one character
    ACH_CRITERIA_MAX_HEALTH        = 5,  // value: max health, on any character
    ACH_CRITERIA_DEATHS            = 6,  // value: deaths (lifetime) at or above Randomizer.Achievements.DeathMinLevel
    ACH_CRITERIA_RARES_IN_RUN      = 7,  // value: different rare creatures killed by one character
    ACH_CRITERIA_KILL_HIGHER       = 8,  // value: levels above the killer
    ACH_CRITERIA_SKILL             = 9,  // arg: skill id, value: skill value, on any character
    ACH_CRITERIA_GATHER            = 10, // arg: skill id (mining, herbalism, skinning), value: gathers (lifetime)
    ACH_CRITERIA_CRAFT             = 11, // value: items crafted (lifetime)
    ACH_CRITERIA_SPELL             = 12, // arg: spell id learned (e.g. a profession specialization)
    ACH_CRITERIA_CATEGORY          = 13  // arg: category; every other achievement of that category done
};

enum RandomizerAchReward : uint8
{
    ACH_REWARD_NONE                = 0,  // text only (not supported yet)
    ACH_REWARD_XP_PCT              = 1,  // value: +XP percent
    ACH_REWARD_LEVEL_CAP           = 2,  // value: +levels over Randomizer.Achievements.BaseLevelCap
    ACH_REWARD_TALENT_GAIN         = 3,  // value: extra talent points per level from level 10
    ACH_REWARD_BONUS_TALENTS       = 4,  // value: flat extra talent points
    ACH_REWARD_START_GOLD          = 5,  // value: gold for new characters
    ACH_REWARD_START_ITEM          = 6,  // arg: item, arg2: count, for new characters
    ACH_REWARD_GOLD_DROP_PCT       = 7,  // value: +percent of looted money
    ACH_REWARD_HEALTH_REGEN_PCT    = 8,  // value: +health regen percent
    ACH_REWARD_MANA_REGEN_PCT      = 9,  // value: +mana regen percent
    ACH_REWARD_SKILL_RATE          = 10, // arg: skill id (0 = every profession), value: extra skill-up chance (1 = +1)
    ACH_REWARD_PROFICIENCY         = 11, // arg: armor / weapon skill id usable by any class
    ACH_REWARD_LEARN_SPELL         = 12, // arg: spell, arg2: skill the character must have (0 = none)
    ACH_REWARD_PROFESSION_SLOT     = 13, // value: extra primary profession slots
    ACH_REWARD_START_PROFESSION    = 14, // arg: profession skill new characters start with
    ACH_REWARD_PROFESSION_CHOICE   = 15, // arg: profession that can be chosen with .rach profession
    ACH_REWARD_START_SKILL_BONUS   = 16, // value: + starting skill of the chosen profession
    ACH_REWARD_GATHER_MULTIPLIER   = 17, // arg: skill id (mining, herbalism, skinning), value: loot multiplier
    ACH_REWARD_SPELL_AT_LEVEL      = 18, // arg: spell, arg2: level it is learned at
    ACH_REWARD_TITLE               = 19  // arg: CharTitles.dbc ID, on every character of the account
};

// Addon messages (whisper to yourself, prefix RANDACH):
//   client -> server  "DEF"              achievement definitions
//                     "GET" / "GET\t<name>"  progress of you / a party member
//   server -> client  "C\t<category>\t<name>"
//                     "D\t<id>\t<category>\t<icon>\t<name>\t<reward text>\t<expansion mask>\t<parent id>"
//                     "T\t<id>\t<description>"
//                     "S\t<name>\t<done>\t<total>\t<deaths>\t<zones>\t<quests>\t<rares>\t<gold>\t<level cap>"
//                     "P\t<id>:<done 0/1>:<progress>:<required>,..."
//                     "E\tDEF\t<count>\t<expansion 1-3>" / "E\tGET\t<name>"   end of a reply;  "X\t<error>"
//                     "L\t<level>"  first talent point level (Randomizer.Talents.StartLevel), before a DEF reply
//   Rolls and the Group tab (co-op start, party info): see WorldRandomizerRoll.cpp and WorldRandomizerCoop.cpp
constexpr char const* RANDOMIZER_ADDON_PREFIX = "RANDACH";
// Goes up whenever the addon and the module change together; the addon's ADDON_VERSION
// (addon/RandomizerAchievements/RandomizerAchievements.lua) must match it
constexpr uint32 RANDOMIZER_ADDON_VERSION = 3;

class Player;
// WorldRandomizerRoll.cpp: ROLL / ROLLPEEK / ROLLSET requests; true when it was one
bool HandleRollAddonMessage(Player* player, std::string const& body);
// The roll this (online) character follows, as text; empty when none
std::string RandomizerRollText(Player* player);
// WorldRandomizerCoop.cpp: START / STARTSOLO / STARTCREATE / STARTJOIN / PARTY requests; true when it was one
bool HandleCoopAddonMessage(Player* player, std::string const& body);

namespace WorldRandomizerAchievements
{
    // For the hardcore fresh-start check: what achievements give a new character of this account
    std::vector<std::pair<uint32, uint32>> GetStartItems(uint32 accountId);
    uint32 GetStartMoney(uint32 accountId);
}

void AddWorldRandomizerAchievementScripts();

#endif // MOD_WORLD_RANDOMIZER_ACHIEVEMENTS_H
