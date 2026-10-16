# Metal V2 game profile: 31 combat bots, two maps

> Historical record from the Metal V2 experiment; V2 is now the only Metal renderer (see [metal.md](metal.md)).


**Status: exploratory evidence. Does not meet the RFC promotion gates.**

## Fixture

- Branch rebased onto `origin/main` (`4428651d8`, includes the offline bots
  port); tip `4a0e6dea0` plus uncommitted prepared-state/profiling work.
  Exact source and executable hashes: `build/metal-frontend-profile-bots/provenance.json`.
- Apple M2 Pro, macOS 26.3.1. V1 and V2 both built with
  `--bots --lto=off --pgo=off` from the same working tree.
- `tools/metal_game_profile.py`: hidden, offline, isolated data root; 31 marine
  bots (join count verified per run), fixed host seed, scripted local player,
  1280x720 window (2560x1440 drawable), AA off, per-pixel lighting off, vsync
  and pacing off, uncapped FPS, Metal state cache on, no API validation.
- Identical learned shader lists, from separate V2 warmup runs, were copied
  into every run. 45 s per run; first third of reporting windows discarded.
- Per map: two **attribution** runs per variant (draw-stage timers on) and two
  **throughput** runs per variant (timers off), ABBA order. 16 runs, all passed.

```sh
HALO_ALLOW_LAUNCH=1 python3 tools/metal_game_profile.py \
  --reference-app build/metal-frontend-profile-bots/builds/v1 \
  --candidate-app build/metal-frontend-profile-bots/builds/v2 \
  --output build/metal-frontend-profile-bots/measured/runs \
  --warm-lists build/metal-frontend-profile-bots/warm-lists \
  --seconds 45 --repeats 2 --bots 31
```

Bot combat is real-time and not a replay: camera content and draw counts vary
between runs. Values are medians of per-run window medians; window p95 values
are not pooled frame percentiles. Two runs per cell cannot support confidence
intervals.

## Per-draw attribution (timers on), ns per call

| Stage | Blood Gulch V1 | Blood Gulch V2 | Hang 'Em High V1 | Hang 'Em High V2 |
| --- | ---: | ---: | ---: | ---: |
| Whole non-immediate `Draw*` call | 1554 | 1290 | 1419 | 1155 |
| `prepare_draw` | 321 | 394 | 327 | 338 |
| `  key` (base key, excl. stages) | 44 | 94 | 47 | 84 |
| `setup_streams` | 480 | 118 | 232 | 90 |
| `gpu_draw` (dispatch + backend) | 631 | 630 | 726 | 622 |
| Draws/frame (incl. immediate) | 1891 | 1806 | 1461 | 1173 |

Backend stages are each 50–140 ns; the largest are encode draw, constant
binding, pipeline lookup and `pass_begin`. V2 frontend cache hit rates:
depth 97–98%, raster 97–98%, sampler 95–97%, blend 87–92%, shader key and
fragment 82–89%, declaration 100%.

Findings:

1. **The V2 per-call reduction (~17–19%) comes from `setup_streams`**, i.e. the
   earlier mirror-volatility policy, not prepared state.
2. **The prepared shader key costs more than V1's uncached key** (~2x). With
   82–89% hit rates, the full 256-word combiner comparison on each hit is not
   cheaper than rebuilding. This is the clearest regression to fix.
3. **Attribute-table reuse rarely applies in gameplay**: 0.98–0.99 binds per
   draw. The offscreen fixture's large reduction does not transfer.
4. **The `gpu_draw` boundary is not a demonstrated bottleneck.** Backend work
   is spread across many ~100 ns stages; no single stage justifies bypassing
   the generic packet.

## Whole frame (timers off), ms

| Map / variant | CPU median | GPU median | CPU window p95 |
| --- | ---: | ---: | ---: |
| Blood Gulch V1 | 3.38 | 2.82 | 9.72 |
| Blood Gulch V2 | 3.65 | 3.00 | 6.12 |
| Hang 'Em High V1 | 2.70 | 3.52 | 7.17 |
| Hang 'Em High V2 | 2.55 | 3.05 | 5.85 |

No consistent whole-frame CPU improvement: Blood Gulch V2 is slower at the
median, Hang 'Em High faster. V2 window p95s are lower in both maps; this may
reflect the mirror policy's reduced refresh spikes, but two runs per cell and
uncontrolled combat cannot establish that.

A separate 5 s `sample` of a V2 warmup run showed 53% of main-thread samples
inside `CAMetalLayer nextDrawable`, and substantial game-side collision BSP
work. Even uncapped, the main thread is often drawable-bound, so FPS is a poor
indicator of draw-submission savings.

## Follow-up: prepared pixel base key removed (do not reintroduce)

Isolated microbenchmark (clang -O2, M2 Pro, 256-word key, constants changing):

| Strategy | ns |
| --- | ---: |
| Old V2 cache hit: branchy per-word scan | 208 |
| Cache hit with `memcmp` over runs between skipped constants | 23 |
| V1 uncached rebuild: 1 KiB copy + zero 18 words | 14 |

Even an optimal hit check costs more than rebuilding, so V2 now uses V1's
uncached base key. The fragment-handle cache (full-key compare) is kept; it was
neutral in-game (shader lookup ~33 ns in both variants).

Re-measured with the same harness, attribution mode, one V1/V2 pair per map
(`build/metal-frontend-profile-bots/measured-key/runs2`):

| Map | Key ns V1 / V2 | `prepare_draw` ns V1 / V2 | Draw* ns V1 / V2 |
| --- | ---: | ---: | ---: |
| Blood Gulch | 43 / 43 | 323 / 355 | 1578 / 1326 |
| Hang 'Em High | 49 / 45 | 341 / 343 | 1444 / 1295 |

The key regression is gone. V2 `prepare_draw` is still not faster than V1: the
remaining depth/blend/raster/sampler caches are at best neutral. The per-draw
win remains `setup_streams` (mirror policy).

## `nextDrawable` wait: source and status

`gpu_metal_present` calls `[CAMetalLayer nextDrawable]` (gpu_metal.m). With
vsync off (`display sync off` logged) and `HALO_MAX_FPS=-1`, it blocks only
when every layer drawable is still held by the GPU or window server. GPU time
is ~3 ms of a ~8–9 ms frame, so the wait is probably not the renderer: the
harness runs with `HALO_HIDDEN_WINDOW=1` (SDL hidden window, not composited
normally), which may throttle drawable release.

**Unverified.** The frame-timing log now appends `slot/drawable wait X ms
(95th Y)` (time blocked on the frame-slot semaphore plus `nextDrawable`,
already excluded from frame CPU). The hidden-vs-visible window comparison was
started and stopped before producing data. Next: run the same scene hidden and
visible and compare this wait and the frame rate. If only hidden runs block,
it is a harness artifact and FPS from hidden runs should not be used.

## Decision

- Do **not** start a `gpu_draw` replacement or wider rewrite on this evidence.
- Done: prepared base key removed (see follow-up). Consider removing the other
  neutral frontend caches unless a future profile shows a win.
- Resolve the `nextDrawable` question before using FPS from hidden runs.
- Retain V1 and keep V2 opt-in. Longer runs, more repeats and more scenes are
  needed before any performance claim.
