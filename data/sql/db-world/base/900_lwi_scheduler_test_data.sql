-- TEMPORARY scheduler framework data. These are not complete playable invasions.
INSERT INTO `lwi_response_origin`
    (`id`, `name`, `map_id`, `team`, `max_active_default`, `enabled`, `comment`)
VALUES
    (1, 'Stormwind', 0, 1, 1, 1, 'Temporary scheduler test origin.'),
    (2, 'Ironforge', 0, 1, 1, 1, 'Temporary scheduler test origin.')
ON DUPLICATE KEY UPDATE
    `name` = VALUES(`name`),
    `map_id` = VALUES(`map_id`),
    `team` = VALUES(`team`),
    `max_active_default` = VALUES(`max_active_default`),
    `enabled` = VALUES(`enabled`),
    `comment` = VALUES(`comment`);

INSERT INTO `lwi_invasion`
    (`id`, `name`, `map_id`, `zone_id`, `team`, `response_origin_id`,
     `recommended_min_level`, `recommended_max_level`, `selection_weight`,
     `minimum_cooldown_seconds`, `maximum_cooldown_seconds`, `allow_random_start`, `enabled`, `comment`)
VALUES
    (1, 'Westfall Scheduler Test', 0, 40, 1, 1, 10, 20, 100, 60, 120, 1, 1, 'Temporary log-only scheduler test.'),
    (2, 'Duskwood Scheduler Test', 0, 10, 1, 1, 20, 30, 100, 60, 120, 1, 1, 'Temporary log-only scheduler test.'),
    (3, 'Wetlands Scheduler Test', 0, 11, 1, 2, 20, 30, 100, 60, 120, 1, 1, 'Temporary log-only scheduler test.')
ON DUPLICATE KEY UPDATE
    `name` = VALUES(`name`),
    `map_id` = VALUES(`map_id`),
    `zone_id` = VALUES(`zone_id`),
    `team` = VALUES(`team`),
    `response_origin_id` = VALUES(`response_origin_id`),
    `recommended_min_level` = VALUES(`recommended_min_level`),
    `recommended_max_level` = VALUES(`recommended_max_level`),
    `selection_weight` = VALUES(`selection_weight`),
    `minimum_cooldown_seconds` = VALUES(`minimum_cooldown_seconds`),
    `maximum_cooldown_seconds` = VALUES(`maximum_cooldown_seconds`),
    `allow_random_start` = VALUES(`allow_random_start`),
    `enabled` = VALUES(`enabled`),
    `comment` = VALUES(`comment`);
