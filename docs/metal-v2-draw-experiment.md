# Metal V2 prepared-state checkpoint

**Status: experimental; backend-only evidence, not a promotion decision.**

Measured on Apple M2 Pro, arm64, macOS 26.3.1, with the working-tree changes
on top of `bf389ec4448d593b1976ee343861329477abd6d6`. This is distinct from the
previous mirror-policy experiment. The V1 reference here is the same backend
compiled without `HALO_MACOS_METAL_V2`; the Xbox frontend is **not** exercised.

## Reproduction

```sh
python3 tools/metal_draw_benchmark.py --output /tmp/metal-draw-ab --rounds 5
python3 tools/metal_draw_benchmark.py --output /tmp/metal-draw-validation --rounds 1 --validation
```

No game data, window or installed config is used. Both executables compile the
actual `gpu_metal.m`, with `-O2`, the state-binding cache enabled, pipeline archives
disabled and vertex specialization disabled for the fixture's custom shaders.
Each process runs the same three ordered draw loops into a 16x16 offscreen target.
There are 2 warmup batches, 7 timed batches and a separate counted batch per
workload, each containing 2,048 draws. Five independent rounds shuffle variant
order with a fixed seed. Compilation, clear, commit, readback and completion waits
are outside timing. Submission profiling is disabled in timed batches.

`v2-native-off` is an **ablation control**: frontend-independent V2 attribute-table
reuse remains enabled, but the new native pipeline/depth reuse is disabled. It is
not the normal V2 configuration. Counter instrumentation and Metal API Validation
are not used for the production-style timing comparison.

The runner retains JSONL samples, stderr logs, build commands, source SHA-256s,
trial order, commit and dirty status in its output directory. The final local
runs were `/tmp/metal-native-final` and `/tmp/metal-native-final-validation`.

## CPU submission results

Median of the five process medians, nanoseconds per `gpu_metal_draw` call:

| Workload | V1 | V2/native-off | V2 | V2 vs V1 median reduction |
| --- | ---: | ---: | ---: | ---: |
| Same state | 272.7 | 272.2 | 239.0 | 12.4% |
| Changing constant serial/value | 439.5 | 445.3 | 409.8 | 6.8% |
| Alternating vertex stream offsets | 282.7 | 308.5 | 270.0 | 4.5% |

**There are slower V2 samples.** These are not universal wins, confidence bounds
or game-wide speedups. All five process medians are retained below, including the
particularly slow first V2 round; no outliers were discarded:

| Variant / workload | Round medians, ns/draw |
| --- | --- |
| V1 / same | 302.327, 266.195, 271.505, 277.547, 272.705 |
| V2/native-off / same | 308.533, 263.733, 275.248, 252.625, 272.156 |
| V2 / same | 352.478, 238.973, 262.390, 227.763, 226.501 |
| V1 / constants | 453.064, 420.369, 443.664, 422.302, 439.453 |
| V2/native-off / constants | 585.490, 441.650, 455.526, 429.810, 445.333 |
| V2 / constants | 700.399, 409.770, 401.835, 416.138, 397.848 |
| V1 / ranges | 296.773, 305.888, 282.654, 270.792, 281.148 |
| V2/native-off / ranges | 353.984, 315.430, 308.451, 295.573, 302.063 |
| V2 / ranges | 434.713, 270.040, 265.361, 266.337, 288.066 |

The ablation's range regression shows that cache bookkeeping is not free. The
fixture exposed a full-table update cost; the attribute cache now treats layout,
constant values and stream ranges independently, updating only streamed slots
when offsets change. Even after this change, the ablation remains slower there.
The combined V2 median is better in this sample, but its margin is small and
individual runs regress. More stable, longer real-scene A/B tests are required.
No thermal/power control or confidence interval was established here.

## Work skipped and correctness checks

Separate counted batches produce the following actual backend operation counts:

| Operation per 2,048 draws | V1 | V2 |
| --- | ---: | ---: |
| Shader-record lookups | 4,096 | 2 |
| Pipeline cache resolutions | 2,048 | 1 |
| Depth-state cache resolutions | 2,048 | 1 |
| Attribute table binds, same state/constants | 2,048 | 1 |
| Attribute table binds, changing offsets | 2,048 | 2,048 |
| Encoded draws | 2,048 | 2,048 |

All variants produce image digest `fa16d7c7a05fa525` for all three workloads.
The fixture additionally checks:

- Blend factors/operation/write mask, separate alpha factors, shader identities,
  attribute kinds, depth attachment format, exact-border variant and live 2x MSAA
  storage changes cause pipeline resolution; blend color alone does not.
- All normalized depth/stencil fields cause the appropriate re-resolution;
  stencil reference and masked-out high bits do not.
- A shader-warm boundary invalidates prepared native references; missing shaders
  do not return a previous valid pipeline.
- Texture and vertex-buffer writes after draw A but before submission rename
  busy storage. Left/right readback sees the old red and new blue texture values,
  respectively. Buffer bindings follow the renamed storage.
- A fresh encoder rebinds an otherwise identical attribute table, without forcing
  immutable native pipeline/depth resolution again.

A separate run of V1, V2/native-off and V2 passed with the **Metal API Validation
Enabled** banner and no validation fault. Debug-layer timings are deliberately
not treated as performance results; that run does include slower V2 timings.
These synthetic checks are not a general moving-scene replay, a many-frame
GPU-delay stress test, or game-wide screenshot/lifetime parity.

## Exact source provenance for the measured binaries

SHA-256s recorded by the final timing run:

```text
120df223b955f10a04151e397fdde5e79757740e2c96364c20f115ce713b9679  tools/metal_draw_probe.m
facf9fb43b94c4fd7e99f1934340eda2ced3a34ac79a743f1c9225f89f97690a  port/macos/metal/gpu_metal.m
ca6bee40f7068bbc0b0cc4b9fdfb18c39fc4a2b0055b18a38d04c93074075ebc  port/macos/metal/metal_attribute_table.h
6ab3613d2c0abe7d259de06f7cfcec40335b5e67d4c500b45a1b38c8a0dc93c2  port/macos/metal/metal_state_cache.h
d6716f34f980f5668d914e32bb6775a9fc1c87b67b64c1b3eb31a9af2dd94bda  port/macos/metal/renderer/draw_profile.h
```

## Phase 3 follow-up: effective attribute inputs

A separate experiment on the same Apple M2 Pro, based on
`36dee1d2421e318c5bb5ff52a114f519e4a3ee3c` with working-tree changes, removes
full stream/constant snapshots from `metal_attribute_cache`. Its fixed-size
layout plan partitions all 16 slots into streamed and constant lists. On a
compatible layout it compares only effective constants and streamed
offset/stride values against the table itself. Ignored streamed constants,
unused streams and buffer handles no longer trigger full-array copies/scans.
Live buffer resolution, binding and resource-use tracking remain unchanged.
This reduces CPU bookkeeping; it does **not** eliminate additional Metal binds.

The unchanged runner was invoked with `--rounds 5` before and after the change;
raw samples, commands, source hashes and logs are in `/tmp/metal-phase3-before`
and `/tmp/metal-phase3-final`. A preliminary candidate run was also retained in
`/tmp/metal-phase3-trial`; it is not substituted for the final measurements.
These are sequential local runs, not an interleaved old/new statistical trial.

Median of five process medians, ns/draw:

| Workload | Before V1 | Before V2 | After V1 | After V2 |
| --- | ---: | ---: | ---: | ---: |
| Same state | 270.4 | 234.7 | 270.3 | 220.4 |
| Changing constants | 434.6 | 421.0 | 439.6 | 404.7 |
| Changing ranges | 273.2 | 266.4 | 284.2 | 267.5 |

Final V2 process medians, including slow first rounds:

- Same state: 349.711, 218.994, 221.171, 218.933, 220.398.
- Constants: 685.893, 404.744, 400.330, 401.754, 413.900.
- Ranges: 425.883, 263.814, 272.990, 256.673, 267.476.

Same-state and constant medians improve in this sample. **Changing ranges do
not improve against the previous V2**, and individual runs still regress.
No confidence interval or game-wide speedup is claimed. The final same-state
V1/V2 difference is still below the RFC's 20% acceptance target.

All variants retain digest `fa16d7c7a05fa525`, the same ordered draw count and
the operation counts above. `/tmp/metal-phase3-validation` passed the separate
one-round Metal API Validation run; its timings are not performance evidence.
CPU ASan/UBSan tests additionally cover shared streams, all-streamed/all-constant
transitions, ignored inputs, format-derived stride and 4,096 deterministic mixed
transitions against the uncached table construction. The 68-test Metal suite
passes (one skip), and `ninja macos_v2` links and bundles the app.

Attribute-header SHA-256 before:
`ca6bee40f7068bbc0b0cc4b9fdfb18c39fc4a2b0055b18a38d04c93074075ebc`;
after:
`dfb706a9340eb978d54ca5fc28170948555452699303292e2da91ab837bbc3b3`.
Other fixture/backend source hashes remain recorded in each run's `summary.json`.

This is a Phase 3 engineering slice, not phase completion. Game/frontend
geometry parity, broader declaration workloads and game-scene performance
remain open. At this checkpoint, per-draw autorelease pools were unchanged;
the separate bounded-pool experiment below does not promote a production change.

## Phase 3 follow-up: bounded autorelease-pool experiment

Production V1/V2 still enter an autorelease pool for **every draw**. The encoding
body is extracted into the private `gpu_metal_draw_in_pool` helper, whose caller
must supply a pool. Only the offscreen fixture bypasses the production wrapper.
It trials V2 groups of **8 or 64 draws**, draining each group before GPU commit
and readback. This is not a frame-lifetime pool or a new production option.

Reproduce (choose fresh output directories):

```sh
python3 tools/metal_draw_benchmark.py --output /tmp/metal-phase3-pools-final --rounds 5 --pool-experiment
python3 tools/metal_draw_benchmark.py --output /tmp/metal-phase3-pools-final-validation --rounds 1 --pool-experiment --validation
```

Without `--pool-experiment`, only the original three variants run. With it, all
five variants run in seeded shuffled order each round; the grouped variants use
the **same V2 executable** as the per-draw control. `summary.json` now also records
exact run commands, group sizes and expected draw-pool drain counts. For each
2,048-draw batch those counts are 2,048 (per draw), 256 (groups of 8), and 32
(groups of 64). These are derived counts of the explicit draw/group scopes,
**not** instrumentation of every nested Foundation/Metal pool or its byte usage.
The aggregator rejects image, draw-count and group-bound mismatches.

Final local run: Apple M2 Pro, arm64, macOS 26.3.1, working tree based on
`36dee1d2421e318c5bb5ff52a114f519e4a3ee3c`. Median of five process medians,
ns/draw; validation is disabled for these measurements:

| Workload | V1 | V2/native-off | V2/per draw | V2/pool8 | V2/pool64 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Same state | 278.4 | 256.5 | 231.6 | 222.8 | 219.6 |
| Changing constants | 446.0 | 443.2 | 413.9 | 405.3 | 406.6 |
| Changing ranges | 279.0 | 296.0 | 270.7 | 259.6 | 257.1 |

V2 process medians for all five rounds (no discarded samples):

| Variant / workload | Round medians, ns/draw |
| --- | --- |
| Per draw / same | 233.602, 230.123, 235.840, 231.608, 230.835 |
| Pool8 / same | 221.781, 231.099, 220.317, 222.799, 222.982 |
| Pool64 / same | 219.015, 221.578, 219.564, 221.415, 218.608 |
| Per draw / constants | 408.183, 418.172, 415.304, 413.188, 413.879 |
| Pool8 / constants | 402.222, 414.266, 402.059, 418.416, 405.335 |
| Pool64 / constants | 406.596, 393.127, 402.689, 415.792, 408.203 |
| Per draw / ranges | 264.730, 269.918, 276.245, 270.732, 271.708 |
| Pool8 / ranges | 259.562, 266.235, 260.640, 259.399, 253.154 |
| Pool64 / ranges | 257.406, 255.554, 259.318, 255.798, 257.060 |

Pooling improves these local medians by roughly 2–5% relative to per-draw V2;
individual samples still regress. A preliminary run in `/tmp/metal-phase3-pools`
had a larger constant-workload improvement; the final data above is not replaced
with that more favorable result. The narrow same-state pool64/V1 median crosses
20%, but this fixture-only result does **not** satisfy the full RFC acceptance
gates. Loop grouping also changes loop/branch overhead, so the differences are
not isolated measurements of Objective-C pool push/pop cost. No confidence
interval, memory high-water proof or game-wide speedup is established.

All five variants preserve digest `fa16d7c7a05fa525`, 2,048 encoded draws per
counted batch and the prior V2 lookup/attribute-binding counts. In-flight buffer
and texture rename checks now explicitly drain a pool before renaming and before
submission; encoder-rebind checks remain active. An additional test drains after
missing-shader and fully clipped early returns, then verifies a valid draw still
encodes and reads back correctly. All five variants pass the separate Metal API
Validation run. The Metal unit suite passes 71 tests (one skip), including the
ASan/UBSan table tests; `ninja macos_v2` links and bundles the app. Both real
V1/V2 fixture builds pass; LSP alone cannot validate the Objective-C sources in
this workspace because its SDK configuration is incomplete.

Measured source SHA-256s:

```text
fd5951e8e6edd4bede15c80685acb842c7dee8fe377b93921aa5b69e323d5ea8  tools/metal_draw_probe.m
570ba7951684fd7e8014e129a36edccb20d9c1d3a166fa6168f9b07f12a57a92  port/macos/metal/gpu_metal.m
dfb706a9340eb978d54ca5fc28170948555452699303292e2da91ab837bbc3b3  port/macos/metal/metal_attribute_table.h
```

**Decision:** retain the fixture experiment, not grouped production pools.
Before adoption, audit autoreleased temporaries on cold/miss/primitive-conversion
paths, measure peak temporary memory, and define a bounded owner across clears,
copies, queries, target transitions, resource operations and presents. A draw
count limit alone does not bound bytes allocated by those paths. Full game
geometry/declaration/lifetime parity and controlled game-scene timings remain
required. Phase 3 is still in progress.

## Decision at the original checkpoint

Commit the independently testable reuse work and the benchmark, **keep V2 opt-in**,
and retain V1. At the original checkpoint the RFC's 20% high-reuse draw-cost
target was not demonstrated by the backend-only sample. The later fixture-only
pool experiment does not establish the complete frontend/frame-time gates either.
No direct `gpu_draw` bypass, argument buffers or indirect drawing are justified
by this experiment. Next validation should compare instrumentation-disabled
V1/V2 gameplay on controlled scenes and investigate the slower samples rather
than extrapolating the median into an FPS claim.
