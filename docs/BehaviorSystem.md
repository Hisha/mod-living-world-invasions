# Living World Invasions - Behavior System

## Purpose

The Behavior System controls what spawned runtime entities do after creation.

It is separate from:

- Scheduler - decides when an invasion starts.
- Runtime - controls invasion lifecycle and stages.
- Stage Actions - initiate engine operations.
- Entity Providers - create and remove AzerothCore world entities.

The Behavior System operates on entities that already belong to an active runtime.

---

## Core Concepts

### Behavior Definition

A reusable sequence of behavior steps.

Examples:

- Scout Recon
- Military March
- Merchant Travel
- Retreat
- Patrol
- Defensive Hold

A behavior definition is permanent SQL data.

### Behavior Controller

A runtime object responsible for executing a behavior definition.

It tracks:

- runtime ID
- runtime group
- active behavior
- current behavior step
- step state
- waiting/completion state

### Movement Path

Defines where entities travel.

A path contains ordered movement nodes.

Paths contain geometry only and do not define the personality or purpose of the entity using them.

### Movement Node

An ordered destination within a path.

A node may specify:

- map
- X/Y/Z
- orientation
- wait time
- optional movement-profile override

A wait time of 0 causes immediate movement toward the next node.

### Movement Profile

Defines how a group traverses a path.

Initial profile properties include:

- walk speed multiplier
- run speed multiplier
- default movement mode
- stealth state

Movement profiles will later be extended with formation and additional movement behavior.

---

## Definition vs Runtime

Definitions are permanent reusable SQL data.

Runtime controllers and runtime entities are temporary.

Definitions must never store runtime GUIDs, active node indexes, current behavior state, or other temporary information.

---

## Behavior Lifecycle

A typical lifecycle:

Stage begins

→ Spawn Group

→ Runtime Group created

→ Activate Behavior

→ Behavior Controller starts

→ Movement Step begins

→ Movement Controller starts Path

→ Nodes are traversed

→ Path completes

→ Movement Step completes

→ Behavior advances

→ Behavior completes

→ Runtime is notified if required

---

## Behavior Steps

Behavior steps will be ordered operations.

Initial planned step types:

- Move
- Wait
- Dialogue
- Emote
- Spell
- Signal

Only movement will be implemented initially.

Additional step types must follow the same completion model.

Every asynchronous behavior step ultimately reports one of:

- Completed
- Failed
- Interrupted

---

## Runtime Groups

Behaviors are activated against runtime groups rather than permanent spawn-group definitions.

This allows one spawned group to change behavior without being destroyed and recreated.

Example:

Spawn Group 100

→ Runtime Group 5017

→ Activate Recon

→ Activate Attack

→ Activate Retreat

The permanent Spawn Group remains unchanged.

---

## Movement Architecture

Movement is divided into four responsibilities.

### Movement Path

Where to go.

### Movement Node

Individual destinations and optional node overrides.

### Movement Profile

How to travel.

### Movement Controller

Executes the path for the runtime group.

The Movement Controller does not decide why the entities are moving.

---

## Movement Completion

A path is complete when all enabled nodes have been reached and any final node wait has completed.

Completion is reported to the Behavior Controller.

Movement completion must not automatically advance an invasion stage unless a behavior or stage definition explicitly requests it.

---

## Interruption

Behaviors must eventually support interruption.

Example:

Merchant Traveling

→ Threat encountered

→ Traveling interrupted

→ Flee activated

→ Flee completes

→ Traveling may resume

The first implementation does not require resume support, but runtime architecture should not prevent it.

---

## Future Extensions

The Behavior System is expected to support:

- dialogue
- emotes
- spell casting
- formation changes
- combat-state changes
- faction/hostility behavior
- mounted movement
- movement-state changes
- runtime signals
- behavior interruption and resume