-- Living World Invasions: per-camp-node terrain Z corrections.
-- Permanent/final schema only. No ALTER statements.
--
-- target_type:
--   1 = merchant
--   2 = mule #1
--   3 = mule #2
--   4 = camp layout prop
--
-- For target_type 4, target_id is the camp layout-prop ROW ID rather than
-- gameobject_entry. Two placements of the same GO can therefore be adjusted
-- independently at the same camp.

CREATE TABLE IF NOT EXISTS `lwi_traveling_camp_node_z_override` (
    `id` INT UNSIGNED NOT NULL,
    `route_node_id` INT UNSIGNED NOT NULL,
    `target_type` TINYINT UNSIGNED NOT NULL,
    `target_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `z_override` FLOAT NOT NULL DEFAULT 0,
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `comment` VARCHAR(255) NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_lwi_travel_camp_node_target`
        (`route_node_id`,`target_type`,`target_id`),
    KEY `idx_lwi_travel_camp_node` (`route_node_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;