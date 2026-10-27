-- mod-world-randomizer: world tables.
CREATE TABLE IF NOT EXISTS `world_randomizer_zone` (
    `zone_id` INT UNSIGNED NOT NULL COMMENT 'AreaTable.dbc zone ID',
    `map_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `continent` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 Eastern Kingdoms, 1 Kalimdor, 2 Outland, 3 Northrend',
    `expansion` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 Vanilla, 1 TBC, 2 Wrath',
    `level_min` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Original level range',
    `level_max` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `territory` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT '0 alliance, 1 horde, 2 contested (start locations)',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '0 = not part of the randomizer at all',
    `shuffle` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '0 = stays in its own bracket (creatures still replaced)',
    `name` VARCHAR(100) NOT NULL DEFAULT '',
    PRIMARY KEY (`zone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: zones to shuffle';

CREATE TABLE IF NOT EXISTS `world_randomizer_instance` (
    `map_id` INT UNSIGNED NOT NULL COMMENT 'Instance map',
    `zone_id` INT UNSIGNED NOT NULL COMMENT 'Zone whose bracket the instance follows',
    `comment` VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`map_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: entrance zone overrides (normally found from area triggers)';

CREATE TABLE IF NOT EXISTS `world_randomizer_spawn_zone` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'creature.guid',
    `zone_id` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: cache of spawn zones the creature table leaves at 0';

CREATE TABLE IF NOT EXISTS `world_randomizer_levelstats` (
    `era` TINYINT UNSIGNED NOT NULL COMMENT '1 Vanilla (vMaNGOS), 2 TBC (cMaNGOS-TBC)',
    `race` TINYINT UNSIGNED NOT NULL,
    `class` TINYINT UNSIGNED NOT NULL,
    `level` TINYINT UNSIGNED NOT NULL,
    `str` INT NOT NULL,
    `agi` INT NOT NULL,
    `sta` INT NOT NULL,
    `inte` INT NOT NULL,
    `spi` INT NOT NULL,
    PRIMARY KEY (`era`, `race`, `class`, `level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: player base stats per era';

CREATE TABLE IF NOT EXISTS `world_randomizer_classlevelstats` (
    `era` TINYINT UNSIGNED NOT NULL COMMENT '1 Vanilla (vMaNGOS), 2 TBC (cMaNGOS-TBC)',
    `class` TINYINT UNSIGNED NOT NULL,
    `level` TINYINT UNSIGNED NOT NULL,
    `basehp` INT UNSIGNED NOT NULL,
    `basemana` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`era`, `class`, `level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: player base health / mana per era';

CREATE TABLE IF NOT EXISTS `world_randomizer_zone_link` (
    `zone_id` INT UNSIGNED NOT NULL,
    `neighbour_id` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`zone_id`, `neighbour_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: zones that border each other (Randomizer.Brackets.Adjacent)';
