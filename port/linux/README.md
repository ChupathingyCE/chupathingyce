# Linux

`ninja linux` compiles the game with clang for 32-bit x86 Linux. The result
is a native executable, `build/linux/chupathingy`. The game shows its graphics with
OpenGL 4.5. It plays sound through SDL3. It accepts keyboard, mouse and
gamepad input.

The game is 32-bit code because its data (tags, cache files, saved games)
contains 32-bit pointers, as on the Xbox.

## Requirements

You do not need the Xbox SDK. The declarations that the game uses are in
`port/include/xdk`.

To build:

- Python and ninja (`python3` and `ninja-build` on Debian and Ubuntu,
  `python` and `ninja` on Arch Linux).
- clang and lld. A release build is optimised with the profiles in `pgo/`
  only with clang 22, the version that wrote them; with an older clang it
  is built without them. Debian and Ubuntu get clang 22 from
  [apt.llvm.org](https://apt.llvm.org) (`clang-22`, `lld-22`, `llvm-22`);
  put `/usr/lib/llvm-22/bin` first on `PATH`, or give `configure.py` the
  compiler with `--linux-cc clang-22`. Arch Linux has it as `clang` and
  `lld`. The option `--linux-cc` of `configure.py` selects a different
  compiler.
- The 32-bit glibc development files: `gcc-multilib` and `libc6-dev-i386`
  on Debian and Ubuntu, `lib32-glibc` on Arch Linux.
- The 32-bit SDL3: `libsdl3-dev:i386` on Debian and Ubuntu, `lib32-sdl3` on
  Arch Linux. The portable build does not need it: refer to "Portable
  build".

To start the game:

- glibc 2.31 or later (32-bit), for the builds from GitHub Actions, which
  are built against glibc 2.31. They should start on Debian 11, Ubuntu
  20.04, Fedora 32 and later distributions. A build that you
  make without `--portable` needs the glibc of the computer that built it,
  or a later one.
- The 32-bit OpenGL libraries: `libgl1:i386` and `libgl1-mesa-dri:i386`
  on Debian and Ubuntu, `lib32-mesa` on Arch Linux.
- The 32-bit PipeWire or PulseAudio client libraries:
  `libpipewire-0.3-0:i386` or `libpulse0:i386` on Debian and Ubuntu,
  `lib32-pipewire` or `lib32-libpulse` on Arch Linux.
- The 32-bit X11 libraries: `libx11-6:i386` and `libxext6:i386` on Debian
  and Ubuntu, `lib32-libx11` and `lib32-libxext` on Arch Linux. In a
  Wayland session, the game uses them through XWayland. SDL uses Wayland
  itself only if the 32-bit Wayland libraries are version 1.20 or later
  (`libwayland-client0:i386`, `libwayland-cursor0:i386`,
  `libwayland-egl1:i386` and `libxkbcommon0:i386` on Debian and Ubuntu,
  `lib32-wayland` and `lib32-libxkbcommon` on Arch Linux; Debian 11,
  Ubuntu 20.04 and Fedora 32 have older ones) and the compositor has the
  fifo-v1 protocol. On GNOME, the window then has borders only with the
  32-bit libdecor (`libdecor-0-0:i386` on Debian and Ubuntu,
  `lib32-libdecor` on Arch Linux).

On Debian and Ubuntu, the 32-bit packages need the i386 architecture
first: `sudo dpkg --add-architecture i386 && sudo apt update`. The builds
from GitHub Actions need no SDL3 package: they bring their own
(`libSDL3.so.0`, next to the executable).

### Portable build

The builds from GitHub Actions are portable builds (`--portable`; refer to
"Useful `configure.py` options" in [docs/building.md](../../docs/building.md)). The portable build
starts on more systems than the build machine's:

- It is compiled and linked against the glibc 2.31 of Debian 11, the glibc
  of the Steam Runtime 3 ("sniper"), not against the glibc of the build
  machine. An executable needs the glibc version that it was built against,
  or a later one.
- It brings SDL 3 (`libSDL3.so.0`), built from source against the same
  glibc. The executable looks for it in its own folder first (a `DT_RPATH`
  of `$ORIGIN`, which comes before `LD_LIBRARY_PATH`), so that it uses this
  SDL even when Steam sets `LD_LIBRARY_PATH`. Few distributions have a
  32-bit SDL 3. This SDL loads X11, Wayland, libdecor, PipeWire, PulseAudio
  and ALSA when it starts, so it starts with whichever of them the system
  has. It is linked only to glibc. Its license is `SDL3-LICENSE.txt`.

The 64-bit build (`ninja linux64`, below) is portable the same way, with
Debian 11's 64-bit (amd64) packages.

At the first build, `tools/linux_sysroot.py` downloads the Debian packages
(about 25 MB) and the source of SDL. Each package comes from
`archive.debian.org` or `deb.debian.org`, or else from
`snapshot.debian.org`; the source of SDL comes from GitHub or from
`libsdl.org`. It checks each download against its SHA-256 sum. The system
root is in `build/linux/third_party/sysroot` (`build/linux64/third_party/sysroot`
for the 64-bit build). `tools/linux_sysroot.cmake` (`tools/linux64_sysroot.cmake`)
builds SDL against it.

To make the portable build you need, in addition to the tools above,
CMake, `pkg-config` and `wayland-scanner`: `cmake`, `pkg-config` and
`libwayland-bin` on Debian and Ubuntu, `cmake`, `pkgconf` and `wayland` on
Arch Linux. You do not need the
system's SDL3. You need the 32-bit glibc only for the tests. Keep the
checkout in a folder whose path has no spaces: SDL's build takes the system
root's include folders from `pkg-config`, which splits a path at its spaces,
so the portable build stops with a message there.

To see the glibc version that a build needs, enter
`readelf -V build/linux/chupathingy | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1`.
`tools/linux_glibc_check.py`, which the GitHub Actions builds run on every
Linux executable and library they make, fails on any version later than
2.31.

## Build the game

1. Install the tools. On Debian and Ubuntu, after adding apt.llvm.org's
   `llvm-toolchain-<release>-22` repository (its instructions are at
   [apt.llvm.org](https://apt.llvm.org); its script does it with
   `sudo ./llvm.sh 22`), enter:

   ```sh
   sudo apt install clang-22 lld-22 llvm-22 gcc-multilib libc6-dev-i386 \
     python3 ninja-build cmake pkg-config libwayland-bin
   export PATH=/usr/lib/llvm-22/bin:$PATH
   ```

   On Arch Linux, enable the multilib repository and enter
   `sudo pacman -S clang lld lib32-glibc python ninja cmake pkgconf wayland`.
   GitHub Actions builds on Ubuntu 24.04 this way
   (`.github/workflows/build.yml`).
2. Go to the root folder of the repository.
3. Enter `python3 configure.py` (with `--portable` for a build that
   starts on other computers, as the releases are: "Portable build").
4. Enter `ninja linux`.

### 64-bit

`ninja linux64` builds the same game as native x86-64 code,
`build/linux64/chupathingy`, the way the macOS build does (the 64-bit build in
[port/macos/README.md](../macos/README.md): `HALO_64BIT` and the `long`
rewrite, `tools/lp64_build.py`). It needs the 64-bit glibc and SDL3
development files (`libsdl3-dev` on Debian 13 and Ubuntu 25.04 or later,
`sdl3` on Arch Linux) instead of the 32-bit ones, and the 64-bit OpenGL and
sound libraries to start. An SDL3 built by hand is found through
`LIBRARY_PATH` when linking and `LD_LIBRARY_PATH` when starting. With
`--portable`, as the releases are built, it needs neither: it is built
against Debian 11's 64-bit glibc 2.31 and brings its own SDL3, as the 32-bit
portable build does (refer to "Portable build"). It is not
optimised with a profile (the committed profiles are the 32-bit build's).
Its releases are their own download, `chupathingyce-linux64-release.zip`,
which its self-updater asks for. It plays Halo PC's maps (`maps_ce/`,
`maps_md/`, `maps_pc/`) and plays with the 32-bit builds and the other ports over the
network, and it is a dedicated server too (`server/README.md`).

## Start the game

Enter `build/linux/chupathingy`. The small `halo` script beside it starts
`chupathingy`, for shortcuts and scripts made before the rename in 0.9.0.

The Steam Deck is not a supported platform yet. The Linux build may run
on it: unpack `chupathingyce-linux64-release.zip` into a folder (in Desktop
Mode), and add `chupathingy` to Steam as a non-Steam game ("Add a Game" in the
Games menu of Steam). Reports are welcome in #bug-reports on the Discord.

The game data is the folder that contains `maps/`, from an Xbox disc image
of any version of the game. The game looks for this folder in this
sequence:

1. `paths.data` in `config.toml`.
2. The current folder.
3. The folder of the executable.
4. `assets/` in the current folder, and `assets/` in the repository that
   contains the executable.

If the game finds no data (or finds an empty `maps/`), it asks where to
get it. This occurs at the first start, on the setup screen's Welcome
(launcher.c, in the game's window, before any map loads; builds without
the game browser show the same choices in message boxes):

- "Copy from my Xbox disc image" opens a file picker. Select an Xbox disc
  image (`.xiso` or `.iso`). The game copies `maps/` next to the executable
  and shows the progress. It also copies the disc's Bink movies (`bink/`,
  the `.bik` files), only the ones `bink/` lacks.
- "Add Halo PC or Custom Edition too" opens the same picker for optional
  extras: a Halo PC CD image (`.iso`) fills `maps_pc/` (pc_disc.c), and
  Custom Edition's installer (`halocesetup_en_1.00.exe`) fills `maps_ce/`
  (ce_installer.c). Both read the Microsoft cabinets the maps are packed in
  (cabinet.c). Only maps the folder lacks are written. The game still needs
  the Xbox maps, so it asks again afterward.
- "Make the folders" makes empty `maps/`, `maps_ce/`, `maps_pc/` and
  `maps_md/`, each with a note saying what goes in it, and opens the folder.
  "Check again" looks for the maps.
- "I already have a maps folder" picks a folder elsewhere; `paths.data` is
  set to it.
- B (Backspace or Escape) quits.

A maps folder that isn't the Xbox maps later (Halo PC's ui.map, or none)
opens the setup screen on Maps instead of a message and a quit. Held Shift
or a controller's Back at start, `--setup`, or `launcher.show_at_start =
"always"` open it too; runs nobody watches (`debug.hidden_window`,
`debug.exit_after`, the null renderer, Steam Deck Game Mode) never show it.
`debug.launcher_test` scripts it for tests and screenshots.

Its Play section picks the mode: vanilla, or a pack with its own `ui.map`,
played as `game.pack_menus` for that run. With two or more installed, the
game asks once (`launcher.mode_choice`: "ask" or "default", with
`launcher.default_mode`); "ask" opens Play at every start. `--play` starts
the default mode and `--mode <id>` that mode, neither asking. A player who
never answered keeps what `game.pack_menus` says, and `HALO_PACK_MENUS` wins
for its run.

Stubbs mode is in the Play section too, for testing, when a `maps_stubbs/`
folder beside `maps/` holds your own Stubbs the Zombie Xbox maps (no game
files come with Chupathingy). It plays as a pack, `game.pack_menus =
"stubbs"`. It is an early look: the menus and the first
level load, and much is still missing. Halo and the packs are not changed by
it.

Either picker finds out what a file is from its contents, not its name.

The game writes the copy to `maps.partial`. When the copy is complete, the
game changes the name to `maps`. If the copy stops before it is complete,
the game asks for the disc image again at the next start. The movies follow
the maps, each written as `<name>.partial` first. A movie that is already in
`bink/` is never replaced, and a movie that can't be copied does not stop the
extraction.

## Files and folders

| Xbox drive | Folder |
| --- | --- |
| `d:\` | The data root: the folder that contains `maps/`. |
| `z:\` | `z/` in the save root. This folder contains the cache (approximately 800 MB of map data) and the saved games. |
| `u:\` | `u/` in the save root. This folder contains the user data. |

The save root is `paths.saves` in `config.toml`. If that setting is empty,
the save root is `$XDG_DATA_HOME/chupathingyce` (usually
`~/.local/share/chupathingyce`). The first time the game starts with that
save root, it copies the saves from the folder earlier versions used
(`$XDG_DATA_HOME/halo-linux` or `~/.local/share/halo-linux`), without the
map cache, which it rebuilds. That folder is left as it was. The file
`shared_saves_checked.txt` in the save root records that the copy was done;
delete it to copy again. A save root set in `paths.saves` or
`HALO_SAVE_ROOT` is never copied into. On macOS, the save root is
`~/Library/Application Support/ChupathingyCE/saves`, and the saves are
copied into it once from the same `halo-linux` folder.

The game makes the folders when it needs them. Names of files and folders
are not case-sensitive, as on the Xbox.

Beside the maps and the saves, the game keeps folders of its own
(`port/linux/src/port_folders.h`), the same on every platform:

| Folder | Contents |
| --- | --- |
| `logs/` in the data root | The logs: `debug.txt`, `halo.log` (Windows) and `crashes/`. Zip this folder for a bug report. `paths.logs` (`HALO_LOG_ROOT`) puts it elsewhere. |
| `data/` in the data root | What a player adds that is not a map: `data/fonts/` holds the player's own fonts (`display.high_res_font`). |
| `cache/` in the save root | What the game can make again; safe to delete. `paths.cache` (`HALO_CACHE_ROOT`) puts it elsewhere. |
| `private/` in the save root | What is the player's alone. |

Until 0.8.0, `debug.txt` and `crashes/` were in the data root itself. The
game does not move or delete them: crash reports waiting there are still
sent, and an old `debug.txt` there is left as it was. An update never
touches these folders.

These files are in the data root:

| File | Contents |
| --- | --- |
| `logs/debug.txt` | The log of the game. At start-up, the game shows the logs folder and the data root in the terminal. A crash writes its report (the faulting address and the calls that led to it) here as well; the `reference address` line at the top of each session places those addresses in the build. |
| `logs/crashes/` | Crash reports that wait to be sent (Linux and macOS; refer to "Crash reports"). |
| `init.txt` | Console commands that the game does at start-up. For example, `map_name levels\a10\a10` starts the first campaign level. |

The settings are in `config.toml` next to the executable. Refer to
"Settings". Internet play's MQTT brokers are in `brokers.txt` next to it
(`network.brokers_file`).

If the game stops because of a fatal signal, it writes the address and a
backtrace to the standard error. To find the function at the address, enter
`addr2line -e build/linux/chupathingy <address>`.

### Crash reports

Releases and nightlies on Linux and macOS also write a crash report when
the game stops because of `SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE` or
`SIGABRT` (`port/linux/src/posix_crash.c`). The report goes to `crashes/`
in the logs folder (`logs/crashes/`; reports from before 0.8.0 wait in
`crashes/` in the data root and are sent from there). When the game starts
the next time, it does what
`crash_reports.upload` says: `"yes"` sends the reports to the Chupathingy
site (`network.browser_url`, `POST /v1/crash`) in the background, `"no"`
deletes them, and `"ask"` asks the player first. `crashes/` keeps at most 8
reports.

A report has the same format as on Windows
(`port/linux/src/crash_report.h`), without a minidump: the build, the
system, the signal, up to 32 calls (each a module and an offset in it, for
example `chupathingy+0x2715b4`), and the last 200 lines of `debug.txt` from before
the crash, with each public IP address replaced with `[address]`. To find
the function of a call on macOS, enter `atos -o chupathingy -l 0x100000000
0x1002715b4` (the offset plus `0x100000000`); on Linux, enter
`addr2line -e chupathingy 0x2715b4`.

Local builds send no crash reports. To test the crash reports with such a
build, set the `HALO_CRASH_REPORTS_ANY_BUILD` environment variable. Android
and the dedicated server have no crash reports.

## Controls

The keyboard and the mouse are a control scheme of their own for the player
of controller 1: each action has up to two keys or mouse buttons, which
Settings > Controls > Keyboard (or `[controls]` in `config.toml`) changes. The
game adds the input of the first gamepad to controller 1. The other
gamepads operate controllers 2 to 4. While one person plays, every gamepad
drives their player in the game as in the menus. With two or more players on this
computer (co-op, or split screen in a network game) and only one gamepad,
that gamepad is controller 2 (player 2) and the keyboard and mouse stay
controller 1. The gamepad changes controller only when none of its buttons
is held. The profile's button layout (Settings > Controls, DEVICE: GAMEPAD)
is the gamepads' only.

Controls Setup (Settings > Controls, or the Xbox and Gearbox menus' pause
menu's CONTROLS) has
every device's controls in one list: DEVICE chooses KEYBOARD/MOUSE or
GAMEPAD (the device used last when it opens), and SHOW chooses the keys'
groups or the mouse's settings, or the controller's settings or buttons.
The controller's BUTTONS
sets each action's button on the controller, named as that controller
names it (RB, R1, Black...): A on an action, then the button wanted; the
action that had it takes the old one, and A confirms. That makes the
profile's button layout Custom, kept in `controls.controller_layout`. A
build without Custom reads it as the default layout. The HUD's and the
menus' button prompts follow the layout. With the Xbox menus, the pause
menu's CONTROLS shows the same two screens (the controller's and the
keyboard's), drawn in the Xbox menus' look.

| Action | Keys and buttons (default) |
| --- | --- |
| move | W, A, S, D |
| aim | mouse (direct aim) |
| fire | left mouse button |
| throw a grenade | right mouse button, G |
| jump (and skip a cutscene) | space |
| crouch | left ctrl, C |
| melee | F, mouse button 4 |
| reload | R |
| action (pick up, hold to swap weapons, enter or leave a vehicle; never reloads) | E |
| change the weapon | mouse wheel, 1 |
| change the grenade | X |
| flashlight | Q |
| zoom | Z, middle mouse button |
| show the scores (hold) | tab |
| pause menu | escape |
| say a line to everyone (text chat) | T |
| say a line to your team | Y |
| the chat's recent lines and players (M mutes one for you) | H |

Always: \` opens the developer console, F12 releases or captures the mouse,
F11 changes between fullscreen and window.

One movement of the mouse wheel changes the weapon one time. A second
movement after a short pause changes it again.

In the menus, the mouse moves a pointer:

- The item below the pointer gets the focus.
- A left click selects the item. On a setting with values, a click on the
  left or right half changes the value. On a button in the key of a screen
  (for example "B = Back"), a click pushes that button.
- On the on-screen keyboard (a profile's name), a left click presses the
  key below the pointer, or pushes the "B =BACK" or "A =ENTER" legend.
- A right click goes back.
- The mouse wheel moves through the items.

The keyboard also operates the menus, with keys of its own: the arrow keys
(and W, A, S, D) move, space or enter selects, escape or backspace goes
back (escape resumes the game from the pause menu), delete deletes. F1 is
the controller's Back and F2 its Start, Tab is Y, C and Q are the black and
white buttons, and X is the right trigger (Online Games' sort). When the game continues, the mouse aims again. A mouse button that you hold from the menu does not fire until
you push it again.

## Menus

The menus are the PC version's (Halo Custom Edition's): its main menu and
every screen it leads to (campaign, profiles, multiplayer with its server
browser, direct IP and server setup, the gametype editor, and the profile's
settings: controls, gamepads, mouse, audio, video, network and colour),
laid out as it has them. Their pictures are the high-res redraws; the few
that are not yet (3D renders, screenshots) are drawn from the Xbox's own
menus where it has the same, else a placeholder, listed in
`port/assets/menus/NON_HANDDRAWN.md`.

The campaign is wired: Campaign, on player 1's profile (the one last used),
continues its saved game, starts a level it has reached at a difficulty (New
Game), or loads or deletes any profile's saved game (Load Game). Many of the
other functions behind the screens are not wired to this game yet
(`port/assets/menus/UNWIRED.md`): the lists they fill (profiles, maps,
servers, key bindings) are empty, and the settings they change do not
change. The screens open, close and move between each other as the PC
version's.

`display.menus` chooses one of three views:

- `"chupathingy"` (the default): the Xbox's menus and look with Halo PC's
  screens added where they help, as one set: PROFILES, SETTINGS (Controls,
  Gamepads, Mouse, Audio, Video, Network Setup and Privacy) and QUIT on the main
  menu, the settings pages laid out as the Xbox's screens with its B/A/Y
  prompts, SETTINGS and the host's END GAME in the pause menu, Online Games
  in MULTIPLAYER, and the Xbox's question box for the clipboard invite
  (`port/assets/menus/chupathingy`).
- `"xbox"`: the Xbox's menus, as close to the console as can be, with only
  what this port needs added (Online Games, joining, the host's END GAME,
  the essential prompts).
- `"gearbox"` (or the older `"pc"`): Gearbox's Halo PC menus, as close to
  Halo PC as can be, with its Server Browser.

To host the campaign online instead: Multiplayer > CREATE GAME > INTERNET,
switch the chooser in the map list's top row to SINGLEPLAYER, pick a level
and a difficulty, then Server Setup.

Multiplayer > CO-OP CAMPAIGN is the Xbox's cooperative play, which the PC
version does not have: two players on this computer play the campaign in
split screen. Player 1 is the player who chose it, on the current profile.
Player 2 then chooses a profile with their own controller (a gamepad), and
New Game's levels are those either profile has reached. Either player's
controller chooses the level and the difficulty. A co-op game does
not continue a saved game of one player.

Network games have split screen too: up to 4 players on each computer. In
the game lobby, another controller presses START to join, and the new
player's profile is chosen on the ADD PLAYER screen that opens (with any
controller). With one gamepad, choose the lobby's ADD PLAYER button first:
until then that gamepad shares controller 1 with the keyboard. Two players on one profile get different names from the host. A
player's B in the lobby leaves the game alone, and the last player of the
computer leaves it for all of them. In the game, each player's pause menu
opens on their part of the screen, and its LEAVE GAME is theirs: their part
of the screen stays until the game ends. A game under way shows its own
screen before JOIN GAME: players join there the same way, START or ADD
PLAYER then START, and JOIN GAME brings them all into the game.

With the PC menus, the pause menu (escape) has SETTINGS after RESUME GAME,
which opens the profile's settings: in the campaign the game stays paused,
in a multiplayer game it goes on. A multiplayer game's host also gets END
GAME, which ends the game as its time limit does: the players stay, and
after the carnage report the host picks the next map and gametype (PICK
GAME). LEAVE GAME of the host still ends the game for everyone. The buttons
go into the map's own pause menu, so the buttons of a custom map stay. The
Xbox's pause box is drawn as it is, its middle longer to hold them; a custom
map's box keeps its size, and what is below its list moves down. The few
pictures of the settings that come from the main menu's map are not drawn
there.

With the Xbox's menus, the host of a multiplayer game (one hosted from this
computer, not a dedicated server) gets END GAME in the pause menu too, after
RESUME GAME: RESUME GAME, END GAME, LEAVE GAME. It ends the game as with the
PC menus, and the Xbox's postgame screens then pick the next game. Other
players' pause menus are as the Xbox's.

The menus are XML files in `port/assets/menus` (`tools/ce_menus.py` writes
them from the PC version's tags), which the game contains. To change them,
put files in a `menus` folder next to `config.toml`: a file with the same
path replaces one of the game's, and another `.xml` file is added.
`port/assets/menus/README.md` describes the files. If a file has a problem,
the game uses the Xbox's menus and the log names the file, the line and the
problem.

### Vehicles on Halo PC maps

On a Halo PC (Custom Edition) map, the game puts the vehicles where Halo PC
puts them. Each vehicle of such a map has a list of the gametypes that it is
for (slayer, capture the flag, king of the hill, oddball), with two marks for
each gametype: the vehicle is there by default, or the vehicle can be there.

- The gametypes of the game (their vehicle setting is Warthogs) and the
  setting `Default` put all the vehicles that are there by default for the
  gametype. These are the vehicles of the map, also the vehicles that only
  this map has (a jet, a walker).
- The settings Ghosts and Scorpions put only the vehicles of that type that
  can be there.
- With a number for each type, the game puts that number of each type, at
  the places of the vehicles that can be there. A vehicle that only this map
  has is there when it is there by default.
- The setting None puts no vehicles.
- A vehicle that the script of the map makes is always made.

For only Warthogs on such a map, set the number of each type in the gametype
editor.

`HALO_HS_DUMP=1` writes the scripts of a map to the standard error stream
when the map loads. `HALO_NETWORK_TEST_BANSHEES=<n>`,
`HALO_NETWORK_TEST_VEHICLES=<set>` and `HALO_NETWORK_TEST_WEAPONS=<set>` set
the vehicles and the weapons of a test game (`game/network_test.c`).

## Settings

The settings are in `config.toml` next to the executable
(`build/linux/config.toml`). At the first start, the game writes the file
with a comment for each setting and its default value, commented out:

```toml
[display]
# ...
# Which menus to use: "chupathingy", "xbox" or "gearbox".
# Put your own menus in a menus folder next to config.toml.
# Restart Chupathingy after a change.
# menus = "chupathingy"
```

A setting that is commented out follows the default of the version that
runs, so a new version with a different default changes it. To choose a
value, remove the `# ` at the start of the line and change the value. Only
the settings that you (or the Settings menu) change are lines without `#`;
those stay as they are, also if a later version changes the default. To get
the default values again, delete the file.

The game reads the file once, when it starts: restart the game after you
edit it. The volumes (`audio.volume`, `audio.music_volume`,
`audio.effects_volume`) are the exception: a change to them saved while the
game runs applies within a second. If a key is not correct, or a value has the wrong type, the game
writes the line to the log and uses the default value. If a line has a
mistake that TOML cannot read, such as a word without quotes (`menus = pc`
for `menus = "pc"`) or `True` for `true`, the game skips that line, reads
all of the others, and the log names the line and the likely fix. If you
change the value of a line but do not remove its `# `, the line does
nothing, and the log names it. Words such as `"borderless"` can be in any
case; a word that a setting does not know is in the log, with what the game
uses in its place. The Settings menu (Controls, Display, Audio and Online)
changes the useful settings, writes them into the file (only their lines
change: the line of a setting at its default loses its `#`) and applies
them at once, but `audio.enabled`, and `display.menus` from the next main
menu. A new version adds its new settings to the file, commented out.

Earlier versions wrote every setting as a value. The first start of this
version updates such a file once (`config_version = 2` at the top
marks it): a setting that holds the default of this version is commented
out, so that it follows the default. A setting with another value stays,
also the default of an earlier version (`display.menus = "pc"`, the default
before 0.5.2b, or `"xbox"`, the default before 0.8.0), which you may have
chosen; the log names it. The log has one
line that tells how many lines changed.

A version from before `config_version`, run on a file of this layout, adds
each commented-out setting again as a value at its own default, so that the
file holds the setting twice: `# key = default` and `key = value`. Such a
value is taken out (with the comment the older version wrote above it) only
when the player answers "Use the new settings" to the question below; "Keep
mine" leaves it, and a value that is no default stays in either case.

When a version changes the default of settings that a `config.toml` has
untouched (commented out, or not there), the game asks once, before the
window opens, in a message box on the desktop and on Android: "Use the new
settings" or "Keep mine" (Return and Escape). It lists the changes in words
(smoother edges, sharper shadows, the Chupathingy menus, on Android a
larger audio buffer, since 0.8.1b a fixed port for hosting, UDP 2302, a
hosted game starting alone and Headphone 3D). Until it is answered, the settings keep their old values and
the file is left as it is. "Keep mine" writes the old values into
`config.toml`; "Use the new settings" lets them follow the new defaults. The
answer is `game.defaults_answered` (the number of the last set of changes
asked about); the question does not come again for those, and a later
version asks only about its own changes. A new
`config.toml` has the new defaults and is not asked. A run without anyone to
answer (a hidden window, a test) logs the question and keeps the old values.
The dedicated server never asks and keeps its own defaults.

Each setting has an environment variable. The environment variable changes
the setting for one start of the game. It has priority over the file.

| Setting | Default | Environment variable | Function |
| --- | --- | --- | --- |
| `display.mode` | `""` | `HALO_DISPLAY_MODE` | `"fullscreen"`: the display, taken at the mode of `display.resolution` (the nearest the display has), or at its desktop mode. `"borderless"`: a window over the whole desktop, whose mode does not change. `"windowed"`: a window of `display.window_size`. Empty: `display.fullscreen` decides (`true`: borderless). When it is set, `display.fullscreen` is ignored, and the log says so if both are set. F11 changes between the window and the fullscreen mode. Video Setup sets it. |
| `display.fullscreen` | `true` (macOS: `false`) | `HALO_FULLSCREEN` | `true`: borderless, as `display.mode = "borderless"`. `false`: a window, as `display.mode = "windowed"`. Used only when `display.mode` is empty. |
| `display.resolution` | `"native"` | `HALO_RESOLUTION` | What fullscreen and borderless draw at: `"native"`, the display's own resolution, or `"<width>x<height>"`, such as `"1920x1080"`, 640x480 or more. Fullscreen sets the display to it. Borderless draws at it and scales the picture to the display, where the display has room for it. The picture has 480 lines of the game and the width of the resolution's shape. A window draws at its own size instead. Video Setup's Resolution sets it, from the display's modes; it shows with Fullscreen and Borderless. |
| `display.resolution_scaling` | `"native"` | `HALO_RESOLUTION_SCALING` | `"native"`: the game draws at the resolution of the window, or of the display (or `display.resolution`) fullscreen. `"original"`: the game draws the 640x480 picture of the Xbox and scales it up to the window or the display, whatever `display.mode` is. Video Setup sets it. |
| `display.window_size` | `""` | `HALO_WINDOW_SIZE` | The size of the window, as `"<width>x<height>"`, such as `"1920x1080"`, 640x480 or more. You can change the size of the window; the game's picture takes its shape. Empty: `display.window_scale` decides. Video Setup's Window Size sets it, from sizes of each shape (4:3, 16:10, 16:9 and 21:9) that fit the desktop; it shows with Windowed. |
| `display.window_scale` | `2` | `HALO_WINDOW_SCALE` | Used when `display.window_size` is empty: the size of the window, as a multiple of 640x480. |
| `display.vsync` | `true` | `HALO_NO_VSYNC=1` sets `false` | `true`: each frame waits for the display. |
| `display.skip_intro` | `false` | `HALO_SKIP_INTRO` | `true`: skip the intro movie on startup and load straight into the main menu. |
| `display.max_fps` | `0` | `HALO_MAX_FPS` | With vsync off, the most frames each second. `0`: twice the display's refresh rate (on Android, 120). `-1`: no limit, which can hang some Intel graphics (Raptor Lake), resetting the desktop's graphics too, and heats a phone. |
| `display.anti_aliasing` | `"msaa2x"` on 64-bit computers, `"off"` on 32-bit ones, Android and the server | `HALO_ANTI_ALIASING` | The smoothing of jagged edges, which the Xbox did not have. `"off"`: none, as on the Xbox. `"fxaa"` or `"smaa"`: a pass over the 3D view after the game draws it. The HUD and the menus stay sharp. SMAA is the sharper and costs more. `"ssaa2x"`: the game draws at two times the resolution in each direction (at most the GPU's largest texture), and the picture is scaled down. The GPU does four times the work. Not with `display.resolution_scaling = "original"`. `"msaa2x"`, `"msaa4x"` or `"msaa8x"`: each pixel of the 3D view has that many samples (at most the GPU's). On Android, `"smaa"` gives FXAA and `"ssaa2x"` none. A change applies from the next frame. Refer to "Anti-aliasing" in "What operates". |
| `display.renderer` | `"gl"` | `HALO_RENDERER` | What draws the game. `"gl"`: OpenGL. `"bgfx"`: an experimental test renderer under the same D3D8 interface, config-only with no menu row: Metal on macOS, Vulkan on Windows and Linux. It is in every 64-bit desktop build (macOS, Windows x64, Linux x64; not the 32-bit builds, Android or consoles). If bgfx can't start (no working Vulkan driver, say), the game says so in the log and draws with OpenGL instead. The log names the backend and the GPU. Known differences: the Halo PC ammo and health meters, a brighter first-person pistol and grainy water. FXAA, SMAA and supersampling are OpenGL only, so with bgfx they become `"msaa2x"` and one line says so in the log. Read at start-up. |
| `debug.gpu_flush_draws` | `-1` | `HALO_GPU_FLUSH_DRAWS` | Flush the GPU's pipeline every this many draws. `-1`: every 3 on Intel graphics with Mesa's driver, which can otherwise hang in the game's long runs of small draws and reset the desktop's graphics too. `0`: never. |
| `display.interpolation` | `true` | `HALO_INTERPOLATION` | `true`: one frame for each refresh of the display. `false`: 30 frames each second, as on the Xbox. Video Setup's Frame Rate sets it (30 (Xbox) is `false`, the others `true`). Refer to "Frame rate". |
| `display.direct_camera` | `true` | `HALO_DIRECT_CAMERA` | `true`: in first person, on foot, the view points where the player aims in each frame, not where the last tick left it. Refer to "Frame rate". Not in Video Setup: config.toml only. |
| `display.field_of_view` | `70.0` | `HALO_FIELD_OF_VIEW` | How wide the first-person view is, in degrees across a 4:3 picture: `70`, as on the Xbox, from `50` to `120`. On a wider screen the view shows more at the sides, as before. Scopes zoom in by as much as before. Only the first-person view changes: cutscenes, the third-person views and the game itself (aim, auto-aim, what each machine sends) do not. Video Setup's Field of View sets it in the menus. |
| `display.viewmodel_field_of_view` | `0.0` | `HALO_VIEWMODEL_FIELD_OF_VIEW` | How wide the first-person weapon and arms are drawn, in degrees as `display.field_of_view`: `0` (the default) follows `display.field_of_view`, as before; `50` to `120` draws them at that width whatever the view's, so a wide view does not stretch them. `70`: the weapon as on the Xbox. Drawing only. |
| `display.reticle_scales_with_fov` | `true` | `HALO_RETICLE_SCALES_WITH_FOV` | `true`: the weapon's reticle is drawn smaller in a wider `display.field_of_view` and larger in a narrower one, as the world is, so it still covers the weapon's spread. At `70`, the Xbox's, it is the Xbox's size. Only in first person; the scopes' overlays that cover the view are not resized. `false`: the reticle keeps the Xbox's size at any field of view. Video Setup's Reticle Follows FOV sets it. |
| `display.far_clip_distance` | `1024.0` | `HALO_FAR_CLIP_DISTANCE` | How far the world is drawn, in world units: `1024`, as on the Xbox, up to `10000`. Raise it on large maps that cut off in the distance (Extinction, Hugeass). It is a floor under the map's own far clip, so it holds across map loads and joins, in single player and multiplayer; scripts and `init.txt` can raise the clip further but not below it. Only the picture changes: the game itself, what each machine sends and the near clip do not. Fog can still end the view sooner. Depth precision at a distance gets coarser as the value grows (about 9 units at 3000, 95 at 10000), so distant overlapping surfaces may flicker at the largest values. |
| `display.match_timer` | `false` | `HALO_MATCH_TIMER` | `true`: in a multiplayer game with a time limit, the time left is drawn at the top of the screen. `false`: the time left shows only on the scoreboard (hold BACK, or tab), as on the Xbox. |
| `display.high_res_hud` | `true` | `HALO_HIGH_RES_HUD` | `true`: the HUD (meters, counters, panels and their outlines, the motion sensor, reticles, waypoints, scopes) is drawn from the high-res assets in `port/assets/hud`, 8x the size of the maps' bitmaps. The bitmaps with English text keep the maps' own. `false`: the maps' own bitmaps. |
| `display.high_res_text` | `true` | `HALO_HIGH_RES_TEXT` | `true`: the menus' and HUD's text is drawn with the fonts in `port/assets/fonts` (Cyborg Interface Gothic, in place of the maps' Interstate) at the resolution the game draws at, laid out as before, and the menus' titles are drawn from the high-res pictures in `port/assets/titles`. `false`: the maps' bitmap fonts and titles. It covers the game's own text only: the Server Browser and the moderation overlays use the system's fonts in any case. For the blocky look of the Xbox, also set `display.resolution_scaling = "original"`. |
| `display.high_res_font` | `""` | `HALO_HIGH_RES_FONT` | The player's own font file (`.ttf` or `.otf`) for the high-res text and the menu text of Online Games, in place of the built-in fonts (`port/assets/fonts/fonts.json`). A relative path is in `data/fonts/` in the data root (a path that leaves that folder is refused); an absolute path is used as it is. A file that is missing or is not a font is logged, and the built-in font is drawn. A change applies while the game runs. |
| `display.high_res_font_large` | `""` | `HALO_HIGH_RES_FONT_LARGE` | As `display.high_res_font`, for the large text only (the font tags with the role `large` in `fonts.json`: `ui\large_ui`, `ui\interstate`; Online Games' large menu text). Empty: `display.high_res_font`. |
| `display.shadow_resolution` | `256` (the server: `128`) | `HALO_SHADOW_RESOLUTION` | The size of the maps that the shadows of the objects are drawn in, in pixels each way: `128`, `256`, `512` or `1024` (other values go down to one of these). The game draws the shadow of each object into a map of 128x128 pixels, blurs it and projects it onto the ground. On a large screen, the edges of these shadows show steps that move when the object moves. A larger map makes the edges smooth; the blur is made wider to match, so the shadows are as soft as on the Xbox. Each doubling adds two passes of the blur. `128`: as on the Xbox. |
| `display.anisotropic_filtering` | `0` | `HALO_ANISOTROPIC_FILTERING` | Sharper textures on surfaces seen at a slant, such as floors and walls in the distance: `0` (off, as on the Xbox), `2`, `4`, `8` or `16` (other values go down to one of these, and to the most the graphics can do). Applies to the flat textures that have mip levels (not to the cube maps the game looks up by direction); the menus and HUD look the same. |
| `display.full_model_detail` | `false` | `HALO_FULL_MODEL_DETAIL` | `true`: every model (characters, vehicles, weapons, scenery) is drawn with its most detailed geometry at any distance, instead of the simpler versions the game uses for models far away. Drawing only: the game itself is unchanged. `false`: as on the Xbox. |
| `display.menus` | `"chupathingy"` | `HALO_MENUS` | `"chupathingy"` (the default): the Xbox's menus with Halo PC's PROFILES, SETTINGS and QUIT, the settings pages, and Online Games, in the Xbox's look. `"xbox"`: the Xbox's menus, as close to the console as can be, with Online Games. `"gearbox"` (or the older `"pc"`): Gearbox's Halo PC menus. The Halo PC screens are built from the files in `port/assets/menus` and a `menus` folder next to `config.toml`. A file that sets `"xbox"` (the default before 0.8.0) keeps it. Refer to "Menus". |
| `display.version_watermark` | `false` | `HALO_VERSION_WATERMARK` | `true`: the build's version ("0.7.5a (5c1a363)"), which the main menu shows small and faint in the bottom right corner, is shown there while playing too: handy for screenshots and bug reports. |
| `display.player_names` | `"none"` | `HALO_PLAYER_NAMES` | In multiplayer, whose names are drawn above their heads: `"all"`, `"allies"`, `"enemies"` or `"none"`. Only where the game's host allows it (its `network.allow_player_names`, a dedicated server's `allow_player_names`); a host that says nothing (OpenCE's, an older build) allows none, and a name shows only on the player aimed at, as on the Xbox. An ally's name is drawn above the triangle the game shows over teammates. An enemy's name shows only within the motion sensor's reach, while the enemy is in sight and not camouflaged, so it never shows where an enemy hides. The gametype's motion tracker setting also applies: no names if it shows no players, only allies' if it shows only friends. |
| `display.player_name_scale` | `1.0` | `HALO_PLAYER_NAME_SCALE` | How large the players' names are drawn: `1.0` is three quarters of the size of the HUD's text, from `0.25` to `4`. With high-res text, larger names are rasterized at their size, so they stay sharp. |
| `display.follow_camera` | `"off"` | `HALO_FOLLOW_CAMERA` | In multiplayer, when a dead player's camera follows a living player from behind once the original death camera is done (3 seconds): a teammate in a team game, the killer (or the leader) in a game without teams. LB and RB (Q and E, or the mouse wheel, on the keyboard) switch who, and a "FOLLOWING" line near the bottom says who; it returns to the player's own view on respawn. `"long_waits"`: only when the wait to respawn is long (`display.follow_camera_after`), the player is out of lives or odd man out, or the game type holds spawns until its next round; a short respawn keeps the original death camera. `"always"`: after every death. `"off"`: the original death camera only, as on the Xbox. Only where the game's host allows it (its `network.allow_follow_camera`, a dedicated server's `allow_follow_camera`); a host that says nothing (OpenCE's, an older build) allows none. Split screen: each player's own. |
| `display.follow_camera_after` | `5.0` | `HALO_FOLLOW_CAMERA_AFTER` | For `display.follow_camera`'s `"long_waits"`: how many seconds of the wait to respawn must be left once the death camera is done for the camera to follow someone. |
| `display.powerup_timers` | `false` | `HALO_POWERUP_TIMERS` | `true`: a small line of the HUD's text near the top of the view shows the seconds left on the player's active camouflage, overshield and full spectrum vision while one is on ("CAMO 12   OVERSHIELD 41"). The overshield's time is what is left of it above full: it counts down only in multiplayer, where it drains, so in campaign it shows none. Split screen: each player's own. `false`: none, as the original game. |
| `display.loading_screen` | `true` | `HALO_LOADING_SCREEN` | Keep the original loading screen (the picture with the glow that crosses it) up for a while when a level loads, as loading from the Xbox's disc did; the port's loads are usually over before it shows. How long is set for each kind of game below; the default is no wait. Press A (Space or Enter on the keyboard) to skip the rest of the wait; a load that is still running finishes first. Also when the level is already loaded (a restart). Never for the main menu or a saved film. `false`: every level loads as fast as it can. |
| `display.loading_screen_campaign_seconds` | `0.0` | `HALO_LOADING_SCREEN_CAMPAIGN_SECONDS` | The least seconds the loading screen stays up when a campaign level loads on this machine alone, split screen co-op too, up to 30. A load that takes longer shows for as long as it takes. `0`: no wait. A value set in 0.8.0's `display.loading_screen_min_seconds` is moved to this setting the first time the game starts. |
| `display.loading_screen_multiplayer_seconds` | `0.0` | `HALO_LOADING_SCREEN_MULTIPLAYER_SECONDS` | The same for every other game: split screen multiplayer, system link, online and network co-op, on a machine that joins and on one that hosts and plays, up to 5: while the loading screen is held, the machine hears nothing from the network, and a host drops a machine it hears nothing from for 15 seconds. Machines already finish loading at their own times. Never on the dedicated server. `0`: no wait. |
| `display.xbox_loading_picture` | `true` | `HALO_XBOX_LOADING_PICTURE` | `true`: the loading screen shows the Xbox's own picture, `xbox_loading.tga` in your maps folder (a PAL disc's `loading.tga` works too; NTSC discs have none), and Chupathingy's own when there is none, as after a manual install without the disc's file. `false`: always Chupathingy's. The setup screen's disc import saves a PAL disc's picture as `maps/xbox_loading.tga`. A picture that is not an uncompressed 24-bit TGA of 320 by 240 is skipped. Not on the consoles, which show the disc's picture. |
| `display.scoreboard_team_layout` | `"teams"` | `HALO_SCOREBOARD_TEAM_LAYOUT` | How the multiplayer scoreboard (hold BACK, or tab) lists a team game's players. `"teams"`: a column for each team, red on the left and blue on the right. `"score"`: all the players in order of score. With more players than fit, the mouse wheel and Page Up / Page Down scroll the scoreboard. |
| `display.scoreboard_background` | `true` | `HALO_SCOREBOARD_BACKGROUND` | `true`: the multiplayer scoreboard (hold BACK, or tab) has a panel behind its text, for clearer text. |
| `display.scoreboard_platforms` | `"auto"` | `HALO_SCOREBOARD_PLATFORMS` | A small logo of each player's platform (Windows, macOS, Linux, Android, Steam Deck, Xbox and so on) before their name on a network game's scoreboard, for players whose client sends it over Delta Peer; other players (OpenCE, retail) get no logo. `"auto"`: shown with the Chupathingy menus only, since the Xbox and Gearbox menus stay close to the originals. `"on"`: with any menus. `"off"`: never. |
| `display.delta_profile` | `true` | `HALO_DELTA_PROFILE` | Delta Profile in the game: the DELTA row of Profiles (the Chupathingy menus) and button of Select Profile (the Gearbox menus), the "Delta:" line on a Halo profile, and Online Games' Start. `false` hides them all, and Start opens the profile's web page as before. Link Profile (RB in Online Games) stays. Refer to `docs/delta.md`, Delta Profile in the game. |
| `display.profile_names` | `true` | `HALO_PROFILE_NAMES` | On the scoreboard, the postgame report and the lobby, players who share their Delta profile (`network.share_profile`) show by the profile's name, in games on Delta. `false`: every player by their game name. OpenCE players, and what OpenCE players see, are unchanged. |
| `display.show_quit_players` | `true` | `HALO_SHOW_QUIT_PLAYERS` | `true`: players who quit stay on the multiplayer scoreboard (hold BACK, or tab) and the score in the corner, as in the original game. `false`: they are left off, and the players still in the game are ranked among themselves, as OpenCE does. Only what this machine draws changes. |
| `display.scoreboard_background_color` | `"16, 16, 16, 150"` | `HALO_SCOREBOARD_BACKGROUND_COLOR` | The colour of the scoreboard's panel: `"red, green, blue, alpha"`, each from `0` to `255`. Alpha `0` is see-through, `255` is solid. |
| `display.pause_change_teams` | `true` | `HALO_PAUSE_CHANGE_TEAMS` | In a team game, the pause menu's CHANGE TEAMS (Halo PC's, in every menu set): it asks the host to put you on the other team, and the chat says what came of it. Needs a Chupathingy host (docs/delta.md, Team changes). `false` hides it. |
| `display.pause_controls` | `true` | `HALO_PAUSE_CONTROLS` | The pause menu's CONTROLS, in every menu set: the controller's buttons and the keyboard's keys (Controls above). `false` hides it. |
| `display.server_messages` | `true` | `HALO_SERVER_MESSAGES` | `true`: a warning or notice from a Delta server is drawn as a banner over the game; `false` hides the banner. The message is written to `debug.txt` either way. Kicks and bans are not affected. |
| `display.server_message_text_color` | `""` | `HALO_SERVER_MESSAGE_TEXT_COLOR` | The banner's text colour, `"#RRGGBB"` or `"#RRGGBBAA"`. Empty, or a value that does not parse (logged once): the default, light for a notice and amber for a warning. Applies to both kinds. |
| `display.server_message_border_color` | `""` | `HALO_SERVER_MESSAGE_BORDER_COLOR` | The banner's outline colour, `"#RRGGBB"` or `"#RRGGBBAA"`. Empty, or a value that does not parse: the default, blue for a notice and amber for a warning. Applies to both kinds. |
| `display.server_message_background_color` | `""` | `HALO_SERVER_MESSAGE_BACKGROUND_COLOR` | The banner's background colour, `"#RRGGBB"` or `"#RRGGBBAA"`. Empty, or a value that does not parse: the default, dark blue. |
| `chat.enabled` | `true` | `HALO_CHAT` | Text chat with other Chupathingy players, in a game whose host is a Chupathingy copy or server (Delta Peer's `chat`; OpenCE players are never sent a line). T says a line to everyone, Y to your team; Enter sends, Escape cancels, and while you type the keyboard drives nothing else. The lines show at the lower left above the motion sensor, in the HUD's message font and tint (a size smaller, with a heavier shadow), four at a time, and fade after 6 seconds; H shows the last ones and the players, where up and down choose one and M mutes them for you alone. `false`: no chat at all, neither said nor shown; the server's own lines still show (as `display.server_messages` says). Nothing of what is said is written down. |
| `chat.show` | `"all"` | `HALO_CHAT_SHOW` | Which players' lines show: `"all"`, `"team"` (your team's alone) or `"off"` (none; you can still talk). |
| `chat.filter` | `"mask"` | `HALO_CHAT_FILTER` | Blocked words in the lines you are shown: `"mask"` stars them out, `"hide"` leaves such a line out, `"show"` shows it as sent. The words are the blocked names list's (`server/blocked_names.txt`, built in) and your own (`chat.filter_file`). Nobody is told and nobody is kicked. |
| `chat.filter_file` | `"chat_filter.txt"` | `HALO_CHAT_FILTER_FILE` | Your own blocked words, a file in the save folder: a word a line (blocked anywhere in a word), or `whole <word>` (only as a whole word); `#` starts a comment. |
| `chat.streamer_mode` | `false` | `HALO_CHAT_STREAMER_MODE` | `true`: other players' names in chat show as their team (or "Player"), and blocked words are starred out even with `chat.filter = "show"`. |
| `chat.quick_messages` | `"Nice shot\|Need backup\|On my way\|Thanks\|Good game\|Sorry"` | `HALO_CHAT_QUICK_MESSAGES` | A controller's quick lines, separated by `\|`: D-pad up shows the next, D-pad down says it (to your team in a team game). Empty: none. |
| `display.per_pixel_lighting` | `false` | `HALO_PER_PIXEL_LIGHTING` | `false`: the models (characters, weapons, vehicles, scenery) are lit at each vertex and the light is blended between them, as on the Xbox. The light across a curved surface then shows facets, and a point light that passes close lights only the vertices it reaches. `true`: the models are lit at each pixel by the same lights (the ambient light, two distant lights and two point lights), which changes their look. |
| `display.particle_limit` | `4096` (the Switch: `2048`) | `HALO_PARTICLE_LIMIT` | The most particles alive at once (sparks, smoke, casings, debris). When it is full, the oldest makes room for a new one. The Xbox had 1024. `0`: the port's own 8192. Changes at once. |
| `display.particle_ticks` | `false` (consoles: `true`) | `HALO_PARTICLE_TICKS` | `true`: particles not attached to an object move once a 30 Hz tick, as on the Xbox, and are drawn smoothly between ticks. That saves collision tests. `false`: they move every frame, which is smoother at high refresh rates. Takes effect at the next map. |
| `display.dynamic_light_limit` | `0` (consoles: `8`) | `HALO_DYNAMIC_LIGHT_LIMIT` | The most dynamic lights (muzzle flashes, plasma, explosions) that light the level at once. The biggest on screen light the walls and floor; the rest light only the objects near them. `0`: no limit, as on the Xbox. Changes at once. |
| `display.specular_light_limit` | `0` (consoles: `2`) | `HALO_SPECULAR_LIGHT_LIMIT` | Of those lights, the most that also add their shine to metal and wet floors. The first-person weapon's go first. `0`: no limit, as on the Xbox. Changes at once. |
| `display.crt_filter` | `false` | `HALO_CRT_FILTER=1` sets `true` | `true`: a CRT look over the picture (scanlines, curvature, a phosphor mask). Only the `bgfx` renderer has it for now. |
| `audio.enabled` | `true` | `HALO_NO_AUDIO=1` sets `false` | `false`: no audio device. The sound continues without output. |
| `audio.volume` | `1.0` | `HALO_VOLUME` | The master volume. |
| `audio.music_volume` | `1.0` | `HALO_MUSIC_VOLUME` | The music's volume, of the master volume. |
| `audio.effects_volume` | `1.0` | `HALO_EFFECTS_VOLUME` | The volume of the other sounds (effects and speech), of the master volume. |
| `audio.device` | `"default"` | `HALO_AUDIO_DEVICE` | The output the sound plays on, by the name the system gives it (Audio Setup's Output Device lists the connected ones). `"default"`: the system's default output, followed as it changes. A device that is not connected plays on the default, and the setting is kept for when it is back. Desktop only. |
| `audio.buffer_frames` | `2048` on macOS, `1024` on Android, `512` elsewhere | `HALO_AUDIO_BUFFER_FRAMES` | The audio device's buffer, in sample frames at 48 kHz, from `64` to `8192`. Larger rides out stalls that would cut the sound out; smaller has less delay (512 is 11 ms, 1024 is 21 ms, 2048 is 43 ms). |
| `audio.channels` | `"auto"` | `HALO_AUDIO_CHANNELS` | `"stereo"`, `"5.1"` or `"7.1"`: surround sound as with Dolby Digital on the Xbox. The game sends its 2D sounds to the center and back speakers as the Xbox did, 3D sounds are panned around the front and back speakers (and the sides on 7.1), and the reverberation plays from all of them. `"auto"`: 5.1 on a device of 6 channels, 7.1 on one of 8, else stereo (a device of some other count is more likely an audio interface's outputs than speakers). The LFE channel is left to the receiver's bass management, as on the Xbox. Read when the game starts. Desktop only. Audio Setup: Speakers. |
| `audio.hrtf` | `"on"` (consoles: `"off"`) | `HALO_AUDIO_HRTF` | `"on"`: 3D sounds are rendered for headphones (binaural, with head-related transfer functions): each is filtered as the head and ears would hear it from its direction, the far ear a little later, so it sounds in front, behind, to the side, above or below you instead of between your ears. This matches the Xbox, which filtered every 3D sound for direction, on TV stereo too, so it is the default; these are measured responses (the SADIE II database's KEMAR, `port/assets/hrtf`), not the Xbox's own,, equalized so they do not change the sounds' tone overall. Close sounds (your own footsteps and weapons) stay centered as with the stereo pan. `"auto"`: on when the output's name says it is headphones (a headset, AirPods, "Headphones"), checked again when the output changes. `"off"`: the stereo pan, for speakers if Headphone 3D sounds odd on them. Stereo output only; changes take effect at once. Audio Setup: Headphone 3D. |
| `audio.doppler` | `true` | `HALO_AUDIO_DOPPLER` | `true`: moving sounds are Doppler shifted, higher coming at you and lower going away (a Warthog, a Banshee, a projectile going past), from the velocities the game gives each sound, as the Xbox's DirectSound shifted them. `false`: every sound plays at its own pitch. Audio Setup: Doppler. |
| `audio.reverb` | `true` (the Steam Link: `false`) | `HALO_REVERB` | `true`: the sounds of the world reverberate as the place the player is in does: the sound environments of the maps (a corridor, a cave, a large hall, outdoors) set the reverberation, as the I3DL2 reverb of the Xbox did. A sound behind a wall or a door is muffled in it too. `false`: no reverberation (sounds behind a wall are still muffled). |
| `audio.resampling` | `"sinc"` (the Steam Link: `"linear"`) | `HALO_AUDIO_RESAMPLING` | How the sounds, most of them recorded at 22 kHz, are brought to the output's 48 kHz. `"sinc"`: a windowed sinc low pass keeps each sound's band (flat to about 9 kHz for a 22 kHz sound) and nothing above it. `"linear"`: linear interpolation, as before OpenCE's build 130: the top of each sound's band is duller (-4 dB at 8 kHz) and images of the band reach up to 22 kHz, a brighter, grainier sound. |
| `input.mouse_sensitivity` | `1.0` | `HALO_MOUSE_SENSITIVITY` | The multiplier for the mouse aim. |
| `input.mouse_vertical_sensitivity` | `0.0` | `HALO_MOUSE_VERTICAL_SENSITIVITY` | The multiplier for the vertical mouse aim. `0`: the same as `input.mouse_sensitivity`. |
| `input.invert_mouse` | `false` | `HALO_MOUSE_INVERT=1` sets `true` | `true`: the vertical mouse aim is inverted. |
| `input.keyboard_prompts` | `true` | `HALO_KEYBOARD_PROMPTS` | Not in the menus: every button prompt follows the device used last. While the keyboard or mouse was used last (or, on the desktop, no controller is connected), the prompts show keys, as Halo PC's did: the HUD's messages name the bound key in brackets ("Hold [E] to pick up"), "Hold BACK for score" names the scoreboard key, and the menus' button pictures ("A = SELECT") become key caps of the keys that press them (Enter, Backspace, Tab, E). A controller's button press switches them back. `false` keeps the controller's buttons everywhere, whatever was used last. |
| `input.mouse_aim_assist` | `false` | `HALO_MOUSE_AIM_ASSIST` | `true`: the magnetism of the controller also operates for the mouse. `false`: when the mouse moved after the right stick, the view is not slowed or dragged by a target. The autoaim of the bullets operates in both cases. |
| `controls.<action>` | (the table in "Controls") | `HALO_KEY_<ACTION>` | The keys and mouse buttons of an action, up to two, separated by a comma: `move_forward`, `move_backward`, `strafe_left`, `strafe_right`, `jump`, `crouch`, `fire`, `throw_grenade`, `melee`, `reload`, `zoom`, `switch_weapon`, `switch_grenade`, `action`, `flashlight`, `scoreboard`, `pause`. Keys by their names (`"W"`, `"Space"`, `"Left Ctrl"`, `"F1"`), and `"Mouse Left"`, `"Mouse Right"`, `"Mouse Middle"`, `"Mouse 4"`, `"Mouse 5"`, `"Wheel"` (either way), `"Wheel Up"`, `"Wheel Down"`. |
| `controls.controller_layout` | `""` | `HALO_CONTROLLER_LAYOUT` | The controller's Custom button layout, which Controller Buttons writes: `"action=button"` for each action, separated by commas (`jump`, `switch_grenade`, `action`, `switch_weapon`, `melee`, `flashlight`, `throw_grenade`, `fire`, `crouch`, `zoom`; `A`, `B`, `X`, `Y`, `LB`, `RB`, `LT`, `RT`, `LS`, `RS`, LB and RB being the Duke's White and Black). An action left out keeps its default button; no button is ever on two actions. Used by profiles whose button layout is Custom. Empty: the default layout. |
| `game.downloaded_maps` | `""` | `HALO_DOWNLOADED_MAPS` | Maps to play as downloaded maps, until the game downloads maps itself: their names as the game names them (`bloodgulch`, `hugeass@ce`), separated by commas, or `"*"` for all the maps. The scripts of a downloaded map cannot change the settings of the player or the games of other players. `debug.txt` names what the game refused. |
| `game.pack_menus` | `""` | `HALO_PACK_MENUS` | Play with an Xbox map pack's own menus, by the pack's folder name: `"cex"` loads `maps_cex/ui.map` (Halo CE+'s) in place of the game's `ui.map`, its multiplayer maps being the pack's (`<file>@cex`) and its scripts allowed to start the pack's own levels when the library knows the pack. Empty: the game's menus. Takes effect after a restart. Refer to "Xbox map packs" in [docs/maps.md](../../docs/maps.md#xbox-map-packs). |
| `game.bomb_spawns` | `""` | `HALO_BOMB_SPAWNS` | Where a Bomb or Grifball game you host puts the bomb on a map: `"<map> <x> <y> [<z>]"` (the map as a playlist names it: `bloodgulch`, `timberland@ce`), entries separated by commas. The bomb rests on the first ground under the point (from a little over its height, when given). A map not named has the game's own spot: Blood Gulch's halfway between its teleporters' exits, any other map's halfway between the goals. Only the host's setting counts. |
| `game.bomb_arm_time` | `1.0` | `HALO_BOMB_ARM_TIME` | In a Bomb or Grifball game you host, the seconds the bomb takes to arm once it is planted in the enemy goal, up to 10. A defender who reaches it in time saves it, and it resets with no score. `0`: it scores the moment it is planted. |
| `game.bomb_open_pitch_speed` | `0` | `HALO_BOMB_OPEN_PITCH_SPEED` | Everyone's speed in a Grifball: Open Pitch game you host, in percent (50 to 200; the carrier's 115% on top stops at 200). `0`: the game type's own, 175. |
| `game.bomb_open_pitch_carrier_health`, `game.bomb_open_pitch_carrier_resistance` | `0`, `0` | `HALO_BOMB_OPEN_PITCH_CARRIER_HEALTH`, `HALO_BOMB_OPEN_PITCH_CARRIER_RESISTANCE` | The bomb carrier in a Grifball: Open Pitch game you host: health in percent of everyone's, damage resistance in percent. `0`: the game type's own, 400 each (the carrier lives through one melee blow, not two). |
| `game.bomb_open_pitch_carrier_shields` | `"none"` | `HALO_BOMB_OPEN_PITCH_CARRIER_SHIELDS` | The bomb carrier's shields in a Grifball: Open Pitch game you host: `"none"`, `"normal"` or `"overshield"`. |
| `game.bomb_open_pitch_fumble` | `true` | `HALO_BOMB_OPEN_PITCH_FUMBLE` | `true`: in a Grifball: Open Pitch game you host, a melee blow that doesn't kill the bomb carrier knocks the bomb out of their hands. |
| `game.console_log` | `"important"` | `HALO_CONSOLE_LOG` | What the console shows on the screen. `"important"`: bans, players that the host drops for cheating, the reasons that the game refuses a command, and the asserts that stop the game. `"all"`: all the lines. `"none"`: only the asserts that stop the game. The output of a command always shows. `debug.txt` gets all the lines. The developer console's own output is drawn only while the console is open (`terminal_render 2`, the default); `terminal_render 1` draws it all the time and `0` never. |
| `game.move_old_map_folders` | `"ask"` | `HALO_MOVE_OLD_MAP_FOLDERS` | Moves maps from the old `maps/ce` and `md_maps` folders into `maps_ce/`, `maps_md/` and `maps_pc/` beside `maps/`: `"ask"`, `"yes"` or `"no"`. |
| `launcher.show_at_start` | `"when_needed"` | `HALO_LAUNCHER_SHOW` | When the setup screen opens before the game: `"when_needed"` (the first start, missing maps, `--setup`, or Shift or a controller's Back held at start) or `"always"`. Refer to "Start the game". |
| `launcher.hold_to_open` | `true` | `HALO_LAUNCHER_HOLD` | `true`: Shift or a controller's Back held at start opens the setup screen. |
| `launcher.mode_choice`, `launcher.default_mode` | `"ask"`, `"vanilla"` | `HALO_LAUNCHER_MODE_CHOICE`, `HALO_LAUNCHER_DEFAULT_MODE` | With more than one way to play (a pack with its own menus): `"ask"` picks on the setup screen at each start, `"default"` starts `launcher.default_mode` (`"vanilla"`, or a pack's id such as `"cex"`). Refer to "Start the game". |
| `game.language` | `""` | `HALO_LANGUAGE` | The language of the menus: `ja`, `de`, `fr`, `es` or `it`. Empty: English. |
| `game.vehicle_multiplier` | `1` | `HALO_VEHICLE_MULTIPLIER` | The number of each vehicle of a map in the multiplayer games that this machine hosts: `1` to `4`. Refer to "More vehicles". |
| `game.vehicle_multiplier_maps` | `""` | `HALO_VEHICLE_MULTIPLIER_MAPS` | The maps that `game.vehicle_multiplier` is for: the names of their files, with commas between them (`"hugeass,coldsnap"`). Empty: all maps. |
| `game.unlock_all_levels` | `false` | `HALO_UNLOCK_ALL_LEVELS` | `true`: a profile created afterward starts with all the campaign levels finished on all the difficulties. `false`: a new profile starts at The Pillar of Autumn, and each level opens when you finish the level before it, as on the Xbox. Existing profiles keep their progress. To start a profile over, type `profile_reset_campaign` in the console. |
| `game.start_with_one_player` | `true` (`false` on the dedicated server) | `HALO_START_WITH_ONE_PLAYER` | A system link, internet or split screen game you host can start with you alone: press START GAME in the lobby. Others can join it in progress. `false` waits for a second machine (split screen: a second player), as on the Xbox. Network Setup's Start With One Player sets it. Players who had it off are asked once, after an update, whether to use the new default. First written by Mootjuh as `debug.solo_game`. |
| `paths.data` | `""` | `HALO_DATA_ROOT` | The data root. Refer to "Start the game". |
| `paths.saves` | `""` | `HALO_SAVE_ROOT` | The save root. Refer to "Files and folders". |
| `paths.logs` | `""` | `HALO_LOG_ROOT` | The logs folder (`debug.txt`, `halo.log`, `crashes/`). Empty: `logs/` in the data root. Refer to "Files and folders". |
| `paths.cache` | `""` | `HALO_CACHE_ROOT` | The cache folder. Empty: `cache/` in the save root. |
| `network.address` | `""` | `HALO_NET_ADDRESS` | The IPv4 address of this machine for system link. Refer to "Play on one computer". |
| `network.broadcast` | `""` | `HALO_NET_BROADCAST` | IPv4 addresses, with commas between them, that get the broadcasts of the game. Empty: 255.255.255.255. |
| `network.online` | `true` | `HALO_NET_ONLINE` | `true`: internet play. `false`: system link on the local network only. |
| `network.join_from_clipboard` | `"never"` | `HALO_NET_JOIN_FROM_CLIPBOARD` | An invite link on the clipboard when the game comes to the front: `"ask"` asks over the menus whether to join its game (with its name and map when the game list knows them; No is kept for that invite), `"always"` joins it, `"never"` leaves it. `true` and `false`, from older versions, mean `"always"` and `"never"`. Network Settings in the Chupathingy and Gearbox menus. |
| `network.copy_to_clipboard` | `false` | `HALO_NET_COPY_TO_CLIPBOARD` | `true`: when this copy starts to host a game, its invite link goes on the clipboard. `false`: the game leaves the clipboard alone; the link is in the log, and INVITE LINK in the Gearbox menus' Server Settings copies it. Network Settings in the Chupathingy and Gearbox menus. |
| `network.map_downloads` | `"ask"` | `HALO_NET_MAP_DOWNLOADS` | A Custom Edition map a game you join needs and you don't have, from the community map repositories (maps.halonet.net and its mirrors, as Chimera downloads from them). `"ask"`: the Online Games screen offers to download it (Yes, No, Don't ask again, which writes `"always"` or `"never"` here). `"always"`: downloaded without asking. `"never"`: the game only says which map is missing. Network Settings in the PC menus. The map is checked before it goes in `maps_ce`: refer to "Halo PC maps" in [docs/maps.md](../../docs/maps.md#halo-pc-maps). Not on the dedicated server. |
| `network.playlist_downloads` | `"ask"` | `HALO_NET_PLAYLIST_DOWNLOADS` | When you join a game of a verified playlist, its map set (refer to [docs/playlist-maps.md](../../docs/playlist-maps.md)): `"ask"` says which of its maps you don't have and offers to download them (Yes, No, Don't ask again, which writes `"always"` or `"never"` here); `"always"` downloads them without asking; `"never"` doesn't check. Each file is checked against the signed list before it goes in its folder. Separate from `network.map_downloads`. Desktop and Android. |
| `network.tunnel_port` | `2302` on the desktop and Android, `0` on the consoles and the server | `HALO_NET_TUNNEL_PORT` | The UDP port for internet play. At its default, every copy of the game starts on a port it selects, and moves to 2302 when it hosts an internet game (Server Setup, or the game's start); if another program has 2302, the next free port up to 2306; if all are taken, the selected port stays. The log names the port. A port that you set (in the file, Server Setup's PORT, or the environment variable) is used from the start, to join as well as to host, and no other is tried. `0`: the game selects a port each run, also to host. Refer to "Internet play". |
| `network.allow_upnp` | `true` | `HALO_NET_ALLOW_UPNP` | `true`: internet play can ask the router to forward its port (UPnP). `false`: the game does not ask. Refer to "Internet play". |
| `network.protocol` | `"auto"` | `HALO_NET_PROTOCOL` | Delta Peer, the messages between Chupathingy machines beside OpenCE's protocol (UDP port 5160; `docs/delta.md`). `"auto"`: Delta with the machines that speak it, plain OpenCE with the others. `"opence"`: Delta off. `"delta"`: as `"auto"` for now. |
| `network.chat` | `"on"` | `HALO_NET_CHAT` | Text chat in the games this copy hosts: `"on"`, `"team"` (team lines alone) or `"off"`. |
| `network.team_change` | `"off"` | `HALO_NET_TEAM_CHANGE` | In the team games this copy hosts, whether players may change teams during the game: `"off"` (players keep their team, as on the Xbox), `"balanced"` (only while neither team ends up more than `network.team_change_margin` players bigger) or `"allow"`. Chupathingy players see CHANGE TEAMS in the pause menu only when it is on. A player changes at most every 10 seconds and never leaves a team empty; they respawn on the new team with no death or penalty counted. |
| `network.team_change_margin` | `1` | `HALO_NET_TEAM_CHANGE_MARGIN` | With `"balanced"`, the most players one team may have over the other after a change (0 to 16). |
| `network.allow_follow_camera` | `false` | `HALO_NET_ALLOW_FOLLOW_CAMERA` | In the games this copy hosts, whether players may watch a living player's view while they wait to respawn (`display.follow_camera`). `false` plays as the Xbox did: the death camera alone. Chupathingy clients are told over Delta Peer (the host's rules). |
| `network.allow_player_names` | `false` | `HALO_NET_ALLOW_PLAYER_NAMES` | In the games this copy hosts, whether players may show names above heads (`display.player_names`). `false` plays as the Xbox did: a name shows only on the player aimed at. Chupathingy clients are told over Delta Peer (the host's rules). |
| `network.max_machines_per_address` | `0` | `HALO_NET_MAX_MACHINES_PER_ADDRESS` | In the games this copy hosts, the most machines that may join from one internet address (1 to 16); a machine past it is refused with OpenCE's "too many players from your address" code (13). `0`: no limit. Local network addresses (10/8, 172.16/12, 192.168/16, 127/8, 169.254/16) are never limited; carrier-grade NAT (100.64/10) is counted. Internet play's peers are counted by the address their tunnel comes from, not their stand-in address. A dedicated server uses its own `max_machines_per_address` (8 by default). |
| `network.chat_legacy` | `true` | `HALO_NET_CHAT_LEGACY` | In the games this copy hosts, OpenCE players chat too, over their own text chat, which the host bridges to Delta chat (`docs/delta.md`, Chat): they see every line and theirs reach everyone, under the same rules. Follows `network.chat` (off there is off for them too). `false`: OpenCE players get no chat here. |
| `network.chat_filter` | `true` | `HALO_NET_CHAT_FILTER` | In the games this copy hosts, a line with a blocked word (the blocked names list's) is not passed on, and its sender is told. Nobody is kicked. |
| `network.share_profile` | `false` | `HALO_NET_SHARE_PROFILE` | `true`: the other Chupathingy players of a game see this copy's player ID (its game list profile), over Delta. |
| `network.platform_limits` | `"on"` | `HALO_NET_PLATFORM_LIMITS` | `"on"`: Delta hosts keep a game to the players this platform takes (an original Xbox: 16). `"off"`: this machine joins games of any size. |
| `network.host_platform_limits` | `true` | `HALO_NET_HOST_PLATFORM_LIMITS` | `true`: a game this machine hosts keeps to the players its Delta machines' platforms take. `false`: their limits are ignored, for testing. |
| `network.public_lobby` | `true` | `HALO_NET_PUBLIC_LOBBY` | `true`: the server browser. Public games are listed, and Join Game > Server Browser shows them. `false`: no games are listed or shown. Refer to "Server browser". |
| `network.ping_games` | `true` | `HALO_NET_PING_GAMES` | `true`: Online Games pings the listed games whose hosts answer pings, before joining (the host sees your public address, as when you join). `false`: no pings; only the estimated ones (`~85`). Refer to docs/delta.md, "Pings before joining". |
| `network.host_public` | `true` | `HALO_NET_HOST_PUBLIC` | `true`: a new game of Create Game > Internet starts as PUBLIC. `false`: it starts as PRIVATE. LISTING in Server Setup changes it for each game. Refer to "Server browser". |
| `network.coop_friendly_fire` | `"on"` | `HALO_NET_COOP_FRIENDLY_FIRE` | Whether the players of an online co-op game hurt each other: `"off"`, `"on"`, `"shields_only"` or `"explosives_only"`. FRIENDLY FIRE in co-op's Server Setup writes its choice here. Their AI allies they always can, as in the campaign. |
| `network.block_mouse_aim_assist` | `false` | `HALO_NET_BLOCK_MOUSE_AIM_ASSIST` | The host's policy on aiming assist for the mouse and keyboard in a game this machine hosts. `false`: each player's own `input.mouse_aim_assist` decides. `true`: the view's magnetism is off for a player aiming with the mouse, whatever their setting, in the game; the players' own settings are never changed. Clients are told when they join and when it changes (network version 32); a client honors the policy of any host that sends one, and, until a host of version 32 or later speaks, treats the mouse's magnetism as blocked. The bullets' autoaim is unaffected. |
| `network.hit_line_of_sight` | `true` | `HALO_NET_HIT_LINE_OF_SIGHT` | In a game this machine hosts, a client's hit on a target with a wall between them is refused, as a hit the shooter could not have made. A shot needs a clear line from where the host had the shooter (their view, their head, or the gun of the vehicle seat they drive or gun) to the target where the shooter saw it, its edges included; an explosion needs one from where it went off, so a grenade thrown around a corner still hits. Glass and what the projectile goes through don't block it, and a projectile that homes in or falls in an arc may go around or over. Melee and vehicle hits are not checked. The host's log counts these refusals with the others. `false`: no line is looked for, as before. Only the host checks; nothing changes for the players who join. Refer to `NETCODE.md`. |
| `network.host_cheats` | `false` | `HALO_NET_HOST_CHEATS` | `false`: in a public internet game you host, once another machine is in it, your developer console and cheats cannot change the game. `true`: they can, and Online Games shows "Host cheats on" for the game. LAN and private games always allow them. |
| `network.host_console_objects` | `32` | `HALO_NET_HOST_CONSOLE_OBJECTS` | How many objects your console and cheats may make in each network game you host (`cheat_all_vehicles`, `object_create` and the like). Past it they are refused. At most 8 while an original Xbox plays. `0`: no limit. |
| `network.coop_player_collisions` | `true` | `HALO_NET_COOP_PLAYER_COLLISIONS` | Whether the players of an online co-op game bump into each other. `false`: they walk through each other, so that one cannot block a doorway or stand on another; they still bump into the AI's characters. PLAYER COLLISIONS in co-op's Server Setup writes its choice here. |
| `network.coop_ally_friendly_fire` | `true` | `HALO_NET_COOP_ALLY_FRIENDLY_FIRE` | Whether the players of an online co-op game you host hurt their AI allies (Marines, Captain Keyes) with their hits. `false`: they don't; the AI's own hits on its allies are as ever. OpenCE's ALLY FRIENDLY FIRE (network version 33), sent to every machine in the game's settings. |
| `network.coop_enemies_mode` | `"per_player"` | `HALO_NET_COOP_ENEMIES_MODE` | Online co-op's extra enemies: `"none"`; `"per_player"`, each squad of enemies that a level places grows by `network.coop_enemies` for each player past the first; or `"multiplier"`, each squad is `network.coop_enemies_multiplier` times as large, for any number of players. The extra enemies stand around the squad's places, and those that a dropship has no seats for drop out of it after its passengers. EXTRA ENEMIES in co-op's Server Setup writes its choice here. |
| `network.coop_enemies` | `50` | `HALO_NET_COOP_ENEMIES` | The extra enemies per player, a percentage from `25` to `200`: for each player past the first, each squad of enemies gets this much of itself more (`100`: as many again, so four players meet four times the squad), up to 8 times the squad however many players there are. PER PLAYER in co-op's Server Setup writes its choice here. |
| `network.coop_enemies_multiplier` | `2` | `HALO_NET_COOP_ENEMIES_MULTIPLIER` | The static multiplier of the enemies, `2` to `32`: each squad of enemies is this many times as large. MULTIPLIER in co-op's Server Setup writes its choice here. |
| `network.coop_public` | `false` | `HALO_NET_COOP_PUBLIC` | `true`: an online co-op game (Create Game > Internet, a SINGLEPLAYER map) starts as PUBLIC. `false`: it starts as PRIVATE. LISTING in co-op's Server Setup writes its choice here. Refer to "Server browser". |
| `network.browser_url` | `"https://stats.chupathingy.org"` | `HALO_NET_BROWSER` | The game list server: Online Games, the Server Browser's listed games, Link Profile, stats and crash reports. Empty: none. A file that still names `https://halo.milenko.org`, the old default, follows the new one. |
| `network.brokers_file` | `"brokers.txt"` | `HALO_NET_BROKERS_FILE` | The file of the public MQTT brokers that let the machines of an invite find each other, and that carry the listings of the server browser: next to `config.toml`, unless a full path. One `host:port` on each line, up to 4; `#` starts a comment. |
| `network.stun_servers` | Google and Cloudflare | `HALO_NET_STUN` | The public STUN servers (`host:port`, with commas between them) that give the internet address of a machine. |
| `bots.count` | `0` | `HALO_BOTS` | The number of bots in the multiplayer games that this machine hosts. `0`: no bots. Refer to "Bots". |
| `bots.fill` | `0` | `HALO_BOTS_FILL` | The games that this machine hosts have this number of players: bots fill the places that people do not have. A bot goes when a person joins and comes back when a person goes. `0`: `bots.count` bots. Refer to "Bots". |
| `bots.skill` | `5` | `HALO_BOT_SKILL` | How good the bots are, from `1` to `10`: `1` easy, `3` normal, `5` hard, `8` legendary, `10` near perfect. The levels went from `1` to `5` before: the old `5` is `10`, and the old `3` is between `5` and `6`. |
| `bots.balance` | `false` | `HALO_BOTS_BALANCE` | `true`: the score moves the skill of each bot in the range of `bots.skill_spread`, down for a team that is ahead and up for a team that is behind. Refer to "Bots". |
| `bots.skill_spread` | `0` | `HALO_BOT_SKILL_SPREAD` | How many levels below `bots.skill` a bot can be, from `0` to `9`. Each bot gets its skill for the game from that range: `bots.skill = 10` with a spread of `4` gives bots from `6` to `10`. `0`: all the bots have `bots.skill`. Refer to "Bots". |
| `infection.rounds`, `infection.round_minutes`, `infection.countdown` | `3`, `4`, `10` | `HALO_INFECTION_ROUNDS`, `HALO_INFECTION_ROUND_MINUTES`, `HALO_INFECTION_COUNTDOWN` | The rounds of a game of infection, the minutes of a round (`0`: no limit) and the seconds before the first zombie. Refer to "Infection". |
| `infection.zombie_speed`, `infection.zombie_damage`, `infection.zombie_health` | `1.3`, `8.0`, `1.5` | `HALO_INFECTION_ZOMBIE_SPEED`, `HALO_INFECTION_ZOMBIE_DAMAGE`, `HALO_INFECTION_ZOMBIE_HEALTH` | How fast a zombie moves (a player is `1`), how hard it hits and how much damage it can take (the Flood of the campaign is `1`). |
| `network.announce` | `"latest"` | `HALO_NET_ANNOUNCE` | The OpenCE network version that the games you host announce. `"latest"` follows the newest one the signed legacy table proves for hosting, between games and with no restart. A number pins it, such as `32`. Refer to `docs/delta.md`, "Auto-announce". |
| `network.legacy_table` | `""` | `HALO_LEGACY_TABLE` | For testing, and for admins: a legacy table file, not signed, next to `config.toml` unless a full path. Its row for the wire of the build sets the OpenCE network versions that the game announces and joins, in place of the signed tables. The log shows a warning at start. Refer to `docs/delta.md`. Empty: none. |
| `network.list_hosted_games` | `true` | `HALO_NET_LIST_GAMES` | `true`: the system link games this machine hosts are listed on `network.browser_url` (stats.chupathingy.org), where anyone can find and join them. Only public games without a password are listed. `false`: they are kept to invites and the local network, and no carnage report of them is sent. |
| `network.report_hosted_games` | `true` | `HALO_NET_REPORT_HOSTED` | The stats of the games this machine hosts: a listed game's carnage report, its Delta Stats (`network.report_events`) and the host's own players' claims of their lines. `false`: none of them are sent, and the game stays listed (`network.list_hosted_games`). On the Privacy screen (Gearbox menus). |
| `network.report_joined_games` | `true` | `HALO_NET_REPORT_GAMES` | When an internet game this machine joined ends, the game sends `network.browser_url` its scores as this machine saw them, with this copy's player ID, and confirms this machine's players' lines in the host's report of the game (`POST /v1/claim`), which puts the game on their profiles. `false`: neither, for games this machine joins. Games it hosts are still reported and confirmed (unless `network.report_hosted_games` is off). On the Privacy screen (Gearbox menus). |
| `network.report_events` | `true` | `HALO_NET_REPORT_EVENTS` | Delta Stats: the games this machine hosts are recorded (kills with weapons and positions, accuracy, medals, objectives, vehicles, pickups) and sent to `network.browser_url` when each ends. Players are their names, never a hardware ID or an address; an OpenCE player is only their name, team and that game's score, place, kills and deaths. `false`: no events are recorded or sent. On the Privacy screen (Gearbox menus). The carnage report of a hosted game is separate: it is sent while the game is listed (`network.list_hosted_games`), unless `network.report_hosted_games` is off, which stops both. Refer to `docs/delta.md`. |
| `discord.application_id` | the application of the project (Chupathingy) | `HALO_DISCORD_APPLICATION` | The Discord application the game shows as. Empty: no Discord. |
| `discord.presence` | `"full"` | `HALO_DISCORD_PRESENCE` | What Discord shows: `"full"` (game type, map, score, players, time, pictures), `"minimal"` (only "Playing Chupathingy") or `"off"`. Network Setup in the PC menus. |
| `discord.show_server_name` | `true` | `HALO_DISCORD_SHOW_SERVER_NAME` | `true`: Discord shows the name of the network game. |
| `discord.show_join` | `true` | `HALO_DISCORD_SHOW_JOIN` | `true`: while this machine hosts a public game (in the server browser, no password), Discord shows a Join game button. |
| `update.auto` | `true` | `HALO_UPDATE_AUTO` | `true`: at start-up, the game looks for a new version. Refer to "Updates". `false`: the game does not look. |
| `crash_reports.upload` | `"ask"` | `HALO_CRASH_REPORTS` | Desktop builds (releases and nightlies). `"yes"`: the game sends a report of each crash to the developers, through `network.browser_url`. `"no"`: the game sends no reports. `"ask"`: the game asks after the next crash (on Linux and macOS, when it starts the next time) and writes the answer here. On the Privacy screen (Gearbox menus). Refer to "Crash reports" below and in [port/windows/README.md](../windows/README.md#crash-reports). |
| `debug.update_answer` | `""` | `HALO_UPDATE_ANSWER` | The answer to the update question, for automatic tests: `yes`, `no` or `never`. Empty: the game asks. |
| `debug.random_seed` | `0` | `HALO_RANDOM_SEED` | The random seed of the games that this machine hosts, and of their bots. With the same seed, a game with only bots is the same each time. Use this setting to compare two builds. `0`: a new seed for each game. |
| `debug.exit_after` | `0.0` | `HALO_EXIT_AFTER` | The game stops after this number of seconds. `0`: never. |
| `debug.screenshot_directory`, `debug.screenshot_every` | `""`, `0` | `HALO_SCREENSHOT_DIR`, `HALO_SCREENSHOT_EVERY` | The game writes each Nth frame to this folder as a BMP file. |
| `debug.screenshot_window` | `false` | `HALO_SCREENSHOT_WINDOW` | `true`: those screenshots are of the window as shown, with the overlay's screens (Online Games and others) on the frame. |
| `debug.hidden_window`, `debug.null_renderer` | `false` | `HALO_HIDDEN_WINDOW`, `HALO_NULL_RENDERER` | `true`: no visible window, or no graphics. |
| `debug.gpu_stats`, `debug.gpu_trace_frame`, `debug.gpu_trace_constants`, `debug.gpu_dump_shaders`, `debug.texture_dump_directory`, `debug.texture_log`, `debug.gl_debug`, `debug.texture_no_cache` | off | `HALO_GPU_STATS`, `HALO_GPU_TRACE`, `HALO_GPU_TRACE_CONSTANTS`, `HALO_GPU_DUMP_SHADERS`, `HALO_TEXTURE_DUMP`, `HALO_TEXTURE_LOG`, `HALO_GL_DEBUG`, `HALO_TEXTURE_NO_CACHE` | Tools to find problems in the graphics: counts for each frame, all the GL state of one frame, the GLSL code, the textures. |
| `debug.discord_log` | `false` | `HALO_DISCORD_LOG` | Log each activity sent to Discord, without the invite. |
| `debug.menu_open` | `""` | `HALO_MENU_OPEN` | Start on this screen of the menus (`main_menu/settings_select/...`, as `port/assets/menus` names it), a player profile being edited, to look at it. |
| `debug.gpu_skip_vertex_shaders`, `debug.gpu_debug_expression`, `debug.gpu_debug_flat`, `debug.gpu_debug_texture0` | off | `HALO_GPU_SKIP_VS`, `HALO_GPU_DEBUG_EXPR`, `HALO_GPU_DEBUG_FLAT`, `HALO_GPU_DEBUG_T0` | Tools to find problems in the graphics: skip the draws of a vertex shader, or replace the output of all pixel shaders with a GLSL expression (for example `t0.rgb`). |
| `debug.network_test`, `debug.network_test_start`, `debug.network_test_kill`, `debug.network_test_score`, `debug.network_test_shoot`, `debug.network_test_shoot_walls`, `debug.network_test_vehicle`, `debug.network_test_pickup`, `debug.network_test_pickup_weapon`, `debug.network_test_fall`, `debug.network_test_bomb_in_goal`, `debug.network_test_bomb_throw`, `debug.network_test_bomb_keep`, `debug.network_test_team_change`, `debug.network_test_pause`, `debug.network_test_moderate`, `debug.network_test_moderate_time`, `debug.network_test_local_players`, `debug.network_test_password`, `debug.network_test_invite_file`, `debug.test_input` | off | `HALO_NETWORK_TEST`, `HALO_NETWORK_TEST_START`, `HALO_NETWORK_TEST_KILL`, `HALO_NETWORK_TEST_SCORE`, `HALO_NETWORK_TEST_SHOOT`, `HALO_NETWORK_TEST_SHOOT_WALLS`, `HALO_NETWORK_TEST_VEHICLE`, `HALO_NETWORK_TEST_PICKUP`, `HALO_NETWORK_TEST_PICKUP_WEAPON`, `HALO_NETWORK_TEST_FALL`, `HALO_NETWORK_TEST_BOMB_IN_GOAL`, `HALO_NETWORK_TEST_BOMB_THROW`, `HALO_NETWORK_TEST_BOMB_KEEP`, `HALO_NETWORK_TEST_TEAM_CHANGE`, `HALO_NETWORK_TEST_PAUSE`, `HALO_NETWORK_TEST_MODERATE`, `HALO_NETWORK_TEST_MODERATE_TIME`, `HALO_NETWORK_TEST_LOCAL_PLAYERS`, `HALO_NETWORK_TEST_PASSWORD`, `HALO_NETWORK_TEST_INVITE_FILE`, `HALO_TEST_INPUT` | Automatic tests of system link (`game/network_test.c`). Refer to `NETCODE.md`. |
| `debug.test_gamepads` | 0 | `HALO_TEST_GAMEPADS` | Virtual gamepads (SDL's, up to 4) for tests of split screen: `debug.test_input`'s `menu:` script presses their buttons and sticks as `pad<n>:<button>` (`pad2:a`, `pad1:rs_left`) and takes them out and back with `pad<n>:unplug` and `pad<n>:plug`. |
| `debug.determinism`, `debug.determinism_hashes`, `debug.determinism_positions`, `debug.determinism_ticks`, `debug.determinism_seed` | off | `HALO_DETERMINISM`, `HALO_DETERMINISM_HASHES`, `HALO_DETERMINISM_POSITIONS`, `HALO_DETERMINISM_TICKS`, `HALO_DETERMINISM_SEED` | The input-replay check of the simulation (`game/determinism_check.c`, `tools/test_determinism.py`): `record:<file>` writes every tick's player actions, `replay:<file>` plays them back, with a line of hashes of the game's state after each tick, and each player's unit position, velocity and vitality (for the trick replays, `tools/test_tricks.py`). |
| `debug.chat_test` | `""` | `HALO_CHAT_TEST` | A script of text chat for the tests (`game/chat.c`): `"<seconds>:<what>[:<text>]"` steps separated by `;`, from when this copy is in a game: `all` and `team` say a line, `server` says one as the host's server, `mute` and `unmute` mute a machine as a moderator does (the host), `localmute` mutes one for this copy, `log` and `close` open and close the log, `type` shows a line being typed. |
| `debug.touch_targets` | `false` | `HALO_TOUCH_TARGETS` | Outlines the tap targets of the menus (item green, value blue, list slot yellow, legend button red, the band beside the slots of a list orange, keys of the on-screen keyboard white), marks where the last finger went down and the last tap landed for 3 seconds, and logs each tap with the target that it hit (for a value, also where it splits into previous and next): to judge the accuracy of touch. |
| `debug.solo_game` | retired | `HALO_SOLO_GAME` | The older name of `game.start_with_one_player`. An old config.toml that has it still loads: a `true` moves to `game.start_with_one_player` once, and the old line is taken out. `HALO_SOLO_GAME=1` still turns `game.start_with_one_player` on for one run. |
| `debug.network_latency`, `debug.network_loss`, `debug.network_corrupt`, `debug.network_corrupt_stream`, `debug.network_corrupt_after` | `0` | `HALO_NETWORK_LATENCY`, `HALO_NETWORK_LOSS`, `HALO_NETWORK_CORRUPT`, `HALO_NETWORK_CORRUPT_STREAM`, `HALO_NETWORK_CORRUPT_AFTER` | The game holds all the data that it receives for this number of milliseconds, ignores this percentage of the datagrams, and damages this percentage of the datagrams it receives, and this percentage of its reads of streams, at random (bytes changed, cut short, stretched or replaced), from this many seconds after the start. Use the first two to test the netcode as on the internet, and the others to test that nothing another machine sends can crash the game (a damaged stream is closed, so a little goes a long way; a host's messages to its own client are damaged too, so start damaging once the game has started). |
| `debug.telnet_console`, `debug.telnet_console_port` | `false`, `2323` | `HALO_TELNET_CONSOLE`, `HALO_TELNET_CONSOLE_PORT` | The game listens on 127.0.0.1, on this port, for a script console (connect with telnet). The console has no password, so only this computer can reach it. |

With Mesa drivers, the game sends its GL calls through the GL thread of
Mesa. To stop this, set the environment variable `mesa_glthread=false`.

## Updates

The builds from GitHub Actions (refer to [docs/install.md](../../docs/install.md#download))
can update themselves. At start-up, the game asks GitHub for the latest
release. The game does not wait for the answer. If the latest release is not
newer, the game does nothing.

If the latest release is newer, the game asks: "Do you want to update?"

- Select "Yes" to update. The game downloads the release for this platform,
  replaces its files and starts the new version. The old files get the
  extension `.old`. The new version deletes them.
- Select "No" to continue. The game asks again at the next start.
- Select "Do not ask again", then "Yes", to stop the questions. The game
  writes `auto = false` in the `[update]` section of `config.toml`. To get
  the questions again, set `auto = true`.

Under gamescope (the Game Mode of the Steam Deck), the game does not ask,
because a system dialog stops the game there. The game writes the new
version in the log. To update, start the game from the desktop, or set
`update_answer = "yes"` in the `[debug]` section of `config.toml`.

A packaged install (a package manager's, such as the AUR's
`chupathingy-bin`) never updates itself. A package turns it on with a file
named `packaged` next to the executable, `HALO_PACKAGED=1` in the
environment, or a build made with `configure.py --packaged`. Then
`update.auto` is off and stays off, and a newer release only puts "update
Chupathingy with your package manager" on the main menu's version line and in
the log (`auto = false` in `config.toml`, or `HALO_UPDATE_AUTO=false`, stops
that look too). On Linux, the maps, `config.toml` and the logs go in the save
root's usual place, `~/.local/share/chupathingyce`, as an AppImage's do. So do
those of a game in any folder it cannot write, unless that folder has its own
`maps/` or `config.toml`.

The game downloads through HTTPS. It examines the certificate of the server
against the certificate authorities of the system: on Linux, the bundle of
the distribution (`src/posix_update.c`, with Mbed TLS); on Windows, the
certificate store of Windows (WinHTTP). The folder of the executable must
let the game write to it.

The game installs only files that the release key signed. First, it reads
the list of update files of the release (`updates.json`) and examines the
signature of the list (`updates.json.sig`). Then it downloads the file that
the list gives for this platform and examines its size and SHA-256 against
the list. A release from before version 0.8.0 has no list. For such a
release, the game examines the signature of the zip (`<zip>.sig`).

The AppImage (`chupathingyce-linux64-release.AppImage`) updates as one
file. The game downloads the new AppImage that the signed list names, checks
its size and SHA-256, and puts it in place of the AppImage it runs from,
with the same file name, so a renamed AppImage or a menu shortcut to it keeps
working. The old AppImage is kept as `<name>.old` until the next start. The
folder of the AppImage must let the game write to it. A release without a
list, or with no AppImage in its list, is not installed over an AppImage:
the game says to download the new AppImage instead. A packaged AppImage
(`HALO_PACKAGED=1`) never updates itself.

The macOS application does not replace itself yet. When a newer release is
out, it offers the release page, where you download the new application.

Builds that you make yourself have no build number. They do not look for
updates.

## Frame rate

The game calculates its world at 30 Hz, as on the Xbox. On the Xbox, the
game showed one frame for each calculation (tick). This port shows one frame
for each refresh of the display, for example at 60, 120 or 240 Hz.

Each frame shows the world between the last two ticks
(`game/render_interpolation.c`):

- After each tick, the game keeps the camera, the position of each part of
  each object, and the first-person weapon.
- Each frame mixes the last two ticks. The mix agrees with the time since
  the last tick.
- Rotations use quaternions. Positions and scales are linear.
- A teleport, a respawn or a cut of the camera does not mix. It jumps.
- After a long frame (several ticks in one frame), the camera mixes the
  last tick only, as the objects do.
- In cinematics, a camera that moves with an object (the lifepod in a30,
  a Pelican) moves with the object as it is drawn.

Thus the frames are one tick (33 ms) after the calculation. The calculation
does not change.

The direction of the view is an exception. The game reads the mouse and the
sticks in each frame. In first person, on foot, each frame points the view
where the player aims at that time (`display.direct_camera`). Thus the view
turns in the frame that the mouse moves. In a vehicle and in cinematics, the
view mixes as the other things do. On Android, the view mixes as before.

To get 30 frames each second, set `display.interpolation = false`.

To see the frame rate:

1. Push \` to open the developer console.
2. Enter `display_framerate true`.

In the game of another host, the console runs only the commands that change
nothing of the game (such as `display_framerate`), and the game puts back
cheats, the game speed and the settings of the drawing that show more of
the world (such as `rasterizer_wireframe`). Refer to `NETCODE.md`.

The frame rate shows at the bottom right of the screen. It is the mean over
half a second.

## System link

The Xbox game lets 16 players on 4 machines play a system link game. This
port lets up to 128 players on up to 128 machines play. Each machine can
have up to 4 players (split screen).

One player can start a split screen game or a system link game: the lobby
does not wait for a second player or a second machine (the Xbox game did),
and other machines can join the game in progress. A player who is alone can
also start a team game. A dedicated server keeps its own rules. Refer to
[docs/dedicated-server.md](../../docs/dedicated-server.md).

- `include/halo_port_limits.h` sets the limits.
- `include/halo_port_capacity.h` sets the memory for the limits. The game
  state is 16 MB at `0x81A00000` (3.3 MB on the Xbox). The pools of objects,
  effects, particles, contrails, lights and sounds are also larger.

Obey these rules:

- All the machines in a game must use a build with the same limits.
- The port uses protocol version 2. It does not see the Xbox game or older
  builds of the port. They do not see the port.

These are the differences from the Xbox:

- The host waits up to 60 seconds (15 seconds on the Xbox) for the other
  machines to load the map.
- If a machine does not read the messages of the host for two seconds, the
  host removes it from the game.
- The saved games contain all the game state. Thus a saved game is 16 MB.
  Saved games from older builds of the port do not operate.
- In campaign and in games of up to 16 players, the game removes garbage
  (bodies, dropped weapons) as on the Xbox. In larger games, it keeps more
  garbage, in proportion to the players.
- The lobby shows the local machine and the first three remote machines.
  The other machines are also in the game.
- In free-for-all games, each player is a team.

Linux, Windows and Android machines can play in the same game. Each machine
simulates the players from the same inputs, and the host does not correct
all of the game. Thus each machine must calculate the same floating-point
results, and all the ports:

- Compile without fused multiply-add (`-ffp-contract=off`).
- Use the math functions of musl (`port/include/halo_math.h`,
  `port/third_party/musl-math`), not the math functions of the system.

### Play on one computer

More than one copy of the game can play on one computer. Each copy must
have a different loopback address. A copy with an address gets no
broadcasts. Thus each copy must send its broadcasts to the other copies.

For a host and two clients, enter these commands in three terminals:

```sh
HALO_NET_ADDRESS=127.0.0.200 HALO_NET_BROADCAST=127.0.0.201,127.0.0.202 build/linux/chupathingy
HALO_NET_ADDRESS=127.0.0.201 HALO_NET_BROADCAST=127.0.0.200 build/linux/chupathingy
HALO_NET_ADDRESS=127.0.0.202 HALO_NET_BROADCAST=127.0.0.200 build/linux/chupathingy
```

Do not give 127.0.0.1 to a copy. Each copy gets to its own address through
127.0.0.1. Linux and Windows send all of 127.0.0.0/8 to the loopback
interface.

### Test with many machines

`tools/system_link_bots.py` adds simple machines to a game. Each machine has
one player. The machines obey the system link protocol, but they do not
calculate the game or move their players.

1. Start a game on the host.
2. Enter `python tools/system_link_bots.py --host 127.0.0.200 --machines 127 --start`.

Each machine uses its own loopback address, from 127.0.0.2. The option
`--start` starts the game when all the machines are in the lobby. If the
host has no `network.address`, do not give `--host`.

## Internet play

Machines with an invite link can play system link on the internet. This
project has no server.

When a copy of the game starts to host a system link game, it makes an
invite link: `halo://join/<64 hexadecimal digits>`. The game writes the link
to the standard error. With `network.copy_to_clipboard` set to `true`, it
also puts the link on the clipboard; INVITE LINK in the Gearbox menus'
Server Settings copies it at any time. The game reads the clipboard only when you paste (PASTE
LINK, Ctrl+V), or when `network.join_from_clipboard` is `"ask"` or
`"always"`. The links of older
versions of the game (44 digits) do not operate. The game writes a message
when it gets one.

To join a game, do one of these steps:

- Open the link. The game is the handler of `halo://` links. If the game
  already operates, the new copy gives the link to it and stops. A key in a
  file that only the user can read (`halo-ce-universal.key` in
  `$XDG_RUNTIME_DIR`, else `~/.halo-ce-universal.key`; on Windows in
  `%LOCALAPPDATA%`) encrypts the link, so the programs of other users cannot
  read it.
- Copy the link and select PASTE LINK in Join Game's Direct Link. With
  `network.join_from_clipboard` set to `"ask"` or `"always"`, going to the
  game after copying the link is enough.
- Enter `chupathingy <link>`.
- Accept a Discord invite. Refer to "Discord".

When the machines connect, the game of the host shows in Multiplayer,
System Link. Join the game as on a local network. System link on a local
network does not need an invite.

### Server browser

Server Setup in Create Game > Internet has a LISTING row:

- PUBLIC (the default): the game also shows in Join Game > Server Browser
  on every machine. Anyone can see and join the game.
- PRIVATE: only players with the invite link can join.

Each new game starts as PUBLIC (`network.host_public = false` makes new
games start as PRIVATE). An online co-op game starts as PRIVATE, and keeps
the last choice of its LISTING (`network.coop_public`). A LAN game is never listed. `network.public_lobby = false` turns the server browser off.

A PUBLIC game can also have a PASSWORD (a row of Server Setup, below
LISTING). The Server Browser shows a lock at the left of a game with a
password. A player who selects that game must type the password, and JOIN
GAME joins only with the correct password. The invite link of a game with a
password also asks for the password before it joins, wherever the link
comes from (a copied link, a link opened in the browser, Discord). A wrong
password is refused, and the game asks again. The host keeps the password
only while the game runs.

In the Server Browser, select a game to join it. The game joins the invite
of the game, as for a link. When it reaches the host, it opens the lobby.
If it cannot reach the host in 30 seconds, it marks the game FAILED. A game
that is full or starting shows CLOSED. REFRESH asks the hosts for their
listings again.

How it operates (`src/p2p_lobby.c`):

- The host of a public game publishes a listing: the invite, and the name,
  map, gametype and player counts of the game. The listing goes to the
  same MQTT brokers as the invites (`network.brokers_file`), retained,
  to a topic of the host (`hceu/3/lobby/s/<hash of its key>`).
- The key of the host signs the listing (Ed25519). The key is the key of the
  invite, so no other machine can list the invite of the host, change its
  listing, or list a game with false details.
- The host publishes the listing again every 30 seconds, when the game
  changes, and when a browser asks. If the topic of the host is empty or
  holds another listing, the host publishes again in 5 seconds or less. Thus
  a broker that deletes the listing does not remove the game.
- When the host stops (it stops hosting, the game becomes private, or the
  game quits), it publishes a closed listing, and then empties its topic.
  If the host loses its connection, the broker empties its topic (the will
  of the connection). A browser removes a game that it does not hear for 90
  seconds.
- When a public game becomes private, the host makes a new invite. Thus a
  player who saw the listing cannot join with the old invite.
- The listing of a game with a password does not hold the invite in clear
  text. The secret part of the invite (its token) is encrypted with a key
  from the password (Argon2id, salted with the key of the host, then
  XChaCha20-Poly1305). The browser makes the key from the password that the
  player types, and opens the invite only if the password is correct. When
  the host sets or changes the password, it makes a new invite. A player who
  has the listing can try passwords on their own machine without the host,
  so use a long password.
- The invite link of a game with a password (`halo://join/p...`) holds a
  random door, not the token. The token is a key from the password and the
  door (Argon2id, salted with the key of the host, then HMAC-SHA256), and
  the host listens only with the token. The link also holds a short check
  of the password, so a wrong password is refused at once. Older versions
  read the link as no invite. As with the listing, a player who has the
  link can try passwords on their own machine, so use a long password.

A public game does not publish the address of the host. But any machine
with the invite can ask the host to connect, and the host then sends its
addresses. Thus anyone can learn the address of the host of a public game,
as for any public server.

The brokers are in `brokers.txt` next to the executable (from
`port/assets/network/brokers.txt`; on Android, the app writes it next to
`config.toml` at each start), one `host:port` on each line. The game uses
all of them at once (up to 4), so one that works is enough. Without the
file (the dedicated server's container, the macOS application, whose
`config.toml` is in Application Support), the game uses its own copy of the
list. An update replaces `brokers.txt`: to use brokers of your own, put them in another
file and name it in `network.brokers_file`. All the players must use the
same broker to see each other's games. The game uses
MQTT 5 if the broker has it, else MQTT 3.1.1. A broker that does not keep
retained messages, or does not let clients subscribe with wildcards, carries
only invites, not listings.

Chupathingy's own parts of the server browser:

- A game hosted from the Xbox menus (System Link, or Online Games) is an
  internet game too, and public as `network.host_public` says, so it shows
  in the Server Browser of OpenCE and Chupathingy. The dedicated server
  (`server/`) is public unless `HALO_DEDICATED_PUBLIC=false`.
- Builds with the game list (`configure.py --game-browser`) also show the
  games of `network.browser_url` (stats.chupathingy.org). While the Server
  Browser is open, the game reads the list with an HTTPS GET of
  `/v1/games.txt` (at most every 5 seconds), and shows the games of its
  network version, not its own. A game that is also listed on the brokers
  shows once, with its listing (the same invite token). Joining a game of
  the list joins its invite, as for a link.
- A game on a Halo PC map, listed as `<map>@ce` (`@md`, `@pc`), shows PC
  (MD) after the map's name. It can be joined only with the map in its
  folder (`maps_ce/`, `maps_md/`, `maps_pc/`),
  on a build that plays Halo PC maps (`HALO_CUSTOM_EDITION`); otherwise
  the Server Browser says what is missing (`game/server_browser.c`).
- Column titles sort the games (players, name, map, gametype, ping). Select
  a title again to sort the other way. The chosen game stays chosen while
  the list changes. The lines below the rows show the players (by name,
  when the host sends a roster) and the rules of the chosen game.

### Security

Only machines with the invite can find the game:

- Each copy of the game makes an Ed25519 key pair when it starts, and from
  it an X25519 key pair (Monocypher, `port/third_party/monocypher`). Its
  identifier is from the hash of its X25519 public key. The Ed25519 key
  signs the listing of a public game.
- The link contains a 16-byte hash of the public key of the host and a
  random 16-byte token. The identifier of the host is from the first 6
  bytes of the hash.
- The machines exchange their public keys and addresses through public MQTT
  brokers (`network.brokers_file`). The topics are HMACs of the token.
  A key from the token encrypts and authenticates the messages
  (`src/p2p_signal.c`, `src/p2p_crypto.c`). The host authenticates its answer
  with a key that only it and the player can calculate. Its public key must
  agree with the hash in the link. The hash is long, so no other machine can
  find a key with the same hash.
- Then the player shows in the same way that it has the private key of its
  public key. Only then does the host make a session for the player. Thus
  other machines with the invite cannot make sessions in the name of a
  player (such a session would keep the player out).
- Each two machines get the keys of their packets from their key pairs and
  a random number from each. The keys do not go through the brokers. Thus
  other machines with the invite cannot read or change the packets.
- Each packet is encrypted and authenticated, with a different key in each
  direction. A machine ignores a packet that it already received.
- A machine can send only to the ports of the game on the other machine.
- The host makes one session from each request of a player. If a person
  sends a copy of an old request again, the host ignores it. A player that
  must ask again sends a new request.
- The host tries to reach at most 8 new players at the same time. The
  other players ask again.
- The host answers a request that is not proven at most one time each
  second through each broker. It answers at most 20 of these requests each
  second, after a first 32. Each answer goes only through the broker that
  brought the request. Thus a flood of requests does not use much of the
  bandwidth of the host.
- The host does the key work of at most 20 requests each second from keys
  that it does not know, after a first 32. It keeps the key work of the
  last 256 keys. Thus the proof of a player does not need more key work. A
  flood of requests can make players join more slowly. A player asks again
  for 90 seconds.
- The host refuses the predicted movement of a player whose game runs
  faster than time (a speed hack). If the messages on the player's
  connection were also ahead for ten seconds, the host drops and bans the
  player: each player sees who in red on the console, and the host adds a
  line to `cheaters.txt` and `bans.txt` (beside `debug.txt`) with the
  address and hardware id of the player, and the Discord name and id that
  the game of the player told it, marked `(self-reported)` (a player can
  change these). The host refuses a machine whose address or hardware id is
  in `bans.txt`. If only the player's datagrams were ahead, the host does
  not drop the player, because another machine can send datagrams with the
  player's address: it adds an `unverified` line to `cheaters.txt`.
- The host can ban a player with `ban <player name>` in the developer
  console (Tab completes the name). Remove a line from `bans.txt` to unban.
  Refer to `NETCODE.md`. `kick <player name>` drops the player the same
  way, but keeps nothing: no line in `bans.txt`, and the player can join
  again at once. The host can also do both from the pause menu: MODERATION
  (or Y, or M) lists the other players. Pick a player and Kick or a Ban
  (1 hour to forever), then confirm with A. Left and right pick a reason,
  which is optional. A timed ban ends by itself. Only the host has this,
  and the host's own machine is never on the list. In co-op, `bringto`
  brings every player to the host.
  So that every player can be named, the host trims the spaces around a
  name and removes characters that draw as nothing. A
  letter with a mark is typed as the plain letter (`ban jose` for "José").
  A name with nothing left to type becomes "Player", and a name that another
  player already has gets a number ("Player 2"). The game refuses a profile
  name that is blank, and a multiplayer game refuses a profile whose name was
  made blank before this check.
- The host keeps each machine's lobby requests (its settings, its players,
  joining and leaving) to a burst of 32, then 16 a second. Requests past
  that are dropped. The host's own machine has no limit.
- The host refuses a client's hit on a target behind a wall
  (`network.hit_line_of_sight`), and a hit report stamped before the game
  began. On a Custom Edition map, it refuses a client's update with a
  facing that is not a number or is far too large.
- In a public internet game, the host's own console and cheats cannot
  change the game once another machine is in it, unless
  `network.host_cheats` is on. In every network game, they may make only
  `network.host_console_objects` objects.
- An invite operates while the copy of the game that made it operates.

### Connection

Each machine gets its public address from public STUN servers. Then the two
machines send packets to each other until the packets get through (UDP hole
punching). There is no relay.

Some networks give a different port for each destination (for example some
mobile and company networks). Two machines behind such networks cannot
connect. To connect, forward the host's port (UDP 2302 by default,
`network.tunnel_port`) on its router to the host. Server Setup's status line
names the port, and says "Forward UDP <port> on your router to this
computer" when UPnP did not forward it.

The game can ask the router to forward the port (UPnP,
`src/posix_upnp.c`, with `port/third_party/miniupnpc`):

- The host asks its router when it hosts an internet game, for the port
  it hosts on. Another copy of the game asks when a player uses its invite.
- A player that joins asks its router when it does not reach the host in
  5 seconds.
- The forwarded port is one more address that the machine gives to the
  other machine.
- The forward has a duration of one hour. The game makes it longer while
  it operates. When the game stops normally, it removes the forward. It
  does not remove the forward after a crash, or if a request to the router
  is still under way 3 seconds after the game starts to stop. Some routers only make forwards without a duration.
- When the game finds the router, it removes the forwards to this machine
  that have the description "Halo internet play" and that no copy of the
  game uses now (forwards that a copy of the game did not remove).
- UPnP does not help behind a second NAT, for example the NAT of a mobile
  network provider. Then the router has a private address, and the game
  does not ask.

To stop all UPnP requests, set `network.allow_upnp` to `false`.

In the game, each machine has an address in 100.64.0.0/10:

- `src/xnet.c` gives the datagrams that the bound UDP sockets of the game
  send to such an address to `src/p2p.c`. Other datagrams (of sockets that
  are not bound yet, or that are connected to the address) and the TCP
  connections of the game go through local sockets on 127.0.0.1 (or
  `network.address`). The traffic from the other machines comes to the
  game from local sockets too.
- `src/p2p.c` sends that traffic through one UDP socket. UDP datagrams go
  as they are. TCP connections go as KCP streams (`port/third_party/kcp`).
- The broadcasts of the game go to all the machines. Thus the game of the
  host shows on the other machines.

### Discord

If the Discord desktop client runs, Discord shows what the player does
(through the application of `discord.application_id`, whose name is what
Discord says is being played): the game type and the map ("Team Slayer on
Blood Gulch"), a team game's score, the network game's name and players,
the time since the game started, or the campaign's level and difficulty, or
the menus. The pictures are the game list site's, by URL. A host of a public
game (in the server browser, with no password) gets a Join game button: it
opens the game's page on chupathingy.org, which opens the invite
(`halo://join/`) in the game of the person who clicks it, Chupathingy or
OpenCE. Otherwise the buttons go to the project's site and Discord server.
Discord's own Ask to Join is not used: it works only between players of the
same Discord application. `discord.presence`, `discord.show_server_name` and
`discord.show_join` choose what shows; `debug.discord_log` logs each
activity sent (without the invite).

### Online Games and the profile

Builds with the game list (`configure.py --game-browser`) have Online Games
in the Multiplayer menu: the games on `network.browser_url`
(stats.chupathingy.org). The screen looks like the Xbox's menus, drawn by the
game itself in the menus' fonts (`port/assets/fonts`). Each game's map has a
tag for its family: XBX (the Xbox's maps), HCE (Custom Edition), HMD
(HaloMD) and GBX (Halo PC retail). It lists every game the Server Browser
lists, co-op games too. A lock marks a game with a password, and A asks for
the password before it joins. The ping column shows the round trip to a host
that this machine is connected to (a game that you join), else nothing:
nothing measures a host before you join it. Sorted by ping, such games come
last. The details show the map's picture from the menus, or from Halo PC's
`ui.map`. Without either, a stock map's picture comes from the site
(stats.chupathingy.org/art/maps), downloaded in the background only while
`network.online` is on and kept in the save root's `map_art` folder. So
does a HaloMD or Custom Edition map's picture, when the site has one
(art/maps/md, art/maps/ce). Any other map gets a card with its name in its
family's color. RB (Link Profile)
shows only while this copy is not linked to a profile: linked on this copy
(kept in the save root's `game_list_profile.txt`), or a profile the site
has for this copy's player ID. Once linked, Start's prompt names the
profile. Each copy of the game has a player key in the save root
(`game_list_player.key`). The key confirms the player's lines in finished
games. The game sends the key only to an HTTPS server, or to a server on
this computer (`http://127.0.0.1`, `http://localhost`) for tests.

To link the game to a profile on the site, do one of these steps in Online
Games:

- Press Start. The game opens the profile page in the web browser, signed
  in as this player.
- Press RB, or C on the keyboard (Link Profile). Use this step where no web
  browser opens: Steam's Game Mode, a Steam Deck, a console. The game shows
  a short code and a QR code. On a phone or a computer, go to
  `<server>/connect` (the address that the game shows), sign in, and enter
  the code, or scan the QR code. Then the game asks "Connect this game to
  <name>?" (or "Move this game from <old name> to <name>?"). Press A to
  connect, or B to cancel. A code operates for two minutes, and the question
  for two minutes. Press RB (or C) for a new code.

You link once. The link stays until you change the save root or the player
key: the profile keeps the link, and the games the key played before it
count too. Linking by code needs no key import.

To use a key from another computer (or a backup of the profile's key) on
Linux, either copy `game_list_player.key` into the save root, or start the
game with the key's link as its argument: `chupathingy 'halo://key/<64 hexadecimal
digits>'` (the profile page's "Copy key link"). The game asks before it
replaces its key. The file is the 32 bytes of the key. The game ignores a
key file that another user owns or that others can read, and then has no
key at all: `chmod 600 game_list_player.key`. The profile page's Install in
Game button needs a desktop that opens `halo://` links for the game
(it registers itself at start); on a Steam Deck's Game Mode, or in a
sandbox, use the argument.

Link Profile uses these requests to the server (`src/browser.c`, on the
thread of the game list): `POST /v1/connect/start` with the key (and the
name of the profile) gives `ok <code> <seconds> <token>`.
`POST /v1/connect/status` with the token, each 3 seconds, gives `pending`,
`confirm <name> [<old name>]`, `connected <name>`, `declined` or `expired`.
`POST /v1/connect/confirm` with the token and the answer gives
`connected <name>`, `declined` or `expired`. The QR code is from
`port/third_party/qrcodegen`.

## Bots

The host of a multiplayer game can add bots. A bot is a player that the
host moves. The bots play all the game types on all the multiplayer maps:
they get weapons and powerups, drive and ride the vehicles, carry the flag
and the ball, and hold the hill.

### Bots in the menus

With the Chupathingy menus, the host sets the bots with a controller, the
keyboard or the mouse. Up and down choose a row, and left and right change
it:

- **GAME SETUP**, which comes after the game type and before the lobby, for
  split screen and for System Link (and Online Games): **BOTS** and **BOT
  SKILL** are rows of GAME SETUP. **BOT OPTIONS** opens the other rows (FILL
  GAME TO, SKILL SPREAD, KEEP GAMES CLOSE), and B goes back to GAME SETUP.
  START GAME keeps the values in `config.toml` for the next games and opens
  the lobby. Split screen's GAME SETUP has only the game type, MAX
  PLAYERS (room for the bots: more than 15 bots need more than 16
  players), the bots and START GAME: the other rows are for games on a
  network.
- **The lobby**: the host presses Y (the hint "Y BOTS" is under the button
  key) to open the bots' rows. The lobby gets the new bots at once.
- **The pause menu** of a game that this machine hosts: **BOTS** opens the
  bots' rows. Bots join or leave the game one each second. A new skill
  applies to all the bots at once. A or B closes the rows and keeps the
  values in `config.toml`.

The rows:

| Row | Values | Setting |
| --- | --- | --- |
| BOTS | `NONE`, `1` to one less than MAX PLAYERS (127 at most, in a game of 128); `AUTO` when FILL GAME TO is on | `bots.count` |
| FILL GAME TO | `OFF`, `2` to MAX PLAYERS: bots fill the places that people do not have | `bots.fill` |
| BOT SKILL | `1 EASY` to `10 NEAR PERFECT` | `bots.skill` |
| SKILL SPREAD | `NONE`, or a range below BOT SKILL | `bots.skill_spread` |
| KEEP GAMES CLOSE | `ON`, `OFF` (only with a skill spread) | `bots.balance` |

With the Xbox's menus (`display.menus = "xbox"`) or Gearbox's, which have no
GAME SETUP, the same rows are on a BOTS screen of their own: the host
presses Y in the lobby (split screen's and System Link's: the hint "Y BOTS"
is under the button key), or chooses BOTS in the pause menu. A or B closes
it and keeps the values in `config.toml`. These menus have no MAX PLAYERS: the
game has room for 128 players, and BOTS goes up to 127.

X sets all the rows to the default values. A game ends when only one team
is left, as when players quit: if you remove the last bot from a game that
only you play, the game ends.

### Bot settings

- `bots.count` in `config.toml` gives the number of bots (`0` by default:
  no bots). `bots.skill` gives how good they are, from `1` to `10`
  (`5` by default). The ten levels are even steps from the easiest bot to
  the best one. The levels went from `1` to `5` before, with the same
  easiest and best bots: the old `5` is `10`, the old `4` is near `8`, the
  old `3` is between `5` and `6`, and the old `2` is near `3`. Change a
  value that you set for the old levels.
- In the console, `bots <number>` and `bot_skill <1-10>` change these values
  until the game stops. A new number of bots applies in the lobby at once,
  and in a game in progress one bot each second.
- `bots.skill_spread` gives the bots different skills. With a spread of
  `4` and a skill of `10`, each bot has a skill between 6 and 10 (not only
  whole levels: 7.3 is possible). A bot keeps its skill for the game and
  gets a different one in the next game. In the console, `bot_skill_spread
  <0-9>` changes the value.
- `bots.balance = true` keeps the games close. The skill of each bot then
  moves in the range of `bots.skill_spread` as the score changes: the bots
  of a team that is ahead play near the bottom of the range, and the bots
  of a team that is behind play near the top. The move is full when a team
  is ahead by a third of the score to win (2 at least). In a game with no
  teams, each bot compares its score with the best score of the other
  players. With a spread of `0`, nothing changes. In the console,
  `bots_balance <0|1>` changes the value.
- The name of a bot ends with `[Bot]`: `Anvil [Bot]`. The lobby, the
  scores, the kill messages and the names above the players show this
  name. A name has 11 letters at most, thus the names of the bots have 5
  letters or fewer.
- `bots.fill` gives a number of players, not a number of bots: the bots
  fill the places that people do not have. A bot goes when a person joins,
  and a bot comes back when a person goes, in the lobby and during the
  game. In the console, `bots_fill <number>` changes the value. `0` stops
  this, and the game has `bots.count` bots.
- The bots are in split screen games, system link games, internet games
  and the games of a dedicated server. Only the settings of the host apply.

The bots are players of the lobby. They are on machines with the name
"Bots", four bots to a machine, and the lobby and the scores show them as
other players. They come when the first person is in the lobby. When the
lobby is full, a bot goes for each person who joins. In team games, each
bot goes to the smaller team, and a bot changes teams when a team is larger
by two players or more.

To the netcode, a bot is one of the host's own players: the host calculates
the input of each bot in each tick and sends the other machines the bots as
it sends its own player. The other machines need no settings, and no build
with bots: a machine with OpenCE, or with a ChupathingyCE without bots,
plays with the bots of a host. Refer to [NETCODE.md](NETCODE.md#bots).

A game that had bots in it sends no carnage report to the game list
(halo.milenko.org): the scores there are the scores of people. This applies
to the report of the host and to the report of a player who joined the
game. A player with a build that has no bots (OpenCE, or a ChupathingyCE
without them) does not know which players are bots, and that build sends
its report.

The first time that a map has bots, the host makes the navigation data of
the map (the places where a player can walk, jump, drop and climb). This
takes 1 to 10 seconds. The host keeps the data in `navcache` in the save
root, and reads it from there in about a second or less. The host does this
while the machines load the map. Thus the game does not stop at its start.

### Baked navigation

The navigation data of all the maps can be made before a game (baked). The
game then reads it from a `navcache` folder next to `maps`, on each machine
that has a copy of that folder, and the first game with bots on a map does
not wait.

- **Windows:** start `bake-navigation.cmd`, which is next to `halo.exe`.
  `bake-navigation.cmd new` bakes only the maps that have no baked data.
- **Linux and macOS:** start the game with `HALO_BAKE_NAVIGATION=all` (or
  `new`) in the environment.

The game then has no window and no sound, and it is not on the network. It
loads each multiplayer map that it finds in the `maps` folder (a campaign
map has none of this data) and, on the builds that play them, each Custom
Edition map (`<name>@ce`) and each HaloMD map (`<name>@md`). For each map it writes two files,
`navcache/<map>_0.height` and `navcache/<map>_0.aas`, and a line that starts
with `bake:`. The 13 maps of the game take 1 to 2 minutes and 200 MB. The
exit code is `0` when each map has its data.

The files of a Custom Edition map or a HaloMD map have the name that the
map is played under: `navcache/timberland_ce_0.aas` for `timberland@ce`.

A map with more than one structure BSP (Hugeass and Coldsnap each have two)
has two files for each: `<map>_1.height` and `<map>_1.aas` for the second.
Without baked files the game makes the data for a structure BSP when the
map's script changes to it, and the game stops for that time (10 to 30
seconds) a moment after it starts. Bake such a map.

The bake makes the data for each structure BSP of a map, and a map is baked
only when all of them are. A structure BSP that the game cannot change to is
a failure for that map. When the last one is done, the bake writes
`navcache/<map>.bsps`, which has the number of structure BSPs of the map.
`new` uses this number: a map is baked when the files of each of its
structure BSPs are complete and of this build. A `navcache` folder from an
earlier build has no `.bsps` files. For such a map, `new` examines the files
of the first structure BSP and of each subsequent one that has files, so it
cannot find a second structure BSP whose files were not written at all. Bake
with `all` one time to write the `.bsps` files.

A very large map (more than about 1,300 world units across: Coldsnap is
1,800, Hugeass is 2,200) has navigation data only for the parts where the
game is played. These parts are the ground near the player starting points,
the weapons and equipment, the flags and the vehicles. The game finds how
far from these points it can make the data within a limit on the size
(21 world units on Coldsnap, 34 on Hugeass) and writes one line that starts
with `navm: a very large map`. A bot moves with full navigation in the part
that it is in. It does not choose a place in a different part as a place to
walk to, because there is no route on foot between the parts. The files of
such a map are 100 to 150 MB for each structure BSP.

The game reads a file only when the file is complete and correct. It makes
the data again when a file is damaged. When the game does not have
sufficient memory to make the data for a map, it gives a line that says so,
and the bots play that map without navigation. It does not use or keep data
that is not complete.

A baked file is for the map that it was made from, and for the build that
made it: the game makes the data again, into the save root, when the map
file is a different one or when a later build has another format. Bake the
maps again after such a change. The code is in `game/bot_bake.c`.

### How the bots move and fight

The bots move and look as a person with a controller or a mouse does:

- The look starts to turn, gets faster, and slows before it stops. It does
  not go at full speed from one tick to the next and then stop dead.
- The look never turns faster than a person turns: 8 radians (about 460
  degrees) a second at the most, at every skill level. Before, the top
  levels turned up to 22 radians a second, and a bot of skill 6 or more
  turned more than 20 degrees in one tick about once a second in a fight,
  a snap onto its target that no stick or hand makes. Their aim and their
  reactions make the top levels good, not the speed of the turn.
  `HALO_BOT_OFF=neck` turns this limit off.
- From skill 8 up the look eases less, and at 10 not at all: up to its
  speed in a tick or two, and onto where it means to look without slowing
  short of it (still no faster than 8 radians a second).
  In 40 games of team slayer at skill 10 (Hang 'Em High, Battle Creek,
  Damnation, Chill Out, Prisoner), a team whose look eased as before
  scored 9.4 points a game fewer than one whose look did not (the parts of
  the movement that are a person's otherwise cost nothing: the thumb, the
  strafe's timing, the stick flat out); with this, 0.6 points (chance).
  `HALO_BOT_HUMANOFF_T0` and `_T1` (a mask: 1 the look's easing, 2 the
  thumb, 4 the strafe's timing, 8 the stick flat out) take a part away
  from one team, for tests.
  `HALO_BOT_OFF=master` gives the top levels the easing of the rest.
  In 40 games of team slayer at skill 10 (Hang 'Em High, Battle Creek,
  Damnation, Chill Out, Prisoner) with the limit on for one team, the
  score of that team did not change by more than chance.
- The movement changes direction in 2 to 4 ticks, as a thumb moves a
  stick. Where the route has a jump, a gap, a ladder or a ledge, the feet
  go exactly where the route says.
- In a fight a bot strafes at full speed. It keeps one direction for about
  half a second, sometimes for more than a second, and sometimes it makes a
  quick step to one side and back.
- A bot goes around a corner of its route in a curve, and it looks around
  the corner before it gets there. It keeps a little distance from the
  corners of walls, so that it does not catch on their edges.
- When a bot walks and hardly moves for 0.2 seconds (a lip, a seam between
  two surfaces), it jumps at once and steps to the side with more room.
- When a bot starts a jump, a drop or a ladder and does not move for 0.5
  seconds, it takes a different route at once.
- A bot goes to the place where an item is now, also when the item moved.
  If the game does not give it the item there, the bot steps onto the item,
  jumps for it, and goes on to a different goal after 1.2 seconds. Before a
  bot gets to a weapon that it will swap for, it takes the weapon that it
  will drop in its hands, and it makes the swap when it gets there.
- A bot that taunts goes to a body that is less than 6 world units away
  and crouches down and up 2 or 3 times, for about 1 second.
- A bot that walks with no enemy in view looks along its route. From time
  to time it looks to one side, and less frequently behind it, for about
  half a second.
- A bot goes only to a weapon that lies in the world. A weapon that a
  player has put away has no place in the world, and no bot goes to it.
- A bot is at an item only when it is on the same floor as the item. A bot
  that is on the roof over an item, or under it, goes on along its route to
  the item.
- While the route to an item continues (for example, a jump up to the
  platform that the item is on), the bot follows the route. The 1.2 seconds
  start when the route ends. Where the route ends before the item, the bot
  goes straight to the item when nothing is between them.

For the route, a jump up costs only the time of the jump, and a jump across
a gap costs the same as the floor next to it. Thus the bots jump when that
is the quickest way, also up to a place and then across from it. A jump
that fails costs more each time it fails, and the bots keep what they
learned in the save root.

**Jumps across gaps.** The navigation calls each place a gap where a jump
at a full run gets to the other side or farther: a break in a walkway, and
also a ledge that is 3 world units away and 2 below. The bots took all of
them at a full run with a jump. These are the changes:

- A bot keeps its full speed where the jump comes down on floor at the
  height of the other side. Where it does not (a ledge with nothing behind
  it), the bot runs at the speed that puts it 0.05 to 0.5 world units onto
  the ledge. When the ledge is more than 0.5 world units below and near,
  the bot runs off the edge and does not jump.
- In the air, a bot steers to the place where it must come down. The game
  lets a body in the air change its speed by about 1 world unit a second
  each second. The bot computes where it comes down with the speed that it
  has, and pushes against the difference.
- A bot jumps only when it moves toward the other side (the speed along
  the line of the jump, not the speed in a different direction). Without
  that speed at the edge, it goes back for its run. Before, a bot that
  came to the edge sideways or backward jumped and fell into the gap.
- Some gaps that the navigation says a bot walks across (a slot less than
  0.2 world units wide) are as wide as a body where a bot crosses them
  (between two decks of Boarding Action, across a corner). A bot looks at
  three points of the gap. If one has no floor 0.6 world units below, the
  bot jumps the gap.
- A bot that pushes at something on its way to a jump or a gap, with the
  next point of the way more than 0.12 world units above its feet (the
  bank of the stream of Beaver Creek), hops after 0.17 seconds.
- A bot that stands 0.6 world units or more below the two ends of a jump
  or a gap stops that jump at once and plans its route again. Before, it
  made the run and the jump on the floor below, against the wall, for 3
  seconds. The time for a way that goes down includes the time of the
  fall.

These are games of 5 minutes of slayer on eight maps (Beaver Creek,
Boarding Action, Chill Out, Damnation, Longest, Prisoner, Chiron TL-34,
Hang 'Em High), 6 to 10 bots:

| | Before | After |
| --- | --- | --- |
| Jumps, gaps, drops and ladders that failed, for each 10 minutes of a bot (seed 5) | 5.0 | 3.0 |
| The same (seed 7) | 5.9 | 3.2 |
| Gaps that failed (seed 5) | 72 of 592 | 38 of 522 |
| Gaps that failed (seed 7) | 84 | 35 of 542 |
| Kills in the eight games (seed 7) | 602 | 588 |

With `HALO_BOT_LOG` set, the log has a line for each jump, gap, drop and
ladder that a bot makes or does not make. `HALO_BOT_REACHTRACE=1` adds a
line for each second tick of one, and `HALO_BOT_WALKTRACE=1` a line for
each third tick of a walk that does not get on.

**Jumps up onto ledges and rocks.** These are the changes:

- A bot jumps onto a ledge that is 0.76 world units above it or less. The
  limit was 0.6, and then 0.72 (see "Jumps at a run" below for the test
  of 0.76). The jump of a player gets 0.69 world units high. When a
  body folds its legs in the air, the game lifts it 0.16 world units more,
  and the bots fold their legs. In a test, a bot got onto a ledge 0.69
  world units above it each time that it left the ground in front of the
  ledge. The navigation data has the new jumps (the host makes the data of
  each map again one time, in 1 to 10 seconds).
- The navigation also finds a ledge that is to one side of the place where
  a bot stands (a diagonal jump), not only one that is straight in front.
- A bot starts the jump only when the game will make it: the body is on
  the ground, it stands on a surface that it can stand on, and its legs
  are straight. The legs lift a body only when they fold in the air. A
  bot that jumped again one tick after it came down, with its legs folded,
  made a low jump or no jump.
- A bot holds the jump button for up to 8 ticks, until its body goes up.
  Before, the button was down for one tick. The game does not jump in a
  tick in which the body is in the air, and a body that runs across a
  rough rock is in the air on each second step.
- A bot goes to the front of a ledge before it jumps. A bot that came
  along the side of a ledge jumped from there, went up along the side and
  came down past the end of the ledge.
- A bot does not jump while it moves across the line of the jump at more
  than 0.6 world units a second (0.3 for a ledge 0.55 or more above it).
  It slows down for up to 0.8 seconds first.
- When the ledge is small (no floor 0.7 world units behind its edge, as on
  the top of a rock), the bot comes to it at half speed and steers in the
  air to come down 0.15 world units behind the edge.
- A bot that comes to the start of a jump or a gap with no floor behind
  the start goes the last 0.8 world units at 65 % and then 45 % of its
  speed.

**The cost of a gap.** The navigation gave a gap the time of the run
across it and no more. In games on eight maps, a gap took 1.5 seconds
more than that (the middle value): the bot comes to the edge, goes back
for its run, and jumps. Thus the bots made a jump of 3 seconds to walk
1 world unit less. These are the changes:

- A gap costs 1 second more than its run. After each gap that a bot makes,
  the route planner corrects the cost of that gap by half of the
  difference from the time that it took, in steps of 0.5 seconds, up to 6
  seconds. The host keeps these costs with the data of the map, as it
  keeps the jumps that fail.
- The run before a gap starts 1 world unit behind the edge. It was 1.6. A
  body gets its full speed in 0.65 world units. For a gap that the bot
  takes at less than its full speed, the run is shorter (0.5 world units
  or more).
- The run does not go up across a bump of 0.2 world units or more. A body
  that runs across the top of a rock is in the air there, and the game
  does not jump.
- A bot jumps up to 0.8 world units before the edge when the jump from
  there gets to the other side. The last part of a rock is round, and the
  game does not jump from a slope.
- A jump or a gap that comes down on a slope of more than 47 degrees, with
  no flat ground 0.15 world units to each side, is not a way. A body
  slides off such a slope.
- When a bot is 1.2 world units or more below the end of a jump or a gap
  less than 1.5 seconds after it made it, the jump counts as failed.

**Hops.** A bot hops when a step 0.2 to 0.75 world units high is in front
of it. It now computes where the hop comes down, with the speed that it
has and the speed that it gets in the air. It does not hop when the hop
comes down more than 0.9 world units below the place where it started, or
when no floor is there to stop on. Before, a bot on a narrow rock hopped
across the rock and came down on its other side.

**Sharp turns.** A bot slows down for a turn of more than 50 degrees when
no floor is 0.5 world units behind the corner in the direction that it
moves (the end of a ledge, the shoulder of a rock). It goes at 65 % of
its speed from 0.8 world units and at 45 % from 0.4. On other floors a
bot takes its corners at full speed.

**The big items.** A rocket launcher, a sniper rifle, an overshield and an
active camouflage are worth half of their value at 30 world units of
walking (20 for the other weapons that the bots value). These are the
changes:

- A bot stops for 45 seconds from an item that it does not get nearer to.
  It measured this in a straight line. A way to an item can go away from
  the item first, thus the bot now measures the walking distance.
- A bot keeps an item as its goal for the time of the walk to it and half
  as long again (20 seconds or more). It was 20 seconds.
- An item that the bot already goes to is worth 1.5 times its value.
  Before, a bot on its way to a rocket launcher changed its goal when a
  different goal was worth a little more for one second.
- A bot that goes to an item does not turn to a weapon that is worth less
  on its way.

These are tests on Battle Creek with bots of skill 10. In the first test
one bot goes to the rocket launcher on the arch (the way goes up a rock, a
ledge and a ridge). In the next tests one bot goes to the roofs of the
bases. The way to the east roof is a rock, a strip 0.7 world units above
the rock, the roof of the porch 0.66 above the strip, and a gap to the
main roof. The way to the roof of the west porch is a rock, a gap of 2
world units to the top of a wall, and the roof 0.69 above the wall.

| | Before | After |
| --- | --- | --- |
| Bots that got to the rocket launcher in 90 seconds | 10 of 12 | 16 of 16 |
| Time to the rocket launcher (the bots that got there) | 10 to 71 s, mean 39 s | 10 to 28 s, mean 18 s |
| Bots that got to the main roof of the east base | no way | 8 of 8, in 13 to 24 s |
| Bots that got to the roof of the east porch | no way | 8 of 8, in 10 to 17 s |
| Bots that got to the roof of the west porch | no way | 6 of 8, in 12 to 19 s (one in 63 s) |
| Two games of slayer, 7 minutes, 8 bots: samples of a bot with the rocket launcher in its hands | 0 of 456 | 12 of 476 |

In the next test, four bots go around the items of a map, one item after
the other, for 5 minutes (`HALO_BOT_GOTO=tour` with `HALO_BOT_TOUR`). A
bot stops a leg after 60 seconds. The maps are Battle Creek, Chill Out,
Damnation, Hang 'Em High, Prisoner, Longest, Boarding Action, Wizard, Rat
Race and Derelict:

| | Before | After |
| --- | --- | --- |
| Legs that the bots completed | 849 | 929 |
| The mean time of a leg | 12.5 s | 11.9 s |
| Legs that a bot stopped after 60 seconds | 20 | 15 |

In games of 5 minutes of slayer on eight maps (8 bots, skill 10), the
jumps, gaps, drops and ladders that failed went from 2.8 to 2.4 for each
10 minutes of a bot. The bots took 222 gaps (467 before) and 167 jumps up
(99 before).

**The barricades of Longest.** On Longest, a barricade stands 1.3 world
units in front of each flag. Its middle is 0.4 world units high, and the
navigation has a hop across the middle. A bot that went to a flag, or
came home with one, ran straight at the barricade. The hop took 2.2
seconds, and the walk around the barricade takes 1.2 seconds. With the
cost of a gap (see above), the route of a bot goes around the barricade.
In a game of capture the flag of 7 minutes with 8 bots, no bot hopped
across a barricade.

**Hops at nothing.** In games on six maps, a bot of skill 10 jumped 6 times
a minute with no enemy in view. Most of these jumps had no cause that a
person can see. These are the changes:

- A bot that walks and hardly moves is "held up": it hops and steps to one
  side. A body in the air moves slowly (more so with its legs folded), and
  the bot counted that as held up. It hopped again in the tick in which it
  came down, and went along a ledge in hops. A bot in the air is now not
  held up. A bot that slips on a slope is.
- A bot hops at a step in front of it only when it is on the ground.
- A bot that moved less than 0.35 world units in a second is stuck: it
  jumps, and steps to one side the second time. A bot that turned around
  and came back to the same place was stuck by this rule. It is now not
  stuck when it was 0.9 world units or more away in that second.
- Near a corner of its route, a bot steers to a point on the next part of
  the route when the floor to that point is clear. When it was not clear,
  the bot steered at the corner, and it changed between the two from one
  tick to the next. It now tries a point half as far along, and then a
  quarter.
- Across a step that the navigation calls a walk (floor on both sides, 0.12
  world units or more up), a bot hops when it is at the step and on the
  ground. Before, it hopped each 20 ticks.

These are six games of 5 minutes with 8 bots of skill 10 (Battle Creek,
Chill Out, Damnation, Prisoner, Hang 'Em High: slayer; Longest: capture the
flag), with `HALO_BOT_REFINE=0` (before) and without it. The numbers are
for each 10 minutes of a bot on foot:

| | Before | After |
| --- | --- | --- |
| Jumps with no enemy in view | 61 | 27 |
| Jumps with an enemy in view | 66 | 39 |
| Hops because the bot was "held up" (all six games) | 691 | 132 |
| Hops at a step (all six games) | 240 | 58 |
| Times that a bot was "stuck" (all six games) | 104 | 76 |
| Times that a bot came back to a place 3 to 8 seconds later, no enemy in view | 21 | 14 |
| Seconds without movement, with an enemy in view | 19 | 15 |
| Kills in the six games | 512 | 502 |

In the test with four bots that go around the items of a map (nine maps:
the ten maps above without Derelict), the bots completed 877 legs in a
mean time of 11.9 seconds. With `HALO_BOT_REFINE=0` they completed 844 in
12.3 seconds.

`HALO_BOT_MOTION=<ticks>` writes the position, the speed and the controls
of each bot on foot to the log each so many ticks. `HALO_BOT_REFINE=0`
turns these five changes off (a sum of 1, 2, 4, 8 and 16 turns on the
change for held up, corners, walked steps, stuck and hops at a step).

**Aircraft.** These are the changes:

- On its way to a place, a craft keeps the height that it has. Before, it
  came down to its least height above the ground (10 world units for a
  Pelican) and followed each rise of the ground. It now comes down only
  along a slope of 1 in 4 to the place where it lands or fights. Each
  pilot also likes some more height than the least (4 to 70 world units,
  drawn again each half minute).
- The room that a craft keeps is by its size. The obstacle rays came from
  a wing 2 world units out, and a Pelican is 11 world units across (a
  Longsword is 24). The rays now come from the ends of the craft, and the
  least height above the ground is more for a large craft.
- A run down at someone ends when the ground is nearer than the craft
  sinks in 1.3 seconds.
- A Longsword with a bomb bombs. It was a craft that carries people,
  because it has seats, and thus it did not attack. Its bomb kills 71
  world units away and hurts 80 world units away. The pilot now goes for
  the place where the bomb kills the most enemies, with no player of its
  own team within 95 world units. It does not choose enemies under a roof
  that is more than 35 world units above them. It flies level over the
  place, higher than the blast reaches (125 world units above where the
  bomb lands). It lets the bomb go when the game's own arithmetic for the
  fall puts it within 21 world units of the place. Then it flies on at
  that height for 11 seconds, the way that has no cliff in it. The bots do
  not get into a Longsword that a bot flies.

These are two games of capture the flag on Hugeass with 11 bots, with
`HALO_BOT_CRAFT=pelican` (the bots prefer Pelicans), with
`HALO_BOT_REFINE=31` (before) and without it:

| | Before | After |
| --- | --- | --- |
| Height of a Pelican above the ground on its way (median) | 15 | 70 |
| Part of that time under 6 world units | 16% | 2% |
| Bombs from Longswords, and kills from Longswords | 0 and 10 | 15 and 22 |

The count of bombs before is from two games of the same kind before
this change.

**Drivers.** These are the changes:

- The driving data now has the trees and the rocks of a map. A floor
  with one of them within 1.8 world units costs four times as much, and a
  straight line of the route does not go through it.
- A driver looks down three lines (the middle and the two sides of the
  vehicle) as far as the vehicle goes before it stops, and half as much
  again. Before, it looked down one line in the middle for 0.9 seconds of
  its speed. When there is something on a line (scenery, a vehicle, a wall
  or a steep bank), the driver steers to the nearest side that is clear,
  and uses the brake when the thing is nearer than the vehicle stops in.
- A driver goes around a player of its own team who is on foot in its
  way, or stops.
- A driver did not go back when there was a wall behind it, because it
  found no ground there and thought that it was an edge. With a wall
  before it as well, it stood there for the rest of the game. On Ice
  Fields, the drivers stood like this for 40% of their time. A wall is
  now not an edge.
- A vehicle with an edge before it and an edge behind it is now stuck,
  and its driver gets out.

These are eight games of capture the flag with 12 bots (Blood Gulch,
Sidewinder, Timberland, Danger Canyon, Ice Fields, Death Island, Hugeass,
Coldsnap), with `HALO_BOT_REFINE=31` (before) and without it. A "knock" is
a change of more than 2 world units a second in the speed of a vehicle in
one tick:

| | Before | After |
| --- | --- | --- |
| Knocks with a tree, a rock or a barricade in front | 20 | 9 |
| Knocks with a wall or a bank in front | 85 | 56 |
| All knocks in a minute of driving | 0.97 | 0.72 |
| Time on foot | 77% | 72% |
| Time at a wheel | 16% | 19% |

**The nearest vehicle.** On a large map, a bot with more than 40 world
units to walk takes a vehicle when one is nearer than half of that
distance. It takes the nearest one that it can get into: one with nobody
at the wheel, or one that a player of its team drives and that stands
still within 25 world units (the gun first, then a passenger seat). A bot
on its way to a vehicle changes to a different empty vehicle when that
one is a quarter nearer. The stock capture the flag game of Blood Gulch
has two Warthogs for 12 players, so most bots walk there.

**Grenades.** These are the changes:

- A bot keeps away from a live grenade. It sees one that is in its view
  within 14 world units, and hears one within 6. When the grenade is
  within reach of its blast, the bot moves away from it, and jumps when
  it is very near. When the grenade is ahead on the bot's way, the bot
  does not go nearer until it has gone off. A bot of skill 10 starts 2
  ticks after it sees the grenade, and a bot of skill 1 after 12 ticks. A
  bot of skill 1 notices one grenade in three.
- A bot that sees its target hold a grenade and look at it moves back or
  to the side until the grenade has landed.
- The look of a bot stays on the throw until the grenade leaves the
  hand. Before, the look went back to the target one tick after the
  button, and the grenade went where the look was.
- The pitch of a throw is from the speed of the throw and the fall of
  the grenade. The bot uses the flat way when it is clear, and the high
  way when it is not (through a door to a floor above, over a wall).
- A bot throws into a fight from as far as the hand throws (28 world
  units on level ground). The limit was 17.
- A bot that has an enemy on the motion tracker, 5 to 17 world units
  away, out of sight and coming nearer, can throw a grenade to the place
  where that enemy comes into view. The chance is much higher where that
  place has walls close on the two sides.

In two team games with 8 bots of skill 10, a grenade went off a median of
1.6 world units from the place that it was thrown at (4.6 before), and
57% of them within 2 world units (18% before).

In six team games with 8 bots of skill 10, the bots of one team did not
keep away from grenades (`HALO_BOT_OFF_T0=wary`). Bots of that team were
in a blast 165 times, 120 of them within 2 world units. Bots of the
other team were in a blast 87 times, 57 of them within 2 world units.

**Jumps at a run.** These are the changes:

- A bot of skill 6 or more that runs at a ledge jumps from the distance at
  which its feet are above the top when it gets there. Before, each bot
  went to the foot of the ledge, turned to it and jumped. The bot must
  come within 45 degrees of square to the ledge, and the ledge must be
  0.65 world units high or less. A bot of skill 10 does this most.
- A bot of skill 6 or more also jumps a gap from its run when it is 0.45
  world units or less from the line of the jump (0.25 before).
- A bot folds its legs in a jump only for a ledge higher than 0.55 world
  units. Before, it folded them in each jump up, and in some jumps in a
  fight.
- A bot jumps onto a ledge 0.76 world units high (0.72 before). In
  tests with a higher limit, bots made 72 of 81 jumps onto ledges 0.72
  to 0.76 high, and 19 of 26 onto ledges 0.76 to 0.80 high. Bake the maps
  again after this change.

These are six games with 8 bots of skill 10 (the six maps of the table
above), with `HALO_BOT_REFINE=31` (before) and without it:

| | Before | After |
| --- | --- | --- |
| Jumps up that left the ground at more than 1.9 world units a second | 27% | 38% |
| Time that a jump up took more than its walk (median) | 0.53 s | 0.35 s |
| Jumped gaps that were jumped from the run | 27% | 37% |
| Jumps up that failed | 5.1% | 3.8% |

**Skill 10 sees sooner.** The time before a bot notices an enemy in view
is half as long at skill 10 as it was (0.3 seconds for an enemy at the
edge of the view 40 world units away). The change is from skill 8 to
skill 10.

**How a good player moves in a fight.** These are the changes for bots of
skill 6 or more (all of the change at skill 10, about half of it at
skill 8, a tenth of it at skill 6):

- The bot always moves to a side in a fight. Before, a bot of skill 10
  with little agility did not move to a side for a third of the time.
- Each time the bot changes its side, it most often goes the other way
  (85% at skill 10). It does not choose the side that has a wall or a drop
  one step away when the other side is free.
- The bot holds a side for about 0.4 to 1.4 seconds. A body needs a third of a
  second to stop and a third more to be at speed the other way, so a
  shorter hold moves the bot nowhere.
- When the bot is hit after 0.45 seconds or more on one side, it can turn
  to the other side at once.
- The bot jumps a fifth as often in a fight. A bot in the air cannot turn.
- When the bot goes nearer to the enemy or back from it, it goes more
  across than along.
- The bot goes back from an enemy with a shotgun, a flamethrower or no
  weapon who is less than 8.5 world units away, when its own weapon is
  not one of these.
- The bot goes into cover sooner when its shields are gone and it is
  losing.
- On a walkway or a beam with no room for a full step, the bot takes
  short steps to the sides. Before, it stood still.
- The bot moves out of the way of a rocket or a fuel rod bolt that it
  sees coming, across its path, and jumps when it is close.

One fault is fixed for all bots: a bot that stepped quickly from side to
side was taken for a bot that was stuck, and was turned away and made to
jump.

In eight games without teams on eight maps (8 bots of skill 10, each
second bot without these changes, `HALO_BOT_OFF_EVEN=agile`), a bot with
the changes moved across its enemy for 46% of the time that it fought on
its own feet (29% without), was in the air for 19% of that time (32%
without), and lost 0.116 of its shields and health a second (0.131
without). The kills were the same (1501 and 1469).

**All the enemies in a room.** A bot started to notice an enemy only when
that enemy was the best one to shoot at. The second enemy in a room was
not noticed while the bot fought the first. A bot of skill 6 or more now
notices each enemy in its view within 40 world units. It also notices an
enemy outside its view who is on its motion tracker, 1.5 times slower at
skill 10 and up to 2.4 times slower at skill 6. An enemy who aims at the bot counts for more when the bot chooses
its target. In eight games without teams (each second bot without the
change, `HALO_BOT_OFF_EVEN=aware`), a bot with the change died 65 times of
1318 to an enemy that it had not noticed (5%), and a bot without it 390
times of 1377 (28%).

**Lying in wait.** A bot can wait out of sight for an enemy who comes its
way and has not seen it:

- The enemy is on the motion tracker, 5 to 22 world units away and not
  in sight, or the enemy is in sight 8 to 30 world units away, does not
  look at the bot, and the bot has a shotgun or a flamethrower (in its
  hands or on its back).
- A bot with a shotgun or a flamethrower does this 85% of the times at
  skill 10. Other bots do it by their character (10% to 55%, more for a
  bot that holds a place, less for a bot that rushes), and less at a
  lower skill.
- The bot goes to a place near it that the enemy cannot see, or stays
  where it is when the enemy cannot see it there. It crouches (a player
  who crouches is not on a motion tracker), takes the shotgun from its
  back, looks to where the enemy comes from, and does not shoot or throw
  a grenade.
- The bot comes out when the enemy is in sight and in reach (4.5 world
  units for a shotgun or a flamethrower, in sight at all for another
  weapon), when the enemy is 3 world units away round the corner, or when
  the enemy has seen it. It sees that enemy at once, and shoots at once.
  With a shotgun, it goes in to 1.2 world units.
- The bot gives up when the enemy goes away, when another enemy comes in
  sight, or after 5 to 13 seconds (by its patience).

In 16 games without teams (each second bot without this,
`HALO_BOT_OFF_EVEN=camp`), bots lay in wait 877 times and came out on the
enemy 291 times. Of the fights between the two that ended in the next 8
seconds, the bot that had waited won 99 and lost 56. When the enemy came
into reach before it saw the bot, the bot won 55 and lost 15. With a
shotgun or a flamethrower the bot won 17 and lost 16: it is seen too often
before the enemy is in reach. The bots that lay in wait had as many kills
(2814 and 2841) and died a little less (2792 and 2863 times).

**When things come back.** The game puts a new item at each item place of
the map every so many seconds of the game clock. A bot of skill 6 or more
knows these times for the power weapons and the powerups (a bot of skill 6,
7 or 8 only for the rocket launcher, the sniper rifle, the fuel rod gun,
the overshield and the camouflage). When nothing lies at such a place and the
next item is due in the time of the walk plus 3 seconds (plus 5 at skill
8 and plus 6.5 at skill 6), the bot goes there and waits, up to a walk of 22 seconds. It does not
go for a weapon that it has, for a weapon that is not better than its
worse one, or for a powerup when it has one. In a team game it leaves the
place to a bot of its team that is nearer. On the way it shoots but stops
only for an enemy who is near.

In 16 team games (8 bots of skill 10, one team without the four changes of
this section), the team with them took 26% of its power items in the 3
seconds after they came (15% for the other team), and an item lay 8.9
seconds before that team took it (12.9 seconds, the median).

The four changes together did not change who wins between bots: in 32
team games the teams with them had 5298 kills and the teams without them
5211.

**Jumps across gaps, and the way up to high places.** These rules are for
a bot on foot near a jump:

- A bot does not come to the start of a gap faster than it can stop. It
  slows down when there is no floor behind the start, unless it runs on
  the line of the jump already.
- Where the floor goes down or is steep before the edge (the top of a
  rock, a rounded edge), the bot jumps before it comes to the edge. A body
  that runs across such an edge is in the air, and the game does not let
  it jump from there.
- A gap of 1.9 world units or less, to a floor that is not higher, needs
  0.35 world units of floor for the run (0.7 before). Longest has such
  gaps from each powerup to the shotgun on the other side.
- A bot that falls off the start of the same jump two times in 20 seconds
  uses another way for some time. Before, it fell there again and again.
- A bot that comes down past the place where a jump ends is at the end of
  the jump. Before, it went back to do the jump again.
- Near a jump (3 world units), a bot does not sidestep and does not hop,
  also in a fight.
- A bot that is held up two times in 3 seconds at one place makes a larger
  step to the side with more room.
- A bot does not give up a place because it did not come nearer during a
  fight. A bot waits 7 seconds longer before it gives up a rocket
  launcher, a sniper rifle or a powerup.
- In a race, a bot fights only an enemy that is 2.5 world units away or
  less (4.5 when that enemy shoots at it), does not lie in wait, and does
  not stop at a checkpoint to fight there.

The tests are with bots of skill 10:

| Test | Before | After |
|---|---|---|
| Battle Creek, one bot to the roof of the west base (16 games) | 12 of 16 got there | 16 of 16, 26 seconds |
| Battle Creek, one bot to two places on the roof of the east base | 16 of 16 | 16 of 16, 14 and 16 seconds |
| Battle Creek, one bot to the rocket launcher | | 16 of 16, 18 seconds |
| Battle Creek, 16 games of 4 bots: the rocket launcher was taken | 55 of 68 times, 50 seconds after it came (the median) | 83 of 90 times, 41 seconds |
| The same games: bots gave the rocket launcher up | 121 times | 17 times |
| Longest, two bots go round the overshield, a shotgun, the camouflage and the other shotgun | 167 places in 4 games, 11.6 seconds for each, 12 jumps failed | 556 places in 12 games, 10.5 seconds for each, 17 jumps or ladders failed |
| Longest, from a powerup to the shotgun | a drop and a ladder, 7.2 to 7.6 seconds | a jump, 5.3 to 5.6 seconds |
| Longest, from a shotgun up the ramp and across to the powerup | 14.2 and 18.0 seconds | 15.3 seconds |
| Race on Wizard, score for each bot in a minute | 2.0 (5.0 without the changes of the section before this) | 11.6 |
| Race on Chiron TL-34 | 12 to 13 | 12 to 13 |
| 11 maps, four bots go round the items of each | 831 places, 11.8 seconds for each, 10 given up | 862 places, 11.7 seconds, 10 given up |

In the Battle Creek games with 8 bots, the rocket launcher was taken 42 of
50 times and 46 seconds after it came (27 of 34 times and 41 seconds
before). A bot of skill 10 lives about 20 seconds in such a game, and most bots
that go to the rocket launcher die on the way. In ordinary games the bots
are almost never on the roofs of the bases: nothing lies there. On
Longest, most bots stop for a run before the jump to a powerup, and do not
jump from the run up the ramp.

The navigation data has a new version for the shorter run. Bake the maps
again (`bake-navigation.cmd`), or the game makes the data in the first
game on each map.

**What a reading of the code found.** These faults were in the code before,
and are corrected:

- A bot did not hear shots. It took the time that a player got a weapon in
  the hands for the time of a shot. Thus it "heard" a player who changed
  weapons or got a new body, saw a player with active camouflage who
  changed weapons, and saw such a player sooner. Now it uses the time of
  the last shot of the weapon (or of the weapon of the vehicle, for its
  gunner). In 32 team games, the team with the correction had 3855 kills
  and the team without it 4143: the fault let a bot shoot sooner at an
  enemy with a new body.
- More than one bot went to the same empty vehicle with one seat.
- A bot at the wheel with a person of its team next to the vehicle, who
  did not get in, waited as long as the person stood there. Now it
  drives away after some seconds.
- A bot that drove by the navigation areas took the end of each straight
  part for a corner, and slowed down there unless it drove to the east.
- A bot that could not get out of a vehicle on the one tick that it tried
  stayed in the vehicle.
- A flag carrier or a ball carrier in a vehicle seat was not known as an
  enemy or as a teammate.
- A flag carrier on a very large map went again and again to a vehicle
  that it could not get to.
- A squad on a small map could have the centre of the world as its place
  to go.
- A bot that stood off the navigation areas, and went in circles or was
  held up there, marked a way as bad that it was not on. All bots then
  did not use that way.
- A sidestep that a bot started before a fight or before a jump continued
  after it, in the old direction.
- The time that the bots add to a jump that was slow one time did not go
  back to zero.
- A vehicle that the bots gave up stayed given up in the next game on the
  same map.
- A weapon that a player took and put on the back counted as a weapon
  that lies at its place, thus the bots did not go there for the next
  one.
- A bot that goes makes its place free for a person: the person then
  counted as a bot for the jobs of the team (driver, sniper).
- A pilot that found no route looked for one again on each tick for 4
  seconds, and the other pilots got no route in that time.
- The order of ways with the same cost was the order of the C library,
  which is not the same on Windows and on Linux. Now the order is the
  same on all systems. The data on Linux did not change (all 97 files of
  22 maps are the same, byte for byte).
- A damaged file of navigation data could make the game write outside its
  memory. The game now examines the numbers in the file first.

The code of the bots was also compiled with the undefined behaviour
sanitizer of the compiler. In 22 games on 21 maps (all game types, with
vehicles and aircraft) it found nothing. The address sanitizer cannot run
with the 64-bit build, which needs a fixed address that the sanitizer also
uses.

These faults are known and not corrected: the test for a bot that goes in
circles uses a route without the ways that the bot avoids; a bot that
stands still against an edge for half a second gives the jump up before
its other rules for that jump can act; on a gap with no room for a run,
the bot goes to a point behind the edge that can be in a wall; the search
of a driver across open country stops after a time on the clock, thus two
games with the same seed are not always the same; a floor square next to
the end of a wall counts as next to a drop.

**How quickly a good bot kills.** Measured at skill 10, the time from an
enemy in view to the bot seeing it, the time to the first shot and the kill
rate between bots had not changed since the bots of the section before
this one. Two things made the bots slow with a gun that fires a shot a
click:

- A bot clicked its pistol, sniper rifle or shotgun on a clock of its own,
  and a click while the gun was still working the last shot does nothing.
  One click in three fired: the pistol fired at half its rate and the
  sniper rifle at half its rate. Now a bot of a level over 5 clicks when
  the gun is ready (the top level at once, with the pistol a little later
  at a distance, for its spread).
- A bot aimed for the head only when the shield was gone. A head hit kills
  when the shot also takes the rest of the shield: with the sniper rifle
  always, and with the pistol on the third shot. Now a bot aims for the
  head for that shot as well.

At skill 10, in 8 games without teams: a bot with the sniper rifle killed
someone with full shields in 1.0 seconds (1.5 before, the median), and a
bot with the pistol took 4% more off whoever it shot at in a second. With
half the bots without the change, the bots with it made 53% of the kills
in 8 games without teams, and 57% in 32 team games. A bot of skill 5 or
lower clicks as before. `HALO_BOT_OFF=head` turns both off, and so does
`HALO_BOT_REFINE=65535`. `HALO_BOT_ENGAGELOG=1` writes to the log each
time that a bot went after someone, and how that ended.

A player who stands still and does not shoot is killed less often than
with the bots of two sections before this one (1.3 times a minute in place
of 1.9, with 8 bots of skill 10). The bots kill each other as often as
before. The cause was not found.

**Pressure.** A bot of a level over 5 (all of it at 10) also does this:

- It finishes whoever it has hurt. When the target that it shot in the
  last 1.5 seconds has less than three quarters of its shield and health,
  the bot changes to another enemy only when that enemy is a good deal
  better, or is less than 12 world units away and aims at it. In a game
  without teams, when the hurt target goes behind something, the bot goes
  after it.
- It knows of an enemy who came back to life less than 2 seconds ago and
  less than 30 to 45 world units away (a sporting bot does not), and goes
  after an enemy that it knows of also when it is on its way to an
  ordinary weapon.
- It lies in wait less with a gun that is not for close in.

Not in a race. In 8 games without teams, the bots with this saw the
target dead at the end of 44% of their fights (35% without), and changed
to someone else in 46% (56%). The kills between bots with and without it
were even: 50% without teams, 49% in 32 team games, and 49% in duels of
two bots (47% for two bots that are the same). `HALO_BOT_OFF=press` turns
it off, and so does `HALO_BOT_REFINE=131071`.

**A second reading of the code.** These faults were corrected:

- A bot that lay in wait was taken for a bot that was stuck, and it
  jumped about once a second where it lay (139 jumps in 118 waits in two
  test games, 0 now).
- A bot that came back to life a moment ago was taken for a person in
  the choice of a squad's leader, and so it was made the leader.
- On a new life, some values that mean "nothing" were 0 in place of
  NONE. With some test switches, a good player's dodge of a rocket was off
  until the first grenade.
- A bot's grenade hit rate and its last bad route went on into the next
  game. A bot that had changed its seat in a vehicle twice did not try
  again in the next vehicle.
- A gunner's shots were heard as far as the weapon in the gunner's hands
  carries, not as far as the vehicle's gun carries.
- The places that bots run to at random could include places that were
  removed from the map's list.
- On the tick that a jump or a ladder ended, the fight took the bot to be
  still on it, and the bot did not strafe.
- A Longsword whose bomb went while its gun was not ready again flew on
  over the place that it had bombed.
- A route search that ran out of memory could be taken for a finished
  search. A damaged navigation file could make the game read past the
  name of the map.

Some code that nothing used was taken out, and the test switches of
finished work were set to their values. The kills between bots, the time
to each item on 11 maps, the race on Wizard and the vehicle maps are as
before. The undefined behaviour sanitizer found nothing in 22 games.

`HALO_BOT_REFINE` has more parts: 32 (the height and the size of craft),
64 (the drivers), 128 (the nearest vehicle), 256 (the grenades), 512
(the jumps at a run and the legs), 1024 (how a good player moves in a
fight), 2048 (lying in wait), 4096 (all the enemies in a room), 8192
(when things come back), 16384 (the rules near a jump, above) and 32768
(the corrections from the reading of the code that change what a bot
does), 65536 (the click and the head, above) and 131072 (the pressure,
above). `HALO_BOT_REFINE=131071` turns the last one off,
`HALO_BOT_REFINE=65535` the last two, `HALO_BOT_REFINE=32767` the last
three, `HALO_BOT_REFINE=16383` the last four, `HALO_BOT_REFINE=1023` the
last eight, and `HALO_BOT_REFINE=31` turns these thirteen off. The names
`agile`, `camp`, `aware`, `timing`, `hear` (shots heard by the shot),
`head`, `press`, `drive` and `pickup` (below, "The bots in a Warthog") in
`HALO_BOT_OFF`,
`HALO_BOT_OFF_T0` and `HALO_BOT_OFF_T1` turn these off for all bots or for
one team, and
`HALO_BOT_OFF_EVEN` does this for each second bot (for a game without
teams). `HALO_NETWORK_TEST_SCORE=<n>` sets the score that ends a test
game. `HALO_BOT_TUCKRISE=<world units>` sets the ledge above which a bot
folds its legs, and `HALO_BOT_FLUENT=<1, 2 or 3>` turns on the jumps from
the run for gaps (1) and for ledges (2).

These settings are for tests: `HALO_BOT_GAPCOST=0` (a gap costs its run
only), `HALO_BOT_STEEP=0` (jumps onto steep slopes stay ways),
`HALO_BOT_RUNUP=<world units>` (the run before a gap) and
`HALO_BOT_JUMPRISE=<world units>` (the highest ledge; the host makes the
navigation data again). `HALO_BOT_BOXTRACE=x0,y0,x1,y1` writes each second
tick of the bots in a box to the log. `HALO_BOT_JUMPTEST=x,y,z,yaw,pace,
ticks` puts the first bot at a place, lets it run in a direction and jump.

With a rocket launcher, a bot shoots at the floor at the feet of an enemy
who is on the ground and less than 22 world units away, and it shoots from
2.8 world units or more (the blast is 2 world units). In a game without
teams, a bot prefers an enemy who is busy with another player, goes to a
fight that it hears, and throws a grenade into a fight or into a group of
enemies.

The bots rate the weapons in this order: the power weapons, the pistol, the
assault rifle, the plasma rifle, the needler, the plasma pistol. The needler,
the plasma pistol and a weapon that is almost empty are weak weapons. A bot
with a weak weapon goes to a pistol, an assault rifle or a power weapon that
lies near and takes it in place of the weak weapon:

- When the bot has only weak weapons, it goes up to 12 world units for the
  better weapon, also during a fight, unless the enemy is less than 2.5
  world units away.
- When the bot also has a good weapon, it goes up to 7 world units, and
  only when it has no enemy in view.
- When the bot goes to the hill, a checkpoint or a flag carrier, it goes up
  to 7 world units. When it goes to a flag or a ball, or carries one, it
  does not go.

In a fight, a bot with a weak weapon in its hands and a better weapon on
its back changes to the better weapon. No bot has the needler as its
favourite weapon.

A bot does not walk past a rocket launcher, a sniper rifle or a fuel rod
gun. When one lies less than 8 world units away, the bot takes it if it is
better than the worse of its two weapons. It does not do this when an enemy
is less than 12 world units away, when it was hit a moment ago, or when the
swap takes its favourite weapon. On a far map (the next section), a bot on
its trip goes to a power weapon or a powerup when the way by the item is
not more than 9 to 27 world units longer than the way straight to the
enemies (4 to 12 seconds on foot, by the character of the bot). In four
test games of 5 minutes on Gephyrophobia, no bot had a sniper rifle in its
hands before this, and bots had one 3 to 11 times in each game after it.
`HALO_BOT_OLDITEMS=1` turns these two rules off, for a comparison.

**Weapons that a map adds.** The bots know the weapons, the powerups and
the vehicles of the game by the names of their tags. For a tag with a
different name, and for all the tags of a map whose names are removed (a
protected map: each name is `<protected>`), the bots use what the tag
says, and treat the thing as the thing of the game that it is most like:

| The tag says | The bots use it as |
|---|---|
| a weapon with no trigger | the flag or the ball |
| more than 3 projectiles in each shot | a shotgun |
| a projectile that turns to its target and is slow | a needler |
| a range of 20 world units or less, slow, 6 shots or more a second | a flamethrower |
| a trigger that charges for 1.5 seconds or more, and a fast projectile | a sniper rifle. The bot holds the trigger until the weapon fires. |
| a trigger that charges for 0.25 seconds or more | a plasma pistol |
| 2 zoom levels, or 1 zoom level with 2.5 shots a second or fewer and a fast projectile | a sniper rifle |
| a slow projectile that explodes, 4 shots a second or fewer | a rocket launcher. If the projectile falls: a fuel rod gun. |
| 6 shots or more a second | an assault rifle if it has rounds, a plasma rifle if not |
| all others | a pistol |
| 30 shots or more a second with no damage (a charge that a player puts down) | nothing: the bots do not go to it |

A powerup gets its kind from the type in its tag (overshield, active
camouflage, health, grenades), and a vehicle with no name gets its kind
from its physics and its seats. A tag with a name that the bots know stays
what its name says, thus the maps of the game are not changed. In a test
with the names of Yoyorast Island hidden (`HALO_BOT_NONAMES=1`), each
weapon of the game got its own kind. On Snow Grove, a protected map, all
weapons were unknown before this. In two test games there, the bots had a
sniper rifle or the laser in their hands 69 and 77 times and fired the
laser 53 and 64 times. `HALO_BOT_WEAPONS=1` writes each weapon of a map to
the log, with its kind.

**How quickly a bot sees an enemy.** A bot sees only an enemy in its view,
and not at once: the time is longer for an enemy that is far, at the side
of the view, or not moving, and shorter for one that shoots. An enemy that
got a new body less than 3 seconds ago takes 0.01 to 0.2 seconds more, and
so does each enemy for a bot that got a new body less than 3 seconds ago.
This time is half as long when the enemy moves or shoots. In a test game
on Chill Out (8 bots of the old skill 3, between 5 and 6 now), the middle
time from in view to seen, for
these enemies, went from 0.23 to 0.33 seconds; the shortest stayed at 0.1
seconds. The number of kills did not change (91 and 91).
`HALO_BOT_OFF=fresh` turns the added time off, and `HALO_BOT_NOTICELOG=1`
writes each time to the log.

**How far a bot sees.** A bot on foot sees an enemy that is 90 world units
away or less, and 150 with a weapon that has a scope. On the large maps,
vehicles fight from farther than this:

- A bot in a vehicle (the driver, the gunner, a passenger, a pilot) sees
  1.5 times as far: 135 world units, and 225 with a scoped weapon.
- An enemy in a vehicle is seen from two times as far, up to 300 world
  units. A Warthog on a hill is easier to see than a person.

The view must be clear in all cases: a bot does not see through the
ground or a wall. A bot keeps a target to the same distances.

`HALO_BOT_OFF=human` gives the bots their movement without the first list
above, for a comparison.

### Far maps

The bots measure each map when its navigation is ready. The measure is the
time on foot between the places where players start: 85 of each 100 pairs of
these places are not farther apart than this time. A map with a time of 20
seconds or less is a near map. A map with a time of 40 seconds or more is a
far map, and a map between the two is partly a far map. Blood Gulch has 39
seconds, Sidewinder has 52, Boarding Action has 31, and Hang 'Em High has
18. With `HALO_BOT_LOG` set, the game writes the measure in a line that
starts with `bots: this map's reach`.

A map that has no navigation (not sufficient memory, or a map too large for
it) has a measure also. The game then uses the player starting points of the
map and straight lines, each multiplied by 1.5. This measure is less
accurate (the line that follows `this map's reach` in the log gives it for
each map: 67 seconds for Blood Gulch, 47 for Sidewinder, 20 for Hang 'Em
High), but a far map is not played as a near one.

On a far map, a bot that has no enemy within 30 world units goes to the
place where the enemies are. This is its trip:

- The bot starts the trip with the weapon that it has. It takes a better
  weapon that lies near its way, as the list of weapons above tells.
- The bot does not stop the trip for an ordinary item, for one of its own
  places, or to hold a place. It stops for a power weapon or a powerup that
  is near, for a vehicle, for the objective of the game, and for its squad.
- A vehicle that is less than 30 world units away is worth more to the bot
  when the enemies are more than 60 world units away.
- The trip goes to the place where the enemies are now. It ends when an
  enemy is less than 30 world units away.

On all maps:

- A driver who has no weapon on the vehicle and no gunner gets out and
  fights on foot when the vehicle has been near an enemy for 4 seconds
  without a view of the enemy, or for 8 seconds with one. Only the time in
  which the vehicle does not get nearer to the enemy than it was is in this
  count. The time in which the driver waits for bots of its team to get in
  is not. The count starts again for a different vehicle or a different
  enemy.
- A person who has not gone 6 world units from one place in 6 seconds does
  not lead the squad of the team. The bots of the team then follow one of
  the bots. This is a measure of travel: a person who fights in one place
  does not lead the squad to a different place. The count starts when the
  person gets a new body.

### Very large maps

Some Halo PC maps are about 2000 world units across (Coldsnap, Hugeass).
The navigation of such a map is made only for the parts where the game is
played: the bases, the weapons, the vehicles. It has no way between the
parts. `port/linux/game/bot_country.c` takes the bots between them, in
ground vehicles and on foot. The game makes this map of the ground on each
map, and the drivers of ground vehicles use it on each map (see [Ground
vehicles on all maps](#ground-vehicles-on-all-maps)). On foot, and to go
between the parts, the bots use it only on a map whose navigation is made
in parts.

- **The ground.** The game makes a map of squares of 2 world units. A
  square has the floors that are below its middle: a floor is a surface of
  45 degrees or less that has 1.6 world units of room above it.
- **The steps.** A vehicle can go from a floor to a floor of the next
  square when the step is not steeper than 40 degrees. If the height of
  the step agrees with the slopes of the two floors, the game accepts it.
  If not, or if the floor has a side (a ramp, a bridge, a road along a
  cliff), the game looks at the ground with rays: the ground must not jump
  more than 0.6 world units, and it must be 2 world units wide. Thus a
  Warthog does not go onto the top of a low wall, and not into a rut. (The
  limit was 1 world unit. In 16 tests, a Warthog that went straight at two
  steps of 0.84 and 0.96 world units at 5 to 6 world units a second did
  not get up one time.)
- **Lifts.** A ramp that is 0.5 to 2 world units wide, that goes up 0.9
  world units or more, and whose top the ground at its foot does not join
  in a different way, is a lift. A Warthog is 1.7 world units wide. A bot
  drives to the foot of a lift. When the vehicle is less than 3.5 world
  units from the foot, slower than 5 world units a second, and nothing of
  the map is between, the game moves the vehicle up the ramp at 4.5 world
  units a second and puts it down at the top. The ten Warthogs of the
  south-east base of Coldsnap stand 1.8 world units below the platform
  that their gate is behind, and the two ramps up are 0.8 world units
  wide. In test drives, a Warthog got up such a ramp one time in three
  (the wheels of one side on the ramp). Before, no route went up: the
  Warthogs went off the edge of the base, and the script of the map put
  them back on the platform. The game makes lifts only on a map whose
  navigation is made in parts.
- **Walls.** A ray at 0.75 world units above the ground goes from the
  middle of each square to the middle of the next. If it hits the map, a
  machine or a piece of scenery, there is no step. This finds a wall, a
  railing or a rock that is thinner than a square.
- **Bridges, scenery and gates.** One second into the game, the game adds
  the tops of the machines and of the scenery that join the ground (the
  two bridges that go up from each hangar of Hugeass), and looks again at
  the steps near them. A script of a map that moves a vehicle from a
  trigger volume to a flag (the gates of Coldsnap) is a way for the
  vehicles that the script names. A bot drives to the volume, and the game
  puts its vehicle into the volume when the vehicle is less than 3.5 world
  units from it and nothing of the map is between.
- **Down a cliff.** A vehicle can go off an edge to a lower floor. This is
  a way in one direction. The bases of Hugeass have no other way down.
- **The file.** The first time that a map is played with bots, the game
  looks at all the steps of the map: 3.5 seconds on Coldsnap and 5.9
  seconds on Hugeass, after 2.6 and 2.2 seconds for the squares (measured
  with two games on a computer of 2 processor cores). It writes the result
  to `navcache/<map>_0.country` (36 MB for each of these maps). After
  that, the game reads the file in 0.03 seconds. `HALO_BAKE_NAVIGATION`
  writes this file with the other files of the map.
- **The route.** For each destination, the game searches from the
  destination across all the map and keeps, in each floor, the next step.
  All the bots that go to the same end of the map use the same search. The
  search gets 6 milliseconds of each tick and is complete in 2 to 4
  seconds. A driver stops and waits for it.
- **Room.** A square at the edge of the ground (at a wall, at a cliff, at
  the side of a bridge) costs 4 times a square in the open. A square next
  to such a square costs 1.5 times, and 4 times if one of 8 rays of 1.8
  world units from its middle hits something. Thus the route goes along
  the middle of a bridge and round the end of a wall.
- **The driving.** The driver steers to the farthest point of the route
  that it can go to in a straight line that does not touch the edge of the
  ground. Before a bend of the route it slows down to 7.5, 5.5 or 4 world
  units each second, by the angle of the bend. Near an edge or a wall it
  does not go faster than 9.5 or 6.5. It goes onto a narrow ramp, and up a
  step, along the line of the ramp or the step and at full speed. If it
  stops at a step, it reverses 10 world units along that line and comes
  again. It brakes when there is no ground at the place where the vehicle
  will be in the next second, and it reverses from an edge at which it has
  stopped. A ray finds a tree, a rock or a different vehicle in front, and
  the driver goes round it. The driver of a Ghost points the nose against
  the direction in which the Ghost slides.
- **Edges.** A driver that reverses stops when there is no ground 2.5 or
  3.5 world units behind the vehicle, and reverses slowly when there is
  none 5 or 7.5 world units behind it. A driver that goes forward to get
  clear of something does not go faster than 5 world units a second,
  prefers a direction that has ground for 12.5 world units, and stops for
  an edge. A vehicle that starts to move after it stood (it waited for its
  route or for its team) is not stuck while it moves faster than 0.5 world
  units a second in the first 4 seconds. Before these changes, the
  Warthogs of the south-east base of Coldsnap went off the edge of the
  base in two ways: forward when the game took a slow start for a stuck
  vehicle, and backward when they reversed for a run at a step. In a test
  game of 7 minutes (11 bots that prefer Warthogs), drivers were below the
  base for 37 seconds before the changes and for 0 seconds after them, and
  9 vehicles went up the lifts and 8 went into the gate of that base.
- **The fight.** When an enemy is less than 28 world units away and the
  vehicle is not on the navigation, the driver goes round the enemy across
  the ground in the same way.
- **On foot.** A bot on foot that is not on the navigation, or whose
  destination the navigation does not join, walks across the same squares
  to a destination that is less than 220 world units away. It does not go
  off edges and does not use the gates. The search for it stops at the
  place where the bot is. Where it must walk in a straight line, it does
  not take a step if there is no ground in the next 2.4 world units.
- **The flag.** A driver stops 16 world units from the flag of the enemy
  and walks to it. A bot that has the flag does not take the wheel or the
  gun of a vehicle, because the game then drops the flag. It gets into a
  passenger seat next to a driver of its team. A bot of its team that is
  less than 120 world units away takes the wheel of a vehicle that has a
  free passenger seat, drives to the bot that has the flag, waits, and
  drives to the base. The bot with the flag gets out 16 world units from
  the base.
- **When it stops.** A driver whose vehicle did not move 4 times, or that
  has no way from the place where it is, gets out. No bot gets into that
  vehicle while it is in that place: for 90 seconds if it did not move,
  for 5 minutes if it has no way.

These are games of 10 minutes of capture the flag with 11 bots, with
`HALO_RANDOM_SEED=5`. Before this, a driver had no route between the parts
of these maps.

| | Hugeass | Coldsnap |
| --- | --- | --- |
| Kills | 15 | 3 |
| Deaths with no killer (a fall, or under a vehicle) | 1 | 6 |
| Drivers that got to less than 100 world units from their destination | 5 | 7 |
| Longest time that one bot drove | 261 seconds | 267 seconds |
| Gates used | 1 | 23 |
| Times a driver got out because the vehicle did not move | 10 | 6 |
| Times a driver got out because there was no way | 1 | 5 |
| Flags captured | 0 | 0 |

On Hugeass, the Ghosts and the Warthogs leave the hangars, go down from the
bases, cross the basin and fight. On Coldsnap, the two teams leave their
bases through the gates, meet in the middle of the map and at the flags,
and fight there. These things do not operate well:

- **The flags of Coldsnap.** No flag was captured. In a test with two
  Warthogs put near the flag of the south-east base, a bot took the flag,
  got into a passenger seat, and a bot of its team drove it 2000 world
  units to 21 world units from its own flag. There the Warthog went into
  the canyon (the driver now stops at the flag and does not go round it;
  this was not tested again). The flag of the north-west base is on a
  platform above a canyon that the map of the ground does not have: 3 of
  the 6 deaths with no killer were bots on foot there.
- **The aircraft.** See [The bots in a Pelican, a Longsword and a
  jet](#the-bots-in-a-pelican-a-longsword-and-a-jet). The flag of the
  south-east base of Coldsnap is under a roof, and no aircraft goes to it.
- **On foot, far from all.** A bot on foot whose destination is more than
  220 world units away does not walk to it.
- **Hugeass.** Some drivers of Warthogs stop at the top of the hangar
  bridges and get out.

With `HALO_BOT_LOG` set, the lines that start with `bots: country:` give
the size of the map of the ground, each gate, each lift, each search, and
where each driver is every 3 seconds. A line that starts with `bots: drv`
gives each driver of a ground vehicle each second: its place, its speed,
its throttle and how it drives (`mode 1`: far across the map, `mode 2`: by
the map of the ground, `mode 3`: by the navigation). `HALO_BOT_COUNTRYTRACE=1`
adds a line for each driver every third of a second.

### Ground vehicles on all maps

On each map, the driver of a Warthog, a Ghost or a Scorpion drives by the
map of the ground of the section above: the same route, the same steering
and the same speeds as on the very large maps. Before, a driver on an
ordinary map followed the areas of the navigation. A driver uses the areas
where the map of the ground has no way or the vehicle is not on it, and for
12 seconds after the vehicle did not move 4 times. A turret is not driven.
On a map whose navigation is whole, the map of the ground is made only for
the part that the navigation has, and 30 world units round it (the shell of
Death Island goes out to the horizon). The game makes it in 0.1 seconds or
less on these maps and keeps it in `navcache/<map>_0.country`.

These are games of 7 minutes of capture the flag with 11 bots that prefer
vehicles (`HALO_BOT_CRAFT=vehicles`), with `HALO_RANDOM_SEED=5`, each one
time by the areas (`HALO_BOT_COUNTRY=0`) and one time by the map of the
ground. The speed is the mean speed of a driver that is on its way (not
when it waits for its team and not when it goes round an enemy), in world
units a second. Slow is the part of that time below 1.5 world units a
second.

| Map | Speed, areas | Speed, ground | Slow, areas | Slow, ground | Kills from vehicles, areas | Kills from vehicles, ground |
| --- | --- | --- | --- | --- | --- | --- |
| Blood Gulch | 3.3 | 3.6 | 24% | 25% | 13 | 13 |
| Sidewinder | 2.7 | 3.9 | 37% | 20% | 12 | 14 |
| Danger Canyon | 3.8 | 3.7 | 21% | 23% | 10 | 13 |
| Death Island | 3.5 | 4.3 | 24% | 12% | 4 | 11 |
| Ice Fields | 1.0 | 2.1 | 81% | 53% | 9 | 25 |
| Timberland | 2.7 | 3.2 | 29% | 21% | 1 | 8 |

A driver that has a gunner and goes round an enemy that nobody in the
vehicle sees can go round for a long time. This is not changed.

### The bots in a Warthog

In a team game, a bot that drives a vehicle with more than one seat does
not go immediately:

- It waits 1.5 seconds at the wheel when a seat is empty.
- It waits up to 3.5 seconds when nobody is on the gun and one of its team
  is on foot less than 14 world units away.
- It waits up to 7 seconds while a bot of its team comes to the vehicle
  from less than 12 world units away, or a person of its team stands less
  than 9 world units away.
- It goes immediately when somebody is on the gun and nobody of its team is
  near, and when all the seats are full.
- It does not wait when an enemy that it sees is less than 25 world units
  away, or when it was hit a moment ago.

A vehicle that stands with a driver of the team and nobody on the gun is
worth more to a bot on foot that is less than 16 world units away. The
nearest bot gets on the gun, whatever its job is.

In five test games of capture the flag with 11 bots (Blood Gulch twice,
Sidewinder, Death Island, Danger Canyon), 33 Warthogs went away: 11 with a
gunner, 22 with only the driver. For 18 of those 22, nobody of the team was
on foot less than 14 world units away.

A bot that drives alone (nobody on the gun, and a vehicle whose driver has
no gun of its own), with no enemy near, drives to the nearest person of
its team on foot less than 80 world units away and stops beside them for
5 seconds, for them to get on. When the person does not get on, the bot
asks nobody for 20 seconds. (It does not do this for bots: in test games
it did not change the score.)

A bot at the controls of a fixed gun (the Covenant turret of Gephyrophobia)
stays there and shoots. When it has no target for 25 seconds it gets off
and leaves that gun alone for 30 seconds. Before, it tried to drive the
gun, was stuck, and got off.

A bot at the wheel of a vehicle on the ground that stands for 10 seconds,
not waiting for anyone and with no enemy to shoot (the place that it goes
to is somewhere a vehicle cannot go, or it has no way there), gets out and
leaves that vehicle alone for 30 seconds. On Hugeass, bots stood in a
Tachikoma by the place where it comes for 8 minutes.

A bot that drives a vehicle on its side or on its roof gets out after 1.5
seconds. It does not leave the vehicle alone: the action button beside a
vehicle on its side puts the vehicle back on its wheels, and the bot then
gets in again.

`HALO_BOT_OFF=drive` turns off the fixed gun and the vehicle on its side,
and `HALO_BOT_OFF=pickup` the ride for a person. In 30 test games
(Blood Gulch, Sidewinder, Danger Canyon, Death Island, Gephyrophobia,
Icefields, Timberland, Hugeass; capture the flag and team slayer; the same
seeds as before the changes), the bots got out of a stuck vehicle 7.3 times
each 10 minutes instead of 10.7 (on Gephyrophobia 1.7 instead of 21), and
killed 14.3 players from a vehicle instead of 11.6. On Hugeass they drove
a vehicle on its side for 4.7% of the time instead of 7.5%. In 40 games of
team slayer with the fixed gun and the vehicle on its side on for one team,
the score of that team did not change by more than chance.

### The bots in a Banshee

`port/linux/game/bot_flight.c` flies each vehicle that has the physics of
the Banshee (the type `alien fighter`; the jets of some Halo PC maps have
this type also).

- **The map from above.** When the map loads, the game makes a map of
  squares of 2 world units: the top of the ground in each square, the
  lowest thing above it, and the top of each tree or large rock that stands
  there (scenery that has a collision model and a radius of 1.5 world units
  or more). The game finds these tops with rays. This took 0.03 seconds on
  Death Island (29 trees and rocks) and 0.06 seconds on Timberland (43).
- **The height.** A pilot flies 4.5 world units above the highest ground of
  the next 2 seconds of its way, and 1.5 world units above the trees there.
  It goes round a thing that is more than 30 world units higher than the
  two ends of its way (a tower). Three rays along its way, one from the
  middle and one from each wing, find the things that the map does not
  show. The pilot climbs over such a thing or turns away from it.
- **The look.** A Banshee does not go exactly where its pilot looks: its
  nose is 15 degrees above the look, and it sinks. A pilot that looks 25
  degrees down flies 6 degrees down. The pilot corrects its look each tick
  by the direction in which the Banshee moves.
- **Where it goes.** At full speed to the nearest enemy that is under the
  sky. If no enemy is under the sky: in capture the flag, to the base of
  the enemy; in the other games, to the nearest enemy. It flies circles
  there. A pilot that has guns does not land to take the flag. In capture
  the flag, the enemy that carries the flag of the team comes first.
- **The attack.** The pilot starts a run when it sees the enemy at less
  than 45 world units. It first comes down to a little above the enemy,
  then it looks at the place where the enemy will be and shoots. The shots
  go where the pilot looks only when the nose of the Banshee is less than
  50 degrees from the look (the game turns a shot of a player that far and
  no more). At less than 15 world units, the pilot stays in the air near
  the enemy for up to 5.5 seconds and continues to shoot. Then it flies
  away past the enemy and comes back.
- **The fuel rod.** The second trigger of the Banshee fires it. It is slow
  and it falls. The pilot calculates where to look from the speed of the
  Banshee, the speed and the fall of the fuel rod, and the movement of the
  enemy. It fires when the fuel rod will land less than 1 world unit from
  the enemy and the enemy is less than 17 world units away. The gun fires
  one fuel rod in about 4 seconds.
- **When it is hurt.** A pilot whose shields are below 30% flies away from
  the enemy, to a place that the enemy does not see if there is one in the
  next 30 world units. It comes back when the shields are above 30% again,
  or after 9 seconds.
- **To get in.** A bot presses the button when the game offers the seat
  that the bot wants. Before, a bot pressed it only when it stood less than
  0.55 world units from the door of the seat. The door of a Banshee is
  under its wing, and the bot stood 0.6 world units away and did not get
  in. A bot also does not stop its way to a vehicle because the straight
  line to it does not get shorter (a Banshee on a roof, or behind a
  teleporter), if the route gets shorter.

Four sets of six test games of 5 minutes with 11 bots (capture the flag
and slayer on Death Island and Gephyrophobia; the last two sets with the
code as it is now):

| | Each set |
|---|---|
| Bots that got into a Banshee | 21 to 25 (1 to 7 in a game) |
| Time in the air for each flight | 42 to 54 seconds |
| Hits against something | 0 or 1 in 16 minutes in the air |
| Kills from a Banshee, for each minute in the air | 0.6 to 0.7 |
| Pilots shot down, for each minute in the air | 0.8 to 1.3 |

With the code of the build before this one for getting in, 2, 3 and 2 bots
got into a Banshee in three games of 5 minutes on the same maps, and 27, 11
and 29 times a bot stopped at the door or on the way. The other numbers
were not measured for that build.

A bot on foot aims well at a Banshee, and most pilots are shot down when
they fly away from an enemy that is near. The hits that remain are against
the wall of a building next to which the Banshee starts, and against a
second Banshee.

### The bots in a Pelican, a Longsword and a jet

These are the aircraft of some Halo PC maps (Hugeass, Coldsnap). The numbers
in this section are from test games of capture the flag with 11 bots on
these two maps.

**The Pelican and the Longsword** have the physics of the Pelican (the type
`human plane`). Such an aircraft flies in two ways. With the throttle
closed, its jets hold it up: the jump button gives a little more than its
weight, the crouch button much less, and it moves the way it leans. With
the throttle open, it goes along its nose at its top speed and its wings
hold it up. The air stops it quickly: about 2 percent of its speed in each
tick.

- **Up.** A pilot goes up 3 world units on the jets, turns to its route,
  and opens the throttle. Under a roof, it stays on the jets, 1.6 world
  units above the floor below it and the floor 7 world units ahead, at 6
  to 9 world units a second, until it is out. Under a roof it does not go
  forward until its nose is less than 0.5 radians from the way out (the
  Longsword of Hugeass stands with its tail to the door of the hangar; it
  went forward during the turn, into the wall, and stayed at the ceiling:
  4 of 5 Longswords in one test game, none of 3 and none of 2 in two games
  after this). A pilot that has something above it (the bottom of a
  bridge) does not go up on the jets: it flies out on its wings from the
  height where it is.
- **Down.** A pilot closes the throttle at a distance of 2.5 seconds of
  flight plus 20 world units from the place, comes over it on the jets, and
  goes down at 4 world units a second or less. In one test game, 14
  landings out of 14 were 3.5 world units or less from the place.
- **Under a roof.** The game keeps the way that each aircraft takes out of
  a place under a roof (a point each 6 world units, for aircraft that bots
  fly and aircraft that people fly). An aircraft that must land under a
  roof goes to the open end of the nearest such way and follows it back.
  Thus a Pelican of Hugeass goes back into its hangar and lands on its
  pad. A place under a roof that no aircraft came out of has no way in.
- **To get in.** The doors of the seats of these aircraft are in the hold
  and at the cockpit, more than 1 world unit from where a body can stand,
  and the game lets a player in only from 1 world unit. A bot that stands
  less than 3.5 world units from the door of its seat, next to an aircraft
  that does not move, is put into the seat (with the function that the
  action button calls). A bot takes the seat whose door is nearest.

**To carry people.** In a team game, the pilot of an aircraft with more
seats than its own does these things:

- It stays on the ground where it took the aircraft: 4 seconds always, up
  to 30 seconds while a bot of its team comes to the aircraft from less
  than 80 world units, up to 20 seconds while a person of its team stands
  less than 14 world units away, and up to 14 seconds while fewer than 2
  are in it and one of its team is on foot less than 70 world units away.
  Bots on foot that are not defenders come to such an aircraft from up to
  80 world units.
- With people in it, it flies to the flag of the other team (in King of
  the Hill, to the hill; in other team games, to 45 world units from the
  nearest enemy). It lands short of the flag by its own size plus 5 world
  units, with its tail to the flag, and the bots in seats that are not
  guns get out.
- It stays there on the ground, up to 50 seconds, while one of its team is
  on foot less than 90 world units from the flag. It leaves when its pilot
  has no shields after 8 seconds.
- When one of its team takes the flag, that bot gets into a seat in which
  the map lets a player hold the flag, and the pilot flies to the flag of
  its own team, lands, and the bot gets out and walks to it. If the bot
  with the flag is on foot more than 150 world units from its base, a
  pilot with an empty seat flies to it and lands next to it.
- With nobody in it and 2 or more of its team on foot at the place where
  it took the aircraft, it flies back there for them. It starts back only
  when it is more than 200 world units from that place.

A bot that just got out of such an aircraft and is still next to it walks
first the way that is open the farthest (rays at the height of the chest),
because the hull is between it and its route.

**Which seats keep the flag** is different on each map. The game asks the
same question that it asks when a player sits down (the animations of the
player for that seat and the flag). On Hugeass, all the seats of the
Pelican and the seat of the jet keep the flag. On Coldsnap, no seat of the
Pelican keeps it, four seats of the Longsword do, and the seat of the jet
does. A bot with the flag takes only a seat that keeps it. When the seat of
a pilot keeps the flag, a pilot that is alone lands at a flag that is under
the open sky, gets out, takes it, gets in again, and flies it to its base.
The pilot nearest the flag does this, and only when nobody of its team is
on foot less than 40 world units from the flag.

**The jets** have the physics of the Banshee, and the code of the Banshee
flies them. A jet goes 30 world units a second (the game does not let a
vehicle go faster than 1 world unit a tick, whatever its tag says). A pilot
of a jet keeps the throttle open in a turn, and leaves a hangar at full
throttle when the way out is clear for more than 70 world units and the
nose is on it.

**The gates of Coldsnap.** A jet of Coldsnap stands in front of a tube in
the wall of its hangar, and the script of the map moves a jet that is in
the tube to the sky. The tube is 6 world units wide and 3.3 high at one
base, and 4.2 wide and 1.6 high at the other. A pilot goes to the tube
level along the floor, and the game puts the jet into the trigger volume
when the jet is less than 24 world units from the middle of the volume,
points at it, and nothing of the map is between. In one test game all 8
jets left their hangars this way in the first 35 seconds. Before this, no
jet left.

The Longswords of Coldsnap leave through two larger volumes (62 and 60
world units across) that put them on top of the mountain. A pilot of a
Longsword flies into its volume on its wings. Volumes that are larger than
90 world units are parts of a base, not gates, and no pilot flies to them.
In a test game of 5 minutes with bots that prefer the Longsword, two
Longswords left their hangars and killed 16. Before this, a Longsword
stayed in its hangar with its pilot and the bots in its seats.

**The guns of the jets.** A gun whose projectile goes 5 world units a tick
or faster has a range of 15 ticks of its projectile (150 world units at
most), whatever range its tag gives. A pilot shoots missiles that do not
fall at aircraft also. In a test game of 10 minutes on Hugeass, the jets
killed 8.

A pilot that leaves an aircraft because the aircraft is stuck does not
take it again, and no other bot takes it, for 180 seconds.

In a test game of 9.5 minutes on Hugeass, two Pelicans took 3 and 5 bots
to the flags, a bot of each team took the flag and got in again, and one
Pelican landed 12.5 world units from its own flag and the team scored.
These things are not complete:

- A pilot lets a bomb of the Longsword go when the game computes that it
  falls less than 5 world units from the enemy. The pilots of the jets of
  Coldsnap get few kills.
- A jet does not land at a flag that is under a roof.
- A pilot that carries people does not shoot.

With `HALO_BOT_LOG` set, a line with `the <vehicle>: <job>` tells each
change of what a pilot that carries people does (`load`, `drop`, `pick
up`, `deliver`, `home`). `HALO_BOT_AIRTRACE=1` adds two lines a second for
an aircraft on its jets or under a roof.

### Vehicles that come back

When the game type has a time for vehicles to come back, the game puts
back a vehicle that is destroyed, or that is empty and away from its
place, after that time. A vehicle that the map names comes back as the
vehicle of that name, made as the script of the map makes it. Before, it
came back as a copy with no name: the scripts of a map did not know the
copy (a gate of Coldsnap did not move it), and a script that makes a
vehicle again when it is gone made a second one. The game now does this
for 256 vehicles of a map, not 64 (Coldsnap has 65).

In a test, all the empty vehicles of Coldsnap and Hugeass were killed at
10 seconds. With a time of 30 seconds, all of them were back with their
names at the end of the test, 80 seconds later. With no time (the setting `never`), only the
vehicles that the scripts of the maps make again came back (the Longsword,
and two kinds of walker); the wrecks of the others stayed.

### More vehicles

With `game.vehicle_multiplier` set to 2, 3 or 4, the host makes that
number of each vehicle of the map. For Hugeass with three of each vehicle,
put these lines in the `[game]` section of `config.toml`:

```
vehicle_multiplier = 3
vehicle_multiplier_maps = "hugeass"
```

- Half a second into the game, the host makes the other vehicles. It puts
  each one next to a vehicle of the map: to its sides, behind it or in
  front of it, up to 3 widths or 3 lengths away. The place must have level
  ground at about the same height (1.2 world units or less higher or
  lower), nothing of the map between it and the vehicle of the map,
  nothing of the map around it or 1.2 world units above it, and no other
  vehicle. If no place is free, the host does not make that vehicle, and
  `debug.txt` has the number.
- The clients get these vehicles from the host, as they get each vehicle
  that the host makes.
- When the game type has a time for vehicles to come back, these vehicles
  come back to their places also.
- These vehicles have no names. A script of the map that moves a vehicle
  by its name (a gate of Coldsnap) does not move them.

On Hugeass the map has 36 vehicles, and with the setting 3 the host made
all 72 others. In a test game of 7 minutes of capture the flag with 11
bots, 12 jets, 6 Pelicans and 3 Longswords were flown.

### The characters of the bots

Each bot has a character for the full game. The bots do not keep it from
one game to the next.

- **A way to fight.** A duelist stays at the range of its weapon and
  strafes. A rusher goes close. A lurker stays a little farther away, holds
  a place, and does not follow an enemy that it lost. A hit-and-run bot
  goes to cover when its shields are low and comes back when they are full.
- **A habit.** One of: it crouches while it strafes, it jumps while it
  shoots, it goes in for a melee hit when the shields of the enemy are
  down, it throws a grenade first, or it moves back while it shoots when it
  is hit.
- **A grudge.** A bot remembers the players that killed it. It prefers
  such a player as a target and goes to find a player that killed it two
  times or more.
- **Its run.** After some kills without a death, a bot becomes more
  aggressive and taunts more. After some deaths without a kill, it becomes
  more reckless or more careful, as its temper is.
- **Its places.** A bot has 2 or 3 places on the map. It goes back to them
  and stays there for a time.
- **The trigger.** Some bots hold the trigger and shoot when the aim is
  near the target. Others wait for the aim and shoot in short bursts.
- **Melee.** Some bots go in for a melee hit when the back of an enemy is
  to them.
- **Vengeance** (team games). When an enemy kills a teammate near a bot,
  the bot can go after that enemy for 5 to 13 seconds.
- **Composure.** A hit moves the aim of some bots more than that of others.
- **Loyalty.** A bot keeps its favourite weapon longer, and goes farther to
  get it.
- **Sportsmanship.** A sporting bot does not prefer an enemy who is busy
  with another player, and it gives a player who came back to life 2
  seconds. Other bots take each kill that they can get.
- **Play.** Some bots turn around and jump after a kill, or crouch at a
  teammate when they have nothing to do.
- **Camping.** How long a bot stays in one place.
- **The herd** (team games). Some bots follow the nearest teammate. Others
  seldom do.

In a game with an objective (capture the flag, king of the hill, oddball,
race), the objective comes before the character. The flag, the hill, the
ball and the checkpoint are worth almost the same to each bot. The grudge,
the vengeance, the places and the herd count for about one third of their
value in a slayer game, and a bot stays at one of its places for a shorter
time. A bot that goes to the objective or carries it does not taunt, does
not turn around after a kill, and does not go to cover as a hit-and-run
bot does. The character still decides how the bot fights on the way.

With `HALO_BOT_LOG=1`, the log has one line for each bot with its
character.

### Bots that look for people, the motion tracker, the top level

- **Quiet time.** The longer a bot sees nobody (no enemy seen, heard, on
  its motion tracker, or shooting at it), the more it goes to find
  someone: from 8 seconds of quiet (4 in a game of 6 players or fewer) to
  30 seconds (15), when finding someone is its main goal. With more than
  half of that, it goes to the nearest dot on its motion tracker first,
  else the nearest enemy, and it looks again every 4 seconds; a bot that
  usually hangs back, or has a poor weapon, goes after a dot too.
  Not in capture the flag, King of the Hill, Oddball or Race, where the
  objective is what to find. `HALO_BOT_OFF=seek` turns this off. (In
  test games of bots only, the number of kills did not change: the bots
  already went to find each other.)
- **The motion tracker.** A bot keeps every dot on its motion tracker in
  mind for 4 seconds (8 at most), not only the nearest, and tells its team
  where they are. It does not turn to look at a dot that is more than 14
  world units away while it goes somewhere: it knows they are there.
  Hurt, with three or more dots near it, it breaks off the fight.
  `HALO_BOT_OFF=radar` turns this off.
- **The top level.** From skill 8 the traits that make mistakes are drawn
  toward a master's, and at 10 they are a master's: no detours on a whim,
  no jumps at nothing outside a fight, steady when hit, bursts where they
  are best, and the objective first. The traits that give a bot its
  character (how bold, how patient, its weapon, its love of vehicles)
  stay. Its movement changes direction at once instead of over a few
  ticks.
- **Grenades up and down.** To a place more than 1.5 world units above or
  below, a bot throws the high way first, which comes down on the place
  and stays there; the low way comes in flat and bounced and rolled on
  past the target, or over an edge. In test games on Chill Out, Hang 'Em
  High, Prisoner and Damnation, grenades thrown down landed within 3 world
  units of the place 80% of the time instead of 68%, and grenades thrown
  up 57% of the time instead of 27%.
- **Teleporters.** A teleporter (a one-way one too) is a way for the bots
  when its pad is near floor that they walk on, also when the pad stands
  on a housing (Death Island's, Icefields', Danger Canyon's were not used
  before). Bake the navigation again (`AAS_CACHE_VERSION` 33 does this by
  itself).
- **Out of reach in a vehicle.** A driver whose enemy is somewhere that no
  vehicle goes (up on rocks, over the water) drives as near them as it can
  and shoots from there if its vehicle has a gun; with no gun, or still
  nowhere after a few seconds, it gets out and goes after them on foot.
  It no longer stands still below them.
- **Team King of the Hill.** The hill comes before anything: a bot on the
  hill stays on it and fights from it, and a bot off the hill goes back.
  When the hill is the team's (players of the team on it, none of the
  other team) and others hold it, a bot can guard just outside it, on the
  side of the nearest enemy. A better weapon a few steps away (6 world
  units or less, near the hill) is taken first. `HALO_BOT_OFF=hill`
  turns this off.
  In 15 games of team King of the Hill (Blood Gulch, Hang 'Em High, Chill
  Out, Damnation, Battle Creek; 8 bots), a team held the hill alone (and
  scored) 67% of the time instead of 55%.

### Crouching, ramps, gaps, the way to the flag, the road ahead

- **Crouching.** A bot crouches in cover, in a jump (the legs tucked up to
  clear a ledge, and only while it is in the air), and as its character
  has it (a duck in a fight, now and then). It no longer crouches where it
  lies in wait, and the tuck of a jump no longer goes on after it lands:
  bots crouch-walked off from a short hop for half a second, and crouched
  where they waited for someone, which looked like a bot gone wrong.
  `HALO_BOT_OFF=standup` puts both back.
- **Up a ramp.** On its way somewhere, straight on up a slope (or at a run
  a step or two from the foot of one) with room over its head and the
  floor going on, and nobody about, a bot hops, as players do. `HALO_BOT_OFF=hop` turns this
  off. (In test games on Chill Out, Wizard, Hang 'Em High and Damnation
  the hops were no quicker: the bots already ran up the ramps at their
  full speed, about 2 world units a second along the slope, and a body in
  the air does not speed up.)
- **Gaps at a run.** A bot of skill 7 or more that comes to a gap already
  running at it, in line with the jump, jumps it from the run instead of
  going back for a run-up.
- **The way to the flag.** A bot on its way to the flag, the hill or the
  ball takes the quickest way most of the time, and a way round by one side
  now and then (the patient more often), instead of nearly always. On
  Wizard, the bots went round the sides to the flag; the quickest way from
  one flag platform to the other is across the middle, by the gaps onto the
  ring round the pillar and off it. In test games on Wizard the bots
  captured a flag 0.58 times a minute instead of 0.47, and in 40 games of
  capture the flag (Wizard, Battle Creek, Hang 'Em High, Blood Gulch, Chill
  Out; skill 10) with this on for one team, that team captured 0.4 more
  flags a game. `HALO_BOT_OFF=direct` puts the way it was back.
- **In for the blow.** A bot that has made up its mind to go in for a melee
  goes in; with a rocket launcher it used to go in and then back off from
  the same one, in turn, every few ticks. `HALO_BOT_OFF=steady`.
- **Water.** The vehicles' navigation (by the areas and across country)
  leaves out floor under water deeper than about 0.8 world units, where
  the game gives a vehicle no drive. (Bake the navigation again:
  `AAS_CACHE_VERSION` 34 and the country's cache version 9 do this.)
- **Driving: the road ahead.** A driver steers at a point further on along
  its way the faster it goes (up to 14 world units by the vehicle areas,
  and 18 world units or 2.2 seconds across country, from 14 or 1.6
  seconds), so that it cuts the corners and turns early and gently instead
  of at each corner as it comes to it. `HALO_BOT_OFF=lookahead` turns this
  off.
  In 21 test games on Death Island, Blood Gulch, Sidewinder, Icefields,
  Timberland and Danger Canyon, the vehicles were on their side 0.24% less
  of the driving time (0.75% before); the speed, the kills and the
  deaths in vehicles did not change by more than chance.

### Driving and flying: measured, and what changed

**The driving benchmark.** `HALO_BOT_DRIVEBENCH=<kind>` (`any`, `ground`,
`air`, or part of a vehicle's tag name: `warthog`, `ghost`, `banshee`...)
turns a game into a test of the driving. Every bot leaves everyone alone,
walks to the nearest free vehicle of that kind and gets in, and drives legs
between places at least 40 world units apart (the places of
`HALO_BOT_TOUR=x,y,z;x,y,z;...`, or the map's own places: the same ones
for the same bot in every build). The log has a `drivetest` line for each
leg: its length, its time, how often the vehicle stood (half a unit a
second or less, for a second or more), how long it stood in all, how long
it went backwards, and how it was driven (1 far across country, 2 by the
ground, 3 by the vehicle areas, 4 flown). A leg ends `in`, `given up`
(after 90 seconds, or a second for each 2 units of it), `out` (the driver
got out) or `no way` (no route from where it is). `tools/bot_drivetest.py`
adds the lines up per game and per vehicle.

**On the ground.**

- **The speed for the bends ahead.** A driver going across country works
  out from the way ahead how sharp each bend of it is (the radius of each
  six-unit stretch) and the speed it can take each one at, and brakes for
  the first one it would be too fast for. Before, it took how far the whole
  way ahead swung in all, so a long gentle curve slowed it as much as one
  sharp corner.
- **The grip.** Each vehicle learns how well it holds the ground from its
  own turns: round a turn at speed with its nose where it is going, a
  little more grip; with its tail out, a good deal less. The bend speeds go
  with it (from 0.8 of what they were, on ice, to 1.8, a Ghost or dirt).
  They were tuned for a Warthog on snow everywhere.
- **Round in three.** With its way behind it, standing, and no room before
  it to go round by a loop, a driver backs round with its wheels turned
  away from the way, then drives on.
- **Fewer stops for riders.** A driver with nobody of its own side on foot
  near it, or on the way to it, drives off at once. The longest waits for
  riders are 5 seconds and 3 seconds on the way (7 and 5 before).
- **In a fight, by vehicle.** A Warthog with nobody on its gun runs people
  down where they will be when it gets there and goes on past them for the
  next turn at them; one with a gunner keeps out at the distance the gun is
  good at, never on top of them; a tank backs off from someone close; a
  Ghost goes past and round, never standing.
- **Vehicles that do not get out of where they stand** (the Tachikomas'
  bays at the ends of Hugeass): each time one is given up where it was
  given up before, the bots leave it be twice as long, up to most of ten
  minutes.
- **The same vehicle again.** A bot that gets back into the vehicle it
  just left drives (or flies) it afresh. It used to carry on the old
  "held up" count, so a pilot got straight out of the jet it had landed.
- `HALO_BOT_OFF=grip` puts back the bend speeds, the grip and the loop
  round; `riders`, the waits; `vfight`, the fighting.
- **Measured** with the benchmark (one game of each, 200 seconds at three
  times the speed, eight bots, the same build with
  `HALO_BOT_OFF=grip,riders,vfight,airsearch,airahead,airpull,evade` and
  without; Blood Gulch, Sidewinder, Death Island, Danger Canyon, Icefields,
  Hugeass): the mean speed over the ground did not change (1.38 world units
  a second of progress, against 1.36), the time stood still came down from
  4.1 seconds a leg to 2.6, and more legs ended with the driver out of the
  vehicle (67 of 286, against 52 of 274: perhaps chance). The vehicles are
  slow: about 2 world units a second over a leg, on maps where a Warthog
  does 8 on a straight. The way across country and the corners still cost
  the most; this is not solved.

**In the air.**

- **More route searches.** The pilots' ways round the high ground are
  searched three a tick instead of one, so a pilot waits less for its turn
  while it flies straight at the hill in front of it (`HALO_BOT_OFF=airsearch`).
- **The ground further ahead.** A craft keeps the height for three seconds
  of the way ahead instead of two, and past the turn it is steering for,
  along the next leg of its way (`airahead`).
- **Out of a dive** while there is still the height to pull out in, by how
  fast it comes down and how fast it goes, for every craft
  (`airpull`).
- **Out of the way of a rocket.** A rocket or a fuel rod coming at a craft:
  across its line and down (up, when it is low), whatever it was about.
  Hit on its way somewhere, it weaves (`evade`). (A Banshee of Halo PC has
  no boost to use for it.)
- **A flag carrier does not take a craft that cannot get off** from where
  it stands: a jet facing the wall of its bay, or one under a roof that
  cannot rise on its jets out from under it. On Hugeass a carrier got into
  a jet facing its bay's wall, held the throttle open for six seconds,
  and got out again.
- Measured as the driving was, the flying is as it was (6.35 world units a
  second over a leg, against 6.31: nobody shoots in the benchmark, so it
  does not try the dodging).
- `HALO_BOT_STATS` has the flying in it: `air_seconds` (at the controls),
  `air_up` and `air_pressed` (percent of that time up, and held where it
  is against something), `air_kills`, `air_deaths`, `air_landings`,
  `air_dropoffs`, `air_bumps` (flown into something), `air_stuck` (out of a
  craft that was held up), and `air_trips` with `air_trip_seconds` (the
  mean time from taking a craft to arriving).

**On foot, the top level.** At skill 10 (the top of the scale, as
`master`) a bot runs where it is going and looks where it runs. Its look
about on the way is a short glance to a corner, about half as far round as
before, less often, and never a turn right round with somewhere to be; a
noise or someone gone out of sight pulls its look half as far off the way
it runs. Lower skills look about as before. At skill 10 it also flicks
round faster: 12.5 radians a second instead of 8. `HALO_BOT_OFF=purpose`.
In 40 team games at skill 10 (Hang 'Em High, Battle Creek, Damnation,
Chill Out, Prisoner) with this on for one team, that team scored 4.1
more a game (± 2.2). In test games on Chill Out the bots of skill 10 ran
with their look more than 0.7 radians off the way they ran 24% of the
time, from 45%.

**More for the vehicles.**

- **Each vehicle's own handling.** The speed limits for turning, the
  steering limit, and the braking that a driver plans with (for corners and
  for something in the way) were the Warthog's for every vehicle. Now they
  scale with the top speed in the vehicle's tag, against the Warthog's tag
  (about 8: the tag holds the throttle's speed, not the speed on the
  ground). A Tachikoma (16.5) takes its turns faster, and a slow vehicle
  slower. The log has each vehicle's values once.
  `HALO_BOT_OFF=handling` gives every vehicle the Warthog's.
- **The driving by the areas only under a roof.** A driver held up
  repeatedly on the ground changed to the areas for 12 seconds, everywhere.
  Now it does so only under a roof (something solid within 8 units above it:
  a building, a tunnel). In the open, the ground's own way out is used.
  `HALO_BOT_OFF=inside`.
- **Round something by a clear way, not braking for it.** A driver that
  steers round a tree or a rock by a way that is clear now brakes for that
  way, not for the thing it is going round. The way round is measured again
  each tick. `HALO_BOT_OFF=swerve`.
- **Getting clear ends when it is clear.** After it is held up, a driver
  backs or turns away for two seconds. Now it stops after about half a
  second if it is 3 units from where it was held and the way it goes is
  open. `HALO_BOT_OFF=clearsoon`.
- **Dead ends where a vehicle stands.** A ground vehicle that stands empty
  is checked once for a way out over the ground to where the map's other
  ground vehicles stand. It checks the ground's ways and then, under a
  roof, the areas. A vehicle with no way out is left alone where it stands
  (the log has `bays:`). A vehicle that strands twice at the place where it
  stood when first seen is a dead end too. Those places go in
  `<map>_<bsp>.bays` beside the map's navigation (one line each: the tag
  and the place), so later games know them. A baked `.bays` file is read
  first. On Hugeass, the check found the two Kagotank bays at the ends.
  `HALO_BOT_OFF=bays`.
- **Pilots share their ways.** A way that one pilot found is used for ten
  seconds by any pilot that sets off from near there to the same place
  with no more room needed. There are only a few searches each tick, and
  two craft that left the same pad waited a second each for the same
  search. `HALO_BOT_OFF=airshare`.
- **A hold before a Pelican or a Longsword lands.** A large craft slows all
  the way in and hangs steady over the place for most of a second before it
  goes down. Before, it came down still sliding, and landed at an angle
  across the place or past it. `HALO_BOT_OFF=airhold`.
- **Kills and deaths in vehicles.** `HALO_BOT_STATS` already had
  `vehicle_kills`, `vehicle_deaths`, `air_kills` and `air_deaths`.
- `HALO_BOT_DRIVELIMIT=1` writes to the log, every 900 driving ticks, which
  step of the driving lowered the throttle on each tick: the way (corners,
  narrow places), the place near, things in the way, a wall, the handling,
  held up, getting clear, a run-up, backing. It also gives how many of those
  ticks the speed was under 2.5.

Measured with the driving benchmark (`HALO_BOT_DRIVEBENCH`, 200 seconds a
map on Blood Gulch, Sidewinder, Death Island, Danger Canyon, Ice Fields and
Hugeass), against the same build with all of these off: the ground
vehicles made 3.22 world units a second toward their places, from
2.24, and 3.00 over a finished leg, from 2.35. The
flying is the same as before (10.9, from 11.2 over a leg). The share of
driving ticks slowed by things in the way went from 9.1% to 7.6%. A first
version ended the getting clear too early, after the short getting clear
that a wall starts. Vehicles on Ice Fields then stood for up to 90
seconds, swerving with the throttle shut. That is fixed.

### How the bots find their way, and what they cost

A bot finds its way with a search that starts at its goal. The search goes
only as far as the bot is from the goal. If a bot farther away asks for the
same goal, the search continues from that point. The result is the same as
that of a search of the full map.

The bots do their heavy work in small parts, so that one tick is not much
longer than the others:

- Each team has a map of the places where its bots saw enemies, and its
  routes go around those places. The game makes this map a part in each
  tick, and each team has a new one every 2 seconds.
- A bot that looks for a way around (a flank, another way into a base)
  tries one place in each tick.
- A bot that selects a thing to get asks for the way to a thing only when
  that thing can be the best one. It selects the same thing.
- The bots start a maximum of 3 new searches in one tick for the things
  that they only compare. After that, a thing that has no search has its
  straight-line distance in that tick, and its search in a subsequent tick.
  A bot that walks to a place always gets its route in the same tick.
- A place in a part of the map that has no connection to the part of the bot
  (a part of a very large map, a ledge that nothing goes to) has no route.
  The game knows this without a search.
- In a game with 12 players or more, a bot looks at all the players every
  second tick (every third tick with 24 players or more). In the other
  ticks it looks only at its target.
- A bot far from a target that moves keeps its route until the target has
  moved a fifth of the distance between them (4 world units at most).

`HALO_GAME_SPEED=<n>` runs a test game at n times its speed (the game
does more ticks for each frame; each tick is the same as at the usual
speed). A game with only bots runs 6 to 9 times as fast on one processor
core, and a game on a very large map about 2 times.

`HALO_BOT_PROFILE=1` writes the time that the parts of the bots use to the
log every 5 seconds, with the longest tick, and a line for each bot that
used 2 ms or more in one tick. `HALO_BOT_ROUTE_CHECK=1` compares each
result with a search of the full map. The code is in `game/bots.c` (what a
bot does), `game/bot_move.c` (how it moves), and `game/bot_nav.c` and
`game/bot_aas.c` (the navigation data).

### The flag carrier, escorts and wingmen

In a game of capture the flag:

- A bot with the flag of the other team gets into a vehicle of its team that
  is near (30 world units or less), next to the driver, when home is 40 world
  units away or more. It uses only a seat where the map lets it keep the
  flag. If the map lets it keep the flag at the wheel (a jet on some maps),
  it drives a vehicle that nobody has. It gets out near its own flag, or
  when the vehicle stops for 3 seconds before it is home.
- A bot that drives near a team member with the flag (30 world units or
  less) goes to them, waits for them to get in, and then drives home.
- The attackers that are nearest to a team member with the flag go with
  them: one bot for a team of 3 players or fewer, two bots for 6 or fewer,
  three bots for more. One goes ahead on the way home, one goes behind
  (between the carrier and the players who chase them), and one goes at the
  side. If the carrier is in a vehicle, they get in it if it has a seat. If
  the carrier dies, an escort that is near takes the flag. The defenders
  stay with their flag.
- A person of the team who goes forward (closer to the other base than
  one and a half times the distance to their own base) on foot, without
  the flag, gets a wingman: the nearest attacking bot (45 world units or
  less) goes with them, a few steps behind and to the side.

In all the team games, a bot drives a vehicle of its team that has nobody
at the wheel and a person in another seat (the gun, or next to the driver):
from 80 world units or less, before it does other things (but not before a
fight, and not a defender in capture the flag). A person who drives gets a
gunner first, as before.

`HALO_BOT_STAND_IN=<n>` is for tests: the other bots treat the bots in the
first n places of the game as persons. Thus a game with only bots shows
what the bots do for persons.

### What the bots know of someone out of sight

A bot that has lost sight of someone it was fighting, while they are still
on its motion tracker or it hears them, knows where they are as a tracker's
dot tells it: about there (within a third of the distance), a little more
than once a second, and not which way they are going. It knew where they were
to the inch every tick, and its look followed them along behind the wall.
Where it expects them to come out is where they were last seen, carried on
for at most a second of how they were going. The tracker's other dots are
looked at as seldom, as roughly, and not at their exact height.
`HALO_BOT_OFF=fair` puts back the way it was.

`HALO_BOT_SIGHTCHECK=1` measures it: each tick a bot has someone in its
sights, whether the map is between them by a stricter test than its own
(back faces as well), and the share of the time with nobody in sight that
it looks straight at someone moving behind a wall. In test games on Hang 'Em
High and Chill Out at skill 10, no bot saw through the map (0 of some 15,000
ticks); it looked straight at someone moving behind a wall 16.9% of the time
it had nobody in sight, from 18.2% (most of that is the place it lost them,
where they still are).

### Everyone for themselves: the free-for-all

In a game without teams, a bot of skill 4 and up (fully at 10) plays some
habits of a good free-for-all player. Each habit has a name, and
`HALO_BOT_OFF2=<names>` turns it off for all bots (`all` turns off all of
them). `HALO_BOT_OFF2_T0` and `_T1` turn it off for one team.
`HALO_BOT_OFF2_EVEN` and `_ODD` turn it off for the bots in the even or the
odd player slots. This compares half the bots of a game with the other half.

- `third`: a target with its shields down (from any player) or busy with
  someone else is taken first. This adds to the bot's earlier preference
  for a busy target.
- `focus`: the target nearest to death comes before the nearest target.
  While the bot sees its target, it changes to a new one only for a real
  threat (near, with its gun on the bot).
- `leader`: the player who leads alone is a more likely target. A bot that
  leads plays carefully: it hunts less, does not go to fights it hears, and
  values items and its own places more.
- `reset`: after a kill, with its shields under three quarters, the bot
  goes to cover near it for up to 5 seconds until its shields are full. The
  cover is out of sight of the nearest enemy, and no more enemies see it
  than see where the bot stands.
- `spots`: each map keeps a count for each part of it. A kill from a place
  adds one, and a death there takes one off. The count lasts until the map
  changes, so it carries over from one game on a map to the next. Every 30
  seconds, a bot replaces its worst place (bot_goal_haunts) with one where
  the count is good. A place with a good count is worth more to go to and
  to hold.
- `spawnwise`: with no shields, the bot does not start a fight with a player
  who spawned less than 4 seconds ago with full shields. If one comes at it
  first, it goes to cover.
- `crowd`: a grenade into a fight. The bot looks for players further from
  its target than before, and two players who shoot at each other count as
  a fight.
- `range`: each gun at the range where it wins. The shotgun and the
  flamethrower go in close. A gun with a scope keeps its own range, whatever
  the bot's aggression. The assault rifle stays close and melees a target
  whose shields are gone.
- `disengage`: a fight that the bot is losing (no shields, its health going,
  the enemy in much better shape) makes it go for health further off, up
  to 30 units.
- `combo`: the bot throws a grenade at a near enemy (5 to 9 units) with some
  shield left, then melees when the shields are gone.
- `sounds`: with full shields and health, a fight the bot hears is worth
  more (to take the winner). Hurt, the bot does not go to it.
- `behind`: the bot looks behind after a kill, and every few seconds while
  it stands still. It does not do this while it moves to a place.
- `routes`: hurt, the bot takes the ways that enemies watch least (the
  danger map that the flag carrier uses).
- `style`: each bot has a free-for-all style for the match. The `roamer`
  hunts and explores. The `controller` takes weapons and knows when they
  come back. The `camper` holds a good place. The `opportunist` goes to the
  fights of other players. The debug panel shows the style.
- `powertime`: the time when power weapons and powerups come back (as
  before) counts for more.

`HALO_BOT_STATS` gives these figures for each half of the bots in a game
without teams: `e0_` for the even slots and `e1_` for the odd slots. The
figures are `kills`, `deaths`, `suicides`, `score`, `trades` (deaths within
2 seconds of a kill by the bot) and `unseen` (deaths to a player the bot had
not seen in the 3 seconds before). There is also `contact`: the mean time in
seconds from a spawn to the first enemy in sight.

Measured with all the habits on for half the bots and off for the other
half, 60 games of slayer at skill 10 (5 minutes each, 8 bots: Chill Out,
Hang 'Em High, Prisoner, Damnation, Battle Creek, Wizard), both ways
round: the half with them scored 1.1 (± 1.0) more a game. That is not more
than chance. In groups of habits (24 games each, the score of the half
with them less that of the half without): the targeting habits (`third`,
`focus`, `leader`, `spawnwise`) +1.0 ± 2.4; `reset`, `spots`, `sounds`,
`style`, `powertime` +0.9 ± 2.2; `crowd`, `range`, `disengage`, `combo`
+0.3 ± 2.3 (1.0 fewer deaths ± 1.1); `behind` +2.7 ± 2.5; `routes`
+0.9 ± 2.6.
`tools/bot_ffa_ab.py` makes this comparison for any habits (turned off for
one half of the bots, then for the other half, with the same seeds). It
needs many more games than these to show a difference of a kill or two in
about 90 kills a game.

### Watch the bots: the debug window, the panel and the cameras

On the machine that runs the bots (your own game, or the one you host):

| Key | What it does |
|---|---|
| F7 | Opens (and closes) the bots' window: a second window beside the game's. It has a table of every bot (team, skill, health, shield, weapon or vehicle, goal and how far, who it shoots at, speed, and what it is doing), then all of the bot chosen: its health, place, speed and facing, weapon and rounds, the controls it pressed; its goal, who it is after and where they were last seen, the motion tracker's dots it keeps, what it looks at and why, how it moves and why it stands still, its route; its vehicle and how it drives or flies it; its character; and its last 40 log lines. Its mouse wheel, Page Up, Page Down and Home scroll it; + and - make its text larger and smaller. (Release the mouse with F12 to use it.) |
| Shift+F7 | The chosen bot's part on the game's own screen, on the left (for a game that fills the one screen) |
| Ctrl+F7 | Saves all of it, of every bot, to `bot_debug_<date>_<time>.txt` in the logs folder: a file to send with a report |
| F8 | The next bot (Shift+F8: the one before; Ctrl+F8: the bot in the middle of your view) |
| F10 | The camera: through the chosen bot's eyes, then from behind it, then free, then your own again. The free camera (noclip) flies as you move: the movement keys (or the left stick) move it, the mouse (or the right stick) looks about, fire rises, the grenade button sinks, and the d-pad's up and down change its speed. Your own player stands still while the camera is not yours. |
| Shift+F10 | Back to your own view |

While the window or the panel is open, the chosen bot has lines in the
world: its goal green, who it shoots at red (where they were last seen
orange), the motion tracker's dots it keeps yellow, where it drives cyan, and
the way it moves white. The bots keep their log lines for the window while
it is open (as `HALO_BOT_LOG=1` would, without writing them to the log).

`HALO_BOT_DEBUG` starts the game with any of them: `window`, `panel`, `eyes`,
`behind`, `fly`, `bot=<number>` (the bot chosen), and `save=<seconds>` (the
file of every bot after that many seconds of the game), for example
`HALO_BOT_DEBUG=window,behind,bot=3`.

The window draws with SDL's own software renderer and its built-in font, so
it does not touch the game's OpenGL; there is none on Android, the consoles
or the dedicated server (Shift+F7's panel works everywhere there are bots).

### Measure the bots

`HALO_BOT_STATS=<seconds>` writes a line that starts with `bot stats:` to the
log at that interval of game time (60 seconds when the value is not a
number). Each value counts from the start of the game, thus the last line of
a game is the result of that game:

- `caps0`, `caps1`: the captures of each team (capture the flag), and
  `score0`, `score1`: the score of each team (all the team games).
- `pickups`, `drops`, `returns`: a flag that a player takes, a flag that a
  carrier drops, and a flag that goes home without a capture.
- `carrier_rides`: a bot that gets into a vehicle with the flag.
- `kills`, `deaths`, `suicides`: the totals of the bots.
- `stuck`: a bot that stops trying a way where it is stuck.
  `route_failed`: a way between two areas that a bot cannot use.
- `vehicle_kills`, `vehicle_deaths`: kills by bots in a vehicle and
  bots killed in one. `bailouts`: a bot that gets out of a vehicle because
  it is stuck, or the vehicle cannot go where the bot goes. `dismounts`: a
  bot that gets out to fight on foot. Each counts once a ride.
- `drive_speed`: the mean speed of the bots that drive, in world units a
  second. `flipped`: the percentage of the driving time in a vehicle on
  its side or its roof.
- `idle`, `driving`, `gunning`, `riding`, `escort`, `with_people`: the
  percentage of the time of the bots that they stand on foot with no
  target and no place to hold, drive, use a gun of a vehicle, ride in
  another seat, escort a carrier, and help a person (their gun, their
  vehicle, or as a wingman).

`HALO_BOT_PORTCHECK=1` checks that the game does what a bot asks of it: the
first bot (the others stand) turns its look by a radian, and pushes its
stick forward, to the side, forward and to the side, and back, two seconds
each, and the log has how many ticks the game took to turn it and to start
it moving, and its top speed in that direction. In this build the game
turns a bot and starts it moving on the next tick, and the speeds are the
game's own (2.25 world units a second forward, 2 to the side and back).

`tools/bot_games.py` plays a set of games with no window and gives these
values for each map and for all the maps: per 10 minutes of game,
per hour of bot time, and as percentages. With `--compare`, it plays the
same games with a second build (the same seeds) and gives the difference
and its standard errors (2 or more: the difference is probably not
chance). For example:

```
python3 tools/bot_games.py --build build/linux64/halo --compare old/halo \
    --data ~/halo --games bloodgulch:ctf:12,sidewinder:ctf:14 --seeds 1-6
```

`--data` is a folder with the `maps` folder (and a `navcache` folder, if
you baked the navigation). The two builds must both write `bot stats:`
lines.

`tools/bot_levels` makes 23 small test maps, one for each thing the bots
do (jumps, chains of jumps upward, ramps, teleporters, mazes, open ground to drive, a loop like
Sidewinder). The maps are built with the Halo CE Editing Kit and Invader,
and `tools/bot_levels/test.py` runs the bots on them and sums up the
results. See `tools/bot_levels/README.md`.

## Infection

Infection is a game type of the port for the multiplayer maps. The name of
the game type is `infection`:

- A dedicated server plays it from its playlist (`bloodgulch infection`).
  Refer to [docs/dedicated-server.md](../../docs/dedicated-server.md).
- A host that plays types `gametype infection` in the console while the
  lobby shows. The menus do not show the game type. `gametype <name>` sets
  each game type that a playlist can name.

The rules:

- All the players start a round alive. After `infection.countdown` seconds,
  the game infects one player. This player is the first zombie. A game
  needs two players.
- A zombie has no weapon, no grenades and no shield, and cannot get them. It
  moves faster than a player, and it kills with a blow of its hands (the
  melee button).
- A player who dies while there is a zombie comes back as a zombie. Zombies
  come back as zombies.
- The players of a side cannot hurt each other.
- The round stops when no player is alive. The last player who was alive
  wins the round. If `infection.round_minutes` minutes go by first, the
  players who are still alive win the round.
- A game has `infection.rounds` rounds. A player gets 1 point for each
  zombie that they kill, 1 point for each player that they infect, and 3
  points for a round that they win. The team scores show how many players
  are alive (blue) and how many are zombies (red).
- A zombie that is alone takes two times as much damage to kill, so that
  the first zombie of a round can get to the other players.

The zombies are the Flood of the campaign (the combat form). The
multiplayer maps do not have the Flood. When a game of infection loads its
map, the game takes the Flood from a campaign map in the `maps` folder
(`c20`, `d20`, `c40`, `c10` or `d40`, the first that it finds) and adds it
to the multiplayer map in memory: the model, the animations, the textures
and the sounds. The first game of infection after the game starts takes
approximately one second longer to load, because the game decompresses the
campaign map. The map files do not change.

The host makes its zombies of the Flood only while all the machines of the
game have it. Each machine with a build that has infection tells the host,
when it has loaded the map, whether its map has the Flood. If one machine
has no campaign map with the Flood, or has a build without infection
(OpenCE, a ChupathingyCE without infection), the zombies are players as
they are, with no shield and no weapon: they hit with their hands, as a
player with no weapon does in each build of this network version. The
zombies that are the Flood at that time come back so.

On the network, a game of infection is a game of team slayer, and a machine
with a build without infection can be in it. That machine does not know the
sides: it shows the teams of the lobby, and its player, as a zombie, has
no weapon and sees no other change. The host applies the rules for all the
players. Refer to [NETCODE.md](NETCODE.md#infection).

The game adds the Flood only to the Xbox's multiplayer maps. On a Custom
Edition map (`<name>@ce`), the zombies are players with no weapon.

A game of infection sends no carnage report to the game list, from the
host or from a player who joined it.

The bots play infection. A bot that is a zombie runs to the nearest player
who is alive and hits them. The other bots fight the zombies as they fight
the players of the other team.

The code is in `game/infection.c` (the rules) and `game/tag_import.c` (the
Flood in a multiplayer map).

## Map checks

The game reads a map's tags straight into memory and uses them as its own
structures: every pointer, count, index and enum in them is the map's, and
the game writes values into tags as it runs. So before anything reads a
map's tags, the port checks every tag against a schema of its group
(`game/tag_schema_*.c`, read by `game/tag_validate.c`), and each structure
BSP as it loads:

- Every block and every piece of data must lie in the tags (or the BSP) and
  overlap no other. Otherwise the game refuses the map.
- A block with more elements than the game has room for is cut to the
  maximum. A tag reference that is not a tag of the right group becomes
  none. So do an index past its block and an enum past its values (or they
  become 0, where the game cannot take none). A string gets its terminator.
  Values that the game sets as it runs are reset.
- Checks that the schema cannot express run last: the BSPs' and the models'
  graphs, vertex and index buffers, and indices into other tags.

Each correction goes to `debug.txt`. The game's own maps need none.
`build/linux/map_validate [--strict] map.map...` runs the same checks on map
files without the game, and `tools/test_linux_port.py` runs it on the maps
in `assets/maps`. `map_validate --fuzz <runs> map.map` changes a few words
of the tags at random in each run. The checks must not crash or hang, and a
map that they let through must need no more corrections.

A map's scripts can call only the script functions that a map needs (the
allowlist in `hs/hs.c`). They cannot call the functions for files, the
saved state of the game, the console, debugging or cheats. A script that
calls one does not run. The developer console can call every function.

Defensive checks stay in the game code too. An index into a tag block, the
tags or a tag's data that is out of range gets zeros (`tag_empty_data` in
`tag_files/tag_groups.c`), not other memory.

A map's name must be its file's: the cache file slots are found by the name
in the map's header, so a map file whose header names another map (a
renamed one) is refused, not copied again for ever. The `loading.tga` (or
`xbox_loading.tga`) a map pack may put in the maps folder, shown with
`display.xbox_loading_picture` on, is read only if it is an uncompressed
24-bit picture of 320 by 240, the loading screen's texture.

A checkpoint (`savegame.bin`, in the profile's folder) and a core are
images of the game state's memory: with the data arrays' pointers to their
elements, the objects' memory pool's blocks and the references to them, and
the caches' procedures. Before one is taken, each of those is checked
against what the game made at startup (`game_state_image_accept` in
`saved games/game_state.c`): an image that does not match (a damaged or
crafted file) is refused, and the level starts over.

## What operates

| Area | Status |
| --- | --- |
| Game code | All 466 C files of the game. The changes are in "Game source changes". |
| Graphics | Direct3D 8 on OpenGL 4.5 core through SDL3 (`src/d3d8_gl.c`). The port translates the NV2A vertex shaders and register combiners to GLSL. It decodes all the Xbox texture formats. The vertex and index buffers come from a GL copy of the Xbox memory. |
| High-res HUD | The HUD is drawn from high-res assets: redraws at 8x the size of the maps' bitmaps (4x for the largest), in `port/assets/hud`. They cover the meters, counters, panels and their outlines, the motion sensor, reticles, waypoints and scopes, but no bitmap with English text. `tools/hud_assets.py` makes them from the SVG redraws, and the build embeds them in the executable. When the game uploads one of those bitmaps, `src/hud_hires.c` gives the high-res texture in its place, if the bitmap's pixels are those of the English maps: another language's maps keep their own. The game sizes and places the HUD from its tags as before. `display.high_res_hud = false` turns this off. |
| High-res text | The menus' and HUD's text is drawn with Cyborg Interface Gothic (`port/assets/fonts`, by fizzy, SIL Open Font License) in place of the maps' bitmap fonts, which are Interstate. `src/text_hires.c` rasterizes each glyph with stb_truetype (`port/third_party/stb`) at the resolution the game draws at, into an atlas that a placeholder bitmap of the game stands for. The game lays the text out from its font tags as before. The menus' titles (the screens' headers and the main menu's items) are pictures of text in the maps, so they are drawn as the high-res HUD is: `tools/title_assets.py` sets each one again in OpenCE, Roger White's public-domain Newtown respaced to match the maps' commercial title typeface (`tools/title_font.py`), at 4x the bitmap's size over its own plate or glow, each letter placed where the map's letter is, in `port/assets/titles`. The postgame carnage report's title is set over a hand-made SVG redraw of its panel (`port/assets/titles/svg`) instead. `display.high_res_text = false` turns it off. |
| Anti-aliasing | Off unless `display.anti_aliasing` is set (`src/d3d8_gl.c`, `src/xgpu_post.c`). FXAA (written in the port) and SMAA (`port/third_party/smaa`, MIT licensed, at its HIGH preset, compiled as GLSL) are passes over the 3D view of each window, after the lens flares and before the HUD and the menus (`render/render.c`). Their programs are built when the setting is chosen. Supersampling draws the render targets the size of the screen at two times the resolution in each direction, and the display blit scales them down. Multisampling draws the back buffer and its depth buffer into multisampled renderbuffers, and with them any target that is drawn together with one of them (a mirror's view, in the secondary target with the back buffer's depth buffer), so that the attachments of a framebuffer are all multisampled or none is. A target's pixels are resolved into its texture before something reads the texture (as a texture, or at the display blit). Visibility tests count samples, divided by the samples of a pixel. An alpha-tested surface (foliage, grates) covers the samples of a pixel in proportion to its alpha past the reference (`gl_SampleMask`, not on Android). |
| Sound | Xbox DirectSound on SDL3 audio (`src/dsound_sdl.c`): PCM and Xbox ADPCM, mixed at 48 kHz in stereo, 5.1 or 7.1, with volume, pitch, mix bins, distance, pan, occlusion, obstruction, the Doppler shift (`audio.doppler`) and sound cones, as DirectSound had them. On stereo, Headphone 3D renders the 3D sounds for headphones (`audio.hrtf`, `port/assets/hrtf`). On surround the game gets the AC-3 speaker setting of the Xbox and sends its 2D sounds to the center and back speakers, 3D sounds pan across the front and back pairs (and the sides on 7.1), and the reverb plays from those speakers. The game sends nothing to the LFE channel. |
| Input | XInput on SDL3 (`src/xinput_sdl.c`): keyboard, mouse, gamepads with rumble, and the debug keyboard for the console. |
| Files | The Win32 file functions and the MSVC file functions on POSIX, with the translation of Xbox paths. |
| Threads | Threads, events, mutexes, critical sections, interlocked operations and alertable waits. |
| Memory | The port reserves the Xbox memory at `0x80000000`. Thus the game gets the fixed addresses that it expects. |
| Saved games | The Xbox `UDATA` layout, with SHA-1 signatures. |
| Networking | Winsock on BSD sockets. System link on a local network and on the internet. |
| Bink video | Decoded with FFmpeg behind the Bink calls the game makes (`port/linux/src/bink_ffmpeg.c`), from the disc's `bink/` folder (`d:\bink\*.bik`). The build makes its own static FFmpeg for its target (`tools/ffmpeg_build.py`; `configure.py --without-ffmpeg` leaves it out). A missing or unreadable movie is skipped, as on the Xbox. |

## How the port operates

### The compiler

`tools/linux_build.py` compiles the game with clang and these options, which
give the ABI of the MSVC compiler:

- `--target=i686-linux-gnu`: 32-bit x86.
- `-fms-extensions`: the MSVC extensions.
- `-fshort-wchar`: 16-bit `wchar_t`.
- `-malign-double`: 8-byte alignment of 64-bit members.
- `-fcommon`: tentative definitions, as in C89.

glibc gives only ISO C (`__STRICT_ANSI__`). Thus POSIX names, for example
`random`, do not conflict with the names of the game.

These files supply the MSVC functions that clang does not have:

| File | Contents |
| --- | --- |
| `include/halo_linux_prefix.h` | The first header of each file: the architecture macros of the SDK, MSVC `__inline`, SEH keywords, `__declspec(selectany)`. |
| `include/` | Headers that add MSVC names to the C runtime headers. |
| `port/include/xdk` | The Xbox SDK declarations. The compiler reads this folder after all the other folders. |
| `tools/linux_msvc_semantics.py` | Makes a header that declares each struct tag at file scope, as MSVC does. It also makes the header inline functions weak, as the COMDAT functions of MSVC. `game/msvc_comdat.c` gives one external copy of each. |
| `include/halo_linux_winsock_names.h` | Gives new names to the Winsock functions of the SDK. Thus they do not link to the glibc functions with the same names. |
| `include/halo_linux_source_fixups.h` | Declares the port's functions that the game's sources call. |

`tools/linux_link_check.py` stops the link if a weak reference has no
definition. Without this check, the linker gives the reference the address
0.

### The platform layer (`src/`)

- The files `posix_*.c` use glibc. The compiler uses the ABI of the host
  for these files, because some glibc structures have a different layout
  with `-malign-double`.
- The other files include the SDK declarations through `platform.h`. Thus
  the compiler examines each definition against the SDK prototype.
- `src/halo_linker_common.c` gives weak storage for some globals of the
  January link, and for `fast_ftol_C` and `main_crash`.
- `main/d3d_intimacy.cpp` reads a private structure of the Xbox Direct3D.
  The Linux build does not use this file. `src/d3d8_gl.c` gives
  `d3d_find_flipcount`.
- The build returns small structures and unions in registers
  (`-freg-struct-return`), as on Win32.

### Consoles and other devices

The console ports and the Steam Link build this same platform layer from
their own repositories ([docs/building.md](../../docs/building.md#console-ports)).
The shared code never tests a console's name. It tests capabilities that a
port turns on: `include/halo_console_features.h` (what a console asks of
the platform layer) and `include/halo_port_features.h` (the desktop's
extras a console leaves out, or adds). Each switch is described there.

The Steam Link (2015, Delta platform 19) uses these, among others:

- `HALO_CONSOLE_GLES2`: the renderer's OpenGL ES path on an ES 2 context,
  which the port fills out to the ES 3 that the renderer uses.
- `HALO_CONSOLE_LIGHT_AUDIO`: linear resampling and no reverb by default,
  for its one slow core.
- `HALO_CONSOLE_HUD_ON_DEMAND`: the high-res HUD's pictures decoded when
  first drawn, for its 256 MB.
- `HALO_CONSOLE_PERFORMANCE_MODE`: a lighter picture that the player
  switches in the game (F9, or Back and down on the d-pad).
- `HALO_CONSOLE_CACHE_SCREEN` and `HALO_CONSOLE_SETUP_SCREENS`: the port's
  own screens for the first start, the map import and updates, on a TV.
- `HALO_CONSOLE_APP_CERTIFICATES` and `HALO_CONSOLE_CONFIG_IN_DATA_ROOT`:
  its own certificate bundle for updates, and `config.toml` in the data
  folder.
- `HALO_PORT_UPDATE_PLATFORM`: updates from Chupathingy's releases, as
  `chupathingyce-<platform>-release.zip` in the signed `updates.json`. A
  release is offered only once it has that zip.

Custom Edition maps on a console (`HALO_CUSTOM_EDITION` with the port's
hooks):

- `HALO_CONSOLE_CE_BITMAPS`: a Custom Edition map's bitmaps are made the
  console's as they are read (`platform_ce_bitmap_loaded`).
- `HALO_CONSOLE_CE_SHARED_TAG_CACHE`: a console with no room for a second
  tag cache lends its own to the map while it plays (the `platform_ce_*`
  hooks).
- `HALO_CONSOLE_MAP_BROWSER`: the console's own Map Browser, under ONLINE
  GAMES.

Every console shows the disc's loading picture, not Chupathingy's
(`HALO_CONSOLE_NO_LOADING_PICTURE`).

### Game source changes

Five files of the game have changes for clang. These changes do not change
the MSVC objects: a comparison of all 612 C objects showed no difference in
code or data.

| File | Change |
| --- | --- |
| `cseries/cseries.c` | The naked function `stristr` uses `[ebp+8]` and `[ebp+12]` for its parameters. |
| `bitmaps/bitmap_drawing.c` | `*((word *)p)++` is now `*(*(word **)&p)++`. |
| `rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.c` | `&(T *)x` is now `(T **)&x`. |
| `hs/hs.c` | Local prototypes that did not agree with `ai_script.h` are removed. |
| `units/vehicles.c` | The local prototype of `unit_update_animation` uses the type of `units.h`. |

`math/real_math.h` had a copy of `plane2d_from_points` that did not agree
with the function in `effects/decals.c`. clang used the copy, and parts of
levels were not visible. The copy now agrees with the function.

Other changes:

| File | Change |
| --- | --- |
| `scenario/scenario.c` | The BSP connection tables have names, not MSVC offsets. |
| `rasterizer/xbox/rasterizer_xbox_environment_fog.c` | A local pointer gets its value from the file-scope array with the same name. |
| `game/player_control.c` | The mouse aims the player on controller 1 directly. |
| `sound/game_sound.c` | The game calculates the obstruction of each sound one time for each tick, not for each frame. |
| `cseries/errors.c` | `debug.txt` stays open between lines. A line that is the same as the line before it is counted, not written again (`last line repeated N times`). At 64 MB, `debug.txt` becomes `debug.old.txt` and a new `debug.txt` starts. |
| `networking/`, `game/`, `interface/`, `bungie_net/network/` and the pools of objects, effects and sounds | The system link limits and the memory for them. |
| `game/`, `objects/`, `units/`, `networking/` | The distributed netcode. Refer to `NETCODE.md`. |
| `cache/cache_files.c` | When a map's tags load and unload, the port finds the bitmaps that the high-res HUD replaces (`game/hud_hires_tags.c`), and adds the tags of the menus to the menus' map (`game/menu_tags.c`). |
| `interface/ui_widget.c`, `interface/ui_widget_event_handler_functions.c`, `interface/ui_widget_game_data_input_functions.c` | The main menu is the PC version's from `port/assets/menus` (`display.menus`); the menus' widgets can call the port's functions (`game/menu_functions.c`) and send the PC version's custom activation event; the widgets' memory is 256 KB, not 16 KB; the main menu and Multiplayer clear co-op's controllers, so that a gamepad going back to controller 1 is not a controller unplugged; the lobby's split screen players leave alone, and one who quits in game is not joined to the next game (`interface/player_ui.c`). |
| `input/input_abstraction.c`, `game/player_control.c`, `game/players.c`, `game/player_queues_new.c`, `units/units.h` | The keyboard and mouse's actions (`src/xinput_sdl.c`, `include/halo_keyboard.h`) join controller 1's game controls; their reload key reloads on its own, and their action key only acts (a control flag of the port's, sent with the player's action, stops the reload the controller's X falls back to). |
| `sound/sound_manager.c`, `interface/hud.c`, `game/game_engine.c` | The music's and the other sounds' volumes; the HUD's and the scoreboard's settings are read again when Settings changes them. |
| `rasterizer/xbox/rasterizer_xbox_shadows.c` | With larger shadow maps (`display.shadow_resolution`), the blur's taps stay half a texel apart, and more passes widen it to cover the same part of the map as on the Xbox. |
| `rasterizer/rasterizer.c` | The 128 KB of vertices of the debug display of a model's vertices are static, not on the stack: the compiler inlines the function in `render_model`, and the Windows build probed the stack a page at a time for every model drawn. |
| `rasterizer/xbox/rasterizer_xbox.c` | The first-person weapon's projection is scaled to its own view width (`display.viewmodel_field_of_view`). |
| `models/models.c` | With `display.full_model_detail`, models are drawn with their most detailed geometry at any distance. |
| `render/render.c` | The 3D view of each window is antialiased before the HUD is drawn (`display.anti_aliasing`). |
| `interface/hud.c` | In multiplayer, players' names are drawn above their heads (`display.player_names`, `display.player_name_scale`). |
| `interface/hud.c` | The seconds left on a player's powerups (`display.powerup_timers`, `game/powerup_timers.c`). |
| `interface/progress_bar.c` | The loading screen is made by the CPU (`game/loading_screen.c`) and put over the back buffer (`src/d3d8_gl.c`): the Xbox drew it from its front buffer, through a texture made from that buffer's address, with no vertex shader, which the port's Direct3D does not have. |
| `game/game.c` | A level's load holds the loading screen for `display.loading_screen`'s seconds; the menus' accept skips the rest of the wait (`game/loading_screen.c`). |
| `rasterizer/rasterizer_text.c`, `text/draw_string.c` | Text is drawn from an atlas of the fonts' glyphs, rasterized at the resolution the game draws at (`src/text_hires.c`), when the font has every character of the string. Text can be drawn scaled about a point (`rasterizer_text_set_scale`), as the players' names are. Each glyph's advance is centred on the font tag character's, so the layout is the same, and a glyph is cut at a text box only where the font tag's character visibly was. |

The x86 inline assembly of the game is replaced by C. Thus the compiler
can optimize that code for each processor:

| File | Assembly | Replacement |
| --- | --- | --- |
| `cseries/cseries.h` | x87 `fistp` (`fast_ftol`) | `__builtin_rint` |
| `bitmaps/bitmaps_inlines.h` | x87 conversions | C conversions |
| `math/matrix_math.c` | SSE `matrix4x3_multiply` | a C loop |
| `effects/decals.c` | an x87 conversion | a C conversion |
| `cseries/profile.c` | `rdtsc` | `QueryPerformanceCounter` |
| `cseries/cseries.c` | naked `stristr` | a C `stristr` |
| `cseries/stack_walk_windows.c` | a read of EBP | `__builtin_frame_address` |
| `interface/hud_draw.c` | a read of `[ebp+4]` | `__builtin_return_address(1)` |
| `bink/bink_playback.c` | `int 3` | `__builtin_trap` |

The x87 control and status words (`_control87`, `_statusfp`, `_clearfp` in
`src/msvc_crt.c`) use `fenv.h`. On Android, they use the FPCR and FPSR.
