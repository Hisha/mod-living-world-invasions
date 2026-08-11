# Behavior System — Deferred Design

The previously proposed generic Behavior Controller/Behavior Step layer is **not part of the current implementation**. Current invasion behavior is expressed directly through stages, stage actions, runtime entity groups, movement paths, and runtime signals.

Do not author SQL against hypothetical behavior-definition/behavior-step tables.

If future invasion content demonstrates a need for a reusable behavior layer, it can be reconsidered then. See `Architecture.md` for the implemented model and `Roadmap.md` for future capabilities.