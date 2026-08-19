# Traveling World Events

Traveling World Events are non-invasion LWI runtimes for persistent civilian/world activity such as traveling merchants, patrols, convoys, and a future Darkmoon caravan.

The first prototype is `901_Traveling_Sales_Wagon.sql`.

## Mobile GameObject wagon architecture

The prototype deliberately does **not** use WoW's Vehicle system for the wagon.

- A normal Creature (`leader_entry`) owns movement and uses the existing authored LWI route network.
- The visible wagon is a GameObject (`wagon_entry`) and is moved with `Map::GameObjectRelocation()` behind that Creature.
- The wagon offset is data driven through distance-behind, lateral, and vertical values.
- The leader is forced passive/non-attackable/immune so incidental combat cannot derail the route test.
- The optional merchant remains protected as well.

This separates the visual wagon from the movement anchor and lets LWI use actual wagon GameObjects such as `180036` rather than hunting for a creature model with a suitable `VehicleId`.

## Phase one

Phase one intentionally has no merchant. It proves only:

`draft-animal Creature -> authored LWI route -> mobile GameObject wagon follows`

The wagon is updated every 100 ms. Its Z currently follows the leader's Z plus the configured vertical offset. Terrain-height correction is intentionally deferred until the first visual route test tells us whether it is needed.

## Event lifecycle

`CAMPED -> TRAVELING -> CAMPED -> ...`

Stops reference `lwi_route_node.id`, so the same route graph used by invasions drives traveling events.

## Commands

- `.lwi travel start <eventId>`
- `.lwi travel stop <eventId>`
- `.lwi travel status`

## Database

Fresh installs use `018_lwi_traveling_world_event.sql`.

Existing installs created with the older creature/VehicleId wagon prototype must run **once**:

`019_lwi_mobile_gameobject_wagon_migration.sql`

The legacy `merchant_seat_id` column may remain in an upgraded database; it is ignored.

## First test data

`901_Traveling_Sales_Wagon.sql` uses:

- wagon GameObject: `180036` (Darkmoon Faire Wagon, unloaded)
- merchant: disabled (`0`) for phase one
- leader creature: intentionally left for the server owner to choose and visually verify

The test loop is:

`Stormwind_Gate -> Goldshire -> Sentinel_Hill_Tower -> Goldshire -> Stormwind_Gate`
