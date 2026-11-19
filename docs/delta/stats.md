# Delta Stats

What a game reports when it ends, and how the site keeps it. Everything is
what the host saw; nothing a client says about others is taken.

- **Reports** (exists): end-of-game totals, with the host, verified and
  player tiers (`/v1/report`, `/v1/client_report`, `/v1/claim`). In the
  host's report, a line of a player not on Chupathingy (no Delta Peer
  platform, not the host's own) is only `name`, `team`, `place`, `score`,
  `kills`, `deaths` and the claim `tag`; such a player who left early is
  only `name`, `team`, `kills` and `deaths` in `departed`. In a client's
  report, only the reporter's own lines are in full; every other line is
  the same minimum.
- **Events** (built: `port/linux/src/event_log.c`, `event_upload.c`,
  `port/linux/game/game_events.c`; the site's `analytics.py`): what happened
  in a game, recorded by its host and sent when it ends.

## Who records

Only a host records. It is on by default and the host's operator or
player can turn it off (`network.report_events`, `HALO_NET_REPORT_EVENTS`
set to false): our [C] servers, community servers and ChupathingyCE
players who host, unless they opt out. That setting stops the events
batch only. The host's carnage report (`/v1/report`) is a separate upload:
it goes whenever the game is listed, and `network.report_hosted_games =
false` stops it and the events batch while the
game stays listed; `network.list_hosted_games = false` (or an empty
`network.browser_url`) stops it too, with the listing. Everything is what the host saw, so it is exact; nothing a
client says is taken. Co-op (campaign) games are never recorded.

## Players

A player is their name, their client (`chupathingyce` when their machine
speaks Delta or is the host's own, else `other`) and platform. No hardware
ID, nor any hash of one, is sent (schema 1 had a keyed hash, `ident`; the
site no longer keeps it). A player whose client is `other` (an OpenCE
player) is only their name, client, team, bot, score, place, kills and
deaths: no other totals, and no event row is about them (their medals,
objectives, pickups, rides, spawns, positions, pings, sessions and flags
are left out; a kill of theirs, or of them, keeps their index but not
where they stood or their vehicle). A player becomes a confirmed
player (a player ID, so a profile) only the way carnage reports confirm
them: their own copy of the game claims their line of the same game's
report with its player key (`/v1/claim`); the site then marks the same
line of the events. A copy claims a game it joined only with
`network.report_joined_games` on (the default), the setting that also sends
its own report of the game; a game it hosted, unless
`network.report_hosted_games` is off. A host's word about who a player is is never taken. No
IP address is ever recorded or sent, by the game or the site.

## The batch

JSON, gzip on the wire. `t` is seconds since the game started; positions
are world units `[x, y, z]`; players are indexes into `players`, `-1`
nobody (the world). Later versions only add fields; unknown fields are
ignored.

```
{"schema": 2,
 "server": {"name", "build", "platform"},
 "game": {"id" (32 hex, random), "part", "final", "invite"?, "playlist"? (a
          dedicated server's playlist file's name), "map", "map_info"?
          (the map's facts, below), "engine",
          "gametype", "teams", "score_limit", "started", "ended" (unix),
          "end_reason": score|time|admin|empty|error|other, "team_scores"?},
 "players": [{"name", "client", "platform"?, "team", "bot", "color"?
              (armor, 0-17),
              "score", "place", "kills", "deaths", "assists", "betrayals",
              "suicides", "best_spree", "damage_dealt", "damage_taken",
              "shots", "hits", "grenades": {"frag", "plasma"},
              "objectives": {"flag_grabs", "flag_returns", "flag_scores",
                             "ball_time", "ball_kills", "hill_time", "laps"},
              "weapons": [{"weapon" (tag folder), "shots", "hits", "kills",
                           "headshots", "damage"}],
              "vehicles": [{"vehicle", "seat", "seconds"}],
              "pickups": {tag: count}, "medals": {key: count}}],
 "sessions": [{"player", "joined", "left", "reason": end|quit|kick|ban|timeout}],
 "kills": [{"t", "killer", "victim", "weapon" (damage tag), "damage":
            bullet|plasma|melee|grenade|explosion|vehicle|fall|other,
            "killer_pos"?, "victim_pos"?, "killer_vehicle"?, "headshot"?,
            "betrayal"?, "suicide"?, "stuck"?, "victim_riding"?, "from_grave"?}],
 "medals": [{"t", "player", "medal"}],
 "objectives": [{"t", "player", "team", "kind": flag_grab|flag_return|
                 flag_score|ball_grab|ball_drop|hill_enter|hill_exit|race_lap,
                 "pos"}],
 "pickups": [{"t", "player", "item", "pos"}],
 "rides": [[from, to, player, vehicle, seat]],
 "spawns": [[t, player, x, y, z]],
 "positions": [[t, player, x, y, z]],
 "pings": [[t, player, ms]],
 "health": [{"time", "players", "tick_ms", "tick_ms_max", "cpu", "memory_mb"}],
 "moderation": [{"t", "kind", "player", "name", "by", "reason"}],
 "flags": [{"t", "player", "kind", "severity", "detail"}],
 "limits": {"events", "capacity", "sample_seconds", "dropped": {type: n}}}
```

How each is known: kills and damage at the blow, shots per
projectile a trigger makes (`weapons.c`; grenades thrown count as shots of
the grenade), hits per projectile that damaged another player (melee
excluded); medals and sprees are worked out from the kills by the rules
under "Medals"; objectives, rides,
pickups, powerups and spawns from each player's state each frame; positions
every `network.events_positions` seconds (2) per living player; ping every
10 seconds; health each minute (dedicated servers only).

Limits: a game keeps at most `network.events_limit` events (40000, about 60
bytes each). When full, position and ping samples thin out (every other
second, then every other of those,...), then pickups, rides, spawns and
medals give way to kills, objectives and moderation; every drop is counted
in `limits`. Totals are counted before anything is dropped, so they stay
exact. The site refuses a batch over 2 MB sent or 8 MB of JSON.

Moderation: `event_log_moderation(kind, who, by, reason)` (event_log.h)
records a kick, ban, warning or other action in the game under way, and a
kicked or banned player's leaving is recorded as such. Delta Control's
audit hands each action to `server_roles_set_recorder()`'s recorder, on any
thread; `server/platform/server_events.c` queues it
 for the game's thread. A game that ends with everyone gone is sent as `empty`; a
server that stops mid-game sends nothing for it. Anti-cheat `flags` are a
schema slot with nothing filling it yet.

## Medals

One set of rules gives every medal the game reports:
Delta Stats' batch (`"medals"`, each
player's `"medals": {key: count}`), the game list's carnage reports and a
console's reports (each line's `"medals"`, from `game_stats.c`), the local
service record, and the HUD's pop-ups (`display.medals`, off by default).
They are worked out from the kills the machine saw and change nothing in
the game. A betrayal or a suicide earns none and ends the dead player's
sprees. The keys, in their order:

- Multikills, each kill within 4 seconds of the one before (the game's own
  window): `double_kill` (2), `triple_kill`, `killtacular`, `killtrocity`
  (5), as Halo: Combat Evolved has them, then Halo 3's `killimanjaro` (6),
  `killtastrophe`, `killpocalypse` and `killionaire` (9 and every kill
  after).
- Sprees, kills without dying: `killing_spree` (5), `killing_frenzy`,
  `running_riot`, `rampage` (20), `untouchable`, `invincible`,
  `inconceivable`, `unfriggenbelievable` (40).
- A kill's: `beat_down` (melee), `sniper_kill`, `grenade_stick` (Stuck),
  `splatter`, `from_the_grave` (the killer dead as it landed), `killjoy`
  (the end of a spree of 5 or more), `first_strike` (the game's first
  kill), `revenge` (the player who killed you last).
- A weapon's sprees, its kills without dying: `sniper_spree` (5) and
  `sharpshooter` (10), `shotgun_spree` and `open_season`, `splatter_spree`
  and `vehicular_manslaughter`, `stick_spree` (5 stuck).

Splatter and its sprees need the damage's category, which only the host's
Delta Stats has; the carnage reports leave them out. Older builds report
fewer keys (Halo CE's four multikills and four sprees, Killtrocity for 5
and more). The site takes any key (at most 64 a line) and counts the ones
it knows (`site.js`, `MEDALS`); its checks hold a line's multikill medals
to its kills, and its sprees, five kills each, too.

With `display.medals` on, the HUD shows each medal you earn as its icon at
the end of the kill's line in the kill feed, at the text's height
(`port/linux/game/medal_hud.c`, from `source/interface/hud_messaging.c`,
drawn by the overlay). The icons come, fade and scroll with that line.
Medals no kill's line took get a line of their own, the icons alone. Off,
the kill feed is retail's. The icons need the game list,
so every console build and the dedicated server are
without them and show the medal's name as a line of the HUD's messages. The Delta Guide's SERVICE
RECORD shows your top medals with their icons, names and counts.

## Map facts

`game.map_info` (optional; added in 0.8.0, still schema 2) describes the
map the game was played on, for the site's map catalog: what the map is and
its shape, so the site can list maps beyond the Xbox's that it can't read
itself. The host fills it in from the map it loaded
(`port/linux/game/game_events.c`, built by `port/linux/src/map_report.c`).
It holds facts about the map only. Nothing about players, machines or
addresses is in it, and no picture or file content.

```
"map_info": {"family": xbox|ce|md|pc,
             "file" (the file's name, without its family's suffix or ".map"),
             "size"? (bytes; not for an Xbox map, whose file is compressed),
             "blake2b"? (64 hex: the whole file's BLAKE2b-256, as Delta Peer's
                         MAP has it; every family but the Xbox's has it),
             "cache_version", "header_checksum" (8 hex), "build" (the cache
             header's build string), "scenario" (its tag path),
             "gametypes": [slayer|ctf|oddball|king|race] (slayer for any
                           spawn; the others for their netgame flags),
             "spawns", "bounds"?: {"x": [min, max], "y": [...], "z": [...]}
                                  (the structure BSP's world bounds)}
```

For example, Danger Canyon from Custom Edition:

```
{"family": "ce", "file": "dangercanyon", "size": 15165396,
 "blake2b": "2a97db833fec4a553a68ddfaeb53507e43cc2e2bcf9036697fb6e65b0bcf1f2e",
 "cache_version": 609, "header_checksum": "c410cd74", "build": "01.00.00.0609",
 "scenario": "levels\\test\\dangercanyon\\dangercanyon",
 "gametypes": ["slayer", "ctf", "oddball", "king", "race"], "spawns": 103,
 "bounds": {"x": [-75.449, 72.65], "y": [-23.019, 69.183], "z": [-12.178, 11.618]}}
```

A map past the Xbox's goes in only with its hash. While the hash is still
being computed, the next part asks again, so a game's first part can come
without `map_info`. A site that doesn't know the field ignores it, as with
any unknown field. Sites should treat it as a claim, like the other
fields: record a map's facts once two reporters agree on the same hash, or
one of our own servers reports them.

## The upload

`POST <network.browser_url>/v1/events`, `Content-Type: application/json`,
`Content-Encoding: gzip`, and `Authorization: Bearer <token>` for a trusted
server (`network.events_token`, `HALO_EVENTS_TOKEN`; made on the site's host
with `analytics.py server-add`, only its hash kept there; sent only over
HTTPS or to this computer). Without a token, the site takes a batch only
while the same address lists the game whose `invite` it carries. Answers:
`200 {"ok", "match", "tier", "duplicate", "ignored"}`; `400` a bad field
(named); `401` a wrong token; `403` not a game this address lists; `409` a
different batch of a finished game; `413` too large; `422` co-op; `429`
too many (one each 20 seconds, 60 an hour, per server).

A batch is sent 5 seconds after its game ends (after the carnage report),
again later after no answer, a 5xx or a 429 (doubling from 30 seconds, for
about a day), and at once when the program exits. A long game is also sent
as it stands every `network.events_part_minutes` (30) with `"final":
false`; the site keeps the latest part and replaces it with the end.
`network.events_folder` keeps a copy of each batch.

## On the site

Stored in `reports.db`: matches and players' lines always;
kills 180 days, timelines 180, replays (positions) 30, heatmap squares 365,
server health 90, moderation 365; past a size budget
(`--events-budget-mb`, 400) the oldest replays, then timelines, then kills
go first. Pages: `/matches/N`, `/players/<who>/stats`, `/maps`,
`/maps/<map>`, `/servers`, `/servers/N`, `/records`; the interface is on
the site's `/api`. Leaderboards and records count ranked matches only (two
or more players, no bots, a minute or more). A confirmed player can hide
their stats or their heatmaps from their profile.
