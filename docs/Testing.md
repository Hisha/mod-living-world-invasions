# Testing and GM Commands

## Configuration for development

Set:

```ini
LWI.Enable = 1
LWI.Debug = 1
LWI.Scheduler.Enable = 1
```

`LWI.Debug` is required for `.lwi trigger` and route authoring/test commands.

## Commands

| Command | Purpose |
|---|---|
| `.lwi version` | Version, scheduler state, active runtime count, loaded definition counts |
| `.lwi status` | Scheduler and runtime status report |
| `.lwi signals` | Current runtime signal state |
| `.lwi start` | Resume scheduler |
| `.lwi stop` | Drain scheduler; active runtimes continue |
| `.lwi reload` | Reload definitions when scheduler is stopped and no runtime is active |
| `.lwi trigger <id>` | Debug-trigger an enabled invasion |
| `.lwi abort` | Show emergency-abort warning |
| `.lwi abort confirm` | Immediately terminate active runtimes and clean tracked entities; scheduler remains stopped |
| `.lwi route path build <StartName> <EndName>` | Automatically record a reusable shared route segment every 5 yards |
| `.lwi route path status` | Show active automatic-build progress |
| `.lwi route path pause` / `resume` | Pause/resume automatic recording |
| `.lwi route path complete` | Finalize the current automatic route and create its segment |
| `.lwi route path show <pathId>` / `hide` | Show/remove temporary authored-node markers |
| `.lwi route path nearest <pathId>` | Identify the nearest authored movement node |
| `.lwi route test <segmentId> <fromNodeId>` | Test one shared segment from either endpoint |
| `.lwi route travel <fromNodeId> <destinationNodeId>` | Test multi-segment graph travel |

See [RouteNetwork.md](RouteNetwork.md) for the full shared-route authoring workflow.

## Normal edit/test loop

```text
1. .lwi stop
2. wait for active runtime(s) to finish
3. edit SQL
4. apply/reapply through the normal AzerothCore DB update workflow
5. .lwi reload
6. verify .lwi version / .lwi status
7. .lwi trigger <id>
8. observe in game and watch logs
```

If definitions are changed directly in the database for a quick development test, LWI still needs `.lwi reload` before cached definitions change. Keep repository SQL synchronized afterward so the database is not the only copy of the change.

## Expected movement milestones

Useful log patterns include:

```text
[LWI Spawn] ... executing spawn group ...
[LWI Creature] ... applied level override ...
[LWI Movement] ... started path ...
[LWI Movement] ... resumed MMAP movement ... after combat.
[LWI Movement] ... accepted formation arrival ... after regroup grace.
[LWI Movement] ... reached final objective ...
[LWI Movement] ... completed movement path ...
[LWI Signal] ... emitted signal ...
[LWI Runtime] ... stage ... satisfied by signal ...
```

## Expected force-wipe behavior

```text
[LWI Movement] ... was defeated while following path ... No living creature entities remain.
[LWI Runtime] ... failed: active movement force defeated.
[LWI Spawn] Cleaned runtime ...
[LWI Scheduler] ... failed because its active force was defeated and entered cooldown ...
```

This should occur shortly after the last moving creature dies, not at the hard runtime limit.

## Expected hard-timeout behavior

If a runtime genuinely becomes stuck beyond `maximum_runtime_seconds`, the runtime manager forces cleanup and the scheduler enters cooldown. Treat this as a safety net, not normal stage progression.

## New-invasion regression checklist

- [ ] Definition counts look correct after reload.
- [ ] Scheduler can select/trigger the invasion.
- [ ] Response-origin capacity behaves as intended.
- [ ] Spawn groups contain the expected entries/counts.
- [ ] GameObjects do not interfere with creature movement counts.
- [ ] Level overrides are visible and logged.
- [ ] Tactical roles produce a usable formation.
- [ ] Route follows MMAP/terrain without underground/floating movement.
- [ ] Shared road segments are authored with the automatic 5-yard builder and tested in both directions.
- [ ] Normal NPC aggro interrupts movement.
- [ ] Survivors resume movement after combat.
- [ ] Partial casualties do not stall intermediate nodes.
- [ ] Final objective completes even if combat is active.
- [ ] Movement signal advances the signal-completed stage.
- [ ] Full force wipe fails the runtime promptly.
- [ ] Successful runtime cleans remaining tracked entities.
- [ ] Failed runtime cleans remaining tracked entities.
- [ ] Hard timeout cleans a deliberately stuck runtime.
- [ ] Scheduler cooldown/state is correct after each outcome.