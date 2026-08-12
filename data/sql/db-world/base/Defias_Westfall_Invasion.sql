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
-- lwi_spawn_group Clear Data
-- ===========================================================================
DELETE FROM lwi_spawn_group
WHERE id IN (100,101,102);

-- ===========================================================================
-- lwi_creature_template Clear Data
-- ===========================================================================
DELETE FROM lwi_creature_template
WHERE id IN (1);

-- ===========================================================================
-- lwi_spawn_member Clear Data
-- ===========================================================================
DELETE FROM lwi_spawn_member
WHERE id IN (100001,100002,100003);

-- ===========================================================================
-- lwi_runtime_signal Clear Data
-- ===========================================================================
DELETE FROM lwi_runtime_signal
WHERE id IN (100,101);

-- ===========================================================================
-- lwi_invasion table(Name the invasion):
-- ===========================================================================
INSERT INTO lwi_invasion(id,name,map_id,zone_id,team,response_origin_id, recommended_min_level,recommended_max_level,selection_weight,minimum_cooldown_seconds,maximum_cooldown_seconds,maximum_runtime_seconds,allow_random_start,enabled,comment) 
VALUES (1,'Defias Westfall Invasion',0,40,1,1,10,20,100,79200,115200,3600,1,0,'Defias attack/control Sentinel Hill');

-- ===========================================================================
-- lwi_invasion_stage(The stages of the invasion):
-- ===========================================================================
INSERT INTO lwi_invasion_stage(id,invasion_id,stage_order,name,duration_seconds,completion_type,completion_target_id,enabled,comment) 
VALUES(1001,1,10,'Defias Scouts',0,1,100,1,'Defias Scouts establish staging point over looking Sentinel Hill'),
      (1002,1,20,'Defias Populate Staging',0,1,101,1,'Defias populate strike force at staging point.'),
      (1003,1,30,'Defias Establish Control',600,0,0,1,'Defias establish control of Sentinel Hill'),
      (1004,1,40,'Defias Leadership Arrives',0,1,102,1,'Defias Leadership arrives at Sentinel Hill'),
      (1005,1,50,'Stormwind Response',0,1,103,1,'Stormwind response forces heads to Sentinel Hill'),
      (1006,1,60,'Stormwind vs Defias',0,1,104,1,'Final battle to destroy the Defias at Sentinel Hill');

-- lwi_stage_action():

-- ===========================================================================
-- lwi_spawn_group(Allows for grouping of spawned NPCs.):
-- ===========================================================================
INSERT INTO lwi_spawn_group(id,name,map_id,x,y,z,orientation,spawn_radius,enabled) 
VALUES (100,'Defias Scouts',0,-11045.854,1509.643,43.164726,5.41409933,10,1),
       (101,'Defias Control Team',0,-11045.854,1509.643,43.164726,5.41409933,10,1),
       (102,'Defias Leadership',0,-11045.854,1509.643,43.164726,5.41409933,10,1),
       (103, 'Stormwind Response Force', 0, <Need x for Stormwind Spawn>, <Need y for Stormwind Spawn>, <Need z for Stormwind Spawn>, <Need orientation for Stormwind Spawn>, 1, 1);

-- ===========================================================================
-- lwi_creature_template(Create custom NPCs to be able to rename/reRank/etc an existing NPC.):
-- ===========================================================================
INSERT INTO lwi_creature_template(id,name,base_creature_entry,name_override,subname_override,faction_override,rank_override,health_modifier_override,armor_modifier_override,damage_modifier_override,enabled,comment) 
VALUES (1, 'Defias Ogre Brute', 644, 'Defias Ogre Brute', NULL, 17, 0, 3.0, 1.5, NULL, 1, 'Defias Ogre based on Rhahk Zor'),
	   (2, 'Westfall Invasion Boss', 639, 'Captain Garrick Vane', 'Defias Field Commander', 17, 1, 5.0, 1.5, 2.0, 1, 'Westfall Invasion Leader based on Edwin VanCleef');

-- ===========================================================================
-- lwi_spawn_member(Gives the spawn list of NPCs being used in invasion.):
-- ===========================================================================
INSERT INTO lwi_spawn_member(id,spawn_group_id,entity_type,entity_entry,lwi_template_id,count,level_override,tactical_role,comment) 
VALUES (100001, 100, 1, 449, NULL, 10, 0, 3, 'Defias scouts - Melee DPS'),
       (100002, 100, 1, 589, NULL, 5, 0, 4, 'Defias scouts - Ranged DPS'),
       (100003, 100, 1, 545, NULL, 5, 0, 5, 'Defias scouts - Healer'),
       (100004, 101, 1, 0, 1, 3, 0, 3, 'Defias Control Team - Melee DPS'),
       (100005, 102, 1, 0, 2, 1, 0, 1, 'Defias Leadership - Melee DPS');

-- lwi_movement_parh():


-- lwi_movement_mode():


-- ===========================================================================
-- lwi_runtime_signal(Signals to indicate completeion of task.):
-- ===========================================================================
INSERT INTO lwi_runtime_signal(id,name,enabled,comment) 
VALUES (100,'ScoutRouteComplete',1,'Emitted when the Defias scout movement route reaches staging point.'),
       (101,'StagingComplete',1,'Emitted when the Defias spawns attack force at staging point.'),
       (102,'LeadershipComplete',1,'Emitted when the Defias spawns leadership at Sentinel Hill.'),
       (103,'StormwindReponseComplete',1,'Emitted when the Stormwind Response arrives at Sentinel Hill.'),
       (104,'StormwindWins',1,'Emitted when the Stormwind Response secures Sentinel Hill.');

-- lwi_dialogue():


-- lwi_announcement():