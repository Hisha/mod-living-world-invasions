-- TEMPORARY scheduler/spawn framework data.
-- This is a complete development test invasion.
-- Safe to reapply: removes and recreates only test IDs.

-- ===========================================================================
-- Cleanup existing test data
-- ===========================================================================

DELETE FROM `lwi_stage_action`
WHERE `id` IN (10001,10002,10003);

DELETE FROM `lwi_spawn_member`
WHERE `id` IN (100001,100002,100003);

DELETE FROM `lwi_spawn_group`
WHERE `id` IN (100,101,102);

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
    `allow_random_start`,
    `enabled`,
    `comment`
)
VALUES
    (1, 'Westfall Scheduler Test', 0, 40, 1, 1, 10, 20, 100, 60, 120, 1, 1, 'Temporary scheduler/spawn test.'),
    (2, 'Duskwood Scheduler Test', 0, 10, 1, 1, 20, 30, 100, 60, 120, 1, 1, 'Temporary scheduler/spawn test.'),
    (3, 'Wetlands Scheduler Test', 0, 11, 1, 2, 20, 30, 100, 60, 120, 1, 1, 'Temporary scheduler/spawn test.');


-- ===========================================================================
-- Runtime Stages
-- completion_type:
-- 0 = timer
-- ===========================================================================

INSERT INTO `lwi_invasion_stage`
(
    `id`,
    `invasion_id`,
    `stage_order`,
    `name`,
    `duration_seconds`,
    `completion_type`,
    `enabled`,
    `comment`
)
VALUES
    (1001, 1, 10, 'Scouts',          20, 0, 1, 'Spawn engine test stage.'),
    (1002, 1, 20, 'Reinforcements',  20, 0, 1, 'Spawn engine test stage.'),
    (1003, 1, 30, 'Lieutenant',      20, 0, 1, 'Spawn engine test stage.'),

    (2001, 2, 10, 'Scouts',          20, 0, 1, 'Runtime framework test.'),
    (2002, 2, 20, 'Reinforcements',  20, 0, 1, 'Runtime framework test.'),
    (2003, 2, 30, 'Lieutenant',      20, 0, 1, 'Runtime framework test.'),

    (3001, 3, 10, 'Scouts',          20, 0, 1, 'Runtime framework test.'),
    (3002, 3, 20, 'Reinforcements',  20, 0, 1, 'Runtime framework test.'),
    (3003, 3, 30, 'Lieutenant',      20, 0, 1, 'Runtime framework test.');


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
-- ===========================================================================

INSERT INTO `lwi_spawn_member`
(
    `id`,
    `spawn_group_id`,
    `creature_entry`,
    `count`,
    `level_override`
)
VALUES
    (100001, 100, 449, 3, 0),
    (100002, 101, 589, 5, 0),
    (100003, 102, 441, 1, 0);


-- ===========================================================================
-- Stage Actions
-- action_type:
-- 1 = Spawn Group
-- ===========================================================================

INSERT INTO `lwi_stage_action`
(
    `id`,
    `stage_id`,
    `action_order`,
    `action_type`,
    `target_id`,
    `delay_seconds`,
    `enabled`,
    `comment`
)
VALUES
(
    10001,
    1001,
    1,
    1,
    100,
    0,
    1,
    'Spawn Westfall Defias scouts'
),
(
    10002,
    1002,
    1,
    1,
    101,
    0,
    1,
    'Spawn Westfall Defias reinforcements'
),
(
    10003,
    1003,
    1,
    1,
    102,
    0,
    1,
    'Spawn Westfall Defias lieutenant'
);