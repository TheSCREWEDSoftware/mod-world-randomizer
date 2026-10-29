-- mod-world-randomizer: instances whose entrance stands in a city (cities are not shuffled), so they follow the
-- zone around that city instead. Every other instance finds its entrance zone from `areatrigger_teleport`.
DELETE FROM `world_randomizer_instance` WHERE `map_id` IN (34, 389, 608);
INSERT INTO `world_randomizer_instance` (`map_id`, `zone_id`, `comment`) VALUES
(34, 12, 'The Stockade (Stormwind City) - follows Elwynn Forest'),
(389, 14, 'Ragefire Chasm (Orgrimmar) - follows Durotar'),
(608, 2817, 'The Violet Hold (Dalaran) - follows Crystalsong Forest');
