-- ============================================================================
-- 901_Traveling_Sales_Wagon.sql
-- Traveling salesman prototype: one merchant + two creature-based pack mules.
--
-- CURRENT PROTOTYPE COLUMN MAPPING
--   leader_entry = traveling merchant creature (route owner)
--   wagon_entry  = pack-mule CREATURE entry
--
-- We intentionally keep the existing prototype table layout for this pass so
-- no database migration is needed while the traveling-event model is evolving.
-- merchant_entry and the old wagon offset columns are ignored by current code.
--
-- Selected visuals:
--   Merchant: entry 221 (Dannus), CreatureDisplayID 23
--   Pack mule: entry 5525 (Caravan Packhorse), tested in-game as display 14551
--
-- First test goal:
--   Merchant and TWO pack mules walk the authored route together.
--   No camp props are installed by this prebuilt yet.
-- ============================================================================

SET @EVENT_ID := 1;
SET @MERCHANT_ENTRY := 221;
SET @PACK_MULE_ENTRY := 5525;

DELETE FROM `lwi_traveling_event_prop` WHERE `event_id` = @EVENT_ID;
DELETE FROM `lwi_traveling_event_stop` WHERE `event_id` = @EVENT_ID;
DELETE FROM `lwi_traveling_event` WHERE `id` = @EVENT_ID;

INSERT INTO `lwi_traveling_event`
    (`id`,`name`,`leader_entry`,`wagon_entry`,`merchant_entry`,
     `wagon_distance_behind`,`wagon_lateral_offset`,`wagon_vertical_offset`,
     `merchant_seat_id`,`enabled`,`comment`)
VALUES
    (@EVENT_ID,
     'Traveling Salesman',
     @MERCHANT_ENTRY,
     @PACK_MULE_ENTRY,
     0,
     0,0,0,
     0,
     1,
     'Creature caravan prototype: merchant route owner plus two Pack Mule followers.');

-- Test loop:
--   Stormwind_Gate -> Goldshire -> Sentinel_Hill_Tower -> Goldshire -> Stormwind_Gate
INSERT INTO `lwi_traveling_event_stop`
    (`id`,`event_id`,`stop_order`,`route_node_id`,`dwell_seconds`,
     `arrival_text`,`departure_text`,`enabled`,`comment`)
VALUES
    (90101,@EVENT_ID,10,10,30,
     'I will be here for a short while if you need supplies.',
     'Come along, you two. Goldshire is next.',1,'Stormwind Gate stop'),
    (90102,@EVENT_ID,20,20,30,
     'Fresh goods from Stormwind! Have a look while we rest.',
     'Time to get moving. Westfall is waiting.',1,'Goldshire outbound stop'),
    (90103,@EVENT_ID,30,70,30,
     'Sentinel Hill! Supplies for anyone who needs them.',
     'Back toward Goldshire, then.',1,'Sentinel Hill stop'),
    (90104,@EVENT_ID,40,20,30,
     'Goldshire again. We will rest here for a moment.',
     'Stormwind Gate is our next stop.',1,'Goldshire return stop');

-- Camp props intentionally omitted in this pass.