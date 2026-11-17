# Host a server

Run your own Chupathingy dedicated server. It hosts games around the clock
with no player of its own, and shows up on
[stats.chupathingy.org](https://stats.chupathingy.org) and in the in-game
Server Browser. Anyone can run one.

This page is the short path. The site has the same guide at
[stats.chupathingy.org/host](https://stats.chupathingy.org/host). The other
documents here go deeper.

## Before you start

You need a machine that stays on. A small VPS or a home PC is enough. A
server waiting for players uses very little CPU and about 100 to 250 MB of
memory.

You also need the game's maps. The server comes with no game files.

1. Make a data folder for the server, with a `maps` folder and a
   `playlists` folder in it.
2. Copy `ui.map` and all the multiplayer maps into `maps`. Use the maps
   from the North American (NTSC) Xbox disc, like the players do.
3. Copy the `playlists` folder from the server's download, or write your
   own ([Playlists](playlists.md)).

Halo PC maps go in folders next to `maps`: `maps_ce` for Custom Edition,
`maps_md` for HaloMD, `maps_pc` for Halo PC.

## Pick a way to run it

| Way | Good for |
|-----|----------|
| [Docker](#docker) | A Linux server or VPS. It restarts by itself and updates with one command. |
| [Linux](#linux) | Any Linux machine. One file, nothing to install. |
| [Windows](#windows) | A 64-bit Windows 10 or 11 PC, or Windows Server. |

## Docker

Each release has a ready image, `ghcr.io/chupathingyce/server`, for x86,
x64 and ARM64 Linux. `latest` is the newest release and `nightly` is for
testing.

1. Make a folder for the server with your data folder in it, named `data`.
2. Download [`docker-compose.yml`](../deploy/docker-compose.yml) into that
   folder.
3. Open it and set your server's name, its playlist and how many players
   it takes.
4. The server runs as user 1000. If that isn't you, change `user:` in the
   file, or run `sudo chown -R 1000:1000 data`.
5. Start it:

   ```sh
   docker compose up -d
   docker compose logs -f
   ```

It shows up on the site within a few seconds. To update, run
`docker compose pull && docker compose up -d`. Your data folder, settings
and playlists stay as they are.

Many one-click cloud hosts only pass web traffic. Players can't reach a game
server there. Use a VPS or your own machine.

More in [Docker and systemd](docker.md).

## Linux

The server is one file that runs on any Linux. Get it from the
[latest release](https://github.com/ChupathingyCE/chupathingyce/releases/latest).
Not sure which one? Run `uname -m`.

- `chupathingyce-server-linux-x64.zip` for `x86_64`: most VPSes and PCs.
- `chupathingyce-server-linux-arm64.zip` for `aarch64`: Oracle Cloud's
  free tier, Raspberry Pi 4 and 5.
- `chupathingyce-server-linux-x86.zip` for `i686`: older 32-bit PCs.

Unpack it, go to your data folder, and start the server with a playlist and
a name:

```sh
cd /path/to/data
HALO_DEDICATED=playlists/free_for_all.txt HALO_DEDICATED_NAME="My Server" \
  /path/to/chupathingyce-server
```

Leave it running. Ctrl+C stops it. The log is `logs/debug.txt` in the data
folder. To keep it running after you log out, use Docker or the systemd
service in [Docker and systemd](docker.md).

## Windows

Get `chupathingyce-server-windows-x64.zip` from the
[latest release](https://github.com/ChupathingyCE/chupathingyce/releases/latest).
It is for 64-bit Windows 10, Windows 11 and Windows Server.

1. Unpack it into a folder of its own. That folder is your data folder. It
   already has `chupathingyce-server.exe`, `start-server.bat` and the
   `playlists` folder.
2. Make a `maps` folder in it and copy in your maps, as above.
3. Open `start-server.bat` in Notepad. Set your playlist and your
   server's name, then save it.
4. Double-click `start-server.bat`. If Windows Firewall asks, allow the
   server.

The server keeps its saves in a `saves` folder in the data folder, apart
from your own game's. A service uses the same folder.

Ctrl+C or closing the window stops it. To run it as a Windows service
that starts with Windows, open a command prompt as administrator in the
data folder and run `start-server.bat install`. The zip's `README.txt`
has the details.

## Ports

On a home connection you usually don't need to forward any ports. Players
reach your server the same way they reach anyone's invite link, even
behind a router.

On a cloud server (Oracle, AWS, Google, Azure and the like), incoming UDP is
blocked by default. Open one port:

1. Give the server a fixed port: `HALO_NET_TUNNEL_PORT=2302` (the compose
   file already does).
2. Allow incoming UDP 2302 in your provider's firewall.
3. Allow it on the machine too, for example `sudo ufw allow 2302/udp`.

Players on your local network also use UDP 5150 and 5160.

Not sure it worked? The control panel's Status tab says whether people can
join, and what to fix if they can't. `sv_status` on the console says the
same.

## The control panel

The server has a web control panel: status, players, maps, playlists, bans,
settings, the log and the people who run it. It is on by default, at
`http://127.0.0.1:8080`, which only this machine can reach. There is no
default password. Our Docker image keeps it off until you set it.

```sh
HALO_DEDICATED_CONTROL=9090            # another port, still this machine only
HALO_DEDICATED_CONTROL=0.0.0.0:8443    # every address, HTTPS only
HALO_DEDICATED_CONTROL=off             # no panel
```

If port 8080 is taken, the server logs one line and keeps hosting without
the panel. Set another port to get it back.

The first time it starts, the server prints a one-time setup link on its
console (`docker compose logs` with Docker) and writes the same code to
`control_setup_code.txt` in the data folder. Open the link, choose a name
and a password, then set up a second factor in My account. On Windows, the
server also opens the link in your browser the first time.

You can also do the whole setup in the browser. Start the server with the
panel on and no playlist (no `HALO_DEDICATED`). It waits instead of
hosting. Make your account, then pick the server's name, how many players
it takes, a playlist and whether it shows in the Server Browser. Press
Start the server and it hosts. More in
[First-run setup](moderation.md#first-run-setup-no-console-needed).

To reach a panel on `127.0.0.1` from your own computer, use an SSH tunnel:

```sh
ssh -N -L 8080:127.0.0.1:8080 you@your-server
```

Then open `http://127.0.0.1:8080/` in your browser. Nothing else needs to be
open. For HTTPS on a public address, with the server's own certificate or
yours, see [Remote access, safely](moderation.md#remote-access-safely).

## Link it to your profile

Linking is optional. A linked server shows on your profile under My
servers. From there you can see who is playing, warn, kick, ban and change
the map from any browser, and give people roles.

1. Sign in on [stats.chupathingy.org](https://stats.chupathingy.org), or
   make a profile.
2. On the server's console, type `sv_link`. It prints a code and a link.
   With Docker, run `docker attach` on the container first, and leave with
   Ctrl+P then Ctrl+Q.
3. Open the link, or go to
   [Add a server](https://stats.chupathingy.org/servers/link) and enter the
   code. The code works once, for 30 minutes.
4. Check the server's name and add it.

Your server connects out to the site, so linking needs no open ports.
`sv_unlink` undoes it at any time.

A linked server can also ask for a badge on the site. See
[Server badges](../../docs/server-badges.md#for-hosts). Details in
[The link to the Chupathingy site](moderation.md#the-link-to-the-chupathingy-site-optional).

## Moderators

Every server has moderation built in. It works without the site too.

- A moderator can warn, kick and ban for up to 7 days.
- An admin can also ban for longer, unban, and change maps, playlists and
  settings.
- The owner can also give and take away roles.

From the site: open My servers on your profile, then Manage moderators.
Enter their profile name and pick moderator or admin. They moderate from the
game's Moderation screen once their game is linked to their profile.

Without the site: `sv_mod_add` on the console, the `moderators.txt` file,
or the control panel's People tab. [Moderation](moderation.md) covers all of
them.

## Need help?

The server's [README](../README.md#if-somethings-wrong) has fixes for the
common problems. Or ask on the Chupathingy Discord.
