-- Living World Invasions: base world database schema

CREATE TABLE IF NOT EXISTS `lwi_invasion` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `name` VARCHAR(120) NOT NULL,
    `zone_id` INT UNSIGNED NOT NULL,
    `team` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = neutral, 1 = Alliance, 2 = Horde',
    `recommended_min_level` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `recommended_max_level` TINYINT UNSIGNED NOT NULL DEFAULT 80,
    `cooldown_seconds` INT UNSIGNED NOT NULL DEFAULT 86400,
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `comment` VARCHAR(255) NULL DEFAULT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_lwi_invasion_name` (`name`),
    KEY `idx_lwi_invasion_enabled` (`enabled`),
    KEY `idx_lwi_invasion_zone` (`zone_id`),
    CONSTRAINT `chk_lwi_invasion_level_range`
        CHECK (`recommended_min_level` <= `recommended_max_level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Disabled smoke-test definition. It proves SQL loading without creating gameplay content.
INSERT INTO `lwi_invasion`
    (`id`, `name`, `zone_id`, `team`, `recommended_min_level`, `recommended_max_level`, `cooldown_seconds`, `enabled`, `comment`)
VALUES
    (1, 'Framework Smoke Test', 12, 1, 1, 10, 3600, 0, 'Enable only when testing the initial loader.')
ON DUPLICATE KEY UPDATE
    `name` = VALUES(`name`),
    `zone_id` = VALUES(`zone_id`),
    `team` = VALUES(`team`),
    `recommended_min_level` = VALUES(`recommended_min_level`),
    `recommended_max_level` = VALUES(`recommended_max_level`),
    `cooldown_seconds` = VALUES(`cooldown_seconds`),
    `comment` = VALUES(`comment`);
