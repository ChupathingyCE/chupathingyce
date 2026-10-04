# Dedicated servers

A dedicated server is a copy of the game that hosts games by itself, with
no player of its own, around the clock. Anyone can run one: it lists its
games on [halo.milenko.org](https://halo.milenko.org), the community's game
list, where players find and join them (from the game's ONLINE PLAY, or
from the site's Join buttons), and where its finished games are kept as
carnage reports.

| | |
| --- | --- |
| `src/dedicated.c` | The dedicated server, compiled into the game (the game browser builds, `HALO_GAME_BROWSER`, on by default). |
| `src/probe.c` | The game list's probe: what an invite leads to (below). |
| `playlists/` | Playlists: `small_maps.txt` (Slayer on the smaller maps), `team_slayer.txt` (Team Slayer on every map), `big_maps.txt` (Slayer on the roomier maps, for 32 players), `bloodgulch.txt` (Blood Gulch, Team Slayer and Slayer, for 128), `free_for_all.txt` (Slayer on every map), `slayer.txt` (Slayer and Team Slayer), `gearbox.txt` (Halo PC's own maps, Slayer and Team Slayer in turn: needs them in `maps/ce/`). |
| `deploy/` | The server as a Docker container and a systemd service, for a Linux host. |

## What it does

Any game browser build is a dedicated server when `HALO_DEDICATED` names a
playlist in the data folder. It then:

- has no window: it draws nothing, plays no sound or movies, and needs no
  display, so it runs on a server with no screen (about 3% of a CPU core
  and 70 MB while it waits);
- hosts a system link game with no player of its own, listed on the game
  list (any build that opens invite links can join it);
- is a public game (unless `HALO_DEDICATED_PUBLIC` is `false`): its signed
  listing goes out through internet play's brokers, so it shows in the
  in-game Server Browser (Join Game > Server Browser) of OpenCE and
  ChupathingyCE alike;
- plays the playlist's entries in order: once a player has joined, the
  lobby counts down by itself; after each game, the carnage report shows
  for 20 seconds, then the next entry's lobby opens;
- ends a game nobody has scored in for 5 minutes, or 30 seconds after
  everyone has left it;
- plays a team entry's next entry without teams while a single player
  waits (a team game needs a player on each team);
- joins no invites and leaves the clipboard alone;
- stops, and withdraws its game from the list and the Server Browser, on
  SIGTERM or SIGINT.

## Settings

As environment variables:

| Variable | Default | |
| --- | --- | --- |
| `HALO_DEDICATED` | (none: not a dedicated server) | The playlist, in the data folder (`playlists/free_for_all.txt`). |
| `HALO_DEDICATED_NAME` | `Dedicated` | The game's name on the lists (at most 15 characters). |
| `HALO_DEDICATED_MINIMUM_PLAYERS` | `1` | The players the countdown waits for. |
| `HALO_DEDICATED_MAXIMUM_PLAYERS` | `12` | The players the game takes. |
| `HALO_DEDICATED_IDLE_LIMIT` | `5` | A game in which nobody scores for this many minutes ends. `0`: never. |
| `HALO_DEDICATED_PUBLIC` | `true` | A public game, listed in every in-game Server Browser (OpenCE's and ChupathingyCE's) through internet play's brokers. `false`: not listed there (the game list still lists it, and its invite still works). |
| `HALO_NET_BROWSER` | `https://halo.milenko.org` | The game list it announces to. |

A playlist has one entry a line: a map (its name, `bloodgulch`, its
path, a Halo PC map in the data folder's `maps/ce/` as `<name>@ce`,
`timberland@ce`, or a HaloMD map in the data folder's `md_maps/` as
`<name>@md`, `bgplus_5@md`) and a game type (`slayer`, `team_slayer`, `ctf`,
`king`, `oddball`, `race`, ...). `#` starts a comment. A HaloMD map needs
Custom Edition's `bitmaps.map`, `sounds.map` and `loc.map` in `maps/ce/`, as
the Halo PC maps do.

## Which build

The 32-bit game (`ninja linux`) is the one for servers of the Xbox maps:
it uses less memory. The 64-bit game (`ninja linux64`) is the one for Halo
PC, Custom Edition and HaloMD maps, which need its larger caches. The
releases ship both: `chupathingyce-dedicated` (32-bit) and
`chupathingyce-dedicated64` (64-bit).

## Run one on a desktop

Build the game (the README at the top), copy a playlist into the data
folder's `playlists/`, and start it:

```
HALO_DEDICATED=playlists/free_for_all.txt HALO_DEDICATED_NAME="My Server" build/linux/halo
```

(or `build/macos/halo` on a Mac). Keep it running; it shows on
halo.milenko.org within a few seconds.

## Run one on a Linux server

`deploy/` runs the Linux game in a Debian container (the host needs
Docker, not the game's libraries), as the `halo-dedicated` systemd
service: the 64-bit game (`ninja linux64`) in a Debian amd64 container, or
the 32-bit game (`ninja linux`) in a Debian i386 one (above: the 32-bit
game for the Xbox maps, the 64-bit one for Halo PC and HaloMD maps).
`deploy.sh` picks the container from the game it is given (the
Dockerfile's `BASE`).

1. Build the Linux game on Debian 13 (its libraries are the container's):
   `python3 configure.py --portable --release && ninja linux64`
   (`build/linux64/halo`), or `ninja linux` for the 32-bit game
   (`build/linux/halo`).
2. Copy the maps to the host's `/opt/halo-dedicated/data/maps`: `ui.map`
   and the multiplayer maps (about 300 MB). Use the North American (NTSC)
   maps. Halo PC maps and their `bitmaps.map`, `sounds.map` and `loc.map`
   go in `maps/ce/` there, HaloMD maps in `/opt/halo-dedicated/data/md_maps`.
3. Run `server/deploy/deploy.sh user@host build/linux64/halo`. It copies the
   game and the playlists, builds the image, and installs and starts the
   service.

The settings are in `/opt/halo-dedicated/dedicated.env` on the host (from
`deploy/dedicated.env` the first time); after a change,
`sudo systemctl restart halo-dedicated`. The game's log is
`/opt/halo-dedicated/data/debug.txt`, the service's
`journalctl -u halo-dedicated`.

The server needs no open ports: internet play reaches players through the
same hole punching as any host's invite.

### More servers on the same host

Each further server is the `halo-dedicated@<name>` service: the same image,
its settings in `deploy/instances/<name>.env` (`team.env`: Team Slayer on
every map), its own data folder `/opt/halo-dedicated/instances/<name>`
(saves, `debug.txt`), and the first server's maps and playlists. All play on
the host's network (hole punching does not get through a bridge's NAT to
players behind their own), each with system link on a loopback address of
its own (`HALO_NET_ADDRESS`: 127.0.0.2 the first, 127.0.0.3 the team
server, 127.0.0.4 `max.env`'s 32-player Slayer, 127.0.0.5 `bloodgulch.env`'s
128-player Blood Gulch), since two cannot share its port on one address.
After `deploy.sh`, run `server/deploy/deploy-instance.sh user@host team`.
Its log is `journalctl -u halo-dedicated@team`.

## Probing a game

Any game browser build is also the game list's **probe** when `HALO_PROBE`
holds an invite (the digits after `halo://join/`). It reads the game that
invite leads to, the way a joining player's copy sees it advertised, prints
it as one line and quits, without joining the game or taking a place in it.
halo.milenko.org uses it to list games hosted by copies without the game
list: a signed-in player gives their invite, and the site checks it before
listing it and while it is listed.

```
HALO_PROBE=068f5721cffe... build/linux/halo
probe: {"ok": true, "name": "Milenko Slayer", "map": "chillout", "engine": "slayer", "players": 0, "maximum_players": 12, "open": true, "teams": false, "network_version": 10, "compatible": true}
```

It needs only `maps/ui.map` in the data folder (and about 33 MB for its
saves, the game's scratch drive), runs without a window as the dedicated
server does, and takes about 3 seconds. A host that does not
answer in 20 seconds gives `{"ok": false, "error": "no answer from the host"}`
(exit status 1).

`deploy/deploy-probe.sh user@host build/linux/halo "<site's SSH key>"` puts it
on a dedicated server's host beside the server, which it leaves as it is:
a `halo-probe` image (the same Dockerfile), `probe.sh`, which runs one probe
in a container of its own that goes when it is done, and a `probe` user
whose key may only ask for a probe (`probe-ssh.sh`). halo.milenko.org runs
its probes there.

## The game list

halo.milenko.org is run by Milenko for the community. It keeps the list of
games being hosted, the carnage reports of finished games, players' service
records and profiles. Its code is not in this repository. Please be kind to
it: one listing per game, as the game does by itself.
