-- mod-world-randomizer: every shuffled zone's bracket, written at startup and read by mod-dynamic-scaling when it
-- scales the open world inside the brackets (collective, DynamicScaling.Collective.WorldScaling).
CREATE TABLE IF NOT EXISTS `world_randomizer_zone_bracket` (
    `zone_id` INT UNSIGNED NOT NULL,
    `level_min` TINYINT UNSIGNED NOT NULL,
    `level_max` TINYINT UNSIGNED NOT NULL,
    PRIMARY KEY (`zone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: written at startup, read by mod-dynamic-scaling';
