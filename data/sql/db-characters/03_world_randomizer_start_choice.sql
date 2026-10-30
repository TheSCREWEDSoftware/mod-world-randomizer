-- mod-world-randomizer: characters that chose to start alone (the addon's "Start alone"), so they can't make or join a
-- co-op party later. Characters that started with a party are in `world_randomizer_coop_member`.
CREATE TABLE IF NOT EXISTS `world_randomizer_start_choice` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
    `chosen_at` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Unix time',
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: characters that started alone';
