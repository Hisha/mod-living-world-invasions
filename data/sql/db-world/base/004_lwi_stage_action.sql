-- Canonical clean-install schema for stage actions.
-- action_type 1 = Spawn Group
-- action_type 2 = Start Movement
-- action_type 3 = Dialogue
-- action_type 4 = World Announcement
-- action_type 5 = Sound
-- action_type 6 = Spell (scripted event cast)
--
-- Start Movement parameter mapping:
--   target_id  = spawn_group_id whose latest runtime entity group should move
--   parameter1 = movement_path_id
--   parameter2 = movement_profile_id (0 = provider/default behavior)
--   parameter3 = completion_signal_id (0 = emit no signal)
--
-- Dialogue parameter mapping:
--   target_id  = spawn_group_id whose latest runtime entity group contains the speaker
--   parameter1 = dialogue_id
--   parameter2 = speaker spawn_member_id (0 = first available creature)
--   parameter3 = reserved
--
-- World Announcement parameter mapping:
--   target_id  = announcement_id
--   parameter1 = scope: 0 global, 1 map, 2 zone, 3 area
--   parameter2 = scope_id (0 derives map/zone from invasion; area requires explicit id)
--   parameter3 = faction: 0 everyone, 1 Alliance, 2 Horde
CREATE TABLE IF NOT EXISTS `lwi_stage_action` (
    `id` INT UNSIGNED NOT NULL,
    `stage_id` INT UNSIGNED NOT NULL,
    `action_order` SMALLINT UNSIGNED NOT NULL,
    `action_type` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `target_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `parameter1` INT UNSIGNED NOT NULL DEFAULT 0,
    `parameter2` INT UNSIGNED NOT NULL DEFAULT 0,
    `parameter3` INT UNSIGNED NOT NULL DEFAULT 0,
    `delay_seconds` INT UNSIGNED NOT NULL DEFAULT 0,
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `comment` VARCHAR(255) NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_lwi_stage_action_order` (`stage_id`, `action_order`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
--
-- Sound parameter mapping:
--   target_id  = spawn_group_id
--   parameter1 = sound_id (SoundEntries.dbc)
--   parameter2 = source spawn_member_id (0 = first available creature)
--   parameter3 = playback mode: 0 distance/positional, 1 direct
--
-- Spell parameter mapping (v1):
--   target_id  = caster spawn_group_id
--   parameter1 = spell_id
--   parameter2 = caster spawn_member_id (0 = first available creature)
--   parameter3 = target mode: 0 self
--
-- Spell Actions are explicit invasion-scripted casts only. Native creature
-- combat spells, automatic self-buffs, rotations, CreatureAI and SmartAI remain unchanged.

