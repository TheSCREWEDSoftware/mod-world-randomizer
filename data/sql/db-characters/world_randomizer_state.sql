-- mod-world-randomizer: realm state (the world seed).
CREATE TABLE IF NOT EXISTS `world_randomizer_state` (
    `key` VARCHAR(32) NOT NULL,
    `value` VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-world-randomizer: realm state';
