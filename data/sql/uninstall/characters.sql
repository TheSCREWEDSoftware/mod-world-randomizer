-- mod-world-randomizer uninstall, characters database. Read the notes in world.sql first.
--
-- What stays on the characters, because it is ordinary game data the core keeps by itself:
--   - Achievement rewards already given: spells (riding, specializations), titles, starting gold and items.
--   - Armor / weapon skills "usable by any class": the core removes the ones a class may not have when the
--     character next logs in, so nothing to do here.
--   - Dead hardcore characters that were soft deleted stay core deleted characters: list them with
--     .character deleted list and bring one back with .character deleted restore. To give every one of them back to
--     its account instead, remove the comment marks from the block below before running this file.
--     A character whose name was taken since stays deleted; restore it under a new name with the command.

-- UPDATE `characters` AS c
--     JOIN `world_randomizer_hardcore` AS h ON h.`guid` = c.`guid` AND h.`soft_deleted` = 1
--     LEFT JOIN (SELECT DISTINCT `name` FROM `characters`) AS taken ON taken.`name` = c.`deleteInfos_Name`
-- SET c.`name` = c.`deleteInfos_Name`, c.`account` = c.`deleteInfos_Account`, c.`deleteDate` = NULL,
--     c.`deleteInfos_Name` = NULL, c.`deleteInfos_Account` = NULL
-- WHERE c.`deleteInfos_Account` IS NOT NULL AND taken.`name` IS NULL;

-- The achievement regen auras, if a character was saved with them
DELETE FROM `character_aura` WHERE `spell` IN (150110, 150111);

-- The module's tables
DROP TABLE IF EXISTS `world_randomizer_state`;
DROP TABLE IF EXISTS `world_randomizer_hardcore`;
DROP TABLE IF EXISTS `world_randomizer_hardcore_revive`;
DROP TABLE IF EXISTS `world_randomizer_hardcore_taint`;
DROP TABLE IF EXISTS `world_randomizer_coop`;
DROP TABLE IF EXISTS `world_randomizer_coop_member`;
DROP TABLE IF EXISTS `world_randomizer_ach_done`;
DROP TABLE IF EXISTS `world_randomizer_ach_counter`;
DROP TABLE IF EXISTS `world_randomizer_ach_zone`;
DROP TABLE IF EXISTS `world_randomizer_ach_run`;
DROP TABLE IF EXISTS `world_randomizer_ach_rare`;
DROP TABLE IF EXISTS `world_randomizer_ach_choice`;
DROP TABLE IF EXISTS `world_randomizer_ach_skill`;

-- Forget the applied file, so a later reinstall runs it again
DELETE FROM `updates` WHERE `state` = 'MODULE' AND `name` = 'world_randomizer_characters.sql';
