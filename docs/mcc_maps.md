# Halo 1 MCC custom maps

This branch adds an independent loader for modern Halo 1 MCC version-13
caches and an **MCC MAPS** choice in the map menus. It is an experimental
runtime adapter, with explicit format limits below. A successful menu
probe or file audit is not proof that a map plays correctly.

## Installation and menus

Put the map in the game data root's `mcc_maps` directory, beside `maps` and
`custom_maps`. For example:

```text
maps/                 Xbox game data
custom_maps/          Halo Custom Edition data
mcc_maps/a10.map       Halo 1 MCC custom map
mcc_maps/a10.txt       Optional plain-text description
```

The level identity is `mcc_maps\a10`. The filename is retained even when
the internal scenario name differs. The supplied Mercury Rising file is
an example: its filename is `a10.map`, while its header names
`mercury_falling`. The namespaces prevent it replacing either Xbox `a10`
or Custom Edition `custom_maps\a10`.

Open the map-kind chooser and select **MCC MAPS**. New Game lists both
campaign and multiplayer MCC maps; campaign maps proceed to difficulty
selection, and multiplayer maps can be explored alone. The network host
map screen routes campaign maps to cooperative setup and multiplayer maps
to gametypes. Split-screen multiplayer lists only multiplayer maps. These
are menu routes in the implementation; multiplayer interoperability and
complete campaigns require gameplay testing.
Each selected MCC campaign map is treated as an independent scenario.
Automatic progression through a package's campaign map sequence is not
implemented; the MCC package manifest is not interpreted.

MCC checkpoints retain the canonical `mcc_maps\<filename>` scenario
identity, including when the original tag names an Xbox campaign level.
Continue and Load Game have MCC-specific routes with separate saved-name
storage; they do not turn an MCC checkpoint into a stock campaign level.

The catalog scans only `mcc_maps`, reads each candidate's 2,048-byte header,
and refreshes when a map menu opens. It accepts at most 1,024 entries, with
filenames of at most 54 characters before `.map`. A description is optional;
the default identifies the scenario type. MCC thumbnail sidecars, Workshop
downloads and MCC package manifests are not implemented. There is no
dependency on the Custom Edition setting or its resource directories.

The loader opens source maps read-only. A rejected map reports the failing
stage in `debug.txt` and returns a menu error. Matching the header checksum
can detect that a network host selected a different map version; it is not
a recomputation or authentication of the map's contents. Network support
uses GulchCE's game protocol, not the MCC game client or its matchmaking.

## Repository architecture and isolation

GulchCE is authoritative for this work. `source/` contains the reconstructed
Xbox game systems, including scenario/tag access, cache I/O, the script
interpreter, rendering, audio, objects, game variants and UI. The native
platform layers under `port/linux/`, `port/windows/` and `port/android/`
provide the host services used by those systems. The desktop renderer
implements the game's Xbox interfaces using the existing native graphics
backend. Menu XML and generated settings define the native map chooser.
The `tools/` tests and native harnesses exercise both portable utilities and
selected game subsystems.

Existing Custom Edition support has its own catalog, cache reader,
conversion, resource and behavior modules. Those modules and their state
are not an MCC implementation dependency. The independent components are:

| Component | Responsibility |
| --- | --- |
| `port/linux/game/mcc_maps.c` | Catalog, display identities, names and descriptions. |
| `port/linux/game/mcc_cache_format.c` | Portable little-endian version-13 reader and file-range audit. |
| `port/linux/game/mcc_cache.c` | MCC path admission, read-only file lifetime, conversion and streaming dispatch. |
| `port/linux/game/mcc_main.c` | MCC load-failure recovery through the native main-menu lifecycle. |
| `port/linux/src/mcc_memory.c` | Separate 64 MiB linked tag window; never replaces an existing allocation. |
| `port/linux/game/mcc_geometry.c` | New model descriptors, model vertex/index conversion, external BSP vertex streams and node palettes. |
| `port/linux/game/mcc_audio.c` | MCC sound decoding, resampling and Xbox ADPCM encoding into an MCC virtual stream. |
| `port/linux/game/mcc_bitmaps.c` | MCC pixel layout, BC7 decoding and shader/HUD channel normalization into an MCC virtual stream. |
| `port/linux/game/mcc_tags.c` | MCC metadata, shader-type, HUD placement and widget normalization. |
| `port/linux/game/mcc_hud.c` | MCC bitmap/placement scaling, including nested weapon and grenade HUD items. |
| `port/linux/game/mcc_scripts.c` | Name-based MCC function/global linking and supported MCC native functions. |
| `port/linux/game/mcc_script_parameters.c` and `mcc_script_runtime.inl` | MCC parameter metadata/scopes and interpreter frames inside the existing HS stack. |
| `port/linux/game/mcc_objects.c` | MCC multiplayer vehicle placement masks. |
| `port/linux/game/mcc_grenades.c` | MCC slots 2/3, salted unit inventories, pickup/throw/drop behavior and HUD. |
| `port/linux/game/mcc_player.c` | MCC grenade selection/action checks and starting-profile counts. |
| `port/linux/game/mcc_network.c` | MCC-only host inventory messages and client reconciliation. |
| `port/linux/game/mcc_checkpoint.c` | MCC inventory snapshot inside proven-unused CPU save memory. |
| `port/linux/game/mcc_tag_validate.c` | Independent range/ownership state while applying stock tag schemas and their validators. |

Shared engine entry points have additive MCC dispatch for an MCC-namespaced
level or active MCC state. They route cache lifetime, raw resources, BSPs,
script extensions, validation callbacks, menu text and model palettes to
the new modules. For other levels those MCC checks do not select a loader.
The new code does not call the Custom Edition parser, converter, allocator,
script adapter, bitmap converter, sound encoder or behavior table. MCC
conversion writes only MCC-owned allocations, and unload releases them.

Maps with two through four grenade definitions are supported. The original
unit datum and player-action layouts are retained; the extra two counts
live in a separately owned table keyed by full salted unit handles.
Selection remains in the native unit fields. MCC profile, input, HUD,
pickup, throwing and inventory-network routes handle these slots.

MCC saves put the additional counts into a 65,640-byte slot at the CPU
arena's end only if the allocator's current high-water mark leaves that
entire slot unused. No `game_state_malloc` call, pool capacity, native
allocation checksum or save-image size changes. Native checkpoint, core
and persistent writers already serialize the complete CPU/GPU arena;
the persistent image checksum therefore includes this snapshot. An MCC
footer adds version, canonical map identity, cache checksum and payload
CRC32. Before accepting the image, the adapter checks the payload and
remaps incoming object-header pointers to verify every salted unit record.
Unload restores the original unused bytes, including nonzero contents.

This is an independently written implementation of documented wire formats,
informed by reading the existing engine's public interfaces and schemas.
No Custom Edition implementation was copied or renamed. Generic engine
operations, including vertex compression and GPU buffer construction, are
shared. The existing generic `stb_vorbis` library decodes Ogg, and the new
MIT-licensed `port/third_party/bcdec` dependency decodes BC7 and DXT blocks;
its accompanying license and provenance are retained. This statement does
not claim the legal two-team meaning of formal clean-room reverse
engineering.

## Wire format and conversions

The reader admits little-endian `head`/`foot` caches with version 13 and
singleplayer or multiplayer scenario type. Tag memory uses the base
inferred from the tag-array pointer minus the 40-byte MCC tag header.
Each index entry is 32 bytes. Linked pointers are checked against the
loaded data before access. File and multiplication ranges use subtraction
or wider arithmetic to avoid wrapping.

Models use the MCC `mod2` part layout and uncompressed 68-byte vertices.
The adapter creates new 104-byte Xbox part descriptors, compresses vertices
to 32 bytes and uploads the index strips. Local node indices are mapped to
global nodes when possible; models requiring more engine palette slots
use an MCC-owned palette per part. No CE palette storage is consulted.

Version-13 BSP vertex data is external to the BSP tag allocation but can be
embedded in the same map file. The adapter follows the BSP header's vertex
byte count and file offset, reads the material's 56-byte environment and
20-byte lightmap streams, and compresses them into adjacent 32-byte and
8-byte streams. The converted streams have a separate tracked lifetime;
BSP changes release only the previous BSP's buffers.

Bitmap conversion supports the common Halo pixel formats, including P8,
DXT1/3/5 and MCC's BC7 format 18. BC7 is decoded to ARGB8888 for the existing
renderer. Pixel rows, Morton ordering and cube/mip layout are packed by
the independent MCC adapter. The MCC environment bitmap flag is metadata, so it is cleared
when constructing the Xbox descriptor. Compressed and palette flags are
derived from the resulting format. Model multipurpose maps and HUD meter
maps receive MCC-specific channel conversion. A bitmap used by consumers
that require different channel layouts gets separate MCC-owned tag and
pixel copies, with the relevant consumer's reference redirected. The tag
index can grow into a separate checked allocation for those copies.

The audio adapter accepts embedded PCM16, Xbox ADPCM and Ogg Vorbis
permutations. It uses independent bounds checks and codec state. Mono
output uses the game's 22,050 Hz format; stereo output follows the admitted
22,050/44,100 Hz tag rate. Necessary conversions produce an MCC-only ADPCM
stream. Unsupported or damaged audio rejects the map instead of silently
removing sound.

Compiled script function and engine-global indices are linked by the
names retained in the script strings. The adapter provides
`objects_distance_to_object` and maps `player_effect_set_max_vibrate` to
the engine's rumble operation. Other unresolved names reject the map.
Compiled MCC static/stub scripts support up to 16 typed parameters.
Signatures, scoped local references and call arguments are checked before
loading. The MCC interpreter extension keeps arguments, mutable local
values and the evaluation cursor in the existing thread stack; object-list
references survive sleeps and release on return or thread termination.
Nested and recursive calls retain separate scopes. The existing 512-byte
stack remains the recursion/depth bound. Dynamic console calls with
parameters are not supported by this extension; the cached map's calls
are the supported path. Reserving 32,767 nodes on disk is accepted when
the active syntax fits the existing interpreter's 19,001-node limit.

## Current boundaries

- Modern uncompressed version-13 maps are the target. Earlier Anniversary
  chunk-compressed caches, Halo 2 or later MCC maps, external indexed tags,
  and shared external bitmap/sound resource files are not supported.
- The portable inspector accepts files up to `INT32_MAX`; runtime raw files
  must be smaller than `0x50000000` bytes because higher offsets identify
  MCC virtual resources. Runtime tag windows must be free, 64 KiB-aligned,
  64 MiB allocations based between `0x42000000` and `0x70000000` inclusive.
  The observed MCC base is `0x50000000`.
- There are at most 65,535 tags and 32 BSP references. Runtime conversion
  descriptors must fit below the lowest BSP in the tag window. Geometry
  currently admits at most 64 nodes per model, 22 palette nodes per part,
  256 geometries per model and 128 parts per geometry. Each part's vertex
  and triangle counts must be nonzero and at most 65,535.
- Non-volume bitmap dimensions are limited to 4,096 by 4,096 with depth one;
  volumes are limited to 512 by 512 by 256. There are at most 12
  mip levels after the base level. Individual raw/decoded allocations are
  capped at 128 MiB; the normalized bitmap stream is smaller than 512 MiB.
  Linear textures must be two-dimensional, unmipped and uncompressed.
  Audio permutations are capped at 64 MiB of input and 16,777,216 decoded
  frames, with at most 256 MiB of converted audio.
- Reusing stock schemas and gameplay/render systems does not implement
  every MCC engine extension. MCC-only object behaviors, new shader
  semantics and UI callbacks require separate
  semantic work and representative maps. MCC widget callback indices
  are cleared because they do not name Xbox callbacks; native menus
  supply navigation. HUD placement normalization and overlay handling
  still require visual comparison against the map's intended appearance.
- No claim is made here of complete campaign/gameplay fidelity or
  synchronization under all conditions, performance parity, or tested
  Android/Linux runtime behavior. See the bounded test status below.

## Validation status

The supplied `MercuryRising/halo1/maps/a10.map` was inspected without adding
its proprietary contents to the repository. Its observed inventory is:

| Field | Observed value |
| --- | --- |
| File size | 407,040,369 bytes |
| Internal name / build | `mercury_falling` / `01.06.66.HELL` |
| Version / scenario type / flags | 13 / 0 (singleplayer) / `0x0007` |
| Tag base / bytes / count | `0x50000000` / `0x012D661C` / 4,575 |
| Models / model parts | 153 / 842 |
| BSPs / BSP materials | 1 / 263 |
| Bitmap records / BC7 records | 1,122 / 38 |
| Sound permutations | 6,629, all embedded |
| Scripts / globals / parameterized scripts | 54 / 12 / 0 |
| Reserved syntax slots / active high-water count | 32,767 / 2,697 |

The parser suite passes 17 tests including this fixture. The native
geometry harness passes ten tests, including conversion of the fixture's
models and BSP, malformed vertex ranges/indices/nodes/normals, and partial
GPU allocation failure cleanup and local/global 64-node palettes. The harness uses the real engine vertex
compressor and mocked GPU buffers; it verifies that no buffers remain after
disposal. This establishes conversion and ownership behavior, not visual
correctness. Eleven media tests pass for independent Xbox ADPCM decoding,
mono/stereo encoding and resampling, BC7 known-color decoding, DXT edge
clipping, channel transforms, independent bitmap-tag copies, HUD scaling,
Morton/cube/mip packing, row padding and MCC multiplayer text normalization,
and virtual sound-stream bounds. Live Mercury
audio conversion processed 4,676 permutations into 103,256,532 ADPCM bytes;
this is a conversion-stage result, not a listening test. Its bitmap stage
converts 1,268 original/copied resources into 522,484,864 bytes. The native
MCC schema validator has 30 passing tests. Twenty-five catalog/path tests cover
the real catalog and runtime admission routines, including same-name maps
in all three namespaces, version/scenario filters, rescan, case handling,
catalog capacity, filename limits, traversal rejection and checkpoint
identity allocation/failure. Nine checkpoint menu tests verify separate
MCC, Xbox and CE routes, mixed save lists, missing-map handling and bounded
difficulty values.
Twenty-one save-image tests cover the actual MCC snapshot module and
native save entry points, including checkpoint/core round trips, stock
bypass, high-water collisions, original-byte restoration, corruption,
map/checksum mismatch and prospective unit salt/type/pointer checks.
Twenty-five generated parameter tests cover wire bounds, local scope,
call signatures, pre-publication AI casts, mutable values, nested/recursive
argument lookup, object-list ownership, disabled scripts, stack overflow
and serialized frame resumption. Neither supplied real map uses parameters;
the parameter extension's runtime evidence is from the generated harness.
Three lifecycle tests exercise the actual scenario-load, BSP-switch and
main-menu recovery functions. They check balanced time/collision state,
nonmodal initial MCC rejection, preserved inactive Xbox/CE dispatch and
later-BSP rollback, released MCC ownership, and UI recovery before any
failed-map gameplay initialization.

An independent script audit compared 748 live function calls, using 103
distinct function names, against the Xbox definitions. It found no
parameter-type drift for functions present in both. The only missing
Xbox names were the four vibrate-alias calls and six
`objects_distance_to_object` calls addressed above. Eight nonuniform
uncompressed cubemaps in the fixture confirmed mip-major storage (all six
faces within each mip): lower mip pixels matched upper mip averages to
less than 0.26/255 mean RGB error. Face-major interpretation produced
37–80/255 error. BC7 follows the same layout in the adapter; that particular
layout comparison did not independently test BC7.

The second supplied fixture, `Nitra v2/dangercanyon.map`, also passes the
parser and geometry harness. It is a 102,888,276-byte multiplayer cache,
internally `nitra_v2` with build `01.03.43.0000`. It has 2,353 tags occupying
5,911,224 bytes at `0x50000000`, 55 models, one BSP with 139 materials, 731
bitmap records (no BC7), and 992 embedded sound permutations. Its 11 scripts
and four globals have no parameters. There are 244 high-water syntax slots,
173 live nodes and 56 function calls using 21 distinct names. The only name
missing from Xbox is the vibrate alias (four calls); no parameter-type
drift or missing engine globals were found. Geometry produces 1,112 buffers
and releases all of them.

Live Mercury loading now reaches the rendered world and runs its scripts
after passing media, tag and BSP validation. The validator corrects one
genuinely empty one-frame animation in
`vehicles\falcon_destroyed\left_wing\one frame empty`: both default-pose and
frame payloads are absent, so its node count becomes zero. This is not a
conversion that discards a present animation payload. A two-process Nitra
test ran 1,020 ticks and accepted ten remote hits with no rejected hits.
That run exposed a NULL-free assertion during geometry teardown; all
nullable geometry cleanup paths were guarded, and the harness now rejects
NULL frees like the native debug allocator. The corrected build subsequently
completed an MCC-to-Xbox-to-CE-to-MCC transition in one process without
rejection or assertions. Mercury save, revert and core reload accepted a
snapshot containing four nonzero extra grenade inventories. A two-process
Mercury cooperative run lasted about 1,320 ticks, ran opening scripts and
cinematics, and spawned both players alive at matching positions. Its host
core contained eight extra grenade records; the client's save request was
refused as host-authoritative. A final cooperative run lasted about 1,350
ticks and found the same eight nonzero type-2/type-3 inventories on host
and client, including exact salted handles, at 40, 50 and 60 seconds.
Both processes shut down cleanly and the client returned to the Xbox menu.

A read-only source fixture was copied into ignored test data and its first
BSP vertex was changed to NaN. The native loader explicitly rejected that
copy, released MCC state and returned to the Xbox UI. The same process then
loaded Xbox `a10` and valid MCC Nitra, executed post-load console markers,
initialized rendering and exited successfully without assertions.

Native keyboard/mouse navigation also opened the fifth **MCC MAPS** tab,
which listed A10 and Dangercanyon with campaign/multiplayer descriptions.
A10 proceeded through difficulty selection into rendered Mercury gameplay;
Dangercanyon proceeded through Slayer, server setup and the LAN lobby into
rendered Nitra multiplayer gameplay. These bounded checks do not establish
a complete campaign playthrough. Separate
15-second native regression runs loaded Xbox `a10` and Custom Edition
`hugeass` into gameplay/BSPs without assertions. The existing `hugeass`
capacity warning was also present in a prior log; these short checks are
not exhaustive playthroughs.

The final combined MCC and existing cache-format suite passed 298 tests
with six optional-fixture skips, including the BSP lifecycle and score-hint
checks. The final native Windows build and a further rendered Nitra load
also passed. The full general legacy Linux harness could
not run with the Windows/MSVC toolchain: its POSIX thread/clock interfaces,
libm linkage and CRT syntax require the existing Linux CI environment.
No full Linux harness or Linux runtime pass is claimed here.

Run the isolated tests with a C compiler and Python/pytest available:

```text
python -m pytest -q tools/test_mcc_cache_format.py
python -m pytest -q tools/test_mcc_geometry.py
python -m pytest -q tools/test_mcc_media.py tools/test_mcc_tag_validation.py
python -m pytest -q tools/test_mcc_maps.py
python -m pytest -q tools/test_mcc_saved_games.py
python -m pytest -q tools/test_mcc_checkpoint.py tools/test_mcc_grenades.py tools/test_mcc_network.py
python -m pytest -q tools/test_mcc_parameters.py tools/test_mcc_scripts.py
python -m pytest -q tools/test_mcc_lifecycle.py
```

Set `MCC_TEST_MAP` to an existing map path to opt into the real-fixture
checks. The parser tests are portable; the geometry harness currently
requires the x86 Windows game headers/toolchain and skips elsewhere.
`port/tools/mcc_cache_report.c` builds with `mcc_cache_format.c` and only
the C standard library, so files can also be audited outside the game.
None of these commands downloads or bundles a game map.

## Public format evidence

These are wire-format references, not copied implementation code. Current
version-13 fields were checked against the supplied file where available;
older CEA documentation sometimes describes different compression and
memory limits and must not be applied indiscriminately.

| Fact | Primary source / observed check |
| --- | --- |
| Version 13, 40-byte PC tag header, 64 MiB tag capacity | [Invader map definitions](https://github.com/SnowyMouse/invader/blob/master/include/invader/hek/map.hpp), [engine configuration](https://github.com/SnowyMouse/invader/blob/master/src/hek/map.cpp); fixture tag pointer `0x50000028`. |
| Infer linked base from tag array; current v13 is uncompressed | [Invader map reader](https://github.com/SnowyMouse/invader/blob/master/src/map/map.cpp); header and index read directly from fixture. |
| MCC BSP external environment/lightmap vertex streams | [Invader BSP compiler](https://github.com/SnowyMouse/invader/blob/master/src/tag/parser/compile/scenario_structure_bsp.cpp), [build layout](https://github.com/SnowyMouse/invader/blob/master/src/build/build_workload.cpp), [BSP schema](https://github.com/SnowyMouse/invader/blob/master/src/tag/hek/definition/scenario_structure_bsp.json); fixture header has `0x2B5D0C` bytes at file offset `0x800`. |
| BC7 format 18, environment flag bit 9 and external resource flag bit 8 | [Invader bitmap schema](https://github.com/SnowyMouse/invader/blob/master/src/tag/hek/definition/bitmap.json); fixture contains 38 BC7 records and environment-flagged bitmaps. |
| MCC script parameters, expanded syntax capacity | [Invader scenario schema](https://github.com/SnowyMouse/invader/blob/master/src/tag/hek/definition/scenario.json), [MCC editing kit change documentation](https://c20.reclaimers.net/h1/h1-ek/); fixture syntax is `56 + 32767 * 20` bytes. |
| Parameter wire records and local references | [Scenario schema](https://github.com/SnowyMouse/invader/blob/master/src/tag/hek/definition/scenario.json) defines 36-byte records, maximum 16 and local-variable flag bit 4; [public script compiler](https://github.com/SnowyMouse/invader/blob/master/src/tag/parser/compile/scenario/pre_compile.cpp) confirms primitive/global/local flags and the parameter slot in node data. |
| Sound compression formats and cached sound data | [Invader sound compiler](https://github.com/SnowyMouse/invader/blob/master/src/tag/parser/compile/sound.cpp), existing Xbox sound definitions; fixture samples and tag fields checked directly. |
| Header and historical format differences | [Reclaimers map documentation](https://c20.reclaimers.net/h1/maps/), [SnowyMouse CEA format research](https://gist.github.com/SnowyMouse/39168bddd597549038a35d78aee39513); historical chunk compression is outside this implementation's scope. |
