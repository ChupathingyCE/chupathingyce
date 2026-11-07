Chupathingy Dedicated Server for Windows
========================================

A Halo: Combat Evolved server for 64-bit Windows 10, Windows 11 and Windows
Server. It hosts games by itself, with no player of its own, from a
playlist of maps and game types. Its games show on stats.chupathingy.org
and in the in-game Server Browser of Chupathingy and OpenCE.

It's the same server as the Linux one, with the same settings, playlists,
game types, commands and control panel. It's a console program: no window
of its own, no sound.

On a home connection you don't need to open or forward any ports.


Quick start
-----------

1. Unpack this zip into a folder of its own. That folder is your data
   folder.
2. Make a folder named "maps" in it, and copy in the game's ui.map and the
   multiplayer maps (all of them, about 300 MB). Use the maps of the North
   American (NTSC) Xbox disc, as the players do. Halo PC maps go in folders
   beside "maps": Custom Edition maps (with Custom Edition's bitmaps.map,
   sounds.map and loc.map) in "maps_ce", HaloMD maps in "maps_md", Halo PC
   retail maps in "maps_pc".
3. Open start-server.bat in Notepad. Set your playlist (one of the
   "playlists" folder's, or your own) and your server's name (15
   characters at most). Save it.
4. Double-click start-server.bat. Leave the window open while the server
   runs. If Windows Firewall asks, allow the server on private and public
   networks.

The server prints what it's doing. It shows on stats.chupathingy.org within
a few seconds. Ctrl+C (or closing the window) stops it, and takes its game
off the lists as it goes.

Or start it yourself from a command prompt in the data folder:

    set HALO_DEDICATED=playlists\free_for_all.txt
    set HALO_DEDICATED_NAME=My Server
    chupathingyce-server.exe

The log is logs\debug.txt in the data folder. start-server.bat keeps the
server's saves in the data folder's "saves" folder, apart from your own
game's. Started without it, the server saves in %APPDATA%\ChupathingyCE,
the game's folder, unless HALO_SAVE_ROOT says otherwise.

Settings are environment variables, the same as on Linux: the full list is
in the server's docs/settings.md, at
https://github.com/ChupathingyCE/chupathingyce/blob/main/server/docs/settings.md


The console
-----------

While the server runs, type commands in its window: help lists them.
sv_players, sv_kick, sv_ban, sv_map, sv_mapcycle_next and the rest are named
after Halo PC's. See
https://github.com/ChupathingyCE/chupathingyce/blob/main/server/docs/admin.md


Run it as a Windows service
---------------------------

A service starts with Windows and keeps running when you log out.

1. Set your settings in start-server.bat, as above, and try it once.
2. Open a command prompt as administrator (Start, type cmd, right-click
   Command Prompt, Run as administrator).
3. Go to the data folder and install it:

       cd /d C:\path\to\your\data\folder
       start-server.bat install

That makes a service named ChupathingyServer and starts it. It:
- starts with Windows, and restarts by itself after a crash;
- runs as its own account (NT SERVICE\ChupathingyServer), not as SYSTEM,
  and that account is given the data folder;
- keeps the settings start-server.bat set when you installed it, with the
  data folder and its "saves" folder.

Stop or start it in the Services panel (services.msc), or with
"sc stop ChupathingyServer" and "sc start ChupathingyServer". Its log is
logs\debug.txt. With no window to print to, what it would print (the
control panel's one-time setup link, a new control API token) goes to
logs\server-output.txt. Keep that file private.

To change its settings: "start-server.bat uninstall", edit
start-server.bat, then "start-server.bat install" again.

More than one server on a machine: give each its own data folder and a
service name of its own: "start-server.bat install ChupathingyServer2".
Each also needs its own HALO_NET_ADDRESS (see docs/docker.md, "More
servers on the same host").

Windows Firewall does not ask a service. Allow it once, from the
administrator command prompt:

    netsh advfirewall firewall add rule name="Chupathingy server" dir=in action=allow program="C:\path\to\your\data\folder\chupathingyce-server.exe" enable=yes

Without start-server.bat, the program installs and removes the service
itself, with the HALO_* variables set in that command prompt:

    chupathingyce-server.exe --install-service [name]
    chupathingyce-server.exe --uninstall-service [name]


Control panel
-------------

The control panel and moderators work as on Linux. The panel is on by
default at http://127.0.0.1:8080 (this machine only). The first start
prints a one-time setup link and opens it in your browser. Set
HALO_DEDICATED_CONTROL=0.0.0.0:8443 in start-server.bat for the network
(HTTPS only), or HALO_DEDICATED_CONTROL=off for no panel. See
https://github.com/ChupathingyCE/chupathingyce/blob/main/server/docs/moderation.md


If something's wrong
--------------------

- It stops at once with "no maps": the data folder needs maps\ui.map.
  start-server.bat starts the server in its own folder.
- "cannot read the playlist": HALO_DEDICATED is a path inside the data
  folder, like playlists\my_playlist.txt.
- It doesn't show on the site: look at logs\debug.txt. The server needs to
  reach the internet (HTTPS to stats.chupathingy.org, and UDP).
- It shows, but nobody can join: the firewall is dropping the game's
  traffic. Give internet play a fixed port (set HALO_NET_TUNNEL_PORT=2302
  in start-server.bat), allow incoming UDP on it in Windows Firewall (and
  in your cloud provider's firewall, on a cloud server), and restart the
  server.
- "access denied" when installing the service: the command prompt must be
  opened with "Run as administrator".

More: https://github.com/ChupathingyCE/chupathingyce/blob/main/server/README.md
