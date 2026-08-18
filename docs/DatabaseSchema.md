# Database Schema

This document describes the schema currently present in `data/sql`.

## Database placement

### `acore_world`
Permanent reusable definitions:

| Table | Purpose |
|---|---|
| `lwi_response_origin` | Logical response source and capacity grouping |
| `lwi_invasion` | Top-level invasion definition and scheduler tuning |
| `lwi_invasion_stage` | Ordered stages for an invasion |
| `lwi_stage_action` | Ordered actions executed when a stage begins |
| `lwi_spawn_group` | Spawn location/container definition |
| `lwi_spawn_member` | Creature/GameObject composition of a spawn group |
| `lwi_movement_path` | Reusable movement route |
| `lwi_movement_node` | Ordered path coordinates |
| `lwi_movement_profile` | Movement mode/profile data |
| `lwi_route_node` | Logical connection point in the shared world-travel graph |
| `lwi_route_segment` | Bidirectional graph edge backed by an `lwi_movement_path` |
| `lwi_runtime_signal` | Reusable signal names/IDs |
| `lwi_dialogue` | Say/Yell text definitions |
| `lwi_announcement` | Announcement text definitions |

### `acore_characters`
Realm/runtime state:

| Table | Purpose |
|---|---|
| `lwi_invasion_runtime` | Scheduler state, cooldowns, counters, active timestamps |
| `lwi_active_runtime` | Active runtime/stage persistence |

## `lwi_response_origin`

Key fields: `id`, `name`, `map_id`, `team`, `max_active_default`, `enabled`.

`team`: `0` neutral, `1` Alliance, `2` Horde. `max_active_default = 0` means unlimited.

## `lwi_invasion`

Key fields include map/zone/team, `response_origin_id`, recommended level range, `selection_weight`, cooldown range, `maximum_runtime_seconds`, `allow_random_start`, and `enabled`.

`maximum_runtime_seconds` is a hard safety limit and must be greater than zero.

`allow_random_start = 0` prevents scheduler random selection but does not make the definition unusable for debug/manual triggering.

## `lwi_invasion_stage`

Stages are ordered by `stage_order` within an invasion.

`completion_type`:

- `0` timer — uses `duration_seconds`;
- `1` runtime signal — waits for `completion_target_id`;
- `2` objective — reserved, not implemented;
- `3` manual — reserved, not implemented.

For signal stages, `completion_target_id` is an `lwi_runtime_signal.id`.

## `lwi_stage_action`

Actions execute in `action_order` when a stage begins.

| Type | Action | `target_id` | `parameter1` | `parameter2` | `parameter3` |
|---:|---|---|---|---|---|
| 1 | Spawn Group | spawn group ID | unused | unused | unused |
| 2 | Start Movement | spawn group ID | movement path ID | movement profile ID (`0` default) | completion signal ID (`0` none) |
| 3 | Dialogue | spawn group ID | dialogue ID | speaker member ID (`0` first creature) | reserved |
| 4 | World Announcement | announcement ID | scope | scope ID | faction |
| 5 | Sound | spawn group ID | SoundEntries ID | source member ID (`0` first creature) | playback mode |
| 6 | Spell | caster spawn group ID | spell ID | caster member ID (`0` first creature) | target mode |

Announcement scope: `0` global, `1` map, `2` zone, `3` area.

Announcement faction: `0` everyone, `1` Alliance, `2` Horde.

Sound playback: `0` distance/positional, `1` direct.

Spell target mode currently supports only `0` self.

**Current limitation:** `delay_seconds` is loaded but is not currently used to defer action execution.

## `lwi_spawn_group`

Defines `map_id`, X/Y/Z/orientation, spawn radius, and enabled state. The spawn group's coordinates are the initial spawn location, not the movement path's first destination.

## `lwi_spawn_member`

`entity_type`:

- `1` Creature
- `2` GameObject

`level_override`: `0` keeps the creature template level behavior; nonzero applies the requested level after spawn.

`tactical_role`:

- `0` Default
- `1` Commander
- `2` Protector
- `3` Melee DPS
- `4` Ranged DPS
- `5` Healer
- `6` Support

Tactical role currently controls formation positioning, not combat AI.

## Movement tables

### `lwi_movement_path`
Names/enables a reusable movement path.

### `lwi_movement_node`
Contains ordered nodes with `map_id`, coordinates, orientation, `wait_ms`, and optional `profile_override_id`. Nodes are ordered by `node_order`, not by primary-key value.

Use enough nodes to follow roads/terrain. For shared road infrastructure, the in-game automatic route builder records at a fixed 5-yard spacing so creatures reproduce curves, fenced corridors, bridges, and other constrained geometry reliably instead of taking long point-to-point chords.

### `lwi_movement_profile`
Fields: `default_mode`, walk/run speed multipliers, stealth flag, enabled state.

`default_mode`: `0` provider/default, `1` walk, `2` run.

The current movement controller applies walk/run mode and per-node profile selection. The speed multiplier and stealth fields are loaded but are not currently applied by movement execution.


## Shared route network

### `lwi_route_node`

Defines a reusable logical connection point with `name`, `map_id`, X/Y/Z/orientation, `arrival_radius`, enabled state, and optional comment. Nodes may represent road junctions, settlements, gates, bridges, docks, or other travel connection points.

### `lwi_route_segment`

Connects `start_node_id` and `end_node_id` using `movement_path_id`. A segment is shared infrastructure rather than invasion-owned data. Runtime consumers may traverse the same segment forward or reverse.

Connected segments form a graph. `.lwi route travel <fromNodeId|name> <destinationNodeId|name>` resolves a connected path through that graph and chains the necessary segments automatically. See [RouteNetwork.md](RouteNetwork.md) for authoring and test commands.

## `lwi_runtime_signal`

Reusable signal definition. Actual emitted signals are runtime-only memory state and are cleared with the runtime.

## `lwi_dialogue`

`chat_type`: `0` Say, `1` Yell. `language` uses the AzerothCore Language enum (`0` universal).

## `lwi_announcement`

Stores announcement text. Delivery scope and faction are supplied by the stage action rather than stored in the announcement definition.

## Runtime tables

`lwi_invasion_runtime.state`: `0` Available, `1` Active, `2` Cooldown.

`times_started` counts accepted starts. `times_completed` counts successful all-stage completions. A force-wipe failure enters cooldown but does not increment successful completion.

`lwi_active_runtime` stores runtime ID, invasion ID, current stage, stage timestamps, and overall start time. Runtime world entities themselves are not persisted in this table.

## Stage action type 7: Start Assault

`Start Assault` turns the latest runtime entity group for a spawn group into an
active assault force.

- `target_id`: spawn group id
- `parameter1`: target search radius in yards (`0` = 40)
- `parameter2`: reacquire interval milliseconds (`0` = 2000, minimum 500)
- `parameter3`: reserved

Target selection still respects AzerothCore hostility and attackability. LWI
forces the *invader* to initiate combat by calling its AI `AttackStart`, which
avoids requiring the target NPC to be the side that proximity-aggros first.
No faction-template database rows are modified.