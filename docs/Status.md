# Current Status — Framework Baseline

**Version reported by module:** `0.2.1-dev`

This snapshot marks the point where the core framework is considered sufficient to begin building/tuning the first real invasion primarily in SQL.

## Proven working

- SQL definition loading and reload.
- Scheduler random selection, cooldowns, weights, and concurrency limits.
- Response-origin capacity.
- Runtime stages with timer and signal completion.
- Creature and GameObject providers.
- Mixed runtime entity groups and cleanup.
- Creature `level_override`.
- Tactical roles and role-aware formation movement.
- MMAP/PathGenerator route traversal.
- Multi-node long-distance routes.
- Reusable bidirectional `lwi_route_node` / `lwi_route_segment` world-travel graph.
- Automatic in-game shared-route authoring at fixed 5-yard waypoint spacing.
- Single-segment forward/reverse testing and automatic multi-segment graph travel.
- Combat interruption and post-combat MMAP resume.
- Casualty-tolerant intermediate-node regrouping.
- Final-objective arrival while combat is active.
- Active movement force-wipe detection and runtime failure.
- Hard maximum-runtime cleanup.
- Runtime completion signals.
- Say/Yell dialogue.
- Scoped/faction announcements.
- Positional/direct sound actions.
- Scripted self-cast spell action.
- Scheduler drain/start, reload, emergency abort, status, signals, version, and debug trigger commands.

## Important current limitations

- Objective and Manual stage completion types are reserved but not implemented.
- `lwi_stage_action.delay_seconds` is loaded but not executed as a delay.
- Movement profile walk/run mode is applied; speed multipliers and stealth are loaded but not currently applied.
- Tactical roles affect formation, not combat AI or faction rules.
- No LWI faction override/hostility layer exists yet.
- Native creature template/SmartAI/faction behavior determines who attacks/heals whom.
- No dedicated LWI objective system, contribution/reward system, achievements, or Playerbot invasion-awareness integration yet.
- `LWI.Playerbots.Enable` exists as configuration, but dedicated Playerbot participation logic is not implemented.
- Active runtime rows persist stage state, but runtime world-entity GUID/state is not a complete restart-recovery system.
- Shared route authoring writes directly to the world database; portable SQL export of completed route networks is not automated yet.

## First content target

The next development phase is the first real Westfall invasion. The existing `900_lwi_scheduler_test_data.sql` is framework test content and should not be mistaken for the final invasion design.

The recommended approach is to build the invasion iteratively in SQL and only return to C++ when testing exposes a reusable framework capability that cannot reasonably be expressed with the current data model.