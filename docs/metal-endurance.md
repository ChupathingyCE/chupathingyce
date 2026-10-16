# Metal V2: second-map and lifetime validation

> Historical record from the Metal V2 experiment; V2 is now the only Metal renderer (see [metal.md](metal.md)).


Local follow-up to [the mirror experiment](metal-mirror-experiment.md).
The renderer policy itself is unchanged in this follow-up. All runs use the
prebuilt bot-enabled V2 integration executables from that experiment, offline
with 31 bots plus the local player. Automated runs use hidden/background-only
windows; the interactive handoff below uses a visible window. The user's
data/configuration is not modified. Executable hashes and environment
overrides are retained in the output directories.

## Progress reporting

`metal_policy_benchmark.py` and the new `metal_lifetime_soak.py` print START,
a flushed “running” heartbeat every 30 seconds, and DONE/PASS or failure.
`progress.json` retains bounded-log snapshots and process RSS samples. The
supervisor has a hard deadline and terminates its process group on timeout or
interruption. These are CLI heartbeats: a tool host that buffers stdout can
still delay display, so the agent launched the supervisors in the background
and polled their progress logs. Details remain in files rather than flooding
the terminal with game logging.

## Second map: Hang 'Em High

```sh
HALO_ALLOW_LAUNCH=1 python3 -u tools/metal_policy_benchmark.py \
  --reference-app build/metal-policy/baseline \
  --candidate-app build/metal-policy/candidate --map hangemhigh \
  --output build/metal-endurance/hangemhigh-abba
```

Four 75-second runs in ABBA order; 1280x720, native scaling, AA off, no vsync,
no frame cap, pacing off. Metal API Validation is off for performance runs.
All four exited successfully and logged 31 bot joins. This benchmark
was not run concurrently with either of the validation tests below.

| Warm-window metric | Old policy 1 | Tuned 1 | Tuned 2 | Old policy 2 |
| --- | ---: | ---: | ---: | ---: |
| Draw CPU, ns/call | 1222 | 1143 | 1204 | 1260 |
| Mirror refresh, ms/frame | 0.25 | 0.00* | 0.01 | 0.22 |
| Whole-frame CPU median, ms | 3.55 | 3.45 | 3.80 | 3.65 |
| Whole-frame CPU p95, ms | 8.00 | 6.35 | 7.15 | 8.10 |
| Whole-frame GPU median, ms | 4.10 | 3.30 | 3.90 | 3.80 |
| Whole-frame GPU p95, ms | 5.30 | 4.80 | 4.95 | 5.30 |
| Non-immediate draws/frame | 1708 | 1577 | 1800 | 1771 |

\* Rounded by the existing profiler to 0.00 ms; not zero work.

Frame statistics are medians of ten warmed 600-frame window statistics;
p95 is **not** a pooled percentile. Draw statistics use separate 60-frame
windows. Moving bot gameplay is not a deterministic replay. The draw CPU
improvement is smaller than on Blood Gulch; refresh work and CPU window-p95
are lower in both tuned runs. CPU medians are mixed. GPU numbers and different
draw counts do not establish an end-to-end FPS win.

Artifacts: `build/metal-endurance/hangemhigh-abba/results.json`, per-run logs,
and `build/metal-endurance/abba-progress.log` (local, ignored build outputs).

## Longer resource-lifetime and map-transition runs

Both tests use MSAA4x at 1280x720, uncapped rendering, the existing bounded
frames-in-flight/versioning machinery, and Metal API Validation. They are
run sequentially after the benchmark, not concurrently. No renderer teardown,
cache-invalidation, memory-watch or GPU-completion behavior was changed to
make the tests pass.

```sh
# Sustained gameplay; the high score target avoids spending the soak at scores.
HALO_ALLOW_LAUNCH=1 python3 -u tools/metal_lifetime_soak.py \
  --app build/metal-policy/candidate --output build/metal-endurance/soak \
  --seconds 300

# Real in-process game teardown/reload, through the existing host playlist.
HALO_ALLOW_LAUNCH=1 python3 -u tools/metal_lifetime_soak.py \
  --app build/metal-policy/candidate --output build/metal-endurance/transitions-score1 \
  --seconds 300 --maps bloodgulch hangemhigh bloodgulch --score 1 --kill-interval 0
```

The soak harness requires the API-validation banner, enough bot joins, the
expected number/order of game starts, each map's shader-list activity, simulation
progress and nontrivial geometry draws. It also checks each game's fixed seed
and all 32 named player records. The single-map soak must sustain simulation
for almost its full requested duration. A configured playlist or mere successful
exit alone is not considered proof of successful map transitions.

### Sustained-gameplay result

The 300-second Hang 'Em High soak passed, exiting normally with API validation
enabled. It reached tick 8850 (295 seconds of game simulation), observed up to
2531 non-immediate draws/frame, and reported 782 resource renames. Ten process
RSS samples showed a 432 MiB peak, with first-third/last-third medians of
263.75/264.11 MiB. This is an observational process-memory result, not proof
that all GPU allocations are released or that no leak exists.

Artifacts: `build/metal-endurance/soak/{fixture,results,progress}.json`,
`run.log`, `debug.txt`, and `build/metal-endurance/lifetime-progress.log`.

### Transition result

The first transition attempt used a five-kill score limit. It remained in the
first game too long to reliably exercise reloads within the time budget, so
it was intentionally stopped and retained under `build/metal-endurance/transitions/`
(with `interrupted.txt`). No renderer validation error was observed before
interruption. The retry changes only the match score to one kill; renderer
validation and loaded-map/player/gameplay checks are unchanged.

The one-kill retry reached the second game and was intentionally stopped at
user request to hand over to visible interactive play. It is retained under
`build/metal-endurance/transitions-score1/` with `interrupted.txt`; it is **not**
a completed three-map validation. A future unattended retry can use the
harness's existing scripted scoring-kill hook (`--kill-interval 15`) rather
than relying on bot combat to end every match. That updated fixture has not
yet been run to completion.

### Interactive handoff

A visible, windowed V2 bot-enabled app was launched on Hang 'Em High with 31
bots, no scripted player input, no automatic exit or scoring kills, and all
online/public/UPnP/reporting flags disabled. Metal API Validation, draw/frame
profiling and 30-second process RSS sampling are enabled. Logs and the exact
launch settings are isolated in `build/metal-endurance/interactive/`.
Memory sampling is bounded to two hours; the game itself has no time limit.
At handoff, all 31 bot joins and the validation banner were confirmed with
no observed assertion/fault/validation-error markers.

The user subsequently played and quit. The 1.6 MiB run log recorded all 31
bot joins, CPU/GPU frame and draw profiling, 217 reported resource renames,
no observed assertion/fault/validation-error markers, and a normal “window
closed” ending. The separate interactive memory sampler produced **zero
samples**; no memory-footprint conclusion can be drawn from that session.
A completed return-trip transition remains pending.

## Limits

These are longer real-game smoke/soak tests, not a GPU residency/leak audit.
Process RSS includes CPU, caches and driver allocations and is not Metal heap
residency. Resource rename counters cover reported 600-frame windows, so the
final incomplete window is not counted. Shader warming, address reuse and
normal in-flight activity occur, but no artificial command-buffer completion
delay is injected. API validation and lack of crashes do not prove animated
image/trace parity or the absence of rare lifetime bugs.

Still needed before making V2 default: deterministic animated replay/trace
parity, actual upload/residency telemetry, synthetic delayed-completion stress,
an instrumentation-disabled comparison and substantially longer endurance
runs on multiple Macs. The earlier nine-case timed-idle V1/V2 image matrix
passed; it is not a 31-bot transition-image replay.
