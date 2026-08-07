# Living World Invasions Roadmap

## Version 0.1 - Foundation

### Project

- [x] Repository created
- [x] Module compiles
- [x] Configuration loads
- [x] SQL installs
- [x] Logging framework

### Data Loading

- [x] Invasion definitions
- [x] Stage definitions
- [x] Stage actions
- [x] Spawn groups
- [x] Spawn members
- [x] Response origins

### Scheduler

- [x] Runtime scheduler
- [x] Random invasion selection
- [x] Cooldown system
- [x] Response origin capacity
- [x] Runtime state persistence

### Runtime

- [x] Runtime manager
- [x] Stage progression
- [x] Timer completion
- [x] Runtime cleanup
- [x] Runtime entity groups

### Commands

- [x] .lwi status

### Entity System

- [x] Creature provider
- [x] GameObject provider
- [x] Mixed entity groups
- [x] Spawn tracking
- [x] Provider-based cleanup
- [x] Runtime entity group ownership

---

# Version 0.2 - World Building

## Movement

- [x] Movement path definitions
- [x] Movement node definitions
- [x] Movement profile definitions
- [x] Movement data loading
- [x] Runtime movement controller
- [x] Ordered node traversal
- [x] Per-node wait times
- [x] Runtime-group movement
- [x] Movement path completion
- [ ] Walk/run mode enforcement
- [ ] Walk speed multipliers
- [ ] Run speed multipliers
- [ ] Node movement-profile overrides
- [ ] Stealth movement
- [ ] Patrol/loop routes
- [ ] Destination movement
- [ ] Escort/follow movement
- [ ] Formation movement

## Runtime Signals

- [ ] Runtime signal definitions
- [ ] Emit runtime signals
- [ ] Wait for runtime signals
- [ ] Movement-completion signals
- [ ] Entity-death signals
- [ ] Objective-completion signals

## Actions

- [ ] Dialogue - Say
- [ ] Dialogue - Yell
- [ ] Dialogue - Emote
- [ ] Zone announcements
- [ ] Faction announcements
- [ ] World announcements
- [ ] Sound actions
- [ ] Spell actions

## Behavior System

- [ ] Behavior definitions
- [ ] Behavior steps
- [ ] Behavior controller
- [ ] Activate behavior on runtime entity group
- [ ] Behavior completion
- [ ] Behavior failure
- [ ] Behavior interruption
- [ ] Behavior resume
- [ ] Wait behavior
- [ ] Movement behavior
- [ ] Dialogue behavior
- [ ] Signal behavior

## AI / Interaction

- [ ] Entity roles
- [ ] Faction overrides
- [ ] Hostility rules
- [ ] Native faction interaction validation
- [ ] Combat behavior profiles
- [ ] Non-combat behavior profiles
- [ ] Merchant/civilian behavior support

---

# Version 0.3 - Gameplay

## Response Forces

- [ ] Response force definitions
- [ ] Response force spawning
- [ ] Response routes
- [ ] Response force formations
- [ ] Commander support
- [ ] Response-force dialogue
- [ ] Reinforcement waves
- [ ] Response force arrival signals

## Playerbot Integration

- [ ] Detect invasion awareness
- [ ] Playerbot alert propagation
- [ ] Playerbot response recruitment
- [ ] Playerbot party/raid formation
- [ ] Player/playerbot force balancing
- [ ] Playerbot participation without dependency
- [ ] Invasion.Playerbots.Enable support

## Objectives

- [ ] Kill targets
- [ ] Kill-count objectives
- [ ] Commander/boss objectives
- [ ] Area control
- [ ] Escort objectives
- [ ] Survival objectives
- [ ] Destination/reach objectives
- [ ] Timed objectives
- [ ] GameObject objectives
- [ ] Failure conditions

## Participation

- [ ] Player participation tracking
- [ ] Playerbot participation tracking
- [ ] Contribution scoring
- [ ] Credit system
- [ ] Level-appropriate XP
- [ ] Level-appropriate gold
- [ ] Class/spec-appropriate rewards
- [ ] Reward framework
- [ ] Failure handling

## Achievements

- [ ] Invasion completion achievements
- [ ] Zone-specific achievements
- [ ] Multi-invasion achievements
- [ ] Optional meta-achievements

---

# Version 0.4 - Administration and Polish

## GM Commands

- [x] .lwi status
- [ ] Force-start invasion
- [ ] Stop invasion
- [ ] Reset invasion
- [ ] List invasions
- [ ] Inspect runtime
- [ ] Inspect runtime entity groups
- [ ] Inspect active behaviors
- [ ] Inspect active movement
- [ ] Reload LWI definitions

## Configuration

- [x] Global enable
- [x] Playerbot integration toggle
- [x] Debug toggle
- [ ] Maximum invasions per map/continent
- [ ] Maximum invasions per response origin
- [ ] Scheduling interval controls
- [ ] Global invasion frequency controls
- [ ] Reward controls
- [ ] Announcement controls

## Reliability

- [ ] Runtime recovery after server restart
- [ ] Cleanup orphaned runtime entities
- [ ] Validate invalid SQL references at startup
- [ ] Detect broken movement paths
- [ ] Detect missing entity providers
- [ ] Detect impossible response-force definitions
- [ ] Improved debug logging

---

# Version 1.0 - First Complete Invasion

## Westfall Invasion

- [ ] Defias scouting party
- [ ] Scout movement/recon
- [ ] Defias camp
- [ ] Camp GameObjects
- [ ] Escalation toward Sentinel Hill
- [ ] Flight-point alarm trigger
- [ ] Zone/faction warnings
- [ ] Stormwind response force
- [ ] Response force march
- [ ] Response-force dialogue along route
- [ ] Player/playerbot participation
- [ ] Battle at Sentinel Hill
- [ ] Defias commander encounter
- [ ] Victory state
- [ ] Failure/recovery state
- [ ] Rewards
- [ ] Achievements
- [ ] Full runtime cleanup
- [ ] Regression-test documentation

---

# Future Content

- [ ] Duskwood invasion
- [ ] Wetlands invasion
- [ ] Horde-zone invasions
- [ ] Outland invasions
- [ ] Merchant caravans
- [ ] Traveling NPC events
- [ ] Military patrols
- [ ] Seasonal invasions
- [ ] World bosses
- [ ] Civilian evacuations
- [ ] Refugee convoys
- [ ] Multi-zone invasions
- [ ] Cross-continent invasions