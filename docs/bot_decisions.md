# Bot decisions: Halo Infinite's design in Halo CE

This documents the design of the offline multiplayer bots in
`source/features/bots/`, built with `configure.py --bots`. It follows Brie
Chin-Deyerle's GDC 2022 talk *Thinking Like Players: How Halo Infinite's Bots
Make Decisions* (slides in `Thinking_Like_Players_Markdown_and_Images/`), mapped
onto this code base. Halo Infinite keeps its game-mode rules and its utility
tuning in Lua; Halo CE's game modes are C (`source/game/game_engine_*.c`), so
everything here is C, with the tuning kept in tables so that it reads like data.

[docs/bots.md](bots.md) describes the currently ported offline scope and the
asset-free tests. No live match or network session was run while porting this
branch; historical local-run notes are not current-branch validation.

## The talk, and where each part lives here

| Talk | Halo Infinite | Here |
| --- | --- | --- |
| Verbs → behaviors (slides 10–15) | Acquire and fight, interact, pick up and deliver, hide and hunt, guard | Slayer leaves in `bots.c`: `fight`, `retreat`, `scavenge`, `vehicle`, `roam`; opt-in standard CTF also uses take, deliver, return and guard duties. King and Oddball are not supported |
| Game rules in Lua, the authority (17) | Lua writes, BotManager stores | Each game engine's adapter in `bot_manager.c` reads the game engine's state and writes the blackboard. The game engine stays the authority: bots never change scores or objects except as a player does |
| BotManager blackboard (17) | Ambitions plus some bot state | `bot_manager.c`: ambitions, and each team's shared sightings of enemies |
| Ambitions (18–19) | An object or area that scores: type, object, teams allowed | `struct bot_ambition`: kind, object, position and radius, the teams allowed (a mask), the owning team |
| Utility system (21–23) | Every behavior scored, the highest runs | `source/engine_ai/utility.{h,c}`: the selector; `bots.c`: one scoring function per behavior |
| Tuning knobs (26–28) | Fixed and functional values, caps, weighted inputs | `engine_ai_utility_curve` (fixed, linear, power, logistic responses), `engine_ai_utility_weighted`, and caps: `struct bot_utility_tuning` in `bots.c`, one table, so a game mode can have its own |
| Confidence (27) | Bot health, target health, weapons, edge cases, in Lua | `bot_confidence()`: shields and health against the target's, weapon against weapon, enemies seen against teammates near |
| Reaction-time budget (31–32) | 200–250 ms per decision, 8 bots, 31.25 ms each | A decision every 8 ticks (267 ms at 30 ticks a second), staggered by slot so that 31 bots spread their scoring evenly. The behavior still runs every tick; only the choice waits. A behavior that can no longer run is replaced at once |
| Hysteresis (34–35) | Inflate the new behavior's utility for a while | The selector adds a bonus to the running behavior that fades over a second, and a newcomer must beat it by a margin |
| Always Be Shooting (36–37) | Combat is not a behavior the bot must choose | Aim and movement are independent; a distant sighting selects behavior and closes distance, but the skill's fire range gates shots. `engine_ai/aim.{h,c}` scales a nonzero error cone by range, time tracking and movement |
| Difficulty and aim | Skill tuning makes harder bots better, not perfect | `bot_skills[]`: recruits turn poorly while moving and drift clumsily; higher skills strafe better, and Spartans crouch-strafe. Sight and fire ranges are separate. Aim settles but retains an error floor |
| Weapon/powerup awareness | A player remembers visible map pickups | `bot_item_known()` requires sight/FOV or arm's reach and keeps up to eight last-seen locations for 30 seconds. A bounded LOS budget avoids omniscient map scans |
| Pickup claims | Teammates do not all choose the same pickup | `engine_ai/claims.{h,c}` and the host-local `bot_manager` allow one owner per item within each team; opposing teams have independent scopes and can pursue the same item. Claims expire/release on abandonment. Humans are never blocked from racing a bot |
| Teleporter sightings | React to a target that reappears far away | A visible jump over 6 m in two ticks is logged as a teleport observation. The normal perception/utility path decides chase or retreat; no invisible destination read or teleporter route is used |
| Shared awareness (40–43) | Bots of some difficulties share enemies they have seen | Spartan bots call out only enemies they actually see; each team's last-seen position is stored on the host-local blackboard and read by teammates without a target |
| Awareness overload (41–43) | Shared targets pulled bots off objectives | Shared contacts are bounded by age, distance and confidence. CTF has objective-specific duties, but their live behavior has not been validated on this port |
| Behavioral simplicity (44–47) | Simple "stand in the zone" beat clever positioning | The opt-in CTF implementation uses bounded take, return, deliver and guard duties; King/Oddball hold/survive behaviors are not implemented |
| Test and validate early (49) | | The selector's standalone test (`tools/test_engine_ai_utility.py`); the decision log (`debug.bot_decisions`); log checkers (`tools/check_bot_*`) |

Only Spartan difficulty currently shares contacts. A callout contains an enemy's
last observed position, not live tracking or hidden information. It is logged
once per new contact, meaningful movement, or four seconds; the blackboard's
freshness window expires it. There is no audible dialogue or HUD radio message.
Pickup memory follows the same rule: bots know only items they can see or have
seen recently, not a hidden inventory of every map spawn. Claims coordinate
teammates only; opposing teams have independent claims and callouts. Claims do
not reserve an item against a player.

## Decisions

Each bot keeps:

- the **selector** (`struct engine_ai_utility_selector`): the running behavior,
  when it was chosen, and when the next decision is due;
- the **scores** of its last decision, for the log.

Every tick, `bot_think` perceives (its own sight; then, with no target, its
team's sightings), then:

1. if a decision is due, or the running behavior's score has fallen to zero,
   it scores every behavior (`bot_score_behaviors`) and asks the selector;
2. if the choice changed, the behavior tree is cancelled, so the running leaf
   lets go (its route, its retreat);
3. the tree runs: its priority root's leaves fail at once unless they are the
   chosen one, so the tree descends straight to it. The tree keeps doing what it
   did before: it gives each leaf an intent emitter and rolls back a faulted
   branch's intents;
4. ABS: if the leaf does not engage by itself and an enemy is in sight, the bot
   aims and fires at it. A leaf that must face something (a seat's entrance, a
   weapon on the ground at arm's length) suppresses this;
5. the intents become the player's action, as before.

A behavior's score is 0 when it cannot run (fight with no target, vehicle on
foot), so the selector never picks it.

### Scores

Values are 0 to 1. The table in `bots.c` (`bot_utility_tuning`) holds them:

| Behavior | Score |
| --- | --- |
| `vehicle` | Fixed, while seated (a seated bot drives, guns or rides; leaving is the leaf's choice) |
| `retreat` | `(1 − confidence) × shields-down response`, for skills that retreat, out of cooldown; capped |
| `fight` | By how the target is known: seen, hurt by it, remembered, or a teammate's sighting; times a distance response; raised by confidence. A teammate's sighting is capped low |
| `scavenge` | The opportunity's value (an empty slot, weapon upgrade, powerup or seat), times a distance response; a fixed high score within 2.5 m, regardless of current enemy visibility (ABS continues fighting) |
| `roam` | A lane-patrol floor: after contact or scavenging stops mattering, the bot routes to its assigned map-relative lane and alternates near/far patrol depths; two bots share a lane as a squad |

In opt-in CTF, objective duties are scored from the manager's bounded,
read-only ambitions: carriers deliver, bots may return a friendly dropped flag,
and teammates guard or search public stands. King and Oddball duties remain
future work. See [objective-mode scope](bot-objective-modes.md).

### Imperfect aim and perception

The aim module returns an error half-angle from skill-owned base error, accurate
range, settle time and moving penalty. Error grows with distance and movement,
shrinks as a visible target is tracked, and has a nonzero floor. `fire_range` is
separate from `sight_range`: a bot can notice and pursue a distant opponent
without shooting it. Movement remains an independent intent, so even a recruit
keeps a clumsy drift while aiming instead of freezing to shoot. Spartans
occasionally crouch during close strafes.

A last-seen weapon or powerup location lasts up to 30 seconds in a per-bot table
of eight items. A bounded line-of-sight budget limits new observations. The
host-local item-claim table grants a short exclusive claim to one bot at a time;
the owner renews it only while the opportunity is relevant. A human's pickup
still wins normally. If an enemy visibly reappears more than six metres from its
last observed position within two ticks, it is logged as a teleport observation;
perception updates through the ordinary sighting path, without reading a hidden
destination. Teleporter path planning remains future work.

### Confidence

```text
confidence = weighted(
    own vitality (shields count double),        weight 3
    1 − target vitality,                         weight 2
    own weapon ÷ (own + target's weapon),        weight 1
    teammates near ÷ (teammates + enemies seen),  weight 1)
```

It is computed once per decision. Retreat rises as confidence falls; fight
rises with it. The target's vitality is what a player can judge from shield
flashes and how the target took its last hits.

## Phases

### Phase 1: decision framework

- `engine_ai/utility`: responses, weighted inputs, the selector with cadence,
  hysteresis and a switching margin; standalone test.
- `engine_ai/aim`: bounded nonzero aim error scaled by distance, tracking time and
  movement; `engine_ai/claims`: expiring, exclusive item claims. Both have
  asset-free sanitizer tests.
- `bots.c`: the priority tree's choice replaced by utility scores; confidence;
  decisions every 8 ticks, staggered; ABS for every leaf; the decision log.
- `bot_manager.c`: the host-local blackboard, bounded ambitions and shared
  awareness. Spartan bots share sightings; objective duties stay game-owned.
- `bot_navigation.c`: collision-checked BSP routes for patrol, scavenging and
  pursuit; map-relative three-lane assignments keep squads spread. Moving
  targets replan after 12 metres rather than restarting on small motion.

### Phase 2: opt-in standard CTF

- The CTF adapter copies only validated public flag/stand state and the rules
  needed for objective selection. It does not mutate engine state or expose an
  enemy carrier's hidden position.
- Bots use ordinary navigation and player actions for take, return, deliver and
  guard/search duties. A return completes when the engine reports the flag home;
  it does not require the bot to acquire a weapon.
- Asset-free tests cover adapter boundaries, lifecycle, scoring gates, claims,
  execution and arrival. The current port has not been live-match validated.
- King, Oddball, Assault, single-flag and manual-reset CTF remain unsupported.

### Phase 3: team play

- The first lane squads and host-local enemy callouts now exist. Next: roles
  from the blackboard, such as how many teammates already pursue an ambition
  (one guard, the rest attack), written by each bot when it chooses.
- Teammate-aware spawning is left to the game, as now.

### Phase 4: the network

Bots replicated to other machines (they are host-only now). Out of this plan's
scope; it needs `HALO_PORT_NETWORK_VERSION` and the netcode's rules
(`port/linux/NETCODE.md`).

## Testing

- `python3 tools/test_engine_ai_utility.py`: the selector and the responses,
  without Halo.
- `debug.bot_decisions` (`HALO_BOT_DECISIONS=1`) logs every change of behavior
  with all its scores and the bot's confidence:
  `bots: bot 2 fight -> retreat (conf 0.21) vehicle 0.00 retreat 0.62 ...`.
- Standalone C-backed engine-AI tests and asset-free bot integration tests; see
  [the offline bot guide](bots.md). Prior local-run notes in
  [bot-testing.md](bot-testing.md) are historical and do not validate this port.
