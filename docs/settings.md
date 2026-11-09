# Settings (config.toml)

Most of what you can change lives in the game's menus, but everything the
port adds is in one file, `config.toml`. The game writes it the first time it
starts, with every setting listed, commented out at its default, and a line
or two saying what each one does. To change one, remove the `#` in front of it
and edit the value; the game reads the file when it starts, so restart it
after a change. A line that starts with `#` does nothing. If a change seems
to do nothing, look in `logs/debug.txt` for lines starting `config.toml` or
`settings:`: they name a misspelled setting, one in the wrong `[section]`, a
line still commented out, a line the game skipped because of a mistake (a
value missing its quotes, `menus = pc` for `menus = "pc"`, or `False` for
`false`; the other lines still count), a word a setting does not know, or a
`config.toml` that is not the one the game reads (such as `config.toml.txt`
on Windows).

| Platform | config.toml is |
| --- | --- |
| Windows | next to `chupathingy.exe` |
| Mac | `~/Library/Application Support/ChupathingyCE/config.toml` |
| Linux | next to the `chupathingy` program; with the AppImage or a packaged install, `~/.local/share/chupathingyce/config.toml` |
| Android | in the game's data folder, `/sdcard/Android/data/dev.horrible.chupathingyce/files` |

A few people look for most:

| Setting | What it does |
| --- | --- |
| `display.mode` | `"fullscreen"`, `"borderless"` or `"windowed"`. F11 switches between a window and the whole screen. When it is set, `display.fullscreen` is ignored; when it is empty, `display.fullscreen = true` is borderless and `false` a window. |
| `display.window_scale` | The window's size, as a multiple of 640x480. |
| `display.vsync`, `display.max_fps` | Vertical sync, and a frame rate cap with V-Sync off (0 for twice the display's refresh rate, -1 for none). |
| `display.interpolation` | `false` keeps the game's own 30 frames a second; Video Setup's Frame Rate at 30 (Xbox) sets it. |
| `display.direct_camera` | Instant aim: the view turns the frame the mouse, stick or touch moves it. On by default; config.toml only. |
| `display.menus` | `"chupathingy"` (the default: the Xbox's look with Halo PC's settings, profiles and more), `"xbox"` (as close to the console as can be) or `"gearbox"` (Halo PC's menus with their Server Browser; `"pc"` still works). |
| `display.high_res_text` | `true` draws the game's text (menus and HUD) with sharp fonts at your resolution; `false` uses the maps' original bitmap fonts. The Server Browser and the moderation overlays use the system's fonts either way. For a blocky look overall, also set `display.resolution_scaling = "original"`. |
| `display.high_res_font`, `display.high_res_font_large` | Your own font file for the sharp text and Online Games, in place of the built-in fonts: put it in `data/fonts` beside `maps` and give its name (`high_res_font = "MyFont.ttf"`), or a full path. `_large` sets one for the large text only. Empty uses the built-in fonts; a file that can't be read is noted in `logs/debug.txt` and the built-in font is used. The font is yours: Chupathingy ships none but its own free ones, and never sends yours anywhere. |
| `display.field_of_view` | How wide the first-person view is, in degrees: 70 (the Xbox's) by default, 50 to 120. |
| `display.far_clip_distance` | How far the world is drawn: 1024 (the Xbox's) by default, up to 10000, for large maps that cut off in the distance. Drawing only. |
| `display.match_timer` | `true` shows a timed multiplayer game's time left at the top of the screen (off by default). |
| `display.player_names` | Names over players' heads: `"all"`, `"allies"`, `"enemies"` or `"none"` (the default, as on the Xbox). Only where the host allows them (`network.allow_player_names`). |
| `display.follow_camera` | After you die, watch a living player's view: `"off"` (the default, as on the Xbox), `"long_waits"` (only during a long respawn wait) or `"always"`. LB and RB, or Q and E, switch players. Only where the host allows it (`network.allow_follow_camera`). |
| `display.xbox_loading_picture` | `true` (the default) shows the Xbox's own loading picture, `xbox_loading.tga` in your maps folder, and Chupathingy's when that file isn't there. `false` always shows Chupathingy's. Not on consoles. |
| `game.unlock_all_levels` | `true` gives new profiles every campaign level. By default a new profile starts at The Pillar of Autumn, as on the Xbox. Profiles made by earlier versions had every level open; `profile_reset_campaign` in the console starts one over. |
| `audio.volume`, `audio.music_volume`, `audio.effects_volume` | Volumes, from 0.0 to 1.0. |
| `audio.device` | The output the sound plays on, by name (Audio Setup lists them); `"default"` follows the system's default output. |
| `audio.channels` | `"auto"` follows your output (5.1 on a 6-channel device, 7.1 on 8, else stereo); `"stereo"`, `"5.1"` or `"7.1"` set it. Desktop only; read at start. |
| `audio.hrtf` | Headphone 3D sound, as the Xbox's 3D sound: `"on"` (the default), `"off"` (for speakers, if it sounds odd), or `"auto"` (on when the output's name says headphones). Stereo output only. |
| `audio.doppler` | Moving sounds shift in pitch as on the Xbox (on by default). |
| `input.mouse_sensitivity`, `input.invert_mouse` | Mouse aim. |
| `input.keyboard_prompts` | Button prompts follow the device you used last: keys after the keyboard or mouse, as Halo PC did, and buttons after a controller. This setting is no longer in the menus; `false` keeps the controller's buttons everywhere. |
| `controls.*` | Every key: `controls.jump = "Space"`, or two at once, such as `"F, Mouse 4"`. |
| `network.browser_url` | The game list Online Games shows (stats.chupathingy.org). |
| `network.copy_to_clipboard` | `false` (the default) leaves the clipboard alone; `true` puts the invite link of a game you host on it. The link is also in the log and under INVITE LINK in the Gearbox menus' Server Settings. |
| `network.host_public` | Whether games you host are listed for everyone (true) or only joinable by invite (false). |
| `network.allow_player_names`, `network.allow_follow_camera` | In games you host, let players turn on names above heads and the follow camera. Both off by default, as on the Xbox. |
| `network.team_change` | Team changes in team games you host: `"off"` (the default: players keep their team, as on the Xbox), `"balanced"` (a change is allowed while no team ends up more than `network.team_change_margin` players ahead, 1 by default) or `"allow"`. |
| `network.hit_line_of_sight` | `true` (the default): in games you host, a hit through a wall is refused. The shot or the explosion needs a clear line to its target. Turn it off if good hits are refused. |
| `network.late_join_wait` | `5` (the default): in games you host, how many seconds a machine that joins a game in progress waits after its first player is in, so the players on its other controllers can join too (split screen). 0 to 30; it starts sooner when the machine has four players. `0` starts it at once. |
| `network.host_cheats` | `false` (the default): while others are in a public internet game you host, your console can't change the game (no cheats, objects or game speed; kick, ban and display commands still work) and cheats set earlier are turned off. `true` allows them, and Online Games shows "Host cheats on" for your game. LAN, private and password games always allow them. |
| `network.host_console_objects` | How many objects your console and cheats can make in each network game you host (`cheat_all_vehicles`, `object_create` and the like). Past it they're refused. 32 by default, 8 while an original Xbox is in the game; `0` means no limit. Objects the map's own scripts make don't count. |
| `network.max_machines_per_address` | In games you host, the most machines that may join from one internet address (1 to 16). `0`, the default, means no limit. A machine past it is told there are too many players from its address. Players on your local network are never limited. Dedicated servers have their own setting, 8 by default. |
| `network.tunnel_port` | The UDP port for games you host, to forward on your router: 2302 by default (the next free one up to 2306 if 2302 is taken). A port you set here is used as is, also when you join. `0` picks a new port each run, as versions before 0.8.1b did. |
| `network.announce` | The OpenCE network version your hosted games announce. `"latest"` (the default) follows the newest the signed table proves, so OpenCE's newest players see your games. A number pins it. |
| `update.auto` | Whether the game updates itself. |
| `paths.data`, `paths.saves` | Where the maps and the saves are, if not the usual place. |
| `paths.logs`, `paths.cache` | Where the logs and the cache go, if not the usual place ([Where your files go](install.md#where-your-files-go)). |

Settings you leave at their default follow each new version's default, so
leaving the file alone is always safe. When a new version changes a default
you were using, the game asks you once whether to take the new one or keep
yours. Every setting can also be given as an
environment variable for one run (the file lists each one's name), which
overrides the file.

Every setting, with its environment variable and default, is in [port/linux/README.md](../port/linux/README.md#settings).

## A new renderer to test

The 64-bit Windows, Mac and Linux builds have a second renderer, bgfx, for
testing. It uses Metal on a Mac and Vulkan on Windows and Linux. OpenGL is
still the default, and nothing changes unless you turn bgfx on.

To try it:

1. Open `config.toml` and find `renderer` under `[display]`.
2. Remove the `#` in front of it and set it to `"bgfx"`.
3. Start the game.

To go back, set it to `"gl"` or put the `#` back. For one run only, start
the game with the environment variable `HALO_RENDERER=bgfx`. If bgfx can't
start, for example with no working Vulkan driver, the game uses OpenGL and
says so in `logs/debug.txt`.

What we know is different from OpenGL:

- FXAA, SMAA and supersampling are OpenGL only. With bgfx they become MSAA 2x.
- The Halo PC ammo and health meters look different.
- The first-person pistol is a little brighter, and water looks grainy.
- On Linux with a software Vulkan driver, some cliffs looked flatter than
  with OpenGL. We don't know yet if real graphics cards do this.
- There's no menu option for it yet. It's in `config.toml` only.
- The 32-bit builds, Android, FreeBSD and the consoles don't have it.

Tell us how it runs. Post in the Discord with your system and graphics card,
and what looks wrong or slow next to OpenGL. Send your logs with **Logs &
help** in the setup screen. It uploads `logs/debug.txt` to
dump.chupathingy.org and gives you a code to paste with your report. The line
starting `bgfx:` in `debug.txt` names the backend and the graphics card.
Screenshots of the same spot with each renderer help a lot.
