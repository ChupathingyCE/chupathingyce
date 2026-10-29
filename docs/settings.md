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
| Windows | next to `halo.exe` |
| Mac | `~/Library/Application Support/ChupathingyCE/config.toml` |
| Linux | next to the `halo` executable |
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
| `display.player_names` | Names over players' heads: `"all"`, `"allies"`, `"enemies"` or `"none"`. |
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
| `network.tunnel_port` | The UDP port for games you host, to forward on your router: 2302 by default (the next free one up to 2306 if 2302 is taken). A port you set here is used as is, also when you join. `0` picks a new port each run, as versions before 0.8.1b did. |
| `update.auto` | Whether the game updates itself. |
| `paths.data`, `paths.saves` | Where the maps and the saves are, if not the usual place. |
| `paths.logs`, `paths.cache` | Where the logs and the cache go, if not the usual place ([Where your files go](install.md#where-your-files-go)). |

Settings you leave at their default follow each new version's default, so
leaving the file alone is always safe. Every setting can also be given as an
environment variable for one run (the file lists each one's name), which
overrides the file.

Every setting, with its environment variable and default, is in [port/linux/README.md](../port/linux/README.md#settings).
