# Running a dedicated server

The dedicated server is now its own download for Linux, for x86, x64 and
arm64 machines (Oracle Cloud's free tier and Raspberry Pis included): see
the **[ChupathingyCE Dedicated Server](../server/README.md)**, with which
download fits your machine, a quick start, playlists, settings, Docker and
systemd.

## On a desktop

The game itself (Windows, Mac or Linux) still turns into a dedicated server
when `HALO_DEDICATED` names a playlist in its data folder, with no window:
handy to try a playlist on the computer you play on. It takes the server's
[settings](../server/docs/settings.md) and
[playlists](../server/docs/playlists.md). For a server that stays up, use the
Linux server download.

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

The first game with bots on each map takes a few seconds longer to start,
while the server works out where the bots can go. To do that ahead of time
for every map, run `bake-navigation.cmd` (Windows), or start the game once
with `HALO_BAKE_NAVIGATION=all` (Linux and Mac): it writes a `navcache`
folder next to `maps` and quits. More in the
[port's README](../port/linux/README.md#baked-navigation).

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
