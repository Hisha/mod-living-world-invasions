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

Select a disposable creature and test a single segment from either endpoint:

```text
.lwi route test <segmentId> <fromNodeId>
```

The same segment should be tested both forward and reverse.

## Testing graph travel

Once multiple segments are connected, select a disposable creature and run:

```text
.lwi route travel <fromNodeId> <destinationNodeId>
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

## Portable route packs

The in-game builder writes directly to the world database because it is an authoring tool. Finished shared route networks should ultimately be exported into module SQL so other installations can reuse the same road infrastructure. SQL export tooling is planned; until then, keep any route data intended for distribution synchronized into repository SQL manually.
