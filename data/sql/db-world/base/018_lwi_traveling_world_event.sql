-- Living World Invasions: reusable non-combat traveling-world-event framework.
-- This intentionally sits beside the invasion runtime rather than pretending
-- caravans, merchants, Darkmoon travel, patrols, etc. are invasions.

CREATE TABLE IF NOT EXISTS `lwi_traveling_event` (
    `id` INT UNSIGNED NOT NULL,
    `name` VARCHAR(120) NOT NULL,
    `wagon_entry` INT UNSIGNED NOT NULL DEFAULT 0,
    `merchant_entry` INT UNSIGNED NOT NULL DEFAULT 0,
    `merchant_seat_id` TINYINT NOT NULL DEFAULT 0,
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
