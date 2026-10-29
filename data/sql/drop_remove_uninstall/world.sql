-- mod-world-randomizer drop / remove / uninstall, world database.
-- Run it only after the module is out of the build (moved out of modules/ or in -DDISABLED_AC_MODULES) and the
-- worldserver was rebuilt; otherwise the DB updater installs everything again on the next start.
-- Everything else the module changes (replaced creatures, creature and dungeon levels, player base stats for the
-- Vanilla / TBC eras, XP rates, start locations) is changed in memory only and is back to the core's values as soon
-- as the module is gone. The player stats tables below are the module's own copies; the core's
-- `player_class_stats` / `player_race_stats` are never touched.

-- The custom spell IDs, if you changed them in the config
SET @HealthRegenSpellID := 150110; -- Randomizer.Achievements.HealthRegenSpell
SET @ManaRegenSpellID := 150111;   -- Randomizer.Achievements.ManaRegenSpell

-- The module's tables
DROP TABLE IF EXISTS `world_randomizer_zone`;
DROP TABLE IF EXISTS `world_randomizer_zone_link`;
DROP TABLE IF EXISTS `world_randomizer_instance`;
DROP TABLE IF EXISTS `world_randomizer_spawn_zone`;
DROP TABLE IF EXISTS `world_randomizer_levelstats`;
DROP TABLE IF EXISTS `world_randomizer_classlevelstats`;
DROP TABLE IF EXISTS `world_randomizer_dungeon_bracket`;
DROP TABLE IF EXISTS `world_randomizer_zone_bracket`;
DROP TABLE IF EXISTS `world_randomizer_achievement`;
DROP TABLE IF EXISTS `world_randomizer_achievement_reward`;

-- The achievement regen auras
DELETE FROM `spell_dbc` WHERE `ID` IN (@HealthRegenSpellID, @ManaRegenSpellID);

-- Forget the applied files, so a later reinstall runs them again
DELETE FROM `updates` WHERE `state` = 'MODULE' AND `name` IN ('01_world_randomizer_tables.sql',
    '02_world_randomizer_zones.sql', '03_world_randomizer_player_stats.sql', '04_world_randomizer_instances.sql',
    '05_world_randomizer_achievement_tables.sql', '06_world_randomizer_achievements.sql',
    '07_world_randomizer_zone_bracket.sql',
    -- the names before the files were numbered first
    'world_randomizer_01_tables.sql', 'world_randomizer_02_zones.sql', 'world_randomizer_03_player_stats.sql',
    'world_randomizer_04_instances.sql', 'world_randomizer_05_achievement_tables.sql',
    'world_randomizer_06_achievements.sql');
