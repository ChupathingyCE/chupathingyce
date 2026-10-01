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
| `playlists/` | Playlists: `free_for_all.txt` (Slayer on every map), `slayer.txt` (Slayer and Team Slayer). |
| `deploy/` | The server as a Docker container and a systemd service, for a Linux host. |

## What it does

Any game browser build is a dedicated server when `HALO_DEDICATED` names a
playlist in the data folder. It then:

- has no window: it draws nothing, plays no sound or movies, and needs no
  display, so it runs on a server with no screen (about 3% of a CPU core
  and 70 MB while it waits);
- hosts a system link game with no player of its own, listed on the game
  list (any build that opens invite links can join it);
- plays the playlist's entries in order: once a player has joined, the
  lobby counts down by itself; after each game, the carnage report shows
  for 20 seconds, then the next entry's lobby opens;
- ends a game nobody has scored in for 5 minutes, or 30 seconds after
  everyone has left it;
- plays a team entry's next entry without teams while a single player
  waits (a team game needs a player on each team);
- joins no invites and leaves the clipboard alone;
- stops, and withdraws its game from the list, on SIGTERM or SIGINT.

## Settings

As environment variables:

| Variable | Default | |
| --- | --- | --- |
| `HALO_DEDICATED` | (none: not a dedicated server) | The playlist, in the data folder (`playlists/free_for_all.txt`). |
| `HALO_DEDICATED_NAME` | `Dedicated` | The game's name on the lists (at most 15 characters). |
| `HALO_DEDICATED_MINIMUM_PLAYERS` | `1` | The players the countdown waits for. |
| `HALO_DEDICATED_MAXIMUM_PLAYERS` | `12` | The players the game takes. |
| `HALO_DEDICATED_IDLE_LIMIT` | `5` | A game in which nobody scores for this many minutes ends. `0`: never. |
| `HALO_NET_BROWSER` | `https://halo.milenko.org` | The game list it announces to. |

A playlist has one entry a line: a map (its name, `bloodgulch`, or its
path) and a game type (`slayer`, `team_slayer`, `ctf`, `king`, `oddball`,
`race`, ...). `#` starts a comment.

## Run one on a desktop

Build the game (the README at the top), copy a playlist into the data
folder's `playlists/`, and start it:

```
HALO_DEDICATED=playlists/free_for_all.txt HALO_DEDICATED_NAME="My Server" build/linux/halo
```

(or `build/macos/halo` on a Mac). Keep it running; it shows on
halo.milenko.org within a few seconds.

## Run one on a Linux server

`deploy/` runs the 32-bit Linux game in a Debian i386 container (the host
needs Docker, not 32-bit libraries), as the `halo-dedicated` systemd
service.

1. Build the Linux game on Debian 13 (its libraries are the container's):
   `python3 configure.py --portable --release && ninja linux`.
2. Copy the maps to the host's `/opt/halo-dedicated/data/maps`: `ui.map`
   and the multiplayer maps (about 300 MB). Use the North American (NTSC)
   maps.
3. Run `server/deploy/deploy.sh user@host build/linux/halo`. It copies the
   game and the playlists, builds the image, and installs and starts the
   service.

The settings are in `/opt/halo-dedicated/dedicated.env` on the host (from
`deploy/dedicated.env` the first time); after a change,
`sudo systemctl restart halo-dedicated`. The game's log is
`/opt/halo-dedicated/data/debug.txt`, the service's
`journalctl -u halo-dedicated`.

The server needs no open ports: internet play reaches players through the
same hole punching as any host's invite.

## The game list

halo.milenko.org is run by Milenko for the community. It keeps the list of
games being hosted, the carnage reports of finished games, players' service
records and profiles. Its code is not in this repository. Please be kind to
it: one listing per game, as the game does by itself.
