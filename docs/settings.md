# Settings (config.toml)

Most settings are in the game's menus. Everything else is in one file,
`config.toml`. The game writes it the first time it starts. Every setting is
in it, commented out at its default, with a line or two about what it does.

To change a setting:

1. Close the game.
2. Open `config.toml` in a text editor.
3. Find the setting and remove the `#` in front of it.
4. Change the value and save the file.
5. Start the game.

A line that starts with `#` does nothing. Words need quotes
(`menus = "xbox"`, not `menus = xbox`), and true and false are lowercase.

If a change seems to do nothing, look in `logs/debug.txt` for lines that
start with `config.toml` or `settings:`. They name the line the game
skipped and why. On Windows, check that the file isn't really called
`config.toml.txt`.

| Platform | config.toml is |
| --- | --- |
| Windows | next to `chupathingy.exe` |
| Mac | `~/Library/Application Support/ChupathingyCE/config.toml` |
| Linux | next to the `chupathingy` program; with the AppImage or a packaged install, `~/.local/share/chupathingyce/config.toml` |
| Steam Deck | `~/.local/share/chupathingyce/config.toml` (the AppImage) |
| FreeBSD | next to the `chupathingy` program |
| Android | in the game's data folder, `/sdcard/Android/data/dev.horrible.chupathingyce/files` |

A few people look for most:

| Setting | What it does |
| --- | --- |
| `display.mode` | `"fullscreen"`, `"borderless"` or `"windowed"`. F11 switches between a window and the whole screen. When it is set, `display.fullscreen` is ignored; when it is empty, `display.fullscreen = true` is borderless and `false` a window. |
| `display.window_scale` | The window's size, as a multiple of 640x480. |
| `display.refresh_rate` | Fullscreen's refresh rate in Hz, the nearest the display has. 0 (the default) keeps the display's own. It only works on Windows and X11. Wayland, gamescope and the Mac keep their own rate. config.toml only for now. |
| `display.vsync`, `display.max_fps` | Vertical sync, and a frame rate cap with V-Sync off (0 for twice the display's refresh rate, -1 for none). On the Steam Deck in Game Mode (under gamescope), a cap at the display's refresh rate or a whole fraction of it (60 at 60 Hz, or 30) keeps the frames in step with the display, so they don't judder. Everywhere else a cap with V-Sync off works as before. |
| `display.vrr` | For a variable refresh display (G-SYNC, FreeSync): `"on"` shows the frames with V-Sync and holds them 3 a second under the refresh rate (117 at 120 Hz, or `display.max_fps` where that is lower), so each frame shows as soon as it is ready, with no tearing and no V-Sync lag. `"auto"` turns it on only where the system says VRR is active (gamescope, or a Linux desktop whose display has it on); on Windows and the Mac it stays off, so choose `"on"` there. `"off"` (the default) changes nothing. VARIABLE REFRESH in the display settings. Based on work by goatbutt and the OpenH1 team. Desktop only. |
| `display.renderer` | What draws the game: `"gl"` (OpenGL, the default), `"bgfx"` (Vulkan, or Metal on a Mac) or, on Windows, `"d3d11"` (Direct3D 11). The RENDERER row in the display settings changes it after a restart. See [Renderers](#renderers). On Android it's `"gl"` (OpenGL ES, the default) or `"vulkan"` to test: see [Graphics](../port/android/README.md#graphics-opengl-es-and-vulkan). |
| `display.light_effects` | Light from shots, muzzle flashes and glowing things, blocked by walls: `"auto"` (the default), `"high"`, `"low"` or `"off"` (the Xbox's own lighting). LIGHT EFFECTS in the display settings. Based on chestahh's dynamic lighting in GulchCE. Desktop only. |
| `display.prepare_shaders` | `true` (the default) makes a map's shaders while its loading screen is up, so the game doesn't stutter the first time it draws something. `false` makes each one when it's first needed. |
| `display.medals` | `true` shows your medals in the kill feed as you earn them. Off by default, as Halo shows none. Your service record counts them either way. |
| `display.interpolation` | `false` keeps the game's own 30 frames a second; Video Setup's Frame Rate at 30 (Xbox) sets it. |
| `display.direct_camera` | Instant aim: the view turns the frame the mouse, stick or touch moves it. On by default; config.toml only. |
| `display.menus` | `"chupathingy"` (the default: the Xbox's look with Halo PC's settings, profiles and more) or `"xbox"` (as close to the console as can be). The Gearbox menus were retired as a menu set: a file that says `"gearbox"` (or the older `"pc"`) gets the Xbox menus, with a one-time notice, and is then written as `"xbox"`. |
| `display.duct_tape_logo` | `true` puts duct tape over the Chupathingy menus' main menu logo. Off by default. The Konami code on the main menu (Up, Up, Down, Down, Left, Right, Left, Right, B, A) turns it on or off and writes it here. |
| `display.theme` | The colors of the Delta Guide and the setup screen: `"blue"` (Halo Blue, the default), `"green"` (Dashboard Green), `"orange"` (Visor Orange), `"live"` (Live Orange) or `"red"` (Chupathingy Red). Change it in the setup screen's Display tab or in the Delta Guide's SETTINGS, where it shows at once. |
| `delta.guide_background` | What the Delta Guide draws behind its panel: `"auto"` (the default: the Xbox dashboard's cellwall with Dashboard Green, a plain dark shade with the other themes), `"cellwall"` or `"plain"`. BACKGROUND in the Delta Guide's SETTINGS. |
| `display.high_res_text` | `true` draws the game's text (menus and HUD) with sharp fonts at your resolution; `false` uses the maps' original bitmap fonts. The Server Browser and the moderation overlays use the system's fonts either way. For a blocky look overall, also set `display.resolution_scaling = "original"`. |
| `display.high_res_text_fields` | With `display.high_res_text`: `true` (the default) draws each letter from a distance field, made once and sharp at any size and any resolution, on the OpenGL, bgfx and Vulkan renderers; `false` draws the letters as pictures made for the window's size, as before 0.9.3. Nothing on the original Xbox, which draws the maps' own fonts, or on a console's own renderer. Based on work by goatbutt and the OpenH1 team. |
| `display.title_font` | The font of the menus' titles: `"classic"` (the default, looks like Halo's own titles), `"cyborg"` (the same font as the menu text) or `"halo"` (the screens' titles in Halo's own lettering, made from your own `ui.map`; nothing is saved or shared). A change shows at once. The consoles have `"classic"` only. |
| `display.high_res_font`, `display.high_res_font_large` | Your own font file for the sharp text and Online Games, in place of the built-in fonts: put it in `data/fonts` beside `maps` and give its name (`high_res_font = "MyFont.ttf"`), or a full path. `_large` sets one for the large text only. Empty uses the built-in fonts; a file that can't be read is noted in `logs/debug.txt` and the built-in font is used. The font is yours: Chupathingy ships none but its own free ones, and never sends yours anywhere. |
| `display.field_of_view` | How wide the first-person view is, in degrees: 70 (the Xbox's) by default, 50 to 120. |
| `display.reticle_scale` | The reticle's size, `0.5` to `2.0`; `1.0` (the default) is the Xbox's. Video Setup, View: Reticle Size. |
| `display.crosshair_position` | Where the crosshair and your aim sit: `"retail"` (the default, as on the Xbox) or `"lowered"` (a tenth of the view's height below the middle, as later Halo games do). The view moves with it, so shots still go where the crosshair is. While you're zoomed, the view snaps back to centered and the scope sits in the middle, by design (Halo 3 does the same). The Guide and the setup screen: Crosshair. |
| `display.hud_safe_zone` | How far the HUD sits from the screen's edges: `"xbox"` (48 in from the sides and 36 from the top and bottom of a 640x480 screen, as on the Xbox), `"pc"` (8 in, as Halo PC), `"auto"` (the default on PCs, Macs and Android: the Xbox's in a game alone, Halo PC's in split screen) or two numbers, `"24 18"` (8 to 48 from the sides, 8 to 36 from the top and bottom). Other than `"xbox"`, each split screen view is laid out in its own middle: its HUD sits the same distance in from all four of its edges, and its crosshair, aim, weapon and timer are in the view's middle, so every view looks the same. On the Xbox they sit a little off each view's middle, toward the screen's center, and `"xbox"` keeps that. This replaces `display.crosshair_position = "centered"`: a settings file with it is changed to `"retail"` (and `"xbox"` here to `"auto"`), and the log says so. Only what is drawn changes. Like Chimera's `chimera_safe_zones`. The consoles (the original Xbox, the Xbox 360, the Wii U, the Switch) keep `"xbox"` as their default: they play on TVs that overscan, where tighter margins can push the HUD off the picture. Settings: Interface > Safe Zones (On, Single Screen, Off). |
| `display.first_person_weapon` | `false` hides your weapon and arms in first person; `true` (the default) draws them. Video Setup, View: Weapon Model. |
| `display.camo_weapon_fix` | `false` (the default) lets a wall close by cut away your camouflaged first-person weapon, as on the Xbox. `true` draws it in front of walls. Drawing only. Video Setup, Advanced: Camo Weapon Fix. Based on work by goatbutt and the OpenH1 team. |
| `display.plasma_sparks` | `false` (the default) leaves out the blue sparks off a live plasma grenade, as on the Xbox, which never drew them. `true` draws them, at any frame rate. Drawing only. Video Setup, Advanced: Plasma Sparks. Thanks to ugoboom for the idea. |
| `display.ragdolls` | `false` (the default) lays a dead body down with its death animation, as on the Xbox. `true`: a character killed plays its death animation and falls limp partway through it, as in Halo 2 and Halo 3: the ragdoll takes over from the animated pose as the body falls or meets a wall or the ground, and follows the animation for a moment so there is no pop. A body an explosion, a melee blow or a vehicle's hit throws into the air plays its airborne death animation as it flies, and goes limp when it hits the ground or a wall; one left on its feet goes limp at once. It comes to rest where it lands (rigid body ragdolls on Jolt Physics, made from each character's own model). Drawing only: the body, its hit boxes, saved games and the network are as before, and each machine simulates its own. Four fall at once at most, twelve are kept. If the physics runs out of memory, the bodies are laid down as on the Xbox and ragdolls stay off until the setting is turned off and on again; the game goes on. Video Setup, Advanced: Ragdolls, and the Guide. Desktop only (Linux, Windows, macOS, FreeBSD). The idea is from jojoxdk's Halo 1 MCC Box3D Physics Mod. |
| `display.opensauce_post_processing` | `true` (the default) draws the post-processing an OpenSauce map carries, as SPV3's levels and menu do: their tone mapping, visor, lens dirt, film grain, ambient occlusion, depth of field, reflections, motion blur, light shafts and bloom, as the map's own settings turn them on. Only maps with OpenSauce post-processing change; every other map draws as before. `false` draws them without it, for performance: on a heavy level it can cost as much as the rest of the frame. Drawn by the OpenGL and bgfx renderers. config.toml only. |
| `display.developer_flames` | `true` (the default) shows a blue flame on the helmets of verified Chupathingy developers in games on Delta, as Halo 2 did for Bungie's staff. It's made from the map's own flame sprite, and only shows on a player whose Delta name this game checked (bound to the game's host) and shows. Never in your own first-person view, on a camouflaged player or on a body. `false` hides them. Drawing only: nothing about the game changes, and nothing is sent. A developer can turn their own off on their profile's Badges panel on the site. The Guide and the setup screen: Developer Flames. Desktop and Android; the consoles don't show verified names yet. |
| `display.doors_block_light` | `true` (the default) stops a light behind a closed door from lighting your side of it, such as a plasma bolt in the next room. `false` lets it through, as on the Xbox. Drawing only. Video Setup, Advanced: Doors Block Light. |
| `display.far_clip_distance` | How far the world is drawn: 1024 (the Xbox's) by default, up to 10000, for large maps that cut off in the distance. Drawing only. |
| `display.match_timer` | A multiplayer game's clock: `"off"` (the default), `"remaining"` (the time left, or the time played when there is no time limit) or `"elapsed"` (the time played). An older `true` still works as `"remaining"`. |
| `display.match_timer_position` | Where the clock goes: `"top"` (the default) or `"bottom_right"`, like later Halo games. |
| `display.player_names` | Names over players' heads: `"all"`, `"allies"`, `"enemies"` or `"none"` (the default, as on the Xbox). Only where the host allows them (`network.allow_player_names`). |
| `display.follow_camera` | After you die, watch a living player's view: `"off"` (the default, as on the Xbox), `"long_waits"` (only during a long respawn wait) or `"always"`. LB and RB, or Q and E, switch players. Only where the host allows it (`network.allow_follow_camera`). |
| `display.join_screen` | While a game is joined or its map downloads, the loading screen with what is going on and B to cancel (on by default). `false`: a small panel over the game list. Settings > Network Setup > Join Screen. |
| `display.lobby` | The game lobby of the Chupathingy menus (on by default): the players with their teams and READY, the map and the host's menu, for a game you host or joined and for dedicated servers, and a preview of a game under way before you join it. `false`: the Xbox's System Link screen, as the Xbox menus have it. |
| `display.xbox_loading_picture` | `true` (the default) shows the Xbox's own loading picture, `xbox_loading.tga` in your maps folder, and Chupathingy's when that file isn't there. `false` always shows Chupathingy's. On the Xbox 360 and the Wii U too; not on the original Xbox. |
| `game.unlock_all_levels` | `true` gives new profiles every campaign level. By default a new profile starts at The Pillar of Autumn, as on the Xbox. Profiles made by earlier versions had every level open; `profile_reset_campaign` in the console starts one over. |
| `game.forge` | `false` (the default) leaves Forge out. `true` turns on Forge, the layout editor of the multiplayer maps and the campaign missions (their AI characters): FORGE in the Multiplayer menu (the Chupathingy menus) opens its lobby, where you build a layout of a map or play a saved one as a game. Also the console's `forge` commands. Local games only. FORGE in the Delta Guide's SETTINGS; the menu row shows the next time the menus load. See [forge.md](forge.md). |
| `game.forge_objects` | `512` (the default): the most objects and markers a Forge layout holds, 64 to 512. A layout of more places what fits and says how many it left out. A build with the console's limits (`--console-limits`) has `256`, at most 256. See [forge.md](forge.md). |
| `game.forge_ai` | `128` (the default): the most AI characters alive at once in Forge, 8 to 128. Past it, a character that should come back waits for room. A build with the console's limits has `32`, at most 32. See [forge.md](forge.md). |
| `game.forge_hold_to_delete` | How Forge's monitor deletes. `"off"` (the default) deletes at a press of Y or the delete key. `"controller"` deletes when the controller's Y is held half a second, with a bar over the crosshair that fills as it's held; the keyboard's key still deletes at a press. `"all"` holds both. Any other value is `"off"`. DELETE in the Forge menu's OPTIONS. See [forge.md](forge.md). |
| `game.forge_grab_range` | How far Forge's crosshair reaches to grab, copy or delete an object or marker: `"near"` (20 world units, about 60 m), `"medium"` (35 world units, about 105 m) or `"far"` (60 world units, about 180 m: the default). Walls and the ground still stop it nearer. Any other value is `"far"`. GRAB RANGE in the Forge menu's OPTIONS. See [forge.md](forge.md). |
| `game.forge_held_facing` | What Forge's monitor holds. `"keep"` (the default) keeps the facing it was grabbed with while the camera turns around it. `"view"` turns it with the camera (its yaw only), so it keeps facing the same way from you, until you turn it by hand (rotate mode, its angles in PROPERTIES, upright); then it keeps its own facing until you let go. Any other value is `"keep"`. HELD FACING in the Forge menu's OPTIONS. See [forge.md](forge.md). |
| `game.mods` | `true` plays the mods in the `mods` folder beside `maps` (tag files over a map's sounds, as a resource pack; see [mods.md](mods.md)). Nothing happens while the folder is empty. Local games only. |
| `audio.volume`, `audio.music_volume`, `audio.effects_volume` | Volumes, from 0.0 to 1.0. |
| `audio.loudness` | `"xbox"` (the default) plays sound at the original Xbox's level, half of the mix; `"louder"` plays it 6 dB louder, as before 0.9.2. An older settings file is asked once. Audio Setup and the Delta Guide's SETTINGS: Loudness. |
| `audio.device` | The output the sound plays on, by name (Audio Setup lists them); `"default"` follows the system's default output. |
| `audio.channels` | `"auto"` follows your output (5.1 on a 6-channel device, 7.1 on 8, else stereo); `"stereo"`, `"5.1"` or `"7.1"` set it. Desktop only; read at start. |
| `audio.hrtf` | Headphone 3D sound: 3D sounds through the Xbox's own HRTF, as measured on an Xbox (a sound behind or above you is told from one ahead). `"on"` (the default), `"off"` (for speakers, if it sounds odd), or `"auto"` (on when the output's name says headphones). Stereo output only. |
| `audio.voice_chat` | Voice chat: `"off"` (the default), `"push_to_talk"` (hold V, or the D-pad right) or `"open_mic"`. The microphone opens only when you first talk. Voice goes through the game's host and is never recorded. |
| `audio.voice_channel` | The channel you talk on: `"party"`, `"team"` (the default), `"proximity"` (players near you, who hear you from where you stand) or `"everyone"` (only where the host allows it). B, or the D-pad left, switches it. |
| `audio.voice_volume`, `audio.voice_volume_party`, `_team`, `_proximity`, `_everyone` | Voices' volume, and each channel's, from 0.0 to 2.0. A channel at 0 is off: the host sends you none of it. |
| `audio.voice_input_device`, `audio.voice_open_mic_level`, `audio.voice_positional` | The microphone (empty for the system's), how loud you must be for open mic to send, and whether proximity voices use the game's 3D sound (else a plain pan and fade). |
| `display.voice_speakers` | Show who is talking in voice chat, small, at the top right (on by default). Your own channel shows there while you talk either way. |
| `audio.hit_sounds` | `true`: a short tone when your shot hits another player in multiplayer, as Halo PC had. `false` (the default) is the Xbox's: none. The tone is the map's own "ting"; on maps without one (the Xbox's), the ting from your own Custom Edition `sounds.map` in `maps_ce`, else `audio.hit_sound_file`, else none. Audio Setup: Hit Sounds. |
| `audio.hit_sound_file` | A sound file (WAV 16-bit PCM, or Ogg Vorbis, any length; its first 2 seconds play) for hit sounds on maps without a ting, when Custom Edition's `sounds.map` is not in `maps_ce`. A name alone means a file in `data/sounds` next to the maps folder. Empty (the default): none. |
| `audio.doppler` | Moving sounds shift in pitch as on the Xbox (on by default). |
| `audio.reverb` | Sounds echo to match the place you are in, as the Xbox's reverb did (on by default; off on the slowest consoles). |
| `input.mouse_sensitivity`, `input.invert_mouse` | Mouse aim. |
| `input.keyboard_prompts` | Button prompts follow the device you used last: keys after the keyboard or mouse, as Halo PC did, and buttons after a controller. This setting is no longer in the menus; `false` keeps the controller's buttons everywhere. |
| `input.button_style` | The buttons prompts show: `"auto"` (the default: the controller you use), or one style for every controller: `"duke"` (the original Xbox controller's), `"xbox"`, `"playstation"` or `"switch"`. The keyboard shows its keys. The Xbox menus show their own. Settings > Video > BUTTONS, and the Delta Guide's SETTINGS. |
| `input.button_look` | The menus' and the Guide's buttons: `"gem"` (the default) or `"flat"`. |
| `input.hud_buttons` | The buttons in a game's prompts ("Hold X to pick up"): `"classic"` (the default: the map's own) or `"hd"` (the style's flat buttons). |
| `controls.*` | Every key: `controls.jump = "Space"`, or two at once, such as `"F, Mouse 4"`. |
| `controls.walk` | A walk key for the keyboard: held, you move at half speed, as a stick pushed halfway. Empty (the default) is none. Controls Setup: Walk. |
| `controls.delta`, `controls.delta_button` | The key that opens the Delta Guide: `"F3"` (on a Mac, F3 or the backslash key). OPEN KEY in the Delta Guide's SETTINGS. A controller's Guide button opens it too, unless Steam or the Xbox Game Bar takes that button. |
| `controls.push_to_talk`, `controls.voice_channel` | Hold `"V"` to talk in voice chat, and press `"B"` to switch the voice channel. |
| `controls.screenshot` | The screenshot key: `"F9"` by default, and Print Screen always works too. Screenshots go to `screenshots/` beside `maps/`, named after the map and the time. Desktop only. |
| `controls.capture_buttons` | `true` lets a controller take screenshots and clips too: its Share button, or Back and the right stick together, takes a screenshot, and Back and the left stick saves a clip. Off by default. |
| `clips.enabled`, `clips.seconds`, `controls.clip` | `clips.enabled = true` keeps the last `clips.seconds` (30 by default, 5 to 120) of the game, picture and sound, in memory, and `controls.clip` (`"F10"`) saves them as an MP4 in `clips/` beside `maps/`. Off by default; when off it costs nothing. Read at start. Desktop only. CLIPS and CLIP LENGTH under Settings > GAME (the Guide's and the setup screen's) change the first two. |
| `controls.forge_*` | Forge's monitor's keys, used only while you fly as the monitor (so they can share keys with the game's): `forge_up` (`"Space"`), `forge_down` (`"Left Ctrl, C"`), `forge_fast` (`"Left Shift"`), `forge_grab` (`"Mouse Left"`), `forge_rotate` (`"Mouse Right"`, held), `forge_nearer` and `forge_farther` (`"Wheel Down"`, `"Wheel Up"`), `forge_copy` (`"F"`), `forge_delete` (`"Delete, X"`), `forge_upright` (`"U"`), `forge_speed` (`"Q"`), `forge_nudge` (`"N"`), `forge_menu` (`"E"`) and `forge_monitor` (`"M"`, monitor or Spartan). The movement keys fly and the mouse looks. Desktop only. See [forge.md](forge.md). |
| `network.browser_url` | The game list Online Games shows (stats.chupathingy.org). |
| `network.networks_trust`, `network.networks_block` | Other networks whose players show as verified, as `Name:domain` (in a narrow column, the name and a `:`). By default a game checks the names of its own network (chupathingy.org) and the networks in the signed list of networks. `networks_trust` adds networks of your own, for private or test networks: each a domain and 1 to 4 root keys of 64 hex digits, separated by spaces, and networks separated by commas (`"example.org <key> <key>"`), 16 at most. `networks_block` takes domains you don't want checked, separated by commas; your own network can't be removed. Both empty by default. config.toml only. Desktop, Android and the dedicated server. |
| `network.copy_to_clipboard` | `false` (the default) leaves the clipboard alone; `true` puts the invite link of a game you host on it. The log shows only the start of the link (a dedicated server's log shows all of it). |
| `network.host_public` | Whether games you host are listed for everyone (true, the default) or only joinable by invite (false). Anyone with a game's invite can find the host's internet address, and a listed game's invite is public: see [privacy.md](privacy.md#your-internet-address). |
| `network.direct_ip_host` | `false` (the default): games you host answer game searches from your local network and internet play only. `true`: they also answer searches from internet addresses, no more than four answers a second, so a player who joins by your address (DIRECT IP, `connect`) finds your game at once. Off, that player still joins, after about 5 seconds. DIRECT IP HOST in the setup screen's Network settings. |
| `network.keep_parties_together` | In team games you host, put a party's players on one team when the teams stay even. On by default. |
| `delta.follow_leader` | When your party's leader goes to a game: `"ask"` (the default) gives you a few seconds to stay and then follows, but in a game you stay unless you press join; `"always"` follows when the seconds run out, in a game too; `"never"` doesn't offer it. Also FOLLOW LEADER in the Delta Guide's PARTY. |
| `network.allow_player_names`, `network.allow_follow_camera` | In games you host, let players turn on names above heads and the follow camera. Both off by default, as on the Xbox. |
| `network.late_join_hold` | In games you host, players who join a game in progress watch through the follow camera until the next game starts. Off by default: they spawn at once, as Halo does. |
| `network.air_turn_aim` | In games you host (and games you play alone), a shot fired while turned around in mid-air goes where you aim: the "180 glitch" fixed. Off by default: the shot leaves at an angle, as on the Xbox. Players who join your game play by your setting, not their own. It also lifts aim holding on the ground (during some animations). Only players on a Chupathingy build that knows the rule get the fix; OpenCE, Custom Edition, console and older players fire as on the Xbox, though your game draws their turned shots as fixed (hits still follow their own game). |
| `network.voice`, `network.voice_team`, `_proximity`, `_everyone`, `_party`, `_lobby`, `network.voice_range`, `network.voice_kbps` | Voice chat in games you host: relayed at all (on), the channels players may talk on (team, proximity and party on, everyone off), everyone heard in the lobby (on), how near is near (15 world units) and the quality (24 kbps). Higher quality uses more of your upload. |
| `network.team_change` | Team changes in team games you host: `"off"` (the default: players keep their team, as on the Xbox), `"balanced"` (a change is allowed while no team ends up more than `network.team_change_margin` players ahead, 1 by default) or `"allow"`. |
| `network.hit_line_of_sight` | Hits through walls are checked and logged; refused only when this is `true`. `false` (the default): in games you host, a hit that looks like it went through a wall still counts, and the host writes one line about it to its log. `true`: those hits are refused. The shot or the explosion needs a clear line to its target. The check still misjudges some good shots, so it stays off until it is reworked. |
| `network.late_join_wait` | `5` (the default): in games you host, how many seconds a machine that joins a game in progress waits after its first player is in, so the players on its other controllers can join too (split screen). 0 to 30; it starts sooner when the machine has four players. `0` starts it at once. |
| `network.lobby_start` | In the game lobby of a game you host: `"button"` (the default) holds the countdown until you choose START GAME; `"auto"` lets it run by itself once enough players are in, as the Xbox's lobby does. |
| `network.pregame_load_timeout` | `90` (the default): in games you host, how many seconds after the lobby's countdown ends the game waits for a player whose map hasn't loaded. Then that player is dropped and the game starts without them, so one stuck machine can't hold the lobby. `0` waits for ever. |
| `network.map_share`, `network.map_share_upload` | `false` (the default): your game doesn't hand out its map. `true`: a player who joins without your Custom Edition map and asks gets it from you, the map file alone, over TCP port 5153, at most `network.map_share_upload` KB a second (`1024`; a game you host from the menus sends 256 unless you set it). A dedicated server's game and a game you host from the menus alike. Our [C] servers turn it on after the map bot's first invite test. Refer to "Map sharing" in [port/linux/README.md](../port/linux/README.md#map-sharing). |
| `network.map_downloads` | A missing Custom Edition map from the community map repositories when you join: `"ask"` (the default), `"always"` or `"never"`. MAP DOWNLOADS in the network settings. See [docs/maps.md](maps.md). |
| `network.map_downloads_from_host`, `network.map_downloads_from_host_limit` | `"off"` (the default): a missing Custom Edition map never comes from the game's host. `"ask"` asks each time, `"always"` doesn't. Maps from hosts come straight from them, not from us, and we haven't checked them. Turn this on only if you're OK with that. The host comes after HaloNet and Chupathingy maps. The file waits in `maps_quarantine` until it passes quick checks and the blacklist. The limit is the largest map taken from a host, in MB (`512`, 16 to 768). No map comes from a host until the blacklist is loaded. Refer to "From the host" in [docs/maps.md](maps.md). Desktop and Android. |
| `network.host_cheats` | `false` (the default): while others are in a public internet game you host, your console can't change the game (no cheats, objects or game speed; kick, ban and display commands still work) and cheats set earlier are turned off. `true` allows them, and Online Games shows "Host cheats on" for your game. LAN, private and password games always allow them. |
| `network.host_console_objects` | How many objects your console and cheats can make in each network game you host (`cheat_all_vehicles`, `object_create` and the like). Past it they're refused. 32 by default, 8 while an original Xbox is in the game; `0` means no limit. Objects the map's own scripts make don't count. |
| `network.max_machines_per_address` | In games you host, the most machines that may join from one internet address (1 to 16). `0`, the default, means no limit. A machine past it is told there are too many players from its address. Players on your local network are never limited. Dedicated servers have their own setting, 8 by default. |
| `network.tunnel_port` | The UDP port for games you host, to forward on your router: 2302 by default (the next free one up to 2306 if 2302 is taken). A port you set here is used as is, also when you join. `0` picks a new port each run, as versions before 0.8.1b did. |
| `network.public_address` | A host setting, empty by default. For a host whose players come in through a relay or tunnel: the address they reach it at, as `host` or `host:port` (the port defaults to `network.tunnel_port`). Games you host advertise it in place of the address STUN finds. Joining someone else's game works as usual. |
| `network.announce` | The OpenCE network version your hosted games announce. `"latest"` (the default) follows the newest the signed table proves, so OpenCE's newest players see your games. A number pins it. |
| `update.auto` | Whether the game looks for updates when it starts. |
| `update.channel` | Which builds the game updates to: `"release"` (the default), `"testing"` or `"nightly"`. See [Update channels](install.md#update-channels). |
| `update.offer_64bit` | 32-bit Windows build: whether to offer the 64-bit build on 64-bit Windows. "Keep 32-bit" turns it off. |
| `paths.data`, `paths.saves` | Where the maps and the saves are, if not the usual place. |
| `paths.logs`, `paths.cache` | Where the logs and the cache go, if not the usual place ([Where your files go](install.md#where-your-files-go)). |
| `paths.steam_api` | A `steam_api` library for the MCC Workshop's Steam app link to use. Empty uses MCC's own on Windows. Signed Mac builds ignore it: the Steam app link is off there, and subscribing opens the item's page in Steam. |

Settings you leave at their default follow each new version's default, so
leaving the file alone is always safe. When a new version changes a default
you were using, the game asks you once whether to take the new one or keep
yours. Every setting can also be given as an
environment variable for one run (the file lists each one's name), which
overrides the file.

Every setting, with its environment variable and default, is in [port/linux/README.md](../port/linux/README.md#settings).

## Tuning

The `[tuning]` section is for players who want more frames and don't mind
the game looking a bit plainer. Nothing in it changes how the game plays:
only what is drawn. Leave it alone and nothing changes.

| Setting | What it does |
| --- | --- |
| `tuning.preset` | `"retail"` (the default) changes nothing. `"lan-party"` caps the lights and particles of a big firefight (16 lights, 4 with shine, 2048 particles, particles moved once a tick) and shows a late frame at once (`present_mode = "adaptive"`). `"potato"` is the setup screen's POTATO: 8 lights, 2 with shine, 1024 particles, smaller shadows, no anti-aliasing, the Xbox's own lighting and the picture drawn at 640x480. A preset never raises a build's own limit. The original Xbox has presets of its own: see below. |
| `tuning.present_mode` | With V-Sync on: `"vblank"` (the default) waits for the display before every frame. `"adaptive"` shows a frame that missed the display at once, with a tear, rather than a whole refresh later. OpenGL and Vulkan (bgfx's, and the Android Vulkan renderer's FIFO_RELAXED) on Windows, Linux and Android, and the original Xbox. Direct3D 11 and the Mac wait, and the log says so. Read at start. |
| `tuning.particle_thinning` | Draws one in this many of the loose particles in the world (sparks, debris, dust, the puffs of explosions), from 1 to 4. `1` (the default) draws every one. Particles on a weapon, player or vehicle are always drawn, and every particle still moves as before. |
| `tuning.effects` | `"full"` (the default) draws every pass. `"reduced"` leaves out the shine of lights, metal and water, the reflections and mirrors, and the grass on the ground. `"minimal"` also leaves out decals, rain and snow. Lens flares (like a charging plasma pistol's glow) and contrails (like an overcharged bolt's trail) are always drawn. |
| `tuning.shadow_pixels` | How big an object must look on screen, in pixels across, to cast a shadow, from 30 to 400. `30` (the default) is the Xbox's. Higher keeps only the shadows of near objects. |
| `tuning.object_cull_pixels` | Leaves out objects that look smaller than this, in pixels across, before they're lit, from 0 to 16. `0` (the default) draws every one. Players, AI, vehicles, shots and grenades, and an object with something attached (a flag), are always drawn. |
| `tuning.texture_cache` | Original Xbox only: the texture cache's size in MB, like `"20"`, or `"auto"` (the default) for the console's own. It's held between 19 MB and what the console's memory has room for. Read at start. |

A preset fills in the settings it names only where you haven't set them
yourself. The limits stay where they always were (`display.dynamic_light_limit`,
`display.specular_light_limit`, `display.particle_limit` and the rest), so a
value you set there wins over the preset. Turning PERFORMANCE to POTATO on
the setup screen writes `preset = "potato"`.

A value out of range is held in range, and a word the game doesn't know
falls back to the default. Either way `logs/debug.txt` says so in a line
starting `tuning:`. That file also says what every knob was at start
(`tuning: at=boot`), at each map (`tuning: at=map`) and whenever one
changes (`tuning: change`). A crash report and Send my logs carry the same
line.

In a network game nobody sees less of the other players than the Xbox would
show them:

- Particles: nobody sees fewer than the Xbox kept (1024). Smoke and dust can
  hide players, and a lower limit would take them away. `particle_thinning`
  only thins past 1024 loose particles in view, and never the mist, steam
  and smoke of the level's particle systems.
- Lights: `display.dynamic_light_limit` never goes under 8 and
  `display.specular_light_limit` never under 2, the consoles' defaults.
  Another player's muzzle flash, plasma or flashlight on the walls can tell
  you where they are. (0, no limit, stays no limit.)
- `effects`: the grass, rain and snow are always drawn. They can hide a
  player. Lens flares and contrails are drawn in every game.
- `object_cull_pixels` leaves out only debris. Weapons, power-ups and other
  pickups, players, vehicles, projectiles and anything that could be cover
  are always drawn.
- Your own decals, shine, reflections and shadow size stay your choice
  online. They are cosmetic, drawn only on your screen and never sent.

The log says once when a knob is held (`tuning: effects=minimal draws the
grass and weather in a network game`).

Every 10 seconds of play (300 frames), while a knob or limit leaves
something out, the log says what it left out, as an average a frame:
`tuning: did lights=-2.1 shine=-1.0 sprites=-35.2 shadows=-3.0
passes=-shine,-reflections (a frame, over 300 frames)`. `lights` are the
dynamic lights let go and `shine` those lit without it, `particles` the
particles let go or not made, `sprites` the loose particles thinned,
`shadows` and `objects` those left out, and `passes` what `effects` left
out. Lines from a network game end with `a network game`; a line never
mixes the menus' frames with a game's.

Thanks to Bruno Santana and Lord BirchWood: the particle thinning, the
effects levels, the shadow size and leaving small objects out early follow
the ideas in Bruno Santana's modified build of BirchWood's Vita port. Our
versions are written from scratch.

### Every knob, and what the presets do on the original Xbox

No knob changes the simulation: the game tick costs the same whatever they
say. They change only the drawing, the waiting for the display, or memory.

The original Xbox's own limits are already where the PC's presets would
put them, so it has presets of its own, set below its own drawing:

- `lan-party` is for big network games: `effects = "reduced"` (no shine,
  reflections or mirrors; the grass stays in a network game), shadows only
  from objects 60 pixels across or more, debris under 4 pixels left out,
  and a late frame shown at once (`present_mode = "adaptive"`). Each of
  these still counts in a network game. It keeps the console's own 8
  dynamic lights, 2 with shine: a network game holds them there anyway.
- `potato` goes further: `effects = "minimal"` (no decals either; outside
  a network game also no grass, rain and snow), shadows from 120 pixels,
  objects under 8 pixels left out (debris only in a network game), and
  outside a network game 2 lights, 1 with shine, and 512 particles with
  every other loose one drawn. A network game keeps 8 lights, 2 with
  shine, and 1024 particles.
- In big games (more than about 24 players) the original Xbox is held back
  by the game tick, not the drawing. A preset takes the drawing's share of
  the frame down, so it helps there too, but it can't make the tick itself
  faster.
- `display.particle_limit` is the original Xbox's setting too: 1024, its
  most, by default. Lower counts only outside a network game.
- `display.particle_ticks`, the shadow size, anti-aliasing, filtering,
  per-pixel lighting, the Light effects and the resolution aren't the
  console's settings. It already moves particles once a tick and draws the
  Xbox's own way, so they would change nothing there, and its table leaves
  them out.

The table is made from the code by `tools/tuning_table.py`
(`tools/test_tuning_table.py` checks it).

<!-- tuning table: made by tools/tuning_table.py, don't edit by hand -->

| Setting | lan-party sets | potato sets | On the original Xbox | Xbox default and range | lan-party on the Xbox | potato on the Xbox | What it costs or saves |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `display.dynamic_light_limit` | `16` | `8` | yes | `8`; 0 to 128 (0: no limit) | nothing | `2` | Drawing. Each dynamic light redraws the level it reaches twice; in a big firefight most of the GPU's time. Never under 8 in a network game, so on the consoles (whose default is 8) it cuts only outside one. |
| `display.specular_light_limit` | `4` | `2` | yes | `2`; 0 to 128 (0: no limit) | nothing | `1` | Drawing. The shine pass of near lights, the costliest pass in a firefight. Never under 2 in a network game. |
| `display.particle_limit` | `2048` | `1024` | yes | `1024`; 0 to 1024 (0: no limit) | nothing | `512` | CPU and drawing: each particle alive is moved, tested against the level and drawn. Never under 1024 in a network game, so on the original Xbox (whose most is 1024) it cuts only outside one. |
| `display.particle_ticks` | `true` | `true` | no | | | | CPU a frame: moving loose particles once a tick saves two in three of their collision tests at 60 fps. |
| `display.shadow_resolution` | nothing | `128` | no | | | | Drawing and video memory: bigger shadow maps. |
| `display.anisotropic_filtering` | nothing | `0` | no | | | | Drawing: texture sampling on the GPU. |
| `display.anti_aliasing` | nothing | `off` | no | | | | Drawing and video memory: extra samples or a filter pass. |
| `display.per_pixel_lighting` | nothing | `false` | no | | | | Drawing: lighting work for each pixel instead of each vertex. |
| `display.light_effects` | nothing | `off` | no | | | | Drawing: the projectile, muzzle and emitter lights, and their wall tests. |
| `display.resolution_scaling` | nothing | `original` | no | | | | Drawing: "original" fills 640x480 instead of the window. |
| `tuning.present_mode` | `adaptive` | nothing | yes | `vblank`; `vblank` or `adaptive` | `adaptive` | `adaptive` | Waiting, not work: "adaptive" shows a late frame at once, with a tear, instead of a refresh later. |
| `tuning.particle_thinning` | nothing | nothing | yes | `1`; 1 to 4 | nothing | `2` | Drawing: fewer sprites of loose particles; each is still moved and counted as before. In a network game only past the Xbox's 1024. |
| `tuning.effects` | nothing | nothing | yes | `full`; `full` or `reduced` or `minimal` | `reduced` | `minimal` | Drawing: "reduced" skips the shine, reflection and grass passes; "minimal" also decals and weather. Lens flares and contrails are always drawn; a network game keeps the grass and weather too. |
| `tuning.shadow_pixels` | nothing | nothing | yes | `30`; 30 to 400 | `60` | `120` | Drawing: each object's shadow redraws the level under it; an object too small for one is not lit for it either. |
| `tuning.object_cull_pixels` | nothing | nothing | yes | `0`; 0 to 16 | `4` | `8` | Drawing, and CPU a frame: small objects are neither lit nor drawn. Never units or projectiles; in a network game only debris. |
| `tuning.texture_cache` | nothing | nothing | yes | `auto`; "auto", or 19 to 512 MB (and what memory has room for) | nothing | nothing | Memory: a bigger cache loads fewer textures from the disc, and leaves less free. |

<!-- end of tuning table -->

More knobs (explosion particles, decal distance, tire tracks) come once
testers' `console.perf_log` lines show what they'd save.

<a id="a-new-renderer-to-test"></a>

## Renderers

OpenGL draws the game by default. The 64-bit Windows, Mac and Linux builds
also have bgfx, a newer renderer we're testing. It uses Metal on a Mac and
Vulkan on Windows and Linux. On Windows you can also try Direct3D 11.

To switch:

1. Open the display settings (in the game, or on the setup screen's
   DISPLAY tab).
2. Pick OPENGL, VULKAN, METAL or DIRECT3D 11 on the RENDERER row.
3. Restart the game.

Or set `renderer` under `[display]` in `config.toml` to `"gl"`, `"bgfx"`
or `"d3d11"`. For one run only, start the game with the environment
variable `HALO_RENDERER=bgfx`.

If bgfx or Direct3D 11 can't start, the game uses OpenGL and says so in
`logs/debug.txt`. The line `renderer=` in `debug.txt` says what drew the
game, and the line starting `bgfx:` names the backend and the graphics card.

Some Vulkan drivers start but run badly on older computers, so the game
skips them and uses OpenGL. Right now that's nouveau's Vulkan (NVK) on a
GeForce older than the 20 series, NVIDIA drivers older than 470, Mesa's
Vulkan for Intel Ivy Bridge, Haswell and Bay Trail graphics, and software
Vulkan. `debug.txt` has a `bgfx: Vulkan GPU` line for each card with the
reason. To try Vulkan anyway, set `vulkan_blocklist = false` under
`[display]`.

If the game shows a blank screen or closes right away on bgfx, go back to
OpenGL. See [Help](help.md#blank-screen-or-a-crash-at-start).

What we know is different from OpenGL:

- The **RENDERER** row in the display settings switches it after a restart.
- The 32-bit builds, Android, FreeBSD and the consoles don't have it. Android has a Vulkan renderer of its own to test.

If OpenGL crashes or stutters on Linux with Mesa drivers, try
`gl_buffer_ring = true` under `[display]` (`HALO_GL_BUFFER_RING=true` for one
run). It reuses three buffers for each frame's vertices instead of asking
the driver for a new one every frame. It's off by default while we test it.

Tell us how it runs. Post in the Discord with your system and graphics card,
and what looks wrong or slow next to OpenGL. Send your logs too: see
[Help](help.md#send-your-logs). The line starting `bgfx:` in `debug.txt`
names the backend and the graphics card. Screenshots of the same spot with
each renderer help a lot.
