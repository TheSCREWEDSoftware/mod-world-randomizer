-- mod-world-randomizer: realm state (the world seed).
CREATE TABLE IF NOT EXISTS `world_randomizer_state` (
    `key` VARCHAR(32) NOT NULL,
    `value` VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: realm state';

-- Hardcore: every hardcore character, its owner account and how it died. Never deleted by the module.
CREATE TABLE IF NOT EXISTS `world_randomizer_hardcore` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
    `account` INT UNSIGNED NOT NULL COMMENT 'Owner account (kept even after a soft delete)',
    `name` VARCHAR(12) NOT NULL DEFAULT '',
    `race` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `class` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `gender` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `level` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Level at death (or when it went hardcore)',
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 alive, 1 dead',
    `soft_deleted` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = unlinked from its account (core deleteInfos_*)',
    `revive_pending` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = restored, comes back alive on next login',
    `revives` INT UNSIGNED NOT NULL DEFAULT 0,
    `started_at` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix time',
    `died_at` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix time',
    `death_map` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `death_zone` INT UNSIGNED NOT NULL DEFAULT 0,
    `killer_type` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 world (fall, drowning...), 1 creature, 2 player',
    `killer_entry` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'creature_template.entry',
    `killer_name` VARCHAR(100) NOT NULL DEFAULT '',
    `seed` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'World seed of the run',
    PRIMARY KEY (`guid`),
    KEY `idx_name` (`name`),
    KEY `idx_account` (`account`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: hardcore characters';

CREATE TABLE IF NOT EXISTS `world_randomizer_hardcore_revive` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
    `revived_at` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix time',
    `revived_by` VARCHAR(32) NOT NULL DEFAULT '' COMMENT 'GM name, console, or core',
    `reason` VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`id`),
    KEY `idx_guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: hardcore restores (bugged deaths)';
