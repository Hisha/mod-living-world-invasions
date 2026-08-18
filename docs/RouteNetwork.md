# Shared Route Network

LWI includes a reusable world-travel graph that is intentionally independent of any one invasion. A road, trail, gate connection, or other travel corridor can be authored once and reused later by invasions, response forces, patrols, merchants, caravans, civilian events, and other Living World systems.

## Data model

The shared route network has two graph-level tables:

- `lwi_route_node` — a logical connection point such as a settlement, road junction, gate, bridge, dock, or trail intersection;
- `lwi_route_segment` — a bidirectional connection between two route nodes backed by one `lwi_movement_path`.

The physical coordinates remain in the existing movement tables:

- `lwi_movement_path` — names the physical path;
- `lwi_movement_node` — ordered coordinates followed along that path.

A segment does not belong to an invasion. Consumers choose the direction at runtime. If a segment is defined as `A -> B`, the same movement-node data can be traversed `A -> B` or `B -> A`.

## Graph authoring rule

Prefer route nodes at places where travel decisions can change:

- road intersections and forks;
- settlements and camps;
- gates and bridges;
- docks or future transport transition points;
- other meaningful connection points.

Do not create one giant town-to-town segment when the road passes a junction that another route may need. Junction-to-junction segments maximize reuse and allow graph routing to select the correct branch automatically.

## Recommended automatic builder

The preferred authoring workflow is the automatic path builder. It records the GM's actual traveled path every **5 yards**. The deliberately dense spacing avoids long point-to-point chords that can cut outside road corridors, fences, curves, bridges, or other world geometry.

Stand at the beginning of the desired segment and run:

```text
.lwi route path build <StartName> <EndName>
```

Example:

```text
.lwi route path build Stormwind_Gate Goldshire
```

Then walk or ride the exact route you want creatures to use. Coordinates are written to the world database continuously while you travel. At the endpoint run:

```text
.lwi route path complete
```

The builder:

1. reuses an existing named start/end route node when one exists;
2. creates missing logical route nodes;
3. allocates the movement-path and route-segment IDs;
4. records the exact starting position;
5. writes movement nodes every 5 yards of traveled distance;
6. records/snaps the exact final endpoint;
7. creates the reusable `lwi_route_segment` automatically.

The builder currently records a continuous segment on one AzerothCore map. Crossing a **zone** boundary is fine as long as the map ID does not change. True map transitions such as Eastern Kingdoms to Kalimdor will require a future transport/transition edge rather than one continuous movement segment.

### Builder commands

```text
.lwi route path build <StartName> <EndName>
.lwi route path status
.lwi route path pause
.lwi route path resume
.lwi route path complete
.lwi route path cancel
.lwi route path cancel confirm
```

`cancel confirm` deletes the unfinished movement path/nodes and removes a start route node if that node was created specifically by the canceled build.

## Inspecting a recorded path

With `LWI.Debug = 1`:

```text
.lwi route path show <pathId>
.lwi route path nearest <pathId>
.lwi route path hide
```

`show` places temporary visual waypoint markers at the loaded movement nodes. `nearest` reports the closest authored movement node to the GM.

## Testing a segment

After authoring or editing route data:

```text
.lwi stop
.lwi reload
```

Select a disposable creature and test a single segment from either endpoint. Segment and node arguments may be numeric IDs or exact names:

```text
.lwi route test <segmentId|name> <fromNodeId|name>
```

Examples:

```text
.lwi route test 1060 10
.lwi route test Stormwind_Gate_Goldshire Stormwind_Gate
```

The same segment should be tested both forward and reverse.

## Testing graph travel

Once multiple segments are connected, select a disposable creature and run either numeric IDs or exact route-node names:

```text
.lwi route travel <fromNodeId|name> <destinationNodeId|name>
```

Examples:

```text
.lwi route travel 10 70
.lwi route travel Stormwind_Gate Sentinel_Hill_Tower
```

LWI resolves the shortest connected route by segment count and automatically chains the necessary route segments. Branches that do not lead to the requested destination are ignored.

## Manual authoring tools

The older/manual tools remain available for special cases:

```text
.lwi route record start <pathId> <pathName>
.lwi route record add
.lwi route record undo
.lwi route record status
.lwi route record finish
.lwi route record cancel
.lwi route record cancel confirm

.lwi route node add <nodeId> <nodeName>
.lwi route segment add <segmentId> <segmentName> <startNodeId> <endNodeId> <pathId>
```

For normal roads, prefer the automatic 5-yard builder so waypoint density is deterministic instead of manually estimated.

## Resetting development route data

During development only, the guarded command below removes the shared route graph and the movement paths referenced by its segments while preserving movement paths that are not owned by `lwi_route_segment`:

```text
.lwi route network reset
.lwi route network reset confirm
```

Run `.lwi reload` afterward.

## Exporting portable route SQL

The in-game builder writes directly to the world database while authoring. Finished routes can be exported into self-contained SQL suitable for shared route packs or invasion-specific prebuilt SQL. Export commands require `LWI.Debug = 1`.

Export one segment by numeric ID or exact segment name:

```text
.lwi route export segment <segmentId|name>
```

Export the connected graph journey between two route nodes, again using IDs or names:

```text
.lwi route export journey <fromNodeId|name> <destinationNodeId|name>
```

Export the complete currently loaded route network as the canonical prebuilt route file:

```text
.lwi route export network
```

This writes `lwi_exports/801_routes.sql`. The intended publish workflow is to copy that generated file to `data/sql/db-world/prebuilt/801_routes.sql`. Once other prebuilt invasion SQL references route-node IDs, those published route-node IDs should be treated as stable data contracts. Add new IDs freely, but do not renumber already-published route nodes without also updating every consumer.

Examples:

```text
.lwi route export segment Stormwind_Gate_Goldshire
.lwi route export journey Stormwind_Gate Sentinel_Hill_Tower
.lwi route export network
```

Exports are written to an `lwi_exports` directory beneath the worldserver working directory. The command prints the absolute output filename in-game. Exported SQL contains the required `lwi_route_node`, `lwi_movement_path`, `lwi_movement_node`, movement-node action, and `lwi_route_segment` data in dependency-safe order. Journey exports de-duplicate route nodes and movement paths used by multiple segments.

The canonical `801_routes.sql` should own the route network itself, including invasion-specific route segments whose endpoints are referenced by prebuilt invasion data. Invasion SQL should reference stable route-node IDs rather than duplicate movement-node data. Segment/journey export remains useful for review, debugging, or extracting a smaller subset, while `export network` is the normal publish path for the module's complete route dataset.
