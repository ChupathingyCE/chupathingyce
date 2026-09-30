# Server tools

The game list and the dedicated server. Both belong to the game list builds
(`configure.py --game-browser`, `HALO_GAME_BROWSER`); the other builds are
unchanged.

| | |
| --- | --- |
| `list_server.py` | The game list: hosts announce their games, players get the list, and finished games' carnage reports are kept. It runs on halo.milenko.org. |
| `src/dedicated.c` | The dedicated server, compiled into the game: a copy of the game that hosts games by itself, with no player of its own. |
| `playlists/` | The dedicated server's playlists. |
| `deploy/` | The dedicated server as a Docker container and a systemd service. |

## The dedicated server

Any game list build is a dedicated server when `HALO_DEDICATED` names a
playlist in the data folder. It then:

- has no window: it draws nothing, plays no sound or movies, and needs no
  display, so it runs on a server with no screen;
- hosts a system link game with no player of its own, which is listed on
  the game list (and joined from it, from the web page's Join button, or
  from any build that opens invite links);
- plays the playlist's entries in order: once a player has joined, the
  lobby counts down by itself; after each game, the carnage report shows
  for 20 seconds, then the next entry's lobby opens;
- plays a team entry's next entry without teams while a single player
  waits (a team game needs a player on each team);
- joins no invites and leaves the clipboard alone;
- stops, and withdraws its game from the list, on SIGTERM or SIGINT.

The settings, as environment variables:

| Variable | Default | |
| --- | --- | --- |
| `HALO_DEDICATED` | (none: not a dedicated server) | The playlist, in the data folder (`playlists/free_for_all.txt`). |
| `HALO_DEDICATED_NAME` | `Dedicated` | The game's name on the lists (at most 15 characters). |
| `HALO_DEDICATED_MINIMUM_PLAYERS` | `1` | The players the countdown waits for. |
| `HALO_DEDICATED_MAXIMUM_PLAYERS` | `12` | The players the game takes. |
| `HALO_NET_BROWSER` | `https://halo.milenko.org` | The game list it announces to. |

A playlist has one entry a line: a map (its name, `bloodgulch`, or its
path) and a game type (`slayer`, `team_slayer`, `ctf`, `king`, `oddball`,
`race`, ...). `#` starts a comment.

To run one on a desktop:

```
python3 configure.py --game-browser && ninja
HALO_DEDICATED=playlists/free_for_all.txt HALO_DEDICATED_NAME="My Server" build/linux/halo
```

with the playlist copied to the data folder's `playlists`.

## Deploying the dedicated server

`deploy/` runs the 32-bit Linux game in a Debian i386 container (the host
needs Docker, not 32-bit libraries), as the `halo-dedicated` systemd service.

1. Build the Linux game with the game list, on Debian 13 (its libraries are
   the container's):
   `python3 configure.py --game-browser --portable --release && ninja linux`.
2. Copy the maps to the host's `/opt/halo-dedicated/data/maps`: `ui.map` and
   the multiplayer maps (about 300 MB).
3. Run `server/deploy/deploy.sh user@host build/linux/halo`. It copies the
   game and the playlists, builds the image, and installs and starts the
   service.

The settings are in `/opt/halo-dedicated/dedicated.env` on the host (from
`deploy/dedicated.env` the first time); after a change,
`sudo systemctl restart halo-dedicated`. The game's log is
`/opt/halo-dedicated/data/debug.txt`, the service's
`journalctl -u halo-dedicated`.
