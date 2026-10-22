# Known Xbox map packs

`xbox_mod_maps.json` is the library of known Xbox-format map packs and their
maps. Each pack lives in a folder of its own beside the game's `maps`,
`maps_<id>` (Halo CE+ is `maps_cex`), and its maps are played as
`<file>@<id>` (see "Xbox map packs" in the README). The game names a pack's
map by the entry whose fingerprint is its file's, so a player who doesn't
have the pack's `maplist.txt`, or the pack (a joining player without it,
for one), still sees "Abyss" from "Halo CE+". Without an entry, the game
takes the name from the pack's `maplist.txt`, then from the file's name, and
names the pack by its folder.

The library names maps only. No map files are hosted or shipped, here or
anywhere else by the project.

## A pack

```json
"packs": {
  "cex": {
    "name": "Halo CE+",
    "tag": "CE+",
    "team": "VKMT",
    "credits": "Halo CE+ X (Original Xbox) by VKMT: ...",
    "source": ""
  }
}
```

The key is the pack's id, its folder's name after `maps_`: 1 to 8 lowercase
letters and digits, never `ce`, `md`, `pc` (the Halo PC families) or `de`,
`fr`, `es`, `it` (the Xbox's language folders).

| Field | What it is |
| --- | --- |
| `aliases` | other names the pack's folder can have after `maps_` (`["ceplus", "ce+"]`: `maps_ceplus`, `maps_ce+`), 1 to 16 lowercase letters, digits, `+`, `_`, `-`; optional |
| `name` | the pack's name (`Halo CE+`) |
| `tag` | its short mark in the menus and Online Games, at most 4 characters (`CE+`) |
| `team` | the team or author that made it |
| `credits` | who made it, at most 400 characters |
| `source` | a public page about the pack (`https://...`), if any; never a download of the files |

## A map

```json
{
  "fingerprint": "92f96180",
  "pack": "cex",
  "file": "bloodgulch",
  "name": "Abyss",
  "pack_version": "1.01",
  "author": "VKMT",
  "source": ""
}
```

| Field | What it is |
| --- | --- |
| `fingerprint` | the map file's fingerprint, 8 hex digits (below) |
| `pack` | the id of its pack, one of `packs` |
| `file` | the file's name in the pack, without `.map` (`bloodgulch`); `<file>@<pack>` fits in 31 characters |
| `name` | the map's name, as the menus show it (`Abyss`) |
| `pack_version` | the pack's version this file is from, if known; else `""` |
| `author` | the map's original author or credits, if known; else `""` |
| `source` | a public page about the map (`https://...`), if any; never a download of the files |

A pack is not its folder's name: a map whose fingerprint is in the library
belongs to its pack in whatever `maps_*` folder it is, and is hosted,
announced and joined as `<file>@<id>` (`sidewinder@cex`), so two players whose
folders are named differently still match. Folders the library doesn't know
keep their names.

Every text is printable ASCII, at most 95 characters (credits 400, a tag 4).
Fingerprints are unique: each version of a pack's file is an entry of its
own, so a player with another version is told so before joining. A pack's
`ui.map` (its menus, for `game.pack_menus`) has an entry too: the game lets a
known pack's menus start the pack's own levels.

## The fingerprint

The fingerprint is the one a host sends as the map's version and a joining
player's game compares: FNV-1a (32 bits) over the file's size (4 bytes,
little endian) and 16 samples of at most 64 KB at `size / 16 * i` (the
first at the start of the file), 1 in place of 0. `tools/xbox_mod_maps.py`
computes it, and `port/linux/game/map_packs.c` the same.

## Adding a pack

1. `python3 tools/xbox_mod_maps.py --scan "path/to/pack/maps" --pack <id>`
   prints an entry for each Xbox map in the folder (its `ui.map` among them),
   named by its `maplist.txt`.
2. Add the pack to `packs` and the maps to `maps` in `xbox_mod_maps.json`,
   filling in what is known.
3. `python3 tools/xbox_mod_maps.py` writes `port/linux/game/xbox_mod_maps.h`,
   which the game is built with (`--check` tells whether it is current). Send
   both in one pull request.
