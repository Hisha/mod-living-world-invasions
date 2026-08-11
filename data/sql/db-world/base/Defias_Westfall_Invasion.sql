-- ===========================================================================
-- lwi_invasion Clear Data
-- ===========================================================================
DELETE FROM lwi_invasion
WHERE id IN (1);

-- ===========================================================================
-- lwi_invasion table(Name the invasion):
-- ===========================================================================
INSERT INTO lwi_invasion(id,name,map_id,zone_id,team,response_origin_id, recommended_min_level,recommended_max_level,selection_weight,minimum_cooldown_seconds,maximum_cooldown_seconds,maximum_runtime_seconds,allow_random_start,enabled,comment) VALUES (1,'Defias Westfall Invasion',0,40,1,1,10,20,100,79200,115200,3600,1,0,'Defias attack/control Sentinel Hill');

-- ===========================================================================
-- lwi_invasion Clear Data
-- ===========================================================================
DELETE FROM lwi_invasion_stage
WHERE id IN (1001,1002,1003,1004,1005);

-- ===========================================================================
-- lwi_invasion_stage(The stages of the invasion):
-- ===========================================================================
INSERT INTO lwi_invasion_stage(id,invasion_id,stage_order,name,duration_seconds,completion_type,completion_target_id,enabled,comment) VALUES(1001,1,10,'Defias Scouts',600,0,0,1,'Defias Scouts invade Sentinel Hill');
INSERT INTO lwi_invasion_stage(id,invasion_id,stage_order,name,duration_seconds,completion_type,completion_target_id,enabled,comment) VALUES(1002,1,20,'Defias Establish Control',600,0,0,1,'Defias establish control of Sentinel Hill');
INSERT INTO lwi_invasion_stage(id,invasion_id,stage_order,name,duration_seconds,completion_type,completion_target_id,enabled,comment) VALUES(1003,1,30,'Defias Leadership Arrives',600,0,0,1,'Defias Leadership arrives at Sentinel Hill');
INSERT INTO lwi_invasion_stage(id,invasion_id,stage_order,name,duration_seconds,completion_type,completion_target_id,enabled,comment) VALUES(1004,1,40,'Stormwind Response',600,0,0,1,'Stormwind response forces heads to Sentinel Hill');
INSERT INTO lwi_invasion_stage(id,invasion_id,stage_order,name,duration_seconds,completion_type,completion_target_id,enabled,comment) VALUES(1005,1,10,'Stormwind vs Defias',0,1,200,1,'Final battle to destroy the Defias at Sentinel Hill');

-- lwi_stage_action():


-- lwi_spawn_group():


-- lwi_spawn_member():


-- lwi_movement_parh():


-- lwi_movement_mode():


-- lwi_runtime_signal():


-- lwi_dialogue():


-- lwi_announcement():