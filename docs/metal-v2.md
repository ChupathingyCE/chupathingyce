# RFC: macOS Metal Renderer V2 — Low-Overhead Draw Submission for chupathingyCE

**Status:** Experimental implementation in progress — draw-path performance remains the primary goal; promotion gates are not yet met

**Scope:** `LopezNoah/chupathingyce`, confirmed branch `feature/macos-metal`  
**Proposed repository location:** `docs/specs/macos-metal-renderer-v2.md`  
**Date:** 2026-10-15  
**Decision authority:** Project maintainers  
**Compatibility baseline:** Existing opt-in macOS Metal backend on `feature/macos-metal`  

## 0. Decision summary

Build a **Mac-only, low-overhead draw-submission path** with one measurable purpose: reduce CPU time spent on repeated `D3DDevice_DrawVertices` / `D3DDevice_DrawIndexedVertices` calls. Each draw should perform only work that actually changed since the previous compatible draw, followed by the required Metal draw command. Keep the Xbox-facing ABI, guest memory semantics, NV2A shaders, shader warming, pipeline archives, post-processing and proven GPU lifetime protections.

The first deliverable is a **fast path within the current Metal architecture**, built behind an opt-in V2 selection and tested against V1. Eliminating `gpu_draw` packets, refactoring the C/Objective-C boundary and retiring the generic dispatch are *possible consequences* if profiling shows that they still cost meaningful time after state preparation and Metal binding work are optimized. Deleting interface layers is not a performance metric.

This is **a targeted draw-path optimization project**, not permission to rewrite the entire frontend and backend. Profile the actual per-draw work first, implement changes in separately benchmarkable slices, and preserve a slow/correct reference path. The goal is to remove repeated frontend interpretation, redundant Metal-object lookup, unnecessary CPU-side packet construction and avoidable encoder bindings. The C wrapper/function pointer alone is unlikely to dominate.

There shall be three mutually exclusive macOS build variants during development: OpenGL (existing default), Metal V1 (existing `--macos-metal` build), and Metal V2 (new opt-in selector). Linux, Windows, Android, and other platform build inputs shall remain unchanged.

### Core decisions

1. **Optimize the common draw first.** Aim for an allocation-free, small-number-of-branches path when the shader, raster/depth/blend configuration, texture bindings, target and vertex declaration stay compatible. Refresh changing uniforms and vertex/index spans without rebuilding static work.
2. **Keep the Xbox API; replace D3D8 internals only as useful.** The game still calls the required `D3DDevice_*`, `D3DResource_*` and supporting symbols. Some XDK inlines write state arrays directly, so state observation at draw time is mandatory.
3. **Keep a semantic layer.** Direct-to-Metal does not mean passing raw Xbox enums into Metal. Use compact, typed Xbox state snapshots and prepared draw descriptors in Mac V2; eliminate the old *cross-API backend* packet/dispatch only if the resulting code measurably reduces CPU submission time.
4. **Do not build a new MSL compiler in V2.** `nv2a_vsh.c`, `nv2a_psh.c`, and `nv2a_msl.c` already generate Metal shaders. Begin with their proven outputs and persistent cache behavior.
5. **Default to draw-order preservation.** Blending, depth/stencil, occlusion, destination reads, render-target aliasing, and HUD effects make global material sorting or automatic batching incorrect without proofs.
6. **Guest memory remains authoritative.** Vertex/index/texture resources may be written through raw pointers without a new setter call. A cached Metal resource may only be reused if its backing content is provably unchanged.
7. **Measure before optional accelerators.** Argument buffers, indirect command buffers, GPU-driven work generation, and an advanced render graph are experiments, not baseline scope.

## Implementation checkpoint — prepared-state and attribute reuse

The opt-in `--macos-metal-v2` path now adds the following to the existing
profiling, selector, memory-watched mirror and backend object caches:

- **Independent frontend depth, blend and raster caches:** compact comparisons
  of live Xbox globals avoid repeating enum conversion when a domain is
  unchanged. Depth-target presence and clip-y convention are dependencies.
  Viewport/scissor, HUD-layer blend rewriting/accounting and body depth clamp
  stay live; draw-local edits never modify the cached base state.
- **Prepared shader inputs:** compare the live combiner words (excluding uniform
  constants) and compact alpha/fog/texture-stage/query inputs before rebuilding
  the base key. Resolve the final pixel shader again only when its complete key
  changes, including live sampler types, coverage and lighting. Failed shader
  resolution still retries. Vertex variants retain their existing object cache.
- **Prepared native objects:** independently retain the last compatible pipeline
  and depth-state resolution (borrowed from device-owned dictionaries). Exact
  canonical key equality bypasses shader-table and front-cache lookup; live
  attachment formats/sample counts and attribute kinds still determine pipeline
  compatibility. Dynamic stencil reference/blend color remain live. Device/cache
  initialization and shader warming invalidate prepared references. Current
  shader handles are not deleted/reused by this backend; any future shader
  replacement API must invalidate these and the existing pipeline dictionaries.
- **Reusable declaration plans:** each declaration retains formats, stream/register
  mapping and constant slots. Stream presence and BGRA capability participate in
  reuse. Per-draw span checks, mirror refresh, streaming and constant-attribute
  values remain authoritative; immediate draws retain their existing path.
- **Encoder-local attribute-table reuse:** `metal_attribute_table.h` compares the
  complete packet inputs plus freshly resolved live-buffer presence before
  entering the 16-slot update loop on layout transitions. Constant changes and
  stream-range changes are handled independently; changing offsets only update
  the precompiled list of streamed slots, not unchanged constant/format entries.
  Bind the table only when its effective contents changed or encoder-local
  knowledge was invalidated. Buffer renaming still binds the current Metal object;
  `use_buffer` runs even when the table/buffer binding is reused. Pass resets,
  clear quads and disabled state caching force the appropriate rebind.
- **Safe stage work reduction:** sampler interpretation is reused only when the
  actual sampler words, mipmapped status and hires status match. V2 now uses
  `xgpu_texture_stage_get` for non-target stages: four bounded entry-identity
  hints avoid bucket hashing/global recent-table lookup for unchanged non-P8
  metadata. The cache checks Xbox Data/Format/Size and the eviction epoch before
  dereferencing a remembered entry. A changed memory-watch serial permits entry
  reuse only; it still goes through generation validation and any required
  upload. Palettized textures retain the full palette-content path on every
  lookup. The resolved texture handle/description is **not** cached: text,
  menu and HUD overrides are still resolved live on every hit. Target lookup,
  mip-composite validation, uploads/renaming and backend resource use tracking
  remain unchanged. Broader stage-resolution bypass remains unfinished until
  all content/lifetime dependencies have tested version predicates.

`debug.gpu_stats` reports per-domain `draw cache:` hits/misses alongside existing
CPU stage timing and actual attribute-table binds. Backend lookup counts and
native pipeline/depth hit/miss counts are reported separately. No new persistent
shader key schema or shader translator is introduced. A separate
`.github/workflows/metal-v2.yml` builds a universal V2 debug artifact, runs CPU
policy/build tests and compiles both real-backend probes without requiring game
data or a Metal device. Its GitHub-hosted execution is not yet confirmed.

Validation at this checkpoint: `ninja macos_v2` links and bundles the experimental
app; the touched renderer translation units also compile with the V2 define removed.
`python3 -m unittest tools.test_metal_prepared_state` runs the real small C cache
functions under ASan/UBSan, comparing cached conversions/base keys with the
uncached path through direct write/draw/restore transitions, declaration changes,
constant/stream transitions, buffer renaming and encoder invalidation. The
`test_metal*.py` suite passes (68 tests, one skipped); build-selector tests pass
separately. `tools.test_metal_texture_stage_cache` additionally exercises the
actual texture lookup/eviction code in V1 and V2 under ASan/UBSan: direct and
partial texel writes, 16 KiB-page false positives, unrelated watched writes,
Data/Format/Size changes, recent-table collisions, palette mutation/recycling,
live overrides, invalid spans, diagnostic no-cache mode, idle/budget eviction
and same-address recreation. Mock operation counts verify unchanged V2 stage
hits skip bucket hashing without skipping override resolution. These are CPU
policy/build checks, not game image parity or end-to-end performance results.

The actual-backend offscreen fixture now provides a separate, narrow runtime
checkpoint: identical V1/V2 image digests and ordered draw counts, native lookup
reductions, state mutation, storage rename before submission and encoder-rebind
checks pass, including a separate Metal API Validation run on Apple M2 Pro.
Instrumentation-disabled backend medians improve in three workloads, but some
individual V2 runs regress. The ablation control also exposes remaining cache
bookkeeping cost. See [the draw experiment](metal-v2-draw-experiment.md) for all
round medians, operation counts, provenance, reproduction and limitations. This
is **not** proof of a game-wide speedup or completion of the RFC performance gates.

**Still required for these state-reuse changes:** controlled V1/V2 profiles and
instrumentation-disabled comparisons on two draw-heavy game scenes; broader
Metal API Validation, screenshot/motion/AA/query/target-alias regressions;
additional safe texture-resolution reuse only if measured worthwhile; and the
remaining acceptance matrix. Generic `gpu_draw` packet construction/dispatch and
per-draw autorelease pools are deliberately unchanged. No profiling evidence yet
justifies bypassing the generic packet boundary or promoting V2.

### Phase 2 follow-up — TigerStyle contracts

Phase 2 is **in progress**, not a new renderer rewrite or a completed promotion
gate. The prepared-state implementation already covers much of its engineering
scope; the two-scene game profiling and broader runtime checks above remain open.

The next incremental slice separates live stencil reference and blend color from
frontend cached depth/blend bases. Changing either value no longer repeats base
conversion; every draw still reads and supplies the current value. Base snapshots
remain private, fixed-capacity values copied to the caller before HUD or other
per-draw rewriting. No allocation or additional dictionary lookup is introduced.

TigerStyle is applied incrementally here, rather than claimed for the whole
renderer:

- Validate sampler stage bounds and output pointers before indexing cache storage.
- Check sampler/depth/blend/raster snapshot sizes at compile time so adding a
  dependency cannot silently overrun a fixed cache key.
- Keep exact live-value comparisons, independent cache domains and V1 conversion
  fallback; setter dirty bits and hashes alone are not authoritative.
- Keep dynamic values outside reusable bases, with explicit zeroed base fields.
- Test direct write/restore transitions and assert both live output values and
  unchanged base-cache miss counts under ASan/UBSan.

These contracts do not bound all existing shader dictionaries or remove cold-path
allocations. Those require separate ownership/limit audits; this slice makes no
new claim about game-wide speedup or Phase 2 completion.

### Phase 3 follow-up — effective attribute-table inputs

Phase 3 is also **in progress**: declaration plans and encoder-local table reuse
already exist. The next slice removes redundant full stream/constant snapshots
from the backend table cache. A fixed-capacity plan partitions all 16 slots into
streamed and constant lists; compatible draws compare only effective values
against the existing table. Live buffer presence, renaming, resource-use tracking
and encoder-local rebind rules remain authoritative. No draw order or shader ABI
changes, allocations, new native resources or autorelease-lifetime changes are
introduced.

The [draw experiment](metal-v2-draw-experiment.md#phase-3-follow-up-effective-attribute-inputs)
records before/after backend measurements, unchanged operation/image checks,
Metal API Validation and expanded sanitizer tests. Same-state/constant medians
improve in that sample; changing-range results do not improve against prior V2.
Broader game validation and the Phase 3 completion gate remain open.

### Phase 3 follow-up — bounded pool trial (fixture only)

The backend now separates its encoding body from the production per-draw
`@autoreleasepool` wrapper. Production V1/V2 retain that wrapper unchanged in
lifetime behavior. The offscreen fixture's opt-in `--pool-experiment` trials
V2 pools drained after at most 8 or 64 draws, always before commit/readback.
All five variants pass image/count, rename-after-drain, early-return and
encoder-rebind checks, plus a separate Metal API Validation run. The Metal
unit suite passes 71 tests (one skip), and the V2 app builds.

The [pool experiment](metal-v2-draw-experiment.md#phase-3-follow-up-bounded-autorelease-pool-experiment)
records modest local median gains, variation and exact reproduction. Grouping
is **not enabled in production**: bounded draw counts alone do not establish
bounded temporary bytes, and game-side cold paths and non-draw operations need
an explicit lifetime audit. This completes a controlled pool trial, not the
Phase 3 game-validation/performance gate or a renderer promotion.

### Prepared-state module extraction — structural, not a new draw ABI

`renderer/metal_v2_prepared.[ch]` now owns frontend depth/blend/raster, four
sampler, pixel-base-key and final fragment-handle reuse. `d3d8_device.c` owns one
zero-initialized cache and resets it before backend initialization. The module
has a compile-time 8 KiB storage ceiling, no heap allocation, no native Metal
objects and no guest-memory ownership. Its header documents rendering-thread
use, borrowed shader-handle lifetime, input spans and reset requirements.

Each operation receives the actual render/stage words and applicable policy
inputs explicitly: depth-target presence, clip-y convention, sampler mip/hires
status and normalized anisotropy, or query sample-count mode. Results are copied
out before HUD/depth-clamp/final shader specialization edits. Dynamic stencil
reference and blend color are still read on cache hits. The synchronous fragment
resolver is called only on a complete-key miss; failed resolution retries.
Reset before any future shader-handle destruction/reuse; native pipeline/depth
object and encoder invalidation remain separate backend responsibilities.

V1 retains its uncached conversions. Mac build wiring includes the new `.c`
only for V2; no OpenGL, other-platform or shared game source is added/replaced.
The draw packet, dispatch, ABI, resource/coherency paths and declaration plans
are unchanged. The frontend only adapts live state into the module's interface.

`tools.test_metal_prepared_state` compiles the actual module as a separate C
translation unit under ASan/UBSan, using synthetic Xbox declarations and the
uncached V1 conversions as the reference. Besides direct-write/restore tests,
it checks independent cache instances, complete reset, returned-copy isolation,
policy changes, final shader specialization, failure retries and disabled
telemetry. These are CPU interface tests, not actual-enum validation, game image
parity or end-to-end draw profiling. The 71-test Metal suite passes (one skip),
seven Mac build tests pass, the real frontend compiles with V2 disabled, and
`ninja macos_v2` links/bundles the app. LSP checks the new module; the large
frontend exceeds its scan limit and is checked by the compiler instead.

This extraction makes dependencies/lifetimes local and testable; it introduces
ordinary C calls rather than claiming a new performance win. Full frontend-to-
Metal/game-scene profiling remains required before considering a replacement
for `gpu_draw()`. V2 stays opt-in; the Phase 2/3 runtime gates remain open.

## 1. Current implementation and evidence

The active Metal implementation is built from `port/macos/metal/renderer/` plus `port/macos/metal/gpu_metal.m` and `metal_host.m`; `port/macos/metal/README.md` explicitly notes that old `port/visr/` sources are not active build inputs.

Current data flow:

```text
Halo game / Xbox XDK inline calls
   -> global render/texture state arrays + exported D3DDevice/Resource API
   -> port/macos/metal/renderer/d3d8_device.c
      + d3d8_resources.c + xbox_textures.c + NV2A translators
   -> port/macos/metal/renderer/gpu.h  [gpu_draw, handles, operations]
   -> gpu_dispatch.c
   -> port/macos/metal/gpu_metal.m
   -> native Metal
```

**Verified implementation facts:**

- `d3d8_device.c` has `D3D__RenderState`, `D3D__TextureState`, `D3D__IndexData`, and `D3D__StateBlockDirty`. It reads simple XDK state from global arrays during `prepare_draw()`; replacing exported setters alone cannot capture all mutations.
- `d3d8_resources.c` preserves 32-bit Xbox addresses and guest/contiguous memory. Vertex/index locks return direct writable CPU pointers. Texture locks also return pointers, with special layout rules. Resource lifetime and registration cannot be replaced with opaque host-only Metal allocations.
- `gpu.h` defines `struct gpu_draw` as a complete per-draw packet (render targets, programs, viewport, scissor, blend/depth/raster, four stages, sixteen streams/attributes, indices, primitive). It also defines query, stream, present, shader warm and resource operations. Dropping the header wholesale would strand current callers; replace each dependency deliberately.
- `gpu_metal.m` already uses native Metal, resource renaming for in-flight writes, bounded frames in flight, transient buffer slots, render-pass reuse, pipeline caching, prewarming, vertex pulling with attribute-function specialization, and optional state-cache optimization. V2 must preserve these verified behaviors, not merely reintroduce them under new names.
- **Correction to earlier emphasis:** `d3d8_device.c` *already* has a 4 KiB-page geometry mirror (`mirror_range` / `mirror_refresh`) based on `memory_watch_generation`, with a volatile-page streaming fallback. It also caches index minima/maxima for eligible mirrored index ranges (`index_extent`). New static-geometry caching is **not** a foundational Phase 1 task. Optimize hit rates, invalidation cost and per-draw metadata before replacing it.
- **V1 baseline:** for each draw, `prepare_draw()` zeroes a complete draw packet, fills raster state, builds a pixel shader key from current Xbox state, resolves up to four texture stages, looks up shaders, assembles a uniform-input array and compares it to the previous draw. `setup_streams()` walks the vertex declaration, validates mirrored/streamed spans and fills up to 16 attribute slots. `gpu_metal_draw()` again resolves Metal objects and pipeline/depth state; `bind_attributes()` loops over 16 slots and unconditionally calls `[encoder setVertexBytes:&table ... atIndex:2]`. Shader constants already use serial-based snapshot reuse, and many Metal state setters already use `metal_state_cache.h`. V2 now skips some of this work as described in the checkpoint above. These remain **candidate costs**, not confirmed top offenders until profiled.
- `tools/macos_build.py` selects Metal via `MetalLp64Build`, filename-based source substitution and explicit Objective-C compilation. The experimental V2 selector is now exclusive of V1, supplies `HALO_MACOS_METAL_V2` and uses a distinct build directory; selector/source-isolation tests exist. Basename substitution still warrants auditing for collisions as dependencies are added.
- `tools/metal_regression.py` currently runs nine OpenGL/Metal cases: menu; seven gameplay anti-aliasing modes; and per-pixel lighting. The existing comparison tolerances are mean RGB error <= 3/255 and fraction of pixels with channel error > 16/255 <= 3%. Gameplay is timed idle, not a deterministic render replay.
- `port/macos/metal/README.md` documents pipeline/archive warming, a 16 MiB upload bound and 65,536-vertex draw limit, texture cache safeguards, API validation, and features that remain unsupported/deferred. These are compatibility requirements, not evidence that the implementation is fully audited.

## 2. Problem definition: cost of a draw, not cost of an API label

The project is motivated by high draw-call frequency. The hot path is `D3DDevice_DrawVertices` / `DrawIndexedVertices` → `prepare_draw` → `setup_streams` / `index_extent` → `submit_draw` → `gpu_draw` dispatch → `gpu_metal_draw` → `draw_pipeline`, `depth_state`, `bind_stages`, `bind_constants`, `bind_attributes` → Metal draw.

**The intended unit of optimization is CPU microseconds per submitted draw and total CPU draw-submission time per frame**, especially when adjacent draws share most state. Reducing the `gpu_draw` call count is neither necessary nor sufficient. Every draw still needs a Metal draw command unless explicitly replaced by a correct, independently validated indirect/multi-draw approach.

### High-frequency work observed in source

| Site | V1 behavior to test | Possible V2 change | Important constraint |
|---|---|---|---|
| `prepare_draw()` | Clears full `struct gpu_draw`; prepares raster/targets | Retain prepared immutable state, populate only changing fields | Guest globals can change via XDK inline writes |
| Pixel-shader key | Copies the relevant D3D render-state range and canonicalizes it per draw | Compare compact shader dependencies; reconstruct/lookup only on miss | Shader keys also depend on stage formats, lighting and query mode |
| `stages_fill()` | Resolves active textures and samplers each draw | Cache last resolved stage metadata/Metal handles where provably valid | Guest texture memory, render-target mip composites, palettes and overrides can change |
| Uniform inputs | Builds a `DRAW_UNIFORM_INPUT_COUNT` input array and `memcmp`s; serial reuse already exists | Make independently dirty groups, avoid rebuilding unaffected converted blocks | Vertex constants, fog and texture scales can change between draws |
| `setup_streams()` | Traverses declaration, checks spans/mirror, prepares attributes | Precompile declaration descriptors; validate current spans/memory generations | Do not bypass mirror updates or bounds checking |
| `index_extent()` | Already caches min/max for mirrored stable index ranges | Improve hit/miss behavior only if profiling warrants | Dynamic index data still requires scanning/copy |
| `gpu_metal_draw()` | Resolves pipeline/depth states and binds dynamic state every draw | Reuse a prepared Metal draw state bundle and skip redundant resolution | Change in render-pass encoder resets its bound state |
| `bind_stages()` | Visits four stages and tests texture/sampler bindings | Skip unchanged groups while maintaining `use_texture`/lifetime tracking | Version changes and in-flight renaming must be detected |
| `bind_attributes()` | Populates 16-entry attribute table and calls `setVertexBytes` per draw | Cache declaration/static table; skip repeated upload when full table is unchanged for current encoder | Stream offsets, constant attributes and buffer versions may differ each draw |
| Per-draw `@autoreleasepool` | Establishes an autorelease scope around every Metal draw call | Trial a safe frame/pass-level scope in a controlled benchmark | Retained/autoreleased objects must not accumulate unboundedly |

**Avoid micro-optimizing the wrong thing:** A function pointer dispatch or modest `memcpy` may be inexpensive relative to texture resolution, Metal-object dictionaries, attribute setup and Objective-C encoder calls. Attribute improvements, CPU state compilation and combined bindings have greater plausible leverage; measurements decide.

### Required Phase 0 profiling

Record both inclusive and exclusive CPU time per draw and per frame for: `prepare_draw` (subdivide targets/raster/key/stages/shader/uniforms), `setup_streams`, index extent/primitive conversion, `gpu_metal_draw` (pipeline/depth lookup, bind stages, bind constants, bind attributes, command encoding), Metal API call counts, pool cost, and resource/memory-watch work. Include draw count and state-change rates, shader/stage cache hit rate, and uploaded bytes. Measure total CPU submission wall time using low-overhead frame counters; use sampled profilers for attribution. Exclude GPU execution and vsync wait from CPU draw-cost numbers.

Required workload categories: (1) long same-material draw sequences; (2) frequent shader/texture swaps; (3) dynamic geometry/constant changes; (4) indexed static-world geometry; (5) HUD/immediate-mode; (6) translucent effects/queries. Include at least two reproducible draw-heavy game scenes. Benchmark V1 with existing `debug.metal_state_cache` enabled, production shader warm caches, and the geometry mirror active.

**Go/no-go:** Optimize the largest measured per-draw stage first. A complete ABI/frontend rewrite is gated on evidence that targeted improvements cannot remove the remaining cost. A small V2 inside the current renderer is an acceptable final result if it wins.

## 2A. Low-overhead draw fast-path requirements

The fast path must be **incremental and compositional**. Compile/reuse a prepared pipeline/shader state, per-shader vertex declaration, depth/raster state, four-stage binding plan and immutable Metal objects, while permitting per-draw geometry, uniforms, texture content versions and render target changes. The common case should require no allocations and no shader/pipeline dictionary lookup when compatible identity is unchanged.

### A. Separate change domains; avoid a single all-or-nothing cache

1. **Shader/pipeline domain:** NV2A program IDs, compact normalized pixel-combiner dependencies, lighting/AA/sample-count/attachment formats, blend/write mask, vertex attribute specialization, border variants and features that alter pipeline creation. Reuse resolved `MTLRenderPipelineState` on a valid hit.
2. **Depth/raster domain:** normalized depth/stencil descriptor versus dynamic viewport, scissor, cull, winding, bias, blend constants and stencil reference. Reuse `MTLDepthStencilState`; emit changed dynamic values only.
3. **Texture/sampler domain:** resource object identity plus allocation/content/target generation, texture format/type, palette, mip-composite dirtiness, high-resolution override and normalized sampler description. Reuse resolved binding metadata *only after* relevant versions and live state are checked.
4. **Vertex declaration domain:** parsed element formats, packed masks, stream/attribute map and constant slots. Build its immutable format plan once per declaration/program, then supply changing stream bases, strides, vertex range and current attribute values per draw.
5. **Uniform/constants domain:** existing serials for shader constants and uniforms; optionally subdivide input groups if it yields a measured win. Never bind a previous draw's constants if their serials changed.
6. **Encoder domain:** track the exact active render encoder identity and resource binding state. Encoder creation/end, target transitions, pass boundaries and explicit state invalidation reset which bindings can be reused.

The fast-path hit is valid only when **every dependency of the reused part** is unchanged. A change in one domain must not force rebuilding unrelated domains. Reusing pipeline state does not imply reusing geometry, bytes, material textures or shader constants.

### B. Xbox global-state observation (critical)

`D3D__RenderState` and `D3D__TextureState` are directly written by XDK inline functions. A setter-maintained dirty bit or monotonic generation is not authoritative. At each draw, perform a bounded, cache-friendly comparison of **the actual relevant state values** against a previously compiled snapshot. Scan only semantically relevant fields of the live arrays, organized by domain. On a changed domain, normalize and resolve state, then update its cache; use a hash only as an index, with sufficient equality checks to avoid collision errors.

Do not cache `D3D__StateBlockDirty` as proof that *all* inline writes are tracked. Prove dependencies with table-driven state mutation tests, including write → draw → restore → draw sequences and shader-dependent stage reinterpretation. Any change to the direct-write state observation mechanism must be demonstrated by byte-for-byte/replay parity first.

### C. Prepared draw-state contract (illustrative)

```c
/* Private to macOS V2; conceptual contract, not the game's ABI. */
typedef struct {
    uint64_t shader_key;
    uint64_t pipeline_key;
    uint64_t depth_stencil_key;
    uint64_t declaration_key;
    uint32_t encoder_generation;
    uint32_t target_generation;
    /* Owned or safely retained native Metal objects live in an Obj-C sidecar. */
} MetalPreparedDrawState;

typedef struct {
    uint32_t primitive, count;
    int32_t base_vertex;
    uint32_t guest_stream_range_id; /* validated range, not a raw host pointer */
    uint32_t index_range_id;
    uint64_t constant_serial, uniform_serial;
    /* Current resource versions and dynamic-state references. */
} MetalDynamicDrawArgs;

/* Resolve state domains independently, then issue the original ordered draw. */
void metal_v2_draw_fast(const MetalPreparedDrawState *,
                        const MetalDynamicDrawArgs *);
```

A separate slow path remains authoritative for unknown combinations, cache misses, unsupported formats and debugging. The proposal does not require introducing those exact typedefs or a new per-draw allocation. Benchmark passing structures versus direct shared state.

### D. Attribute table and Metal bindings

The 16-slot vertex pulling table is an especially concrete target. Reuse predecoded declaration fields and keep an encoder-local copy or validated key for its effective table (including constants, offsets, strides, formats, buffers and stream versions). **Suppress `[encoder setVertexBytes:... atIndex:2]` only when the entire table has not changed since the last bound table on that encoder.** When dynamic vertex offsets change, the table may have to be rebound even if the pipeline and shader are identical. Do not rely on a declaration-only key for this decision. A ring-buffer-backed table is another A/B candidate if repeated `setVertexBytes` calls dominate.

Avoid repeating sampler/texture binding calls when the actual Metal objects and samplers are unchanged; continue tracking every GPU resource referenced by a draw, including after an encoder-state cache hit. Test a grouped argument-buffer binding path **after** the cheap early optimizations. Apple documents argument buffers as a method for reducing the number and cost of individual resource bindings; they add encoder, shader-layout and residency responsibilities and must win a benchmark before adoption.

### E. Indexed geometry and memory coherency

Reuse the existing `mirror_range` / `mirror_refresh` / `index_extent` machinery. Preserve volatile-page streaming, `memory_watch` generation checks, 4 KiB mirror bookkeeping on 16 KiB macOS host pages, current upload span limits and in-flight Metal buffer renaming. Investigate whether repeated per-draw `mirror_range`, declaration traversal or index-range cache misses dominate before developing a replacement cache. `Lock` is not an authoritative dirty notification. Correctness fallback remains streaming/resolving via the V1 logic.

### F. State invalidation and forbidden optimizations

Invalidate the right state domains on target resize, render-pass restart, shader deletion/replacement, resource release/reuse, texture edit, palette edit, mip-composite update, map change, AA/lighting configuration changes, pipeline archive reset, Metal object renaming, visibility/query transition, and explicit diagnostic/debug mode. Do not globally sort opaque and blended draws or combine different draws into a single hardware draw without a separate semantics proof. Keep per-draw ordering and one Metal draw per Halo draw as the MVP. Indirect command buffers are a post-MVP experiment, not a prerequisite.

### G. Required fast-path tests

- Adjacent identical pipeline/stages/declaration with changing vertex ranges and constants; compare against V1.
- Direct global state write with no setter; mid-stream toggle and restoration; `StateBlock` behavior.
- Vertex declaration switch; packed and constant attributes; indexed base vertex; fan/quad/line conversion; large/invalid spans.
- A guest CPU write after draw A but before draw B, including writes into an already-mirrored range.
- Texture/target alias and mip composite, palette update, high-resolution texture replacement, sampler border, depth-only draws and query mode.
- New encoder after target change or pass split must rebind required state, even with otherwise identical shader/material.
- Draw trace comparison includes command count, order, pipeline/depth keys, resource-generation identities and sampled uniform checks.

## 3. Goals, non-goals, invariants

### Goals

- Reduce per-draw frontend interpretation and Metal encoder CPU work; use prepared state across compatible draws. Eliminate `gpu_draw()`/`gpu_dispatch.c` as mandatory **V2 hot-path** transitions only if the removal produces additional measured gains.
- Reduce redundant CPU work per draw (shader-key construction/lookup, stage resolution, declaration walks, pipeline/depth lookups and attribute/binding commands). Improve geometry upload only when a measured opportunity remains beyond the existing mirror.
- Preserve screenshot and animated-frame visual parity, Xbox observable behavior, and current Mac features.
- Bound transient memory, avoid CPU writing into memory still read by the GPU, and retain useful debugging and validation.
- Allow independent build, benchmark, and removal of V2 without changing other platforms.

### Non-goals

- Replacing Halo's game renderer or changing Xbox game-side rendering calls.
- Removing every D3D8-named symbol or Xbox-addressed resource structure.
- Rewriting the proven NV2A instruction semantics, inventing a new shading language, or requiring an ahead-of-time MSL compiler.
- General material reordering, GPU-driven visibility/culling, universal batching, global render graph reorganization, or indirect command buffers in the required MVP.
- Automatically adding Forge selectors, unsupported custom GL platform meshes, stereo, or previously deferred input/UI features. Existing supported functionality must remain supported; unrelated unsupported features are not silently promoted to acceptance gates.
- Promising a specific FPS increase before bottleneck attribution.

### Non-negotiable invariants

- **Ordering:** preserve the order and effects of every visible draw, clear, state-dependent query, copy, lock/readback and present operation.
- **ABI:** preserve exported Xbox/D3D-facing function names, signatures, calling conventions, globals, 32-bit address conversions, game-visible object layouts, and reference/lifetime behavior.
- **Memory:** Xbox guest memory is the source of truth; a change visible to the original renderer must be visible to V2 at the same draw/operation boundary.
- **Render correctness:** retain D3D clip/depth conventions, pixel-center/HUD behavior, format/swizzle semantics, texture border modes, precision assumptions and post-processing/present rules.
- **Safety:** the present upload bounds, draw limits and texture allocation guards remain enforceable; newly introduced code must not bypass them.
- **Isolation:** no changed C/Objective-C objects or symbols in non-Mac build inputs; OpenGL and Metal V1 continue to build and run.

## 4. Proposed V2 architecture

```text
Halo / XDK (unchanged)
  | exported symbols + inline writes to Xbox state globals
  v
MetalV2XboxABI
  | Xbox resource layouts, entry points, state storage, commands
  v
MetalV2StateCompiler
  | domain-specific live Xbox state comparison + prepared draw state
  | reusable shader/declaration/material plans + changing draw args
  v
MetalV2Execution
  | existing memory-watched mirrors + in-flight-safe buffers
  | encoder-local binding reuse + ordered native Metal draws
  v
Metal device / command queue
```

Suggested ownership boundaries (module names are proposed, not files claimed to exist):

| Proposed module | Responsibility | Explicitly excluded |
|---|---|---|
| `metal_v2_xbox_abi.[ch]` | Exported Xbox symbols, state arrays, ABI/static assertions, device lifetime | GPU pipeline internals |
| `metal_v2_state.[ch]` | Draw-time state snapshot, canonical shader/pipeline keys, constant snapshot | Direct Metal API objects |
| `metal_v2_resources.[chm]` (optional/late) | Adapt existing mirror, guest-memory checks and Metal resource versions as needed | Replacing working memory coherence without profiling |
| `metal_v2_draw.[chm]` | Stream/index conversion, immutable pipeline acquisition, binding and draw encoding | Arbitrary draw sorting |
| `metal_v2_passes.[hm]` (optional/late) | Only measured improvements to ordered existing pass transitions | Speculative global scheduling |
| `metal_v2_host.m` or adapter | Device, CAMetalLayer, frame slots, present/readback, instrumentation | Rewriting SDL/window lifecycle without evidence |
| Shared existing NV2A files | Vertex/pixel translation semantics, MSL output and shader keys | Compiler rewrite in MVP |

**Implementation structure rule:** Do not fork the full 201 KB D3D8 frontend and 171 KB Metal backend indefinitely. Extract Mac-neutral Xbox semantics and validated shader/format helpers *where they do not affect other platform builds*, then share those helpers between V1 and V2. Keep explicit ownership and named interfaces; enforce that only one implementation of each exported symbol is linked.

### Internal C/Objective-C interfaces (illustrative, not ABI)

```c
/* V2-internal only, not game-visible layouts. Fixed-width fields as needed. */
typedef struct {
    uint64_t render_state_hash;
    uint64_t texture_state_hash;
    uint64_t vertex_program_id;
    uint64_t pixel_program_key;
    uint64_t state_revision;
} MetalV2StateIdentity;

typedef struct {
    MetalV2StateIdentity state;
    uint32_t color_target_id, depth_target_id;
    uint32_t primitive, element_count;
    int32_t base_vertex;
    uint32_t stream_binding_count;
    /* validated guest ranges, shader constant snapshot, scissor/viewport, etc. */
} MetalV2DrawDesc;

/* Strictly ordered call from Xbox frontend, consumes immutable draw data. */
void metal_v2_encode_draw(const MetalV2DrawDesc *draw);
void metal_v2_transition_for_copy(/* source/destination versions */);
void metal_v2_present(/* resolved back buffer and frame metadata */);
```

A typed draw descriptor remains useful for lifetime, testing and deterministic replay. The change is that its fields and compiler are **Metal-specific**, and V2 need not construct/dispatch a generic backend packet before encoding.

## 5. Xbox state / ABI contract

1. Inventory actual exported linker symbols and `D3DDevice_*` / `D3DResource_*` entry points by building Metal V1, extracting the symbol list, and comparing against V2 after each milestone. Verify function signatures, `WINAPI` / `D3DFASTCALL` where applicable, and guest layouts for each architecture.
2. Retain `D3D__RenderState`, `D3D__TextureState`, `D3D__IndexData`, `D3D__StateBlockDirty` and any other game-visible globals discovered by the symbol audit.
3. At each draw, compare or snapshot **all relevant dependencies** of any reused shader/pipeline state, not necessarily every element in the source arrays. An intercepted setter may accelerate updates but must never be the sole truth for an array written by XDK inline code. Avoid using a cached generation number unless writes to its underlying inputs are guaranteed observable.
4. Split state into: (a) immutable shader/pipeline key; (b) dynamic Metal state (cull, viewport, scissor, depth/stencil, blend constants, etc.); (c) shader constants; (d) resource bindings and content versions. Canonicalize irrelevant and undefined fields to avoid pipeline explosion.
5. Snapshot shader constants/uniforms before recording work that can outlive the CPU call; explicitly retain the game's reserved constants, state-block behavior and viewport-derived constants.
6. Preserve errors, edge conditions, visibility tests, callback/kick semantics and rendering behavior as observed in V1. `D3DDevice_IsBusy` and callback behavior should be studied before attempting more asynchronous behavior; do not silently change an existing guest-visible synchronization contract.

**Required tests:** direct mutation of render and texture state arrays with no setter; state change between adjacent draws; state-block capture/apply behavior; shader constant updates; zero/invalid handles; viewport and target transitions; guest address conversion on arm64 and x86_64.

## 6. Resource and memory coherence contract

### Identity and storage

An Xbox resource is identified first by its game-visible address/metadata, not by a host Metal pointer. **Reuse V1 identity, mirror and resource tables first.** If fast-path lookups require a new side table, key it by resource identity and **generation/epoch** that changes on destruction, re-registration, resize or address reuse. Its metadata must correctly account for format, dimensions, layout, subresources, residency, last GPU serial and in-flight storage versions; do not duplicate tables merely for architectural tidiness.

Separate these concepts:

- **Guest allocation:** CPU-visible memory the game can write and lock.
- **Logical resource:** the Xbox object and its formats/aliases, including game-created resources.
- **Content version:** a specific validated snapshot of bytes suitable for an encoded draw.
- **Metal allocation:** `MTLBuffer` / `MTLTexture` backing a content version, retained until the consuming GPU work completes.

Prevent stale cache hits from address reuse: neither a physical address alone nor a pointer alone is a durable identity. Render-target textures aliasing the same Xbox `Data` region must resolve to consistent underlying contents, including copy/readback and MSAA rules.

### Dirty detection

Locks in the current frontend frequently return raw writeable guest pointers and do **not** furnish a general corresponding unlock notification; the guest may also write through preexisting addresses. Therefore “upload on Unlock” is insufficient.

Coherency policy, from safest to fastest:

1. **Correctness fallback:** continue using the proven V1 mirror/streaming and texture-resolution paths, including conservative uploads for unknown ownership/mutability. Do not introduce extra per-draw uploads merely to implement V2.
2. **Existing write detection:** keep the 4 KiB mirror/page `memory_watch` generation checks and their volatile-page fallback for geometry; keep proven texture invalidation. Record relevant dirty *ranges/subresources* and generation changes where doing so improves the hot path.
3. **Improve static reuse:** measure whether existing mirrored geometry and index extent caches already hit sufficiently. Cache additional geometry/texels only if mapped allocation, mutability and lifetime support reliable version predicates; invalidate on map transition, memory reuse, modifications, and format changes.
4. **Optional page protection / hashing:** must be separately benchmarked and correctness-tested; macOS 16 KB pages and unrelated writes may cause false positives. Hashing whole large regions may exceed saved upload cost. No unverified page protection fast path in MVP.

All CPU uploads go through checked 64-bit address/span arithmetic and preserve existing 16 MiB / 65,536-vertex limits (or a separately audited and tested tightening). Handle swizzled, linear, compressed, mipmapped, cube, volume, depth-stencil, palette and render-target formats according to existing `xbox_textures.c` behavior. Require exact bounds checks for partial updates.

### Asynchrony, renaming and release

The current Metal code renames busy texture/buffer storage and has bounded in-flight frame slots. V2 shall preserve equivalent safety: a CPU mutation after draw A and before draw B must give A the old bytes and B the new bytes even if both GPU commands execute later. This may require copy-on-write/rename and multiple in-flight versions; merely keeping one persistent `MTLBuffer` is incorrect. Destruction may invalidate guest handles immediately while deferring native Metal release until its last GPU serial completes.

**Required tests:** write-after-draw-before-submit; write-after-submit-before-completion; texture and vertex partial writes; reuse same Xbox address for different objects; map transition; multi-frame GPU delay; manual lock pointer writes; volume/cube/mips; dirty detection under 16 KB page granularity; invalid sizes and overflow; low-memory eviction while resources are in flight.

## 7. Shader, pipeline and binding contract

### Shader compiler

Reuse `nv2a_vsh.c`, `nv2a_psh.c`, `nv2a_msl.c` and their real-program corpus/tests initially. The existing translator handles NV2A register combiners and vertex programs, shader constants, fog, border behavior, lighting variants, vertex pulling and recognized per-pixel lighting. Retain its exact semantics and precision/clip behavior, including fallback for unrecognized lighting programs.

A dedicated new NV2A -> MSL compiler is explicitly deferred until it has its own failing case, profiling evidence, differential tests, and a separate RFC. MSL is **already** generated by the current branch, so changing the code generator is not required to claim native Metal execution.

### Pipeline cache

Retain/port V1 archive segments, warm lists, compile statistics and safe archive lifecycle. A pipeline key must cover at least shaders/variants, vertex attribute specialization, attachment formats, sample count, blend factors and operations, color write mask, border specialization, and any feature flag that changes generated MSL or pipeline creation. Depth/stencil state and other dynamic values may be separate Metal objects or encoder state where supported.

- Use stable canonical key serialization; exclude pointers/padding/uninitialized bytes.
- Version persistent keys by renderer schema, shader-translator revision and relevant device/OS capability information. Never reuse incompatible binary caches silently.
- A cache miss compiles correctly and logs cause/cost, without drawing corrupted output.
- Compilation/warming off-thread must not mutate game-owned state or violate cache/archive thread safety.
- Cache sizes and eviction policies are bounded and measured.

### Binding and geometry

The current backend already uses shader-driven vertex pulling and function constants for attribute kinds. Do not assume `MTLVertexDescriptor` conversion is faster. Benchmark both for representative formats; preserve packed/swizzled/normalized formats, missing attribute defaults, base vertex, primitive conversion and immediate-mode data.

First optimization targets: reuse resolved shader/pipeline/depth objects and declared vertex layouts, avoid rebuilding unchanged stage metadata, and eliminate avoidable `setVertexBytes`/encoder binds. The existing vertex mirror already prevents many repeat uploads. Then trial grouped argument buffers or per-material descriptors; add them only if measured CPU savings outweigh encoder, shader-layout, GPU residency and storage costs.

## 8. Ordered pass coordinator and synchronization

Start with V1's proven rule: render passes begin at the first draw/clear for a compatible color/depth target pair and end at copy, mip generation, readback, present or target changes. V2 can improve load/store/resolve choices, but **must retain submitted command order**. Do not sort by material, pipeline or depth by default.

For each resource/version, track reads, writes, attachment use, sample/resolve state and CPU readback barriers. A pass may be merged only if these relationships, attachment compatibility and query semantics are unchanged. A clear may be converted to a Metal `loadAction=clear` only when it is a full compatible attachment clear at a pass boundary and no earlier visible content is required. Partial rectangles, per-channel clears, HUD/split view bounds, stencil/depth preservation and ordered clears retain explicit handling. A `DontCare` store is valid only if **all** later consumers (including screenshots, texture sampling, postprocessing and presentation) provably do not need the contents.

MSAA resolve and depth/stencil sampling must maintain current semantics, including supported sample-count fallbacks. Preserve actual occlusion query/sample-count behavior and any necessary synchronization at result polls. Capture/readback, overlays, AA, HUD blending and fullscreen/resize remain part of the render dependency chain.

An advanced whole-frame dependency graph is **optional later work**. Avoid postponing submission past a point where the game expects a query or resource read to be available.

## 9. Build integration and platform isolation

### Proposed selector

- Existing `--no-macos-metal`: macOS OpenGL reference, unchanged.
- Existing `--macos-metal`: macOS Metal V1, unchanged and the initial fallback/reference.
- New `--macos-metal-v2`: macOS Metal V2, exclusive of the above at configure/build selection; names and configuration plumbing to be finalized by source audit.

Add explicit source manifests/allowlists rather than relying on filename matches alone for V2. Exclude V1 implementations of replaced exported functions; avoid duplicate symbols. Reuse the same LP64 transformation, Xbox address conversion, macOS minimum deployment target, Apple silicon/x86_64 compiler flags and app-bundle process. Build the V1 and V2 executables into distinct artifact directories/names to avoid overwriting test references. Add a CI job for the V2 build and unit checks; runtime screenshot checks remain opt-in on eligible macOS agents with game data.

**Scope guard:** A CI/source-manifest assertion confirms the new build selector and file list do not add/replace source files for Linux, Windows, Android or other platforms. Any change to shared game code must be separately justified, compiled/tested across targets, and kept outside the baseline V2 patch if feasible.

## 10. Observability and test plan

### Required telemetry

Emit a per-frame structured record (JSONL or equivalent, opt-in and low-overhead) with: renderer/version, hardware/OS, map/scene, frame number, CPU frame/render preparation/encode times, GPU completion time if available, draw/clear/pass/encoder counts, pipeline/cache hits/misses, compile time, state cache hits, uploaded vertex/index/texture bytes, renamed allocation count, render-target load/store/resolve counts, in-flight/high-water memory and wait times. Provide separate cold/warm data and instrument validity. Distinguish GPU completion time from on-screen presentation cadence.

### Correctness test pyramid

**A. Pure/unit:** Xbox state hashing and direct array writes; canonical pipeline keys; state snapshot lifetime; address/span overflow; buffer versioning; format/swizzle/mip layout; draw primitive expansion; pass transition decisions; cache invalidation; shader program classification and MSL output. Run sanitizers where feasible.

**B. Metal smoke/validation:** compile shaders, triangles, all relevant render/depth target formats, synthetic load/store, clear, MSAA resolve, readback, occlusion, resource rename under delayed completion, low memory, resize; Metal API Validation enabled. Preserve `macos-metal-smoke` for V1 and add an independent V2 target.

**C. Image regression:** retain all nine current cases and their existing GL-vs-Metal **3/255 mean, 3% bad-pixel** tolerances. Extend capture harness to compare GL, Metal V1 and Metal V2, with build artifacts fixed to exact commit/configuration and preserved logs/diffs. Require V2 to pass the same GL threshold. Also compare V2 against V1; initially collect metrics and set stricter scene-specific tolerances from repeated reference runs because gameplay snapshots are timed idle rather than deterministic. Do not invent a universal zero-difference promise from nondeterministic frames.

**D. Deterministic rendering replay:** implement a debug-only, opt-in stream capture around Xbox draws/state/memory-version snapshots, or a reproducible input/tick/camera capture with verified frame identity, before making precise pixel claims. Ensure trace files cannot retain invalid guest pointers and have bounded size. Maintain per-frame draw counts and command-order digests.

**E. Manual gameplay matrix:** main menu and custom overlays; Blood Gulch bots/moving combat; campaign maps with decals, water, transparent effects and shadow cases; camera/cutscenes/intro/credits where applicable; viewport splits and HUD; all AA modes; per-pixel lighting on/off; resolution scaling, live resize/fullscreen, vsync on/off, ProMotion; map changes/restarts and long sessions. Record untested configurations explicitly. Apple silicon runtime required; x86_64 runtime is a separate target, never inferred from a universal binary compile.

**F. Isolation:** `ninja`/CI compile the unchanged OpenGL and platform targets, and compare their source/build manifests before/after V2.

### Draw-cost performance protocol

In addition to whole-frame timings, report **CPU nanoseconds or microseconds per draw** for the complete `DrawVertices` / `DrawIndexedVertices` submission call, and inclusive `prepare_draw`, `setup_streams`, `gpu_metal_draw`, shader/stage lookup, attribute and constant binding time. Express cost by draw category and state transition (identical pipeline/material, changed constants, changed texture, changed target, changed declaration), not just a global mean. Preserve unoptimized counters showing what was skipped, and count actual Objective-C encoder calls per draw. An optimization that merely moves CPU work to map load should also report that setup cost.

**Required microbench workloads:** many same-state draws with varying vertex ranges; frequent pixel shader changes; alternating textures/samplers; static mirrored indexed geometry; CPU-mutated dynamic geometry; HUD/immediate-mode draws; visibility tests and render-target changes. Include an opt-in low-overhead logging mode and a sampling-profiler mode, and measure how much instrumentation costs.

### Proposed performance protocol

- Use the same machine, OS, cooling/power profile, display mode, resolution, asset set, settings and map scene for both variants. Document exact commits and archive warm state.
- Sample at least five repeated runs per scene after warmup, with a minimum 3,000 steady-state frames when possible. Keep shader cold-start separately measured.
- Benchmark vsync-off/unrestricted draw throughput and realistic vsync-on presentation separately. Do not conflate GPU frame duration, wall-clock frame pacing and CPU submission duration.
- Report median, p95 and p99 CPU render time, GPU time, frame interval, upload bytes, pass count, peak Metal residency, shader compilations and stall events. Include confidence/variation; describe thermal behavior.
- Use profile traces to explain gains or regressions; an aggregate FPS number is not sufficient.

### Initial performance acceptance targets (provisional until Phase 0 baseline)

For at least two reproducible draw-heavy scenes demonstrated to be CPU-limited: **>=15% lower p95 renderer CPU submission time** than Metal V1, measured with the same warm state, with no more than a **5% regression in p95 GPU time** and no more than a **5% regression in p95 frame time** across the representative non-target scenes. Peak retained Metal resource memory should be within **+15%** of V1 in like-for-like workloads, absent a documented, approved tradeoff. These are review thresholds, not predictions; refine using baseline variance and hardware capabilities *before* the implementation is evaluated.

Additionally, demonstrate **>=20% lower median CPU cost per draw** in a predetermined high-reuse microbenchmark, with the same required GPU-visible draws and results (fewer redundant Metal state/binding calls are explicitly desired; a different hardware draw mechanism requires separate review). The threshold is an engineering acceptance target, not a projected outcome; record baseline variance first. Quantify percentage reductions in shader/pipeline lookups, attribute-table updates and encoder binding calls.

A V2 that reaches parity but fails to improve attributable bottlenecks is a useful experiment, **not a reason to retire V1**.

## 11. Migration plan — ordered by likely draw-path savings

| Phase | Engineering work | Completion gate |
|---|---|---|
| **0 — Measure actual per-draw cost** | Instrument `prepare_draw`, stage/shader lookup, `setup_streams`, pipeline/depth lookup, attribute/binding calls and Metal draw encoding; capture controlled microbench and real game baselines. | Ranked hot spots with CPU time/draw, cache-hit rates, real-state-change rates and stable V1 baseline. |
| **1 — Add isolated experimental selector** | Add build-time `--macos-metal-v2` with V1 slow-path parity/forwarding and a feature-gated fast path. Add drawing-digest instrumentation and source-manifest tests. | All existing nine cases pass with fast path disabled; V1 still selectable; no non-Mac changes. |
| **2 — Cache prepared shader/material/depth state** | Compare live Xbox dependencies per draw; resolve shaders, pipelines, samplers and depth states only when relevant domains change; reuse currently compiled shader code and archives. | Direct-write mutation tests pass; no stale state; fewer expensive lookups and measurable CPU-per-draw improvement. |
| **3 — Precompile declaration plans and cut binding work** | Predecode vertex declarations, avoid redundant full 16-slot attribute table rebuilding and `setVertexBytes` on exact encoder-local match; preserve stream validation; test a frame/pass autorelease pool. | Unchanged/dynamic geometry parity, Metal validation and lower encoder calls per draw. |
| **4 — Collapse frontend/Metal boundary if profitable** | Benchmark direct Metal-specific draw arguments against current `struct gpu_draw`/`gpu_draw` interface, avoid duplicate state conversion/lookup; refactor only measured critical seams. | Incremental improvement over Phase 3 with no new ABI/ordering errors; otherwise retain existing dispatch. |
| **5 — Targeted follow-ups** | If traces warrant: tune mirror/index hit behavior, texture stage coherency, grouped argument buffers or per-draw uniform packing. Pass/load/store work only if it is material. | Each experiment has controlled A/B gains, correctness proof and easy disable/rollback. |
| **6 — Validate and decide** | Dynamic campaign/HUD/effects coverage, AA and platform matrix, long soak, repeat performance runs, V1 fallback. | Predetermined draw-cost and frame-time goals met without GPU/memory/visual regressions; otherwise retain V1. |

**Pull request discipline:** Keep each phase reviewable and benchmarkable. A direct native ABI rewrite is deliberately later than cheap skip-work optimizations; it is not required to finish the project successfully. Avoid one PR replacing `d3d8_device.c`, `d3d8_resources.c`, `gpu.h` and `gpu_metal.m` at once.

## 12. Risks, flaws corrected from the initial proposal

| Initial idea or gap | Why it is flawed/incomplete | Corrected decision |
|---|---|---|
| “Replace the entire D3D8 renderer for speed.” | Most draw-path time may be state translation, texture resolution or encoder calls rather than the dispatch abstraction itself. | Cut measured per-draw repeat work first; only collapse architecture where additional measured savings remain. |
| “Remove `gpu.h` entirely.” | Existing shader/texture/query/present/warm code depends on its types/operations. | Bypass the generic *draw* path first; retire old interfaces only once dependencies are enumerated. |
| “Compile materials as immutable objects.” | Raw XDK arrays change outside intercepted APIs; not all state belongs in a pipeline object. | Draw-time canonical state snapshots with shader/pipeline/dynamic/constant/binding separation. |
| “Build a new persistent-geometry cache first.” | V1 already has a memory-watched page mirror and an index-range cache; locks expose raw writable pointers. | Profile mirror hit/miss/validation cost and preserve generations, fallback streaming and GPU-safe renaming. |
| “Introduce a native MSL compiler.” | Existing branch already emits and compiles MSL with substantial parity tests. | Reuse proven translator; new compiler is separate research. |
| “Build a render-pass scheduler that groups work.” | Unrestricted grouping/reordering breaks blending, query order, feedback, copy and partial clears. | Ordered pass coordinator, dependency-based merging only. |
| “Argument buffers/ICBs are an aggressive fast path.” | Commands have inherited state/resource residency constraints and CPU costs; scenes may be dynamic. | Only trial after measurement; require A/B traces and correctness parity. |
| “The nine-case suite makes rewriting manageable.” | The matrix has mostly idle Blood Gulch and lacks deterministic frame replay/motion/other maps. | Expand dynamic and stateful coverage before retirement. |
| “Other platforms remain unchanged automatically.” | Shared source replacements and LP64 tooling could change source manifests unintentionally. | Explicit V2 build manifests, compiled target CI and isolation checks. |
| “Native Metal V2 should greatly boost FPS.” | Fragment/tile/bandwidth bottlenecks are unaffected by C frontend cleanup. | Define CPU/GPU classifications and stop conditions. |
| “Cache by Xbox address.” | Reuse, aliasing, resize and object lifetimes cause stale resources. | Address + metadata + allocation generation + content version; deferred GPU release. |

## 13. Acceptance criteria for merging V2 as the default opt-in Metal renderer

All of the following must be true:

1. Exported symbol/ABI checklist passes on every supported compiled Mac architecture; Xbox state direct-write tests pass.
2. V2 passes existing GL image thresholds in all nine cases without relaxing thresholds, plus the agreed V1 comparison and expanded deterministic/motion/campaign matrix.
3. All currently supported Mac Metal features remain supported: HUD, overlays, display controls, per-pixel lighting, all AA options, shader warm/archive behavior, screenshot/debug capture and readback. Existing known limitations are documented, not obscured.
4. Render/state/resource lifetime tests pass sanitizers as applicable; Metal API Validation and explicit GPU in-flight stress tests report no unresolved errors.
5. No blocker regressions in target/swap/resize, alpha/depth/stencil, visibility queries, palette/mips/texture aliasing, frame pacing and memory safety.
6. Build/source isolation tests confirm no unintended changes to OpenGL or non-Mac target inputs.
7. The predetermined **CPU cost per draw** and whole-frame render-thread submission goals are met with trace-backed attribution, alongside meaningful reductions in expensive per-draw lookups/bind calls and without unacceptable regressions to GPU time, visual fidelity or memory.
8. The V1 build can still be selected; a tested rollback plan restores V1 without changing game data or saved settings.

## 14. Open decisions to resolve in Phase 0/1

- Exact baseline commit hash for the V1 renderer, and whether the branch is rebased/frequently changing.
- Which currently shared frontend helper modules can be used by both V1 and V2 without weakening platform isolation?
- Where else does game code write directly into guest texture/vertex/index memory, and what memory-watch APIs are reliable on Apple silicon/macOS 16 KB pages?
- Which specific draw-submission operations dominate on repeated and changing-state calls: frontend key/stage construction, resource version checks, shader/pipeline/depth lookups, 16-slot attribute-table work, Objective-C state bindings, or Metal draw command encoding.
- Whether per-pixel lighting, HUD preloading, AA variants and shader-list warming can share V1 implementations without retaining the entire `gpu.h` API in V2.
- What runtime-tested hardware matrix is attainable (Apple GPU generations; x86_64 Intel Mac)?
- Which animation/visibility and capture features permit a deterministic frame replay test without adding intrusive gameplay changes?

## 15. Initial backlog / suggested PR sequence

1. **`metal: profile per-draw CPU path and cache hit rates`** — isolate hotspots around `prepare_draw`, `stages_fill`, `setup_streams`, `gpu_metal_draw`/`bind_attributes`; capture two scenes and six microbench mixes.
2. **`build: add independent metal-v2 selection and slow-path baseline`** — preserve V1 build and visual regressions, expose debug/opt-in fast path.
3. **`metal-v2: reuse prepared shader, pipeline and depth state`** — live Xbox dependency checks, per-domain invalidation, collision-safe lookups and tests.
4. **`metal-v2: compile declaration layouts, cache attribute table bindings`** — independent per-encoder binding checks, unchanged-state skips and dynamic-vertex correctness.
5. **`metal-v2: cache safe texture stage resolution`** — retain palettes, mip-composites, guest memory version checks, target alias and overrides.
6. **`metal-v2: benchmark direct draw encoding vs gpu_draw packet`** — eliminate abstraction only if trace-backed incremental savings exist.
7. **`metal-v2: optional argument-buffer/streaming experiments`** — A/B test grouped binds, pool scope and mirror/index improvements; separate pass scheduling.
8. **`tests: deterministic draw replay and broader scenes`** — GPU validation, state mutation, dynamic campaign/effects/HUD parity and soak.
9. **`macos: evaluate v2 promotion, rollback and documentation`** — use measured per-draw and per-frame gates.

## References (inspected source branch / relevant platform documentation)

- [Metal implementation notes](https://github.com/LopezNoah/chupathingyce/blob/feature/macos-metal/port/macos/metal/README.md)
- [Xbox D3D frontend](https://github.com/LopezNoah/chupathingyce/blob/feature/macos-metal/port/macos/metal/renderer/d3d8_device.c)
- [Xbox D3D resources](https://github.com/LopezNoah/chupathingyce/blob/feature/macos-metal/port/macos/metal/renderer/d3d8_resources.c)
- [Generic GPU interface](https://github.com/LopezNoah/chupathingyce/blob/feature/macos-metal/port/macos/metal/renderer/gpu.h)
- [Native Metal backend](https://github.com/LopezNoah/chupathingyce/blob/feature/macos-metal/port/macos/metal/gpu_metal.m)
- [NV2A to MSL translator](https://github.com/LopezNoah/chupathingyce/blob/feature/macos-metal/port/macos/metal/renderer/nv2a_msl.c)
- [Mac build selection](https://github.com/LopezNoah/chupathingyce/blob/feature/macos-metal/tools/macos_build.py)
- [Existing screenshot regression harness](https://github.com/LopezNoah/chupathingyce/blob/feature/macos-metal/tools/metal_regression.py)
- [Apple: Metal debugger and performance counters](https://developer.apple.com/documentation/xcode/metal-debugger)
- [Apple: managing groups of resources with argument buffers](https://developer.apple.com/documentation/metal/managing-groups-of-resources-with-argument-buffers)
- [Apple: indirect command buffers (CPU)](https://developer.apple.com/documentation/metal/encoding-indirect-command-buffers-on-the-cpu)
- [Apple: load/store actions](https://developer.apple.com/documentation/metal/setting-load-and-store-actions/)
- [Apple: resource synchronization](https://developer.apple.com/documentation/metal/resource-synchronization)
