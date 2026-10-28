# Chimera checklist

[Chimera](https://github.com/SnowyMouse/chimera) is by SnowyMouse and contributors (GPL-3.0). This page is a checklist of what it does, for comparison. No Chimera code is copied here.

Chimera patches Halo PC 1.10, Gearbox's port. Chupathingy is a different game underneath: a decompilation of the original Xbox Halo, ported to Linux, macOS, Windows and Android, with Custom Edition maps loaded on top. So many of the Halo PC bugs Chimera fixes were never in our code. The Xbox renderer, Xbox netcode rules and Xbox HUD are what we run. Other Chimera fixes deal with things our port added, like drawing faster than 30 frames a second. Those we had to fix ourselves.

Each row says what the item does, where we handle it (a file or a `config.toml` setting), and a status:

- **done**: we do it, or the Xbox code already does.
- **partial**: some of it is there.
- **todo**: not there yet.
- **n/a**: not applicable. It is a Halo PC or Windows bug or feature our game never had.
- **unverified**: we looked but could not confirm either way.

The **Maps** column says whether the item changes how a map plays or looks (yes), or is polish, settings or convenience (no).

## Counts

| Status | Items |
| --- | --- |
| done | 53 |
| partial | 13 |
| todo | 20 |
| n/a | 36 |
| unverified | 9 |
| **total** | **131** |

41 of the 131 items affect how maps play or look.

## Rendering

| Item | What it does | Chupathingy | Status | Maps |
| --- | --- | --- | --- | --- |
| Object interpolation | Draws objects between the 30 Hz ticks. | `port/linux/game/render_interpolation.c`: camera, every object's nodes and the first-person weapon blended between ticks; `display.interpolation` | done | no |
| First-person interpolation | Smooth first-person weapon at high frame rates. | `render_interpolation.c` keeps the first-person pose per tick; `display.direct_camera` turns the view on the frame the mouse moves | done | no |
| Flag and antenna interpolation | Cloth and antennas move right at high frame rates. | `source/objects/widgets/flags.c`, `antenna.c` (per-frame updates scaled to ticks) | done | no |
| Particle interpolation | Particles move smoothly between ticks. | `source/effects/particles.c` (`particle_render_position`); `display.particle_ticks` | done | no |
| Light interpolation | Dynamic lights follow objects between ticks. | No light snapshots in `render_interpolation.c`; not checked whether lights pick up the blended pose | unverified | no |
| Camera shake fix | Camera shake works at high frame rates. | `source/effects/player_effects.c` shakes once a tick from a tick-based seed | done | no |
| Contrail fix | Short contrails work at high frame rates. | `source/effects/contrails.c` emits on the first frame after each tick | done | no |
| Weather fix | Rain and snow behave at high frame rates. | `source/effects/weather_particle_systems.c` turns once per tick | done | no |
| Uncap cinematics | Cutscenes above 30 frames a second. | Cinematics are interpolated like gameplay (`render_interpolation.c`, README "Frame rate") | done | no |
| Throttle frame rate | Caps the frame rate. | `display.max_fps` | done | no |
| Video mode settings | Resolution, refresh rate, vsync, windowed, borderless. | `display.mode`, `display.resolution`, `display.window_size`, `display.vsync` | done | no |
| Anisotropic filtering | AF on level and object textures. | `display.anisotropic_filtering` on every mipmapped texture (`port/linux/src/d3d8_gl.c`, `forced_anisotropy`) | done | no |
| FOV fix | FOV no longer narrows at higher vertical resolutions. | The game always lays out 480 lines and the picture is scaled up (`d3d8_gl.c`, `halo_screen_width`), so the bug can't happen | n/a | no |
| FOV setting | Sets the field of view, with separate vehicle and cutscene values. | `display.field_of_view` (first person only); no vehicle or cutscene FOV | partial | no |
| Lock first-person model FOV | The weapon keeps its FOV when the view FOV changes. | `display.viewmodel_field_of_view` | done | no |
| Model LOD fix | Model LOD no longer depends on vertical resolution ("4K headless Chief"). | The game sees 480 lines at any resolution | n/a | no |
| Force highest model detail | Draws every model at full detail. | `display.full_model_detail` | done | no |
| Sun fix | Lens flare size no longer depends on resolution. | The game sees 480 lines at any resolution | n/a | no |
| Zoom blur fix | Zoom blur radius and quality scale with resolution. | `source/rasterizer/xbox/rasterizer_xbox_screen_effect.c` scales by `halo_screen_scale` | done | no |
| Block zoom blur | Turns the zoom blur off. | No setting (`debug.original_zoom` on Android only changes how it is drawn) | todo | no |
| Z-fighting fix | Less decal flicker far from the map's center. | Xbox z bias kept as a polygon offset (`d3d8_gl.c`, `D3DDevice_SetRenderState_ZBias`); no frustum change | partial | yes |
| shader_transparent_generic | Draws the shader type Gearbox dropped. | Xbox renderer has it (`shader_transparent_generic_preprocessor.c`, `rasterizer_xbox_transparent_geometry.c`) | done | yes |
| Gearbox shader regressions | Restores Xbox-accurate chicago, glass, water fog, effect, environment, decal and fixed-function shading. | We draw with the Xbox renderer (`source/rasterizer/xbox/`) through `d3d8_gl.c` and `nv2a_psh.c`; these were never broken | n/a | yes |
| Fog fix | Sky fog (Assault on the Control Room) works. | Xbox fog code (`rasterizer_xbox_environment_fog.c`) | n/a | yes |
| Model "detail after reflection" flag | The flag works as intended. | Xbox model shader honors it (`rasterizer_xbox_models.c`) | n/a | yes |
| Multitexture overlay fix | Sniper scope ticks blend correctly. | Xbox renderer draws them as on the Xbox | n/a | yes |
| Blue 32-bit environment maps | Fixes blue particles from A8R8G8B8 cube maps. | Gearbox renderer bug | n/a | yes |
| Index buffer fix | Fixes a Custom Edition index buffer overrun. | Gearbox renderer bug | n/a | no |
| Texture formats | Monochrome (Y8, A8Y8) and P8 bump formats. | `port/linux/src/xbox_textures.c` decodes the Xbox formats (L8, A8L8, AL8, P8) | done | yes |
| BC7 bitmaps | Draws MCC's BC7 compressed bitmaps. | `port/linux/game/ce_repairs.c` and `xbox_textures.c` (BC7 decode) | done | yes |
| Xbox channel order flag (model shaders) | Supports MCC's "use Xbox channel order" flag. | `port/linux/game/ce_models.c` (multipurpose map channels, flag bit 6) | done | yes |
| NVIDIA camo fix | Liquid camo on NVIDIA cards. | Halo Trial only | n/a | no |
| Block letterbox | Removes cutscene letterbox bars. | No setting | todo | no |

## HUD

| Item | What it does | Chupathingy | Status | Maps |
| --- | --- | --- | --- | --- |
| Widescreen fix | HUD, menus and text scaled right on wide screens. | HUD keeps to the corners of a wider window, scope elements stay centered (`source/interface/hud_draw.c`); menus centered (`d3d8_gl.c`, `halo_screen_ui_offset`) | done | no |
| High-res HUD scale flags | Halo PC's "use high resolution scale", MCC's "0.5 HUD scale", Chimera's "force HUD use high resolution scale". | `port/linux/game/ce_hud.c` | done | yes |
| HUD numbers fix | Higher-res HUD digits aren't drawn oversized. | `ce_hud.c` and `hud_draw_numbers` draw flagged digits at half size | done | yes |
| HUD meters | Xbox meter shading, plus the Xbox channel order flag on meters. | `ce_hud.c` swaps Halo PC's meter channels for the Xbox meter shader; no support found for the channel order flag on meter elements | partial | yes |
| Motion sensor fix | Radar fades right at high frame rates. | `source/interface/motion_sensor.c` updates once a tick | done | no |
| Scoreboard fade fix | Scoreboard fades right at high frame rates. | `source/game/game_engine.c` scales the fade by elapsed time | done | no |
| Name fade fix | Player names fade right at high frame rates. | Xbox names fade by distance, not over time (`source/interface/hud.c`) | n/a | no |
| Safe zones | Sets HUD margins between PC and Xbox values. | No setting found | todo | no |
| Split screen HUD | Forces the split-screen HUD. | We have real split screen; no override | todo | no |
| Gametype indicator | Hides or fixes Halo PC's gametype icon. | The Xbox HUD has none | n/a | no |
| Gametype rules on join | Hides the rules flash on join. | Halo PC behavior | n/a | no |
| "Hold F1 for score" | Hides the join hint. | Halo PC behavior | n/a | no |
| Button prompt quotes | Removes quotes in Hold "E". | Xbox prompts show button icons; key names are ours (`input.keyboard_prompts`) | n/a | no |
| Extended description fix | Fixes the profile "About" bitmap. | Gearbox menu bug | n/a | no |
| Block multitexture overlays | Hides scope ticks for broken tags. | No setting | todo | no |
| Shrink empty weapons | Draws empty weapons smaller. | Not found | todo | no |
| Show FPS | Shows the frame rate. | `display_framerate` console command | done | no |
| Show timer | Shows time since the map started. | `display.match_timer` shows time left in timed games | partial | no |
| Show coordinates | Shows your position. | `debug_camera` in the developer console | partial | no |
| Budget | Shows object and BSP polygon budgets. | `rasterizer_stats` in the developer console; not checked against Chimera's list | partial | no |
| Font overrides | Replace the game's fonts. | `display.high_res_font`, `display.high_res_font_large`, `display.high_res_text` | done | no |
| Console prompt color | Sets the console input color. | Not found | todo | no |

## Input

| Item | What it does | Chupathingy | Status | Maps |
| --- | --- | --- | --- | --- |
| Auto center fix | Vehicle look centering no longer depends on frame rate. | `source/game/player_control.c` steps autolevel by ticks | done | yes |
| Block auto center | Turns auto centering off. | The profile's Xbox autocenter option (`player_ui_autolevel_enabled`) | done | no |
| Weapon swap ticks fix | Holding action picks up weapons with the right timing. | `source/input/input_xbox.c` (`update_hold_ticks`) counts ticks held, not frames | done | yes |
| Aim assist fix | Restores aim assist for sticks. | Xbox aim assist intact; `input.mouse_aim_assist`, `network.block_mouse_aim_assist` | done | yes |
| Block mouse acceleration | Removes Halo PC's mouse acceleration. | Mouse turn is added raw after the stick's acceleration (`player_control.c`, `halo_linux_mouse_look`) | done | no |
| Mouse sensitivity | Finer horizontal and vertical sensitivity. | `input.mouse_sensitivity`, `input.mouse_vertical_sensitivity` | done | no |
| Deadzones | Sets analog deadzones. | No setting found | todo | no |
| Diagonals | Tunes stick-to-digital movement. | Halo PC's netcode makes movement digital; not checked how our network paths send stick movement | unverified | no |
| Auto uncrouch | Full-speed stick movement stops crouching, as on Xbox. | Xbox behavior for sticks; the keyboard crouches at any speed (`player_control.c`) | done | yes |
| Controller settings | Gamepad configuration. | `controls.controller_layout` (`port/linux/src/controller_layout.c`), Controls Setup | done | no |
| Hotkeys | Run commands from key combos. | Not found | todo | no |

## Network

| Item | What it does | Chupathingy | Status | Maps |
| --- | --- | --- | --- | --- |
| Custom chat | Replaces the crashy Keystone chat. | `port/linux/game/chat.c`, `port/linux/src/delta_chat.c`; `chat.*` settings | done | no |
| Chat: block IPs | Hides IPs in chat. | Addresses are redacted in logs (`log_address.c`); not checked in every chat line | unverified | no |
| Chat: block server messages | Hides server messages. | `display.server_messages` | done | no |
| Block server IP | Hides the server IP on the score screen. | The Xbox scoreboard never shows it | n/a | no |
| Descope fix | Descope when the host says you took damage. | `source/objects/damage.c` (`damage_replay_aftermath`) replays unzoom with host damage; not checked on every network path | partial | yes |
| Multi-team vehicles desync fix | No desync on servers with mixed-team vehicles. | Halo PC netcode issue | n/a | yes |
| Allow all passengers | Anyone can ride in anyone's vehicle. | Not found | todo | yes |
| Block equipment rotation | Removes Halo PC's spinning equipment. | The Xbox game has none | n/a | yes |
| Delete empty weapons | Removes empty weapons, as on Xbox. | Not found in the multiplayer code | unverified | yes |
| Timer offset fix | Equipment no longer spawns `floor(8.1n)` ticks late. | Halo PC bug | n/a | yes |
| Reduced DRM | Drops the CD key check. | No CD keys | n/a | no |
| Fast server listing | Faster master server query. | Our own Server Browser (`port/linux/game/server_browser.c`, `port/linux/src/browser.c`) | n/a | no |
| Removed update check | Skips Halo PC's update check. | Our own updater (`port/linux/src/updater.c`) | n/a | no |
| Bookmarks | Save and join favorite servers. | Not found | todo | no |
| Server history | Rejoin recent servers. | Not found | todo | no |
| Spam to join | Retry when a server is full. | Not found | todo | no |
| Spectate | Watch any player. | After death only (`port/linux/game/follow_camera.c`, `display.follow_camera`; `coop_spectate.c`) | partial | no |
| Player list | Lists players. | Chat history and player list (`controls.chat_log`) | partial | no |
| Set name and color | Change name and FFA color. | Profile screen (`port/linux/game/profile_screen.c`) | done | no |
| Server and client port | Sets the default ports. | `network.tunnel_port` | done | no |
| Bad server data fix | Guards against bad Gearbox server data. | Gearbox netcode | n/a | no |
| Tick rate | Changes the game's tick rate in local games. | `game_speed` in the developer console changes speed, not ticks | partial | yes |
| Teleport | Teleports players. | `debug_teleport_player`, `object_teleport` in the developer console | done | no |
| Devmode | Dev commands without leaving multiplayer. | The full developer console (README "Frame rate"); other hosts' games allow only harmless commands | done | no |

## Limits

| Item | What it does | Chupathingy | Status | Maps |
| --- | --- | --- | --- | --- |
| BSP polygon limit | Raised to 32767. | `HALO_PORT_MAXIMUM_RENDERED_ENVIRONMENT_SURFACES` 32767 on desktop (`port/linux/include/halo_port_capacity.h`) | done | yes |
| Visible object limit | Raised to 1024. | `HALO_PORT_MAXIMUM_RENDERED_OBJECTS` 1024 on desktop (`halo_port_capacity.h`) | done | yes |
| Draw distance | Raised to 2250 world units. | `display.far_clip_distance` up to 10000, default 1024 like the Xbox (`source/main/main.c`) | partial | yes |
| Large address aware | 4 GB for the 32-bit exe. | 64-bit builds and our own memory layout | n/a | no |
| Maps in RAM | Loads maps into memory to cut stutter. | Not checked | unverified | no |
| 128 MiB map leak fix | Stops leaking file handles on big maps. | Halo PC bug | n/a | no |

## Maps

| Item | What it does | Chupathingy | Status | Maps |
| --- | --- | --- | --- | --- |
| Custom Edition maps on retail | Plays CE maps with CE resource maps. | `port/linux/game/ce_resources.c`, `ce_models.c`, `ce_bsp.c`, `ce_hud.c`; `maps_ce/` | done | yes |
| Campaign in Custom Edition | Shows Campaign when the maps exist. | We have the Xbox campaign | n/a | no |
| Compressed maps | Loads zstd maps made with chimera-compress. | Branch `chimera-compressed-maps` | todo (in progress) | yes |
| Fast loading | Checksums a map when it loads, not every map at startup. | `port/linux/src/map_file_hash.c` hashes on demand, once a run | done | no |
| Map downloading | Downloads missing maps when you join. | `port/linux/src/map_download.c`, `network.map_downloads` | done | yes |
| Download template | Configurable download source. | Fixed list of repositories and mirrors (`map_download.c`) | partial | no |
| Custom map lobby fix | Lobby shows custom maps. | `port/linux/game/ui_map_list.c` | done | yes |
| Map compatibility list | Per-map flags for maps built around Gearbox drawing (meters, bump, old widescreen). | No per-map list | todo | yes |
| Stock tag fixups | Fixes known stock CE tags (HUD digits, scope ticks, detail flag, HUD scale). | `ce_hud.c` covers the HUD scale cases; the rest not found | partial | yes |
| Uncompressed sounds | Plays uncompressed sound tags. | PCM sounds play; mismarked ones repaired (`ce_resources.c`, `ce_sound_compression_repair`) | done | yes |
| PAS crash fix | Stops reading missing sound PAS data. | Not checked | unverified | yes |
| Biped in UI map | Fixes a crash when a biped spawns in a UI map. | Not checked | unverified | yes |
| Download retail maps | Optional download of stock maps. | Never downloaded by design (`map_archive_stock_name`) | n/a | no |

## Scripting and Lua

| Item | What it does | Chupathingy | Status | Maps |
| --- | --- | --- | --- | --- |
| Global Lua scripts | Loads scripts at startup. | Not supported | todo | no |
| Map Lua scripts | Loads scripts with a map. | Not supported | todo | yes |
| Embedded Lua | Runs scripts stored in map files. | Not supported | todo | yes |
| Reload scripts | Reloads Lua scripts. | Not supported | todo | no |

## Other

| Item | What it does | Chupathingy | Status | Maps |
| --- | --- | --- | --- | --- |
| Checkpoint fix | Checkpoints don't time out at high frame rates. | `source/main/main.c` counts ticks (`tools/test_checkpoint_timing.py`) | done | yes |
| Death reset time | Death and respawn timers count ticks, not frames. | `main.c` (`tools/test_death_timing.py`) | done | yes |
| Sane defaults | Modern default video and audio settings. | Our own defaults in `config.toml` | n/a | no |
| Removed Watson | No dead crash reporter. | Our own crash reports (`port/linux/src/crash_report.c`) | n/a | no |
| Safe mode removed | No safe mode prompt. | Windows Halo PC only | n/a | no |
| Background playback | Keeps running when unfocused. | Losing focus only stops input (`port/linux/src/sdl_platform.c`) | done | no |
| Multiple instances | Run more than one copy. | Supported (README "Play on one computer") | done | no |
| Profile and map paths | Moves profiles and maps. | `paths.saves`, `paths.data` | done | no |
| Console | Enables the console. | The developer console, on by default | done | no |
| Main menu music | Turns off menu music. | No setting | todo | no |
| Intro videos | Skips the intro videos. | Not checked | unverified | no |
| Language | Chooses the language. | `game.language` | done | no |
| Invalid command crash | Bad console commands don't crash. | The Xbox debug console | n/a | no |
| First-person reverb | Reverb on first-person sounds. | Not checked in `port/linux/src/dsound_sdl.c` | unverified | no |
| Countdown timer fix | Fixes the d40 countdown timer drawing wrong. | Halo PC bug | n/a | yes |
| Gamma | Halo PC's broken gamma, disabled. | We never touch system gamma | n/a | no |
| Registry and Keystone | Drops registry writes and Keystone. | Windows Halo PC only | n/a | no |
| Tab-out video fix | Fixes video when you alt-tab. | Windows Halo PC only | n/a | no |

## Recommended order

1. Compressed maps: finish the open branch so zstd maps load.
2. Map compatibility list and the remaining stock tag fixups, so maps built around Gearbox drawing look like their authors meant.
3. HUD meter channel order flag, to finish the HUD meter work.
4. Lua for maps (map and embedded scripts), behind a sandbox like downloaded maps' scripts have.
5. Allow all passengers and delete empty weapons, as host settings.
6. Check the unverified items: light interpolation, PAS data, UI map bipeds, first-person reverb.
7. Draw distance: decide whether to raise the default above 1024 for big maps.
8. Polish: safe zones, deadzones, hotkeys, bookmarks and server history, letterbox and zoom blur toggles.
