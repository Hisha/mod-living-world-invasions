# Traveling World Events

Traveling World Events are non-invasion LWI runtimes for persistent civilian/world activity.

The first prototype is `901_Traveling_Sales_Wagon.sql`.

## Prototype lifecycle

`CAMPED -> TRAVELING -> CAMPED -> ...`

The wagon owns route movement. A merchant is mounted as a vehicle passenger when the wagon template supports a
VehicleKit. The merchant's vendor NPC flag is removed while traveling and restored only while camped.

Both wagon and merchant are continuously forced passive, non-attackable, and immune to PC/NPC combat during this
prototype. Combat-capable caravans can be added later as a separate policy.

Stops reference `lwi_route_node.id`, so all travel uses the same authored route graph as invasions.

## Commands

- `.lwi travel start <eventId>`
- `.lwi travel stop <eventId>`
- `.lwi travel status`

## First test

Apply `019_lwi_traveling_world_event.sql`, load `901_Traveling_Sales_Wagon.sql`, choose a wagon creature entry with a
working `VehicleId`, choose an existing vendor creature entry, enable event 1, then `.lwi reload`.

The prototype route is:

`Stormwind_Gate -> Goldshire -> Sentinel_Hill_Tower -> Goldshire -> Stormwind_Gate`

Camp props are optional and data-driven through `lwi_traveling_event_prop`.
