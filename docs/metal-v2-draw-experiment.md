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

## Decision at this checkpoint

Commit the independently testable reuse work and the benchmark, **keep V2 opt-in**,
and retain V1. The RFC's 20% high-reuse draw-cost target is not demonstrated by
this backend-only sample, nor are its complete frontend/frame-time gates met.
No direct `gpu_draw` bypass, argument buffers or indirect drawing are justified
by this experiment. Next validation should compare instrumentation-disabled
V1/V2 gameplay on controlled scenes and investigate the slower samples rather
than extrapolating the median into an FPS claim.
