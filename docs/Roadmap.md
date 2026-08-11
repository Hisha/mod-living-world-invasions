# Living World Invasions Roadmap

## Framework baseline — complete enough for invasion authoring

- [x] SQL-driven invasion/stage/action definitions
- [x] Scheduler, cooldowns, selection weight, capacity limits
- [x] Runtime persistence tables and active runtime manager
- [x] Timer and runtime-signal stage completion
- [x] Creature/GameObject providers and runtime groups
- [x] Level overrides
- [x] Tactical roles and role-aware formation
- [x] MMAP movement paths and ordered nodes
- [x] Per-node waits and profile overrides
- [x] Walk/run movement mode
- [x] Combat interruption/resume
- [x] Casualty-tolerant regrouping
- [x] Final-objective arrival semantics
- [x] Moving-force wipe failure
- [x] Hard runtime timeout
- [x] Dialogue Say/Yell
- [x] Announcements with scope/faction filtering
- [x] Sound actions
- [x] Scripted self-cast spell action
- [x] GM status/start/stop/reload/abort/trigger/version/signals commands

## First real invasion — Westfall

- [ ] Replace framework test scenario with an authored invasion design
- [ ] Define final attacker composition and level tuning
- [ ] Define final spawn locations and routes
- [ ] Define stage progression and arrival signals
- [ ] Define Sentinel Hill assault behavior using available native AI/factions
- [ ] Add presentation: warnings, dialogue, sound, spell moments
- [ ] Tune cooldown/selection/runtime limits
- [ ] Test success path
- [ ] Test casualty and wipe paths
- [ ] Test interaction with normal NPCs and players/playerbots
- [ ] Document final SQL package and regression test

## Framework capabilities to revisit when content requires them

### Stage/action system

- [ ] Objective completion type
- [ ] Manual completion type
- [ ] Action delay scheduling
- [ ] Additional dialogue/emote actions
- [ ] Additional spell target modes

### Movement

- [ ] Apply movement-profile speed multipliers
- [ ] Apply movement-profile stealth
- [ ] Patrol/loop routes
- [ ] Escort/follow movement
- [ ] Destination-only movement helper

### AI / interaction

- [ ] Optional faction/hostility overrides
- [ ] Combat behavior profiles
- [ ] Non-combat/civilian behavior profiles
- [ ] Explicit commander/protector/healer tactical behavior beyond formation

### Gameplay

- [ ] Kill/kill-count objectives
- [ ] Commander/boss objectives
- [ ] Area-control objectives
- [ ] Escort/survival/GameObject objectives
- [ ] Player participation/contribution tracking
- [ ] Rewards and credit
- [ ] Achievements

### Playerbots

- [ ] Invasion awareness
- [ ] Alert propagation
- [ ] Response recruitment
- [ ] Party/raid formation
- [ ] Participation without making Playerbots a hard dependency

### Reliability / administration

- [ ] Complete world-entity recovery after worldserver restart
- [ ] Orphan runtime-entity detection
- [ ] Stronger cross-reference validation at definition load
- [ ] Broken-route diagnostics
- [ ] Additional runtime inspection commands

## Future content

- [ ] Duskwood invasion
- [ ] Wetlands invasion
- [ ] Horde-zone invasions
- [ ] Merchant caravans / military patrols / civilian events
- [ ] Seasonal events and world bosses
- [ ] Multi-zone and cross-continent events