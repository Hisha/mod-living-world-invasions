-- ===========================================================================
-- lwi_invasion Clear Data
-- ===========================================================================
DELETE FROM lwi_invasion
WHERE id IN (1);

-- ===========================================================================
-- lwi_invasion_stage Clear Data
-- ===========================================================================
DELETE FROM lwi_invasion_stage
WHERE id IN (1001,1002,1003,1004,1005,1006);

-- ===========================================================================
-- lwi_stage_action Clear Data
-- ===========================================================================
DELETE FROM lwi_stage_action
WHERE id IN (10001,10002,10003,10004,10005,10006,10007,10008,10009,10010,10011,10012,10013,10014,10015,10016,10017,10018,10019,10020,10021,10022);

-- ===========================================================================
-- lwi_spawn_group Clear Data
-- ===========================================================================
DELETE FROM lwi_spawn_group
WHERE id IN (100,101,102,103,104,105,106);

-- ===========================================================================
-- lwi_creature_template Clear Data
-- ===========================================================================
DELETE FROM lwi_creature_ability
WHERE id IN (100);

-- ===========================================================================
-- lwi_creature_template Clear Data
-- ===========================================================================
DELETE FROM lwi_creature_template
WHERE id IN (1,2,3);

-- ===========================================================================
-- lwi_spawn_member Clear Data
-- ===========================================================================
DELETE FROM lwi_spawn_member
WHERE id IN (100001,100002,100003,100004,100005,100006,100007,100008,100009,100010,100011,100012);

-- ===========================================================================
-- lwi_movement_path Clear Data
-- ===========================================================================
DELETE FROM lwi_movement_path
WHERE id IN (100,101,102,103,104,105);

-- ===========================================================================
-- lwi_movement_node Clear Data
-- ===========================================================================
DELETE FROM lwi_movement_node
WHERE id IN (10000,10001,10002,10003,10004,10005,10006,10007,10008,10009,10010,10011,10012,10013,10014,10015,10016,10017);

-- ===========================================================================
-- lwi_movement_profile Clear Data
-- ===========================================================================
DELETE FROM lwi_movement_profile
WHERE id IN (100);

-- ===========================================================================
-- lwi_runtime_signal Clear Data
-- ===========================================================================
DELETE FROM lwi_runtime_signal
WHERE id IN (100,101,102,103,104);

-- ===========================================================================
-- lwi_dialogue Clear Data
-- ===========================================================================
DELETE FROM lwi_dialogue
WHERE id IN (100,101);

-- ===========================================================================
-- lwi_announcement Clear Data
-- ===========================================================================
DELETE FROM lwi_announcement
WHERE id IN (100);

-- ===========================================================================
-- lwi_invasion table(Name the invasion):
-- ===========================================================================
INSERT INTO lwi_invasion(id,name,map_id,zone_id,team,response_origin_id, recommended_min_level,recommended_max_level,selection_weight,minimum_cooldown_seconds,maximum_cooldown_seconds,maximum_runtime_seconds,allow_random_start,enabled,comment) 
VALUES (1, 'Defias Westfall Invasion', 0, 40, 1, 1, 10, 20, 100, 79200, 115200, 3600, 1, 0, 'Defias attack/control Sentinel Hill');

-- ===========================================================================
-- lwi_invasion_stage(The stages of the invasion):
-- ===========================================================================
INSERT INTO lwi_invasion_stage(id,invasion_id,stage_order,name,duration_seconds,completion_type,completion_target_id,enabled,comment) 
VALUES (1001, 1, 10, 'Defias Scouts', 0, 1, 100, 1, 'Defias Scouts establish staging point over looking Sentinel Hill'),
       (1002, 1, 20, 'Defias Populate Staging', 10, 0, 0, 1, 'Defias populate strike force at staging point.'),
       (1003, 1, 30, 'Defias Establish Control', 600, 0, 0, 1, 'Defias establish control of Sentinel Hill'),
       (1004, 1, 40, 'Defias Leadership Arrives', 10, 0, 0, 1, 'Defias Leadership arrives at Sentinel Hill'),
       (1005, 1, 50, 'Stormwind Response', 0, 1, 103, 1, 'Stormwind response forces heads to Sentinel Hill'),
       (1006, 1, 60, 'Stormwind vs Defias', 0, 1, 104, 1, 'Final battle to destroy the Defias at Sentinel Hill');

-- ===========================================================================
-- lwi_stage_action():
-- ===========================================================================
INSERT INTO lwi_stage_action(id,stage_id,action_order,action_type,target_id,parameter1,parameter2,parameter3,delay_seconds,enabled,comment)
VALUES (10001, 1001, 1, 1, 100, 0, 0, 0, 0, 1, 'Spawn Defias Scout group.'),
	   (10002, 1001, 2, 1, 101, 0, 0, 0, 0, 1, 'Spawn Defias 2nd Scout group.'),
       (10003, 1001, 3, 2, 100, 100, 100, 100, 0, 1, 'Move Defias Scout Group to staging point and emit ScoutRouteComplete'),
       (10004, 1001, 4, 2, 101, 100, 100, 100, 0, 1, 'Move Defias 2nd Scout Group to staging point and emit ScoutRouteComplete'),
       (10005, 1002, 1, 3, 100, 100, 0, 0, 0, 1, 'A scout says a warning.'),
       (10006, 1002, 2, 1, 102, 0, 0, 0, 0, 1, 'Spawn Defias Control team.'),
       (10007, 1002, 3, 1, 103, 0, 0, 0, 0, 1, 'Spawn Defias 2nd Control team.'),
       (10008, 1002, 4, 1, 104, 0, 0, 0, 0, 1, 'Spawn Defias 3rd Control team.'),
       (10009, 1003, 1, 4, 100, 2, 0, 1, 0, 1, 'Alliance-only Westfall zone warning; zone id derives from invasion.'),
       (10010, 1003, 2, 2, 100, 102, 100, 0, 0, 1, 'Move Defias Scout group to Sentinel Hill.'),
       (10011, 1003, 3, 2, 101, 103, 100, 0, 0, 1, 'Move Defias 2nd Scout group to Sentinel Hill.'),
       (10012, 1003, 4, 2, 102, 102, 100, 0, 0, 1, 'Move Defias Control group to Sentinel Hill.'),
       (10013, 1003, 5, 2, 103, 103, 100, 0, 0, 1, 'Move Defias 2nd Control group to Sentinel Hill.'),
       (10014, 1003, 6, 2, 104, 104, 100, 0, 0, 1, 'Move Defias 3rd Control group to Sentinel Hill.'),
       (10015, 1003, 7, 7, 100, 75, 2000, 7, 0, 1, 'Defias Scouts assault Sentinel Hill.'),
       (10016, 1003, 8, 7, 101, 75, 2000, 7, 0, 1, 'Defias 2nd Scouts assault Sentinel Hill.'),
       (10017, 1003, 9, 7, 102, 75, 2000, 7, 0, 1, 'Defias Control Team assaults Sentinel Hill.'),
       (10018, 1003, 10, 7, 103, 75, 2000, 7, 0, 1, 'Defias 2nd Control Team assaults Sentinel Hill.'),
       (10019, 1003, 11, 7, 104, 75, 2000, 7, 0, 1, 'Defias 3rd Control Team assaults Sentinel Hill.'),
       (10020, 1004, 1, 1, 105, 0, 0, 0, 0, 1, 'Spawn Defias Leadership.'),
       (10021, 1004, 2, 3, 105, 101, 100005, 0, 0, 1, 'Defias Leadership yells a warning.'),
       (10022, 1004, 3, 7, 105, 75, 2000, 7, 0, 1, 'Defias Leadership assaults Sentinel Hill.');

-- ===========================================================================
-- lwi_spawn_group(Allows for grouping of spawned NPCs.):
-- ===========================================================================
INSERT INTO lwi_spawn_group(id,name,map_id,x,y,z,orientation,spawn_radius,enabled) 
VALUES (100, 'Defias Scouts', 0, -10898.128, 1466.9028, 42.519577, 5.4439473, 10, 1),
	   (101, 'Defias 2nd Scouts', 0, -10898.128, 1466.9028, 42.519577, 5.4439473, 10, 1),
       (102, 'Defias Control Team', 0, -10490.681, 1212.7977, 67.30977, 4.8823605, 10, 1),
       (103, 'Defias 2nd Control Team', 0, -10711.868, 1192.233, 63.428204, 5.3473687, 10, 1),
       (104, 'Defias 3rd Control Team', 0, -10711.868, 1192.233, 63.428204, 5.3473687, 10, 1),
       (105, 'Defias Leadership', 0, -10509.177, 1046.7267, 60.51838, 4.956951, 10, 1),
       (106, 'Stormwind Response Force', 0, -9005.095, 480.13635, 96.55263, 3.814248, 10, 1);

-- ===========================================================================
-- lwi_creature_template(Create custom NPCs to be able to rename/reRank/etc an existing NPC.):
-- ===========================================================================
INSERT INTO lwi_creature_template(id,name,base_creature_entry,name_override,subname_override,faction_override,rank_override,health_modifier_override,armor_modifier_override,damage_modifier_override,enabled,comment) 
VALUES (1, 'Defias Ogre Brute', 644, 'Defias Ogre Brute', 'Defias Brotherhood', 17, 0, 3.0, 1.5, NULL, 1, 'Defias Ogre based on Rhahk Zor'),
	   (2, 'Westfall Invasion Boss', 639, 'Captain Garrick Vane', 'Defias Field Commander', 17, 1, 5.0, 1.5, 2.0, 1, 'Westfall Invasion Leader based on Edwin VanCleef'),
       (3, 'Defias Field Medic', 4418, 'Defias Field Medic', 'Defias Brotherhood', 17, 0, 1.0, 1.0, 0.7, 1, 'Defias healer based on Defias Wizard');

-- ===========================================================================
-- lwi_creature_ability(Data-driven combat abilities for LWI templates.):
-- ===========================================================================
INSERT INTO lwi_creature_ability(id,lwi_template_id,spell_id,target_type,priority,health_threshold_pct,cooldown_ms,range_yards,require_combat,enabled,comment)
VALUES (100, 3, 2054, 1, 100, 70.0, 4000, 30.0, 1, 1, 'Defias Field Medic heals the lowest-health LWI ally at or below 70%.');

-- ===========================================================================
-- lwi_spawn_member(Gives the spawn list of NPCs being used in invasion.):
-- ===========================================================================
INSERT INTO lwi_spawn_member(id,spawn_group_id,entity_type,entity_entry,lwi_template_id,count,level_override,tactical_role,comment) 
VALUES (100001, 100, 1, 449, NULL, 3, 25, 3, 'Defias scouts - Melee DPS'),
	   (100002, 101, 1, 449, NULL, 3, 25, 3, 'Defias 2nd scouts - Melee DPS'),
       (100003, 102, 1, 589, NULL, 15, 25, 4, 'Defias Control Team - Ranged DPS'),
       (100004, 102, 1, 0, 3, 10, 25, 5, 'Defias Control Team - Healer'),
       (100005, 102, 1, 0, 1, 5, 25, 3, 'Defias Control Team - Melee DPS'),
       (100006, 103, 1, 589, NULL, 15, 25, 4, 'Defias 2nd Control Team - Ranged DPS'),
       (100007, 103, 1, 0, 3, 10, 25, 5, 'Defias 2nd Control Team - Healer'),
       (100008, 103, 1, 0, 1, 5, 25, 3, 'Defias 2nd Control Team - Melee DPS'),
       (100009, 104, 1, 589, NULL, 15, 25, 4, 'Defias 3rd Control Team - Ranged DPS'),
       (100010, 104, 1, 0, 3, 10, 25, 5, 'Defias 3rd Control Team - Healer'),
       (100011, 104, 1, 0, 1, 5, 25, 3, 'Defias 3rd Control Team - Melee DPS'),
       (100012, 105, 1, 0, 2, 1, 35, 1, 'Defias Leadership - Melee DPS');

-- ===========================================================================
-- lwi_movement_path(Grouping for the path the NPCs will take.):
-- ===========================================================================
INSERT INTO lwi_movement_path(id,name,enabled,comment)
VALUES (100, 'Defias Scout Route 1', 1, 'First Defias scout route to staging point.'),
       (101, 'Defias Scout Route 2', 1, 'Second Defias scout route to staging point.'),
       (102, 'Defias West Assault Route', 1, 'Western Defias force route into Sentinel Hill.'),
       (103, 'Defias North Assault Route', 1, 'Northern Defias force route into Sentinel Hill.'),
       (104, 'Defias South Assault Route', 1, 'Southern Defias force route into Sentinel Hill.'),
       (105, 'Stormwind Response Force Route', 1, 'Stormwind response route to Sentinel Hill.');

-- ===========================================================================
-- lwi_movement_node(The path broken down by each node of it.):
-- ===========================================================================
INSERT INTO lwi_movement_node(id,path_id,node_order,map_id,x,y,z,orientation,wait_ms,profile_override_id,enabled,comment)
VALUES (10000, 100, 10, 0, -10898.128, 1466.9028, 42.519577, 5.4439473,   0, 0, 1, 'Defias Scout route node 10'),
       (10001, 100, 20, 0, -10816.262, 1388.863, 34.848286, 5.5813813,  0, 0, 1, 'Defias Scout route node 20'),
       (10002, 100, 30, 0, -10733.547, 1328.5238, 41.804104, 5.679553,   0, 0, 1, 'Defias Scout route node 30'),
       (10003, 100, 40, 0, -10563.534, 1303.9185, 47.28148, 5.408586,   0, 0, 1, 'Defias Scout route node 40'),
       (10004, 100, 50, 0, -10490.681, 1212.7977, 67.30977, 4.8823605, 0, 0, 1, 'Defias Scout route node 50'),
       (10005, 101, 10, 0, -10898.128, 1466.9028, 42.519577, 5.4439473,   0, 0, 1, 'Defias 2nd Scout route node 10'),
       (10006, 101, 20, 0, -10816.262, 1388.863, 34.848286, 5.5813813,  0, 0, 1, 'Defias 2nd Scout route node 20'),
       (10007, 101, 30, 0, -10733.547, 1328.5238, 41.804104, 5.679553,   0, 0, 1, 'Defias 2nd Scout route node 30'),
       (10008, 101, 40, 0, -10711.868, 1192.233, 63.428204, 5.3473687, 0, 0, 1, 'Defias 2nd Scout route node 40'),
       (10009, 102, 10, 0, -10497.578, 1145.7003, 44.997818, 4.261898,   0, 0, 1, 'Defias 1st move to SH route node 10'),
       (10010, 102, 20, 0, -10517.282, 1067.6008, 54.94022, 5.0943923,   0, 0, 1, 'Defias 1st move to SH route node 20'),
       (10011, 102, 30, 0, -10509.177, 1046.7267, 60.51838, 4.956951,   0, 0, 1, 'Defias 1st move to SH route node 30'),
       (10012, 103, 10, 0, -10712.419, 1138.1829, 45.58178, 5.696045,   0, 0, 1, 'Defias 2nd Control move to SH route node 10'),
       (10013, 103, 20, 0, -10658.855, 1150.3375, 33.672962, 5.994494,   0, 0, 1, 'Defias 2nd Control move to SH route node 20'),
       (10014, 103, 30, 0, -10626.727, 1126.5358, 33.848064, 4.23914,   0, 0, 1, 'Defias 2nd Control move to SH route node 30'),
       (10015, 104, 10, 0, -10712.419, 1138.1829, 45.58178, 5.696045,   0, 0, 1, 'Defias 3rd Control move to SH route node 10'),
       (10016, 104, 20, 0, -10683.241, 1054.9269, 35.460896, 5.3897476,   0, 0, 1, 'Defias 3rd Control move to SH route node 20'),
       (10017, 104, 30, 0, -10643.531, 1017.29474, 31.459375, 5.621436,   0, 0, 1, 'Defias 3rd Control move to SH route node 30');

-- ===========================================================================
-- lwi_movement_profile(How is the spawn group moving toward it's target.):
-- ===========================================================================
INSERT INTO lwi_movement_profile(id,name,default_mode,walk_speed_multiplier,run_speed_multiplier,stealth_enabled,enabled,comment)
VALUES (100, 'Defias Scout Movement', 2, 1.0, 1.0, 0, 1, 'Defias Scout group movement to Sentinel Hill.');

-- ===========================================================================
-- lwi_runtime_signal(Signals to indicate completeion of task.):
-- ===========================================================================
INSERT INTO lwi_runtime_signal(id,name,enabled,comment) 
VALUES (100, 'ScoutRouteComplete', 1, 'Emitted when the Defias scout movement route reaches staging point.'),
       (101, 'StagingComplete', 1, 'Emitted when the Defias spawns attack force at staging point.'),
       (102, 'LeadershipComplete', 1, 'Emitted when the Defias spawns leadership at Sentinel Hill.'),
       (103, 'StormwindResponseComplete', 1, 'Emitted when the Stormwind Response arrives at Sentinel Hill.'),
       (104, 'StormwindWins', 1, 'Emitted when the Stormwind Response secures Sentinel Hill.');

-- ===========================================================================
-- lwi_dialogue(Say/Yells by the NPCs):
-- ===========================================================================
INSERT INTO lwi_dialogue(id,name,text,chat_type,language,enabled,comment)
VALUES (100, 'Defias Scout Warning', 'Keep your eyes open. Sentinel Hill is ahead.', 0, 0, 1, 'Defias Scout say at staging point.'),
       (101, 'Defias Leadership Warning', 'The Brotherhood have taken Westfall!', 1, 0, 1, 'Defias Leadership announcing they control Westfall.');

-- ===========================================================================
-- lwi_announcement(Announcements to the world, limited by stage action parameters):
-- ===========================================================================
INSERT INTO lwi_announcement(id,name,text,enabled,comment)
VALUES (100, 'Westfall Alliance Warning', 'Defias activity has been reported near Sentinel Hill. Alliance forces in Westfall are advised to remain alert.', 1, 'Westfall area Defias warning.');
