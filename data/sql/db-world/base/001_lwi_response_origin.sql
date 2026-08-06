-- Canonical clean-install schema: response origins must be created before invasions.
CREATE TABLE IF NOT EXISTS `lwi_response_origin` (
    `id` INT UNSIGNED NOT NULL,
    `name` VARCHAR(100) NOT NULL,
    `map_id` SMALLINT UNSIGNED NOT NULL,
    `team` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = neutral, 1 = Alliance, 2 = Horde',
    `max_active_default` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT '0 = unlimited',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `comment` VARCHAR(255) NULL DEFAULT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_lwi_response_origin_name_map` (`name`, `map_id`),
    KEY `idx_lwi_response_origin_map` (`map_id`),
    KEY `idx_lwi_response_origin_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
