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

-- Co-op: parties made with .coop create, and who joined them.
CREATE TABLE IF NOT EXISTS `world_randomizer_coop` (
    `code` CHAR(6) NOT NULL,
    `leader` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
    `map` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Where the party starts',
    `zone` INT UNSIGNED NOT NULL DEFAULT 0,
    `x` FLOAT NOT NULL DEFAULT 0,
    `y` FLOAT NOT NULL DEFAULT 0,
    `z` FLOAT NOT NULL DEFAULT 0,
    `o` FLOAT NOT NULL DEFAULT 0,
    `team` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 Alliance, 1 Horde',
    `created_at` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix time',
    PRIMARY KEY (`code`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: co-op parties';

CREATE TABLE IF NOT EXISTS `world_randomizer_coop_member` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
    `code` CHAR(6) NOT NULL,
    `joined_at` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix time',
    PRIMARY KEY (`guid`),
    KEY `idx_code` (`code`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: co-op party members';

-- Hardcore: characters ruled out of going hardcore (a trade, a mail, a level up, a failed check), saved for good.
-- `allowed` = 1 is an admin override (.hardcore allow): the character may go hardcore whatever the checks say.
CREATE TABLE IF NOT EXISTS `world_randomizer_hardcore_taint` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
    `reason` VARCHAR(255) NOT NULL DEFAULT '' COMMENT 'Why it was ruled out (the first reason)',
    `at` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix time',
    `allowed` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = admin override',
    `allowed_by` VARCHAR(32) NOT NULL DEFAULT '' COMMENT 'GM name or console',
    `allowed_at` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix time',
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: hardcore eligibility';
