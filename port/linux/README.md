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

- Python and ninja.
- clang. The option `--linux-cc` of `configure.py` selects a different
  compiler.
- The 32-bit glibc development files: `lib32-glibc` on Arch Linux,
  `gcc-multilib` and `libc6-dev-i386` on Debian and Ubuntu.
- The 32-bit SDL3: `lib32-sdl3` on Arch Linux, `libsdl3-dev:i386` on Debian
  and Ubuntu.

To start the game:

- The 32-bit OpenGL libraries (`lib32-mesa`).
- The 32-bit PipeWire or PulseAudio client libraries (`lib32-pipewire` or
  `lib32-libpulse`).

## Build the game

1. Go to the root folder of the repository.
2. Enter `python configure.py`.
3. Enter `ninja linux`.

### 64-bit

`ninja linux64` builds the same game as native x86-64 code,
`build/linux64/halo`, the way the macOS build does (the 64-bit build in
[port/macos/README.md](../macos/README.md): `HALO_64BIT` and the `long`
rewrite, `tools/lp64_build.py`). It needs the 64-bit glibc and SDL3
development files (`sdl3` on Arch Linux, `libsdl3-dev` on Debian 13 and
Ubuntu 25.04 or later) instead of the 32-bit ones, and the 64-bit OpenGL and
sound libraries to start. An SDL3 built by hand is found through
`LIBRARY_PATH` when linking and `LD_LIBRARY_PATH` when starting. It is not
optimised with a profile (the committed profiles are the 32-bit build's).
Its releases are their own download, `chupathingyce-linux64-release.zip`,
which its self-updater asks for. It plays Halo PC's Custom Edition maps
(`maps/ce/`) and plays with the 32-bit builds and the other ports over the
network, and it is a dedicated server too (`server/README.md`).

## Start the game

Enter `build/linux/halo`.

The game data is the folder that contains `maps/`, from an Xbox disc image
of any version of the game. The game looks for this folder in this
sequence:

1. `paths.data` in `config.toml`.
2. The current folder.
3. The folder of the executable.
4. `assets/` in the current folder, and `assets/` in the repository that
   contains the executable.

If the game finds no data, it asks for an Xbox disc image (`.xiso` or
`.iso`). This occurs at the first start:

- Select "No" to stop the game.
- Select "Yes" to open a file picker. Select the disc image. The game copies
  `maps/` next to the executable and shows the progress.

The game writes the copy to `maps.partial`. When the copy is complete, the
game changes the name to `maps`. If the copy stops before it is complete,
the game asks for the disc image again at the next start.

## Files and folders

| Xbox drive | Folder |
| --- | --- |
| `d:\` | The data root: the folder that contains `maps/`. |
| `z:\` | `z/` in the save root. This folder contains the cache (approximately 800 MB of map data) and the saved games. |
| `u:\` | `u/` in the save root. This folder contains the user data. |

The save root is `paths.saves` in `config.toml`. If that setting is empty,
the save root is `$XDG_DATA_HOME/halo-linux` (usually
`~/.local/share/halo-linux`).

The game makes the folders when it needs them. Names of files and folders
are not case-sensitive, as on the Xbox.

These files are in the data root:

| File | Contents |
| --- | --- |
| `debug.txt` | The log of the game. At start-up, the game shows the data root in the terminal. A crash writes its report (the faulting address and the calls that led to it) here as well; the `reference address` line at the top of each session places those addresses in the build. |
| `init.txt` | Console commands that the game does at start-up. For example, `map_name levels\a10\a10` starts the first campaign level. |

The settings are in `config.toml` next to the executable. Refer to
"Settings". Internet play's MQTT brokers are in `brokers.txt` next to it
(`network.brokers_file`).

If the game stops because of a fatal signal, it writes the address and a
backtrace to the standard error. To find the function at the address, enter
`addr2line -e build/linux/halo <address>`.

## Controls

The keyboard and the mouse are a control scheme of their own for the player
of controller 1: each action has up to two keys or mouse buttons, which
Settings > Controls Setup (or `[controls]` in `config.toml`) changes. The
game adds the input of the first gamepad to controller 1. The other
gamepads operate controllers 2 to 4. With two or more players on this
computer (co-op, or split screen in a network game) and only one gamepad,
that gamepad is controller 2 (player 2) and the keyboard and mouse stay
controller 1. The gamepad changes controller only when none of its buttons
is held. The profile's button layout (Settings > Gamepads) is the
gamepads' only.

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
version's. ChupathingyCE starts on the Xbox's menus (`display.menus = "xbox"`),
which start every kind of game and have Online Games; `display.menus = "pc"`
gives these.

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

In a multiplayer game, the pause menu (escape) has SETTINGS, which opens
the profile's settings while the game goes on, and for the host END GAME.
END GAME ends the game as its time limit does: the players stay, and after
the carnage report the host picks the next map and gametype (PICK GAME).
LEAVE GAME of the host still ends the game for everyone. The buttons go into
the map's own pause menu, before LEAVE GAME, so the buttons of a custom map
stay. The Xbox's pause box is drawn taller to hold them (a redraw,
`port/assets/menus/port_svg/pause`); a custom map's box keeps its size, and
what is below its list moves down. The few pictures of the settings that
come from the main menu's map are not drawn there.

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
# The menus: "xbox" for the Xbox's (with Online Games), "pc" for the
# ...
# menus = "xbox"
```

A setting that is commented out follows the default of the version that
runs, so a new version with a different default changes it. To choose a
value, remove the `# ` at the start of the line and change the value. Only
the settings that you (or the Settings menu) change are lines without `#`;
those stay as they are, also if a later version changes the default. To get
the default values again, delete the file.

The game reads the file at start-up. If a key is not correct, or a value
has the wrong type, the game writes the line to the log and uses the default
value. The Settings menu (Video, Mouse, Audio, Network and Controls Setup)
changes the useful settings, writes them into the file (only their lines
change: the line of a setting at its default loses its `#`) and applies
them at once, but `audio.enabled`, and `display.menus` from the next main
menu. A new version adds its new settings to the file, commented out.

Earlier versions wrote every setting as a value. The first start of this
version updates such a file once (`config_version = 2` at the top
marks it): a setting that holds the default of this version, or the
default of an earlier version (`display.menus = "pc"`, the default before
0.5.2b), is commented out, so that it follows the default. A setting with
another value stays. The log has one line that tells what changed.

Each setting has an environment variable. The environment variable changes
the setting for one start of the game. It has priority over the file.

| Setting | Default | Environment variable | Function |
| --- | --- | --- | --- |
| `display.mode` | `""` | `HALO_DISPLAY_MODE` | `"fullscreen"`: the display, taken at the mode of `display.resolution` (the nearest the display has), or at its desktop mode. `"borderless"`: a window over the whole desktop, whose mode does not change. `"windowed"`: a window of `display.window_size`. Empty: `display.fullscreen` decides (`true`: borderless). F11 changes between the window and the fullscreen mode. Video Setup sets it. |
| `display.fullscreen` | `true` | `HALO_FULLSCREEN` | `true`: borderless, as `display.mode = "borderless"`. `false`: a window, as `display.mode = "windowed"`. Used when `display.mode` is empty. |
| `display.resolution` | `"native"` | `HALO_RESOLUTION` | What fullscreen and borderless draw at: `"native"`, the display's own resolution, or `"<width>x<height>"`, such as `"1920x1080"`, 640x480 or more. Fullscreen sets the display to it. Borderless draws at it and scales the picture to the display, where the display has room for it. The picture has 480 lines of the game and the width of the resolution's shape. A window draws at its own size instead. Video Setup's Resolution sets it, from the display's modes; it shows with Fullscreen and Borderless. |
| `display.resolution_scaling` | `"native"` | `HALO_RESOLUTION_SCALING` | `"native"`: the game draws at the resolution of the window, or of the display (or `display.resolution`) fullscreen. `"original"`: the game draws the 640x480 picture of the Xbox and scales it up to the window or the display, whatever `display.mode` is. Video Setup sets it. |
| `display.window_size` | `""` | `HALO_WINDOW_SIZE` | The size of the window, as `"<width>x<height>"`, such as `"1920x1080"`, 640x480 or more. You can change the size of the window; the game's picture takes its shape. Empty: `display.window_scale` decides. Video Setup's Window Size sets it, from sizes of each shape (4:3, 16:10, 16:9 and 21:9) that fit the desktop; it shows with Windowed. |
| `display.window_scale` | `2` | `HALO_WINDOW_SCALE` | Used when `display.window_size` is empty: the size of the window, as a multiple of 640x480. |
| `display.vsync` | `true` | `HALO_NO_VSYNC=1` sets `false` | `true`: each frame waits for the display. |
| `display.max_fps` | `0` | `HALO_MAX_FPS` | With vsync off, the most frames each second. `0`: twice the display's refresh rate. `-1`: no limit, which can hang some Intel graphics (Raptor Lake), resetting the desktop's graphics too. |
| `debug.gpu_flush_draws` | `-1` | `HALO_GPU_FLUSH_DRAWS` | Flush the GPU's pipeline every this many draws. `-1`: every 3 on Intel graphics with Mesa's driver, which can otherwise hang in the game's long runs of small draws and reset the desktop's graphics too. `0`: never. |
| `display.interpolation` | `true` | `HALO_INTERPOLATION` | `true`: one frame for each refresh of the display. `false`: 30 frames each second, as on the Xbox. Refer to "Frame rate". |
| `display.direct_camera` | `true` | `HALO_DIRECT_CAMERA` | `true`: in first person, on foot, the view points where the player aims in each frame, not where the last tick left it. Refer to "Frame rate". |
| `display.high_res_hud` | `true` | `HALO_HIGH_RES_HUD` | `true`: the HUD (meters, counters, panels and their outlines, the motion sensor, reticles, waypoints, scopes) is drawn from the high-res assets in `port/assets/hud`, 8x the size of the maps' bitmaps. The bitmaps with English text keep the maps' own. `false`: the maps' own bitmaps. |
| `display.high_res_text` | `true` | `HALO_HIGH_RES_TEXT` | `true`: the menus' and HUD's text is drawn with the fonts in `port/assets/fonts` (Overpass, in place of the maps' Interstate) at the resolution the game draws at, laid out as before, and the menus' titles are drawn from the high-res pictures in `port/assets/titles`. `false`: the maps' bitmap fonts and titles. |
| `display.menus` | `"xbox"` | `HALO_MENUS` | `"xbox"`: the Xbox's menus, with Online Games (ChupathingyCE's default). `"pc"`: the PC version's menus, from the files in `port/assets/menus` and a `menus` folder next to `config.toml`. Refer to "Menus". |
| `display.player_names` | `"all"` | `HALO_PLAYER_NAMES` | In multiplayer, whose names are drawn above their heads: `"all"`, `"allies"`, `"enemies"` or `"none"`. An ally's name is drawn above the triangle the game shows over teammates. An enemy's name shows only within the motion sensor's reach, while the enemy is in sight and not camouflaged, so it never shows where an enemy hides. The gametype's motion tracker setting also applies: no names if it shows no players, only allies' if it shows only friends. |
| `display.player_name_scale` | `1.0` | `HALO_PLAYER_NAME_SCALE` | How large the players' names are drawn: `1.0` is three quarters of the size of the HUD's text, from `0.25` to `4`. With high-res text, larger names are rasterized at their size, so they stay sharp. |
| `display.scoreboard_team_layout` | `"teams"` | `HALO_SCOREBOARD_TEAM_LAYOUT` | How the multiplayer scoreboard (hold BACK, or tab) lists a team game's players. `"teams"`: a column for each team, red on the left and blue on the right. `"score"`: all the players in order of score. With more players than fit, the mouse wheel and Page Up / Page Down scroll the scoreboard. |
| `display.scoreboard_background` | `true` | `HALO_SCOREBOARD_BACKGROUND` | `true`: the multiplayer scoreboard (hold BACK, or tab) has a panel behind its text, for clearer text. |
| `display.show_quit_players` | `true` | `HALO_SHOW_QUIT_PLAYERS` | `true`: players who quit stay on the multiplayer scoreboard (hold BACK, or tab) and the score in the corner, as in the original game. `false`: they are left off, and the players still in the game are ranked among themselves, as OpenCE does. Only what this machine draws changes. |
| `display.scoreboard_background_color` | `"16, 16, 16, 150"` | `HALO_SCOREBOARD_BACKGROUND_COLOR` | The colour of the scoreboard's panel: `"red, green, blue, alpha"`, each from `0` to `255`. Alpha `0` is see-through, `255` is solid. |
| `audio.enabled` | `true` | `HALO_NO_AUDIO=1` sets `false` | `false`: no audio device. The sound continues without output. |
| `audio.volume` | `1.0` | `HALO_VOLUME` | The master volume. |
| `audio.music_volume` | `1.0` | `HALO_MUSIC_VOLUME` | The music's volume, of the master volume. |
| `audio.effects_volume` | `1.0` | `HALO_EFFECTS_VOLUME` | The volume of the other sounds (effects and speech), of the master volume. |
| `audio.buffer_frames` | `2048` on macOS, `512` elsewhere | `HALO_AUDIO_BUFFER_FRAMES` | The audio device's buffer, in sample frames at 48 kHz, from `64` to `8192`. Larger rides out stalls that would cut the sound out; smaller has less delay (512 is 11 ms, 2048 is 43 ms). |
| `input.mouse_sensitivity` | `1.0` | `HALO_MOUSE_SENSITIVITY` | The multiplier for the mouse aim. |
| `input.mouse_vertical_sensitivity` | `0.0` | `HALO_MOUSE_VERTICAL_SENSITIVITY` | The multiplier for the vertical mouse aim. `0`: the same as `input.mouse_sensitivity`. |
| `input.invert_mouse` | `false` | `HALO_MOUSE_INVERT=1` sets `true` | `true`: the vertical mouse aim is inverted. |
| `input.mouse_aim_assist` | `false` | `HALO_MOUSE_AIM_ASSIST` | `true`: the magnetism of the controller also operates for the mouse. `false`: when the mouse moved after the right stick, the view is not slowed or dragged by a target. The autoaim of the bullets operates in both cases. |
| `controls.<action>` | (the table in "Controls") | `HALO_KEY_<ACTION>` | The keys and mouse buttons of an action, up to two, separated by a comma: `move_forward`, `move_backward`, `strafe_left`, `strafe_right`, `jump`, `crouch`, `fire`, `throw_grenade`, `melee`, `reload`, `zoom`, `switch_weapon`, `switch_grenade`, `action`, `flashlight`, `scoreboard`, `pause`. Keys by their names (`"W"`, `"Space"`, `"Left Ctrl"`, `"F1"`), and `"Mouse Left"`, `"Mouse Right"`, `"Mouse Middle"`, `"Mouse 4"`, `"Mouse 5"`, `"Wheel"` (either way), `"Wheel Up"`, `"Wheel Down"`. |
| `game.console_log` | `"important"` | `HALO_CONSOLE_LOG` | What the console shows on the screen. `"important"`: bans, players that the host drops for cheating, the reasons that the game refuses a command, and the asserts that stop the game. `"all"`: all the lines. `"none"`: only the asserts that stop the game. The output of a command always shows. `debug.txt` gets all the lines. |
| `game.language` | `""` | `HALO_LANGUAGE` | The language of the menus: `ja`, `de`, `fr`, `es` or `it`. Empty: English. |
| `paths.data` | `""` | `HALO_DATA_ROOT` | The data root. Refer to "Start the game". |
| `paths.saves` | `""` | `HALO_SAVE_ROOT` | The save root. Refer to "Files and folders". |
| `network.address` | `""` | `HALO_NET_ADDRESS` | The IPv4 address of this machine for system link. Refer to "Play on one computer". |
| `network.broadcast` | `""` | `HALO_NET_BROADCAST` | IPv4 addresses, with commas between them, that get the broadcasts of the game. Empty: 255.255.255.255. |
| `network.online` | `true` | `HALO_NET_ONLINE` | `true`: internet play. `false`: system link on the local network only. |
| `network.join_from_clipboard` | `true` | `HALO_NET_JOIN_FROM_CLIPBOARD` | `true`: when the game comes to the front, it joins the game of an invite link on the clipboard. |
| `network.tunnel_port` | `0` | `HALO_NET_TUNNEL_PORT` | The UDP port for internet play. `0`: the game selects a port. Refer to "Internet play". |
| `network.allow_upnp` | `true` | `HALO_NET_ALLOW_UPNP` | `true`: internet play can ask the router to forward its port (UPnP). `false`: the game does not ask. Refer to "Internet play". |
| `network.public_lobby` | `true` | `HALO_NET_PUBLIC_LOBBY` | `true`: the server browser. Public games are listed, and Join Game > Server Browser shows them. `false`: no games are listed or shown. Refer to "Server browser". |
| `network.host_public` | `true` | `HALO_NET_HOST_PUBLIC` | `true`: a new game of Create Game > Internet starts as PUBLIC. `false`: it starts as PRIVATE. LISTING in Server Setup changes it for each game. Refer to "Server browser". |
| `network.brokers_file` | `"brokers.txt"` | `HALO_NET_BROKERS_FILE` | The file of the public MQTT brokers that let the machines of an invite find each other, and that carry the listings of the server browser: next to `config.toml`, unless a full path. One `host:port` on each line, up to 4; `#` starts a comment. |
| `network.stun_servers` | Google and Cloudflare | `HALO_NET_STUN` | The public STUN servers (`host:port`, with commas between them) that give the internet address of a machine. |
| `bots.count` | `0` | `HALO_BOTS` | The number of bots in the multiplayer games that this machine hosts. `0`: no bots. Refer to "Bots". |
| `bots.fill` | `0` | `HALO_BOTS_FILL` | The games that this machine hosts have this number of players: bots fill the places that people do not have. A bot goes when a person joins and comes back when a person goes. `0`: `bots.count` bots. Refer to "Bots". |
| `bots.skill` | `3` | `HALO_BOT_SKILL` | How good the bots are: `1` easy, `2` normal, `3` hard, `4` legendary, `5` near perfect. |
| `infection.rounds`, `infection.round_minutes`, `infection.countdown` | `3`, `4`, `10` | `HALO_INFECTION_ROUNDS`, `HALO_INFECTION_ROUND_MINUTES`, `HALO_INFECTION_COUNTDOWN` | The rounds of a game of infection, the minutes of a round (`0`: no limit) and the seconds before the first zombie. Refer to "Infection". |
| `infection.zombie_speed`, `infection.zombie_damage`, `infection.zombie_health` | `1.3`, `8.0`, `1.5` | `HALO_INFECTION_ZOMBIE_SPEED`, `HALO_INFECTION_ZOMBIE_DAMAGE`, `HALO_INFECTION_ZOMBIE_HEALTH` | How fast a zombie moves (a player is `1`), how hard it hits and how much damage it can take (the Flood of the campaign is `1`). |
| `discord.application_id` | the application of the project | `HALO_DISCORD_APPLICATION` | The Discord application for invites. Empty: no Discord. |
| `update.auto` | `true` | `HALO_UPDATE_AUTO` | `true`: at start-up, the game looks for a new version. Refer to "Updates". `false`: the game does not look. |
| `debug.update_answer` | `""` | `HALO_UPDATE_ANSWER` | The answer to the update question, for automatic tests: `yes`, `no` or `never`. Empty: the game asks. |
| `debug.random_seed` | `0` | `HALO_RANDOM_SEED` | The random seed of the games that this machine hosts, and of their bots. With the same seed, a game with only bots is the same each time. Use this setting to compare two builds. `0`: a new seed for each game. |
| `debug.exit_after` | `0.0` | `HALO_EXIT_AFTER` | The game stops after this number of seconds. `0`: never. |
| `debug.screenshot_directory`, `debug.screenshot_every` | `""`, `0` | `HALO_SCREENSHOT_DIR`, `HALO_SCREENSHOT_EVERY` | The game writes each Nth frame to this folder as a BMP file. |
| `debug.hidden_window`, `debug.null_renderer` | `false` | `HALO_HIDDEN_WINDOW`, `HALO_NULL_RENDERER` | `true`: no visible window, or no graphics. |
| `debug.gpu_stats`, `debug.gpu_trace_frame`, `debug.gpu_trace_constants`, `debug.gpu_dump_shaders`, `debug.texture_dump_directory`, `debug.texture_log`, `debug.gl_debug`, `debug.texture_no_cache` | off | `HALO_GPU_STATS`, `HALO_GPU_TRACE`, `HALO_GPU_TRACE_CONSTANTS`, `HALO_GPU_DUMP_SHADERS`, `HALO_TEXTURE_DUMP`, `HALO_TEXTURE_LOG`, `HALO_GL_DEBUG`, `HALO_TEXTURE_NO_CACHE` | Tools to find problems in the graphics: counts for each frame, all the GL state of one frame, the GLSL code, the textures. |
| `debug.menu_open` | `""` | `HALO_MENU_OPEN` | Start on this screen of the menus (`main_menu/settings_select/...`, as `port/assets/menus` names it), a player profile being edited, to look at it. |
| `debug.gpu_skip_vertex_shaders`, `debug.gpu_debug_expression`, `debug.gpu_debug_flat`, `debug.gpu_debug_texture0` | off | `HALO_GPU_SKIP_VS`, `HALO_GPU_DEBUG_EXPR`, `HALO_GPU_DEBUG_FLAT`, `HALO_GPU_DEBUG_T0` | Tools to find problems in the graphics: skip the draws of a vertex shader, or replace the output of all pixel shaders with a GLSL expression (for example `t0.rgb`). |
| `debug.network_test`, `debug.network_test_start`, `debug.network_test_kill`, `debug.network_test_score`, `debug.network_test_shoot`, `debug.network_test_vehicle`, `debug.network_test_pickup`, `debug.network_test_pickup_weapon`, `debug.test_input` | off | `HALO_NETWORK_TEST`, `HALO_NETWORK_TEST_START`, `HALO_NETWORK_TEST_KILL`, `HALO_NETWORK_TEST_SCORE`, `HALO_NETWORK_TEST_SHOOT`, `HALO_NETWORK_TEST_VEHICLE`, `HALO_NETWORK_TEST_PICKUP`, `HALO_NETWORK_TEST_PICKUP_WEAPON`, `HALO_TEST_INPUT` | Automatic tests of system link (`game/network_test.c`). Refer to `NETCODE.md`. |
| `debug.touch_targets` | `false` | `HALO_TOUCH_TARGETS` | Outlines the tap targets of the menus (item green, value blue, list slot yellow, legend button red, the band beside the slots of a list orange, keys of the on-screen keyboard white), marks where the last finger went down and the last tap landed for 3 seconds, and logs each tap with the target that it hit (for a value, also where it splits into previous and next): to judge the accuracy of touch. |
| `debug.network_latency`, `debug.network_loss` | `0` | `HALO_NETWORK_LATENCY`, `HALO_NETWORK_LOSS` | The game holds all the data that it receives for this number of milliseconds, and ignores this percentage of the datagrams. Use these settings to test the netcode as on the internet. |
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

The game downloads through HTTPS. It examines the certificate of the server
against the certificate authorities of the system: on Linux, the bundle of
the distribution (`src/posix_update.c`, with Mbed TLS); on Windows, the
certificate store of Windows (WinHTTP). The folder of the executable must
let the game write to it.

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
to the standard error and puts it on the clipboard. The links of older
versions of the game (44 digits) do not operate. The game writes a message
when it gets one.

To join a game, do one of these steps:

- Open the link. The game is the handler of `halo://` links. If the game
  already operates, the new copy gives the link to it and stops. A key in a
  file that only the user can read (`halo-ce-universal.key` in
  `$XDG_RUNTIME_DIR`, else `~/.halo-ce-universal.key`; on Windows in
  `%LOCALAPPDATA%`) encrypts the link, so the programs of other users cannot
  read it.
- Copy the link (or the 64 digits) and go to the game.
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
games start as PRIVATE). A LAN game is never listed. `network.public_lobby = false` turns the server browser off.

In the Server Browser, select a game to join it. The game joins the invite
of the game, as for a link. When it reaches the host, it opens the lobby.
If it cannot reach the host in 30 seconds, it marks the game FAILED. REFRESH
asks the hosts for their listings again.

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
  games of `network.browser_url` (halo.milenko.org). While the Server
  Browser is open, the game reads the list with an HTTPS GET of
  `/v1/games.txt` (at most every 5 seconds), and shows the games of its
  network version, not its own. A game that is also listed on the brokers
  shows once, with its listing (the same invite token). Joining a game of
  the list joins its invite, as for a link.
- A game on a Halo PC (Custom Edition) map, listed as `<map>@ce`, shows PC
  after the map's name. It can be joined only with the map in `maps/ce/`,
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
- The host drops a player whose game runs faster than time (a speed hack)
  for ten seconds, and keeps that address out of its games. Each player
  sees who in red on the console. The host adds a line to `cheaters.txt`
  (beside `debug.txt`) with the address and hardware id of the player, and
  the Discord name and id that the game of the player told it (a player can
  change these). The host also bans the player: it adds the line to
  `bans.txt`, and refuses a machine whose address or hardware id is in it.
- The host can ban a player with `ban <player name>` in the developer
  console (Tab completes the name). Remove a line from `bans.txt` to unban.
  Refer to `NETCODE.md`. So that every player can be named, the host trims
  the spaces around a name and removes characters that draw as nothing. A
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

If the Discord desktop client operates, the game of the host shows in
Discord (through the application of `discord.application_id`). The activity
has a private party with the invite as its join secret. The host can send
the invite with the invite button of Discord. When a person accepts it, that
person joins the game. If the game does not operate, Discord starts it.
The game sends the activity only to a Discord client of the same user.

### Online Games and the profile

Builds with the game list (`configure.py --game-browser`) have Online Games
in the Multiplayer menu: the games on `network.browser_url`
(halo.milenko.org). Each copy of the game has a player key in the save root
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

- `bots.count` in `config.toml` gives the number of bots (`0` by default:
  no bots). `bots.skill` gives how good they are, from `1` to `5`.
- In the console, `bots <number>` and `bot_skill <1-5>` change these values
  until the game stops. A new number of bots applies in the lobby. A game in
  progress keeps its bots.
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
on Chill Out (8 bots, skill 3), the middle time from in view to seen, for
these enemies, went from 0.23 to 0.33 seconds; the shortest stayed at 0.1
seconds. The number of kills did not change (91 and 91).
`HALO_BOT_OFF=fresh` turns the added time off, and `HALO_BOT_NOTICELOG=1`
writes each time to the log.

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
ground vehicles and on foot. It operates only on a map whose navigation is
made in parts. On all other maps the game does not make it and does not use
it: three games of 4 minutes (Blood Gulch and Sidewinder, capture the flag,
11 bots; Hang 'Em High, slayer, 7 bots) had the same deaths with it, tick
for tick, as before it.

- **The ground.** The game makes a map of squares of 2 world units. A
  square has the floors that are below its middle: a floor is a surface of
  45 degrees or less that has 1.6 world units of room above it.
- **The steps.** A vehicle can go from a floor to a floor of the next
  square when the step is not steeper than 40 degrees. If the height of
  the step agrees with the slopes of the two floors, the game accepts it.
  If not, or if the floor has a side (a ramp, a bridge, a road along a
  cliff), the game looks at the ground with rays: the ground must not jump
  more than 1 world unit, and it must be 2 world units wide. After a jump
  of more than 0.6 world units, the ground must continue at its new height
  for 2.5 world units. Thus a Warthog goes up a step onto a platform, but
  not onto the top of a low wall, and not into a rut.
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
the size of the map of the ground, each gate, each search, and where each
driver is every 3 seconds. `HALO_BOT_COUNTRYTRACE=1` adds a line for each
driver every third of a second.

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
  to 9 world units a second, until it is out.
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

## What operates

| Area | Status |
| --- | --- |
| Game code | All 466 C files of the game. The changes are in "Game source changes". |
| Graphics | Direct3D 8 on OpenGL 4.5 core through SDL3 (`src/d3d8_gl.c`). The port translates the NV2A vertex shaders and register combiners to GLSL. It decodes all the Xbox texture formats. The vertex and index buffers come from a GL copy of the Xbox memory. |
| High-res HUD | The HUD is drawn from high-res assets: redraws at 8x the size of the maps' bitmaps (4x for the largest), in `port/assets/hud`. They cover the meters, counters, panels and their outlines, the motion sensor, reticles, waypoints and scopes, but no bitmap with English text. `tools/hud_assets.py` makes them from the SVG redraws, and the build embeds them in the executable. When the game uploads one of those bitmaps, `src/hud_hires.c` gives the high-res texture in its place, if the bitmap's pixels are those of the English maps: another language's maps keep their own. The game sizes and places the HUD from its tags as before. `display.high_res_hud = false` turns this off. |
| High-res text | The menus' and HUD's text is drawn with Overpass (`port/assets/fonts`, SIL Open Font License) in place of the maps' bitmap fonts, which are Interstate. `src/text_hires.c` rasterizes each glyph with stb_truetype (`port/third_party/stb`) at the resolution the game draws at, into an atlas that a placeholder bitmap of the game stands for. The game lays the text out from its font tags as before. The menus' titles (the screens' headers and the main menu's items) are pictures of text in the maps, so they are drawn as the high-res HUD is: `tools/title_assets.py` sets each one again in OpenCE, Roger White's public-domain Newtown respaced to match the maps' commercial title typeface (`tools/title_font.py`), at 4x the bitmap's size over its own plate or glow, each letter placed where the map's letter is, in `port/assets/titles`. The postgame carnage report's title is set over a hand-made SVG redraw of its panel (`port/assets/titles/svg`) instead. `display.high_res_text = false` turns it off. |
| Sound | Xbox DirectSound on SDL3 audio (`src/dsound_sdl.c`): PCM and Xbox ADPCM, mixed at 48 kHz, with volume, pitch, mix bins, distance, stereo pan, occlusion and obstruction. There is no Doppler effect, no cones and no reverb. |
| Input | XInput on SDL3 (`src/xinput_sdl.c`): keyboard, mouse, gamepads with rumble, and the debug keyboard for the console. |
| Files | The Win32 file functions and the MSVC file functions on POSIX, with the translation of Xbox paths. |
| Threads | Threads, events, mutexes, critical sections, interlocked operations and alertable waits. |
| Memory | The port reserves the Xbox memory at `0x80000000`. Thus the game gets the fixed addresses that it expects. |
| Saved games | The Xbox `UDATA` layout, with SHA-1 signatures. |
| Networking | Winsock on BSD sockets. System link on a local network and on the internet. |
| Bink video | Not available. The game skips the movies. |

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
| `include/halo_linux_source_fixups.h` | Repairs one declaration conflict (`rasterizer_debug_drawing_begin`). |

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
| `cseries/errors.c` | `debug.txt` stays open between lines. |
| `networking/`, `game/`, `interface/`, `bungie_net/network/` and the pools of objects, effects and sounds | The system link limits and the memory for them. |
| `game/`, `objects/`, `units/`, `networking/` | The distributed netcode. Refer to `NETCODE.md`. |
| `cache/cache_files.c` | When a map's tags load and unload, the port finds the bitmaps that the high-res HUD replaces (`game/hud_hires_tags.c`), and adds the tags of the menus to the menus' map (`game/menu_tags.c`). |
| `interface/ui_widget.c`, `interface/ui_widget_event_handler_functions.c`, `interface/ui_widget_game_data_input_functions.c` | The main menu is the PC version's from `port/assets/menus` (`display.menus`); the menus' widgets can call the port's functions (`game/menu_functions.c`) and send the PC version's custom activation event; the widgets' memory is 256 KB, not 16 KB; the main menu and Multiplayer clear co-op's controllers, so that a gamepad going back to controller 1 is not a controller unplugged; the lobby's split screen players leave alone, and one who quits in game is not joined to the next game (`interface/player_ui.c`). |
| `input/input_abstraction.c`, `game/player_control.c`, `game/players.c`, `game/player_queues_new.c`, `units/units.h` | The keyboard and mouse's actions (`src/xinput_sdl.c`, `include/halo_keyboard.h`) join controller 1's game controls; their reload key reloads on its own, and their action key only acts (a control flag of the port's, sent with the player's action, stops the reload the controller's X falls back to). |
| `sound/sound_manager.c`, `interface/hud.c`, `game/game_engine.c` | The music's and the other sounds' volumes; the HUD's and the scoreboard's settings are read again when Settings changes them. |
| `interface/hud.c` | In multiplayer, players' names are drawn above their heads (`display.player_names`, `display.player_name_scale`). |
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
