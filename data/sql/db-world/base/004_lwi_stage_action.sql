-- Canonical clean-install schema for stage actions.
-- action_type 1 = Spawn Group
-- action_type 2 = Start Movement
--
-- Start Movement parameter mapping:
--   target_id  = spawn_group_id whose latest runtime entity group should move
--   parameter1 = movement_path_id
--   parameter2 = movement_profile_id (0 = provider/default behavior)
--   parameter3 = completion_signal_id (0 = emit no signal)
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
