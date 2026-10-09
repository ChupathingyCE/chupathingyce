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

On a home connection you don't need to open or forward any ports. Players
reach your server the same way they reach anyone's invite link, even behind a
home router. On a cloud server (Oracle, AWS, Google, Azure and the like), open
one UDP port: see "It shows, but nobody can join" below.

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
   maps go in folders beside `maps`: Custom Edition maps (with Custom
   Edition's `bitmaps.map`, `sounds.map` and `loc.map`) in `maps_ce`,
   HaloMD maps in `maps_md`, Halo PC retail maps in `maps_pc`
   ([Playlists](docs/playlists.md)).
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
  file, or through its control API (off unless turned on) and the control
  panel on the same port. See [docs/admin.md](docs/admin.md).
- Has moderators, admins and owners of its own, with accounts, a second
  factor, in-game moderation and an audit file
  ([docs/moderation.md](docs/moderation.md)).
- Makes and edits playlists, game types and its settings while it runs
  ([docs/playlists.md](docs/playlists.md)).

## Run your own server: moderators and the control panel

Everything here works without halo.milenko.org. Details, the roles table
and the security notes: [docs/moderation.md](docs/moderation.md).

1. **Turn on the control panel.** `HALO_DEDICATED_CONTROL=8080` keeps it on
   this machine (reach it with `ssh -N -L 8080:127.0.0.1:8080 you@server`);
   `HALO_DEDICATED_CONTROL=0.0.0.0:8443` opens it to the network, HTTPS
   only. The server prints the certificate's SHA-256 fingerprint: check the
   browser shows the same, once.
2. **Make the owner's account.** The first start prints a one-time setup
   link (on the console, not in the log). Open it, choose a name and a
   password, log in, and set up a second factor in My account (any
   authenticator app).
3. **Invite your moderators.** People, then Make an invitation: send the
   link privately. Each person chooses their own password and second
   factor. People can require a second factor of everyone.
4. **Moderators in the game.** A moderator binds their account to their
   ChupathingyCE game (My account: join, then Ask my game), or you add them
   by their moderator key: `sv_mod_add <player> moderator` on the console,
   once they have signed in from the game's Moderation screen, or a line in
   `moderators.txt`. They then warn, kick and ban from the game.
5. **Back up** `control_accounts.txt`, `moderators.txt` and the rest of the
   data folder's `control_*` files, privately: they hold the accounts'
   password hashes and second factors.

Roles: a **moderator** warns, kicks and bans for up to 7 days; an
**admin** also bans for longer, unbans, changes maps, playlists and
settings, and invites moderators; an **owner** also manages roles. The
console and control tokens act as the owner. Every change is checked
against its role wherever it came from, limited per person, and written to
`control_audit.log` (who, what, whom, why, when; never addresses).

**Security notes.** Off by default: no panel, no API, no moderators, no
link. On the loopback address unless told otherwise; HTTPS beyond it.
Passwords as Argon2id hashes, guessing slowed per address and per account,
codes good once, sessions in HttpOnly SameSite cookies with CSRF tokens.
Moderators are known by a key their game proves, never by a name or
hardware id. Linking to halo.milenko.org is optional (`sv_link`), outbound
only, capped at admin, and undone at once with `sv_unlink`.

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
| [docs/moderation.md](docs/moderation.md) | Moderators and the control panel: roles, accounts, second factors, invitations, in-game moderation, HTTPS, the link to halo.milenko.org, and security notes. |
| [docs/playlists.md](docs/playlists.md) | Playlists: the maps (Xbox, `@ce`, `@md`), the game types, and the ones included. |
| [docs/docker.md](docs/docker.md) | The container image, the systemd services, more servers on one host, and the game list's probe. |
| [docs/building.md](docs/building.md) | Building the server: the targets, musl and glibc, and how it differs from the game. |
| [CHANGELOG.md](CHANGELOG.md) | What changed in the server, release by release. |

In this folder: `src/` is compiled into the game: the dedicated server's
director (`dedicated.c`), its commands (`server_commands.c`), the command
line (`command_line.c`) and the game list's probe (`probe.c`); `platform/` is
the server's own platform layer, with no window, input or sound, and its
console, control API and web admin page; `webui/` that page's files;
`tests/` the control API's tests; `docs/` the server's documents;
`playlists/` the playlists; `deploy/` the container and services.

## Stats (Delta Stats)

A server can send each game's events to halo.milenko.org when it ends, for
the site's match pages, heatmaps, records and leaderboards: kills (weapon,
how, where the killer and victim were), shots and hits by weapon, medals and
sprees, flag, ball, hill and lap events, vehicle rides, weapon and powerup
pickups, spawns, joins and leaves, each player's position every 2 seconds,
ping, and the server's own minute (frame time, CPU, memory). Players are
their names and a keyed hash of their hardware ID. No address of anyone is
ever sent. Co-op games are never recorded.

It is on by default; set `HALO_NET_REPORT_EVENTS=false` to turn it off:

| Setting | Default | |
| --- | --- | --- |
| `HALO_NET_REPORT_EVENTS` | `true` | `false` stops recording and sending games. |
| `HALO_EVENTS_TOKEN` | (none) | A token from the site's operator: the games count as a trusted server's. Without one, the site takes a game only while the server lists it, from the same address (as the carnage report). |
| `HALO_EVENTS_POSITIONS` | `2` | Seconds between position samples (0 none). |
| `HALO_EVENTS_LIMIT` | `40000` | The most events a game keeps (about 60 bytes each); past it the position samples thin out first. |
| `HALO_EVENTS_PART_MINUTES` | `30` | A long game is also sent as it stands this often. |
| `HALO_EVENTS_FOLDER` | (none) | A folder to keep your own copy of each batch (JSON). |

The log takes at most 2.4 MB of memory (the default limit), and a 15-minute
game of 16 players is about a 30 KB upload (gzip); encoding and compressing
even an hour's game takes a few milliseconds. The format and the upload are in docs/delta.md, "Delta
Stats". Tell your players: the site's Delta page says what is collected,
and players can hide their stats from their profile.

## If something's wrong

- **It stops at once with "no maps".** The data folder needs
  `maps/ui.map`. Start the server from the data folder, or set
  `HALO_DATA_ROOT` to it.
- **"cannot read the playlist".** `HALO_DEDICATED` is a path inside the data
  folder: `playlists/my_playlist.txt`, not a path from elsewhere.
- **It doesn't show on the site.** The log is `debug.txt` in the data folder.
  The server needs to reach the internet (HTTPS to halo.milenko.org, and UDP).
- **It shows, but nobody can join.** If the player sees a message, it
  probably names a version mismatch. If they just can't connect (the log
  says `could not connect` or `lost the connection` for their player), the
  server's firewall is dropping the game's traffic. Cloud servers block
  incoming UDP by default, and players whose router changes ports can only
  reach a server that lets them in. Fix it in three steps:
  1. Give internet play a fixed port: `HALO_NET_TUNNEL_PORT=2302` (any
     free port). The log's `this machine's public address is ...:2302` line
     shows the port in use.
  2. Allow incoming UDP on that port in the provider's firewall: on Oracle
     Cloud, an ingress rule in the subnet's security list (or the instance's
     network security group), source `0.0.0.0/0`, protocol UDP, destination
     port 2302. Other providers call it a security group or firewall rule.
  3. Allow it on the machine too. Oracle's Ubuntu images reject everything
     but SSH in iptables: `sudo iptables -I INPUT -p udp --dport 2302 -j ACCEPT`,
     then `sudo netfilter-persistent save` to keep it. With ufw:
     `sudo ufw allow 2302/udp`.

  Internet play carries everything, Delta Peer included, over that one port.
  System link on a LAN uses UDP 5150 and 5160 instead.
- **"cannot commit the Xbox heap", then a crash** (64-bit server before
  0.7.1b, on a machine with 1 GB of memory or less). The server asked the
  system to set aside more memory than the machine has, though it uses far
  less. 0.7.1b no longer asks for that. On an older version, use the x86
  (32-bit) server, or add swap.
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
