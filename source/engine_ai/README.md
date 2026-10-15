# Reusable AI primitives

These bounded, caller-owned C modules were imported from `engine/main` and
adapted for Halo's native build. They have no ECS or simulation/timeline header
dependencies. Halo's existing campaign actor AI under `source/ai/` is unchanged.

- `behavior.*`: iterative priority/sequence/leaf trees, validation, cancellation,
  reevaluation and bounded traces (128 nodes, 16 levels).
- `intent.*`: one actor's decision-to-execution batch, with game-owned identity,
  tick and revision types; up to 32 requests with 64-byte copied payloads.
  Execution validates epoch/tick/revision and consumes the batch once. Copying a
  batch is not a safe retry mechanism. There is no global duplicate suppression.
- `behavior_intent.*`: intent emission from behavior leaves and caller-owned RNG.
- `navigation.*`: incremental directed-graph route search, at most 256 nodes and
  2,048 links. Positive link costs, capability gates and revision-scoped routes.
- `surface_navigation.*`: validated BSP-style convex polygon resources, height-aware
  projection, budgeted A* and revision-scoped portal routes, string pulling
  (collision-checked with the search's callback). Separate from the small
  graph, with no Halo dependencies.
- `surface_navigation_jobs.*`: a fixed-memory queue of surface searches from up
  to 32 clients over four shared A* workspaces; per tick at most four begins,
  64 expansions and one checked string-pull, whatever the client count.
  Requests are token-scoped; results stay in their workspace until taken.
- `traversal.*`: route following, smart-link callbacks and reservation cleanup;
  stale graph revisions require replanning.
- `fire_control.*`: automatic fire, tap cadence and bounded charged-shot holds,
  mandatory release ticks, and immediate release on lost firing intent.
- `aim.*`: a difficulty-owned, nonzero aim-error cone scaled by range, tracking
  time and movement.
- `claims.*`: fixed-capacity, independently scoped object claims with expiry,
  renewal and owner release. Halo uses one scope per team (scope 0 for FFA);
  claims coordinate teammates, not opponents, and do not reserve objects from
  humans.

No heap allocation, ECS, timeline, navmesh generation, replicated bot protocol,
objective policy or weapon selection policy is provided by these modules.
Graph arrays must remain alive and immutable during a search; changing links
requires a new graph revision. Snapshot/serialization is the caller's concern;
raw structs and pointers are not a wire format.

## Consumers

These modules define reusable algorithms and data contracts only. A game-side
feature owns perception, world queries, player lifecycle, policy and conversion
to engine actions. The library does not register itself as an extension or
select a game mode.

## Tests

From the repository root:

```sh
python3 tools/test_engine_ai_aim_claims.py --sanitize
python3 tools/test_engine_ai_utility.py --sanitize
python3 tools/test_engine_ai_navigation.py --sanitize
python3 tools/test_surface_navigation.py --sanitize
python3 tools/test_engine_ai_behavior.py --sanitize
python3 tools/test_engine_ai_traversal.py --sanitize
cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tools/test_engine_ai_fire_control.c source/engine_ai/fire_control.c \
  -o /tmp/test-bot-fire
/tmp/test-bot-fire
ninja macos
```

The behavior runner covers the available behavior/definition tests. Timeline,
ECS, standalone intent and behavior-intent runners from the original engine
are not present here; their former documentation references are not claims of
coverage in this repository.
