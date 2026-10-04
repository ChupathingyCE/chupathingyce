# Bug sweep

A triage of the decompilation's known original bugs (the "BUG (preserved for
exact matching)" comments and the other "BUG (original)" notes), the
#bug-reports forum and recent assertion failures, with the fixes made
for every build.

## Every build

The fixes apply to every build: the 64-bit and 32-bit PC builds, Android, and
the Xbox and Xbox 360 builds (Warthog). There is no switch; where a fix
corrects a "BUG (preserved for exact matching)" note, the note is gone with the
original code. The 32-bit builds are no longer byte-identical for the units the
fixes touch (see "Verified").

The rule for what is fixed: nothing that changes what is sent over the network,
and nothing that changes what a networked peer computes (game state, AI,
physics, game engine rules, scripts' results). Those are listed only.

## Summary

| Category | Count |
| --- | --- |
| FIX (fixed on this branch) | 35 sites, 22 commits |
| FIX, in a file another branch owns (noted only) | 3 sites |
| LIST ONLY (simulation, AI, game rules, wire format) | 27 |
| NOT A BUG, or unreachable in the game | 29 |

Of the 57 "preserved for exact matching" notes: 19 fixed, 1 for another
branch, 18 listed, 19 not a bug or unreachable.

## Asserts and crashes hit recently

| Where | What | Category | Notes |
| --- | --- | --- | --- |
| `source/cache/xbox_texture_cache.c:436` (#320, `!stolen_memory`) | The network test's fast setup halted while the intro movie played. Bink takes the texture cache's spare memory (`bink_playback.c:543`) and keeps it until the movie ends; starting a map precaches it (`cache_files_precache_map_begin`, `cache_files_windows.c:740`) and takes the same memory again. Bink refuses to start during a precache, but a precache doesn't check for a movie. | FIX | Fixed in `port/linux/game/network_test.c`: the test skips the movie first, as a button press does (2d4a7def). Verified: the macOS build hosting bloodgulch with `HALO_NETWORK_TEST=host:bloodgulch` logs "skipped the movie", precaches the map and sets up the game with no halt. Anything else that starts a map while a movie plays would halt the same way (an invite joined during the intro?); a general fix belongs in `cache_files_precache_map_begin` (stop the movie first), but `bink_playback_stop` loads the main menu for these movies, so it needs care. Owner decision. |
| `source/math/periodic_functions.c:374`, `:466` | Out-of-range transition or periodic function types: the default arms assert and leave `result` unassigned. Release builds carry on past assertions (`cseries.h`), so the table gets an uninitialized value. The type comes from tag data (Halo PC maps). | FIX (other branch) | File owned by another branch; not touched. Suggested fix: assign `result` (0) in both default arms. The task named `source/game/periodic_functions.c`, which doesn't exist; this is the file. |
| `source/sound/sound_manager.c:2555` | "attempt to play a sound that was not a mono 22k compressed sound or a stereo 22k or 44k compressed sound." The Xbox's sound channels only take Xbox ADPCM mono 22 kHz or stereo 22/44 kHz (Halo PC's PCM too, with `HALO_CUSTOM_EDITION`). A mono 44 kHz sound, an IMA ADPCM one, or an Ogg Vorbis one that `ce_resources.c` couldn't decode is refused. A log line, not a crash; the stock Xbox maps don't have such sounds. | FIX (diagnostic) | The message now names the sound tag and its compression, encoding and sample rate (e859daf0), so the next report says which map has it. Playing mono 44 kHz sounds would mean resampling them when Halo PC maps are loaded (`port/linux/game/ce_resources.c`), part of the paused Halo PC maps work. |

## The preserved bugs

Line numbers are `fork/main`'s.

### Fixed

| File:line | What | Commit |
| --- | --- | --- |
| `hs/hs_library_external.c:168` | Script `print` passes its text as the format; a `%` reads arguments never passed. | 8b7b51dd |
| `hs/hs_runtime.c:2160` | Script `inspect`, the same. | 8b7b51dd |
| `saved games/saved_game_files.c:1883` | A stray saved-games folder's name used as the format. | c17bc148 |
| `ai/ai_script.c:2730` | `ai_migrate_and_speak` log: four `%s`, three values; the fourth reads a register or stack slot as a string (crash with `ai_print_migration`/`ai_print_scripting`). Logging only. | eba1d45d |
| `ai/ai_script.c:1568` | `ai_follow_target_ai` log prints the first name twice. Logging only. | eba1d45d |
| `ai/actor_looking.c:1122` | Debug message prints the point's z twice. | 3f3e5f1b |
| `ai/actors.c:700` | Activation check's diagnostic compares the vehicle index as a time. Only decides whether an `_error_silent` message is printed. | 3f3e5f1b |
| `rasterizer/xbox/rasterizer_xbox_profile.c:635` | `%s` given the profile index (NONE): reads -1 as a string. | 8c80017f |
| `sound/sound_dsound_xbox.c:2909` | `#%d` with no value. | faf2789f |
| `physics/breakable_surfaces.c:218` | A real printed with `%d`. Message only. | 8a46bd0f |
| `ui_widget_game_data_input_functions.c:2346`, `:2415` | Game options description index uninitialized with no focused item. | a11c6719 |
| `tag_files/files.c:359` | `datastore_read` returns an uninitialized result on failure. | 95327cbe |
| `rasterizer/rasterizer.c:2035` | `closest_debug_vertex_dot` never initialized (vertex debugging). | 407a5455 |
| `rasterizer/xbox/rasterizer_xbox_hardware_geometry.c:274` (and the vertex buffer's at :97) | Buffer pointer tested uninitialized after a failed create. | 28f0a433 |
| `render/render_contrails.c:372` | Unsupported contrail type returns with its buffers locked and allocated and the lock operation set. | b3353865 |
| `bitmaps/bitmaps.c:1131` | `lod=%f` given the short mipmap index. | e1169e05 |
| `bitmaps/bitmaps.c:1207` | Unsupported compressed format returns an uninitialized pixel (release builds continue past the assertion). Only object lighting samples bitmaps. | e1169e05 |
| `effects/effects.c:2579` | Unknown effect environment (tag data) returns an uninitialized `allowed`; now FALSE. The shipped maps never reach it; the original result was undefined, so no peer could depend on it. | a82be230 |

### For the branch that owns the file

| File:line | What | Suggested fix |
| --- | --- | --- |
| `rasterizer/xbox/rasterizer_xbox_transparent_geometry.c:2313` | Chicago extra layers: `layer_index` never advances, so the loop doesn't end for a transparent shader with extra layers (a hang, or layer 0 drawn over and over). | Increment `layer_index`. |

### Listed only (simulation)

| File:line | What | Why not fixed |
| --- | --- | --- |
| `ai/action_vehicle.c:691` | Unassigned `best_*` copied out when no seat qualifies (the caller ignores them). | AI. |
| `ai/actions.c:4180` | `alignment_vector` stored unassigned (caller doesn't read it). | AI. |
| `ai/actor_combat.c:1612` | Grenade `aim_vector` unassigned in a vehicle. | AI: changes where actors aim. |
| `ai/actor_perception.c:3867` | Squad and platoon references compared the wrong way round. | AI targeting. |
| `ai/actor_perception.c:5824` | Walks the perceiving actor's swarm, not the swarm actor's. | AI. |
| `ai/actor_perception.c:5949` | Discard loop also deletes the entry that hit the limit. | AI props. |
| `ai/ai_communication.c:2407` | `cause_point` left uninitialized. | AI dialogue choice. |
| `ai/path.c:1248`, `ai/path_smoothing.c:83`, `ai/path_structure_bsp.c:109`, `:329`, `:663` | Pathfinding reads the byte before the surface array for an open edge's NONE surface; `left_result` unassigned. No shipped BSP has an open edge; Halo PC maps might. | AI pathfinding. A NONE guard is worth doing once AI changes are allowed. |
| `ai/ai.c:2701`, `:2735` | `csmemmove` given a record count as a byte count. | `ai_handle_editing` has no callers in any build. |
| `game/game_engine.c:7743` | CTF spawn checks pass team 0 for both teams. | Map verification; a real fix needs the starting locations' team field recovered. |
| `items/projectiles.c:1199` | Air damage range takes the water upper bound. | Projectile damage. |
| `units/unit_dialogue.c:993` | Minor pain timer decremented twice, major never: major pain vocalizations stop after the first. | Unit state; also changes what is heard. |
| `hs/hs_compile.c:719` | A `cond` clause without a result isn't rejected. | Script compilation: could reject scripts upstream accepts. |

### Not a bug, or unreachable in the game

| File:line | What | Why |
| --- | --- | --- |
| `ai/actor_perception.c:3392` | The scan reads `specific_threats[9]`, which is `cumulative_threats[0]`. | Layout asserted; that byte is always zero, so the result is the same. |
| `game/game_engine_ctf.c:627` | `game_engine_get_variant()` result discarded. | No effect. |
| `hs/hs_compile.c:2879`, `bitmaps/bitmap_utilities.c:1181`, `:1368`, `:1386` | An extra unused vararg. | Harmless; the bitmap ones are tool code. |
| `interface/hud_weapon.c:874` | `result` unassigned in the default arm. | Unreachable: every crosshair state has a case. |
| `cseries/profile.c:1304` | Appends can exhaust the buffer (MSVC's `_snprintf` doesn't terminate). | The ports' `_snprintf` is `snprintf`, which does. |
| `cseries/profile.c:1653` | `result` unassigned in an unreachable arm. | Sort mode is checked first. |
| `cseries/stack_walk_windows.c:531`, `:573`, `:609`, `:632`, `:746`, `:783` | Symbol map parser bugs. | Reads `d:\cachebeta.map`, which the PC builds don't have. |
| `math/geometry.c:321`, `:331` | `geosphere_new` at one segment, and on allocation failure. | Its one caller (`random_math.c`) uses a fixed larger count. |
| `structures/leaf_map.c:1349`, `tool/connected_geometry.c:287` | Assertion misplaced; `direction` uninitialized. | Tool code (BSP compilation). |

## The other "BUG (original)" notes

| File:line | What | Category | Commit or reason |
| --- | --- | --- | --- |
| `hs/hs_runtime.c:1546`, `:1563`, `:1621`, `:1643`, `:1921`, `:1973`, `:2025` | Boolean and short results write only the low byte or word of a long. | FIX | bb239e22. Every reader takes only the low byte or word (`hs_type_sizes`), so script results are unchanged; the saved games stop carrying stack garbage. |
| `render/render_particles.c:395` | Fully culled particle group: 0/0 into the shader's secondary map radius. | FIX | 11128fd7 |
| `units/units.c:4853` | Mouth debugging uses an absent head marker. | FIX | a56597a9 |
| `camera/director.c:507` | Debug camera controls use the height variable's hyper scale for all. | FIX | 3db16e44 |
| `ai/ai_profile.c:275` | Meter history never adds the new sample. | FIX | aa409b4b (profiling display only) |
| `objects/widgets/antenna.c:300`, `objects/widgets/flags.c:446` | Movement truncated to whole units: a two-unit threshold. | FIX | 99c2db2d (visual widgets) |
| `rasterizer/xbox/rasterizer_xbox_hardware_geometry.c:97` | See above. | FIX | 28f0a433 |
| `ai/actors.c:2848` | Swarm actor index ignored. | LIST ONLY | AI. |
| `ai/ai_communication.c:6167` | Reads the slot's stored delay before writing it. | LIST ONLY | AI dialogue. |
| `ai/ai_script.c:2497` | Tests the masked index's reference type. | LIST ONLY | AI migration. |
| `ai/encounters.c:2638` | One candidate can fill both post-combat slots. | LIST ONLY | AI. |
| `game/game_engine_oddball.c:505` | Uninitialized ball position after a missing spawn (release builds continue). | LIST ONLY | Game engine. |
| `game/game_engine_race.c:440` | Marks the wrong race flag as used. | LIST ONLY | Game engine rules. |
| `game/players.c:3146` | Powerup swap compares the nearby equipment with itself. | LIST ONLY | Player actions. |
| `memory/data_packets.c:220` | A version-ineligible field takes an uninitialized or stale size. | LIST ONLY | Packet layout: the wire format. Only matters if a packet has such fields; must not change. |
| `structures/structures.c:1003` | Planar fog animation multiplied by zero. | LIST ONLY | Owner decision: turning fog motion on changes how every map looks, and the zero may be deliberate. |
| `rasterizer/rasterizer_frame_statistics.c:784` | "dynamic vertices (lit*)" stored as 0x48. | NOT A BUG | Debug text; the intended size is a guess. |
| `hs/hs.c:12251`, `:14214` | Compile error outputs passed in reverse. | NOT A BUG | The names are swapped both ways; the message and source line come out right. |
| `main/main.c:1666` | `crash` writes through NULL. | NOT A BUG | Deliberate. |
| `memory/data_encoding.c:128`, `:421`, `sound/sound_manager.c:2034` | Uninitialized after an assertion on a bad element size or spatialization mode. | NOT A BUG | Both come from code, never data; unreachable. |
| `structures/structures.c:457` | Output pointer advances past capacity. | NOT A BUG | Nothing is written past it (the count gates every write); only the pointer is formed. |
| `bitmaps/bitmap_extract.c:2117`, `:2261` | Bitmap extraction bounds and a stale pointer. | NOT A BUG | Tool code. |

## #bug-reports

One thread, "Game crashing and texture bugs" (Campaign, macOS, Crash): crashes
loading campaign missions and odd water on The Silent Cartographer. The
crashes went away after a clean reinstall of the game data; the water is
v0.6.2b's fix (54d30467, "macOS: water ripples draw to the game's frame
again"). Nothing left for this branch.

## Verified

- `ninja macos` builds, with no warnings in the changed files.
- The 32-bit Linux build (`configure.py --portable --release --pgo=off`,
  `ninja linux`, clang 19 on Debian 13) builds, with no warnings in the changed
  files.
- `tools/port_neutrality_check.py --base fork/main`: 502 of 525 units of the
  32-bit Linux build are byte-identical. The 23 that differ are exactly the
  units the fixes change: the 22 game units below and
  `port/linux/game/network_test.o`. The breakable surfaces fix changes only a
  format string (data, not code), so its unit compares identical.
  `ai/actor_looking`, `ai/actors`, `ai/ai_profile`, `ai/ai_script`,
  `bitmaps/bitmaps`, `camera/director`, `effects/effects`,
  `hs/hs_library_external`, `hs/hs_runtime`,
  `interface/ui_widget_game_data_input_functions`, `objects/widgets/antenna`,
  `objects/widgets/flags`, `rasterizer/rasterizer`,
  `rasterizer/xbox/rasterizer_xbox_hardware_geometry`,
  `rasterizer/xbox/rasterizer_xbox_profile`, `render/render_contrails`,
  `render/render_particles`, `saved games/saved_game_files`,
  `sound/sound_dsound_xbox`, `sound/sound_manager`, `tag_files/files`,
  `units/units`.
- The network test (`HALO_NETWORK_TEST=host:bloodgulch`) on the macOS build,
  with a scratch home, save root and data root: the movie is skipped, the map
  precaches and the game is set up with no halt.
