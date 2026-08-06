# Scheduler Framework Test

This code drop intentionally starts no creatures. It selects temporary invasion definitions, marks them active, completes them after a short test duration, applies a random cooldown, and persists runtime state.

## Pre-release database replacement rule

Drop the current development tables and import the complete canonical base SQL files. Do not commit ALTER statements before the first public release.

### acore_characters

```sql
DROP TABLE IF EXISTS `lwi_invasion_runtime`;
```

Import:

```text
data/sql/db-characters/base/001_lwi_invasion_runtime.sql
```

### acore_world

```sql
DROP TABLE IF EXISTS `lwi_invasion`;
DROP TABLE IF EXISTS `lwi_response_origin`;
```

Import in order:

```text
data/sql/db-world/base/001_lwi_response_origin.sql
data/sql/db-world/base/002_lwi_invasion.sql
data/sql/db-world/base/900_lwi_scheduler_test_data.sql
```

## Expected behavior

Within 20-40 seconds, map 0 is evaluated. One eligible test invasion is selected. It remains active for 45 seconds, then enters a 60-120 second cooldown. Stormwind can answer only one active test at a time; Ironforge has a separate response slot.

Runtime state survives a worldserver restart through `acore_characters.lwi_invasion_runtime`.

## Release content promise

The `900_` data is temporary scheduler test content. The public module will ship with at least one complete playable invasion and documentation that uses it as the reference implementation.
