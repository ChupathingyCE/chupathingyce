# Settings

The server is set up with environment variables, one per setting. (A
settings file of its own may come later; see the [changelog](../CHANGELOG.md).)

## The server

| Variable | Default | What it does |
| --- | --- | --- |
| `HALO_DEDICATED` | (none) | The playlist, a path inside the data folder: `playlists/free_for_all.txt`. Required: without it the server only says how to start one. |
| `HALO_DEDICATED_NAME` | `Dedicated` | The server's name on the lists. 15 characters at most, the game's limit. |
| `HALO_DEDICATED_MINIMUM_PLAYERS` | `1` | The players a game waits for before its countdown starts. |
| `HALO_DEDICATED_MAXIMUM_PLAYERS` | `12` | The players a game takes, up to 128 (see below). |
| `HALO_DEDICATED_IDLE_LIMIT` | `5` | A game in which nobody scores for this many minutes ends. `0`: never. |
| `HALO_DEDICATED_PUBLIC` | `true` | A public game, listed in every in-game Server Browser (OpenCE's and ChupathingyCE's) through internet play's brokers. `false`: not listed there; the game list still lists it, and its invite still works. |
| `HALO_DEDICATED_COMMANDS` | (none) | A file of [commands](admin.md#startup-commands) in the data folder, run once the server first hosts. |
| `HALO_DEDICATED_CONSOLE` | on in a terminal | Commands typed on the server's standard input ([the console](admin.md#the-console)). `true`: read them even when it is not a terminal; `false`: never. |
| `HALO_DEDICATED_CONTROL` | (off) | The [control API](admin.md#the-control-api)'s address: a port (`8080`, on 127.0.0.1 only) or an address and port (`127.0.0.1:8080`, `[::1]:8080`; any other is warned of). Its token is made and printed the first time. The [web admin page](admin.md#the-web-admin-page) is on the same address. |

A game everyone has left ends after 30 seconds. After each game the
carnage report shows for 20 seconds, then the next entry's lobby opens.

## Folders

| Variable | Default | What it does |
| --- | --- | --- |
| `HALO_DATA_ROOT` | the current folder, else the program's | The data folder: `maps/` (with `ui.map`), `maps/ce/`, `md_maps/`, `playlists/`. The server's log, `debug.txt`, goes here. |
| `HALO_SAVE_ROOT` | `~/.local/share/halo-linux` | Where the server keeps its saves (the game's profile and scratch files, about 33 MB). Give each server on a machine its own. |

The server also writes `config.toml`, its settings at their defaults, beside
the program if it can (an image's or a system folder's program cannot, which
is fine). Settings in the environment override it.

## The network

| Variable | Default | What it does |
| --- | --- | --- |
| `HALO_NET_BROWSER` | `https://halo.milenko.org` | The game list the server announces to; empty for none. |
| `HALO_NET_LIST_GAMES` | `true` | `false`: not listed on the game list (invites and the Server Browser still work). |
| `HALO_NET_ONLINE` | `true` | Internet play: how players outside your network reach the server. `false` keeps it to the local network. |
| `HALO_NET_TUNNEL_PORT` | `0` (any) | The UDP port internet play uses. A fixed one can be forwarded, for networks whose NAT stops connections. |
| `HALO_NET_ALLOW_UPNP` | `true` | Let internet play ask the router to forward its port (UPnP). |
| `HALO_NET_BROKERS_FILE` | `brokers.txt` | The file of internet play's MQTT brokers (one `host:port` on each line), beside the program unless a full path. Without one, the server uses the game's own list, the same as `port/assets/network/brokers.txt`. |
| `HALO_LEGACY_TABLE` | (none) | A legacy table file, not signed, used in place of the signed ones: its row for the build's wire sets the OpenCE network versions the server announces and its range. For tests, and for an admin who knows better; the log warns at start, and the server neither fetches nor passes on signed tables meanwhile ([Delta](../../docs/delta.md#local-override)). |
| `HALO_NET_ADDRESS` | (any) | The IPv4 address system link uses. Several servers on one machine each need their own: `127.0.0.2`, `127.0.0.3`, ... ([Docker](docker.md#more-servers-on-the-same-host)). |
| `HALO_NET_BROADCAST` | (the local network's) | Comma-separated addresses system link announces games to, instead of the local network's broadcast. |
| `HALO_NET_PROTOCOL` | `auto` | Delta Peer ([docs/delta.md](../../docs/delta.md)), ChupathingyCE's messages beside OpenCE's protocol, on UDP port 5160 beside the game's 5150: `auto` speaks it with the machines that do (the rest play plain OpenCE); `opence` turns it off; `delta` is `auto` for now. |
| `HALO_NET_HOST_PLATFORM_LIMITS` | `true` | Keep the game to the players its Delta machines' platforms take (an original Xbox: 16, unless its player opted out); `false` ignores them, for testing. |
| `SSL_CERT_FILE` | the system's | The certificates the server checks halo.milenko.org's against, when they are somewhere unusual. |

Players' addresses are left out of `debug.txt` and the output, as in the
game; `HALO_LOG_ADDRESSES=1` logs them whole, for an operator chasing a
problem.

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

In the data folder: `debug.txt` (its log), `bans.txt` (its bans, read on
every join: [admin.md](admin.md#bans)), `cheaters.txt` (players dropped for
cheating), and `control_credentials.txt` (the control API's token's hash,
once the API is turned on).

## Probing a game

With `HALO_PROBE` holding an invite (the digits after `halo://join/`), the
server is instead the game list's probe: it reads the game the invite leads
to, prints it as one line and exits, without joining it. See
[docker.md](docker.md#the-game-lists-probe).
