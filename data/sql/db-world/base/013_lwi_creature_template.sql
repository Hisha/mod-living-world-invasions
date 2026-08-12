-- ============================================================================
-- Living World Invasions - Derived Creature Templates
-- ============================================================================
--
-- Defines custom creatures used by LWI without requiring invasion SQL to
-- hard-code entries in AzerothCore's creature_template table.
--
-- Each definition starts with an existing AzerothCore creature specified by
-- base_creature_entry. LWI creates a derived creature_template entry at server
-- startup and stores the allocated entry in lwi_creature_template_map.
--
-- Values ending in "_override" are optional:
--
--     NULL = inherit the value from the base creature
--     value = replace the base creature's value
--
-- The generated creature also inherits the base creature's:
--
--     * creature model(s)
--     * equipment
--     * basic physical/combat properties
--
-- LWI deliberately does NOT inherit things such as SmartAI, ScriptName,
-- vendor/quest/trainer/gossip behavior, loot, gold, or permanent movement.
--
-- Creature level is NOT defined here. Level can be controlled independently
-- for each spawn using lwi_spawn_member.level_override.
--
-- ============================================================================


-- ============================================================================
-- lwi_creature_template
-- ============================================================================
--
-- Logical definitions for LWI-generated creatures.
--
-- id
--     LWI's logical identifier for this creature definition.
--
--     This is NOT the AzerothCore creature_template.entry.
--
--     Spawn members reference this value through:
--         lwi_spawn_member.lwi_template_id
--
--     Example:
--         id = 100
--
--     A spawn member using lwi_template_id = 100 will be resolved at runtime
--     to whatever creature_template entry LWI allocated for definition 100.
--
--
-- name
--     Internal descriptive name for the LWI definition.
--
--     Primarily intended to make the database readable for administrators.
--     This does not have to match the creature's visible in-game name.
--
--     Example:
--         'Westfall Defias Ogre Commander'
--
--
-- base_creature_entry
--     Existing AzerothCore creature_template.entry used as the foundation for
--     the generated creature.
--
--     LWI inherits the creature's model, equipment, and appropriate physical/
--     combat properties from this entry.
--
--     Example:
--         639 = Edwin VanCleef
--
--
-- name_override
--     Visible in-game creature name.
--
--     NULL = use the base creature's name.
--
--     Example:
--         'Defias Lieutenant'
--
--
-- subname_override
--     Visible subtitle displayed beneath the creature's name.
--
--     NULL = inherit the base creature's subname.
--
--     Example:
--         'Defias Brotherhood'
--
--
-- faction_override
--     AzerothCore faction template ID assigned to the generated creature.
--
--     NULL = inherit the base creature's faction.
--
--     This determines normal faction relationships such as hostility and
--     friendliness.
--
--
-- rank_override
--     Creature rank.
--
--     NULL = inherit the base creature's rank.
--
--     AzerothCore/WotLK creature ranks include values such as:
--         0 = Normal
--         1 = Elite
--         2 = Rare Elite
--         3 = World Boss
--         4 = Rare
--
--
-- health_modifier_override
--     Multiplier used for the generated creature's health.
--
--     NULL = inherit the base creature's HealthModifier.
--
--     Examples:
--         1.0 = normal modifier
--         2.0 = twice the modifier
--         3.0 = three times the modifier
--
--
-- mana_modifier_override
--     Multiplier used for the generated creature's mana.
--
--     NULL = inherit the base creature's ManaModifier.
--
--
-- armor_modifier_override
--     Multiplier used for the generated creature's armor.
--
--     NULL = inherit the base creature's ArmorModifier.
--
--
-- damage_modifier_override
--     Multiplier used for the generated creature's damage.
--
--     NULL = inherit the base creature's DamageModifier.
--
--     Example:
--         1.5 = 150% of the normal modifier
--
--
-- unit_class_override
--     Overrides the creature's AzerothCore unit_class.
--
--     NULL = inherit the base creature's unit_class.
--
--     Normally this should be left NULL unless the derived creature needs a
--     different underlying class/resource behavior.
--
--
-- enabled
--     Controls whether LWI materializes this definition.
--
--         1 = enabled
--         0 = disabled
--
--     When disabled, LWI removes the generated creature/template/model/
--     equipment rows during startup and marks its persistent allocation as
--     retired. The allocated entry remains reserved for this logical template.
--
--     Re-enabling the definition causes LWI to recreate the creature using
--     the same allocated entry.
--
--
-- comment
--     Optional administrator/developer notes.
--
--     Has no effect on runtime behavior.
--
-- ============================================================================

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
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;


-- ============================================================================
-- lwi_creature_template_map
-- ============================================================================
--
-- Persistent ownership and allocation information for generated creatures.
--
-- IMPORTANT:
--
-- Premade invasion SQL should generally NOT insert rows into this table.
--
-- LWI manages this table automatically. Invasion authors define logical
-- creatures in lwi_creature_template and reference their logical IDs from
-- lwi_spawn_member.
--
--
-- lwi_template_id
--     Logical creature ID from lwi_creature_template.id.
--
--     Example:
--         100
--
--
-- allocated_entry
--     Actual AzerothCore creature_template.entry allocated by LWI.
--
--     Example:
--         15000007
--
--     This value is chosen dynamically so premade invasions do not need to
--     reserve globally fixed creature_template IDs.
--
--     Different installations may allocate different entries for the same
--     logical LWI template.
--
--
-- base_creature_entry
--     Records the AzerothCore creature_template.entry from which this generated
--     creature is currently derived.
--
--     This mirrors lwi_creature_template.base_creature_entry and provides
--     ownership/history information for the allocation.
--
--
-- retired
--     Indicates whether the allocation currently has an active generated
--     creature.
--
--         0 = active
--         1 = retired/reserved
--
--     Retiring a definition does NOT automatically make allocated_entry
--     available to another LWI creature.
--
--     This prevents IDs from unexpectedly changing ownership.
--
--
-- created_at
--     Timestamp when this logical-template-to-creature-entry allocation was
--     originally created.
--
--
-- updated_at
--     Timestamp automatically updated whenever the mapping row changes.
--
-- ============================================================================

CREATE TABLE IF NOT EXISTS `lwi_creature_template_map` (
    `lwi_template_id` INT UNSIGNED NOT NULL,
    `allocated_entry` MEDIUMINT UNSIGNED NOT NULL,
    `base_creature_entry` MEDIUMINT UNSIGNED NOT NULL,
    `retired` TINYINT(1) UNSIGNED NOT NULL DEFAULT 0,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (`lwi_template_id`),
    UNIQUE KEY `uq_lwi_creature_template_allocated_entry` (`allocated_entry`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;