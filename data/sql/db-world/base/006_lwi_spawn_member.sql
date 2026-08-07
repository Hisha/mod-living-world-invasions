CREATE TABLE IF NOT EXISTS `lwi_spawn_member` (
    `id` INT UNSIGNED NOT NULL,
    `spawn_group_id` INT UNSIGNED NOT NULL,
    `creature_entry` INT UNSIGNED NOT NULL,
    `count` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
    `level_override` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`),
    KEY `idx_lwi_spawn_member_group` (`spawn_group_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;