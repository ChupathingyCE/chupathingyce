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
| `HALO_DEDICATED_TEAM_CHANGE` | `balanced` | Players changing teams during a team game (the pause menu's CHANGE TEAMS, [below](#team-changes)): `balanced`, `allow` or `off` (`1`, `2` or `0`; the settings file's `team_change`). |
| `HALO_DEDICATED_TEAM_CHANGE_MARGIN` | `1` | With `balanced`, the most players one team may have over the other after a change, 0 to 16 (`team_change_margin`). |
| `HALO_DEDICATED_WELCOME` | (none) | A welcome: a server message each Chupathingy player gets in their chat as they join, up to 127 printable ASCII characters (`sv_welcome` sets it). OpenCE players get it as the host's notice in their chat, while they may chat. |
| `HALO_DEDICATED_POSTGAME_SECONDS` | `20` | How long the carnage report shows after each game before the next game's lobby, 0 to 300 seconds. |
| `HALO_DEDICATED_SKIP_LOBBY` | `false` | `true`: after the carnage report, if enough players stayed (`HALO_DEDICATED_MINIMUM_PLAYERS`, and teams as the game needs), the next game starts after a 3-second countdown instead of waiting out the lobby's. Otherwise the lobby opens as always. |
| `HALO_DEDICATED_PLAYLIST_SHUFFLE` | `false` | `true`: each time the playlist starts over, its entries are put in a new random order (the first pass is in the file's order). The entry just played is not first in the next pass. `sv_set playlist_shuffle true` changes it while the server runs. |
| `HALO_DEDICATED_COMMANDS` | (none) | A file of [commands](admin.md#startup-commands) in the data folder, run once the server first hosts. |
| `HALO_DEDICATED_CONSOLE` | on in a terminal | Commands typed on the server's standard input ([the console](admin.md#the-console)). `true`: read them even when it is not a terminal; `false`: never. |
| `HALO_DEDICATED_CONTROL` | (off) | The [control API](admin.md#the-control-api)'s address: a port (`8080`, on 127.0.0.1 only) or an address and port (`127.0.0.1:8080`, `[::1]:8080`, `0.0.0.0:8443`). Its token is made and printed the first time, and the first owner's setup link. The [control panel](moderation.md#the-control-panel) is on the same address: plain HTTP on the loopback address, HTTPS beyond it. |
| `HALO_DEDICATED_CONTROL_TLS` | `auto` | `auto`: HTTPS beyond the loopback address; `on`: always; `off`: never (refused on a public address). [moderation.md](moderation.md#remote-access-safely) |
| `HALO_DEDICATED_CONTROL_CERT`, `HALO_DEDICATED_CONTROL_KEY` | (the server's own) | Your own certificate and key for the control panel's HTTPS (PEM, as certbot writes them; a path in the data folder or absolute). A renewal is taken within a minute. |
| `HALO_DEDICATED_LINK` | `true` | `false` turns the [link to the Chupathingy site](moderation.md#the-link-to-the-chupathingy-site-optional) off for good. Until `sv_link`, it does nothing either way. |
| `HALO_DEDICATED_LINK_URL` | `HALO_NET_BROWSER` if set, else `https://stats.chupathingy.org` | The site the server links to (HTTPS). |
| `HALO_DEDICATED_LINK_ROLE` | `admin` | The most a role from the site may be on this server: `admin` or `moderator`. |
| `HALO_DEDICATED_LINK_ROLE_HOURS` | `24` | How long the site's roles hold without the site answering (1 to 720 hours), kept in `delta_roles.cache` across restarts ([moderation.md](moderation.md#who-can-moderate)). |

The settings file's keys (`sv_settings` lists them, with where each value
comes from): `name`, `maximum_players`, `minimum_players`, `idle_limit`,
`public` (from the next start), `name_filter`, `chat`, `chat_filter`,
`welcome`, `postgame_seconds`, `skip_lobby`, `chat_cooldown`, `team_change`
(0 off, 1 balanced, 2 allow), `team_change_margin`, `playlist_shuffle`, `chat_legacy`, and the playlist
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

In a team game a ChupathingyCE player can change teams from the pause menu
(CHANGE TEAMS, as on Halo PC). Their game asks the server over Delta Peer
([docs/delta.md](../../docs/delta.md#team-changes)) and the server decides:

- `balanced` (the default): only when neither team ends up more than
  `HALO_DEDICATED_TEAM_CHANGE_MARGIN` players (1) bigger than the other.
- `allow`: any change. `off`: none (only `sv_team` moves players).
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

## Folders

| Variable | Default | What it does |
| --- | --- | --- |
| `HALO_DATA_ROOT` | the current folder, else the program's | The data folder: `maps/` (with `ui.map`), `maps_ce/`, `maps_md/`, `maps_pc/`, `playlists/` (the older `maps/ce/`, `md_maps/` and OpenCE's `custom_maps/` are read too). The server's log goes in `logs/` here (`logs/debug.txt`). |
| `HALO_SAVE_ROOT` | `~/.local/share/chupathingyce` | Where the server keeps its saves (the game's profile and scratch files, about 33 MB). Give each server on a machine its own. |
| `HALO_LOG_ROOT` | `logs/` in the data folder | Where the server's log (`debug.txt`) goes. |
| `HALO_CACHE_ROOT` | `cache/` in the save folder (`/data/cache` in the Docker image) | Where the server keeps what it can make again. |

The server also writes `config.toml`, its settings at their defaults, beside
the program if it can (an image's or a system folder's program cannot, which
is fine). Settings in the environment override it.

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
| `HALO_LEGACY_TABLE` | (none) | A legacy table file, not signed, used in place of the signed ones: its row for the build's wire sets the OpenCE network versions the server announces and its range. For tests, and for an admin who knows better; the log warns at start, and the server neither fetches nor passes on signed tables meanwhile ([Delta](../../docs/delta.md#local-override)). |
| `HALO_NET_ADDRESS` | (any) | The IPv4 address system link uses. Several servers on one machine each need their own: `127.0.0.2`, `127.0.0.3`, ... ([Docker](docker.md#more-servers-on-the-same-host)). |
| `HALO_NET_BROADCAST` | (the local network's) | Comma-separated addresses system link announces games to, instead of the local network's broadcast. |
| `HALO_NET_PROTOCOL` | `auto` | Delta Peer ([docs/delta.md](../../docs/delta.md)), ChupathingyCE's messages beside OpenCE's protocol, on UDP port 5160 beside the game's 5150: `auto` speaks it with the machines that do (the rest play plain OpenCE); `opence` turns it off; `delta` is `auto` for now. |
| `HALO_NET_HOST_PLATFORM_LIMITS` | `true` | Keep the game to the players its Delta machines' platforms take (an original Xbox: 16, unless its player opted out); `false` ignores them, for testing. |
| `HALO_NET_BLOCK_MOUSE_AIM_ASSIST` | `false` | The server's policy on aiming assist for the mouse and keyboard: `false` leaves each player's own setting (`input.mouse_aim_assist`); `true` turns the view's magnetism off for players aiming with the mouse, in every game of the server. Clients are told when they join and when it changes (network version 32). |
| `SSL_CERT_FILE` | the system's | The certificates the server checks stats.chupathingy.org's against, when they are somewhere unusual. |

Players' addresses are left out of `debug.txt` and the output, as in the
game; `HALO_LOG_ADDRESSES=1` logs them whole, for an operator chasing a
problem (the log then says so at the first one). `0`, `false`, `no` and
`off` leave it off, as for every switch here that only has to be set. Logs
written with it on hold everyone's address: delete them once the problem is
found.

## The command line

```
chupathingyce-server [--version | --help]
```

| | |
| --- | --- |
| `--version`, `-v` | Prints the version, architecture and network version, and exits. |
| `--help`, `-h` | Prints a summary of the settings, and exits. |

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
