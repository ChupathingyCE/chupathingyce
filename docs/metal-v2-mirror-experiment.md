# V2 geometry-mirror volatility experiment

## Change

For `HALO_MACOS_METAL_V2` only, classify a mirrored page as volatile after two
observed rewrites separated by at most 30 rendered frames (previously four
rewrites within two frames). V1 retains the previous policy. The 600-frame
cooldown is unchanged. The window is in rendered frames, not simulation ticks.

This is an eviction/streaming heuristic, not a new dirty detector. Generation
checks, bounds, 4 KiB mirror bookkeeping, memory-watch protection, the existing
streaming fallback and in-flight buffer renaming are unchanged. After cooldown,
a page returns through the existing absent-page upload path. Unwritten geometry
continues to use the mirror. More conservative classification can increase
streamed bytes; it never licenses reuse of stale bytes.

## Fixture and artifacts

Tested locally on Apple M2 Pro, 12 cores, 16 GiB RAM, using the temporary
`local/metal-v2-bots` integration branch (V2 plus `feature/multiplayer-bots`).
Baseline: V2 with the existing mirror sub-timers and old policy. Candidate:
identical build with only the V2 policy changed. Both used `--bots --lto=off
--pgo=off`. Exact copied executables and logs are in the ignored
`build/metal-policy/` directory; `summary.json` contains the extracted measurements.
These local artifacts are not a persistent replay or a portable benchmark.

Each executable was run twice for 75 seconds in order baseline, candidate,
candidate, baseline, hidden/background-only. The first third of each run's
60-frame reporting windows was discarded. Example invocation (change executable
and output directory for the other runs):

```sh
HALO_ALLOW_LAUNCH=1 HALO_APP="$PWD/build/metal-policy/candidate" \
  tools/metal_scripted_run.sh "$PWD/build/metal-policy/candidate-1" 75 \
  HALO_BOTS=31 HALO_GPU_STATS=1 HALO_RENDER_REGRESSION=1 \
  HALO_WINDOW_SIZE=1280x720 HALO_DISPLAY_MODE=windowed \
  HALO_NETWORK_TEST_START=2
```

The runner isolates configuration/saves, disables online listing, UPnP and
reporting, and starts a local host with no remote clients. Each run logged 31 bot
joins. `bot:1` scripts the local player's controller, separately from those bots.
The fixture fixes the game seed, but **does not produce deterministic frames**:
real-time bot simulation, camera motion and combat differ across runs.

## Results

Values below are medians of **60-frame aggregate reporting windows**, not
individual-frame medians or p95/p99 samples. Timings include instrumentation and
OS scheduling; nested stage times must not be added together.

| Metric | Baseline 1 | Baseline 2 | Candidate 1 | Candidate 2 |
| --- | ---: | ---: | ---: | ---: |
| Whole non-immediate draw call, ns/call | 1516 | 1479 | 1324 | 1140 |
| `setup_streams`, ns/call | 508 | 503 | 142 | 131 |
| `setup_streams`, ms/frame | 0.97 | 1.01 | 0.21 | 0.28 |
| `mirror_refresh`, ms/frame | 0.82 | 0.85 | 0.03 | 0.03 |
| Streaming upload, ms/frame | 0.01 | 0.01 | 0.04 | 0.07 |
| Streamed geometry, KiB/frame | 255 | 247 | 246 | 335 |
| Non-immediate draws/frame | 1864 | 2064 | 1342 | 2214 |

`mirror_range`/`mirror_refresh` instrumentation also sees mirrored indices;
stream-upload instrumentation covers `setup_streams`' vertex fallback.
Mirrored-byte telemetry counts referenced spans, **not actual GPU upload bytes**.
Actual mirror-upload volume, GPU time and peak memory were not measured here.

The reduction in refresh work supports retaining this experimental V2 policy.
Different draw counts and camera contents mean these results are **not** proof of
an end-to-end FPS gain or satisfaction of the RFC's performance acceptance gates.
Sparse updates, address reuse, map transitions and host-page sharing can affect
classification. More scenes, soak tests, actual upload/residency telemetry and
repeated GPU/frame measurements are still needed.

## Checks

- Both builds compiled and all four gameplay runs exited successfully.
- Additional 45-second candidate run with 31 bots and `MTL_DEBUG_LAYER=1`:
  confirmed “Metal API Validation Enabled”, exited successfully, no observed
  validation errors/assertions/fault reports.
- `python3 -m unittest tools.test_metal_mirror_policy tools.test_macos_metal_build`
  passes. The mirror test compiles the actual mirror implementation twice
  (V1/V2), with ASan/UBSan and mocked memory-watch/upload operations. It covers
  stable reuse, raw writes between resolves, rotating writes, rapid writes,
  sparse writes, volatile-range fallback, cooldown expiry/re-upload, 16 KiB
  host-page generation sharing and invalid/cross-segment spans. Reinstating the
  old V2 thresholds makes the rotating-write regression assertion fail.
- These mocks do not prove real page protection or GPU version lifetime.
  Validation smoke tests do not establish image parity. No nine-case image
  regression, deterministic replay, delayed-completion stress, GPU timing,
  memory-residency audit or V1 bot runtime comparison was performed here.

## Follow-up: image matrix and uncapped frame timings

### V1/V2 visual regression

Built fresh V1 and V2 (without bots) from the same working tree and ran the
existing nine-case matrix, with Metal API Validation, unchanged mean error
limit 3/255 and bad-pixel fraction limit 3%. `metal_regression.py` now accepts
prebuilt V1/V2 renderer labels so artifacts do not incorrectly call V1 OpenGL.

```sh
HALO_ALLOW_LAUNCH=1 python3 tools/metal_regression.py \
  --gl-app "$PWD/build/metal-next/v1" --metal-app "$PWD/build/metal-next/v2" \
  --reference-name MetalV1 --candidate-name MetalV2 \
  --output "$PWD/build/metal-next/parity"
```

All nine cases passed: menu, AA off/FXAA/SMAA/SSAA2x/MSAA2x/MSAA4x/MSAA8x,
and per-pixel lighting. Camera checks passed; no rendering failures were
reported. Worst mean error was AA off, 1.697/255, with 2.89% bad pixels
(close to the existing 3% limit). This is timed-idle screenshot parity, not
animated bot replay parity. Exact images, diffs and per-case metrics remain
in `build/metal-next/parity/`.

### Policy A/B with uncapped rendering

Added `tools/metal_policy_benchmark.py` for repeatable offline ABBA launches of
prebuilt, bot-enabled binaries. It checks bot joins, the seed hook, draw/frame
telemetry and render errors; retains logs, executable hashes and overrides;
and reports medians of reporting windows after dropping the first third.
This compares the old-policy V2 and tuned-policy V2 integration executables,
**not** V1 versus V2. Existing GPU completion timing is used unchanged.

```sh
HALO_ALLOW_LAUNCH=1 python3 tools/metal_policy_benchmark.py \
  --reference-app build/metal-policy/baseline \
  --candidate-app build/metal-policy/candidate \
  --output build/metal-next/performance
```

Four 75-second, 31-bot runs, 1280x720, AA off, native scaling, pacing off,
vsync off and no FPS cap. Validation is disabled for timing. Results retained
in `build/metal-next/performance/results.json` and per-run logs:

| Warm-window metric | Old policy 1 | Tuned 1 | Tuned 2 | Old policy 2 |
| --- | ---: | ---: | ---: | ---: |
| Draw CPU, ns/call | 1682 | 1104 | 1119 | 1465 |
| Mirror refresh, ms/frame | 0.83 | 0.03 | 0.02 | 0.86 |
| Whole-frame CPU median, ms | 3.65 | 4.30 | 4.25 | 4.20 |
| Whole-frame CPU p95, ms | 10.25 | 6.75 | 6.80 | 11.15 |
| Whole-frame GPU median, ms | 2.75 | 2.70 | 2.70 | 2.80 |
| Whole-frame GPU p95, ms | 3.85 | 3.40 | 3.55 | 3.90 |
| Non-immediate draws/frame | 1340 | 2065 | 2076 | 2103 |

Each frame median/p95 above is itself the median of 10 warmed 600-frame
window statistics, **not** a pooled percentile. The draw/geometry counters
use separate 60-frame windows. CPU timing is the backend's work interval
minus its tracked pacing waits; GPU duration is accumulated from completed
command buffers, not a synchronized per-frame CPU/GPU pair.

Interpretation: submission cost and CPU window-p95 improved; whole-frame CPU
medians did not improve, and GPU medians stayed similar. Different real-time
bot scenes/draw counts prevent an end-to-end FPS or acceptance-gate claim.
There were no observed render errors and all four runs logged 31 joins.

### Remaining gates

The visual matrix and repeated CPU/GPU frame measurements above supersede
those omissions in the first experiment. Still outstanding: deterministic
animated replay/trace parity, a second draw-heavy map, actual upload and
memory-residency telemetry, delayed-completion/resource-lifetime stress,
long-running/map-transition tests and an instrumentation-disabled performance
comparison. Keep V2 experimental; do not promote it to default on this evidence.
