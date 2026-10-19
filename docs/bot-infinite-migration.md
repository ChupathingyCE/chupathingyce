# Infinite-style bots in C

## Deferred tuning — deliberately missing for now

**Infinite's Lua goal-scoring policy and numerical difficulty/weapon tag tuning
are missing.** Continue the C architecture/behavior migration without them.
All numerical defaults are explicitly CE-owned placeholders for later tuning;
missing data must not be presented as recovered behavior or block the other
implementation work.

## Reference and evidence limits

The migration reference is the local sibling checkout `../halo-rea/readable/`:

- `diagrams/01-architecture.png`: observed inputs, goal utility outputs,
  native combat evaluators, selector, ambitions and difficulty profiles.
- `diagrams/02-decision-loop.png`: selection, ambition eligibility and hysteresis.
- `diagrams/04-combat.png`: reaction memory, action cooldowns and melee scoring.
- `bots/DecisionLoop.cpp`: selector `0x142651fb0`, ambition gate
  `0x142c2f420`, choice tracking `0x142c2e7e4`, utility input/output split.
- `bots/BehaviorEvaluators.cpp`: acquisition `0x142c2e9a0`, melee
  `0x142747fe0`, grenade eligibility `0x142749074`, disengage `0x14274982c`.
- `bots/Behaviors.cpp`: ambition lookup/claim `0x142c29fa8`, team/bot
  eligibility, slot limits and replacing an existing claim.
- `bots/BotTypes.h`, `bots/DifficultyProfile.md`: recovered field readers.

These are readable reconstructions, **not compilable original source**. Their
README explicitly says the goal-scoring Lua and numerical difficulty tag data
are not recovered. The C implementation must distinguish recovered control
flow from CE-owned policy, tuning and adaptations. No new binary analysis or
Infinite runtime validation was performed for this change.

Do not copy offsets, opaque fields, engine IDs, unknown helpers or speculative
struct layouts into CE. Do not preserve apparent decompiler bugs: the readable
selector says its fallback loop may select the *last* eligible candidate, while
the diagram describes the best eligible runner-up. Our desired contract is the
highest-scoring eligible candidate, with deterministic ties and tests. The
recovered hysteresis array indexing is also explicitly unresolved.

## Target architecture

```text
CE perception + read-only mode state
                  |
          observed input snapshot
                  |
     C goal policy + combat evaluators <--- CE difficulty profile
                  |
            utility outputs
                  |
       selector + ambition eligibility
                  |
       chosen behavior init/update/cancel
                  |
       intent validation -> player_action
```

Game engines own scoring, flag lifecycle, damage and inventory. Bots only act
through normal player controls. Shared sightings remain observed, team-scoped,
expiring positions, not live hidden-target tracking. Navigation and weapon
safety remain CE modules; Infinite engine actions cannot be transplanted.

C replaces Lua at the policy seam, not with a Lua interpreter. Combat and goal
execution can be separate internally while callers still use a small decision
interface. Keep all state and work bounded for 31 bots; no hot-path allocation.

## First increment: scoring seam (implemented)

`source/features/bots/bot_decision.{h,c}` takes an immutable
`bot_decision_inputs` snapshot and initially wrote the six original behavior scores.
`bots.c` gathers engine observations, computes confidence and supplies the
snapshot. The policy cannot inspect objects, claim items, draw random numbers,
change a selector or execute actions. Existing utility cadence, cancellation,
intent validation, objective authority and ABS remain in place.

The current default tuning and formulas were moved, **not replaced with
Infinite values**. In particular, retreat is still our shield/confidence curve,
not Infinite's recovered fixed disengage score. Duties, offered rides, guard
combat, pickup commitment and shared-target caps retain their old priorities.
This establishes the input/output seam; it does not yet implement the complete
recovered hierarchy or its separate combat evaluators.

Verification for this increment:

- `python3 tools/test_bot_decision.py --sanitize`: production policy under
  strict C11 warnings, ASan and UBSan; target sources/distances, retreat gates,
  objective/ride precedence, guard combat, missing seat entrance and scavenging.
- `python3 -m pytest -q tools/test_bot_decision.py tools/test_bot_ctf.py
  tools/test_bot_perception_steering.py tools/test_features.py`: 14 passed
  (run with the existing pi-lens Python environment, which provides pytest).
- A temporary ASan/UBSan fixture compared the old `HEAD` scoring function with
  the extracted production policy and engine snapshot adapter: 20,000 seeded
  snapshots matched within `0.00001`. This was a refactor check, not live play.
- The edited production `bots.c` passed a native ABI syntax check using the
  existing macOS compilation flags and LP64 rewriter. LSP checked the new C
  files; `bots.c` exceeds its 5,000-line limit. No full game link or live match
  was performed.

## Profile ownership and reaction memory (implemented)

`bot_profile.{h,c}` owns the four CE profiles, including acquisition/visibility
delay ranges, reaction memory, pursuit memory and selector hysteresis. Previous
aim/movement/ability defaults are retained. New delay ranges and memory windows
are provisional CE values, not Infinite tag values. Pursuit memory remains
separate from reaction memory.

Each bot now has independently assigned base/active profile IDs.
`bots_set_player_difficulty` validates both IDs before mutation; changing them
resets reaction memory and requests a new decision, without altering health,
damage or ongoing action cooldowns. Reapplying the same override is a no-op.
Scenario skill overrides update already-admitted bots as well as new joins.

`bot_reaction.{h,c}` implements separate acquisition and visibility timers.
Recent reacquisition only applies to the same salted unit identity and cannot
shorten an outstanding timer. Shared/damage contacts do not count as having
seen a target. A new enemy, respawned enemy, expired memory, own new life or
rewound clock cannot inherit an old target's instant reaction. Firing and aim
tracking use this state; hidden contacts still cannot authorize shooting.

Cost is O(1) per bot/tick with no allocation; random delays are sampled only
on acquisition/visibility edges. Hysteresis is now read from the active profile
at each decision rather than one global selector configuration.

Verification:

- `tools/test_bot_profile_reaction.py --sanitize`: strict C11/ASan/UBSan tests
  for delay conversion, first sight, short occlusion, exact memory boundary,
  shared-to-visible contact, changed/respawned unit identity, same-unit
  reacquisition, zero memory, rewind, reset and deadline saturation.
- Production perception/reaction adapter tests additionally cover separate
  base/active lookup, public override validation/no-op behavior, sampled timer
  creation and brief/long visibility loss.
- 19 targeted pytest tests passed, including CTF, lobby, feature registration,
  perception/steering and friendly-fire regressions. Separate sanitizer runs
  of perception/steering and friendly-fire fixtures passed.
- Current bot/profile/reaction sources passed the native macOS ABI compiler
  flags using temporary LP64-rewritten headers. New modules passed active LSP
  checks. Header-only fallback findings about missing engine include paths
  were configuration false positives, not production compiler errors.
- No full game link or live match was performed for this increment.

## Native-style combat selection and action cooldowns (implemented core)

`bot_combat.{h,c}` evaluates fight, melee, grenade, disengage, seek and avoid
separately from goal utility. Disabled/playfighting/passenger exclusions are
represented as -2; unavailable actions as zero. The highest positive score
wins with deterministic ties, without allocation or mutation during scoring.

Recovered numerical evaluator constants are distinct from missing tag values:
fight 0.5 (2 in playfighting), grenade 0.85, disengage 0.7, seek 0.1, avoid 0.9;
melee uses the recovered range response, low-confidence/low-health/close-range
modifiers, advanced counter/vulnerable handling and empty-ammo/melee-weapon
floor. Melee outputs can exceed 1, unlike the current normalized goal policy.
All profile ranges, cooldowns and thresholds remain explicitly CE defaults.

The production adapter supplies visible target state only; remembered/shared
targets do not expose live vitality or authorize attacks. Actions still need
reaction readiness, fire range, ordinary turn speed/alignment, capability,
inventory, seat and busy-state gates. A chosen melee outside CE control reach
approaches through normal movement rather than inventing an Infinite lunge.
The fight leaf backs away for a native disengage choice; goal movement remains
independent, so ABS cannot drag a delivering carrier off its route.

Full melee/grenade cooldowns start only on observed animation falling edges.
Observation runs before scenario/goal early returns, including with no enemy.
Submitting an input gets a short retry gate; a refused throw gets a bounded
one-second retry, without shortening any real cooldown. Death/respawn and
rewound simulation reset state. A requested or rejected intent is not evidence
that the game performed an action.

`bots.playfighting` excludes melee/grenade/disengage, not damage. The
`bots.combat_fight`, `bots.combat_melee`, `bots.combat_grenade` and
`bots.combat_disengage` switches control those evaluators (all enabled by
default). Weapon fire/charge and teammate safety gates remain authoritative;
disengaging cannot bypass a disabled firing evaluator.

Verification: `tools/test_bot_combat.py --sanitize` covers production evaluator
formulas, profile base/active ownership, exclusions, hidden-target gates,
request-versus-completion lifecycle, retry preservation, rewind and saturated
deadlines. Its fake-world adapter fixture runs the actual `bots.c` combat
input, animation observation, aim and intent-emission bodies. Perception/
submission tests cover short retries; friendly-fire tests verify refused
throws leave full cooldowns untouched. Production bot/profile/combat sources
also passed native macOS ABI syntax checks. No live match was performed.

The trajectory/danger increment below adds the sampled ballistic gate and
observed avoidance supplier/execution. CE has no Infinite throw-weapon control
or exact player lunge action; these are not faked with engine state changes.

## Per-weapon profile rows (implemented)

The profile module stores up to 32 copied rows per difficulty, keyed by full
salted CE weapon-definition handles. C policy can install rows with
`bot_profile_set_weapon_attributes` after map initialization. Replacement is
atomic after validation of every key, value, range and duplicate. Map changes
clear rows so an old tag identity cannot affect a new map.

**There are deliberately no guessed Infinite weapon rows.** Unknown keys use
neutral desirability/aim factors (1), unspecified preferred range (0), swap
allowed, and no melee/throwable flags. Attribute numbering and the seven flag
slots follow the reconstruction; the three aim factors' interpretation as
error/settle/moving-error scales is explicitly our CE adaptation.

Production consumers use base difficulty rows for pickup preference, automatic
weapon switching, melee-weapon scoring, aim error/settling/movement penalties,
and preferred engagement bands. Raw ammunition usability is kept separate from
desirability. Preferred range cannot exceed the skill's fire-range gate, aim
retains an error floor, carried flags still cannot auto-switch or fire, and
driver/gunner lookup uses the same vehicle weapon unit as action submission.
A throwable flag is retained as data only: CE has no throw-weapon action.

Strict profile tests cover bounded rows, full salted keys, isolation between
difficulties, neutral fallback, invalid/duplicate atomic rejection and map reset.
Actual combat/weapon adapter tests verify preference, swap exclusion, empty-slot
acquisition, vehicle weapon lookup, range clamping and all three aim factors.

## Ambition eligibility and claims (implemented)

`bot_ambitions.{h,c}` owns a bounded 16-rule publication and one assignment per
bot (31 maximum), without allocation or world reads. Identity is an authored
kind/team/object/carrier/source tuple, not an array index or position hash.
Publishing a reordered table retains claims; removal, changed salt/carrier,
eligibility changes, expiry and clock rewind invalidate them. Slot reductions
retain lower bot numbers deterministically.

Selection reads caller-owned utility/distance scores, filters team/bot/player
eligibility and slot availability, and claims only the best eligible candidate.
It never claims rejected runners-up or accidentally chooses the last eligible
one. Renewing one's current assignment consumes no extra slot; replacing it is
atomic. Cancellation can release only an expected identity, so a stale old
behavior cannot drop a replacement chosen earlier in the tick.

The manager now publishes these rules for every CTF duty. Take/return retain
one slot; other duties retain their existing unlimited assignment policy for
now. Deliver is restricted to the actual carrier. Removed/disabled mode
publications invalidate claims. Execution and viability require a live claim;
cancellation, failed routes, death and departure release it. Item/seat claims
remain independent, team-scoped and never block humans; departure cleanup now
also releases the bot's old team scope if its team changed/player disappeared.

`tools/test_bot_ambitions.py --sanitize` checks runner-up ranking, bounded
capacity, renewal/replacement, stale cancellation, salt/area identity,
publication reordering/removal, team/include/exclude/specific-player gates,
slot shrinking, invalid atomic requests, nearest ties, expiry, rewind and
saturated leases. The production CTF manager/execution fixtures additionally
exercise new-goal preservation and all-scope cleanup. At this increment,
22 targeted pytest tests passed, and bot/manager/profile/ambition sources
passed native macOS ABI syntax checks. No full link or live match was run.

Work is bounded by 16 candidates and 31 owners; weapon lookup by 32 rows.
These are explicit resource ceilings, not a measured live 31-bot performance
claim.

## Distinct goals and ballistic/danger adapters (implemented)

Goal selection now has separate take/deliver/return/guard/hunt/hold/reach/hide,
acquire-weapon/acquire-equipment/enter-vehicle and avoid identities. Shared
navigation/pickup primitives remain shared, not duplicated implementations.
Viability checks match the selected identity; objective cancellation releases
the executed claim. Hide reaches an authored point, crouches and suppresses
ABS. CTF remains the only objective-mode publisher; a callable goal is not a
claim of King/Oddball or authored hide-point support. Legacy scavenge telemetry
aggregates its three acquisition leaves.

`bot_ballistics.{h,c}` solves a bounded low arc in constant air gravity.
`bot_grenade.{h,c}` reads CE unit throw speed, projectile gravity/collision
radius and player release offset. Two fixed aim/origin passes account for that
offset. Planning and final actual-facing validation each use at most 16 capsule
chords with the projectile collision mask. Radius inflation includes the maximum
parabolic sag between samples. Flight is capped at 120 ticks; excessive,
nonfinite, unreachable, guided, accelerating or underwater inputs fail closed.
Capsule radii, coordinate/speed limits, a 1.5-unit landing tolerance and all sensor
budgets are CE defaults, not recovered Infinite tuning.

Only a selected, otherwise eligible grenade pays for planning. A rejected plan
re-evaluates combat without grenade eligibility. Submission rechecks the arc
using the facing actually submitted after ordinary turn limits; its predicted
landing must also pass self/predicted-teammate blast checks. Existing lane, wall,
reaction, capability, inventory, carrier and cooldown gates remain in place.
Accepted controls hold the planned aim through observed throw wind-up, including
lost sight/goal movement, until animation completion. A refused proposal never
arms that hold; an ignored request expires its short hold. Death/rewind reset it.

Danger observation examines at most 256 grenade/vehicle candidates and performs
at most four LOS tests per scan, every four ticks, with half-second remembered
observations. It recognizes the map's actual grenade projectile tags, ignores
grenades still held in inventory, and uses world origins for released sticky
attachments. Only FOV/LOS or close-range LOS observations can authorize escape;
a hidden projectile scan cannot produce an enemy sighting. Approaching moving
vehicles use a bounded six-tick linear prediction; stationary/receding vehicles
remain navigation obstacles. Fast threats are sidestepped; stationary grenades
are fled radially. Normal movement intents still pass final collision steering.

Recovered avoid utility is 0.9. While danger is observed, CE's parent fight score
is capped at native fight's 0.5 so the old high-confidence score cannot stall
avoidance. This is explicitly CE policy, not recovered Lua. Higher-valued
objectives/rides/pickups retain their priorities; this is not an unconditional
emergency override or permission to pull a carrier off its route.
`bots.combat_avoid` defaults true; navigation-only tests and seated actors do
not run the sensor or avoidance movement.

Limits: no bounce/fuse/blast-tag simulation, future teammate turns, guided or
water trajectories, Infinite safe-zone supplier, or guarantee of detecting every
threat under bounded sensor load. Terminal target contact is permitted; this is
not a simulation of its later ricochet. Conservative refusals are preferable to
invented physics or direct state changes. No live grenade/avoidance match or
performance/parity acceptance has been run.

Verification: 68 bot/feature/log/engine-AI pytest tests passed. New
`tools/test_bot_grenade.py` runs the production solver and complete CE adapter
bodies under ASan/UBSan, including 20,000 randomized solver attempts, release
offsets, actual-facing alignment, walls/terminal contacts, unsupported physics,
FOV/occlusion, held/sticky grenades, moving vehicles and work ceilings.
Existing production-body fixtures additionally cover fallback combat, wind-up
aim hold, final landing blast/refusal, observation cadence/memory/rewind, and
score -> selector -> avoid intent execution/expiry. Perception/steering and
friendly-fire sanitizer runs passed. Native ABI syntax checks and active LSP
probes supplement these fixtures; they do not establish live gameplay parity.

## Build acceptance (performed)

On a clean worktree of `HEAD` plus only this migration's bot files,
`python3 configure.py --bots && ninja macos` compiled, linked and bundled the
macOS game. There, 66 bot, feature and engine-AI pytest tests passed, together
with sanitizer runs of the perception/steering, friendly-fire and combat
fixtures. This is build acceptance, not gameplay acceptance.

## Remaining validation

1. **Gameplay acceptance.** Run asset-free regressions first, then isolated,
   hidden-window navigation and match scenarios described in
   [bot-testing.md](bot-testing.md). Demonstrate actual arrival, carrier
   discipline despite shared contacts, claim limits, reaction differences and
   action cooldowns. Helper tests are not proof of live gameplay parity.

Network replication/backfill and additional modes remain separate work. This
migration must not silently enable unsupported games or change the network
protocol. Existing limitations are in [bots.md](bots.md) and
[bot-objective-modes.md](bot-objective-modes.md).
