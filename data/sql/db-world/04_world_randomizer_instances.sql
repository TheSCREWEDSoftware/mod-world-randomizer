-- mod-world-randomizer: instances whose entrance stands in a city (cities are not shuffled), so they follow the
-- zone around that city instead. Every other instance finds its entrance zone from `areatrigger_teleport`.
SET @StockadeMapID := 34;
SET @StockadeZoneID := 12;          -- Elwynn Forest
SET @RagefireChasmMapID := 389;
SET @RagefireChasmZoneID := 14;     -- Durotar
SET @VioletHoldMapID := 608;
SET @VioletHoldZoneID := 2817;      -- Crystalsong Forest

DELETE FROM `world_randomizer_instance` WHERE `map_id` IN (@StockadeMapID, @RagefireChasmMapID, @VioletHoldMapID);
INSERT INTO `world_randomizer_instance` (`map_id`, `zone_id`, `comment`) VALUES
(@StockadeMapID, @StockadeZoneID, 'The Stockade (Stormwind City) - follows Elwynn Forest'),
(@RagefireChasmMapID, @RagefireChasmZoneID, 'Ragefire Chasm (Orgrimmar) - follows Durotar'),
(@VioletHoldMapID, @VioletHoldZoneID, 'The Violet Hold (Dalaran) - follows Crystalsong Forest');
