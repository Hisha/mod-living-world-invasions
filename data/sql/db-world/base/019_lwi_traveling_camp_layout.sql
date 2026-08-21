-- Living World Invasions: reusable traveling-event camp layouts.
-- A stop points at a camp layout; the route node is the camp center.
-- All layout coordinates are local to the route-node orientation:
--   +forward = direction the node faces
--   +right   = right side of that facing

CREATE TABLE IF NOT EXISTS `lwi_traveling_camp_layout` (
    `id` INT UNSIGNED NOT NULL,
    `name` VARCHAR(120) NOT NULL,

    `merchant_forward` FLOAT NOT NULL DEFAULT 0,
    `merchant_right` FLOAT NOT NULL DEFAULT 0,
    `merchant_z` FLOAT NOT NULL DEFAULT 0,
    `merchant_orientation_offset` FLOAT NOT NULL DEFAULT 0,

    `mule1_forward` FLOAT NOT NULL DEFAULT 0,
    `mule1_right` FLOAT NOT NULL DEFAULT 0,
    `mule1_z` FLOAT NOT NULL DEFAULT 0,
    `mule1_orientation_offset` FLOAT NOT NULL DEFAULT 0,

    `mule2_forward` FLOAT NOT NULL DEFAULT 0,
    `mule2_right` FLOAT NOT NULL DEFAULT 0,
    `mule2_z` FLOAT NOT NULL DEFAULT 0,
    `mule2_orientation_offset` FLOAT NOT NULL DEFAULT 0,

    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `comment` VARCHAR(255) NULL,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `lwi_traveling_camp_layout_prop` (
    `id` INT UNSIGNED NOT NULL,
    `layout_id` INT UNSIGNED NOT NULL,
    `gameobject_entry` INT UNSIGNED NOT NULL,
    `forward_offset` FLOAT NOT NULL DEFAULT 0,
    `right_offset` FLOAT NOT NULL DEFAULT 0,
    `z_offset` FLOAT NOT NULL DEFAULT 0,
    `orientation_offset` FLOAT NOT NULL DEFAULT 0,
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `comment` VARCHAR(255) NULL,
    PRIMARY KEY (`id`),
    KEY `idx_lwi_travel_camp_prop_layout` (`layout_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;