# Settings

The server is set up with environment variables, one per setting, and a
settings file, `admin/settings.toml` in the data folder, which `sv_set` and
the control panel's Settings write. A setting in the environment wins over
the file.

## The server

| Variable | Default | What it does |
| --- | --- | --- |
| `HALO_DEDICATED` | (none) | The playlist, a path inside the data folder: `playlists/free_for_all.txt`. Required: without it the server only says how to start one. |
| `HALO_DEDICATED_NAME` | `Dedicated` | The server's name on the lists. 15 characters at most, the game's limit. |
| `HALO_DEDICATED_MINIMUM_PLAYERS` | `1` | The players a game waits for before its countdown starts. |
| `HALO_DEDICATED_MAXIMUM_PLAYERS` | `12` | The players a game takes, up to 128 (`sv_maxplayers` in [admin.md](admin.md) changes it while running, until a restart: set here, the environment wins over the settings file). |
| `HALO_DEDICATED_IDLE_LIMIT` | `5` | A game in which nobody scores for this many minutes ends. `0`: never. |
| `HALO_DEDICATED_PUBLIC` | `true` | A public game, listed in every in-game Server Browser (OpenCE's and ChupathingyCE's) through internet play's brokers. `false`: not listed there; the game list still lists it, and its invite still works. |
| `HALO_DEDICATED_NAME_FILTER` | `false` | `true`: players with a blocked name are warned and kicked ([below](#blocked-names)); `sv_name_filter 1` on the console does the same. |
| `HALO_DEDICATED_CHAT` | `2` | Text chat between ChupathingyCE players: `2` on, `1` team lines alone, `0` off (`sv_chat` sets it; the environment wins over the settings file). |
| `HALO_DEDICATED_CHAT_FILTER` | `true` | A chat line with a word of `blocked_names.txt`'s list (the game has it built in) is not passed on; its sender's game says "Not sent: blocked words". Nobody is kicked. |
| `HALO_DEDICATED_CHAT_LEGACY` | `true` | OpenCE players chat too, over their own text chat, bridged to ChupathingyCE's ([below](#text-chat)), while `HALO_DEDICATED_CHAT` is on. `false`: they get no chat here (`sv_set chat_legacy false`). |
| `HALO_DEDICATED_CHAT_COOLDOWN` | `5` | Once a player says lines faster than the chat allows (1 a second, 3 at once), their chat pauses for this many seconds and their game says "Chat paused for 5 seconds" (an older game says "Not sent: slow down"). `0`: no pause, each line past the limit is refused alone. Up to 600. |
| `HALO_DEDICATED_TEAM_CHANGE` | `off` | Players changing teams during a team game (the pause menu's CHANGE TEAMS, [below](#team-changes)): `off` (players keep their team, as on the Xbox), `balanced` or `allow` (`0`, `1` or `2`; the settings file's `team_change`). Chupathingy players see CHANGE TEAMS only when it is on. |
| `HALO_DEDICATED_TEAM_CHANGE_MARGIN` | `1` | With `balanced`, the most players one team may have over the other after a change, 0 to 16 (`team_change_margin`). |
| `HALO_DEDICATED_ALLOW_PLAYER_NAMES` | `false` | `true`: Chupathingy players may show names above other players' heads (their `display.player_names`). `false` plays as the Xbox did: a name shows only on the player aimed at (`allow_player_names`; the host's rules, [docs/delta.md](../../docs/delta.md#host-rules)). |
| `HALO_DEDICATED_VOICE` | `false` | Relay voice chat between Chupathingy players who turned it on (`voice`). Off by default, since every talker costs the server upload; turn it on with `voice = true` in the settings file, `sv_set voice 1`, or the control panel's Settings. The server never decodes, plays or keeps it, and relays 8 talkers at once at most. |
| `HALO_DEDICATED_VOICE_TEAM`, `_PROXIMITY`, `_EVERYONE`, `_PARTY` | `true`, `true`, `false`, `true` | The channels players may talk on (`voice_team`, `voice_proximity`, `voice_everyone`, `voice_party`). |
| `HALO_DEDICATED_VOICE_LOBBY` | `true` | Everyone hears everyone in the lobby and after a game (`voice_lobby`). |
| `HALO_DEDICATED_VOICE_RANGE` | `15` | How near is near for proximity voice, 5 to 100 world units (`voice_range`). |
| `HALO_DEDICATED_VOICE_KBPS` | `24` | Voice quality, 8 to 64 kbps (`voice_kbps`). Each talker costs the server about 5 KB/s of upload for each player hearing them at 24. |
| `HALO_DEDICATED_ALLOW_FOLLOW_CAMERA` | `false` | `true`: Chupathingy players may watch a living player's view while they wait to respawn (their `display.follow_camera`). `false` plays as the Xbox did: the death camera alone (`allow_follow_camera`). |
| `HALO_DEDICATED_LATE_JOIN_HOLD` | `false` | `true`: a player who joins a game in progress watches through the follow camera until the next game starts (everyone, OpenCE players too). `false` spawns them at once, as Halo does (`late_join_hold`). |
| `HALO_DEDICATED_BAN_SHARE` | `false` | `true`: while linked, your bans that name a Delta account or device go to your shared ban list on the site (`ban_share`). Never an address. |
| `HALO_DEDICATED_WELCOME` | (none) | A welcome: a server message each Chupathingy player gets in their chat as they join, up to 127 printable ASCII characters (`sv_welcome` sets it). OpenCE players get it as the host's notice in their chat, while they may chat. |
| `HALO_DEDICATED_POSTGAME_SECONDS` | `20` | How long the carnage report shows after each game before the next game's lobby, 0 to 300 seconds. |
| `HALO_DEDICATED_SKIP_LOBBY` | `false` | `true`: after the carnage report, if enough players stayed (`HALO_DEDICATED_MINIMUM_PLAYERS`, and teams as the game needs), the next game starts after a 3-second countdown instead of waiting out the lobby's. Otherwise the lobby opens as always. |
| `HALO_DEDICATED_PLAYLIST_SHUFFLE` | `false` | `true`: each time the playlist starts over, its entries are put in a new random order (the first pass is in the file's order). The entry just played is not first in the next pass. `sv_set playlist_shuffle true` changes it while the server runs. |
| `HALO_DEDICATED_GAMESPY_LISTING` | `false` | `true`: answer GameSpy status queries and send heartbeats to GameSpy masters ([below](#gamespy-status-queries)). `sv_set gamespy_listing true` turns it on while the server runs. |
| `HALO_DEDICATED_GAMESPY_PORT` | `2302` | The UDP port of the GameSpy status queries (`gamespy_port`). |
| `HALO_DEDICATED_GAMESPY_MASTERS` | `s1.master.hosthpc.com:27900` | The masters the heartbeats go to, `host:port`, comma separated (at most 4). Set but empty: none, the queries are answered alone. |
| `HALO_DEDICATED_GAMESPY_VERSION` | the server's version | The version (`gamever`) the queries and heartbeats report. |
| `HALO_DEDICATED_MAX_MACHINES_PER_ADDRESS` | `8` | The most machines that may join from one internet address, 0 to 16 (`max_machines_per_address`; 0: no limit). A machine past it is told there are too many players from its address. Local network addresses (10/8, 172.16/12, 192.168/16, 127/8, 169.254/16) are never limited. Players behind one carrier-grade NAT (100.64/10) can share an address, which is why the default is generous. Internet play counts each player by the address their connection comes from. |
| `HALO_DEDICATED_CE_JOINS` | `false` | `true`: answer Halo Custom Edition 1.0.10 join attempts on the GameSpy port ([below](#custom-edition-join-attempts)); needs `gamespy_listing`. |
| `HALO_DEDICATED_CE_WELCOME` | a line about cross-play | The chat line each Custom Edition player gets as they join (`ce_welcome`, up to 255 characters). Set but empty: none. |
| `HALO_DEDICATED_BETRAYAL_KICK` | `0` | Betrayals (teammates killed) in one game that kick a player, 1 to 100; they are warned one short of it, and a second such kick within 24 hours bans them for 30 minutes ([moderation.md](moderation.md#betrayal-and-afk-limits)). `0`: off. `sv_set betrayal_kick 3` sets it while the server runs. |
| `HALO_DEDICATED_AFK_KICK_MINUTES` | `0` | Minutes without input in a game in progress that kick a player, 1 to 120, warned 30 seconds before; never in the lobby or the carnage report, never a moderator ([moderation.md](moderation.md#betrayal-and-afk-limits)). `0`: off (`afk_kick_minutes`). |
| `HALO_DEDICATED_KEEP_PARTIES_TOGETHER` | `false` | `true`: in team games, Chupathingy players in one party go to one team where the teams stay within one player of each other: in the lobby (a member moved, or traded with a player in no party), and a late joiner before they spawn. Players can still change team in the lobby afterwards. `false` places them as the Xbox did (`keep_parties_together`; [docs/delta.md](../../docs/delta.md#party-teams)). |
| `HALO_DEDICATED_COMMANDS` | (none) | A file of [commands](admin.md#startup-commands) in the data folder, run once the server first hosts. |
| `HALO_DEDICATED_CONSOLE` | on in a terminal | Commands typed on the server's standard input ([the console](admin.md#the-console)). `true`: read them even when it is not a terminal; `false`: never. |
| `HALO_DEDICATED_CONTROL` | `127.0.0.1:8080` (`off` in our Docker image) | The [control API](admin.md#the-control-api)'s address, or `off` for none: a port (`8080`, on 127.0.0.1 only) or an address and port (`127.0.0.1:8080`, `[::1]:8080`, `0.0.0.0:8443`). Its token is made and printed the first time, and the first owner's setup link. The [control panel](moderation.md#the-control-panel) is on the same address: plain HTTP on the loopback address, HTTPS beyond it. |
| `HALO_DEDICATED_CONTROL_TLS` | `auto` | `auto`: HTTPS beyond the loopback address; `on`: always; `off`: never (refused on a public address). [moderation.md](moderation.md#remote-access-safely) |
| `HALO_DEDICATED_CONTROL_TRUSTED_PROXY` | (none) | The reverse proxies in front of the control panel (their addresses, comma separated: `127.0.0.1`): their `X-Forwarded-For` names the client, so the login limits count each client and not the proxy. [admin.md](admin.md#reaching-it-safely) |
| `HALO_DEDICATED_CONTROL_CERT`, `HALO_DEDICATED_CONTROL_KEY` | (the server's own) | Your own certificate and key for the control panel's HTTPS (PEM, as certbot writes them; a path in the data folder or absolute). A renewal is taken within a minute. |
| `HALO_DEDICATED_LINK` | `true` | `false` turns the [link to the Chupathingy site](moderation.md#the-link-to-the-chupathingy-site-optional) off for good. Until `sv_link`, it does nothing either way. |
| `HALO_DEDICATED_LINK_URL` | `HALO_NET_BROWSER` if set, else `https://stats.chupathingy.org` | The site the server links to (HTTPS). |
| `HALO_DEDICATED_LINK_ROLE` | `admin` | The most a role from the site may be on this server: `admin` or `moderator`. |
| `HALO_DEDICATED_LINK_ROLE_HOURS` | `24` | How long the site's roles hold without the site answering (1 to 720 hours), kept in `delta_roles.cache` across restarts ([moderation.md](moderation.md#who-can-moderate)). |

The settings file's keys (`sv_settings` lists them, with where each value
comes from): `name`, `maximum_players`, `minimum_players`, `idle_limit`,
`public` (from the next start), `name_filter`, `chat`, `chat_filter`,
`welcome`, `postgame_seconds`, `skip_lobby`, `chat_cooldown`, `team_change`
(0 off, 1 balanced, 2 allow), `team_change_margin`, `playlist_shuffle`, `chat_legacy`, `gamespy_listing`, `gamespy_port`, `ce_joins`, `allow_player_names`, `allow_follow_camera`, `max_machines_per_address`, `betrayal_kick`, `afk_kick_minutes`, `keep_parties_together`, `late_join_hold`, `ban_share`, `voice`, `voice_team`, `voice_proximity`, `voice_everyone`, `voice_party`, `voice_lobby`, `voice_range`, `voice_kbps`, and the playlist
`sv_playlist_use` chose. `sv_set welcome Hello and welcome` takes
the rest of the line as the value (quotes optional).

## Text chat

ChupathingyCE players talk over Delta Peer ([docs/delta.md](../../docs/delta.md#chat)):
the server checks each line (its rate, its text, the player's mute, the
filter) and passes it to everyone, or to the speaker's team. It writes
nothing of what is said: not to the log, not to the audit file. What it
does write is the admins' own: `sv_say`'s messages and each mute, in the
audit file. OpenCE players chat too, over their own text chat, which the
server bridges: they see every line (accents dropped, cut to 63 characters),
and theirs pass the same checks and reach everyone. `chat_legacy` (or
`HALO_DEDICATED_CHAT_LEGACY=false`) leaves them out; with the chat off they
are left out anyway.

## Team changes

Players keep the team they joined, as on the Xbox, unless the server allows
team changes. Then a ChupathingyCE player can change teams from the pause
menu (CHANGE TEAMS, as on Halo PC). Their game asks the server over Delta
Peer ([docs/delta.md](../../docs/delta.md#team-changes)) and the server
decides:

- `off` (the default): none (only `sv_team` moves players), and the pause
  menu has no CHANGE TEAMS.
- `balanced`: only when neither team ends up more than
  `HALO_DEDICATED_TEAM_CHANGE_MARGIN` players (1) bigger than the other.
- `allow`: any change.
- A player changes at most once every 10 seconds, and never leaves a team
  empty while others play (a team game with everyone on one team is over).

The player's unit dies as nobody's kill: no death, suicide or betrayal is
counted, no respawn penalty is added, their score stays theirs and the team
scores stay as they are. They respawn on the new team after the game type's
respawn time. Everyone in the game, OpenCE players too, sees the player on
the new team from their respawn: the game protocol is unchanged, and OpenCE
players see only a death and a respawn. OpenCE players cannot ask to change
teams. `sv_team <player> <red|blue>` (the control panel's To Red and To
Blue) moves a player whatever the setting says.

## Blocked names

`sv_name_filter` (the settings file's `name_filter`, or
`HALO_DEDICATED_NAME_FILTER`) is off unless it is turned on. On, the server
looks at each player's name as they join and whenever it changes, and a
player whose name is blocked is sent a warning, "That name isn't allowed
here. Pick another and rejoin.", which a ChupathingyCE game shows, and is
kicked three seconds later (at once if their game cannot show it), as
`sv_kick` kicks. They may rejoin under another name. The log and the audit
file say "kicked player 3: blocked name", never the name or the address.

The names are `blocked_names.txt` in the data folder, which the server's
download and its container's deploy scripts include: the same list the
ChupathingyCE site uses, read as the server starts and again whenever the
filter is turned on (`sv_name_filter 1` after editing it). Without the
file the filter blocks nothing, and the log says so. Its header explains
its form: a line an entry, `anywhere`, `whole` or `allow` and a word in
ROT13. A name is compared after it is put in lower case, its accents,
spaces, dots, underscores, dashes and the digits at its end dropped, and
digits, signs and Cyrillic and Greek letters that stand for Latin letters
read as those letters (`0` as `o`, `1` as `i`, `@` as `a`, ...), and also
with each run of a repeated letter squeezed to one.

A game everyone has left ends after 30 seconds. After each game the
carnage report shows for 20 seconds (`HALO_DEDICATED_POSTGAME_SECONDS`), then
the next entry's lobby opens, or with `HALO_DEDICATED_SKIP_LOBBY` the next
game starts at once when enough players stayed. The playlist vote on the
Chupathingy site changes the playlist files each week; it needs no lobby time
in the game.

## GameSpy status queries

Off unless `gamespy_listing` is on. The server then answers GameSpy status
queries on UDP port 2302 (`HALO_DEDICATED_GAMESPY_PORT`) in the two forms
Halo Custom Edition's servers answer: GameSpy's `\basic\` (and `\status\`,
`\info\`, `\players\`, `\rules\`) and QR2's `FE FD 00`, plus QR2's echo,
which a LAN server browser broadcasts. It also sends QR2 heartbeats to the
masters in `HALO_DEDICATED_GAMESPY_MASTERS` (the ones Custom Edition 1.0.10
reports to, by default, game name `halom`) and answers their challenge. As
it stops, it tells the masters it is gone.

What it reports: the server's name, the map, the game type, the score
limit, teams or not, the seats and the players (name, score, ping, team;
32 rows at most). A map has the name Custom Edition players know it by: an
Xbox map is `xbox_<map>` (`xbox_bloodgulch`; Custom Edition's own
`bloodgulch` is the PC version, with other geometry), a Custom Edition or
Halo PC map (`timberland@ce`, `icefields@pc`) its own name. Two keys of the
server's own, `bridge` (`chupathingy`) and `bridge_version` (the server's
version), let a client tell it from a Custom Edition server, and `chimera`
(`optional`) says a Custom Edition player needs no Chimera here.

The version reported is the server's own (`HALO_DEDICATED_GAMESPY_VERSION`
sets another). Custom Edition 1.0.10's server browser shows only servers
reporting its own version, `01.00.10.0621`, so by default the server is in
the masters' list but that browser does not show it. Report
`01.00.10.0621` only on a server that takes Custom Edition players
(`ce_joins`, below) and plays Custom Edition maps, or the browser shows its
players a game they cannot join.

The UDP port must be open (forwarded) for the queries to reach the server
from outside. The naming of Xbox maps is the Custom Edition community's,
passed on by ugoboom.

### Custom Edition join attempts

Off unless `ce_joins` is on too (`sv_set ce_joins true`). The same UDP port
then answers a Halo Custom Edition 1.0.10 client that connects to it: the
connection's handshake, its key agreement and its enciphered, reliable
messages, as Custom Edition's own server does them. The server tells the
client its map (by the name above) and reads its join request (the
player's name goes in the log; the CD key hash is not logged).

The player joins while a game is being played on a map they have too: a
Custom Edition map (`bloodgulch@ce`, the same .map file on both sides). They
play as one of the server's own players, moved by their client's input.
On any other map, or with no seat left, the join is refused with Custom
Edition's own message, not a timeout. This is new: test it on your server
before you rely on it.

What works:

- Moving, aiming and shooting, both ways. Their own position is corrected
  to the server's twice a second, so their shots land at range.
- The other players, their weapons and their shots, as the server's game
  has them.
- Shield and health on their HUD, deaths and the respawn countdown.
- The scoreboard: kills, assists, betrayals, deaths, suicides and Slayer's
  scores.
- Weapons and equipment on the ground, picking them up and dropping them.
- Vehicles: getting in and out of seats, and driving.
- A welcome line in their chat as they join (`ce_welcome`), and `sv_say`.
- The end of a game and the next one: the scores come up on their screen,
  and when the next game starts they join it again by themselves (their
  client reconnects, as it does on Custom Edition's own servers) and keep
  their seat. They are let go when the next map is not a Custom Edition
  map, or when they don't come back within 45 seconds.
- `sv_kick` and `sv_ban` (a ban is by their address). The bans, the limit
  on players from one address (`max_machines_per_address`) and the
  betrayal and AFK limits apply to them too: warnings go in their chat. The
  host's line of sight check is not needed for them, since the server's
  game fires their shots itself.

Not yet:

- At most 4 Custom Edition players at once (they are seats of the server's
  own machine); a fifth is told the server is full.
- Their client is told the game is Slayer, free for all: play Slayer with
  them. Other game types and teams are not shown right on their side.
- A join between games waits up to 12 seconds for the next one. On an
  empty server, their player goes into the lobby and a game starts after a
  short countdown.
- Weapons and equipment coming back (respawning) on the ground.
- Checked live only in part: a Chupathingy player in a vehicle as Custom
  Edition players see it, and swapping weapons.

## Folders

| Variable | Default | What it does |
| --- | --- | --- |
| `HALO_DATA_ROOT` | the current folder, else the program's | The data folder: `maps/` (with `ui.map`), `maps_ce/`, `maps_md/`, `maps_pc/`, the Xbox map packs' `maps_<pack>/` (`maps_cex/`), `playlists/` (the older `maps/ce/`, `md_maps/` and OpenCE's `custom_maps/` are read too). The server's log goes in `logs/` here (`logs/debug.txt`). |
| `HALO_SAVE_ROOT` | `~/.local/share/chupathingyce` (Windows: `%APPDATA%\ChupathingyCE`; `start-server.bat` and a Windows service: `saves` in the data folder) | Where the server keeps its saves (the game's profile and scratch files, about 33 MB). Give each server on a machine its own. |
| `HALO_LOG_ROOT` | `logs/` in the data folder | Where the server's log (`debug.txt`) goes. |
| `HALO_CACHE_ROOT` | `cache/` in the save folder (`/data/cache` in the Docker image) | Where the server keeps what it can make again. |

The server also writes `config.toml`, its settings at their defaults, beside
the program if it can (an image's or a system folder's program cannot, which
is fine). Settings in the environment override it. A new file has only the
settings the server uses: internet play, the game list, Delta Stats, the
Bomb and Grifball games, downloaded maps, the folders, and a few debug
settings (full addresses in the log, the telnet console and its limits in
a public game, and the network tests). The game's display, sound and control settings are left out. The
server's own settings (name, players, chat and the rest) are not in it:
they are in `admin/settings.toml`. An older file that has the game's
settings is left as it is and still works.

## The network

| Variable | Default | What it does |
| --- | --- | --- |
| `HALO_NET_BROWSER` | `https://stats.chupathingy.org` | The game list the server announces to; empty for none. |
| `HALO_NET_LIST_GAMES` | `true` | `false`: not listed on the game list (invites and the Server Browser still work). |
| `HALO_NET_REPORT_HOSTED` | `true` | `false`: the server stays listed, but sends no carnage reports or Delta Stats of its games. |
| `HALO_NET_ONLINE` | `true` | Internet play: how players outside your network reach the server. `false` keeps it to the local network. |
| `HALO_NET_TUNNEL_PORT` | `0` (any) | The UDP port internet play uses. A fixed one can be forwarded, for networks whose NAT stops connections. |
| `HALO_NET_ALLOW_UPNP` | `true` | Let internet play ask the router to forward its port (UPnP). |
| `HALO_NET_BROKERS_FILE` | `brokers.txt` | The file of internet play's MQTT brokers (one `host:port` on each line), beside the program unless a full path. Without one, the server uses the game's own list, the same as `port/assets/network/brokers.txt`. |
| `HALO_NET_ANNOUNCE` | `latest` | The OpenCE network version the server announces. `latest` follows the newest one Chupathingy's signed table says our servers host well, so OpenCE's newest players see the server. The server checks for a new table every 30 minutes, and changes by itself between games, with no restart. A table row may name host protections a server needs for a number; a build without them announces its own number (32) and logs why. A number pins it, for example `32` for a server meant for console players on older builds ([Delta](../../docs/delta.md#auto-announce)). |
| `HALO_LEGACY_TABLE` | (none) | A legacy table file, not signed, used in place of the signed ones: its row for the build's wire sets the OpenCE network versions the server announces and its range. For tests, and for an admin who knows better; the log warns at start, and the server neither fetches nor passes on signed tables meanwhile ([Delta](../../docs/delta.md#local-override)). To announce a newer or older number, use `HALO_NET_ANNOUNCE` instead. |
| `HALO_NET_ADDRESS` | (any) | The IPv4 address system link uses. Several servers on one machine each need their own: `127.0.0.2`, `127.0.0.3`, ... ([Docker](docker.md#more-servers-on-the-same-host)). |
| `HALO_NET_BROADCAST` | (the local network's) | Comma-separated addresses system link announces games to, instead of the local network's broadcast. |
| `HALO_NET_PROTOCOL` | `auto` | Delta Peer ([docs/delta.md](../../docs/delta.md)), ChupathingyCE's messages beside OpenCE's protocol, on UDP port 5160 beside the game's 5150: `auto` speaks it with the machines that do (the rest play plain OpenCE); `opence` turns it off; `delta` is `auto` for now. |
| `HALO_NET_HOST_PLATFORM_LIMITS` | `true` | Keep the game to the players its Delta machines' platforms take (an original Xbox: 16, unless its player opted out); `false` ignores them, for testing. |
| `HALO_NET_HIT_LINE_OF_SIGHT` | `true` | Refuse a client's reported hit through a wall: the shot needs a clear line to its target from where the server had the shooter (their view, head or vehicle seat's gun), or, for an explosion, from where it went off. Glass, what the projectile passes through, and homing or arcing projectiles are allowed for; melee and vehicle hits are not checked. `false` turns it off if good hits are refused (`network.hit_line_of_sight`). Players need nothing new. |
| `HALO_NET_MAP_SHARE` | `false` | `true`: a player who joins without the server's Custom Edition map, and asks for it, gets it from the server: only the map it is on now, only the map file, only to machines in the game, over TCP port 5153 (open it as you open the game's port). A machine downloading is not dropped for having no player. Off by default; our [C] servers turn it on after the map bot's first invite test. `network.map_share`. Refer to "Map sharing" in [port/linux/README.md](../../port/linux/README.md#map-sharing). |
| `HALO_NET_MAP_SHARE_UPLOAD` | `1024` | The most map sharing sends, in KB a second, for everyone downloading at once (16 to 20480). `network.map_share_upload`. |
| `HALO_NET_LATE_JOIN_WAIT` | `5` | How many seconds a machine that joins a game in progress waits after its first player is in, so the players on its other controllers can join too (split screen), 0 to 30. It starts sooner when the machine has four players; 0 starts it at once. |
| `HALO_NET_BLOCK_MOUSE_AIM_ASSIST` | `false` | The server's policy on aiming assist for the mouse and keyboard: `false` leaves each player's own setting (`input.mouse_aim_assist`); `true` turns the view's magnetism off for players aiming with the mouse, in every game of the server. Clients are told when they join and when it changes (network version 32). |
| `HALO_PLAYLIST_ID` | (none) | The id of the playlist's verified map set on the Chupathingy site (`c_ce_maps`), sent with the game list entry so joining players can get missing maps ([Playlists](playlists.md#the-playlists-map-set)). |
| `SSL_CERT_FILE` | the system's | The certificates the server checks stats.chupathingy.org's against, when they are somewhere unusual. |

Players' addresses are left out of `debug.txt` and the output, as in the
game; `HALO_LOG_ADDRESSES=1` logs them whole, for an operator chasing a
problem (the log then says so at the first one). `0`, `false`, `no` and
`off` leave it off, as for every switch here that only has to be set. Logs
written with it on hold everyone's address: delete them once the problem is
found.

The settings for hosts in `config.toml`, `network.host_cheats`
(`HALO_NET_HOST_CHEATS`, `false`) and `network.host_console_objects`
(`HALO_NET_HOST_CONSOLE_OBJECTS`, `32`), are about a player's own console
and cheats in a game they host. On the server they touch only the debug
telnet console below: in a public game with players in it, its commands
that change the game are refused unless `HALO_NET_HOST_CHEATS=true`, and
it may make at most that many objects a game. The `sv_*` commands are
not affected.

### Debug settings

For testing and support. Leave them off on a real server.

| Variable | Default | What it does |
| --- | --- | --- |
| `HALO_TELNET_CONSOLE` | `false` | Debug: opens the game's script console on 127.0.0.1, at `HALO_TELNET_CONSOLE_PORT` (`2323`). It has no password. |
| `HALO_NETWORK_LATENCY`, `HALO_NETWORK_LOSS`, `HALO_NETWORK_CORRUPT`, `HALO_NETWORK_CORRUPT_STREAM`, `HALO_NETWORK_CORRUPT_AFTER` | `0` | Debug: delay what is received (milliseconds), drop or damage a percent of it, after so many seconds. |

## The command line

```
chupathingyce-server [--version | --help]
chupathingyce-server.exe [--install-service [name] | --uninstall-service [name]]   (Windows)
```

| | |
| --- | --- |
| `--version`, `-v` | Prints the version, architecture and network version, and exits. |
| `--help`, `-h` | Prints a summary of the settings, and exits. |
| `--install-service [name]` | Windows: makes a Windows service of the server (`ChupathingyServer` unless named), with the `HALO_*` settings set in that command prompt, and starts it. Run as administrator. `start-server.bat install` does it for you ([windows/README.txt](../windows/README.txt)). |
| `--uninstall-service [name]` | Windows: stops the service and takes it out. |

Commands for a running server (kick, ban, change the map, ...) are typed
on its console or sent to its control API: [admin.md](admin.md).

Exit statuses: `0` stopped as asked (SIGTERM, SIGINT); `1` no maps, or a
playlist it cannot read; `2` no playlist (`HALO_DEDICATED`), or an argument
it does not know.

## The files it writes

In the data folder: `logs/debug.txt` (its log; `HALO_LOG_ROOT` puts `logs/` elsewhere, and `HALO_CACHE_ROOT` the cache, `/data/cache` in the Docker image), `bans.txt` (its bans, read on
every join: [admin.md](admin.md#bans)), `cheaters.txt` (players dropped for
cheating), `control_credentials.txt` (the control API's credentials, its tokens' hashes,
once the API is turned on), `control_accounts.txt` (the control panel's
accounts), `control_audit.log` (every change: [moderation.md](moderation.md)),
`moderators.txt` (moderators by moderator key), `control_tls.crt` and
`control_tls.key` (the panel's own certificate, when it is HTTPS),
`delta_link.key` (the link to the Chupathingy site, once linked),
`delta_roles.cache` (the site's roles, kept for a restart while the site is
down), and
`admin/` (playlists, game types and settings made on the server:
[playlists.md](playlists.md)). Keep them private, and back them up.

## Probing a game

With `HALO_PROBE` holding an invite (the digits after `halo://join/`), the
server is instead the game list's probe: it reads the game the invite leads
to, prints it as one line and exits, without joining it. See
[docker.md](docker.md#the-game-lists-probe).
