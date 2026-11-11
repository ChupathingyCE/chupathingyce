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
| `display.refresh_rate` | Fullscreen's refresh rate in Hz, the nearest the display has. 0 (the default) keeps the display's own. It only works on Windows and X11. Wayland, gamescope and the Mac keep their own rate. config.toml only for now. |
| `display.vsync`, `display.max_fps` | Vertical sync, and a frame rate cap with V-Sync off (0 for twice the display's refresh rate, -1 for none). On the Steam Deck in Game Mode (under gamescope), a cap at the display's refresh rate or a whole fraction of it (60 at 60 Hz, or 30) keeps the frames in step with the display, so they don't judder. Everywhere else a cap with V-Sync off works as before. |
| `display.interpolation` | `false` keeps the game's own 30 frames a second; Video Setup's Frame Rate at 30 (Xbox) sets it. |
| `display.direct_camera` | Instant aim: the view turns the frame the mouse, stick or touch moves it. On by default; config.toml only. |
| `display.menus` | `"chupathingy"` (the default: the Xbox's look with Halo PC's settings, profiles and more), `"xbox"` (as close to the console as can be) or `"gearbox"` (Halo PC's menus with their Server Browser; `"pc"` still works). |
| `display.theme` | The colors of DELTA and the setup screen: `"blue"` (Halo Blue, the default) or `"red"` (Chupathingy Red). Change it in the setup screen's Display tab or in DELTA's SETTINGS, where it shows at once. |
| `display.high_res_text` | `true` draws the game's text (menus and HUD) with sharp fonts at your resolution; `false` uses the maps' original bitmap fonts. The Server Browser and the moderation overlays use the system's fonts either way. For a blocky look overall, also set `display.resolution_scaling = "original"`. |
| `display.high_res_font`, `display.high_res_font_large` | Your own font file for the sharp text and Online Games, in place of the built-in fonts: put it in `data/fonts` beside `maps` and give its name (`high_res_font = "MyFont.ttf"`), or a full path. `_large` sets one for the large text only. Empty uses the built-in fonts; a file that can't be read is noted in `logs/debug.txt` and the built-in font is used. The font is yours: Chupathingy ships none but its own free ones, and never sends yours anywhere. |
| `display.field_of_view` | How wide the first-person view is, in degrees: 70 (the Xbox's) by default, 50 to 120. |
| `display.doors_block_light` | `true` (the default) stops a light behind a closed door from lighting your side of it, such as a plasma bolt in the next room. `false` lets it through, as on the Xbox. Drawing only. Video Setup, Advanced: Doors Block Light. |
| `display.far_clip_distance` | How far the world is drawn: 1024 (the Xbox's) by default, up to 10000, for large maps that cut off in the distance. Drawing only. |
| `display.match_timer` | A multiplayer game's clock: `"off"` (the default), `"remaining"` (the time left, or the time played when there is no time limit) or `"elapsed"` (the time played). An older `true` still works as `"remaining"`. |
| `display.match_timer_position` | Where the clock goes: `"top"` (the default) or `"bottom_right"`, like later Halo games. |
| `display.player_names` | Names over players' heads: `"all"`, `"allies"`, `"enemies"` or `"none"` (the default, as on the Xbox). Only where the host allows them (`network.allow_player_names`). |
| `display.follow_camera` | After you die, watch a living player's view: `"off"` (the default, as on the Xbox), `"long_waits"` (only during a long respawn wait) or `"always"`. LB and RB, or Q and E, switch players. Only where the host allows it (`network.allow_follow_camera`). |
| `display.xbox_loading_picture` | `true` (the default) shows the Xbox's own loading picture, `xbox_loading.tga` in your maps folder, and Chupathingy's when that file isn't there. `false` always shows Chupathingy's. Not on consoles. |
| `game.unlock_all_levels` | `true` gives new profiles every campaign level. By default a new profile starts at The Pillar of Autumn, as on the Xbox. Profiles made by earlier versions had every level open; `profile_reset_campaign` in the console starts one over. |
| `audio.volume`, `audio.music_volume`, `audio.effects_volume` | Volumes, from 0.0 to 1.0. |
| `audio.device` | The output the sound plays on, by name (Audio Setup lists them); `"default"` follows the system's default output. |
| `audio.channels` | `"auto"` follows your output (5.1 on a 6-channel device, 7.1 on 8, else stereo); `"stereo"`, `"5.1"` or `"7.1"` set it. Desktop only; read at start. |
| `audio.hrtf` | Headphone 3D sound, as the Xbox's 3D sound: `"on"` (the default), `"off"` (for speakers, if it sounds odd), or `"auto"` (on when the output's name says headphones). Stereo output only. |
| `audio.voice_chat` | Voice chat: `"off"` (the default), `"push_to_talk"` (hold V, or the D-pad right) or `"open_mic"`. The microphone opens only when you first talk. Voice goes through the game's host and is never recorded. |
| `audio.voice_channel` | The channel you talk on: `"party"`, `"team"` (the default), `"proximity"` (players near you, who hear you from where you stand) or `"everyone"` (only where the host allows it). B, or the D-pad left, switches it. |
| `audio.voice_volume`, `audio.voice_volume_party`, `_team`, `_proximity`, `_everyone` | Voices' volume, and each channel's, from 0.0 to 2.0. A channel at 0 is off: the host sends you none of it. |
| `audio.voice_input_device`, `audio.voice_open_mic_level`, `audio.voice_positional` | The microphone (empty for the system's), how loud you must be for open mic to send, and whether proximity voices use the game's 3D sound (else a plain pan and fade). |
| `display.voice_speakers` | Show who is talking in voice chat, small, at the top right (on by default). Your own channel shows there while you talk either way. |
| `audio.doppler` | Moving sounds shift in pitch as on the Xbox (on by default). |
| `input.mouse_sensitivity`, `input.invert_mouse` | Mouse aim. |
| `input.keyboard_prompts` | Button prompts follow the device you used last: keys after the keyboard or mouse, as Halo PC did, and buttons after a controller. This setting is no longer in the menus; `false` keeps the controller's buttons everywhere. |
| `controls.*` | Every key: `controls.jump = "Space"`, or two at once, such as `"F, Mouse 4"`. |
| `controls.screenshot` | The screenshot key: `"F9"` by default, and Print Screen always works too. Screenshots go to `screenshots/` beside `maps/`, named after the map and the time. Desktop only. |
| `controls.capture_buttons` | `true` lets a controller take screenshots and clips too: its Share button, or Back and the right stick together, takes a screenshot, and Back and the left stick saves a clip. Off by default. |
| `clips.enabled`, `clips.seconds`, `controls.clip` | `clips.enabled = true` keeps the last `clips.seconds` (30 by default, 5 to 120) of the game, picture and sound, in memory, and `controls.clip` (`"F10"`) saves them as an MP4 in `clips/` beside `maps/`. Off by default; when off it costs nothing. Read at start. Desktop only. |
| `network.browser_url` | The game list Online Games shows (stats.chupathingy.org). |
| `network.copy_to_clipboard` | `false` (the default) leaves the clipboard alone; `true` puts the invite link of a game you host on it. The link is also in the log and under INVITE LINK in the Gearbox menus' Server Settings. |
| `network.host_public` | Whether games you host are listed for everyone (true) or only joinable by invite (false). |
| `network.keep_parties_together` | In team games you host, put a party's players on one team when the teams stay even. On by default. |
| `delta.follow_leader` | When your party's leader goes to a game: `"ask"` (the default) gives you a few seconds to stay and then follows, but in a game you stay unless you press join; `"always"` follows when the seconds run out, in a game too; `"never"` doesn't offer it. Also FOLLOW LEADER in DELTA's PARTY. |
| `network.allow_player_names`, `network.allow_follow_camera` | In games you host, let players turn on names above heads and the follow camera. Both off by default, as on the Xbox. |
| `network.late_join_hold` | In games you host, players who join a game in progress watch through the follow camera until the next game starts. Off by default: they spawn at once, as Halo does. |
| `network.voice`, `network.voice_team`, `_proximity`, `_everyone`, `_party`, `_lobby`, `network.voice_range`, `network.voice_kbps` | Voice chat in games you host: relayed at all (on), the channels players may talk on (team, proximity and party on, everyone off), everyone heard in the lobby (on), how near is near (15 world units) and the quality (24 kbps; about 5 KB/s of your upload for each talker and each listener). |
| `network.team_change` | Team changes in team games you host: `"off"` (the default: players keep their team, as on the Xbox), `"balanced"` (a change is allowed while no team ends up more than `network.team_change_margin` players ahead, 1 by default) or `"allow"`. |
| `network.hit_line_of_sight` | `true` (the default): in games you host, a hit through a wall is refused. The shot or the explosion needs a clear line to its target. Turn it off if good hits are refused. |
| `network.late_join_wait` | `5` (the default): in games you host, how many seconds a machine that joins a game in progress waits after its first player is in, so the players on its other controllers can join too (split screen). 0 to 30; it starts sooner when the machine has four players. `0` starts it at once. |
| `network.map_share`, `network.map_share_upload` | `false` (the default): your game doesn't hand out its map. `true`: a player who joins without your Custom Edition map and asks gets it from you, the map file alone, over TCP port 5153, at most `network.map_share_upload` KB a second (`1024`; a game you host from the menus sends 256 unless you set it). A dedicated server's game and a game you host from the menus alike. Our [C] servers turn it on after the map bot's first invite test. Refer to "Map sharing" in [port/linux/README.md](../port/linux/README.md#map-sharing). |
| `network.map_downloads_from_host`, `network.map_downloads_from_host_limit` | `"off"` (the default): a missing Custom Edition map never comes from the game's host. `"ask"` asks each time, `"always"` doesn't. Maps from hosts come straight from them, not from us, and we haven't checked them. Turn this on only if you're OK with that. The host comes after HaloNet and Chupathingy maps. The file waits in `maps_quarantine` until it passes quick checks and the blacklist. The limit is the largest map taken from a host, in MB (`512`, 16 to 768). No map comes from a host until the blacklist is loaded. Refer to "From the host" in [docs/maps.md](maps.md). Desktop and Android. |
| `network.host_cheats` | `false` (the default): while others are in a public internet game you host, your console can't change the game (no cheats, objects or game speed; kick, ban and display commands still work) and cheats set earlier are turned off. `true` allows them, and Online Games shows "Host cheats on" for your game. LAN, private and password games always allow them. |
| `network.host_console_objects` | How many objects your console and cheats can make in each network game you host (`cheat_all_vehicles`, `object_create` and the like). Past it they're refused. 32 by default, 8 while an original Xbox is in the game; `0` means no limit. Objects the map's own scripts make don't count. |
| `network.max_machines_per_address` | In games you host, the most machines that may join from one internet address (1 to 16). `0`, the default, means no limit. A machine past it is told there are too many players from its address. Players on your local network are never limited. Dedicated servers have their own setting, 8 by default. |
| `network.tunnel_port` | The UDP port for games you host, to forward on your router: 2302 by default (the next free one up to 2306 if 2302 is taken). A port you set here is used as is, also when you join. `0` picks a new port each run, as versions before 0.8.1b did. |
| `network.announce` | The OpenCE network version your hosted games announce. `"latest"` (the default) follows the newest the signed table proves, so OpenCE's newest players see your games. A number pins it. |
| `update.auto` | Whether the game updates itself. |
| `update.offer_64bit` | 32-bit Windows build: whether to offer the 64-bit build on 64-bit Windows. "Keep 32-bit" turns it off. |
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

On Windows there's also Direct3D 11, for testing on computers whose Vulkan
is missing or bad. It's only used when you ask for it: set `renderer` to
`"d3d11"`, or pick **DIRECT3D 11** on the **RENDERER** row of the display
settings and restart. If it can't start, or Windows would draw it in
software, the game uses OpenGL.

Some Vulkan drivers start but run badly on older computers, so the game
skips them and uses OpenGL. Right now that's nouveau's Vulkan (NVK) on a
GeForce older than the 20 series, NVIDIA drivers older than 470, Mesa's
Vulkan for Intel Ivy Bridge, Haswell and Bay Trail graphics, and software
Vulkan. `debug.txt` has a `bgfx: Vulkan GPU` line for each card with the
reason. To try Vulkan anyway, set `vulkan_blocklist = false` under
`[display]`. The line `renderer=` in `debug.txt` says what drew the game.

What we know is different from OpenGL:

- FXAA, SMAA and supersampling are OpenGL only. With bgfx they become MSAA 2x.
- The Halo PC ammo and health meters look different.
- The first-person pistol is a little brighter, and water looks grainy.
- The **RENDERER** row in the display settings switches it after a restart.
- The 32-bit builds, Android, FreeBSD and the consoles don't have it.

Tell us how it runs. Post in the Discord with your system and graphics card,
and what looks wrong or slow next to OpenGL. Send your logs with **Logs &
help** in the setup screen. It uploads `logs/debug.txt` to
dump.chupathingy.org and gives you a code to paste with your report. The line
starting `bgfx:` in `debug.txt` names the backend and the graphics card.
Screenshots of the same spot with each renderer help a lot.
