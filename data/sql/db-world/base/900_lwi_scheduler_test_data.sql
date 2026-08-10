-- TEMPORARY scheduler/spawn/movement framework data.
-- This is development test content and is safe to reapply.

-- ===========================================================================
-- Cleanup existing test data
-- ===========================================================================

DELETE FROM `lwi_stage_action`
WHERE `id` IN (10001,10002,10003,10004,10005,10006,10007,10008,10009);

DELETE FROM `lwi_runtime_signal`
WHERE `id` IN (100);

DELETE FROM `lwi_dialogue`
WHERE `id` IN (100,101);

DELETE FROM `lwi_announcement`
WHERE `id` IN (100);

DELETE FROM `lwi_spawn_member`
WHERE `id` IN (100001,100002,100003,100004);

DELETE FROM `lwi_spawn_group`
WHERE `id` IN (100,101,102);

DELETE FROM `lwi_movement_node`
WHERE `path_id` IN (100);

DELETE FROM `lwi_movement_path`
WHERE `id` IN (100);

DELETE FROM `lwi_movement_profile`
WHERE `id` IN (100);

DELETE FROM `lwi_invasion_stage`
WHERE `invasion_id` IN (1,2,3);

DELETE FROM `lwi_invasion`
WHERE `id` IN (1,2,3);

DELETE FROM `lwi_response_origin`
WHERE `id` IN (1,2);

-- ===========================================================================
-- Response Origins
-- ===========================================================================

INSERT INTO `lwi_response_origin`
(
    `id`,
    `name`,
    `map_id`,
    `team`,
    `max_active_default`,
    `enabled`,
    `comment`
)
VALUES
    (1, 'Stormwind', 0, 1, 1, 1, 'Temporary scheduler test origin.'),
    (2, 'Ironforge', 0, 1, 1, 1, 'Temporary scheduler test origin.');

-- ===========================================================================
-- Invasions
-- ===========================================================================

INSERT INTO `lwi_invasion`
(
    `id`,
    `name`,
    `map_id`,
    `zone_id`,
    `team`,
    `response_origin_id`,
    `recommended_min_level`,
    `recommended_max_level`,
    `selection_weight`,
    `minimum_cooldown_seconds`,
    `maximum_cooldown_seconds`,
    `maximum_runtime_seconds`,
    `allow_random_start`,
    `enabled`,
    `comment`
)
VALUES
    (1, 'Westfall Scheduler Test', 0, 40, 1, 1, 10, 20, 100, 60, 120, 300, 1, 1, 'Temporary scheduler/spawn/movement test.'),
    (2, 'Duskwood Scheduler Test', 0, 10, 1, 1, 20, 30, 100, 60, 120, 300, 1, 1, 'Temporary scheduler test.'),
    (3, 'Wetlands Scheduler Test', 0, 11, 1, 2, 20, 30, 100, 60, 120, 300, 1, 1, 'Temporary scheduler test.');

-- ===========================================================================
-- Runtime Stages
-- completion_type: 0 = timer, 1 = runtime signal
-- ===========================================================================

INSERT INTO `lwi_invasion_stage`
(
    `id`,
    `invasion_id`,
    `stage_order`,
    `name`,
    `duration_seconds`,
    `completion_type`,
    `completion_target_id`,
    `enabled`,
    `comment`
)
VALUES
    (1001, 1, 10, 'Scouts',           0, 1, 100, 1, 'Completes when ScoutRouteComplete is emitted.'),
    (1002, 1, 20, 'Reinforcements',  20, 0,   0, 1, 'Spawn engine test stage.'),
    (1003, 1, 30, 'Lieutenant',      20, 0,   0, 1, 'Spawn engine test stage.'),

    (2001, 2, 10, 'Scouts',          20, 0,   0, 1, 'Runtime framework test.'),
    (2002, 2, 20, 'Reinforcements',  20, 0,   0, 1, 'Runtime framework test.'),
    (2003, 2, 30, 'Lieutenant',      20, 0,   0, 1, 'Runtime framework test.'),

    (3001, 3, 10, 'Scouts',          20, 0,   0, 1, 'Runtime framework test.'),
    (3002, 3, 20, 'Reinforcements',  20, 0,   0, 1, 'Runtime framework test.'),
    (3003, 3, 30, 'Lieutenant',      20, 0,   0, 1, 'Runtime framework test.');

-- ===========================================================================
-- Spawn Groups
-- ===========================================================================

INSERT INTO `lwi_spawn_group`
(
    `id`,
    `name`,
    `map_id`,
    `x`,
    `y`,
    `z`,
    `orientation`,
    `spawn_radius`,
    `enabled`
)
VALUES
(
    100,
    'Westfall Defias Scouts',
    0,
    -10191.058,
    1801.637,
    34.94533,
    0,
    10,
    1
),
(
    101,
    'Westfall Defias Reinforcements',
    0,
    -10191.058,
    1801.637,
    34.94533,
    0,
    10,
    1
),
(
    102,
    'Westfall Defias Lieutenant',
    0,
    -10191.058,
    1801.637,
    34.94533,
    0,
    5,
    1
);

-- ===========================================================================
-- Spawn Members
-- entity_type: 1 = Creature, 2 = GameObject
-- tactical_role:
--   0 Default, 1 Commander, 2 Protector, 3 Melee DPS,
--   4 Ranged DPS, 5 Healer, 6 Support
-- ===========================================================================

INSERT INTO `lwi_spawn_member`
(
    `id`,
    `spawn_group_id`,
    `entity_type`,
    `entity_entry`,
    `count`,
    `level_override`,
    `tactical_role`,
    `comment`
)
VALUES
    (100001, 100, 1,   449, 3, 0, 3, 'Westfall Defias scouts - Melee DPS'),
    (100004, 100, 2, 29784, 1, 0, 0, 'Basic Campfire - mixed entity provider test'),
    (100002, 101, 1,   589, 5, 0, 4, 'Westfall Defias reinforcements - Ranged DPS'),
    (100003, 102, 1,   441, 1, 0, 1, 'Westfall Defias lieutenant - Commander');

-- ===========================================================================
-- Runtime Signals
-- ===========================================================================

INSERT INTO `lwi_runtime_signal`
(
    `id`,
    `name`,
    `enabled`,
    `comment`
)
VALUES
(
    100,
    'ScoutRouteComplete',
    1,
    'Emitted when the temporary Westfall scout movement route completes.'
);

-- ===========================================================================
-- Dialogue
-- chat_type: 0 = Say, 1 = Yell
-- ===========================================================================

INSERT INTO `lwi_dialogue`
(
    `id`,
    `name`,
    `text`,
    `chat_type`,
    `language`,
    `enabled`,
    `comment`
)
VALUES
    (100, 'Westfall Scout Warning', 'Keep your eyes open. Sentinel Hill is ahead.', 0, 0, 1, 'Temporary Say test for the scout runtime group.'),
    (101, 'Westfall Lieutenant Challenge', 'The Brotherhood will take Westfall!', 1, 0, 1, 'Temporary Yell test for the lieutenant runtime group.');

-- ===========================================================================
-- World Announcements
-- Delivery scope and faction filtering are configured in lwi_stage_action.
-- ===========================================================================

INSERT INTO `lwi_announcement`
(
    `id`,
    `name`,
    `text`,
    `enabled`,
    `comment`
)
VALUES
(
    100,
    'Westfall Alliance Warning',
    'Defias activity has been reported near Sentinel Hill. Alliance forces in Westfall are advised to remain alert.',
    1,
    'Temporary Alliance-only zone announcement test.'
);

-- ===========================================================================
-- Movement Profile
-- default_mode: 0 = provider/default, 1 = walk, 2 = run
-- ===========================================================================

INSERT INTO `lwi_movement_profile`
(
    `id`,
    `name`,
    `default_mode`,
    `walk_speed_multiplier`,
    `run_speed_multiplier`,
    `stealth_enabled`,
    `enabled`,
    `comment`
)
VALUES
(
    100,
    'Westfall Scout Test Movement',
    2,
    1.0,
    1.0,
    0,
    1,
    'Temporary run profile used to prove runtime group movement.'
);

-- ===========================================================================
-- Movement Path and Nodes
-- Short route near the current Westfall test spawn so it can complete inside
-- the 20-second timer stage.
-- ===========================================================================

INSERT INTO `lwi_movement_path`
(
    `id`,
    `name`,
    `enabled`,
    `comment`
)
VALUES
(
    100,
    'Westfall Scout Test Route',
    1,
    'Temporary short route used to prove runtime group movement.'
);

INSERT INTO `lwi_movement_node`
(
    `id`,
    `path_id`,
    `node_order`,
    `map_id`,
    `x`,
    `y`,
    `z`,
    `orientation`,
    `wait_ms`,
    `profile_override_id`,
    `enabled`,
    `comment`
)
VALUES
    (10001, 100, 10, 0, -10186.000, 1801.637, 34.94533, 0, 1500, 0, 1, 'Scout test route node 1'),
    (10002, 100, 20, 0, -10182.000, 1805.000, 34.94533, 0, 1000, 0, 1, 'Scout test route node 2'),
    (10003, 100, 30, 0, -10178.000, 1801.637, 34.94533, 0,    0, 0, 1, 'Scout test route final node');

-- ===========================================================================
-- Stage Actions
-- action_type: 1 = Spawn Group
-- action_type: 2 = Start Movement
-- action_type: 3 = Dialogue
-- action_type: 4 = World Announcement
-- action_type: 5 = Sound
-- action_type: 6 = Spell
--
-- Start Movement:
--   target_id  = spawn_group_id
--   parameter1 = movement_path_id
--   parameter2 = movement_profile_id
--   parameter3 = completion_signal_id
--
-- Dialogue:
--   target_id  = spawn_group_id
--   parameter1 = dialogue_id
--   parameter2 = speaker spawn_member_id (0 = first available creature)
--   parameter3 = reserved
--
-- World Announcement:
--   target_id  = announcement_id
--   parameter1 = scope (0 global, 1 map, 2 zone, 3 area)
--   parameter2 = scope_id (0 derives map/zone from invasion)
--   parameter3 = faction (0 everyone, 1 Alliance, 2 Horde)
--
-- Sound:
--   target_id  = spawn_group_id
--   parameter1 = sound_id
--   parameter2 = source spawn_member_id (0 = first available creature)
--   parameter3 = playback mode (0 distance/positional, 1 direct)
--
-- Spell (v1):
--   target_id  = caster spawn_group_id
--   parameter1 = spell_id
--   parameter2 = caster spawn_member_id (0 = first available creature)
--   parameter3 = target mode (0 self)
-- ===========================================================================

INSERT INTO `lwi_stage_action`
(
    `id`,
    `stage_id`,
    `action_order`,
    `action_type`,
    `target_id`,
    `parameter1`,
    `parameter2`,
    `parameter3`,
    `delay_seconds`,
    `enabled`,
    `comment`
)
VALUES
    (10007, 1001, 1, 4, 100,   2,      0,   1, 0, 1, 'Alliance-only Westfall zone warning; zone id derives from invasion'),
    (10001, 1001, 2, 1, 100,   0,      0,   0, 0, 1, 'Spawn Westfall Defias scouts and campfire'),
    (10008, 1001, 3, 5, 100, 847, 100001,   0, 0, 1, 'Temporary positional sound test from a Westfall scout'),
    (10005, 1001, 4, 3, 100, 100, 100001,   0, 0, 1, 'A scout says a warning before beginning the route'),
    (10004, 1001, 5, 2, 100, 100,    100, 100, 0, 1, 'Move Westfall Defias scout runtime group and emit ScoutRouteComplete'),
    (10002, 1002, 1, 1, 101,   0,      0,   0, 0, 1, 'Spawn Westfall Defias reinforcements'),
    (10003, 1003, 1, 1, 102,   0,      0,   0, 0, 1, 'Spawn Westfall Defias lieutenant'),
    (10009, 1003, 2, 6, 102, 1459, 100003,   0, 0, 1, 'Temporary scripted self-cast test: Arcane Intellect rank 1'),
    (10006, 1003, 3, 3, 102, 101, 100003,   0, 0, 1, 'The lieutenant yells after spawning');
