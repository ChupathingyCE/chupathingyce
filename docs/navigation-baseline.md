# Historical geometry-navigation baseline

> This is a research note from the legacy `main` worktree, not a current status
> report or an accepted ADR. Its local map observations and smoke-test notes
> have not been repeated while porting bots to the ChupathingyCE baseline. The
> current branch was build- and asset-free-test checked only; no live match or
> network session was run.

## Local inspection / baseline at the time

Work started on `main` at `656761d4` with uncommitted weapon/vehicle bot changes
in `source/features/bots/bots.c`, `source/units/vehicles.*`, `port/linux/src/port_config.c`,
`docs/bots.md`, and a new opportunity-log checker. These are preserved; no
remote operation is part of this task.

- `engine/ai/*` are not present. Imported graph/search/traversal/intent/behavior
  mechanisms are in `source/engine_ai/`. No ECS or timeline modules are present.
- The requested ADR 0113 and `docs/adr/` are absent locally; this note does not
  manufacture an accepted ADR or rely on missing engine types.
- Forge is a single local desktop module, `port/linux/game/forge.c`, with scene
  placements, transforms, undo/redo, a render overlay and checksum-bound JSON
  overlays. See `FORGE_IMPLEMENTATION_NOTES.md`. No nav-authoring tools exist.
- Build instructions: `README.md`, `port/macos/README.md`, `port/linux/README.md`.
  macOS builds rewrite long/layouts under `build/macos/lp64`; original source LSP
  probes can report false layout mismatches against transformed headers.
- Baseline: `ninja macos` and sanitized navigation, behavior-definition and
  traversal runners pass. Legally supplied Xbox maps, including Blood Gulch,
  exist outside the repository in the user's Application Support directory.

## CE source findings (primary local sources)

`source/structures/structure_bsp_definitions.h` stores a collision BSP plus
`pathfinding_surfaces` and `pathfinding_edges` tag blocks. The former is one byte
per collision surface in retail maps. `source/ai/path.c:path_get_edges` follows
collision surface edge rings to derive neighboring surfaces. Edge records have
two vertex indices, two next-edge indices and two surface indices. Retail rings
have 3–8 edges; the collision definitions specify an eight-vertex maximum.

`source/ai/path_structure_bsp.c` and `path_smoothing.c` identify bit 6 as walkable
and bit 7 as breakable. Unused quantization tables list widths
(0.2, 0.4, 0.6, 0.8, 1, 1.5, 2, 4) and heights
(0.25, 0.5, 0.75, 1, 1.5, 2, 4, 8). Their low-bit clearance interpretation is
not established: runtime testing rejected every Blood Gulch polygon when these
were treated as physical widths. The adapter now reports unknown clearance and
requires live CE capsule checks (see `navigation.md`). The legacy smoothing routines
also contain deliberately preserved NONE-before-array reads and assertions.
New code must validate rings/references first and must not inherit those bugs.
`pathfinding_edges` are not a replacement for the validated collision adjacency;
no interpretation of an undocumented edge byte is assumed by the new slice.

`biped_definitions.h` supplies collision radius, standing/crouching height,
maximum slope, jump velocity and derived minimum normal K. The adapter reads the
actual spawned biped tag rather than using Infinite movement dimensions. The
first slice is continuous walking only: no inferred steps, clamber or jump/drop
links. Jump feasibility requires a later CE-specific physics validation slice.

`scenario_switch_structure_bsp` unloads old tag storage, may fall back to reloading
it, then reconnects objects. Derived data must be invalidated before unloading.
`game_state_call_before_load_procs` covers checkpoint, persistent and core loads.
Navigation resources are derived host state, not serialized into Halo game state.

## External inspiration and library decision

Halo's official Forge requirements describe seed points for missing mesh areas,
one-/two-way jump hints, Explore markers with named Neighbors and Hide markers
without Neighbors. Source:
https://support.halowaypoint.com/hc/en-us/articles/14796740242708-Community-Forge-Map-Requirements
(search-indexed primary-source passages were available; direct fetch returned 403).
These are authoring semantics, not disclosure of Infinite's internal algorithm.

Recast/Detour is an established C++ navigation implementation with zlib licensing:
https://github.com/recastnavigation/recastnavigation
https://github.com/recastnavigation/recastnavigation/blob/main/License.txt
Its build guide supports Windows/macOS/Linux and custom allocation hooks:
https://github.com/recastnavigation/recastnavigation/blob/main/Docs/_2_BuildingAndIntegrating.md
It would add C++ compilation/linking and allocation/build-memory management to a
legacy C/gnu89, multi-platform, LP64-rewritten build. Exact generation memory
needs depend on voxel resolution and map extents; no fabricated budget is claimed.
**Do not add it in this first slice:** reuse CE's cooked walkable collision polygons
and adjacency, behind an independent navigation resource interface. Recast remains
an option for recooking static Forge geometry once the CE adapter is proven.

## Ordered scope

Phase 1 is the baseline above. Phase 2 implements a validated, bounded polygon
resource, explicit point projection and budgeted route queries, plus diagnostic
output. Phase 3 uses normal host player actions for an opt-in route-following
probe. No original map/tag schema, campaign actor AI, physics or network format
is replaced. Later phases (seed/jump/tactical authoring, persistence, visualization,
tiled/background recooking) must not be represented by nonfunctional stubs.
