# Living World Invasions

Living World Invasions (LWI) is an AzerothCore module for building dynamic, staged world invasions primarily through SQL. It requires no client modification and no AzerothCore core patch.

**Current development version:** `0.2.1-dev`

## Current framework capabilities

The current framework can:

- schedule enabled invasions with weighted random selection and cooldowns;
- enforce global, per-map, and response-origin concurrency limits;
- persist scheduler state and active stage state in `acore_characters`;
- run ordered invasion stages completed by timers or runtime signals;
- execute stage actions for spawn groups, movement, dialogue, announcements, sounds, and scripted self-cast spells;
- spawn and track mixed Creature/GameObject runtime groups;
- apply creature level overrides and tactical roles;
- move temporary creature groups over MMAP paths in role-aware formations;
- define reusable bidirectional route nodes/segments and chain them through a shared world-travel graph;
- author shared road segments in game with an automatic 5-yard path recorder;
- pause movement for combat and resume survivors afterward;
- tolerate casualties and imperfect regrouping at intermediate nodes;
- recognize a final objective area without requiring combat to stop;
- fail a marching runtime immediately if its active creature force is wiped;
- clean up tracked runtime entities on completion, failure, timeout, or emergency abort;
- reload SQL definitions without restarting worldserver when no runtime is active.

## Quick start

1. Build/install the module normally with AzerothCore.
2. Copy `conf/mod_living_world_invasions.conf.dist` to the module configuration location used by your installation.
3. Start worldserver and verify the `lwi_*` tables install and definitions load.
4. Enable `LWI.Debug = 1` while developing invasions.
5. Use `.lwi status` and `.lwi version` to verify the framework.
6. Follow [docs/CreatingAnInvasion.md](docs/CreatingAnInvasion.md) to create invasion content.

## Documentation

- [Architecture](docs/Architecture.md) — framework ownership and runtime flow.
- [Database Schema](docs/DatabaseSchema.md) — current tables, values, and action parameter mappings.
- [Creating an Invasion](docs/CreatingAnInvasion.md) — practical SQL-first creation process.
- [Shared Route Network](docs/RouteNetwork.md) — reusable road graph, automatic 5-yard authoring, ID/name route commands, and SQL export tooling.
- [Testing](docs/Testing.md) — GM commands and validation checklist.
- [Current Status](docs/Status.md) — proven features, known limitations, and next development focus.
- [Roadmap](docs/Roadmap.md) — remaining framework/gameplay work.
- [Coding Standards](docs/CodingStandards.md) — project conventions.

## Important development rule

The files under `data/sql/*/base` are the canonical clean-install schema/content for this pre-release module. AzerothCore's database updater tracks their hashes. During development, do not manually import those same repository update files and then also expect the updater to manage them unchanged. Test-data files that are intentionally reapplied must be written to be idempotent.

## Project philosophy

LWI is a framework rather than a collection of hard-coded events. Invasion-specific tuning—NPC entries, levels, routes, timing, announcements, compositions, and stage structure—should live in SQL wherever the current framework supports it. C++ should provide reusable mechanics, not encode a particular invasion.

### Route network publishing

Use `.lwi route export network` with `LWI.Debug = 1` to generate `lwi_exports/801_routes.sql`, then copy it into `data/sql/db-world/prebuilt/801_routes.sql` when publishing the current canonical route network.
