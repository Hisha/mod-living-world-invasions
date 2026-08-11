# State Machine

## Scheduler state

Per invasion, persisted in `acore_characters.lwi_invasion_runtime`:

```text
Available -> Active -> Cooldown -> Available
```

A normal successful runtime increments completion state/counters and enters cooldown. An active-force failure or hard timeout also enters cooldown but is not a successful completion. A start failure returns the scheduler record to an available state rather than treating the invasion as completed.

## Scheduler control state

The scheduler itself can be Running, Paused, or Draining. `.lwi stop` uses draining behavior: no new invasions start while existing runtimes are allowed to finish.

## Runtime state

An `InvasionRuntime` progresses through ordered enabled stages. Each stage waits for its configured completion condition (currently Timer or Runtime Signal). Completing the final stage ends the runtime and triggers cleanup.

Runtime failure paths include active movement force wipe and hard maximum-runtime timeout.