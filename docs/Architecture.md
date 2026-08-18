
# Architecture

## Design boundary

LWI separates reusable engine mechanics from invasion content.

- **C++** owns scheduling, runtime lifecycle, entity providers, movement, signals, action execution, failure handling, and cleanup.
- **SQL** owns invasion definitions, stages, stage actions, spawn compositions, routes, dialogue, announcements, and most tuning.
- **AzerothCore** continues to own normal CreatureAI/SmartAI, faction behavior, combat, pathfinding/MMAP, spells, and world rules.

## High-level flow

```text
Scheduler
  -> selects invasion
  -> Runtime Manager creates runtime
  -> Runtime begins Stage
  -> Stage Actions execute in action_order
       -> announcement
       -> spawn runtime entity group
       -> dialogue / sound / spell
       -> movement
  -> Stage waits for timer or runtime signal
  -> Runtime advances to next Stage
  -> completion / failure / timeout
  -> movement cancelled + entities cleaned + scheduler cooldown
```

## Definition layer

`InvasionMgr` loads enabled definitions from `acore_world` into memory:

- response origins;
- invasions;
- stages;
- stage actions;
- spawn groups and members;
- movement paths, nodes, and profiles;
- runtime signals;
- dialogue;
- announcements.

Definitions contain no runtime GUIDs.

## Scheduler

`InvasionScheduler` decides when an invasion may start. It tracks Available, Active, and Cooldown states in `acore_characters.lwi_invasion_runtime` and enforces configured concurrency limits. Random selection considers enabled/random-start definitions and selection weight.

A manual `.lwi trigger <id>` still goes through scheduler/runtime start logic; it is a debug trigger, not a separate execution engine.

## Runtime engine

`InvasionRuntimeManager` owns active `InvasionRuntime` objects. A runtime has one current stage. Implemented stage completion types are:

- `0` Timer
- `1` Runtime Signal

Completion types `2` Objective and `3` Manual are reserved in schema comments but are not implemented by the current runtime switch.

A runtime can end by:

- completing all stages;
- explicit framework failure (currently used by active movement force wipe);
- exceeding `maximum_runtime_seconds`;
- emergency `.lwi abort confirm`.

## Stage actions

Implemented action types are:

1. Spawn Group
2. Start Route Journey
3. Dialogue
4. World Announcement
5. Sound
6. Spell

Actions are read in `action_order`. `delay_seconds` is loaded into the definition but is **not currently scheduled/executed as a delay**; design stages accordingly.

## Runtime entity groups

A Spawn Group definition is permanent SQL. Each execution creates a temporary runtime entity group containing the actual spawned entities and GUIDs. Later actions target the latest runtime group created from the referenced spawn group.

Entity providers currently support:

- `1` Creature
- `2` GameObject

Cleanup is provider-based.

## Creature tactical roles

Spawn members may use these roles:

- `0` Default
- `1` Commander
- `2` Protector
- `3` Melee DPS
- `4` Ranged DPS
- `5` Healer
- `6` Support

Roles currently influence LWI formation placement. They do not replace the creature's native AzerothCore AI, faction template, or spell behavior.

## Movement

`MovementController` moves living creatures in a runtime group using AzerothCore `PathGenerator`/MMAP. GameObjects remain tracked but are ignored by movement.

Movement supports:

- ordered path nodes;
- node wait times;
- movement profiles and per-node profile overrides;
- walk/run mode selection;
- role-aware formation destinations;
- terrain-aware MMAP paths;
- combat interruption and post-combat resume;
- casualty-tolerant intermediate arrival;
- final-objective arrival;
- completion-signal emission;
- force-wipe detection while movement is active.

### Intermediate arrival

The ideal case is all survivors reaching their exact formation destinations. When formation endpoints are imperfect, at least 75% of survivors within 10 yards starts a short regroup grace period. Intermediate movement does not advance while surviving members are in combat.

### Final objective arrival

The last node is treated as a strategic objective area. At least 75% of survivors within 20 yards of the final database node completes the path even if combat is active. This prevents a force that has physically reached its objective from waiting forever for perfect formation.

### Combat resume

LWI observes creatures interrupted by combat. After combat ends it recalculates an MMAP path toward that creature's current formation destination and resumes the strategic route.

### Force wipe

If an actively moving runtime group has no living creature entities, the movement controller fails the runtime immediately. The scheduler then puts that invasion into normal cooldown without counting it as a successful completion.

## Shared route graph

`lwi_route_node` and `lwi_route_segment` form reusable world-travel infrastructure above the existing movement-path system. A route segment references one `lwi_movement_path` and can be traversed in either direction. Connected segments form a graph, allowing a consumer to request travel between logical route nodes without hard-coding every intermediate segment.

The preferred road-authoring tool records the GM's traveled position every 5 yards. Dense deterministic spacing is intentional: the movement engine only knows the authored coordinates, not the semantic shape of a visible road, so long chords between sparse nodes can cut outside curves or fenced corridors.

See [RouteNetwork.md](RouteNetwork.md) for the graph model and authoring commands.

### Invasion-facing spatial model

The invasion layer now consumes route nodes rather than raw coordinates or movement-path IDs:

- spawn groups reference one `route_node_id`;
- stage action type 2 requests a route journey from a start route-node ID to a destination route-node ID;
- route-node actions attach invasion-specific dialogue/announcements/sounds to semantic route-node IDs.

The underlying `lwi_movement_path`/`lwi_movement_node` rows remain the physical execution layer owned by the route network. This keeps dense 5-yard breadcrumbs out of prebuilt invasion logic and allows routes to be rerecorded without changing invasion definitions as long as stable route-node IDs are preserved.

## Runtime signals

Runtime signals are reusable definitions stored in `acore_world`, while emitted signal state is transient and held by `RuntimeSignalManager`. Signals are idempotent within a runtime.

Movement can emit a configured completion signal, allowing a signal-completed stage to represent "march until the route/objective is reached."

## Native AI and faction behavior

LWI does not currently implement a separate combat AI or faction override layer. Spawned creatures retain their template/SmartAI/native faction relationships. Tactical roles affect formation only. If an invasion NPC should attack or heal another NPC, its normal AzerothCore faction/AI data must support that behavior.