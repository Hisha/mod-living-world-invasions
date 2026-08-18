-- One-time compatible migration for installations created before spawn groups
-- were converted from raw map/XYZ anchors to route-node anchors.
--
-- Existing rows are intentionally allowed to remain NULL until their owning
-- prebuilt SQL is re-imported with route_node_id values.
ALTER TABLE `lwi_spawn_group`
  ADD COLUMN IF NOT EXISTS `route_node_id` INT UNSIGNED NULL AFTER `name`;

ALTER TABLE `lwi_spawn_group`
  DROP COLUMN IF EXISTS `map_id`,
  DROP COLUMN IF EXISTS `x`,
  DROP COLUMN IF EXISTS `y`,
  DROP COLUMN IF EXISTS `z`,
  DROP COLUMN IF EXISTS `orientation`;
