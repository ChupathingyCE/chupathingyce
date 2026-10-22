# Bomb: the cut game type, and Grifball

Status (October 14, 2026): built (branch `grifball`). Bomb is the game type
Bungie cut from Halo, finished here. Grifball and Open Pitch are presets of
it, with player traits. Nothing here changes the game protocol OpenCE
defines: Bomb is played only between Chupathingy machines that agree on
Delta Peer's `bomb` capability (see "Delta only"). Every standard game type
plays exactly as before, with anyone.

## What Bungie left

Nothing but names:

- `source/game/game_engine_bomb.c`, `game_engine_defend.c` and
  `game_engine_terminator.c` were empty ("no symbols in this file").
- `game_engine_terminator` was 6 in `enum game_engine_type`, and
  `game_engines[]` put `stub_engine` in that slot.
- The terminator survived as Oddball's Juggernaut ball type
  (`_oddball_terminator`), not as an engine.
- No string, sound, HUD message or tag mentions a bomb.

So Bomb is built new, from parts the shipped engines already have:

| Bomb needs | Taken from |
| --- | --- |
| carry an object to a base and score | CTF: a carrier within 1.0 world unit of the base scores |
| a neutral object mid-map | Oddball: the map's ball, placed halfway between the goals (`bomb_find_spawn`) |
| the goals | the map's `_netgame_flag_ctf_flag` markers of team 0 and 1 |
| spawns | CTF's: `match_game_type` treats Bomb as CTF, so every starting location and item marked for CTF is Bomb's too |
| melee only | the port's custom loadout (`_loadout_weapon_none`, `no_map_weapons`) |
| waypoints | `game_engine_set_goal_position` with the nav points `ball_blue`, `default`, `default_red`, `target_red` and `target_blue` |
| the goals' outlines | King of the Hill's hill: the map's hill shader, drawn as `king_engine_post_rasterize` draws it |
| the round's clock and score | the script timer's HUD digits and colors (`hud_render_timer`), the teams in their waypoint arrows' colors |
| the arming meter | the player's own unit's shield meter and its frame, drawn again in the middle |
| the carrier's line | the HUD's state message (`game_engine_state_message`), as CTF's and Oddball's carriers have |
| the clients' state | the game type's state in the distributed netcode (`game_engine_write_network_state`) |
| sounds | the team score sounds, "play ball", the countdown timer and its end tone, the hill's move tone, the "ting" where the map's globals have it (no announcer line of another game type's) |

## The rules

Two teams. One bomb (the map's oddball ball) spawns on the ground halfway
between the goals, so neither team's run to it is longer. A map's oddball
spawn flags are scattered for Oddball: Blood Gulch's first, where earlier
builds put the bomb, is 68.4 world units from red's goal and 33.0 from
blue's. The ground is the first floor straight down over the point, found
in the map's structure as the map loads. A map can have its own point
instead: Blood Gulch's is built in (below), and the host's
`game.bomb_spawns` setting (`HALO_BOMB_SPAWNS`) names others as
`"<map> <x> <y> [<z>]"`, comma separated, the map as a playlist names it.
With a height, the ground is looked for from just over it. Without ground
there, the bomb goes to the map's first oddball spawn flag, as before. The
host alone places the bomb, so only its setting counts. Either team picks it up by
running over it and carries it into the other team's goal (its CTF flag
stand). A goal scores a round. The first team to the score limit wins.

- **The goals.** Red defends the team 0 CTF marker and scores at team 1's.
  Each team sees a waypoint on the goal it attacks, in the goal's team's
  color, and everyone sees one on the bomb while it lies loose. A carried
  bomb's waypoint is its carrier: a target to the other team, and one to
  defend to the carrier's own. Each goal is outlined as King of the Hill's
  hill is, a circle of the goal's radius (a goal that is a volume will give
  its own outline: `bomb_goal_outline`).
- **Arming.** The carrier stands within 1.0 world unit of the goal for the
  variant's arm time. An arm time of 0 scores on contact (Grifball). The
  HUD fills a meter as it arms, with a countdown beep each second. Leaving the goal or dropping the bomb starts
  it over.
- **The save.** Armed, the bomb is planted in the goal and arms for the
  host's `game.bomb_arm_time` (`HALO_BOMB_ARM_TIME`, 1 second by default,
  up to 10; 0 scores the moment it is planted). No one can pick it up
  meanwhile. A living defender who comes within 0.6 world units of it
  saves it: it resets to its spawn with "bomb reset", and no one scores.
  Else it scores once, as below. Only the host decides it: other
  machines see the bomb lie in the goal, then the goal or the reset.
- **A goal** gives the team a round at once and plants the bomb in the
  goal: out of the carrier's hands, and no one can pick it up. The round
  clock stops. A 3, 2, 1 countdown runs in the middle of the screen with
  the countdown beeps, and everyone is free to run or fight. Then the bomb
  goes off: everyone within 5 world units (about 15 meters, a Blood Gulch
  base and around it) dies and the bodies are thrown out and up. It's
  drawn as a cluster and two rings of the map's grenade explosions (the
  globals' frag and plasma), with their sounds and none of their damage, on
  every machine. The other team's dead are the scorer's kills; the scoring
  team's own (the carrier too) count nothing: no death, suicide, betrayal,
  message or respawn penalty.
- **After the blast**, 2.5 seconds on, with "new round on score" (every
  preset): every player is sent back to one of their team's starting
  locations nearest their own goal, whole again (no death is counted), the
  dead respawning there, the bomb is back at its spawn, and the next round
  starts with "Round N" and "Play ball". The host's state carries the
  countdown and pause (`hold_ticks`, a spare byte of the game state, which
  earlier builds ignore). A winning goal plays the same countdown and
  blast before the game ends.
- **Round time.** A round that runs out ends with no score: a new round
  starts the same way. The last 5 seconds tick aloud, and the clock
  flashes; the end tone sounds at 0.
- **Untouched reset.** A bomb on the ground for the reset time (30 seconds)
  goes back to its spawn.
- **The carrier** can't use active camouflage, and its armor pulses gold
  and white on every machine. It has the carrier's traits (below).
- **Spawns.** Team starting locations, the ones nearest the team's own goal
  rated highest.
- **Changing teams** (the pause menu's CHANGE TEAMS, `sv_team`:
  [delta.md](delta.md), Team changes) waits for the player's next death or
  the next round, whichever comes first: at the round's start the host
  kills a waiting player's unit as nobody's kill (nothing counted) and they
  respawn at once at the new team's end. A carrier carries for their own
  team until then. No change leaves a team without players.
- **Statistics.** A player's goals and carries are kept in
  `ctf_statistics` (`flag_scores`, `flag_grabs`), so the carnage report
  works as it is. The report gives them under Bomb's own names (see
  "Reports").

### The HUD

All of it from the game's own HUD pieces, drawn from the state every
machine has from the host (split screen and any shape of screen too):

- At the top middle, the round's clock in the script timer's digits and
  colors (minutes and seconds, flashing in its last 5 seconds), with each
  team's rounds beside it in the team's waypoint color, red's left and
  blue's right. A variant without a round time shows the rounds alone.
- The carrier's state message, as CTF's and Oddball's carriers have:
  "Take the bomb to the enemy goal", or "Arming the bomb" while arming,
  with the arming meter (the shield meter's likeness) above the crosshair.
- The game's messages: "You have the bomb!", "Player has the bomb." (to
  the carrier's team) or "Player has the bomb!" (to the other team), "You
  scored! 2 to 0", "The enemy scored. 0 to 2", "Round 3", "Time is up.
  Round 3", "The bomb was reset.". The stock "Picked up the odd ball" is
  hidden in Bomb games.
- A goal's countdown, "3", "2", "1", in the HUD's digits four times their
  size in the upper middle of the screen.
- The bottom-right score and the postgame report show "Rounds".
- The game type menus show Oddball's skull for Bomb's game types, and
  their own descriptions.

## The variant

Bomb takes slot 6, Bungie's cut terminator slot after Race:
`game_engine_bomb` in `enum game_engine_type`, and `game_engines[6]` is
`&bomb_engine`. Its rules are `struct bomb_variant` in
`union game_engine_variant` (still 0x18 bytes):

| Field | Meaning |
| --- | --- |
| `arm_time` | seconds in the goal to score; 0 scores on contact (0..60) |
| `reset_time` | seconds a dropped bomb lies before it resets; 0 never (0..600) |
| `round_time` | seconds a round lasts; 0 no limit (0..3600) |
| `flags` | `_bomb_new_round_on_score_bit`, `_bomb_melee_only_bit` |
| `traits` | the traits preset: 0 none, 1 Grifball, 2 Open Pitch |

`game_engine_variant_cleanup` pins the index to 1..6 and these fields to
their ranges in the builds with Delta Peer (`HALO_GAME_BROWSER`: every
desktop and Android build, and the dedicated server). A build without it
(Warthog) keeps 1..5, as before.

The built-in variants are added by name (`game_engine_get_variant_by_name`:
`bomb`, `grifball`, `grifball_open_pitch`). They are also written as
default gametypes 26 to 28 after the map's 26, so the menus' STANDARD list
shows **Bomb**, **Grifball** and **Open Pitch**. Their names are the port's
(`playlist_profile_default_name`), since the map's string list has 26.

A melee-only variant's PC options default to no starting weapons, no
weapons on the map, and no grenades or powerups (`game_variant_options_default`,
`game_engine_remap_equipment`). On a map with a player-usable energy sword
(a weapon tag named for a sword, with a first-person model and animations,
that doesn't vanish when dropped), every player spawns with it. Without
one (Blood Gulch), every player spawns with the globals' multiplayer pistol
as their fists: none of it is drawn, the first-person arms show only while
striking (the pistol whip, which reads as a punch), and in third person
players stand, run and swing empty-handed (the "unarmed" animations, with
the pistol's melee swing), it holds no rounds and can't fire, zoom or
reload, its HUD is the bare crosshair, and it is never left lying or picked
up (the host deletes one that is dropped, a dead player's too). Carrying
the bomb holsters it, as any second weapon. Every machine applies these
rules from the variant and the weapon's definition
(`game_engine_bomb_hides_weapon`). A unit with no weapon at all still
melees with its fists' blow and the pistol's third-person swing.

## Grifball

The official rules, adapted to Halo CE:

| Rule | Grifball here |
| --- | --- |
| Health | 10% (one hit kills) |
| Damage | 200% |
| Shields | none |
| Respawn | 3 seconds, at your own goal |
| Friendly fire | on |
| Motion tracker | on |
| The carrier | 150% speed, an overshield, 150% damage resistance (takes two thirds of the damage), a highlighted color |
| Rounds | 3 minutes; a goal ends the round; first to 3 rounds wins |
| Weapons | the map's player-usable energy sword if it has one; otherwise fists, an unseen pistol (melee only: no map weapons, no grenades, no powerups) |
| Gravity | 100% (the old 50% is gone) |
| Falls | a landing hurts no one (one hit kills, and a fall would be one). Falling for too long, a fall of more than 16.5 world units on Blood Gulch, still kills, as do the map's kill zones |
| Vehicles | none |

**The overshield.** Halo CE's overshield charges the shields to three times
full, which is Halo 3's double overshield. The carrier's is charged when it
picks the bomb up. While it carries the bomb, the shields charge past full
again whenever they have drained or been shot down to normal, once the
shields aren't stunned (`bomb_overshield_update`, the host's). A game
without shields gives the carrier its unit's shields back while it carries.

### Open Pitch

A second preset, for a big map: the [C] Grifball demo server plays it on
Blood Gulch.

| Rule | Open Pitch |
| --- | --- |
| Map | Blood Gulch: the bomb on the ground halfway between the two teleporter exits (63.31, -120.38: 50.7 world units from red's goal and 47.3 from blue's), a spot both teams reach faster through the teleporters; the goals at the CTF flag stands, the teleporters as they are |
| Speed | everyone 175% (the host's `game.bomb_open_pitch_speed`, `HALO_BOMB_OPEN_PITCH_SPEED`, from 50 to 200), the carrier 200% (115% on top, never past 200%) |
| The carrier | no overshield; 400% health on top of everyone's 10% and 400% damage resistance (the host's `game.bomb_open_pitch_carrier_health`, `_resistance` and `_shields`): a melee blow takes 93% of its health, so it lives through one and the second kills it. Its highlight stays |
| Fumble | a melee blow the carrier lives through knocks the bomb out of its hands (thrown a little ahead, as a drop is; the carrier can't take it straight back). The host's `game.bomb_open_pitch_fumble`, on by default |
| Vehicles | none |
| Rounds | 5 minutes, first to 3 |
| Everything else | Grifball's |

## Player traits

General settings that any game type could use. For now only Bomb game
types play by them, so nothing changes elsewhere:

| Trait | Range (percent) | Applied |
| --- | --- | --- |
| health | 10 to 400 | the damage a player takes is multiplied by 100/health (the host's damage) |
| damage | 25 to 400 | the damage a player deals |
| resistance | 25 to 400 | the damage a player takes is multiplied by 100/resistance |
| speed | 50 to 200 | `player->speed_multiplier` (each machine, for its prediction; the host's movement checks scale by it) |
| shields | unchanged, none, normal, overshield | `game_engine_has_shield`; the overshield as above |
| highlight | on or off | the carrier's color |

There are two sets: everyone's, and the bomb carrier's on top of it (each
percent multiplies, and speed is capped at 200%). The presets:

| | everyone | carrier |
| --- | --- | --- |
| Bomb | none | highlight |
| Grifball | health 10, damage 200, shields none | speed 150, resistance 150, overshield, highlight |
| Open Pitch | health 10, damage 200, speed 175, shields none | speed 115 (200 in all), resistance 150, overshield, highlight |

The traits can't go in the shared records: `game_variant` and
`game_variant_options` have no room, and OpenCE reads both. So the host
sends them over Delta Peer as **TRAITS** (below), to every client that
agreed to `bomb`, and a client plays by its host's. The host's traits come
from its variant's preset (`game_engine_traits_default`). Editing traits in
the menus, and saving them with a gametype (a `GPVT` block after `GPVO` in
`blam.lst`), is still to come.

## Delta only

An OpenCE client given engine 6 pins it to Race, shows "RACE", reads the
union as Race's, and refuses every game state the host sends. So no OpenCE
machine may play a Bomb game. The game protocol can't be changed to tell it
so.

- **The capability**: `bomb`, bit 11 in `enum delta_capability`, named in
  `delta_capability_names[]`. Every build with Delta Peer offers it. The
  signed legacy table's kill switch (`disabled_capabilities`) can turn it
  off; every other machine then lacks it.
- **TRAITS** (type 18, host to client, only with `bomb` agreed): the
  traits' kind (1), reserved (3), then everyone's and the carrier's sets,
  12 bytes each: health (2), damage (2), resistance (2), speed (2), shields
  (1), highlight (1), reserved (2). Kind 0 means "no traits" (the sets are
  zeros). A value past its range or unknown is refused, never mended. The
  host sends it when the traits change and every 5 seconds
  (`DELTA_PEER_TRAITS_INTERVAL`).
- **Who lacks it** (`delta_peer_machine_lacks`): a Delta machine that
  didn't agree to `bomb` lacks it at once; a machine that says nothing for
  the handshake's window (4 seconds since the host first saw it in the
  game) lacks it then (an OpenCE machine, one of another major, or one
  whose Delta can't reach the host). A host without Delta (`network.protocol
  = "opence"`, or its port taken) treats every other machine as lacking it.
- **The refusal** (`network_game_server_bomb_refusals`): while the hosted
  game is a Bomb game type, each machine that lacks `bomb` is dropped with
  the rejection code `_rejection_code_version_too_old` (it may join again,
  since the next game may be another). Every player gets a notice (in a
  game, the clients too; in the lobby, the host's console and log): "Player
  cannot play Grifball: it is played only between Chupathingy builds with
  Bomb". The dropped machine shows the stock "Failed to join the System
  Link game." That's all an OpenCE client can show.
- **The lobby** waits: its countdown holds while a machine is still in its
  window (`network_game_server_bomb_ready`; the log says "bomb
  handshakes").
- **A game in progress**: the host adds none of a joining machine's players
  until its window has ended, so a machine without `bomb` never loads the
  game. It's dropped without seeing it.
- **Our lists**: the Online Games list names engine 6 "Bomb". A game whose
  engine this build doesn't have (a newer build's game type) shows "New
  game type", A is greyed, and the footer says why, so our players never
  meet "Failed to join". OpenCE's lists show what OpenCE shows ("RACE").

Standard games are untouched: their wire, their joins and their play are
the same with or without any of this. The cross-play test
(`tools/crossplay_test.py`) shows both.

## The dedicated server

- Playlists name `bomb`, `grifball` and `grifball_open_pitch` as game types
  (`bloodgulch grifball_open_pitch`). `sv_map` takes them too.
- A game type file may start from them (`base = "grifball"`) and set
  `bomb.arm_time`, `bomb.reset_time`, `bomb.round_time` (seconds),
  `bomb.new_round_on_score`, `bomb.melee_only` and `bomb.traits`
  (`"none"`, `"grifball"`, `"open_pitch"`).
- `server/playlists/grifball.txt`: Open Pitch on Blood Gulch, with Grifball
  on Hang 'Em High and Chill Out between.
- The refusal, TRAITS and the reports are the game's own: the same code
  runs on the server.

## Reports

The end-of-game reports (`/v1/report`, the host's, and `/v1/client_report`,
a client's: `port/linux/game/game_stats.c`, `port/linux/src/browser.c`) and the
event stream (`/v1/events`: `port/linux/game/game_events.c`) describe a
Bomb game as follows. Every existing field keeps its key, type and
meaning: older site code reads the reports as before. These fields are
final for this version.

**The game's members** (both reports):

| Key | Type | Meaning |
| --- | --- | --- |
| `engine` | integer | 6 for Bomb (every preset). The host's report now carries it too; the client's had it already. 1 CTF, 2 Slayer, 3 Oddball, 4 King, 5 Race, as before |
| `gametype` | string | the variant's name as players see it, any game type: `"Bomb"`, `"Grifball"`, `"Open Pitch"`, `"Team Slayer"`, a saved gametype's own name (at most 11 characters; empty for a nameless built-in) |
| `bomb` | object | only in a Bomb game type |
| `bomb.preset` | string | `"bomb"`, `"grifball"` or `"open_pitch"`: the traits preset, which tells Grifball and Open Pitch from plain Bomb |
| `bomb.rounds` | integer | the round the game ended in (from 1) |
| `bomb.team_rounds` | [integer, integer] | rounds won by red and blue (the same as `team_scores`) |
| `bomb.round_time` | integer | seconds a round lasts (0: no limit) |
| `bomb.arm_time` | integer | seconds in the goal to score (0: on contact) |

`team_scores` holds the rounds won, as `score` holds a player's goals.

**Each player's line**, only in a Bomb game type, beside the existing
`flag_*` and `ball_*` fields (which stay 0 in a Bomb game):

| Key | Type | Range | Meaning |
| --- | --- | --- | --- |
| `bomb_goals` | integer | 0..32767 | goals scored (the same as `score`) |
| `bomb_carries` | integer | 0..32767 | times the player picked the bomb up |
| `bomb_carry_time` | integer | seconds, 0.. | time with the bomb |
| `bomb_carrier_kills` | integer | 0..32767 | enemy carriers killed |
| `bomb_kills_as_carrier` | integer | 0..32767 | kills made while carrying the bomb |

A host counts all of them. A client counts goals and carries from the host's
statistics, and carry time and kills from what its own game saw (the
carrier and every death are every machine's).

**The event stream**: the game's `engine` is 6, and `gametype` is the
variant's name, as above. Three new objective kinds follow the existing
eight (the numbers of the old ones are unchanged): `bomb_grab` (the bomb
picked up), `bomb_drop` (dropped, or its carrier killed), `bomb_score` (a
goal). Their `team` is the player's.

An example host report (Grifball, abridged to one line):

```json
{"invite": "57ee...", "teams": 1, "duration": 27, "team_scores": [3, 0],
 "players": [{"name": "Player", "team": 0, "place": 1, "score": 3, "kills": 0,
   "assists": 0, "deaths": 0, "betrayals": 0, "suicides": 0, "shots_fired": 0,
   "shots_hit": 0, "multikills": 0, "color": 1, "flag_grabs": 0,
   "flag_returns": 0, "flag_scores": 0, "ball_time": 0, "ball_carrier_kills": 0,
   "hill_time": 0, "laps": 0, "tag": "a5a6...", "joined": 0, "best_spree": 0,
   "medals": {}, "weapons": {}, "bomb_goals": 3, "bomb_carries": 3,
   "bomb_carry_time": 9, "bomb_carrier_kills": 0, "bomb_kills_as_carrier": 0}],
 "engine": 6, "gametype": "Grifball",
 "bomb": {"preset": "grifball", "rounds": 3, "team_rounds": [3, 0],
   "round_time": 180, "arm_time": 0},
 "stats": 1, "sources": {"statistics": "host", "team_scores": "host",
   "medals": "host", "weapons": "host", "departed": "host"}, "departed": []}
```

The game list's announcement still carries only `engine` (6), so the list
can say "Bomb" but not "Grifball". Telling them apart in the list needs a
`gametype` field in `/v1/announce` and `/v1/games.txt` (the site's side
first).

## Tests

- `tools/test_delta.py`: bit 11's name and the registry (the existing
  checks cover the new bit).
- `tools/test_delta_peer.py` (`port/linux/tests/delta_test.c`): TRAITS's
  layout, round trip and refusals (`test_wire_traits`); `test_bomb`: the
  machine that agreed, one that didn't (lacks it at once), a silent one
  (pending, then lacking after 4 seconds), a new machine at an old index
  (a new window), the room-wide check, TRAITS sent to the agreeing client
  alone and again after a lost datagram, strangers' TRAITS dropped, the
  kill switch. The random-input test reads TRAITS too.
- `network_test.c`: `debug.network_test_bomb` (`HALO_NETWORK_TEST_BOMB`)
  stands the last player on the bomb and, 3 seconds later, in the goal
  its team attacks, again every so often; every machine logs `network
  test: bomb: tick T red R blue B round N carrier C` each second.
  `debug.network_test_local_players` (2..4) plays split screen on the
  host. Run on macOS: split screen (2 players), and two copies over
  loopback (127.0.0.2 and .3): both logs agree on every score, round and
  carrier, tick for tick, to the end of the game.
- `debug.network_test_fall` (`HALO_NETWORK_TEST_FALL`) lifts the last
  player 10 world units every so often, a landing that kills: in a Bomb
  game it dies, in a Grifball game it lands unhurt (two copies over
  loopback, the joining copy's player). The host logs the bomb's spawn and
  its distance to each goal as the map loads (`bomb: host tick 0 spawn
  ...`).
- `debug.network_test_bomb_in_goal` and `debug.network_test_bomb_throw`
  (with `debug.network_test_bomb`): the host lays the bomb in the goal the
  joining player attacks, and that player holds the trigger as it stands
  on it, so the pickup leaves the bomb holstered and scores at once. One
  score a round, the blast, the next round, and the game's end at the
  score limit. Before the fix the bomb stayed in the carrier's inventory
  after the score (a debug build stops on it; a release build scored
  again every tick the carrier stood in the goal).
- `debug.network_test_bomb_save` (with `debug.network_test_bomb`): the
  host stands a defender on the bomb planted in its goal as it arms: it
  resets, no score. Without it the planted bomb scores once, a second
  after the plant; with `_in_goal` and `_throw` too, one score a goal.
- `debug.network_test_bomb_keep` with `debug.network_test_melee`: the
  carrier stays where it took the bomb and the host's player strikes it:
  it lives (7% of its health left), fumbles, and the striker takes the
  bomb.
- `tools/crossplay_test.py`: the `grifball` direction has our host play
  Grifball with an OpenCE client joining its lobby and another joining in
  progress. It checks that both are dropped and told, the late one never
  loads the game, and the host plays on. The `ours-host` direction is the
  same host build playing Slayer and CTF with OpenCE, unchanged.

## Courts

Any map with two CTF stands plays Bomb, and an oddball spawn puts the bomb
where the map wants it. Blood Gulch (Open Pitch), Hang 'Em High, Chill Out,
Prisoner and Wizard all work. A real Grifball court (a flat floor, walls, a
goal at each end) is a Custom Edition map, played as any is. Candidates
found (nothing downloaded or shipped):

| Map | Where | Notes |
| --- | --- | --- |
| `grifball.map`, "Grifball Beta" by SlappyThePirate (July 2011; reposted 2016), 23.1 MB | [haloce3.com](https://haloce3.com/downloads/multiplayer/original-multiplayer/grifball/) | A court with energy swords and gravity hammers as the players' weapons, played as CTF; the bomb carrier turns orange. Its sword is likely player-usable (the map is built around it), so our sword detection should hand it out; the gravity hammer is a new tag we don't give. It has its own scripts and scoreboard for its CTF Grifball, which may show beside ours. Whether HaloNet's locator has it under `grifball` is unchecked (a check is a download) |

A court the game knows (`bomb_courts` in `game_engine_bomb.c`, by its
file's version: grifball@ce so far) plays as its own scripts laid it out,
with the scripts off: the bomb starts where the map places its own ball,
which is not placed; each team's goal is the map's goal trigger volume
(grifball@ce's `redgoal` and `bluegoal`: the carrier scores standing in
the other team's, after the arming time), not the CTF markers, which are
off its court; and a player in its out of bounds volume (`OOB`) is
killed, the bomb lying in it reset.

Searches of halomaps.org found no other Custom Edition Grifball court.
Halo 4's and Halo 5's Grifball Courts (Erosion, Glacier) are those games'
Forge maps, not Halo CE maps.

## Open questions

- **A clearer refusal for OpenCE players.** The game protocol has no
  message for "this game type needs another build", so the dropped player
  sees "Failed to join". A host could show its game in OpenCE's browser
  as closed while it plays a Bomb game type (the advertisement's open
  flag), at the cost of looking full to them.
- **The gravity hammer.** It's a new tag. A court map that has one could
  hand it out as the sword is.
- **Editing.** Bomb's rules and the traits can't be edited in the menus
  yet (no options screen for engine 6, no `GPVT` block). The dedicated
  server's game type files can set them.
- **The list.** A `gametype` field in the announcement, so the list can
  tell Grifball from Bomb.
