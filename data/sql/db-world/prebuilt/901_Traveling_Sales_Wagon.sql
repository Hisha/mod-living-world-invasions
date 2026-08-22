-- ============================================================================
-- 901_Traveling_Sales_Wagon.sql
-- Traveling salesman: custom merchant + two creature-based pack mules.
-- Server-side only; no client patch required.
-- ============================================================================

SET @EVENT_ID := 1;
SET @MERCHANT_ENTRY := 14999990;
SET @MERCHANT_BASE := 221;       -- Dannus: display 23 / merchant visual
SET @PACK_MULE_ENTRY := 5525;    -- Caravan Packhorse
SET @CAMP_LAYOUT_ID := 1;         -- Reusable Traveling Salesman camp

-- LWI-owned custom camp GameObjects.
SET @GO_SALESMAN_TENT := 14999001;      -- clones stock 180031
SET @GO_SALESMAN_CRATE := 14999002;     -- clones stock 271
SET @GO_SALESMAN_CAMPFIRE := 14999003;  -- clones stock 1798

-- --------------------------------------------------------------------------
-- Custom LWI-owned camp GameObject templates.
-- One custom ID per logical asset; individual placements remain separate
-- layout-prop rows.
-- --------------------------------------------------------------------------
DELETE FROM `gameobject_template`
WHERE `entry` IN (@GO_SALESMAN_TENT,@GO_SALESMAN_CRATE,@GO_SALESMAN_CAMPFIRE);

INSERT INTO `gameobject_template`
    (`entry`,`type`,`displayId`,`name`,`IconName`,`castBarCaption`,`unk1`,`size`,
     `Data0`,`Data1`,`Data2`,`Data3`,`Data4`,`Data5`,`Data6`,`Data7`,
     `Data8`,`Data9`,`Data10`,`Data11`,`Data12`,`Data13`,`Data14`,`Data15`,
     `Data16`,`Data17`,`Data18`,`Data19`,`Data20`,`Data21`,`Data22`,`Data23`,
     `AIName`,`ScriptName`,`VerifiedBuild`)
SELECT
    @GO_SALESMAN_TENT,`type`,`displayId`,'LWI Traveling Salesman Tent',
    `IconName`,`castBarCaption`,`unk1`,`size`,
    `Data0`,`Data1`,`Data2`,`Data3`,`Data4`,`Data5`,`Data6`,`Data7`,
    `Data8`,`Data9`,`Data10`,`Data11`,`Data12`,`Data13`,`Data14`,`Data15`,
    `Data16`,`Data17`,`Data18`,`Data19`,`Data20`,`Data21`,`Data22`,`Data23`,
    `AIName`,`ScriptName`,`VerifiedBuild`
FROM `gameobject_template` WHERE `entry` = 180031;

INSERT INTO `gameobject_template`
    (`entry`,`type`,`displayId`,`name`,`IconName`,`castBarCaption`,`unk1`,`size`,
     `Data0`,`Data1`,`Data2`,`Data3`,`Data4`,`Data5`,`Data6`,`Data7`,
     `Data8`,`Data9`,`Data10`,`Data11`,`Data12`,`Data13`,`Data14`,`Data15`,
     `Data16`,`Data17`,`Data18`,`Data19`,`Data20`,`Data21`,`Data22`,`Data23`,
     `AIName`,`ScriptName`,`VerifiedBuild`)
SELECT
    @GO_SALESMAN_CRATE,`type`,`displayId`,'LWI Traveling Salesman Crates',
    `IconName`,`castBarCaption`,`unk1`,`size`,
    `Data0`,`Data1`,`Data2`,`Data3`,`Data4`,`Data5`,`Data6`,`Data7`,
    `Data8`,`Data9`,`Data10`,`Data11`,`Data12`,`Data13`,`Data14`,`Data15`,
    `Data16`,`Data17`,`Data18`,`Data19`,`Data20`,`Data21`,`Data22`,`Data23`,
    `AIName`,`ScriptName`,`VerifiedBuild`
FROM `gameobject_template` WHERE `entry` = 271;

INSERT INTO `gameobject_template`
    (`entry`,`type`,`displayId`,`name`,`IconName`,`castBarCaption`,`unk1`,`size`,
     `Data0`,`Data1`,`Data2`,`Data3`,`Data4`,`Data5`,`Data6`,`Data7`,
     `Data8`,`Data9`,`Data10`,`Data11`,`Data12`,`Data13`,`Data14`,`Data15`,
     `Data16`,`Data17`,`Data18`,`Data19`,`Data20`,`Data21`,`Data22`,`Data23`,
     `AIName`,`ScriptName`,`VerifiedBuild`)
SELECT
    @GO_SALESMAN_CAMPFIRE,`type`,`displayId`,'LWI Traveling Salesman Campfire',
    `IconName`,`castBarCaption`,`unk1`,`size`,
    `Data0`,`Data1`,`Data2`,`Data3`,`Data4`,`Data5`,`Data6`,`Data7`,
    `Data8`,`Data9`,`Data10`,`Data11`,`Data12`,`Data13`,`Data14`,`Data15`,
    `Data16`,`Data17`,`Data18`,`Data19`,`Data20`,`Data21`,`Data22`,`Data23`,
    `AIName`,`ScriptName`,`VerifiedBuild`
FROM `gameobject_template` WHERE `entry` = 1798;

-- --------------------------------------------------------------------------
-- Custom Traveling Salesman creature.
-- Keep it below LWI's generated-creature allocation range (15000000+).
-- Rebuild safely each time this prebuilt is applied.
-- --------------------------------------------------------------------------
DELETE FROM `npc_vendor` WHERE `entry` = @MERCHANT_ENTRY;
DELETE FROM `creature_template_model` WHERE `CreatureID` = @MERCHANT_ENTRY;
DELETE FROM `creature_template` WHERE `entry` = @MERCHANT_ENTRY;

INSERT INTO `creature_template` (
    `entry`,`difficulty_entry_1`,`difficulty_entry_2`,`difficulty_entry_3`,
    `KillCredit1`,`KillCredit2`,`name`,`subname`,`IconName`,`gossip_menu_id`,
    `minlevel`,`maxlevel`,`exp`,`faction`,`npcflag`,`speed_walk`,`speed_run`,
    `speed_swim`,`speed_flight`,`detection_range`,`rank`,`dmgschool`,
    `DamageModifier`,`BaseAttackTime`,`RangeAttackTime`,`BaseVariance`,`RangeVariance`,
    `unit_class`,`unit_flags`,`unit_flags2`,`dynamicflags`,`family`,`type`,`type_flags`,
    `lootid`,`pickpocketloot`,`skinloot`,`PetSpellDataId`,`VehicleId`,`mingold`,`maxgold`,
    `AIName`,`MovementType`,`HoverHeight`,`HealthModifier`,`ManaModifier`,`ArmorModifier`,
    `ExperienceModifier`,`RacialLeader`,`movementId`,`RegenHealth`,`CreatureImmunitiesId`,
    `flags_extra`,`ScriptName`,`VerifiedBuild`)
SELECT
    @MERCHANT_ENTRY,0,0,0,0,0,
    'Traveling Salesman','Traveling Merchant',b.`IconName`,0,
    b.`minlevel`,b.`maxlevel`,b.`exp`,b.`faction`,128,
    b.`speed_walk`,b.`speed_run`,b.`speed_swim`,b.`speed_flight`,b.`detection_range`,
    b.`rank`,b.`dmgschool`,b.`DamageModifier`,b.`BaseAttackTime`,b.`RangeAttackTime`,
    b.`BaseVariance`,b.`RangeVariance`,b.`unit_class`,b.`unit_flags`,b.`unit_flags2`,
    b.`dynamicflags`,b.`family`,b.`type`,b.`type_flags`,0,0,0,0,0,0,0,'',0,
    b.`HoverHeight`,b.`HealthModifier`,b.`ManaModifier`,b.`ArmorModifier`,
    b.`ExperienceModifier`,0,0,b.`RegenHealth`,b.`CreatureImmunitiesId`,
    b.`flags_extra`,'',b.`VerifiedBuild`
FROM `creature_template` b
WHERE b.`entry` = @MERCHANT_BASE;

INSERT INTO `creature_template_model`
    (`CreatureID`,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`,`VerifiedBuild`)
SELECT @MERCHANT_ENTRY,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`,`VerifiedBuild`
FROM `creature_template_model`
WHERE `CreatureID` = @MERCHANT_BASE;

-- Basic road/general-goods stock. The runtime removes the vendor flag while
-- traveling and restores it only while camped.
INSERT INTO `npc_vendor`
    (`entry`,`slot`,`item`,`maxcount`,`incrtime`,`ExtendedCost`,`VerifiedBuild`)
VALUES
    (@MERCHANT_ENTRY,0,117,  0,0,0,NULL),   -- Tough Jerky
    (@MERCHANT_ENTRY,1,159,  0,0,0,NULL),   -- Refreshing Spring Water
    (@MERCHANT_ENTRY,2,454,  0,0,0,NULL),   -- Tough Hunk of Bread
    (@MERCHANT_ENTRY,3,4496, 0,0,0,NULL),   -- Small Brown Pouch
    (@MERCHANT_ENTRY,4,2320, 0,0,0,NULL),   -- Coarse Thread
    (@MERCHANT_ENTRY,5,2321, 0,0,0,NULL),   -- Fine Thread
    (@MERCHANT_ENTRY,6,3371, 0,0,0,NULL);   -- Empty Vial

-- --------------------------------------------------------------------------
-- Traveling event.
-- Current prototype mapping:
--   leader_entry = merchant creature / route owner
--   wagon_entry  = pack-mule creature
-- --------------------------------------------------------------------------
DELETE FROM `lwi_traveling_event_prop` WHERE `event_id` = @EVENT_ID; -- legacy table, kept clean
DELETE FROM `lwi_traveling_event_stop` WHERE `event_id` = @EVENT_ID;
DELETE FROM `lwi_traveling_camp_layout_prop` WHERE `layout_id` = @CAMP_LAYOUT_ID;
DELETE FROM `lwi_traveling_camp_layout` WHERE `id` = @CAMP_LAYOUT_ID;
DELETE FROM `lwi_traveling_event` WHERE `id` = @EVENT_ID;

INSERT INTO `lwi_traveling_event`
    (`id`,`name`,`leader_entry`,`wagon_entry`,`merchant_entry`,
     `wagon_distance_behind`,`wagon_lateral_offset`,`wagon_vertical_offset`,
     `merchant_seat_id`,`enabled`,`comment`)
VALUES
    (@EVENT_ID,'Traveling Salesman',@MERCHANT_ENTRY,@PACK_MULE_ENTRY,0,
     0,0,0,0,1,
     'Custom traveling merchant with two Pack Mule followers; dedicated caravan formation.');

-- --------------------------------------------------------------------------
-- One reusable campsite layout. The stop route node is the center/origin.
--
--                 Crate     Tent     Crate
--
--            Mule #1                Merchant
--          Mule #2
--
--                                     Fire
--
-- Offsets below are intentionally easy to tune in SQL after the first visual
-- test. +forward follows the route-node orientation; +right is local right.
-- --------------------------------------------------------------------------
INSERT INTO `lwi_traveling_camp_layout`
    (`id`,`name`,
     `merchant_forward`,`merchant_right`,`merchant_z`,`merchant_orientation_offset`,
     `mule1_forward`,`mule1_right`,`mule1_z`,`mule1_orientation_offset`,
     `mule2_forward`,`mule2_right`,`mule2_z`,`mule2_orientation_offset`,
     `enabled`,`comment`)
VALUES
    (@CAMP_LAYOUT_ID,'Traveling Salesman Basic Camp',
     0.5, 1.5, 0, 3.14159,
    -3.0,-7.5, 0, 0,
    -4.5,-9.0, 0, 0,
     1,'Reusable roadside camp centered and rotated by the stop route node.');

INSERT INTO `lwi_traveling_camp_layout_prop`
    (`id`,`layout_id`,`gameobject_entry`,
     `forward_offset`,`right_offset`,`z_offset`,`orientation_offset`,
     `enabled`,`comment`)
VALUES
    (901101,@CAMP_LAYOUT_ID,@GO_SALESMAN_TENT, 4.0, 0.0,0,0,1,'Food Tent - purple/white'),
    (901102,@CAMP_LAYOUT_ID,@GO_SALESMAN_CRATE,    4.0,-3.0,0,0,1,'Crates - left of tent'),
    (901103,@CAMP_LAYOUT_ID,@GO_SALESMAN_CRATE,    4.0,3.0,0,0,1,'Crates - right of tent'),
    (901104,@CAMP_LAYOUT_ID,@GO_SALESMAN_CAMPFIRE,  -6.0,0,0,0,1,'Camp Fire');

-- --------------------------------------------------------------------------
-- Per-physical-camp terrain Z overrides.
--
-- 240 = Goldshire camp
-- 250 = Stormwind camp
-- 260 = Sentinel Hill camp
--
-- target_type: 1=merchant, 2=mule1, 3=mule2, 4=layout prop
-- For props, target_id is the layout-prop row ID:
--   901101 tent, 901102 left crates, 901103 right crates, 901104 campfire.
--
-- Goldshire's campfire is the first known correction from testing.
-- --------------------------------------------------------------------------
DELETE FROM `lwi_traveling_camp_node_z_override`
WHERE `route_node_id` IN (240,250,260);

INSERT INTO `lwi_traveling_camp_node_z_override`
    (`id`,`route_node_id`,`target_type`,`target_id`,`z_override`,`enabled`,`comment`)
VALUES
    (901201,240,4,901104,-0.75,1,'Goldshire: lower campfire to terrain');

INSERT INTO `lwi_traveling_event_stop`
    (`id`,`event_id`,`stop_order`,`route_node_id`,`camp_layout_id`,`dwell_seconds`,
     `arrival_text`,`departure_text`,`enabled`,`comment`)
VALUES
    (90101,@EVENT_ID,10,250,@CAMP_LAYOUT_ID,30,
     'I will be here for a short while if you need supplies.',
     'Come along, you two. Goldshire is next.',1,'Stormwind Gate stop'),
    (90102,@EVENT_ID,20,240,@CAMP_LAYOUT_ID,30,
     'Fresh goods from Stormwind! Have a look while we rest.',
     'Time to get moving. Westfall is waiting.',1,'Goldshire outbound stop'),
    (90103,@EVENT_ID,30,260,@CAMP_LAYOUT_ID,30,
     'Sentinel Hill! Supplies for anyone who needs them.',
     'Back toward Goldshire, then.',1,'Sentinel Hill stop'),
    (90104,@EVENT_ID,40,240,@CAMP_LAYOUT_ID,30,
     'Goldshire again. We will rest here for a moment.',
     'Stormwind Gate is our next stop.',1,'Goldshire return stop');