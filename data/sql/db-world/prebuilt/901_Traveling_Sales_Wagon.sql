-- ============================================================================
-- 901_Traveling_Sales_Wagon.sql
-- Phase-one prototype for the reusable LWI Traveling World Event subsystem.
--
-- FIRST MILESTONE
--   Prove that a normal Creature can own an authored LWI route while a real
--   GameObject wagon is continuously relocated behind it.
--
--   Route leader: creature_template entry chosen as the draft animal/anchor.
--   Wagon:        gameobject_template entry 180036 (Darkmoon Faire Wagon, unloaded).
--   Merchant:     deliberately disabled for this first movement-only test.
--
-- The leader + wagon are protected/non-aggro.  Once the mobile GO follows the
-- route cleanly, merchant attachment/camp setup becomes phase two.
-- ============================================================================

SET @EVENT_ID := 1;

-- Pick the draft-animal Creature entry after visually testing candidates.
-- Do NOT enable the event while this remains zero.
SET @LEADER_ENTRY := 582;

-- Confirmed physical wagon GameObject from in-game testing.
SET @WAGON_GO_ENTRY := 180036;

-- Phase one: no merchant.  Zero is explicitly supported by the runtime.
SET @MERCHANT_ENTRY := 0;

DELETE FROM `lwi_traveling_event_prop` WHERE `event_id` = @EVENT_ID;
DELETE FROM `lwi_traveling_event_stop` WHERE `event_id` = @EVENT_ID;
DELETE FROM `lwi_traveling_event` WHERE `id` = @EVENT_ID;

INSERT INTO `lwi_traveling_event`
    (`id`,`name`,`leader_entry`,`wagon_entry`,`merchant_entry`,
     `wagon_distance_behind`,`wagon_lateral_offset`,`wagon_vertical_offset`,
     `enabled`,`comment`)
VALUES
    (@EVENT_ID,'Traveling Sales Wagon',@LEADER_ENTRY,@WAGON_GO_ENTRY,@MERCHANT_ENTRY,
     4.5,0.0,0.75,
     0,
     'Phase-one mobile GO wagon test. Enable after choosing leader_entry.');

-- Existing route-network test loop:
--   Stormwind_Gate -> Goldshire -> Sentinel_Hill_Tower -> Goldshire -> Stormwind_Gate
--
-- Short 30-second stops make repeated travel legs easy to observe while testing.
INSERT INTO `lwi_traveling_event_stop`
    (`id`,`event_id`,`stop_order`,`route_node_id`,`dwell_seconds`,`arrival_text`,`departure_text`,`enabled`,`comment`)
VALUES
    (90101,@EVENT_ID,10,10,30,'','',1,'Stormwind Gate test stop'),
    (90102,@EVENT_ID,20,20,30,'','',1,'Goldshire outbound test stop'),
    (90103,@EVENT_ID,30,70,30,'','',1,'Sentinel Hill test stop'),
    (90104,@EVENT_ID,40,20,30,'','',1,'Goldshire return test stop');

-- No camp props during phase one.  Keep the test focused on the moving GO.
--
-- After choosing a draft animal:
-- UPDATE lwi_traveling_event
-- SET leader_entry=<CREATURE_ENTRY>, enabled=1
-- WHERE id=1;
--
-- Then:
--   .lwi reload
--   .lwi travel start 1
--   .lwi travel status
--   .lwi travel stop 1
