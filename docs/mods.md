# Mods

A mod is a folder of Halo Editing Kit tag files that the game plays over a
map's own tags on your machine, as a resource pack does. Today that is
sounds: a high-bitrate music pack, better weapon sounds, a dialogue pack.
Bitmaps, strings and models come next.

The game's code is `port/linux/game/mods.c` and `mod_sounds.c`, and
`port/linux/src/port_mods.c` (the mods folder and its files). The design and
most of that code are MrBruh's, from OpenCE before its license changed, with
thanks.

## A mod's folder

The mods are in the `mods` folder of the data root (the folder with
`maps/`; on Android the app's data folder). Each mod is a folder in it, with
a `mod.toml` and a `tags` folder, or a zip archive (`.pk3` or `.zip`) with
the same inside:

```
mods/
  hd_music/
    mod.toml
    tags/
      sound/music/a10/a10_01.sound
  hd_weapons.pk3
    mod.toml
    tags/sound/sfx/weapons/assault rifle/fire.sound
```

The files in `tags` have the paths of the tags they replace: a tag's name,
then its group's extension, as the Halo Editing Kit and Invader keep them.
`tags/sound/sfx/weapons/assault rifle/fire.sound` replaces the sound tag
`sound\sfx\weapons\assault rifle\fire` of every map that has one. Names are
not case-sensitive. The game only looks up the names of the map's tags among
a mod's files: a mod cannot add tags, and no tag name of a map makes a path
outside the mod. The same files work as HEK or Invader tags, so a mod made
for the HEK or MCC's tools needs no change, and a mod made here goes back
to them.

`mod.toml` says what the mod is. Every key is optional:

```toml
name = "High-bitrate music"   # what the game shows (else the folder's name)
id = "hd_music"               # what other mods name it by (else the folder's name)
version = "1.0"
authors = ["Someone", "Someone else"]   # or one string
description = "The campaign's music at 44 kHz"
maps = ["a10", "a30"]         # the maps it is for, by their files' names (else every map)
load_after = ["hd_weapons"]   # mods it plays over: they load before it
load_before = ["fixes"]       # mods that play over it: they load after it
incompatible = ["other_music"]  # mods it cannot be played with
format = 1                    # the mods' format it is made for
```

`load_after`, `load_before` and `incompatible` name other mods by their `id`
or their folder's name, not case-sensitive. A mod they name that is not in
the mods folder is passed over. `maps = []` is every map.

A folder without `mod.toml` is not a mod; an archive without one is a mod
named after its file. A mod made for a later `format` than the build's, or
whose `maps` names no map, is not played. The game logs anything else wrong
in `mod.toml` (an unknown key, a value of the wrong type) and uses the rest;
a `mod.toml` that is not TOML at all is logged, and the mod played without
what it says.

Limits: 256 mods, folder names of 63 bytes at most, 262144 files in all, 24
folders deep under `tags`, 64 maps in a mod's `maps`, and 8192 entries in an
archive.

## Sounds

A `.sound` file is the Halo Editing Kit's (version 4), with 16-bit PCM, Xbox
ADPCM or Ogg Vorbis samples, mono or stereo at 22 or 44 kHz, as `tool.exe`
and `invader-sound` write them. The game reads the whole tag, checks every
count, offset and value against the file, fills in what a map's build fills
in (the sound class's distances, the per-tick bend, the longest permutation)
and plays the samples as a Custom Edition map's: Xbox ADPCM as it is, PCM
and decoded Ogg Vorbis as PCM. Mono plays at 22 kHz (a 44 kHz mono sound is
brought to it), stereo at its own rate. A permutation's samples must fit a
sound cache block (4 MB): split long sounds as `invader-sound --split` does;
a chain of linked permutations plays whole.

A file the game cannot trust is logged with the reason and left out, and the
map's own sound plays. Sounds that are not in a map are simply not looked
for: a music pack for the whole campaign is one mod.

## The load order

Every mod in the mods folder is on until you turn it off. The mods load in
an order, and where two mods have a file for the same tag, the one loaded
later plays.

- The default order is the folders' names in alphabetical order (not
  case-sensitive), except that the mods a mod must load after (those it
  asks to load after, and those that ask to load before it) are taken to
  just before it. Mods that ask in a ring (a after b, b after a) load
  alphabetically, and the game's log says so.
- `mods/load_order.toml` keeps the order, and the mods turned off, by the
  mods' folders' names:

```toml
order = ["hd_weapons", "hd_music"]
disabled = ["hd_weapons"]
```

A mod added to the folder later is put where the default order puts it,
after the mod before it there (but on the side it asks for of the mods it
names), and is on. A mod gone from the folder is left out.

When a mod that is on names another that is on as `incompatible`, both are
played and the log says which conflict; `disabled` in `load_order.toml`
turns one off. A Mods menu that shows the order and turns mods on and off is
the next step.

## Network games

A mod is this machine's alone: nothing of it is sent to another machine.
Even so, a sound of another length changes how long the host's scripts and
AI wait, and a louder or quieter sound what a player hears of others, so
**in a network game the mods are not played** until the game can tell that
the host's content matches yours. Local games (the campaign, split screen,
solo multiplayer) play them. The log says which.

## Settings and the console

| Setting | Default | What it does |
| --- | --- | --- |
| `game.mods` (MODS in the audio settings and the Guide) | `true` | Play the mods in the mods folder. Nothing happens while the folder is empty. |

| Command | What it does |
| --- | --- |
| `mods_list` | Lists the mods in the mods folder in their load order, which are on, and how many of the map's sounds the mods play. |
| `mods_reload` | Finds the mods again and reads their files again: a changed file is read again (one that now fails a check leaves what was played before), a new one is played, and the map's tag comes back for a file that is gone. If a sound changed, all sounds stop. |
| `mods_enable false` | Plays the map's own tags until `mods_enable true` or the next map. |

The game's log (`logs/debug.txt`) says when the mods are off and why:
`mods: off (no mod in the mods folder)`, `mods: off (game.mods is false)`,
`mods: not played in a network game`.

## Making a sound tag

`invader-sound` (Invader, GPL-3.0) makes `.sound` tags from WAV or FLAC, and
`tool.exe` from the Halo Editing Kit. For a quick test,
`tools/hek_sound_tag.py` writes a 16-bit PCM `.sound` from a WAV, split into
chained permutations as `invader-sound --split` does:

```
python3 tools/hek_sound_tag.py --wav intro.wav mods/hd_music/tags/sound/music/a10/a10_01.sound
```

## Where the mods folder is

- **Linux, Windows, macOS**: `mods/` next to `maps/` in the data folder.
- **Android**: `mods/` in the app's data folder,
  `/sdcard/Android/data/dev.horrible.chupathingyce/files/mods`.
