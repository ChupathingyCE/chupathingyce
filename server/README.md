# ChupathingyCE Dedicated Server

A Halo: Combat Evolved server for Linux. It hosts games by itself, with no
player of its own, around the clock, from a playlist of maps and game types.
Its games show on [halo.milenko.org](https://halo.milenko.org), the
community's game list, and in the in-game Server Browser of ChupathingyCE
and OpenCE alike. Finished games get carnage reports there.

It is the game itself (the same code, network protocol and version as the
ChupathingyCE release it comes with), built as a program of its own with
nothing a player sits in front of: no window, no sound, no controller. It is
one file, with no libraries to install, and it runs on any Linux of its
architecture.

You don't need to open or forward any ports. Players reach your server the
same way they reach anyone's invite link, even behind a home router.

## Which download

Each [release](https://github.com/ChupathingyCE/chupathingyce/releases/latest)
has the server for three kinds of machine. Run `uname -m` on yours if you're
not sure.

| Download | `uname -m` | For |
| --- | --- | --- |
| `chupathingyce-server-linux-x64` | `x86_64` | Most VPSes and PCs: Intel and AMD 64-bit. |
| `chupathingyce-server-linux-arm64` | `aarch64` | 64-bit ARM: Oracle Cloud's free tier (Ampere A1), other Ampere and Graviton servers, Raspberry Pi 4 and 5 with a 64-bit OS. |
| `chupathingyce-server-linux-x86` | `i686`, `i386` | Older 32-bit PCs. It runs on a 64-bit x86 Linux too, and uses a little less memory there. |

Halo PC (Custom Edition) and HaloMD maps: use the x64 or arm64 server. The
x86 one is for the Xbox maps (it can load the others, with less room for
their textures and sounds).

A server waiting for players uses about 2% of one CPU core, and about 85 MB
of memory (x86) or 215 MB (x64 and arm64, whose caches are larger), so a
small VPS will do.

## Quick start

1. Download the server for your machine and unpack it. You get
   `chupathingyce-server`, a `playlists` folder and this README.
2. Make a data folder with a `maps` folder in it, and copy in the game's
   `ui.map` and the multiplayer maps (all of them: about 300 MB). Use the
   maps of the North American (NTSC) Xbox disc, as the players do. Halo PC
   maps go in `maps/ce`, HaloMD maps in `md_maps` ([Playlists](docs/playlists.md)).
3. Put a playlist in the data folder's `playlists` folder (the download's,
   or your own), and start the server:

```sh
cd /path/to/data
HALO_DEDICATED=playlists/free_for_all.txt HALO_DEDICATED_NAME="My Server" \
  /path/to/chupathingyce-server
```

The server prints what it is doing; leave it running. It shows on
halo.milenko.org within a few seconds. Ctrl+C stops it, and takes its game
off the lists as it goes.

The data folder is the current folder, unless `HALO_DATA_ROOT` says
otherwise. The server writes its log, `debug.txt`, there, and its saves in
`~/.local/share/halo-linux` unless `HALO_SAVE_ROOT` says otherwise
([Settings](docs/settings.md)).

To keep a server running all the time, with restarts and several servers on
one machine, use [Docker or systemd](docs/docker.md).

## What it does

- Hosts a system link game with no player of its own, listed on the game
  list. Any build that opens invite links can join it.
- Is a public game (unless `HALO_DEDICATED_PUBLIC` is `false`): it shows in
  the in-game Server Browser (Join Game > Server Browser) of OpenCE and
  ChupathingyCE.
- Plays the playlist's entries in order: once enough players have joined,
  the lobby counts down by itself; after each game the carnage report shows
  for 20 seconds, then the next entry's lobby opens.
- Ends a game nobody has scored in for 5 minutes, or 30 seconds after
  everyone has left it.
- Plays a team entry's next entry without teams while a single player waits
  (a team game needs a player on each team).
- Stops on SIGTERM or SIGINT, and withdraws its game from the lists.
- Takes commands, named after Halo PC's (`sv_players`, `sv_kick`, `sv_ban`,
  `sv_map`, `sv_mapcycle_next`, ...): typed on its console, from a startup
  file, or through its control API (HTTP and JSON, off unless turned on,
  with a token of its own) and the web admin page on the same port. See
  [docs/admin.md](docs/admin.md).

## Who can join

Players need a build of the same network version: the ChupathingyCE release
the server comes from (or any other of that network version), or OpenCE
builds of that network version. `chupathingyce-server --version` prints it.
A player with another version sees a message saying which version each side
is on.

## More

| | |
| --- | --- |
| [docs/settings.md](docs/settings.md) | Every setting, the command line, exit statuses, and the files the server writes. |
| [docs/admin.md](docs/admin.md) | Running a server: its commands, its console, startup commands, the control API and web admin page (and reaching them safely). |
| [docs/playlists.md](docs/playlists.md) | Playlists: the maps (Xbox, `@ce`, `@md`), the game types, and the ones included. |
| [docs/docker.md](docs/docker.md) | The container image, the systemd services, more servers on one host, and the game list's probe. |
| [docs/building.md](docs/building.md) | Building the server: the targets, musl and glibc, and how it differs from the game. |
| [CHANGELOG.md](CHANGELOG.md) | What changed in the server, release by release. |

In this folder: `src/` is the dedicated server's director, compiled into the
game (`dedicated.c`) and the game list's probe (`probe.c`); `platform/` is
the server's own platform layer, with no window, input or sound; `playlists/`
the playlists; `deploy/` the container and services.

## If something's wrong

- **It stops at once with "no maps".** The data folder needs
  `maps/ui.map`. Start the server from the data folder, or set
  `HALO_DATA_ROOT` to it.
- **"cannot read the playlist".** `HALO_DEDICATED` is a path inside the data
  folder: `playlists/my_playlist.txt`, not a path from elsewhere.
- **It doesn't show on the site.** The log is `debug.txt` in the data folder.
  The server needs to reach the internet (HTTPS to halo.milenko.org, and UDP).
- **It shows, but nobody can join.** The player probably has another
  version; the message they see says which.
- **It sits in the lobby and never starts.** Check that every multiplayer
  map is in `maps`, not just the playlist's. With
  `HALO_DEDICATED_MINIMUM_PLAYERS` above 1, it waits for that many.
- **"cannot reserve the Xbox address space"** (arm64). The kernel gives the
  program fewer addresses than it needs (below 39 bits). Every 64-bit
  Raspberry Pi OS, Ubuntu and Oracle Linux kernel has enough; tell us which
  system this is.

## The game list

halo.milenko.org is run by Milenko for the community. It keeps the list of
games being hosted, the carnage reports of finished games, players' service
records and profiles. Its code is not in this repository. Please be kind to
it: one listing per game, as the server does by itself.
