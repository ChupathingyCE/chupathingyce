# Bot test procedures and historical local-run notes

> **Historical note:** most run logs and native-game results below came from the
> legacy working tree. They are local/offline experiments, not results for the
> ChupathingyCE port. This port was build- and asset-free-test checked only; no
> live match or network session was run. Bots remain local-only and do not
> replicate to remote machines. Current setup and supported scope are in
> [bots.md](bots.md).

Controlled behavior trials (open-field contact, line-of-sight memory, routes,
low shields, pickups, team sightings, strafing, unseen attackers and crowds)
are implemented by `tools/run_bot_scenarios.py`; see
[bot-scenarios.md](bot-scenarios.md) for current setup and interpretation.

Run all commands from the repository root. Python 3, clang, Ninja and the
platform build dependencies are required. Standalone runners do not need pytest
or game assets.

## Asset-free checks

```sh
python3 tools/test_engine_ai_utility.py --sanitize
python3 tools/test_engine_ai_aim_claims.py --sanitize
python3 tools/test_engine_ai_navigation.py --sanitize
python3 tools/test_surface_navigation.py --sanitize
python3 tools/test_engine_ai_behavior.py --sanitize
python3 tools/test_engine_ai_traversal.py --sanitize
python3 tools/test_bot_movement_log.py
python3 tools/test_bot_validation_runner.py
python3 tools/test_bot_awareness_log.py
python3 tools/test_bot_perception_steering.py --sanitize
python3 tools/test_bot_fire_safety.py --sanitize
python3 -m pytest -q tools/test_bot_scenarios.py
cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tools/test_engine_ai_fire_control.c source/engine_ai/fire_control.c \
  -o /tmp/test-bot-fire
/tmp/test-bot-fire
sh -n tools/run_bot_match.sh
git diff --check
```

`--sanitize` enables AddressSanitizer and UndefinedBehaviorSanitizer;
`--cc clang` selects the compiler on the Python C runners. A successful primitive
test is not proof of live bot movement.

### Standard two-flag CTF

`python3 -m pytest -q tools/test_bot_ctf.py` compiles production CTF pickup,
return, capture and drop callbacks, the copied-state adapter, manager, objective
execution and floor-arrival checks with ASan/UBSan. Unit/physics operations are
fake-world seams, not bot-side ownership or score writes. Cases include friendly
return without inventory acquisition, the strict 1 m capture boundary,
own-flag-away/scoring/client gates, stale and dead carriers, drop/reset, claims,
intentional stand waits, retries, two-carrier rescue duties and hidden-position
isolation. Carrier weapon/grenade restrictions are in `test_bot_fire_safety.py`.

After a bots-enabled build, use legal Xbox map assets:

```sh
python3 tools/check_bot_ctf_live.py --maps /path/to/maps --map beavercreek
python3 tools/check_bot_ctf_live.py --maps /path/to/maps --map bloodgulch --seconds 80
python3 tools/check_bot_lobby_live.py --maps /path/to/maps --menus pc --mode ctf
python3 tools/check_bot_lobby_live.py --maps /path/to/maps --menus xbox --mode ctf --accelerated
```

The runners use hidden windows, fresh saves and disabled online/reporting, and
opt into CTF only for their own process. The fast runner advances game time at
20x (the same debug timing setting is now registered on macOS); menu input stays
on wall time. It requires both flags to be acquired and a real capture, not
just reaching a goal. Add `--both-captures` for the stricter release sample of
a capture by each team. Real match outcomes vary; that stricter gate can
legitimately fail for a one-sided round. Inspect its retained artifacts rather than assuming a route
bug. The ordinary lobby checker requires an actual capture by either team.

CTF stays off by default (`bots.ctf = false`); King and Oddball are not enabled.
See [objective scope and validation](bot-objective-modes.md).

### Camouflage and final steering seams

`test_bot_perception_steering.py` compiles the real perception, recovery and
submission functions extracted from `bots.c` against a fake world (the `.c.in`
file supplies only fixtures/stubs). It checks:

- Active/super camouflage flags and residual fade amounts suppress **new visual
  acquisition and callouts**, even at two metres. There is no close-range visual
  exception yet. An earlier sighting remains a frozen last-known position until
  normal memory expiry; an old teammate callout is likewise a position, not
  live tracking. A recent attacker record still gives nonvisual awareness,
  without making that attacker visible or issuing a callout.
- All on-foot movement is filtered **after stuck recovery, before throttle**.
  Eight candidate headings and two clearance passes bound this to sixteen
  capsule sweeps per bot per tick, each with eight collision planes. The live
  capsule includes crouch height and follows its current support plane uphill.
  Free movement first prefers 0.12 m of extra wall clearance; retreat
  initially accepts 0.02 m. Following an already capsule-checked BSP route uses
  the exact capsule first and caps the probe at the next waypoint's distance:
  extra clearance or a wall beyond the waypoint must not steer a bot away
  from a valid route. Both free policies can fall back to an exact fit. If all
  probes fail, movement stops rather than submitting a blocked heading.
- Fake-wall, narrow-passage, boxed-in, uphill and seated/no-move cases, including
  submission of recovery-generated movement. Vehicle steering is unchanged.

There is no sneaking behavior. A future stealth behavior may choose a different
clearance preference, but must not bypass collision safety. These tests verify
policy and integration ordering, not the game's actual BSP collision routines.

### Friendly-fire gates

`test_bot_fire_safety.py` compiles the real lane, grenade and trigger
functions from `bots.c` against a fake world. It checks that a teammate on or
near the firing lane holds fire (the margin widening with range, and with the
weapon's tag cone: a precise weapon fires past a teammate a unit off the line
at ten units, one with a 0.1-radian cone holds) while an enemy, a dead
teammate, or one behind, beside, above or past the fire range does not; that
hostile tag angles (NaN, negative, infinite, huge) are ignored or capped; that
riders in the bot's own vehicle do not block it and riders in another do; that
a full player table, a missing body or player, a non-finite facing, range or
spread fail closed; that throws are refused for a teammate near the target or
the aim point, or running (or driven) into the blast within the fuse, for the
bot's own blast, a surface in its face and a teammate in the throwing lane,
and retried in a second; that a held or overcharged trigger stays held with a
swap to the other slot (none for a vehicle's weapon, a missing weapon or a
third slot); that a blocked lane still lets an empty magazine reload; and the
gates' counts. It does not fire real projectiles.

## Implicit human callouts (Team Slayer)

With `configure.py --bots`, `bots.human_callouts = true` is the default.
Disable it with `HALO_BOT_HUMAN_CALLOUTS=false` or in the isolated config:

```toml
[bots]
count = 7
skill = "spartan"
human_callouts = true
```

Local humans (including split-screen teammates) automatically share an enemy's
last-seen position after **one continuous second** in a conservative cone inside
that player's actual camera view, within 60 m and with clear LOS. Zoom and pitch
matter. Looking away, obstruction, camouflage, death/respawn, a team change or a
missed simulation tick resets acquisition. Fully qualified contacts refresh
only while visible; bots consume them through the existing shared-awareness
policy, not as permission to shoot through walls. All difficulties can receive
human callouts; only Spartans automatically call out their own sightings.

When you take non-silent damage from an enemy, nearby teammate patrol bots
(within 32 m of you) turn toward the incoming-fire bearing for up to two seconds.
The cue is an approximate area 8 m from where you were hit, derived from the
**damage direction**, never the hidden attacker's location or distance. It has
no enemy identity, does not start firing or hijack combat/retreat/pickups, and
is allowed even if the attacker is cloaked. Near misses are not detected yet.
There is no dialogue, ping UI or remote-player callout protocol.

Human callout behavior is implemented, but this repository has no dedicated
`test_human_callouts.py` runner. Asset-free perception and fire-safety checks do
not validate camera, split-screen or damage-to-bot-facing behavior. Those local
integration checks are deferred; none was run while porting this branch.

## Build and offline 4v4 Rat Race match (macOS)

One local host plus seven bots makes eight Team Slayer players. The script
balances them 4v4, disables online/public networking, runs the repository
executable, and defaults this preset to Rat Race:

```sh
python3 configure.py --bots
ninja macos
BOT_PRESET=4v4 BOT_SECONDS=120 sh tools/run_bot_match.sh \
  > build/macos/botrun/ratrace-4v4.log 2>&1
```

The previous Rat Race smoke run passed sampled movement for all seven bots
and logged both weapon and powerup pursuits. That run predates the latest claim
release logging, so it does not validate release/handoff behavior. The host is
the eighth player; there is no additional remote player in this offline fixture. Use
`BOT_HIDDEN_WINDOW=1` for a hidden-window/null-renderer run; it still uses the
platform's SDL video driver, so Linux windowed runs should use the documented
Xvfb setup. For the normal 24-bot Blood Gulch run, use the command below
without `BOT_PRESET`.

## Build and visible 24-bot match (macOS)

```sh
python3 configure.py --bots
ninja macos
# Prepare an isolated data root with maps/ from your own Xbox Halo installation.
# The existing build/macos/botrun/data root can be reused if already prepared.
sh tools/run_bot_match.sh > build/macos/botrun/match-$(date +%Y%m%d-%H%M%S).log 2>&1
```

The script runs `build/macos/halo` directly from the repository, not an app
bundle. It disables public networking/reporting, uses Team Slayer on Blood
Gulch, and equips Assault Rifle + Magnum. Keep existing run data and saves;
do not remove them just to rerun. For separately preserved evidence, use a new
data/save root for each run and copy or symlink your legal Xbox maps into it.

Script overrides:

```sh
BOT_DATA_ROOT=/absolute/path/to/test-data \
BOT_SAVE_ROOT=/absolute/path/to/test-saves \
BOT_COUNT=24 BOT_SKILL=spartan BOT_SECONDS=180 \
sh tools/run_bot_match.sh
```

| Script variable | Default / meaning |
| --- | --- |
| `BOT_BINARY` | Repository `build/macos/halo`; override for another built executable |
| `BOT_DATA_ROOT` | `build/macos/botrun/data`; contains Xbox `maps/` and game log |
| `BOT_SAVE_ROOT` | `build/macos/botrun/saves`; isolated settings/saves |
| `BOT_PRESET` | `24` (Blood Gulch, 24 bots) or `4v4` (Rat Race, 7 bots plus host) |
| `BOT_MAP` | Map file name; preset default is `bloodgulch` or `ratrace` |
| `BOT_GAME` | `team_slayer` (default) or `slayer` |
| `BOT_COUNT` | Preset default; supported bot range 1–31 |
| `BOT_SKILL` | `spartan`; also `recruit`, `marine`, `odst` |
| `BOT_LOADOUT` | `rifle_pistol`; Assault Rifle + Magnum |
| `BOT_SECONDS` | 180; requests an exit after this many seconds. Timed exit did not fire in the latest headless run, so stop headless runs externally |
| `BOT_HIDDEN_WINDOW` | `1` for hidden-window/null-renderer test runs |

The script's `BOT_COUNT` and `BOT_SKILL` override the saved configuration. For a
normal direct launch, persistent settings live in the save root's `config.toml`.
For an automatically started offline Rat Race match, merge these sections into
an **isolated test save root's** config (do not replace your normal config):

```toml
[bots]
count = 7
skill = "spartan"

[network]
online = false
public_lobby = false
host_public = false
list_hosted_games = false
report_joined_games = false
report_events = false
allow_upnp = false

[debug]
solo_game = true
network_test = "host:ratrace:team_slayer"
network_test_start = 5.0
network_test_loadout = "rifle_pistol"
bot_decisions = true
exit_after = 120.0
```

Launch that config with the built executable (Xbox maps must be in the data
root's `maps/`):

```sh
HALO_DATA_ROOT=/absolute/path/to/test-data \
HALO_SAVE_ROOT=/absolute/path/to/test-saves build/macos/halo
```

Environment overrides take precedence; clear stale `HALO_BOTS`, `HALO_BOT_SKILL`
and `HALO_NETWORK_TEST` overrides when using the config example. Remove the
`network_test`/`solo_game` test settings when returning to normal play.

The script enables decision logging automatically. The script sets these game flags:

| Flag | Meaning |
| --- | --- |
| `HALO_DATA_ROOT`, `HALO_SAVE_ROOT` | Isolate assets/logs and settings/saves |
| `HALO_SOLO_GAME=1` | Private local-host fixture |
| `HALO_NETWORK_TEST=host:<map>:<game>` | Start the selected private match automatically |
| `HALO_NETWORK_TEST_START=5` | Startup countdown |
| `HALO_NETWORK_TEST_LOADOUT=rifle_pistol` | Assault Rifle + Magnum |
| `HALO_BOTS`, `HALO_BOT_SKILL` | Bot count and difficulty |
| `HALO_BOT_DECISIONS=1` | Utility decisions and behavior diagnostics |
| `HALO_EXIT_AFTER` | Timed shutdown |
| `HALO_NET_ONLINE`, `HALO_NET_PUBLIC_LOBBY`, `HALO_NET_HOST_PUBLIC` | Disabled |
| `HALO_NET_LIST_GAMES`, `HALO_NET_REPORT_GAMES`, `HALO_NET_REPORT_EVENTS`, `HALO_NET_ALLOW_UPNP` | Disabled |

The script sets `HALO_HIDDEN_WINDOW` and `HALO_NULL_RENDERER` only when
`BOT_HIDDEN_WINDOW=1`; otherwise it clears them. It always unsets
`HALO_NAV_PROBE`, `HALO_BOT_SANDBOX` and the pickup fixtures for normal runs.
For manual controlled tests, see [bots.md](bots.md): `HALO_BOT_SANDBOX=1`
with three bots exercises gunner boarding; `HALO_NETWORK_TEST_PICKUP=5`
and `HALO_NETWORK_TEST_PICKUP_WEAPON=sniper` exercise pickup.
`HALO_NAV_PROBE=1` overrides Bot 1 for geometry diagnostics, not normal AI;
`HALO_NAV_DUMP=bloodgulch.nav` dumps derived geometry. See [navigation.md](navigation.md).

## Check the match afterward

```sh
# Use the fresh match-*.log captured above; debug.txt may contain older runs.
python3 tools/check_bot_navigation_log.py /path/to/match.log --minimum-bots 24 --movement-only
python3 tools/check_bot_navigation_log.py /path/to/match.log --minimum-bots 24
python3 tools/check_bot_awareness_log.py /path/to/ratrace-4v4.log --require-pursuit
```

The full lane checker is for the 24-bot fixture: it requires both teams' lane
assignments, spread-out goals and successful patrol routes in at least two lanes
per team. It counts distinct assigned bots, not repeated assignment lines. The
seven-bot 4v4 fixture cannot populate all three lanes per team; use movement-only
there rather than treating that structural mismatch as a navigation failure.
Both modes require sufficient status samples and reject crash/assertion logs.
An on-foot bot remaining within 0.5 metres **horizontally** for three consecutive
same-life status samples fails; vertical jumping cannot hide a wall stall.
Respawns reset the stationary window. Vehicle samples are exempt (a passenger
may legitimately stay seated). This is sampled evidence, not a guarantee that every tick moves; route
construction logs do not prove arrival. Navigation failures remain failures,
even when movement recovery prevents idle bots.

The awareness checker scopes each claim to its team: it rejects duplicate
same-team pursuit until a give-up/release event, but permits opposing teams to
pursue the same world object. New pursuit and release logs carry an explicit
team; older logs use the bot's team assignment when available. FFA uses one
shared scope.

Other fixture checkers (use their corresponding three-bot runs, not this 24-bot log):

```sh
python3 tools/check_bot_team_slayer_log.py /path/to/three-bot.log
python3 tools/check_bot_opportunity_log.py gunner /path/to/gunner.log
python3 tools/check_bot_opportunity_log.py weapon /path/to/pickup.log
```

## Fresh movement/navigation baseline (2026-10-11)

The PDF comparison and next feature order are in
[bot-pdf-gap-analysis.md](bot-pdf-gap-analysis.md).

An isolated POSIX/macOS evidence runner (Python 3.11+) creates fresh data/saves,
symlinks existing legal maps, clears inherited Halo/test overrides, disables
human callouts for a navigation baseline, and runs hidden/null-renderer:

```sh
python3 tools/run_bot_validation.py --map ratrace --bots 7 --seconds 75
python3 tools/run_bot_validation.py --map bloodgulch --bots 24 --seconds 75
# Run movement-only on either printed game.log; the full lane check is for 24 bots.
python3 tools/check_bot_navigation_log.py /printed/run/game.log --minimum-bots 7 --movement-only
```

The runner records `game.log`, isolated `data/debug.txt`, `run.json` (binary hash,
source commit/dirty flag, settings, exit status), and `trace.json` when available.
Its exit is not a checker verdict. The watchdog allows five seconds for SIGTERM,
then SIGKILL, and acts only on its own process group. Keep these local artifacts;
do not commit proprietary map-derived navigation dumps or copy maps into Git.

Both runs used the current dirty tree on base commit `76648ffd`, Spartan bots,
Team Slayer, rifle/pistol loadout and human callouts disabled. Both exceeded the
75-second run deadline, needed watchdog SIGKILL after five seconds of shutdown
grace, and lasted about 80 wall seconds. No assertion/crash signature was found
before that deliberate termination. Natural timed shutdown remains unresolved.

Evidence roots under `build/macos/botrun/validation/`:

| Run | Evidence directory | Revised movement check | Navigation result |
| --- | --- | --- | --- |
| Rat Race, 7 bots | `ratrace-7bots-20261011T044611-089820Z` | Pass: every bot has 5–7 samples; no detected same-life horizontal stall | 299 published polygons; full three-lane check not applicable to this fixture |
| Blood Gulch, 24 bots | `bloodgulch-24bots-20261011T044739-391345Z` | Pass: every bot has 4–8 samples; no detected same-life horizontal stall | 2,807 polygons; **fail**: team 0 has only two patrol routes, both in lane 2 |

Search failure counts in captured stdout (patrol and tactical combined):

| Run | no-path | bad-goal | bad-start | stale |
| --- | ---: | ---: | ---: | ---: |
| Rat Race | 2,347 | 1,296 | 361 | 1 |
| Blood Gulch | 10,481 | 2,608 | 100 | 0 |

These passes are sampled evidence, not goal-arrival proof or validation of every
corner/stair/door. Frequent failures remain real even when recovery keeps bots
moving. No fresh walking-probe/arrival evidence has been collected yet.

The trace files are **not adequate performance evidence**: the final reports
retain only 4 Rat Race / 3 Blood Gulch AI ticks and report millions of lost trace
events in the unpaced null-renderer runs. Do not infer full-run averages or a
worst-case capsule-probe budget from these snapshots.

Next diagnosis should separate tactical endpoint projection (aim positions versus
ground-foot goals), static connectivity/capsule-clearance rejection, and transient
occupants/obstacle revisions. Reproduce these in a one-bot geometry probe before
changing routing; also establish actual patrol-arrival logging and recheck the
24-bot fixture. No production navigation policy was changed in this validation
pass. Checker regressions now cover jumping-in-place, respawn windows and crashes;
runner tests cover isolation, natural failure and owned-process watchdog cleanup.

## Navigation correctness pass (2026-10-12)

The failure lines now say why a search failed: `search failed: <status> kind
<kind> start (...) polygon P component C goal (...) polygon P component C
rejected-edges N`. Kinds: `bad-start`/`bad-goal` (endpoint does not project),
`disconnected` (endpoints in different static components; no search is run),
`clearance-start` (every exit from the start polygon refused), `clearance-goal`
(the way into the goal polygon refused), `clearance-edges` (refused between
other polygons). Collision refusal does not yet distinguish static geometry from
a transient occupant. Completed routes log `surface patrol|tactical arrived goal
(...) at (...) error E waypoints N`.

First diagnostic run (components and kinds only, before routing changes): on Blood
Gulch, 10,724 of 10,725 `no-path` were **tactical endpoint** refusals. 6,757 were
the goal segment (the pursuit goal is the enemy's own position: the sweep from
the goal polygon's center hit the enemy). Patrol goals contributed almost
nothing. These were 1,509 distinct bot/start/goal triples over 254 goal polygons,
not retries of a few goals. Changes then made (`docs/navigation.md`): the start
and goal polygons are traversed from/to the actual endpoints instead of their
centers, pursuit/pickup goals skip the segment occupied by the target, endpoint
clearance is checked once instead of on every search step, bots no longer
invalidate each other's routes through the global obstacle revision, a stuck
bot blocks the next polygon rather than its own, and lane/random goals are
restricted to the bot's component.

Same fixtures (`run_bot_validation.py`, 75 s, base `c95b2898` plus this change):

| Run | no-path | bad-goal | bad-start | disconnected | total | baseline total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Rat Race, 7 bots (`ratrace-7bots-20261012T011306-642968Z`) | 25 | 400 | 41 | 15 | 481 | 4,004 |
| Blood Gulch, 24 bots (`bloodgulch-24bots-20261012T011146-494008Z`) | 0 | 250 | 17 | 0 | 267 | 13,189 |

Movement-only checks pass on both (7 and 24 bots); no assertion/sanitizer
signature. Blood Gulch found 98 tactical routes (22 in the baseline). The full
lane check still fails (team 1: no patrol routes): in these runs bots spend the
match in fight/scavenge/retreat (roam scores a constant 0.15), so few patrols
start, and a long patrol search (4 expansions per tick over 2,807 polygons) is
often abandoned by a behaviour change before it completes. That is behaviour
selection and search budget, not route validity. Remaining `bad-goal`s are pursuit
targets with no walkable polygon within standing height below them (seen near the
Blood Gulch bases at z≈1; plausibly vehicles or non-walkable surfaces), still to
be confirmed.

## Route completion (2026-10-12, continued)

The combat runs above cannot show whether routes complete: bots spend the match
fighting, and roam scores a constant 0.15. `--navigation-only` sets
`debug.bot_navigation_only`: bots only patrol their lanes and hold fire. Every
run also writes the map's anchors to `data/map_anchors.json`.

```sh
python3 tools/run_bot_validation.py --map bloodgulch --bots 24 --seconds 120 --navigation-only
python3 tools/check_bot_navigation_log.py /printed/run/game.log --require-lane-arrivals
```

The checker prints what ended each route (arrived, stuck, blocked-ahead,
new-life, behavior, replanned, unfinished) and, with `--require-lane-arrivals`,
requires each team to complete a patrol in every lane. Route and failure lines
now carry the search time (`search N ticks`), and a missed waypoint deadline
logs `waypoint i/n not reached ... occupant-wait N ticks`.

Blood Gulch, 24 bots, navigation-only, 120 s, one change at a time (run
directories `build/macos/botrun/validation/bloodgulch-24bots-20261012<Run>-*`):

| Change | Routes | Arrived | Stuck | Run |
| --- | ---: | ---: | ---: | --- |
| A* and cached static search (bots holding fire) | 83 | 46 | 9 | `T012834` |
| + waypoint deadline (exposed the drift below) | 274 | 67 | 128 | `T013827` |
| + no escape drift while searching/waiting; start at waypoint 1; passed waypoints | 247 | 56 | 122 | `T014134` |
| + live check for vehicles and machines only | 219 | 126 | 46 | `T014608` |
| + escalating blocks; leaving a blocked start polygon | 232 | 129 | 55 | `T014906` |
| + lane axis between CTF flags (97.6 m instead of 22.6 m: longer routes) | 168 | 74 | 56 | `T015417` |
| + string pulling (about 10 points per route instead of about 73) | 170 | 98 | 29 | `T015855` |

The last run passes `--require-lane-arrivals` (both teams, all three lanes),
with no failed search; the other routes ended 20 blocked-ahead replans, 29
stuck replans and 23 unfinished at the end (one per bot). Arrival error is at
most 0.45 m. Search sweeps fell to about 100 per 10 s against about 10,000
cached verdicts; a search takes 19 ticks on average. A slope-weighted search
cost was tried (`T020316`: 160 routes, 96 arrived, 30 stuck) and reverted: no
measurable difference.

Rat Race, 7 bots, navigation-only (`ratrace-7bots-20261012T020113-219634Z`): 75
routes, 64 arrived, 4 stuck, no failed search.

Normal combat, same binary: Blood Gulch 24 bots (`T020539`) found 393 routes
with 19 failed searches (8 bad-goal, 10 bad-start, 1 clearance); Rat Race 7 bots
(`T020714`) found 179 with 98 failed (68 bad-goal, 12 bad-start, 18
disconnected). Movement checks pass on both. The Rat Race failures cluster at a
few goals: one spot near (23, -8.5, -1.0) that never projects, and two upper
areas in components separate from the main floor. Stairs are the likely cause,
unconfirmed: a tread narrower than the agent is never a route node, and no step
links exist. Blood Gulch's remaining stuck points are mostly waypoints 1-1.6 m
above the bot on short steep rises. Both need step/jump links.

The found cause of the drift: `bot_keep_moving` ran a bot at full speed in its
escape direction on every tick a leaf emitted no move, including every tick of a
route search and every wait for an occupant, so bots left their routes while
planning.

## Seam crossings and short ascents (2026-10-12)

Offline analysis of `walkable.nav` disproved the earlier Rat Race stair
hypothesis for the main floor: narrow BSP seam polygons connected the floor
regions but were rejected because their centers lacked boundary clearance.
Single-seam pass-through now joins two usable neighbors when the segment
between their portals stays a full radius from exterior edges. The corridor
retains that seam and query-time collision checks still test the crossing.
Endpoint projection also tries a nearby usable point within 1 m for goals at
walls/seams; it does not assert reachability through walls.

Short ascents are local, guarded jump attempts along an existing corridor, not
new graph connections across isolated floors. The biped must be grounded and
unstunned, the rise 0.3–1.6 m within 2.5 m horizontally, and a descending landing
must be supported. Its actual jump impulse/current momentum and CE's per-tick
gravity generate at most 60 candidate samples; each is capsule-swept before a
normal jump intent. A request is not proof of completion: grounded outcomes
log horizontal/vertical landing error. Generic gap/step/drop graph links and
multi-seam chains remain future work.

New runs (same local fixtures; all movement checks pass):

| Run | Routes | Arrived | Stuck replans | Search failures |
| --- | ---: | ---: | ---: | ---: |
| Rat Race, 7, patrol-only, 90 s (`T022827`) | 68 | 57 | 4 | 0 |
| Blood Gulch, 24, patrol-only, 120 s (`T023003`) | 164 | 99 | 31 | 0 |
| Rat Race, 7, combat, 75 s (`T023208`) | 151 | 2 | 5 | 0 |
| Blood Gulch, 24, patrol-only, 90 s, landing logs (`T023520`) | 127 | 74 | 18 | 1 |

The Blood Gulch runs pass `--require-lane-arrivals` for both teams/all lanes.
In the landing-log run, 116 jumps were requested, 98 grounded landings observed,
and 82 of those were within 0.45 m horizontally/0.35 m vertically of the target;
5 requests never became airborne. Remaining requests were interrupted or still
in flight. These runs do not prove all possible step/jump links safe and do not
show a clear aggregate arrival-rate improvement attributable to jumps alone.

ASan/UBSan surface-navigation and perception/steering tests pass; the latter now
explicitly checks that route-search and occupant waits do not emit escape moves.
The 28 movement/awareness/runner unit tests pass. `ninja macos` passes; this
configured tree still has no `android` target. Runs still need the owned-process
watchdog; timed shutdown remains unresolved. All map-derived dumps stay local.

## Shared route searches and checked string pulling (2026-10-12)

Route searches queue for four shared workspaces instead of one A* query per
bot (9.8 MB for 31 bots, now 1.27 MB in all), and found routes are
string-pulled with the search's collision checks so that a shortcut no longer
cuts through scenery the searched path avoided ([navigation.md](navigation.md),
"Routes" and "Scheduling"). Route lines now end `search N ticks, M slices`:
the ticks include the wait for a workspace, the slices are the search itself.
The 10-second status lines end `betrayals B, held fire H ticks, refused R
throws`, the last two counted since the bot's previous line.

Blood Gulch, 24 bots, 100 s wall (`run_bot_validation.py`, directories under
`build/macos/botrun/validation/`):

| Run | Routes | Arrived | Failed | Median waypoints | Worst wait at start |
| --- | ---: | ---: | ---: | ---: | ---: |
| Patrol-only, 4 x 16 expansions (`T143104`) | 126 | 65 | 0 | 9 | 284 ticks |
| Patrol-only, 4 x 32, experiment only (`T143335`) | 137 | 83 | 0 | 9 | 109 ticks |

Pulled routes stay as short as the unchecked pull's (about 10 points; the
searched routes have about 73), and a search takes the same ~16 slices as
before. The cost is queueing: when all 24 ask at once at the start, the last
waits about 9.5 s with 64 expansions per tick, 3.6 s with 128. The shipped
constants are 4 x 16; 32 per workspace (same memory) is one line in
`surface_navigation_jobs.h`. Search sweeps rose from about 100 to several
hundred per 10 s (about 9,400 in the first 10 s): the string-pull's capsule
sweeps, at most 256 per tick. Frame time was not measured.

Friendly fire, combat (Team Slayer, betrayals = the players' friendly-fire
kills in the network-test lines; "riding" when the betrayer was in a vehicle):

| Runs | Kills | Betrayals on foot | Riding |
| --- | ---: | ---: | ---: |
| 7 earlier runs, no gates (2026-10-11/12) | 19–47 each | 0–3 each, 10 in all | 2 in all |
| Gates, first version (`T143631`) | 21 | 1 | 4 |
| + fuse-aware blast (`T144246`) | 30 | 1 | 0 |
| + weapon-cone lane (`T144652`) | 31 | 0 | 2 |

In the last run the gates held fire for 2,035 bot-ticks (65 of 202 status
windows) and refused 52 throws; kills stayed in the earlier range. One run
each is not a measured reduction. The riding betrayals were drivers running
teammates over (bot 10, seat 0, in `T143631`): driving is not covered.

## Still unresolved

- Camouflage and final-steering regression tests pass under ASan/UBSan, and
  `ninja macos` builds. Fresh sampled movement evidence is recorded above, but
  controlled geometry acceptance still needs corners, narrow doors, ramps/stairs,
  crouching, moving occupants and recovery in Rat Race and Blood Gulch. The probe horizon is
  0.65 m; current velocity, ledges and step negotiation are not modeled by the
  steering selector, and stopping in a fully blocked pocket is intentional.
- Measure capsule-probe cost in a 24/31-bot match before extending the budget.
  Tune the close-range camouflage exception only after a deliberate design
  decision; it is currently disabled. Sneaking remains future work.
- Android validation was attempted, but this configured tree has no `android`
  Ninja target. Linux/Windows/Android builds remain to be checked; existing
  `configure_args = --bots` was preserved.
- The 85-second visible run at `build/macos/botrun/movement-terminal.log`
  passes sampled movement for all 24 bots, including previously stalled Bots 17
  and 18. This only checks positions at ten-second intervals; it does not prove
  physical progress on every tick. Movement recovery can issue input and change
  direction, but cannot guarantee progress against every collision or geometry.
- BSP search still reports `bad-start`, `bad-goal` and `no-path`. The lane checker
  fails: team 1 produced no successful patrol routes in that run. Movement
  recovery is not a BSP route repair; full lane coverage remains unverified.
- The previous Rat Race log predates claim-release logging. Re-run the
  awareness checker on a fresh match to validate handoffs after abandonment,
  death and bot exit.
- Aim-profile helper tests pass, but accuracy and difficulty differences have
  not been measured in a live match. Teleport reacquisition also needs a
  teleporter-map test.
- A vehicle entry was observed, but driver-to-teammate pickup and boarding has
  not been verified in the latest run. Vehicle obstacle avoidance and good
  driving remain unverified.
- FFA/Team Slayer are the ordinary modes. Standard two-flag CTF is opt-in and
  experimental; King, Oddball, Assault, single-flag and manual-reset CTF remain
  unsupported. No live CTF validation was run on this port. Android and other
  platform builds remain unchecked by this historical test procedure.

## Historical native-menu validation (2026-10-13)

The following PC/XML lobby results refer to the legacy implementation and do
not describe the current ChupathingyCE port, which has the feature-gated local
lobby overlay on the Xbox-style multiplayer list; native PC/XML lobby wiring
was not ported in this commit. `tools/check_bot_lobby_live.py` in the old tree
accepted `--menus pc` as well as `xbox`.
The PC run opens Multiplayer, navigates its real BOT MATCH entry, changes the
native XML rows with keyboard arrows, and starts a private match. It does not
open the overlay directly or inject a gameplay/network-test launch.

Both hidden-window Metal runs passed: Battle Creek loaded with one friendly
and two enemy ODST bots and the custom Assault Rifle/Magnum selection. Evidence:

- PC: `/private/tmp/bot-lobby-pc-checks/bot-lobby-e93lyw3z/`.
- Xbox regression: `/private/tmp/bot-lobby-xbox-checks/bot-lobby-_vjgc6f2/`.

The first PC attempts caught Metal's separate XML reader and pause-only widget
references to art absent from ui.map. Both were fixed before the passing run;
those early failures remain in the same artifact parent directory.

The legacy tree's `tools/test_bot_lobby.py` covered its shared model/launch path;
its XML widget/filter test is not part of this port. The current branch instead
has `tools/test_bot_lobby_menu.py` for the dynamic feature-gated row and
`tools/test_bot_lobby.py` for the lobby model.

`ninja macos` passed with bots enabled and disabled. A hidden five-second
no-bots PC-menu smoke loaded 926 widgets without XML fallback; optional bot
widgets were filtered. Those build and smoke results are historical and are not current-port
validation. The current port's macOS enabled/disabled builds passed; Android is
not a target in the current macOS-generated Ninja file. See
[the current objective scope](bot-objective-modes.md).
