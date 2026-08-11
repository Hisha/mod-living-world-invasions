# Creating an Invasion

This is the baseline SQL-first workflow for creating an invasion with the current LWI framework.

## 1. Plan the invasion before writing SQL

Write the event as stages first. For each stage decide:

- what spawns;
- where it spawns;
- whether anything moves;
- what dialogue/announcement/sound/spell fires;
- what ends the stage: timer or runtime signal;
- what happens if the moving force is destroyed;
- how long the entire runtime may safely exist.

A useful pattern is:

```text
Stage 10: Spawn / March
  -> spawn force
  -> start movement
  -> movement emits Arrival signal
  -> stage waits for Arrival signal

Stage 20: Assault / Reinforcement
  -> spawn additional force
  -> timer or another movement signal

Stage 30: Commander / Resolution
  -> spawn commander
  -> dialogue/spell/etc.
  -> timer
  -> runtime completes and cleanup runs
```

## 2. Choose an ID range

Keep one consistent ID range for an invasion's related definitions. The framework does not allocate IDs automatically. Avoid collisions with existing LWI definitions.

## 3. Define or reuse a response origin

Insert `lwi_response_origin` first. The response origin is currently a scheduler/capacity concept. It allows related invasions to compete for a limited source such as Stormwind or Ironforge.

Set the correct map/team and decide how many simultaneous invasions that origin may support.

## 4. Create `lwi_invasion`

Define:

- map and zone;
- team;
- response origin;
- recommended level range;
- scheduler selection weight;
- minimum/maximum cooldown;
- hard `maximum_runtime_seconds`;
- whether random scheduling is allowed;
- enabled state.

During development, consider `allow_random_start = 0` until the invasion is ready for normal scheduler selection.

The hard runtime should be comfortably longer than the longest legitimate route/battle but short enough to clean up a genuinely stuck event.

## 5. Create stages

Insert ordered `lwi_invasion_stage` rows.

Use timer completion for simple timed phases. Use runtime-signal completion when a stage must wait for an asynchronous event currently supported by the framework, especially movement completion.

Do not use reserved Objective/Manual completion types yet.

## 6. Define spawn groups

Create one `lwi_spawn_group` for each distinct spawn location/composition that needs to be independently spawned or targeted later.

The group's XYZ is where entities initially appear. `spawn_radius` spreads individual spawns around that point.

## 7. Add spawn members

For each group, add `lwi_spawn_member` rows with:

- entity type;
- creature/GameObject entry;
- count;
- optional level override;
- tactical role.

For moving combat formations, deliberately assign roles. Commander/Protector/Melee/Ranged/Healer/Support roles produce different formation offsets.

Remember: roles do **not** give a creature tank/healer/combat logic. Native CreatureAI/SmartAI/faction data still decides actual combat behavior.

## 8. Build movement routes

If the group moves:

1. create `lwi_movement_path`;
2. create an `lwi_movement_profile` if you need explicit walk/run mode;
3. add ordered `lwi_movement_node` rows.

### Route design rules

- Use MMAP-friendly coordinates on traversable terrain.
- Follow roads and terrain with intermediate nodes instead of expecting one enormous path calculation.
- The spawn point does not need to duplicate node 10; node 10 is the first destination.
- Use `wait_ms` only when you actually want a pause at a node.
- The last node is special: it is treated as an objective area. The current engine accepts final arrival when at least 75% of living members are within 20 yards, even during combat.
- Intermediate nodes can tolerate formation disruption: 75% within 10 yards plus regroup grace can advance after combat.

## 9. Create runtime signals

For a movement-completed stage, create an `lwi_runtime_signal`, for example `WestfallForceArrived`.

Configure the movement stage action's `parameter3` to that signal ID and configure the stage as `completion_type = 1` with `completion_target_id` equal to the same signal.

This creates the chain:

```text
Start Movement -> final node reached -> signal emitted -> stage satisfied -> next stage
```

## 10. Add optional presentation definitions

Create reusable `lwi_dialogue` and `lwi_announcement` rows as needed.

Dialogue currently supports Say/Yell. Announcements can be delivered globally or filtered by map/zone/area and faction through the stage action.

## 11. Create stage actions

Add `lwi_stage_action` rows in the exact order they must execute.

A common movement-stage order is:

1. announcement;
2. spawn group;
3. sound/dialogue;
4. start movement.

A movement action must target a spawn group that has already been spawned in that runtime, because movement resolves the latest runtime entity group created from that spawn-group definition.

Do not rely on `delay_seconds` yet; it is not currently executed as an action delay. Use stages/timers instead when sequencing needs time separation.

## 12. Make development SQL re-runnable

During pre-release development, test-data SQL should clean up its own IDs before inserting them, in dependency-safe order. This avoids duplicate primary-key failures when AzerothCore reapplies a changed tracked SQL file.

Do not blindly delete shared definitions that another invasion uses. Scope cleanup to the IDs owned by your invasion/test package.

## 13. Reload safely

The supported development workflow is:

```text
.lwi stop
```

Wait for active runtimes to finish, then:

```text
.lwi reload
```

Reload is refused while active runtimes exist or while the scheduler is still Running. For an emergency development reset only:

```text
.lwi abort confirm
.lwi reload
```

Reload restarts the scheduler after rebuilding definitions.

## 14. Trigger the invasion

With `LWI.Debug = 1` and the scheduler running:

```text
.lwi trigger <invasion_id>
```

Use `.lwi status` and the server log while observing the event in game.

## 15. Validate the happy path

Confirm:

- announcement targeting is correct;
- every expected entity spawns;
- level overrides are applied;
- tactical formation looks reasonable;
- MMAP follows terrain;
- combat interruption resumes afterward;
- casualties do not permanently stall intermediate nodes;
- final arrival emits the expected signal;
- the next stage begins;
- final completion cleans all remaining tracked entities;
- scheduler enters cooldown.

## 16. Validate failure paths

At minimum test:

- full moving-force wipe;
- partial casualties;
- combat near an intermediate node;
- combat at the final objective;
- maximum-runtime safety cleanup;
- response-origin/map/global capacity rejection if relevant.

A full wipe during active movement should fail immediately and enter cooldown rather than waiting for the hard runtime timeout.

## 17. Tune content in SQL

Once mechanics work, tune composition, levels, counts, routes, stage duration, cooldown, text, sounds, and selection weight in SQL. Avoid adding invasion-specific C++ unless the content exposes a genuinely reusable missing framework capability.

## Minimal dependency order

A practical insertion order is:

```text
response origin
invasion
runtime signal(s)
dialogue / announcement definitions
spawn group(s)
spawn member(s)
movement profile(s)
movement path(s)
movement node(s)
stage(s)
stage action(s)
```

The exact physical order of independent definitions is flexible, but stage actions should be added only after their referenced IDs are known.

## Assaulting a settlement

Use stage action type `7` (`Start Assault`) when a spawned invasion group has
reached an objective and should actively initiate combat instead of waiting for
normal proximity aggro.

Mapping:

| Field | Meaning |
|---|---|
| `target_id` | Spawn group whose latest runtime entity group becomes the assault force |
| `parameter1` | Search radius in yards; `0` uses 40 yards |
| `parameter2` | Reacquire interval in milliseconds; `0` uses 2000 ms; minimum 500 ms |
| `parameter3` | Reserved |

Example:

```sql
INSERT INTO lwi_stage_action
(id, stage_id, action_order, action_type, target_id,
 parameter1, parameter2, parameter3, delay_seconds, enabled, comment)
VALUES
(20050, 2002, 10, 7, 200, 60, 2000, 0, 0, 1,
 'Defias assault Sentinel Hill after arrival');
```

The assault action does **not** replace AzerothCore combat AI. It periodically
looks for the nearest target AzerothCore considers hostile and attackable and
explicitly calls `AttackStart` from the invader side. Once combat starts,
CreatureAI/SmartAI and normal AzerothCore combat behavior take over.

This is particularly useful for settlement NPCs such as vendors, quest givers,
and similar civilians that may not proactively proximity-aggro the invasion
force. It does not globally modify faction templates or NPC definitions.

The behavior remains active for that runtime group until the invasion runtime
ends, fails, times out, or is aborted.