# mod-living-world-invasions
Create a Living World framework for AzerothCore that allows server owners to create dynamic invasions entirely through SQL, with no client modifications and no required core patches.

#Project Goals
- No client patches required.
- No required AzerothCore core patches.
- Playerbots support is optional.
- SQL-driven invasion definitions.
- Recoverable world state.
- Zone-appropriate encounters.
- Easy for server owners to create new invasions.
- Modular and data-driven architecture.

#Design Philosophy
Living World Invasions is not a collection of scripted events.
It is a framework for creating dynamic world events that make Azeroth feel alive.
Every invasion should:

- React to player activity.
- Progress through defined stages.
- Allow NPCs to respond naturally.
- Support optional Playerbot participation.
- Recover automatically without GM intervention.
- Be defined almost entirely through SQL.
