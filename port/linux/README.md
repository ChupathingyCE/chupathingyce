# Linux

`ninja linux` compiles the game with clang for 32-bit x86 Linux. The result
is a native executable, `build/linux/halo`. The game shows its graphics with
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
"Build options" in the main [README](../../README.md)). The portable build
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
`readelf -V build/linux/halo | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1`.
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
`build/linux64/halo`, the way the macOS build does (the 64-bit build in
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

Enter `build/linux/halo`.

The Steam Deck is not a supported platform yet. The Linux build may run
on it: unpack `chupathingyce-linux64-release.zip` into a folder (in Desktop
Mode), and add `halo` to Steam as a non-Steam game ("Add a Game" in the
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
get it. This occurs at the first start:

- "Xbox disc image" (the default) opens a file picker. Select an Xbox disc
  image (`.xiso` or `.iso`). The game copies `maps/` next to the executable
  and shows the progress. It also copies the disc's Bink movies (`bink/`,
  the `.bik` files), only the ones `bink/` lacks.
- "Halo PC or Custom Edition" opens the same picker for optional extras: a
  Halo PC CD image (`.iso`) fills `maps_pc/` (pc_disc.c), and Custom
  Edition's installer (`halocesetup_en_1.00.exe`) fills `maps_ce/`
  (ce_installer.c). Both read the Microsoft cabinets the maps are packed in
  (cabinet.c). Only maps the folder lacks are written. The game still needs
  the Xbox maps, so it asks again afterward.
- "Make the folders" makes empty `maps/`, `maps_ce/`, `maps_pc/` and
  `maps_md/`, says what goes in each, and checks again when you say so.
- "Quit" stops the game.

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
`addr2line -e build/linux/halo <address>`.

### Crash reports

Releases and nightlies on Linux and macOS also write a crash report when
the game stops because of `SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE` or
`SIGABRT` (`port/linux/src/posix_crash.c`). The report goes to `crashes/`
in the logs folder (`logs/crashes/`; reports from before 0.8.0 wait in
`crashes/` in the data root and are sent from there). When the game starts
the next time, it does what
`crash_reports.upload` says: `"yes"` sends the reports to the ChupathingyCE
site (`network.browser_url`, `POST /v1/crash`) in the background, `"no"`
deletes them, and `"ask"` asks the player first. `crashes/` keeps at most 8
reports.

A report has the same format as on Windows
(`port/linux/src/crash_report.h`), without a minidump: the build, the
system, the signal, up to 32 calls (each a module and an offset in it, for
example `halo+0x2715b4`), and the last 200 lines of `debug.txt` from before
the crash, with each public IP address replaced with `[address]`. To find
the function of a call on macOS, enter `atos -o halo -l 0x100000000
0x1002715b4` (the offset plus `0x100000000`); on Linux, enter
`addr2line -e halo 0x2715b4`.

Local builds send no crash reports. To test the crash reports with such a
build, set the `HALO_CRASH_REPORTS_ANY_BUILD` environment variable. Android
and the dedicated server have no crash reports.

## Controls

The keyboard and the mouse are a control scheme of their own for the player
of controller 1: each action has up to two keys or mouse buttons, which
Settings > Controls > Keyboard (or `[controls]` in `config.toml`) changes. The
game adds the input of the first gamepad to controller 1. The other
gamepads operate controllers 2 to 4. With two or more players on this
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
back (escape resumes the game from the pause menu), delete deletes. When the game continues, the mouse aims again. A mouse button that you hold from the menu does not fire until
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

## Settings

The settings are in `config.toml` next to the executable
(`build/linux/config.toml`). At the first start, the game writes the file
with a comment for each setting and its default value, commented out:

```toml
# The menus: "xbox" for the Xbox's (with Online Games), "gearbox" for
# ...
# menus = "xbox"
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
(smoother edges, sharper shadows, the Chupathingy menus, and on Android a
larger audio buffer). Until it is answered, the settings keep their old
values and the file is left as it is. "Keep mine" writes the old values into
`config.toml`; "Use the new settings" lets them follow the new defaults. The
answer is `game.defaults_answered`; the question does not come again. A new
`config.toml` has the new defaults and is not asked. A run without anyone to
answer (a hidden window, a test) logs the question and keeps the old values.
The dedicated server never asks and keeps its own defaults.

Each setting has an environment variable. The environment variable changes
the setting for one start of the game. It has priority over the file.

| Setting | Default | Environment variable | Function |
| --- | --- | --- | --- |
| `display.mode` | `""` | `HALO_DISPLAY_MODE` | `"fullscreen"`: the display, taken at the mode of `display.resolution` (the nearest the display has), or at its desktop mode. `"borderless"`: a window over the whole desktop, whose mode does not change. `"windowed"`: a window of `display.window_size`. Empty: `display.fullscreen` decides (`true`: borderless). When it is set, `display.fullscreen` is ignored, and the log says so if both are set. F11 changes between the window and the fullscreen mode. Video Setup sets it. |
| `display.fullscreen` | `true` | `HALO_FULLSCREEN` | `true`: borderless, as `display.mode = "borderless"`. `false`: a window, as `display.mode = "windowed"`. Used only when `display.mode` is empty. |
| `display.resolution` | `"native"` | `HALO_RESOLUTION` | What fullscreen and borderless draw at: `"native"`, the display's own resolution, or `"<width>x<height>"`, such as `"1920x1080"`, 640x480 or more. Fullscreen sets the display to it. Borderless draws at it and scales the picture to the display, where the display has room for it. The picture has 480 lines of the game and the width of the resolution's shape. A window draws at its own size instead. Video Setup's Resolution sets it, from the display's modes; it shows with Fullscreen and Borderless. |
| `display.resolution_scaling` | `"native"` | `HALO_RESOLUTION_SCALING` | `"native"`: the game draws at the resolution of the window, or of the display (or `display.resolution`) fullscreen. `"original"`: the game draws the 640x480 picture of the Xbox and scales it up to the window or the display, whatever `display.mode` is. Video Setup sets it. |
| `display.window_size` | `""` | `HALO_WINDOW_SIZE` | The size of the window, as `"<width>x<height>"`, such as `"1920x1080"`, 640x480 or more. You can change the size of the window; the game's picture takes its shape. Empty: `display.window_scale` decides. Video Setup's Window Size sets it, from sizes of each shape (4:3, 16:10, 16:9 and 21:9) that fit the desktop; it shows with Windowed. |
| `display.window_scale` | `2` | `HALO_WINDOW_SCALE` | Used when `display.window_size` is empty: the size of the window, as a multiple of 640x480. |
| `display.vsync` | `true` | `HALO_NO_VSYNC=1` sets `false` | `true`: each frame waits for the display. |
| `display.max_fps` | `0` | `HALO_MAX_FPS` | With vsync off, the most frames each second. `0`: twice the display's refresh rate (on Android, 120). `-1`: no limit, which can hang some Intel graphics (Raptor Lake), resetting the desktop's graphics too, and heats a phone. |
| `display.anti_aliasing` | `"msaa2x"` on 64-bit computers, `"off"` on 32-bit ones, Android and the server | `HALO_ANTI_ALIASING` | The smoothing of jagged edges, which the Xbox did not have. `"off"`: none, as on the Xbox. `"fxaa"` or `"smaa"`: a pass over the 3D view after the game draws it. The HUD and the menus stay sharp. SMAA is the sharper and costs more. `"ssaa2x"`: the game draws at two times the resolution in each direction (at most the GPU's largest texture), and the picture is scaled down. The GPU does four times the work. Not with `display.resolution_scaling = "original"`. `"msaa2x"`, `"msaa4x"` or `"msaa8x"`: each pixel of the 3D view has that many samples (at most the GPU's). On Android, `"smaa"` gives FXAA and `"ssaa2x"` none. A change applies from the next frame. Refer to "Anti-aliasing" in "What operates". |
| `debug.gpu_flush_draws` | `-1` | `HALO_GPU_FLUSH_DRAWS` | Flush the GPU's pipeline every this many draws. `-1`: every 3 on Intel graphics with Mesa's driver, which can otherwise hang in the game's long runs of small draws and reset the desktop's graphics too. `0`: never. |
| `display.interpolation` | `true` | `HALO_INTERPOLATION` | `true`: one frame for each refresh of the display. `false`: 30 frames each second, as on the Xbox. Video Setup's Frame Rate sets it (30 (Xbox) is `false`, the others `true`). Refer to "Frame rate". |
| `display.direct_camera` | `true` | `HALO_DIRECT_CAMERA` | `true`: in first person, on foot, the view points where the player aims in each frame, not where the last tick left it. Refer to "Frame rate". Not in Video Setup: config.toml only. |
| `display.field_of_view` | `70.0` | `HALO_FIELD_OF_VIEW` | How wide the first-person view is, in degrees across a 4:3 picture: `70`, as on the Xbox, from `50` to `120`. On a wider screen the view shows more at the sides, as before. Scopes zoom in by as much as before. Only the first-person view changes: cutscenes, the third-person views and the game itself (aim, auto-aim, what each machine sends) do not. Video Setup's Field of View sets it in the menus. |
| `display.viewmodel_field_of_view` | `0.0` | `HALO_VIEWMODEL_FIELD_OF_VIEW` | How wide the first-person weapon and arms are drawn, in degrees as `display.field_of_view`: `0` (the default) follows `display.field_of_view`, as before; `50` to `120` draws them at that width whatever the view's, so a wide view does not stretch them. `70`: the weapon as on the Xbox. Drawing only. |
| `display.far_clip_distance` | `1024.0` | `HALO_FAR_CLIP_DISTANCE` | How far the world is drawn, in world units: `1024`, as on the Xbox, up to `10000`. Raise it on large maps that cut off in the distance (Extinction, Hugeass). It is a floor under the map's own far clip, so it holds across map loads and joins, in single player and multiplayer; scripts and `init.txt` can raise the clip further but not below it. Only the picture changes: the game itself, what each machine sends and the near clip do not. Fog can still end the view sooner. Depth precision at a distance gets coarser as the value grows (about 9 units at 3000, 95 at 10000), so distant overlapping surfaces may flicker at the largest values. |
| `display.match_timer` | `false` | `HALO_MATCH_TIMER` | `true`: in a multiplayer game with a time limit, the time left is drawn at the top of the screen. `false`: the time left shows only on the scoreboard (hold BACK, or tab), as on the Xbox. |
| `display.high_res_hud` | `true` | `HALO_HIGH_RES_HUD` | `true`: the HUD (meters, counters, panels and their outlines, the motion sensor, reticles, waypoints, scopes) is drawn from the high-res assets in `port/assets/hud`, 8x the size of the maps' bitmaps. The bitmaps with English text keep the maps' own. `false`: the maps' own bitmaps. |
| `display.high_res_text` | `true` | `HALO_HIGH_RES_TEXT` | `true`: the menus' and HUD's text is drawn with the fonts in `port/assets/fonts` (Cyborg Interface Gothic, in place of the maps' Interstate) at the resolution the game draws at, laid out as before, and the menus' titles are drawn from the high-res pictures in `port/assets/titles`. `false`: the maps' bitmap fonts and titles. It covers the game's own text only: the Server Browser and the moderation overlays use the system's fonts in any case. For the blocky look of the Xbox, also set `display.resolution_scaling = "original"`. |
| `display.high_res_font` | `""` | `HALO_HIGH_RES_FONT` | The player's own font file (`.ttf` or `.otf`) for the high-res text and the menu text of Online Games, in place of the built-in fonts (`port/assets/fonts/fonts.json`). A relative path is in `data/fonts/` in the data root (a path that leaves that folder is refused); an absolute path is used as it is. A file that is missing or is not a font is logged, and the built-in font is drawn. A change applies while the game runs. |
| `display.high_res_font_large` | `""` | `HALO_HIGH_RES_FONT_LARGE` | As `display.high_res_font`, for the large text only (the font tags with the role `large` in `fonts.json`: `ui\large_ui`, `ui\interstate`; Online Games' large menu text). Empty: `display.high_res_font`. |
| `display.shadow_resolution` | `256` (the server: `128`) | `HALO_SHADOW_RESOLUTION` | The size of the maps that the shadows of the objects are drawn in, in pixels each way: `128`, `256`, `512` or `1024` (other values go down to one of these). The game draws the shadow of each object into a map of 128x128 pixels, blurs it and projects it onto the ground. On a large screen, the edges of these shadows show steps that move when the object moves. A larger map makes the edges smooth; the blur is made wider to match, so the shadows are as soft as on the Xbox. Each doubling adds two passes of the blur. `128`: as on the Xbox. |
| `display.anisotropic_filtering` | `0` | `HALO_ANISOTROPIC_FILTERING` | Sharper textures on surfaces seen at a slant, such as floors and walls in the distance: `0` (off, as on the Xbox), `2`, `4`, `8` or `16` (other values go down to one of these, and to the most the graphics can do). Applies to the textures that have mip levels; the menus and HUD look the same. |
| `display.full_model_detail` | `false` | `HALO_FULL_MODEL_DETAIL` | `true`: every model (characters, vehicles, weapons, scenery) is drawn with its most detailed geometry at any distance, instead of the simpler versions the game uses for models far away. Drawing only: the game itself is unchanged. `false`: as on the Xbox. |
| `display.menus` | `"chupathingy"` | `HALO_MENUS` | `"chupathingy"` (the default): the Xbox's menus with Halo PC's PROFILES, SETTINGS and QUIT, the settings pages, and Online Games, in the Xbox's look. `"xbox"`: the Xbox's menus, as close to the console as can be, with Online Games. `"gearbox"` (or the older `"pc"`): Gearbox's Halo PC menus. The Halo PC screens are built from the files in `port/assets/menus` and a `menus` folder next to `config.toml`. A file that sets `"xbox"` (the default before 0.8.0) keeps it. Refer to "Menus". |
| `display.version_watermark` | `false` | `HALO_VERSION_WATERMARK` | `true`: the build's version ("0.7.5a (5c1a363)"), which the main menu shows small and faint in the bottom right corner, is shown there while playing too: handy for screenshots and bug reports. |
| `display.player_names` | `"all"` | `HALO_PLAYER_NAMES` | In multiplayer, whose names are drawn above their heads: `"all"`, `"allies"`, `"enemies"` or `"none"`. An ally's name is drawn above the triangle the game shows over teammates. An enemy's name shows only within the motion sensor's reach, while the enemy is in sight and not camouflaged, so it never shows where an enemy hides. The gametype's motion tracker setting also applies: no names if it shows no players, only allies' if it shows only friends. |
| `display.player_name_scale` | `1.0` | `HALO_PLAYER_NAME_SCALE` | How large the players' names are drawn: `1.0` is three quarters of the size of the HUD's text, from `0.25` to `4`. With high-res text, larger names are rasterized at their size, so they stay sharp. |
| `display.follow_camera` | `"long_waits"` | `HALO_FOLLOW_CAMERA` | In multiplayer, when a dead player's camera follows a living player from behind once the original death camera is done (3 seconds): a teammate in a team game, the killer (or the leader) in a game without teams. LB and RB (Q and E, or the mouse wheel, on the keyboard) switch who, and a "FOLLOWING" line near the bottom says who; it returns to the player's own view on respawn. `"long_waits"`: only when the wait to respawn is long (`display.follow_camera_after`), the player is out of lives or odd man out, or the game type holds spawns until its next round; a short respawn keeps the original death camera. `"always"`: after every death. `"off"`: the original death camera only. Only what this machine shows changes, so it works in any host's game. Split screen: each player's own. |
| `display.follow_camera_after` | `5.0` | `HALO_FOLLOW_CAMERA_AFTER` | For `display.follow_camera`'s `"long_waits"`: how many seconds of the wait to respawn must be left once the death camera is done for the camera to follow someone. |
| `display.powerup_timers` | `false` | `HALO_POWERUP_TIMERS` | `true`: a small line of the HUD's text near the top of the view shows the seconds left on the player's active camouflage, overshield and full spectrum vision while one is on ("CAMO 12   OVERSHIELD 41"). The overshield's time is what is left of it above full: it counts down only in multiplayer, where it drains, so in campaign it shows none. Split screen: each player's own. `false`: none, as the original game. |
| `display.loading_screen` | `true` | `HALO_LOADING_SCREEN` | Keep the original loading screen (the picture with the glow that crosses it) up for a while when a level loads, as loading from the Xbox's disc did; the port's loads are usually over before it shows. How long is set for each kind of game below; the default is no wait. Press A (Space or Enter on the keyboard) to skip the rest of the wait; a load that is still running finishes first. Also when the level is already loaded (a restart). Never for the main menu or a saved film. `false`: every level loads as fast as it can. |
| `display.loading_screen_campaign_seconds` | `0.0` | `HALO_LOADING_SCREEN_CAMPAIGN_SECONDS` | The least seconds the loading screen stays up when a campaign level loads on this machine alone, split screen co-op too, up to 30. A load that takes longer shows for as long as it takes. `0`: no wait. A value set in 0.8.0's `display.loading_screen_min_seconds` is moved to this setting the first time the game starts. |
| `display.loading_screen_multiplayer_seconds` | `0.0` | `HALO_LOADING_SCREEN_MULTIPLAYER_SECONDS` | The same for every other game: split screen multiplayer, system link, online and network co-op, on a machine that joins and on one that hosts and plays, up to 5: while the loading screen is held, the machine hears nothing from the network, and a host drops a machine it hears nothing from for 15 seconds. Machines already finish loading at their own times. Never on the dedicated server. `0`: no wait. |
| `display.scoreboard_team_layout` | `"teams"` | `HALO_SCOREBOARD_TEAM_LAYOUT` | How the multiplayer scoreboard (hold BACK, or tab) lists a team game's players. `"teams"`: a column for each team, red on the left and blue on the right. `"score"`: all the players in order of score. With more players than fit, the mouse wheel and Page Up / Page Down scroll the scoreboard. |
| `display.scoreboard_background` | `true` | `HALO_SCOREBOARD_BACKGROUND` | `true`: the multiplayer scoreboard (hold BACK, or tab) has a panel behind its text, for clearer text. |
| `display.scoreboard_platforms` | `"auto"` | `HALO_SCOREBOARD_PLATFORMS` | A small logo of each player's platform (Windows, macOS, Linux, Android, Steam Deck, Xbox and so on) before their name on a network game's scoreboard, for players whose client sends it over Delta Peer; other players (OpenCE, retail) get no logo. `"auto"`: shown with the Chupathingy menus only, since the Xbox and Gearbox menus stay close to the originals. `"on"`: with any menus. `"off"`: never. |
| `display.delta_profile` | `true` | `HALO_DELTA_PROFILE` | Delta Profile in the game: the DELTA row of Profiles (the Chupathingy menus) and button of Select Profile (the Gearbox menus), the "Delta:" line on a Halo profile, and Online Games' Start. `false` hides them all, and Start opens the profile's web page as before. Link Profile (RB in Online Games) stays. Refer to `docs/delta.md`, Delta Profile in the game. |
| `display.profile_names` | `true` | `HALO_PROFILE_NAMES` | On the scoreboard, the postgame report and the lobby, players who share their Delta profile (`network.share_profile`) show by the profile's name, in games on Delta. `false`: every player by their game name. OpenCE players, and what OpenCE players see, are unchanged. |
| `display.show_quit_players` | `true` | `HALO_SHOW_QUIT_PLAYERS` | `true`: players who quit stay on the multiplayer scoreboard (hold BACK, or tab) and the score in the corner, as in the original game. `false`: they are left off, and the players still in the game are ranked among themselves, as OpenCE does. Only what this machine draws changes. |
| `display.scoreboard_background_color` | `"16, 16, 16, 150"` | `HALO_SCOREBOARD_BACKGROUND_COLOR` | The colour of the scoreboard's panel: `"red, green, blue, alpha"`, each from `0` to `255`. Alpha `0` is see-through, `255` is solid. |
| `display.pause_change_teams` | `true` | `HALO_PAUSE_CHANGE_TEAMS` | In a team game, the pause menu's CHANGE TEAMS (Halo PC's, in every menu set): it asks the host to put you on the other team, and the chat says what came of it. Needs a Chupathingy host (docs/delta.md, Team changes). `false` hides it. |
| `display.pause_controls` | `true` | `HALO_PAUSE_CONTROLS` | The pause menu's CONTROLS, in the Xbox and Gearbox menus: the controller's buttons and the keyboard's keys (Controls above). `false` hides it. The Chupathingy menus have none: their SETTINGS opens Controls. |
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
| `audio.enabled` | `true` | `HALO_NO_AUDIO=1` sets `false` | `false`: no audio device. The sound continues without output. |
| `audio.volume` | `1.0` | `HALO_VOLUME` | The master volume. |
| `audio.music_volume` | `1.0` | `HALO_MUSIC_VOLUME` | The music's volume, of the master volume. |
| `audio.effects_volume` | `1.0` | `HALO_EFFECTS_VOLUME` | The volume of the other sounds (effects and speech), of the master volume. |
| `audio.device` | `"default"` | `HALO_AUDIO_DEVICE` | The output the sound plays on, by the name the system gives it (Audio Setup's Output Device lists the connected ones). `"default"`: the system's default output, followed as it changes. A device that is not connected plays on the default, and the setting is kept for when it is back. Desktop only. |
| `audio.buffer_frames` | `2048` on macOS, `1024` on Android, `512` elsewhere | `HALO_AUDIO_BUFFER_FRAMES` | The audio device's buffer, in sample frames at 48 kHz, from `64` to `8192`. Larger rides out stalls that would cut the sound out; smaller has less delay (512 is 11 ms, 1024 is 21 ms, 2048 is 43 ms). |
| `audio.channels` | `"auto"` | `HALO_AUDIO_CHANNELS` | `"stereo"`, `"5.1"` or `"7.1"`: surround sound as with Dolby Digital on the Xbox. The game sends its 2D sounds to the center and back speakers as the Xbox did, 3D sounds are panned around the front and back speakers (and the sides on 7.1), and the reverberation plays from all of them. `"auto"`: 5.1 on a device of 6 channels, 7.1 on one of 8, else stereo (a device of some other count is more likely an audio interface's outputs than speakers). The LFE channel is left to the receiver's bass management, as on the Xbox. Read when the game starts. Desktop only. Audio Setup: Speakers. |
| `audio.hrtf` | `"off"` | `HALO_AUDIO_HRTF` | `"on"`: 3D sounds are rendered for headphones (binaural, with head-related transfer functions): each is filtered as the head and ears would hear it from its direction, the far ear a little later, so it sounds in front, behind, to the side, above or below you instead of between your ears. The Xbox filtered its 3D sounds for direction too; these are measured responses (the SADIE II database's KEMAR, `port/assets/hrtf`), equalized so they do not change the sounds' tone overall. Close sounds (your own footsteps and weapons) stay centered as with the stereo pan. `"auto"`: on when the output's name says it is headphones (a headset, AirPods, "Headphones"), checked again when the output changes. `"off"`: the stereo pan. Stereo output only; changes take effect at once. Audio Setup: Headphone 3D. |
| `audio.doppler` | `true` | `HALO_AUDIO_DOPPLER` | `true`: moving sounds are Doppler shifted, higher coming at you and lower going away (a Warthog, a Banshee, a projectile going past), from the velocities the game gives each sound, as the Xbox's DirectSound shifted them. `false`: every sound plays at its own pitch. Audio Setup: Doppler. |
| `audio.reverb` | `true` | `HALO_REVERB` | `true`: the sounds of the world reverberate as the place the player is in does: the sound environments of the maps (a corridor, a cave, a large hall, outdoors) set the reverberation, as the I3DL2 reverb of the Xbox did. A sound behind a wall or a door is muffled in it too. `false`: no reverberation (sounds behind a wall are still muffled). |
| `audio.resampling` | `"sinc"` | `HALO_AUDIO_RESAMPLING` | How the sounds, most of them recorded at 22 kHz, are brought to the output's 48 kHz. `"sinc"`: a windowed sinc low pass keeps each sound's band (flat to about 9 kHz for a 22 kHz sound) and nothing above it. `"linear"`: linear interpolation, as before OpenCE's build 130: the top of each sound's band is duller (-4 dB at 8 kHz) and images of the band reach up to 22 kHz, a brighter, grainier sound. |
| `input.mouse_sensitivity` | `1.0` | `HALO_MOUSE_SENSITIVITY` | The multiplier for the mouse aim. |
| `input.mouse_vertical_sensitivity` | `0.0` | `HALO_MOUSE_VERTICAL_SENSITIVITY` | The multiplier for the vertical mouse aim. `0`: the same as `input.mouse_sensitivity`. |
| `input.invert_mouse` | `false` | `HALO_MOUSE_INVERT=1` sets `true` | `true`: the vertical mouse aim is inverted. |
| `input.keyboard_prompts` | `true` | `HALO_KEYBOARD_PROMPTS` | Not in the menus: every button prompt follows the device used last. While the keyboard or mouse was used last (or, on the desktop, no controller is connected), the prompts show keys, as Halo PC's did: the HUD's messages name the bound key in brackets ("Hold [E] to pick up"), "Hold BACK for score" names the scoreboard key, and the menus' button pictures ("A = SELECT") become key caps of the keys that press them (Enter, Backspace, Tab, E). A controller's button press switches them back. `false` keeps the controller's buttons everywhere, whatever was used last. |
| `input.mouse_aim_assist` | `false` | `HALO_MOUSE_AIM_ASSIST` | `true`: the magnetism of the controller also operates for the mouse. `false`: when the mouse moved after the right stick, the view is not slowed or dragged by a target. The autoaim of the bullets operates in both cases. |
| `controls.<action>` | (the table in "Controls") | `HALO_KEY_<ACTION>` | The keys and mouse buttons of an action, up to two, separated by a comma: `move_forward`, `move_backward`, `strafe_left`, `strafe_right`, `jump`, `crouch`, `fire`, `throw_grenade`, `melee`, `reload`, `zoom`, `switch_weapon`, `switch_grenade`, `action`, `flashlight`, `scoreboard`, `pause`. Keys by their names (`"W"`, `"Space"`, `"Left Ctrl"`, `"F1"`), and `"Mouse Left"`, `"Mouse Right"`, `"Mouse Middle"`, `"Mouse 4"`, `"Mouse 5"`, `"Wheel"` (either way), `"Wheel Up"`, `"Wheel Down"`. |
| `controls.controller_layout` | `""` | `HALO_CONTROLLER_LAYOUT` | The controller's Custom button layout, which Controller Buttons writes: `"action=button"` for each action, separated by commas (`jump`, `switch_grenade`, `action`, `switch_weapon`, `melee`, `flashlight`, `throw_grenade`, `fire`, `crouch`, `zoom`; `A`, `B`, `X`, `Y`, `LB`, `RB`, `LT`, `RT`, `LS`, `RS`, LB and RB being the Duke's White and Black). An action left out keeps its default button; no button is ever on two actions. Used by profiles whose button layout is Custom. Empty: the default layout. |
| `game.downloaded_maps` | `""` | `HALO_DOWNLOADED_MAPS` | Maps to play as downloaded maps, until the game downloads maps itself: their names as the game names them (`bloodgulch`, `hugeass@ce`), separated by commas, or `"*"` for all the maps. The scripts of a downloaded map cannot change the settings of the player or the games of other players. `debug.txt` names what the game refused. |
| `game.pack_menus` | `""` | `HALO_PACK_MENUS` | Play with an Xbox map pack's own menus, by the pack's folder name: `"cex"` loads `maps_cex/ui.map` (Halo CE+'s) in place of the game's `ui.map`, its multiplayer maps being the pack's (`<file>@cex`) and its scripts allowed to start the pack's own levels when the library knows the pack. Empty: the game's menus. Takes effect after a restart. Refer to "Xbox map packs" in the README. |
| `game.bomb_spawns` | `""` | `HALO_BOMB_SPAWNS` | Where a Bomb or Grifball game you host puts the bomb on a map: `"<map> <x> <y> [<z>]"` (the map as a playlist names it: `bloodgulch`, `timberland@ce`), entries separated by commas. The bomb rests on the first ground under the point (from a little over its height, when given). A map not named has the game's own spot: Blood Gulch's halfway between its teleporters' exits, any other map's halfway between the goals. Only the host's setting counts. |
| `game.bomb_open_pitch_speed` | `0` | `HALO_BOMB_OPEN_PITCH_SPEED` | Everyone's speed in a Grifball: Open Pitch game you host, in percent (50 to 200; the carrier's 115% on top stops at 200). `0`: the game type's own, 175. |
| `game.bomb_open_pitch_carrier_health`, `game.bomb_open_pitch_carrier_resistance` | `0`, `0` | `HALO_BOMB_OPEN_PITCH_CARRIER_HEALTH`, `HALO_BOMB_OPEN_PITCH_CARRIER_RESISTANCE` | The bomb carrier in a Grifball: Open Pitch game you host: health in percent of everyone's, damage resistance in percent. `0`: the game type's own, 400 each (the carrier lives through one melee blow, not two). |
| `game.bomb_open_pitch_carrier_shields` | `"none"` | `HALO_BOMB_OPEN_PITCH_CARRIER_SHIELDS` | The bomb carrier's shields in a Grifball: Open Pitch game you host: `"none"`, `"normal"` or `"overshield"`. |
| `game.bomb_open_pitch_fumble` | `true` | `HALO_BOMB_OPEN_PITCH_FUMBLE` | `true`: in a Grifball: Open Pitch game you host, a melee blow that doesn't kill the bomb carrier knocks the bomb out of their hands. |
| `game.console_log` | `"important"` | `HALO_CONSOLE_LOG` | What the console shows on the screen. `"important"`: bans, players that the host drops for cheating, the reasons that the game refuses a command, and the asserts that stop the game. `"all"`: all the lines. `"none"`: only the asserts that stop the game. The output of a command always shows. `debug.txt` gets all the lines. The developer console's own output is drawn only while the console is open (`terminal_render 2`, the default); `terminal_render 1` draws it all the time and `0` never. |
| `game.language` | `""` | `HALO_LANGUAGE` | The language of the menus: `ja`, `de`, `fr`, `es` or `it`. Empty: English. |
| `game.unlock_all_levels` | `false` | `HALO_UNLOCK_ALL_LEVELS` | `true`: a profile created afterward starts with all the campaign levels finished on all the difficulties. `false`: a new profile starts at The Pillar of Autumn, and each level opens when you finish the level before it, as on the Xbox. Existing profiles keep their progress. To start a profile over, type `profile_reset_campaign` in the console. |
| `game.start_with_one_player` | `false` | `HALO_START_WITH_ONE_PLAYER` | A system link, internet or split screen game you host can start with you alone: press START GAME in the lobby. Others can join it in progress. `false` waits for a second machine (split screen: a second player), as on the Xbox. Network Setup's Start With One Player sets it. |
| `paths.data` | `""` | `HALO_DATA_ROOT` | The data root. Refer to "Start the game". |
| `paths.saves` | `""` | `HALO_SAVE_ROOT` | The save root. Refer to "Files and folders". |
| `paths.logs` | `""` | `HALO_LOG_ROOT` | The logs folder (`debug.txt`, `halo.log`, `crashes/`). Empty: `logs/` in the data root. Refer to "Files and folders". |
| `paths.cache` | `""` | `HALO_CACHE_ROOT` | The cache folder. Empty: `cache/` in the save root. |
| `network.address` | `""` | `HALO_NET_ADDRESS` | The IPv4 address of this machine for system link. Refer to "Play on one computer". |
| `network.broadcast` | `""` | `HALO_NET_BROADCAST` | IPv4 addresses, with commas between them, that get the broadcasts of the game. Empty: 255.255.255.255. |
| `network.online` | `true` | `HALO_NET_ONLINE` | `true`: internet play. `false`: system link on the local network only. |
| `network.join_from_clipboard` | `"never"` | `HALO_NET_JOIN_FROM_CLIPBOARD` | An invite link on the clipboard when the game comes to the front: `"ask"` asks over the menus whether to join its game (with its name and map when the game list knows them; No is kept for that invite), `"always"` joins it, `"never"` leaves it. `true` and `false`, from older versions, mean `"always"` and `"never"`. Network Settings in the Chupathingy and Gearbox menus. |
| `network.copy_to_clipboard` | `false` | `HALO_NET_COPY_TO_CLIPBOARD` | `true`: when this copy starts to host a game, its invite link goes on the clipboard. `false`: the game leaves the clipboard alone; the link is in the log, and INVITE LINK in the Gearbox menus' Server Settings copies it. Network Settings in the Chupathingy and Gearbox menus. |
| `network.map_downloads` | `"ask"` | `HALO_NET_MAP_DOWNLOADS` | A Custom Edition map a game you join needs and you don't have, from the community map repositories (maps.halonet.net and its mirrors, as Chimera downloads from them). `"ask"`: the Online Games screen offers to download it (Yes, No, Don't ask again, which writes `"always"` or `"never"` here). `"always"`: downloaded without asking. `"never"`: the game only says which map is missing. Network Settings in the PC menus. The map is checked before it goes in `maps_ce`: refer to "Halo PC maps" in the [README](../../README.md). Not on the dedicated server. |
| `network.tunnel_port` | `0` | `HALO_NET_TUNNEL_PORT` | The UDP port for internet play. `0`: the game selects a port. Refer to "Internet play". |
| `network.allow_upnp` | `true` | `HALO_NET_ALLOW_UPNP` | `true`: internet play can ask the router to forward its port (UPnP). `false`: the game does not ask. Refer to "Internet play". |
| `network.protocol` | `"auto"` | `HALO_NET_PROTOCOL` | Delta Peer, the messages between ChupathingyCE machines beside OpenCE's protocol (UDP port 5160; `docs/delta.md`). `"auto"`: Delta with the machines that speak it, plain OpenCE with the others. `"opence"`: Delta off. `"delta"`: as `"auto"` for now. |
| `network.chat` | `"on"` | `HALO_NET_CHAT` | Text chat in the games this copy hosts: `"on"`, `"team"` (team lines alone) or `"off"`. |
| `network.team_change` | `"balanced"` | `HALO_NET_TEAM_CHANGE` | In the team games this copy hosts, whether players may change teams during the game: `"balanced"` (only while neither team ends up more than `network.team_change_margin` players bigger), `"allow"` or `"off"`. A player changes at most every 10 seconds and never leaves a team empty; they respawn on the new team with no death or penalty counted. |
| `network.team_change_margin` | `1` | `HALO_NET_TEAM_CHANGE_MARGIN` | With `"balanced"`, the most players one team may have over the other after a change (0 to 16). |
| `network.chat_legacy` | `true` | `HALO_NET_CHAT_LEGACY` | In the games this copy hosts, OpenCE players chat too, over their own text chat, which the host bridges to Delta chat (`docs/delta.md`, Chat): they see every line and theirs reach everyone, under the same rules. Follows `network.chat` (off there is off for them too). `false`: OpenCE players get no chat here. |
| `network.chat_filter` | `true` | `HALO_NET_CHAT_FILTER` | In the games this copy hosts, a line with a blocked word (the blocked names list's) is not passed on, and its sender is told. Nobody is kicked. |
| `network.share_profile` | `false` | `HALO_NET_SHARE_PROFILE` | `true`: the other ChupathingyCE players of a game see this copy's player ID (its game list profile), over Delta. |
| `network.platform_limits` | `"on"` | `HALO_NET_PLATFORM_LIMITS` | `"on"`: Delta hosts keep a game to the players this platform takes (an original Xbox: 16). `"off"`: this machine joins games of any size. |
| `network.host_platform_limits` | `true` | `HALO_NET_HOST_PLATFORM_LIMITS` | `true`: a game this machine hosts keeps to the players its Delta machines' platforms take. `false`: their limits are ignored, for testing. |
| `network.public_lobby` | `true` | `HALO_NET_PUBLIC_LOBBY` | `true`: the server browser. Public games are listed, and Join Game > Server Browser shows them. `false`: no games are listed or shown. Refer to "Server browser". |
| `network.ping_games` | `true` | `HALO_NET_PING_GAMES` | `true`: Online Games pings the listed games whose hosts answer pings, before joining (the host sees your public address, as when you join). `false`: no pings; only the estimated ones (`~85`). Refer to docs/delta.md, "Pings before joining". |
| `network.host_public` | `true` | `HALO_NET_HOST_PUBLIC` | `true`: a new game of Create Game > Internet starts as PUBLIC. `false`: it starts as PRIVATE. LISTING in Server Setup changes it for each game. Refer to "Server browser". |
| `network.coop_friendly_fire` | `"on"` | `HALO_NET_COOP_FRIENDLY_FIRE` | Whether the players of an online co-op game hurt each other: `"off"`, `"on"`, `"shields_only"` or `"explosives_only"`. FRIENDLY FIRE in co-op's Server Setup writes its choice here. Their AI allies they always can, as in the campaign. |
| `network.block_mouse_aim_assist` | `false` | `HALO_NET_BLOCK_MOUSE_AIM_ASSIST` | The host's policy on aiming assist for the mouse and keyboard in a game this machine hosts. `false`: each player's own `input.mouse_aim_assist` decides. `true`: the view's magnetism is off for a player aiming with the mouse, whatever their setting, in the game; the players' own settings are never changed. Clients are told when they join and when it changes (network version 32); a client honors the policy of any host that sends one, and, until a host of version 32 or later speaks, treats the mouse's magnetism as blocked. The bullets' autoaim is unaffected. |
| `network.coop_player_collisions` | `true` | `HALO_NET_COOP_PLAYER_COLLISIONS` | Whether the players of an online co-op game bump into each other. `false`: they walk through each other, so that one cannot block a doorway or stand on another; they still bump into the AI's characters. PLAYER COLLISIONS in co-op's Server Setup writes its choice here. |
| `network.coop_enemies_mode` | `"per_player"` | `HALO_NET_COOP_ENEMIES_MODE` | Online co-op's extra enemies: `"none"`; `"per_player"`, each squad of enemies that a level places grows by `network.coop_enemies` for each player past the first; or `"multiplier"`, each squad is `network.coop_enemies_multiplier` times as large, for any number of players. The extra enemies stand around the squad's places, and those that a dropship has no seats for drop out of it after its passengers. EXTRA ENEMIES in co-op's Server Setup writes its choice here. |
| `network.coop_enemies` | `50` | `HALO_NET_COOP_ENEMIES` | The extra enemies per player, a percentage from `25` to `200`: for each player past the first, each squad of enemies gets this much of itself more (`100`: as many again, so four players meet four times the squad), up to 8 times the squad however many players there are. PER PLAYER in co-op's Server Setup writes its choice here. |
| `network.coop_enemies_multiplier` | `2` | `HALO_NET_COOP_ENEMIES_MULTIPLIER` | The static multiplier of the enemies, `2` to `32`: each squad of enemies is this many times as large. MULTIPLIER in co-op's Server Setup writes its choice here. |
| `network.coop_public` | `false` | `HALO_NET_COOP_PUBLIC` | `true`: an online co-op game (Create Game > Internet, a SINGLEPLAYER map) starts as PUBLIC. `false`: it starts as PRIVATE. LISTING in co-op's Server Setup writes its choice here. Refer to "Server browser". |
| `network.brokers_file` | `"brokers.txt"` | `HALO_NET_BROKERS_FILE` | The file of the public MQTT brokers that let the machines of an invite find each other, and that carry the listings of the server browser: next to `config.toml`, unless a full path. One `host:port` on each line, up to 4; `#` starts a comment. |
| `network.stun_servers` | Google and Cloudflare | `HALO_NET_STUN` | The public STUN servers (`host:port`, with commas between them) that give the internet address of a machine. |
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
| `debug.exit_after` | `0.0` | `HALO_EXIT_AFTER` | The game stops after this number of seconds. `0`: never. |
| `debug.screenshot_directory`, `debug.screenshot_every` | `""`, `0` | `HALO_SCREENSHOT_DIR`, `HALO_SCREENSHOT_EVERY` | The game writes each Nth frame to this folder as a BMP file. |
| `debug.screenshot_window` | `false` | `HALO_SCREENSHOT_WINDOW` | `true`: those screenshots are of the window as shown, with the overlay's screens (Online Games and others) on the frame. |
| `debug.hidden_window`, `debug.null_renderer` | `false` | `HALO_HIDDEN_WINDOW`, `HALO_NULL_RENDERER` | `true`: no visible window, or no graphics. |
| `debug.gpu_stats`, `debug.gpu_trace_frame`, `debug.gpu_trace_constants`, `debug.gpu_dump_shaders`, `debug.texture_dump_directory`, `debug.texture_log`, `debug.gl_debug`, `debug.texture_no_cache` | off | `HALO_GPU_STATS`, `HALO_GPU_TRACE`, `HALO_GPU_TRACE_CONSTANTS`, `HALO_GPU_DUMP_SHADERS`, `HALO_TEXTURE_DUMP`, `HALO_TEXTURE_LOG`, `HALO_GL_DEBUG`, `HALO_TEXTURE_NO_CACHE` | Tools to find problems in the graphics: counts for each frame, all the GL state of one frame, the GLSL code, the textures. |
| `debug.discord_log` | `false` | `HALO_DISCORD_LOG` | Log each activity sent to Discord, without the invite. |
| `debug.menu_open` | `""` | `HALO_MENU_OPEN` | Start on this screen of the menus (`main_menu/settings_select/...`, as `port/assets/menus` names it), a player profile being edited, to look at it. |
| `debug.gpu_skip_vertex_shaders`, `debug.gpu_debug_expression`, `debug.gpu_debug_flat`, `debug.gpu_debug_texture0` | off | `HALO_GPU_SKIP_VS`, `HALO_GPU_DEBUG_EXPR`, `HALO_GPU_DEBUG_FLAT`, `HALO_GPU_DEBUG_T0` | Tools to find problems in the graphics: skip the draws of a vertex shader, or replace the output of all pixel shaders with a GLSL expression (for example `t0.rgb`). |
| `debug.network_test`, `debug.network_test_start`, `debug.network_test_kill`, `debug.network_test_score`, `debug.network_test_shoot`, `debug.network_test_vehicle`, `debug.network_test_pickup`, `debug.network_test_pickup_weapon`, `debug.network_test_fall`, `debug.network_test_bomb_in_goal`, `debug.network_test_bomb_throw`, `debug.network_test_bomb_keep`, `debug.network_test_team_change`, `debug.network_test_pause`, `debug.network_test_local_players`, `debug.test_input` | off | `HALO_NETWORK_TEST`, `HALO_NETWORK_TEST_START`, `HALO_NETWORK_TEST_KILL`, `HALO_NETWORK_TEST_SCORE`, `HALO_NETWORK_TEST_SHOOT`, `HALO_NETWORK_TEST_VEHICLE`, `HALO_NETWORK_TEST_PICKUP`, `HALO_NETWORK_TEST_PICKUP_WEAPON`, `HALO_NETWORK_TEST_FALL`, `HALO_NETWORK_TEST_BOMB_IN_GOAL`, `HALO_NETWORK_TEST_BOMB_THROW`, `HALO_NETWORK_TEST_BOMB_KEEP`, `HALO_NETWORK_TEST_TEAM_CHANGE`, `HALO_NETWORK_TEST_PAUSE`, `HALO_NETWORK_TEST_LOCAL_PLAYERS`, `HALO_TEST_INPUT` | Automatic tests of system link (`game/network_test.c`). Refer to `NETCODE.md`. |
| `debug.chat_test` | `""` | `HALO_CHAT_TEST` | A script of text chat for the tests (`game/chat.c`): `"<seconds>:<what>[:<text>]"` steps separated by `;`, from when this copy is in a game: `all` and `team` say a line, `server` says one as the host's server, `mute` and `unmute` mute a machine as a moderator does (the host), `localmute` mutes one for this copy, `log` and `close` open and close the log, `type` shows a line being typed. |
| `debug.touch_targets` | `false` | `HALO_TOUCH_TARGETS` | Outlines the tap targets of the menus (item green, value blue, list slot yellow, legend button red, the band beside the slots of a list orange, keys of the on-screen keyboard white), marks where the last finger went down and the last tap landed for 3 seconds, and logs each tap with the target that it hit (for a value, also where it splits into previous and next): to judge the accuracy of touch. |
| `debug.solo_game` | `false` | `HALO_SOLO_GAME` | The older name of `game.start_with_one_player`: `true` turns it on too. |
| `debug.network_latency`, `debug.network_loss`, `debug.network_corrupt`, `debug.network_corrupt_stream`, `debug.network_corrupt_after` | `0` | `HALO_NETWORK_LATENCY`, `HALO_NETWORK_LOSS`, `HALO_NETWORK_CORRUPT`, `HALO_NETWORK_CORRUPT_STREAM`, `HALO_NETWORK_CORRUPT_AFTER` | The game holds all the data that it receives for this number of milliseconds, ignores this percentage of the datagrams, and damages this percentage of the datagrams it receives, and this percentage of its reads of streams, at random (bytes changed, cut short, stretched or replaced), from this many seconds after the start. Use the first two to test the netcode as on the internet, and the others to test that nothing another machine sends can crash the game (a damaged stream is closed, so a little goes a long way; a host's messages to its own client are damaged too, so start damaging once the game has started). |
| `debug.telnet_console`, `debug.telnet_console_port` | `false`, `2323` | `HALO_TELNET_CONSOLE`, `HALO_TELNET_CONSOLE_PORT` | The game listens on 127.0.0.1, on this port, for a script console (connect with telnet). The console has no password, so only this computer can reach it. |

With Mesa drivers, the game sends its GL calls through the GL thread of
Mesa. To stop this, set the environment variable `mesa_glthread=false`.

## Updates

The builds from GitHub Actions (refer to the main [README](../../README.md#download))
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
HALO_NET_ADDRESS=127.0.0.200 HALO_NET_BROADCAST=127.0.0.201,127.0.0.202 build/linux/halo
HALO_NET_ADDRESS=127.0.0.201 HALO_NET_BROADCAST=127.0.0.200 build/linux/halo
HALO_NET_ADDRESS=127.0.0.202 HALO_NET_BROADCAST=127.0.0.200 build/linux/halo
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
- Enter `halo <link>`.
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
GAME joins only with the correct password. The invite link of the game joins
it without the password. The host keeps the password only while the game
runs.

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

ChupathingyCE's own parts of the server browser:

- A game hosted from the Xbox menus (System Link, or Online Games) is an
  internet game too, and public as `network.host_public` says, so it shows
  in the Server Browser of OpenCE and ChupathingyCE. The dedicated server
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
  again at once. In co-op, `bringto` brings every player to the host.
  So that every player can be named, the host trims the spaces around a
  name and removes characters that draw as nothing. A
  letter with a mark is typed as the plain letter (`ban jose` for "José").
  A name with nothing left to type becomes "Player", and a name that another
  player already has gets a number ("Player 2"). The game refuses a profile
  name that is blank, and a multiplayer game refuses a profile whose name was
  made blank before this check.
- An invite operates while the copy of the game that made it operates.

### Connection

Each machine gets its public address from public STUN servers. Then the two
machines send packets to each other until the packets get through (UDP hole
punching). There is no relay.

Some networks give a different port for each destination (for example some
mobile and company networks). Two machines behind such networks cannot
connect. To connect, forward `network.tunnel_port` on the router of one of
the machines.

The game can ask the router to forward the port (UPnP,
`src/posix_upnp.c`, with `port/third_party/miniupnpc`):

- The host asks its router when a player uses its invite.
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
(HaloMD) and GBX (Halo PC retail). A lock marks a game with a password, as
its public listing says. The ping column shows the round trip to a host
that this machine is connected to (a game that you join), else nothing:
nothing measures a host before you join it. Sorted by ping, such games come
last. The details show the map's picture from the menus, or from Halo PC's
`ui.map`. Without either, a stock map's picture comes from the site
(stats.chupathingy.org/art/maps), downloaded in the background only while
`network.online` is on and kept in the save root's `map_art` folder. Any
other map gets a card with its name in its family's color. RB (Link Profile)
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
game with the key's link as its argument: `halo 'halo://key/<64 hexadecimal
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
renamed one) is refused, not copied again for ever. The `loading.tga` a map
pack may put in the maps folder is read only if it is an uncompressed 24-bit
picture of 320 by 240, the loading screen's texture.

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
| Sound | Xbox DirectSound on SDL3 audio (`src/dsound_sdl.c`): PCM and Xbox ADPCM, mixed at 48 kHz in stereo or 5.1, with volume, pitch, mix bins, distance, pan, occlusion and obstruction. There is no Doppler effect and no cones. On 5.1 the game gets the AC-3 speaker setting of the Xbox and sends its 2D sounds to the center and back speakers, 3D sounds pan across the five speakers, and the reverb plays from the front and back pairs. The game sends nothing to the LFE channel. |
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
