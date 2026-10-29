-- mod-world-randomizer: characters tied to their roll from the web roller (.randomizer roll <code> <player>).
-- Talents only go into the rolled tree and trainers only teach the rolled primary professions.
CREATE TABLE IF NOT EXISTS `world_randomizer_roll` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
    `name` VARCHAR(64) NOT NULL DEFAULT '' COMMENT 'The player''s name in the roller',
    `race` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `class` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `spec` TINYINT NOT NULL DEFAULT -1 COMMENT 'Talent tab 0-2, -1 = not rolled',
    `prof1` TINYINT NOT NULL DEFAULT -1 COMMENT 'Roller profession index (0 Alchemy ... 10 Tailoring), -1 = none',
    `prof2` TINYINT NOT NULL DEFAULT -1,
    `set_at` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: characters tied to a roll';
