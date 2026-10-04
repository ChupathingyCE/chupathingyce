# Running a dedicated server

A dedicated server is a copy of ChupathingyCE that hosts games on its own,
with nobody playing on it. It runs through a list of maps and game types
(a playlist), starts each game when players join, and moves on to the next
one when it ends.

Your server shows up on [halo.milenko.org](https://halo.milenko.org) and in
the game's Online Games list by itself, a few seconds after it starts.
Finished games get carnage reports there like any other.

You don't need to open or forward any ports. Players reach your server the
same way they reach anyone's invite link, even if it's behind a home router.

## What you need

- ChupathingyCE, set up and working: you've started it once and it found
  your maps. The server uses the same maps, and needs all of the
  multiplayer maps.
- A computer that stays on while the server runs. It doesn't need a screen
  or a graphics card: an idle server uses about 3% of one CPU core and
  70 MB of memory.

## 1. Make a playlist

Find the folder with your `maps` folder in it:

- **Windows and Linux:** the ChupathingyCE folder, next to the program.
- **Mac:** `~/Library/Application Support/ChupathingyCE` (in Finder: Go,
  Go to Folder, and paste that in).

Not sure? When the game starts it prints a line like `data root: ...`:
that's the folder.

Make a folder called `playlists` next to `maps`, and in it a text file
called `my_playlist.txt`:

```
# map            game type
bloodgulch       slayer
prisoner         slayer
damnation        team_slayer
chillout         slayer
```

One game per line: the map, then the game type. Lines starting with `#`
are ignored. The server plays them in order and starts over at the end.

**Maps:** `beavercreek` (Battle Creek), `bloodgulch`, `boardingaction`,
`carousel` (Derelict), `chillout`, `damnation`, `hangemhigh`, `longest`,
`prisoner`, `putput` (Chiron TL-34), `ratrace`, `sidewinder`, `wizard`.

**Game types:** `slayer`, `team_slayer`, `ctf`, `ironctf`, `king`,
`team_king`, `oddball`, `team_oddball`, `race`, `team_race`, `rally`,
`elimination`, `stalker`, `accumulation`, `infection`.

A team game needs at least two players. While only one player is waiting,
the server skips ahead to the next game in your playlist that isn't a team
game. If there isn't one, that player waits for a second.

## 2. Start it

The server is the normal game, started with a few settings. Pick a name
(15 characters at most, the game's limit) and run:

**Windows** (Command Prompt, in the ChupathingyCE folder):

```
set HALO_DEDICATED=playlists\my_playlist.txt
set HALO_DEDICATED_NAME=My Server
halo.exe
```

**Mac** (Terminal):

```
HALO_DEDICATED=playlists/my_playlist.txt HALO_DEDICATED_NAME="My Server" \
  /Applications/ChupathingyCE.app/Contents/MacOS/halo
```

**Linux** (in the ChupathingyCE folder):

```
HALO_DEDICATED=playlists/my_playlist.txt HALO_DEDICATED_NAME="My Server" ./halo
```

No window opens: the server just runs and prints what it's doing. Leave it
running. Check [halo.milenko.org](https://halo.milenko.org) after a few
seconds and you should see your server.

To stop it, press Ctrl+C. It takes its game off the list as it closes.

## Settings

Set these the same way as `HALO_DEDICATED` above.

| Setting | Default | What it does |
| --- | --- | --- |
| `HALO_DEDICATED` | (none) | Your playlist, inside the data folder. Without it, the game starts normally. |
| `HALO_DEDICATED_NAME` | `Dedicated` | The server's name on the lists. 15 characters at most. |
| `HALO_DEDICATED_MINIMUM_PLAYERS` | `1` | How many players a game waits for before it starts. |
| `HALO_DEDICATED_MAXIMUM_PLAYERS` | `12` | How many players a game takes, up to 128. |
| `HALO_DEDICATED_IDLE_LIMIT` | `5` | A game where nobody scores for this many minutes ends. `0` means never. |

A game everyone has left ends after 30 seconds. After each game, the
carnage report shows for 20 seconds, then the next game's lobby opens.

## Bots

A server can have bots: players the server drives itself, so a game is
worth joining before anyone else is there.

| Setting | Default | What it does |
| --- | --- | --- |
| `HALO_BOTS` | `0` | This many bots in every game. |
| `HALO_BOTS_FILL` | `0` | Fill every game up to this many players with bots. A bot leaves when a person joins, and comes back when one leaves. `0` means `HALO_BOTS` bots, whoever joins. |
| `HALO_BOT_SKILL` | `3` | How good they are: `1` easy to `5` near perfect. |

The bots come in with the first person to join: the server doesn't play
games of bots alone, and a game ends 30 seconds after the last person
leaves it, bots or not. With bots, one person is enough for a team game.

A game the bots played in gets no carnage report on the site: the stats
there are for people.

## Infection

`infection` is a game type of its own: everyone starts alive, one player
turns into a zombie, and whoever a zombie kills comes back as one. It needs
two players (bots count).

The zombies are the Flood from the campaign. For that, the server and the
players need one of the campaign maps `c20`, `d20`, `c40`, `c10` or `d40`
in their `maps` folder, next to the multiplayer maps. If anyone in the game
doesn't have one, or plays a build without infection (OpenCE, or
ChupathingyCE's own releases), the zombies are ordinary players with no
weapon instead, so that everyone sees the same thing. Players on a build without
infection can still play, but their game doesn't know who is on which side:
it shows them a team game with the teams they picked in the lobby.

On a Halo PC map (`<name>@ce`) the zombies are always ordinary players with
no weapon: the Flood is only added to the Xbox's maps.

Infection games get no carnage report on the site.

`HALO_INFECTION_ROUNDS` (3), `HALO_INFECTION_ROUND_MINUTES` (4) and
`HALO_INFECTION_COUNTDOWN` (10 seconds) set the length of a game.

## Who can join

Players need a build with the same network version: ChupathingyCE 0.5.1b
or newer, or OpenCE build-73 or newer. Players can also join from the
site's Join button, or with your server's invite link, which the server
prints when it starts.

## Running one all the time

For a server that runs around the clock on a Linux machine or VPS, with
restarts handled for you and several servers on one machine, see
[server/README.md](../server/README.md). It's how the `[D]` servers on
halo.milenko.org run.

## If something's wrong

- **It doesn't show on the site.** Make sure the playlist path is right:
  it's relative to the folder with `maps` in it. The server's log is
  `debug.txt` in that same folder.
- **It shows, but nobody can join.** The player probably has an older
  version. They'll see a message saying which version each side is on.
- **It sits in the lobby and never starts a game.** Check that you have
  every multiplayer map, not just the ones in your playlist.
