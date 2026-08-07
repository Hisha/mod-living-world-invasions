# Runtime Engine (Milestone 0.3)

The scheduler now decides **when** an invasion starts, then hands execution to the runtime engine.

```text
Scheduler
    -> InvasionRuntimeManager
        -> InvasionRuntime
            -> SQL-defined stages
        -> completion callback
    -> Scheduler cooldown
```

## Current scope

- Stages load from `lwi_invasion_stage`.
- Each active invasion receives a persistent runtime ID.
- Timer-completed stages advance independently.
- Active stage progress persists in `acore_characters.lwi_active_runtime`.
- `.lwi status` includes active runtime and stage information.
- Completing the final stage notifies the scheduler, which applies the invasion cooldown.

This milestone intentionally performs no creature spawning. The next milestone will attach generic actions, beginning with spawn-group actions, to stages.

## Clean development installation

Before importing this milestone on a pre-release development database:

```sql
-- acore_world
DROP TABLE IF EXISTS `lwi_invasion_stage`;

-- acore_characters
DROP TABLE IF EXISTS `lwi_active_runtime`;
DROP TABLE IF EXISTS `lwi_invasion_runtime`;
```

Import world base SQL in numeric order, then the test data file. Import character base SQL in numeric order.