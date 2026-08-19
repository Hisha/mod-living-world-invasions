-- Living World Invasions: reusable non-combat traveling-world-event framework.
-- This intentionally sits beside the invasion runtime rather than pretending
-- caravans, merchants, Darkmoon travel, patrols, etc. are invasions.
--
-- Mobile-wagon design:
--   leader_entry  = Creature route owner (draft animal / driver anchor)
--   wagon_entry   = GameObject entry relocated behind the leader while traveling
--   merchant_entry= optional Creature used later for camp/vendor behavior

CREATE TABLE IF NOT EXISTS `lwi_traveling_event` (
    `id` INT UNSIGNED NOT NULL,
    `name` VARCHAR(120) NOT NULL,
    `leader_entry` INT UNSIGNED NOT NULL DEFAULT 0,
    `wagon_entry` INT UNSIGNED NOT NULL DEFAULT 0,
    `merchant_entry` INT UNSIGNED NOT NULL DEFAULT 0,
    `wagon_distance_behind` FLOAT NOT NULL DEFAULT 4.5,
    `wagon_lateral_offset` FLOAT NOT NULL DEFAULT 0,
    `wagon_vertical_offset` FLOAT NOT NULL DEFAULT 0,
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `comment` VARCHAR(255) NULL,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `lwi_traveling_event_stop` (
    `id` INT UNSIGNED NOT NULL,
    `event_id` INT UNSIGNED NOT NULL,
    `stop_order` INT UNSIGNED NOT NULL,
    `route_node_id` INT UNSIGNED NOT NULL,
    `dwell_seconds` INT UNSIGNED NOT NULL DEFAULT 120,
    `arrival_text` VARCHAR(255) NOT NULL DEFAULT '',
    `departure_text` VARCHAR(255) NOT NULL DEFAULT '',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `comment` VARCHAR(255) NULL,
    PRIMARY KEY (`id`),
    KEY `idx_lwi_travel_stop_event_order` (`event_id`,`stop_order`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `lwi_traveling_event_prop` (
    `id` INT UNSIGNED NOT NULL,
    `event_id` INT UNSIGNED NOT NULL,
    `gameobject_entry` INT UNSIGNED NOT NULL,
    `offset_x` FLOAT NOT NULL DEFAULT 0,
    `offset_y` FLOAT NOT NULL DEFAULT 0,
    `offset_z` FLOAT NOT NULL DEFAULT 0,
    `orientation_offset` FLOAT NOT NULL DEFAULT 0,
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `comment` VARCHAR(255) NULL,
    PRIMARY KEY (`id`),
    KEY `idx_lwi_travel_prop_event` (`event_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
