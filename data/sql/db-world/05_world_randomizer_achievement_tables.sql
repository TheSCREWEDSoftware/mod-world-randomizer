-- mod-world-randomizer: achievement definitions (world DB). Criteria / reward type numbers:
-- see src/WorldRandomizerAchievements.h. `icon` is a texture name from Interface\Icons (shown by the addon).
-- Only achievements whose `expansion` matches Randomizer.Achievements.Expansion count; the addon shows the other
-- versions (same `parent_id`) as alternatives.
CREATE TABLE IF NOT EXISTS `world_randomizer_achievement` (
    `id` INT UNSIGNED NOT NULL,
    `category` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `category_name` VARCHAR(64) NOT NULL DEFAULT '',
    `name` VARCHAR(128) NOT NULL DEFAULT '',
    `description` VARCHAR(255) NOT NULL DEFAULT '',
    `icon` VARCHAR(64) NOT NULL DEFAULT 'INV_Misc_QuestionMark',
    `expansion` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Content it is for: 1 Vanilla, 2 TBC, 4 Wrath (add up), 0 = all',
    `parent_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Versions of one achievement share a parent; 0 or own id = the base',
    `criteria` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `value` INT UNSIGNED NOT NULL DEFAULT 0,
    `arg` INT UNSIGNED NOT NULL DEFAULT 0,
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: achievements';

CREATE TABLE IF NOT EXISTS `world_randomizer_achievement_reward` (
    `achievement_id` INT UNSIGNED NOT NULL,
    `idx` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `type` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = text only',
    `value` FLOAT NOT NULL DEFAULT 0,
    `arg` INT UNSIGNED NOT NULL DEFAULT 0,
    `arg2` INT UNSIGNED NOT NULL DEFAULT 0,
    `text` VARCHAR(128) NOT NULL DEFAULT '',
    PRIMARY KEY (`achievement_id`, `idx`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: achievement rewards';

-- Server side auras for the regen rewards: SPELL_AURA_MOD_HEALTH_REGEN_PERCENT (88) and
-- SPELL_AURA_MOD_POWER_REGEN_PERCENT (110, mana). The module sets the amount.
DELETE FROM `spell_dbc` WHERE `ID` IN (150110, 150111);
INSERT INTO `spell_dbc` (`ID`, `Attributes`, `Effect_1`, `EffectAura_1`, `EffectMiscValue_1`, `Name_Lang_enUS`, `ProcChance`, `RangeIndex`, `EquippedItemClass`) VALUES
(150110, 448, 6, 88, 0, 'Randomizer - Health Regen', 101, 1, -1),
(150111, 448, 6, 110, 0, 'Randomizer - Mana Regen', 101, 1, -1);
