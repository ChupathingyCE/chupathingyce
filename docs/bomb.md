# Bomb: the cut gametype, and Grifball

Status (October 10, 2026): designed, not built. This is the plan for
completing the bomb gametype Bungie cut from Halo, and for the general
player traits that let it play as Grifball. Nothing here changes the
game protocol OpenCE defines: Bomb is a Delta game type, played only
between ChupathingyCE machines (see "Delta only").

## What Bungie left

Nothing but names:

- `source/game/game_engine_bomb.c`, `game_engine_defend.c` and
  `game_engine_terminator.c` are empty ("no symbols in this file"). The
  build already compiles them (`tools/linux_build.py` globs the sources).
- `game_engine_terminator` is 6 in `enum game_engine_type`
  (`game_engine.h:41`); `game_engines[]` puts `stub_engine` in that slot
  (`game_engine_list.c:33`), and `stub_engine.type` is 7.
- The terminator survived as Oddball's Juggernaut ball type
  (`_oddball_terminator`, `terminator_scoring_rules`), not as an engine.
- The scenario's `_netgame_flag_vegas_bank` and `_netgame_flag_ctf_vehicle`
  are read nowhere.
- No string, sound, HUD message or tag mentions a bomb.

So Bomb is built new, almost entirely from parts the shipped engines
already have:

| Bomb needs | Already in the engine |
|---|---|
| carry an object to a base and score | CTF: a carrier within 1.0 world unit of the base scores (`ctf_engine_player_update`), drops it, and it resets |
| a neutral object mid-map | Oddball: the ball at `_netgame_flag_oddball_ball_spawn` (`find_position_for_ball`, `create_the_ball`), reset after 40 seconds untouched |
| the goals | the map's `_netgame_flag_ctf_flag` markers of team 0 and 1 |
| the carrier's speed and traits | Oddball's `speed_with_ball` (0.75, 1.0, 1.25) and `trait_with_ball` (invisible, extra damage, damage resistant) |
| melee only | the port's loadout `_loadout_weapon_none` with `no_map_weapons` (`game_variant_options`) |
| waypoints | `game_engine_set_goal_position` with nav points (`"ball_blue"`, `"default"`) |
| the clients' state | the game type's state in the distributed netcode (`game_engine_write_network_state`) |
| sounds | the team score sounds, "play ball", "hill moved" (`game_engine_multiplayer_sounds.c`) |

What the engine lacks: a gravity option, a speed option for everyone, a
melee multiplier, a gravity hammer and an energy sword a player can carry.
The first three are "Player traits" below. The weapons need new tags;
Grifball here is fists and the bomb until a map brings its own.

## The rules

Two teams. One bomb spawns at the middle of the map. Either team picks it
up and carries it into the other team's goal; a goal scores a point and
starts a new round. First to the score limit wins.

- **The bomb** is the map's ball (`get_ball_definition_index`), placed at
  the first `oddball_ball_spawn` flag (team index ignored). A map without
  one gets it halfway between the goals, dropped to the ground. It belongs
  to no team (`owner_team_index` NONE): anyone picks it up.
- **The goals** are the `ctf_flag` markers: a team scores at the *other*
  team's marker. Each team sees a waypoint on the goal it attacks while one
  of its players carries the bomb, and on the bomb otherwise.
- **Arming**: the carrier standing within the goal's radius (1.0 world
  unit, as CTF) for the variant's arm time scores. An arm time of 0 scores
  on contact (Grifball). Leaving the radius, dropping the bomb or dying
  restarts the time. The carrier's HUD counts it down.
- **A score** awards the point, takes the bomb from the carrier, and resets
  it to the middle. With the variant's "new round on score", every player
  is killed without a death counted and respawns, as at a round's start.
- **Untouched reset**: a bomb on the ground the variant's reset time
  (default 30 seconds) goes back to the middle.
- **The carrier** can't use active camouflage
  (`game_engine_player_depower_active_camo`), moves at the variant's
  speed, and has the variant's trait. The bomb is a melee weapon (the
  ball's own `melee_attack_damage`).
- **Spawns**: team starting locations as CTF's (`test_flag(0)` TRUE),
  rated away from the enemy goal. A starting location or netgame equipment
  marked for CTF counts for Bomb too (`match_game_type`), so every CTF map
  works without editing.
- **Statistics**: a player's scores and carries reuse `ctf_statistics`
  (`flag_scores`, `flag_grabs`), so the postgame carnage report, Delta
  Stats and the event log need no new fields.

## The variant

Bomb takes slot 6, Bungie's own cut slot after Race. `enum
game_engine_type` gets `game_engine_bomb = 6` in place of
`game_engine_terminator`, and `game_engines[6]` becomes `&bomb_engine`
(nothing reaches `stub_engine` through that slot: every variant is pinned
to 1..5 today).

`struct bomb_variant` goes in `union game_engine_variant`, which must stay
0x18 bytes (`struct game_variant` is 0x68: asserted, saved to disk and
carried in the network game):

```c
struct bomb_variant
{
	long arm_time;            /* seconds; 0: scores on contact */
	long reset_time;          /* seconds a dropped bomb waits; 0: never */
	long speed_with_bomb;     /* oddball's: slow, normal, fast */
	long trait_with_bomb;     /* oddball's: none, invisible, extra damage, damage resistant */
	boolean new_round_on_score;
	boolean random_bomb_spawn; /* among the map's oddball spawns */
	byte pad[6];
};
```

`game_engine_variant_cleanup` pins `game_engine_index` to 1..6 instead of
1..5 and pins these fields; a variant of 6 without Delta (below) is
refused rather than cleaned up into something else.

Built-in variants: "Bomb" and "Grifball". The 26 default playlist profiles
are a fixed table (`NUMBER_OF_DEFAULT_PLAYLIST_PROFILES`, asserted, with
names from the map's `ui\default_multiplayer_game_setting_names`), so the
two are added the way the port's own built-ins are, by name
(`game_engine_get_variant_by_name`, the dedicated server's
`server_builtins`), not as profiles 27 and 28.

## The engine

`game_engine_bomb.c` fills `struct game_engine` with CTF's shape:

- `initialize_for_new_map`: find the goals and the spawn, create the bomb,
  clear `bomb_globals` and `bomb_events`.
- `player_update_each_tick`: the carrier's speed, camo and arming; on the
  host only (`network_game_distributed_client()` returns early, as CTF's
  does).
- `objective_weapon_update`: the untouched reset, the waypoints.
- `picking_up`: a carry counted, the "has the bomb" messages;
  `allow_pick_up` always TRUE.
- `update`: the score limit ends the game (host only).
- `get_player_score`, `format_*`, `format_message`, `starting_location_rating`,
  `test_flag`, `test_trait`: as CTF's and Oddball's.
- `player_update` stays NULL: a value there replaces the default loadout
  code (`game_engine_postspawn_player_update`).

Every place that switches on the engine or pins it to 1..5 gets a case for
6. The places, from a survey of the tree:

- `game_engine.c`: `match_game_type`, `game_engine_variant_cleanup`,
  `game_engine_get_variant_by_name`, `game_engine_predict_resources` (preload
  the ball), `game_engine_force_autopickup`, `game_engine_verify_current_map`
  (warn without goals), `game_engine_ce_vehicle_default_bit`, the browser's
  statistics, and the network state's write and read.
- Xbox-style UI: `ui_widget_game_data_input_functions.c` (titles, bitmaps,
  score units, rule text: Bomb uses "unknown"'s bitmap until it has one).
- The port's menus: `menu_functions.c` (`engine_names[]`, the seven
  `PIN(...,0,5)`, `engine_items[]`, `gametype_engine_name`),
  `browser_screen.c`, `game_events.c`, `game_stats.c`, and
  `network_objects.c:2152` (an objective weapon's team range).
- The network code's reads of the index:
  `network_server_message_handler.c:1714, 1729`,
  `network_client_manager.c:2603`, `network_server_manager.c:1447, 1455,
  1596`, `network_game_manager.c:793`.
- The dedicated server: `server_config.c` (built-ins, `bomb.*` fields),
  `server_config.h`, `server_admin.c`, `server_commands.c`, `probe.c`, and
  the web panel's selector.

### Text and sounds

The map's `ui\multiplayer_game_text` has no bomb lines. Bomb's messages
("You have the bomb.", "Your ally has the bomb.", "The enemy has the
bomb.", "Arming: %d", "The bomb was reset.", "New round.") are the port's
own, appended to its string table past the map's indices, with the score
lines borrowed from CTF's ("You scored %d to %d." and its kin). Sounds are
the shipped ones: the team score sounds, "play ball" at a round's start,
"hill moved" for a reset.

### The clients

The host decides pickups, arming and scores; a client shows them. Bomb's
state for the distributed netcode, under the 0xF00 cap:

```c
struct bomb_network_state
{
	long scores[2];
	long bomb_index;          /* the host's object: same index everywhere */
	byte carrier;             /* distributed_player_to_byte */
	byte arming_team;         /* NONE as 0xFF */
	short arm_ticks_left;
	byte scores_events, scorer;
	byte carries, carrier_event;
	byte resets, rounds;
	byte pad[2];
};
```

The reader replays the events (score, carry, reset, round) as CTF's
replays captures, and the exact-size check stays. The bomb object itself
travels as any objective weapon does (`network_objects.c`).

## Delta only

An OpenCE client given engine 6 doesn't crash: it pins it to Race,
shows "RACE", reads the union as Race's, and then refuses every game
state the host sends (a size mismatch). So no OpenCE machine may play a
Bomb game, and the game protocol can't be changed to tell it so.

- **A capability**, `bomb` (bit 11 in `enum delta_capability`, its name in
  `delta_capability_names[]`, offered by default, a row in delta.md's
  registry). A host offers Bomb game types only while it hosts with Delta
  (`network.protocol` not `opence`).
- **The pregame**: a machine that hasn't agreed `bomb` once the handshake's
  window (4 seconds) has passed is dropped
  (`network_game_server_drop_machine`) with a notice saying the game is
  Bomb and needs ChupathingyCE; the countdown waits until every machine
  agrees (`delta_peer_room_has`).
- **A game in progress**: a joining machine is accepted as now, and dropped
  the same way if it hasn't agreed when its window ends, before its load
  finishes. Until then the host sends it nothing of the game but the
  pregame keep-alive, as for any joining machine.
- **Changing to Bomb** in a room with OpenCE machines: the host's menu says
  how many would be dropped, and asks.
- **Listings**: the LAN advertisement's `engine_type` is 6 (OpenCE's
  browser shows "RACE"; there's no field for a name); the internet
  listing's game type string and Delta List's `engine` say "Bomb" or
  "Grifball". Delta List's filters gain the game type.

The kill switch's `disabled_capabilities` can turn `bomb` off everywhere;
Bomb game types are then refused like any game without Delta.

## Player traits

General settings, any game type may use, Bomb first:

| Trait | Range | Applied |
|---|---|---|
| gravity | 25% to 200% | `global_gravity` (`physics.c`), set at the game's start, restored after |
| speed | 50% to 200% | `player->speed_multiplier`, times the carrier's |
| damage dealt, damage taken | 25% to 400% | `game_engine_get_damage_multiplier` (the host's damage) |
| melee | 50% to 400% | `unit_cause_player_melee_damage`'s scale |
| shields | on or off | the variant's `_game_variant_no_shields_bit` (already shared) |

Two sets: everyone's, and the bomb carrier's (applied on top).

They can't go in the shared records: `game_variant` and
`game_variant_options` have no room and OpenCE reads both. So they travel
over Delta Peer, as a new message (`DELTA_TRAITS`, the next free type in
`delta_wire.h`), sent by the host with WELCOME and when they change, and
gated by the same `bomb` capability (a room playing traits is a Delta
room). Every machine applies them: gravity and speed are simulated by each
client for its own player (prediction), and the host's movement checks
(`port/linux/NETCODE.md`, "Corrections") scale their limits by them, or a
fast player is pulled back.

A saved gametype keeps its traits in a third block of `blam.lst`, after the
PC options' 'GPVO' block at 0x100: a 'GPVT' header, the traits, and the
signature, as 'GPVO' does (`playlist_profile.c`). A gametype saved without
one has default traits.

## Grifball

A preset of Bomb and the traits, every value editable:

- Bomb: arm time 0, reset time 30 seconds, new round on score, carrier
  fast with extra damage.
- Teams, score to win 5, respawn 5 seconds, no shields.
- Loadout custom: none and none, no map weapons, no grenades: fists and
  the bomb.
- Traits: gravity 50%, speed 150%, melee 300%.

The numbers are starting points for a playtest, not Halo 3's.

**The court**: any map with two CTF markers works, an oddball spawn
puts the bomb where the map wants it. A Grifball court (a flat floor,
walls, a goal at each end) is a map, made with the Halo Custom Edition
tools and played as any Custom Edition map is. Restored maps such as
Digsite's are the players' own MCC files; ChupathingyCE loads them and
never ships them.

## Tests

- `tools/test_delta_peer.py`: the TRAITS message's round trip and
  refusals, the capability's room-wide check, and the pregame drop of a
  machine without it.
- `tools/test_delta.py`: bit 11's name and the registry.
- `network_test.c` bots: `host:bloodgulch:bomb` with pickups, a scripted
  carry into the goal, scores and a reset, compared across machines.
- `tools/crossplay_test.py`: an OpenCE client against a Bomb host is
  dropped with the notice, and the host plays on.
- The engine: the variant's cleanup (6 allowed, fields pinned), and a
  saved gametype with and without 'GPVT'.

## Plan

1. The engine, the variant and the menus, playable in a game of one
   machine (splitscreen) and between ChupathingyCE machines.
2. The `bomb` capability and the pregame and in-progress drops, before any
   release offers Bomb.
3. Player traits over Delta, with the host's movement checks.
4. The Grifball preset, the dedicated server's `bomb.*` and traits fields,
   Delta List's filters.
