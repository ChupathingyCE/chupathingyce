# BSP surface navigation: first working slice

> **Port status:** this document includes historical local-map smoke results
> from the legacy working tree. They were not repeated for the ChupathingyCE
> port; this branch was checked with macOS builds and asset-free tests only. No
> live match or network session was run. Bots remain offline-only.

This is a **host-local, walking-only geometry navigation system**, independent
of combat/tactical selection. It does not replace campaign actors or modify map
tags, original map files, player spawning, saved game layouts, or network messages.
Normal multiplayer bots use its collision-checked BSP routes for lane patrol,
scavenging and pursuit; their spawn-location graph is only a delayed fallback if
the BSP resource is unavailable for ten seconds. The opt-in Bot 1 probe remains
for isolated geometry acceptance tests. Forge nav authoring is not implemented.

## Modules and contracts

- `source/engine_ai/surface_navigation.*`: dependency-light convex polygon resource,
  geometry validation, height-aware projection (also looking down,
  `sn_project_below`), static components, budgeted deterministic A* and
  string-pulled routes. No Halo, ECS, timeline, renderer, heap or network types.
- `source/engine_ai/surface_navigation_jobs.*`: the bots' shared search queue:
  four A* workspaces for up to 32 token-scoped requests, with fixed per-tick work.
- `source/features/bots/bot_navigation.*`: each bot's route, lane goal, following
  and blockage recovery, on the shared queue.
- `source/features/bots/navigation_world.*`: validated CE collision edge-ring adapter,
  actual biped tag dimensions/slope, double-buffer publication, live CE capsule
  checks, temporary region blocking and incremental diagnostic dumps.
- `source/features/bots/navigation_probe.*`: one consumer that steers a living Bot 1 through
  the ordinary `player_action` queue. It never sets positions, grants inventory,
  jumps, flies, or bypasses collision/physics. The normal solo-host Slayer gates
  still apply.

Resources are keyed by map-name identity, checksum, BSP index, agent dimensions,
format version and Forge object-edit revision. Queries/routes carry the key,
resource generation and temporary-obstacle generation; mismatches return stale.
Normal bots pass a fixed obstacle generation: a temporary blockage made by one bot
does not discard every other bot's route. Each bot instead checks its next six
route polygons against the blockage list and replans only when one is blocked.
The probe still uses the global obstacle generation.
BSP switching invalidates before unloading (also on failed-switch fallback), map
teardown invalidates before disposal, and game-state restoration invalidates
before loading. Resources contain copied geometry, never borrowed BSP pointers.

A failed staging build is not published. Previously published storage is retained
until replaced, but is **not usable** if its source key has changed. Forge object
creation/move/delete/undo/redo increments the derived-data revision. This does not
cook new walkable surfaces for placed objects: they are checked as live collision
obstacles. No authored seed points, jump hints or tactical markers are claimed.

### Geometry and movement

Use retail CE walkable/nonbreakable surface bits, validated convex collision
rings and reciprocal shared edges. Slopes use the spawned biped definition's
minimum normal K. Boundary projection and sufficiently wide portals account for
biped radius; stacked floors are projected separately by height. Only continuous
shared-edge walking is linked: no inferred step, jump, drop, climb or teleport
links. Radius-inset projection is intentionally conservative. Polygons with unusable
centers are not endpoints, but a single narrow BSP seam can be crossed between
two usable neighbors when the portal-to-portal segment clears every exterior
edge by the agent radius. Rat Race's apparent stair disconnections were these
seams: 24 floor components become 20, with the main floor growing from 122 to
183 usable polygons. Chains of narrow polygons are not yet bridged.

The quantization tables in legacy `path_structure_bsp.c` are unused there; their
low-bit clearance interpretation was **not proven** for these multiplayer maps.
Using them as physical widths rejected every Blood Gulch polygon. The adapter
therefore reports clearance as unknown (`width=height=0`), and the core requires a
collision callback before it can return a route. Native callbacks use CE's pill:
lower sphere center above ground, center-to-center height = standing height minus
twice radius, with slope-aware ground offset. This corrected a second integration
error where ground-level sphere centers made all routes appear obstructed.

The build also records static topology: each polygon whose center the agent can
stand on gets the lowest polygon index of its connected set (union-find over
reciprocal adjacency, bounded per tick), `sn_component()`. A query whose projected
endpoints lie in different components returns **disconnected** without searching
or calling collision; **no-path** therefore always means collision checks refused
every route within one component. Lane and random patrol goals are chosen only
among polygons in the bot's own component; a rejected preferred polygon is
logged with its reason (`center-unusable` or `disconnected`).

**Search.** A* over polygons minimizes the length of the start -> portal ->
center ... portal -> goal polyline, with the straight line to the goal as the
heuristic and ties broken by polygon index. (Breadth-first search, which
minimized polygon count, sent bots on 70-waypoint detours across Blood Gulch's
uneven terrain triangles.) Inside the start polygon a route leaves from the start
point straight to the portal, and inside the goal polygon it goes straight to the
goal point (both convex), so neither polygon's center can refuse the search. A
caller whose goal is a unit's position may clear `check_goal_segment` to skip the
segment inside the goal polygon; bots do so for pursuit and pickup goals.

Bot searches check segments against the **structure and scenery only**
(`navigation_world_search_clear`): never bipeds, vehicles or machines, which
move. The two canonical halves of every polygon edge (center->portal,
portal->center) are swept once per resource and the verdict cached (128 KiB);
segments to or from start and goal points are swept each time. Temporarily
blocked polygons are refused before the cache, except that a bot may always
leave the polygon it stands in. In 24-bot Blood Gulch runs this is about
100 sweeps per 10 s against ~10,000 cached verdicts. The probe keeps the live
`navigation_world_segment_clear` for both search and following.

**Routes.** The polygon corridor is kept (`corridor[]`, each point's
`corridor_index[]`), and `sn_route_straighten` replaces the center/portal
points by the shortest path through the corridor (Mononen's simple stupid
funnel), turning only at portal ends held the agent radius in. Blood Gulch
patrol routes average ~10 points instead of ~73.

The funnel knows only the polygons, not the scenery standing on them (rocks,
trees, crates), so a pulled segment could cut through an object the searched
center/portal path went around. Bots therefore use
`sn_route_straighten_checked` with the search's own callback: each new segment
is checked one corridor polygon at a time, on that polygon's floor (one chord
across a hill would hit the ground), and a piece across a seam must also keep
the radius from the seam's open edges, as the search's crossing does. A
refused stretch is pulled again through the original polygon center nearest
the refusal; a stretch with no center left keeps its searched points. Every
segment of the result is therefore either checked or the search's own, and
the route stays smooth away from the obstacle. At most 256 checks per route
(`SN_STRAIGHTEN_CHECKS_MAX`); once they are spent the rest of the route stays
as searched. A single straight run across more than 256 polygons therefore
stays unsmoothed.

**Scheduling.** Bot searches no longer each own an A* query. They queue, one
per bot, for four shared workspaces (`source/engine_ai/surface_navigation_jobs.*`),
served in order. Per tick, however many bots there are, at most four queries
begin (each placing its two endpoints, `sn_project_near`, 3 m down / 1 m
across), 64 polygons are expanded (16 per workspace, as each bot's own search
did before) and one found route is string-pulled with its checks. A queued
search takes the bot's new position and a moving target's new position in
place, keeping its turn; a found route keeps its workspace until the bot takes
it. Requests a bot did not ask after for a tick (its behavior changed, it left
or respawned) and those whose body died are cancelled before each step.
Queueing adds latency under load: when every bot asks at once (a match start),
the last ones wait for several searches ahead of them (24 bots on Blood Gulch:
about 9.5 s for the last, 3.6 s with 32 expansions per workspace; see
[bot-testing.md](bot-testing.md)).

**Following.** A waypoint is reached within 0.45 m horizontally and 0.35 m
vertically, or passed: within 2 m and already closer to the next waypoint than
the waypoint itself is. The live check before each move sweeps **vehicles and
machines only** (`navigation_world_occupant_clear`): a straight full-pill sweep
from the bot to its waypoint across uneven ground read hills as obstacles, and
other bipeds are avoided by the bot's own per-tick capsule steering. A tactical
route's last segment is not checked (the target stands there). A bot is stuck
when it moves less than 0.5 m in each of two seconds, or misses a waypoint's
deadline (2 s plus 1 s per metre); it then blocks the next corridor polygon
after the one it stands in and replans. Blocks last 2 s; a polygon blocked again
within 30 s of its last block is blocked twice as long, up to 32 s, so a parked
vehicle or an unclimbable rise is learned. Each bot checks its next six corridor
polygons against the blocks; one bot's block never invalidates another's route.
While a bot searches or follows a route, the bots' stall escape no longer runs
it in an arbitrary direction on ticks it emits no move; a real stall still does.
This is not crowd avoidance, door interaction or advanced dynamic-obstacle
navigation.

**Short ascents.** While following an existing corridor, a grounded, unstunned
biped may request one short jump per waypoint (rise 0.3–1.6 m, horizontal distance
at most 2.5 m). `sn_jump_candidate` uses its current horizontal momentum, actual
jump impulse and per-tick gravity to find a descending landing within 0.25 m,
with at most 60 ticks. `navigation_world_jump_clear` requires a projected landing
and sweeps the real capsule along every predicted tick before emitting a normal
jump intent. Grounded landings, missed landings and requests that never become
airborne are logged; steering/acceleration can still change the actual trajectory.
These are local ascent attempts, **not graph links across disconnected floors**.
Arbitrary gap jumps, drops, chained stair traversal and platform-to-platform
links remain unimplemented.

Bot endpoints first project below, then may move horizontally up to 1 m toward a
usable polygon center (`sn_project_near`) when a target is against a wall or on a
seam. This is a candidate, not a guarantee that the endpoint is reachable through
an intervening wall; collision checks still decide whether it can be approached.

**Anchors.** `source/features/bots/map_anchors.*` reads the scenario's starting
locations, netgame flags (CTF flags and vehicles, oddball spawns, race, hills,
teleporter sources and targets) and netgame equipment (weapon and powerup
spawns with item collection, respawn time and game types), each element count
checked, places each on the walkable polygons (`sn_project_below`, 3 m), logs
them (`anchors: ...`) and writes `debug.map_anchors_dump` (`.json`,
data-root-relative, published by rename). The lane axis runs between the two
teams' CTF flags when the map has them (Blood Gulch: 97.6 m; the average team
starting locations gave 22.6 m), else between the starting-location averages.
Blood Gulch has 297 anchors, 286 on walkable polygons; all but one (the blue
base's teleporter source, an isolated polygon) are in one component.

### Explicit ceilings and costs

- 131,072 input surfaces; 16,384 accepted polygons; 8 vertices/edges per polygon.
- 512 polygons per output corridor, 1,025 output points. Longer results explicitly
  return capacity rather than an incomplete route.
- Build: at most 128 source/adjacency rows per game tick; no background thread.
- Probe search: at most 2 polygon expansions per tick, each with at most 8 edges.
- Normal bot searches, all bots together: at most 4 query begins, 64 polygon
  expansions (16 per workspace), each with at most 8 edges (or 8 × 8 candidate
  exits through seams), and one checked string-pull (at most 256 capsule sweeps)
  per tick. Canonical edge verdicts are mostly cached; seam crossings and
  string-pulled pieces are swept. A query gets at most 1,024 slices. Moving
  goals of a search under way, or of a route, replan after 12 metres.
- Dump: at most 64 polygons per tick, published by renaming a `.partial` file.
- Projection/query initialization is a bounded **linear** polygon scan, not tiled
  or spatially indexed. A* is O((P+E) log P) with an indexed binary heap.
- Resource 2,818,112 bytes (64 KiB of it static components); two resources plus
  524,304-byte builder, 128 KiB edge cache and the probe's 295,016-byte
  query/20,560-byte route total about 6.6 MB static reserved storage. The bots'
  shared scheduler is 1,267,864 bytes (four queries and four found routes),
  whatever the bot count, and each bot owns a 20,560-byte route: about 1.9 MB
  for 31 bots, where one query per bot took 9.8 MB. String-pulling scratch is
  about 33 KB. When disabled, geometry buffers are not built/touched. No
  per-query allocation.

This is suitable for validating the architecture, **not a claim of production
completion** or measured worst-case frame time on every CE map.

## Private playable probe

Build with `ninja macos`. Add these to the private solo-host Slayer command in
`docs/bots.md` (use isolated data/save roots and keep public networking disabled):

```sh
HALO_BOTS=1
HALO_NAV_PROBE=1
HALO_NAV_DUMP=bloodgulch.nav
HALO_NETWORK_TEST=host:bloodgulch:slayer
```

`debug.nav_probe` defaults false. `debug.nav_dump` defaults empty; filenames must
end in `.nav`, to avoid targeting map/Forge-overlay files. Paths go through the
port's game-file translation: use a **data-root-relative** filename, not a Unix
`/tmp` path. Dumping alone also works with the human's live biped (including
campaign), without overriding its controls. Allow time for both build and dump.
Dumps are diagnostic derived geometry, not an authored resource loader/cache.
Do not commit dumps derived from proprietary map assets.

The default probe chooses a nearby polygon center and tries at most eight goals.
Set `HALO_NAV_GOAL=x,y,z` / `debug.nav_goal` to request an explicit world-space goal;
its failures are reported without silently choosing a substitute. For example:

```sh
HALO_NAV_GOAL=99999,99999,99999  # explicit bad-goal test, no movement/teleport
```

Logs distinguish resource publication, query failures, route success, sampled
following, stuck replanning and goal arrival. Diagnostic `.nav` files include
version/agent/key headers, polygon/source IDs, normals, vertices and adjacency.
There is no in-game mesh renderer in this slice.

## Verification

```sh
python3 tools/test_surface_navigation.py --sanitize
python3 tools/test_engine_ai_navigation.py --sanitize
python3 tools/test_engine_ai_behavior.py --sanitize
python3 tools/test_engine_ai_traversal.py --sanitize
python3 tools/check_navigation_probe_log.py walk /path/to/probe.log
python3 tools/check_navigation_probe_log.py bad-goal /path/to/failure.log
```

Core tests compile with strict warnings as errors and ASan/UBSan. They cover flat
floors and ramps, stacked floors without cross-floor links, disconnected islands,
bad endpoints, narrow boundary clearance, insufficient height, unknown clearance
requiring a callback, dynamic blocked/unblocked callbacks, BSP/overlay/agent/
checksum/obstacle/resource invalidation, malformed vertices/references/counts,
source/polygon/corridor capacity, invalid budgets, failed staging isolation,
repeat-build determinism, static components (budget-independent), endpoint
segments that bypass centers, clearance diagnostics, A* choosing the shorter of
two corridors where breadth-first would not, string pulling (an L turning once
at the radius-inset corner; straight corridors of 4 and 512 polygons becoming
one segment) and looking down onto the nearest floor below. Checked string
pulling: a rock on the pulled line (kept searched points; pinned through the
nearest center), pieces on their own ramp floors, a seam's open edge, a
callback refusing everything, the 256-check budget, invalid/stale routes, and
591 random routes among random rocks on a 6x6 grid (never crossing a rock,
never more points than searched). The scheduler test (a second program,
same runner) covers its per-tick bounds, queue order, refresh before begin,
results held until taken, one string-pull per tick (oldest first), cancel,
capacity, stale resources and obstacle revisions, endpoint placement and
failure diagnostics. They do not emulate every CE collision/asset-loader
failure or validate real campaign BSP transitions.

Native smoke evidence (legal local retail assets; `/tmp/nav-test`):

- Blood Gulch: 4,916 input surfaces, **2,807** walkable polygons; ~2.5 ms total
  build CPU distributed across ticks. 9-waypoint probe arrived with error **0.147**.
- Beaver Creek: 2,476 input surfaces, **639** polygons; ~0.8 ms build CPU.
  17-waypoint probe climbed from z=-1.356 to z=-0.217 and arrived with error **0.173**.
- Blood Gulch explicit out-of-bounds goal: **bad-goal**, with no fallback route.
- Both walking and bad-goal log checkers passed; no crash/assertion recorded.
- Native `ninja macos` and the four sanitized standalone runners passed.

Original-source clang probes can misinterpret CE layouts on LP64. Native generated
translation units and the Ninja build are authoritative for those layout checks;
do not remove assertions or alter tag layouts to make a standalone header probe
happy. Log checkers confirm sampled acceptance evidence, not absence of collision
errors on every frame or complete tactical correctness.

## Next increments (in order)

1. Extend the single-seam crossing and local short-ascent attempts into explicit
   directed graph step/jump/drop links with CE-grounded capability checks and
   landing verification; do not assume clamber. The Rat Race floor seams are
   now crossed, but isolated platforms and chains of narrow polygons remain.
2. Teleporter links from the anchors: source and target flags pair by team
   index (Blood Gulch: red base roof -> (82.8, -114.5), blue -> (43.8, -126.2));
   confirm the engine's pairing before linking.
3. A semantic layer over the anchors: named zones and routes, per-polygon
   danger/exposure/cover costs measured from match telemetry (deaths, kills,
   sightings), role-dependent weights.
4. Add Forge seed/jump/tactical authoring, selection/undo/visualization and a
   separate versioned checksum-bound overlay. Keep Explore Neighbors explicit
   and Hide markers neighbor-free; validation must reject unusable authored links.
5. Evaluate spatial/tiled recooking and background scheduling for placed static
   geometry. Preserve stable publication/revision contracts and explicit capacity
   failures rather than enlarging the old small region graph.
