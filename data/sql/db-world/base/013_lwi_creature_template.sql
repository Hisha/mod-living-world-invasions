-- Living World Invasions derived creature-template definitions.
--
-- These are LOGICAL LWI creature templates. They do not use creature_template
-- entry IDs directly. At worldserver startup LWI allocates a free concrete
-- creature_template.entry for each enabled definition and stores that mapping
-- in lwi_creature_template_map.
--
-- NULL override = inherit the value from base_creature_entry.
CREATE TABLE IF NOT EXISTS `lwi_creature_template` (
    `id` INT UNSIGNED NOT NULL,
    `name` VARCHAR(100) NOT NULL,
    `base_creature_entry` MEDIUMINT UNSIGNED NOT NULL,

    `name_override` VARCHAR(100) NULL DEFAULT NULL,
    `subname_override` VARCHAR(100) NULL DEFAULT NULL,
    `faction_override` SMALLINT UNSIGNED NULL DEFAULT NULL,
    `rank_override` TINYINT UNSIGNED NULL DEFAULT NULL,

    `health_modifier_override` FLOAT NULL DEFAULT NULL,
    `mana_modifier_override` FLOAT NULL DEFAULT NULL,
    `armor_modifier_override` FLOAT NULL DEFAULT NULL,
    `damage_modifier_override` FLOAT NULL DEFAULT NULL,
    `unit_class_override` TINYINT UNSIGNED NULL DEFAULT NULL,

    `enabled` TINYINT(1) UNSIGNED NOT NULL DEFAULT 1,
    `comment` VARCHAR(255) NULL DEFAULT NULL,

    PRIMARY KEY (`id`),
    KEY `idx_lwi_creature_template_base` (`base_creature_entry`),
    KEY `idx_lwi_creature_template_enabled` (`enabled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Persistent ownership/allocation table.
--
-- allocated_entry is never automatically recycled. Disabled or removed LWI
-- definitions are marked retired so their concrete IDs remain reserved.
CREATE TABLE IF NOT EXISTS `lwi_creature_template_map` (
    `lwi_template_id` INT UNSIGNED NOT NULL,
    `allocated_entry` MEDIUMINT UNSIGNED NOT NULL,
    `base_creature_entry` MEDIUMINT UNSIGNED NOT NULL,
    `retired` TINYINT(1) UNSIGNED NOT NULL DEFAULT 0,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (`lwi_template_id`),
    UNIQUE KEY `uq_lwi_creature_template_allocated_entry` (`allocated_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;