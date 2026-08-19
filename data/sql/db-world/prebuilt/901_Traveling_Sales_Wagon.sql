-- ============================================================================
-- 901_Traveling_Sales_Wagon.sql
-- Prototype for the reusable LWI Traveling World Event subsystem.
--
-- PURPOSE
--   Prove:
--     * wagon is the route/movement owner
--     * one merchant rides the wagon when the selected wagon supports vehicles
--     * wagon + merchant are protected/non-aggro
--     * merchant cannot vend while traveling
--     * merchant dismounts and vends while camped
--     * optional camp props appear only while camped
--     * event loops through authored LWI route nodes
--
-- IMPORTANT FIRST-TEST SETUP
--   Set @WAGON_ENTRY to a creature_template entry that has a working VehicleId.
--   Set @MERCHANT_ENTRY to an EXISTING vendor creature entry whose inventory you
--   want to use for the prototype. The runtime vendor flag is removed while
--   traveling and restored while camped.
--
--   This prebuilt is intentionally DISABLED until those two entries are chosen.
-- ============================================================================

SET @EVENT_ID := 1;

-- TODO FOR FIRST TEST:
SET @WAGON_ENTRY := 0;
SET @MERCHANT_ENTRY := 0;
SET @MERCHANT_SEAT := 0;

DELETE FROM `lwi_traveling_event_prop` WHERE `event_id` = @EVENT_ID;
DELETE FROM `lwi_traveling_event_stop` WHERE `event_id` = @EVENT_ID;
DELETE FROM `lwi_traveling_event` WHERE `id` = @EVENT_ID;

INSERT INTO `lwi_traveling_event`
    (`id`,`name`,`wagon_entry`,`merchant_entry`,`merchant_seat_id`,`enabled`,`comment`)
VALUES
    (@EVENT_ID,'Traveling Sales Wagon',@WAGON_ENTRY,@MERCHANT_ENTRY,@MERCHANT_SEAT,0,
     'Prototype caravan. Enable after choosing a working vehicle wagon and vendor entry.');

-- Test loop uses the route network already proven by the Westfall work:
--   Stormwind_Gate -> Goldshire -> Sentinel_Hill_Tower -> Goldshire -> Stormwind_Gate
--
-- Repeating Goldshire intentionally creates the return journey without needing
-- special reverse-loop code.
INSERT INTO `lwi_traveling_event_stop`
    (`id`,`event_id`,`stop_order`,`route_node_id`,`dwell_seconds`,`arrival_text`,`departure_text`,`enabled`,`comment`)
VALUES
    (90101,@EVENT_ID,10,10,120,
     'The traveling sales wagon has arrived! Come see what I have for sale!',
     'Pack it up! Goldshire is next!',1,'Stormwind Gate camp'),
    (90102,@EVENT_ID,20,20,120,
     'Fresh goods from Stormwind! I will be here for a short while!',
     'Westfall calls! Last chance before I move on!',1,'Goldshire outbound camp'),
    (90103,@EVENT_ID,30,70,120,
     'Sentinel Hill! The traveling merchant is open for business!',
     'Time to head back toward Goldshire!',1,'Sentinel Hill camp'),
    (90104,@EVENT_ID,40,20,120,
     'Back in Goldshire! Come browse before the wagon rolls north!',
     'Stormwind Gate is our next stop!',1,'Goldshire return camp');

-- Optional camp props.
-- Leave these commented until you choose gameobject_template entries you like.
-- Offsets are relative to the authored stop route node.
--
-- INSERT INTO `lwi_traveling_event_prop`
--     (`id`,`event_id`,`gameobject_entry`,`offset_x`,`offset_y`,`offset_z`,`orientation_offset`,`enabled`,`comment`)
-- VALUES
--     (90151,@EVENT_ID,<CAMPFIRE_GO_ENTRY>, 3.0,  2.0, 0.0, 0.0,1,'Campfire'),
--     (90152,@EVENT_ID,<CRATE_GO_ENTRY>,   -2.0,  2.5, 0.0, 0.5,1,'Merchant crate');

-- After choosing entries:
-- UPDATE lwi_traveling_event
-- SET wagon_entry=<wagon>, merchant_entry=<vendor>, merchant_seat_id=0, enabled=1
-- WHERE id=1;
--
-- Then:
--   .lwi reload
--   .lwi travel start 1
--   .lwi travel status
--   .lwi travel stop 1
