# Playlist map sets

A playlist map set says which map files a playlist plays, where each one
goes, and where a player can get it. Chupathingy's site publishes one per
verified playlist, signed, and the game reads it when you join a game of
that playlist: "This playlist uses 16 maps. 3 aren't installed (74 MB).
Download from Chupathingy?"

This page is the file format, for hosts, modders and other builds. The
format is public: anyone may read the files, check them with the keys
below, and use them.

## The files

For a playlist `<id>` the site serves two files:

    GET https://stats.chupathingy.org/v1/playlists/<id>/maps.json
    GET https://stats.chupathingy.org/v1/playlists/<id>/maps.json.sig

`maps.json` is the manifest, a JSON document of at most 65536 bytes.
`maps.json.sig` is its signature: 128 hex digits and a line feed.

`GET /v1/playlist-maps` lists the playlists that have one:

    {"playlists": [{"playlist": "c_ce_maps", "name": "[C] CE Maps",
                    "serial": 3, "files": 16, "size": 412000000, "verified": true}]}

`GET /v1/playlists/<id>/maps.draft.json` is the site's unsigned draft, built
from the playlist and the site's pack catalog, for CI to check and sign. CI
commits each signed manifest to the `playlist-maps` branch of
ChupathingyCE/chupathingyce (`playlists/<id>.json`, `playlists/<id>.json.sig`,
and `playlists/index.json`, `{"playlists": ["<id>", ...]}`). The site pulls
them from there, checks each signature and serial, and serves only
playlists its admin has marked verified. The game checks them again itself.

The game list says which playlist a game plays: field 23 of
`/v1/games.txt?fields=23` is its playlist id (empty for none). A dedicated
server names its playlist with `HALO_PLAYLIST_ID` (see
server/docs/playlists.md); the site also knows its own [C] servers'.

## The manifest

    {
      "playlist_maps": 1,
      "playlist": "c_cex",
      "name": "[C] CE+ X",
      "serial": 1,
      "issued": 1793145600,
      "packs": {
        "cex-1.01": {
          "name": "Halo CE+ X 1.01",
          "author": "VKMT",
          "credits": "Halo CE+ X (Original Xbox) by VKMT: Sean T, Ludus, Elote and others.",
          "permission": "Mirrored with VKMT's permission (2026-10-28)"
        },
        "xbox": {"name": "Halo: Combat Evolved (Xbox)", "author": "Bungie"}
      },
      "files": [
        {"map": "bloodgulch@cex", "title": "Abyss", "folder": "maps_cex",
         "file": "bloodgulch.map", "size": 24887296,
         "sha256": "7d6edf51...", "source": "chupathingy",
         "path": "packs/cex/1.01/bloodgulch.map", "pack": "cex-1.01"},
        {"folder": "maps_cex", "file": "ui.map", "size": 12288000,
         "sha256": "34cd5497...", "source": "chupathingy",
         "path": "packs/cex/1.01/ui.map", "pack": "cex-1.01"},
        {"map": "sanctuary@ce", "folder": "maps_ce", "file": "sanctuary.map",
         "size": 19000000, "sha256": "...", "source": "halonet"},
        {"map": "prisoner", "folder": "maps", "file": "prisoner.map",
         "source": "own", "pack": "xbox"}
      ]
    }

The top level:

| Field | |
|---|---|
| `playlist_maps` | The format: 1. A reader refuses any other number. |
| `playlist` | The playlist's id: 1 to 32 of `a-z`, `0-9`, `_`. |
| `name` | The playlist's name for people, 1 to 48 printable characters. |
| `serial` | 1 to 4294967294; a new manifest for the playlist has a higher one. The top byte is the key epoch, as in Delta's legacy table. |
| `issued` | When it was made, in seconds since 1970 (UTC). |
| `packs` | Optional. Packs by id (1 to 24 of `a-z`, `0-9`, `_`, `.`, `-`): `name` (required), `author`, `credits`, `permission`. Shown with the files that name them. |
| `files` | 1 to 128 entries, below. |

Each entry of `files`:

| Field | |
|---|---|
| `map` | Optional. The map's name as the game names it online: `prisoner` (Xbox), `<file>@ce` (Custom Edition), `<file>@<pack>` (an Xbox map pack, such as `bloodgulch@cex`). Left out for a pack's other files (`ui.map`, `maplist.txt`). |
| `title` | Optional. The map's name for people ("Abyss"), up to 48 printable characters. |
| `folder` | Where the file goes, next to the game's `maps` folder: `maps` (Xbox), `maps_ce` (Custom Edition) or `maps_<pack>` (an Xbox map pack: `maps_cex`; `<pack>` is 1 to 16 of `a-z`, `0-9`). |
| `file` | The file's name: 1 to 40 of `a-z`, `0-9`, `_`, `-`, `.`, not starting with `.`, no `..`, ending in `.map` or `.txt`. |
| `size` | The file's size in bytes, 1 to 1073741824. Required unless `source` is `own`. |
| `sha256` | The file's SHA-256, 64 lowercase hex digits. Required unless `source` is `own`. |
| `source` | Where to get it: `halonet`, `chupathingy` or `own`. |
| `path` | For `chupathingy` only: the file's path on `https://dl.chupathingy.org/`, 1 to 160 of `a-z`, `0-9`, `_`, `-`, `.`, `/`, no part empty or starting with `.`. |
| `pack` | Optional. A key of `packs`. Required for `own`. |

A reader ignores fields it does not know, so later versions can add some.
It refuses the whole manifest if a known field is wrong, a `pack` names no
pack in `packs`, or two entries have the same `folder` and `file`.

### Sources

- `halonet`: the community map repositories (maps.halonet.net and its
  mirrors, as Chimera uses them), asked for the file by its name, as the
  game's single-map downloader does. Only for `maps_ce`, and never for one
  of Halo PC's own maps or files (bloodgulch, ui, bitmaps, ...): those are
  the player's to copy from their own install.
- `chupathingy`: Chupathingy's mirror, `https://dl.chupathingy.org/<path>`,
  the file itself (not an archive). Only files whose authors said yes are
  mirrored, and never into `maps`.
- `own`: never downloaded. The player brings their own copy (the Xbox's
  maps from their own disc, Halo PC's from their own install). The game
  says "Needs your own copy of <pack name>". `size` and `sha256` may be
  given, but a different copy is not refused: it is the player's own.

Retail maps (the Xbox's, Halo PC's) are never hosted by Chupathingy.

## The signature

The signature is Ed25519, made with Delta's keys: the same primary and
recovery keys that sign Delta's legacy table (port/linux/src/delta_key.h;
keys/delta.pub.json in ChupathingyCE's command repository), with the same
epoch rule (the primary key signs serials of epoch 0 only).

What is signed is the text `chupathingy-playlist-maps 1`, a line feed, and
then `maps.json`'s exact bytes. The prefix keeps a manifest's signature from
ever passing as a legacy table's, and a table's as a manifest's.

The private keys live only in CI's secrets. `tools/playlist_maps.py`
checks, makes and signs manifests:

    tools/playlist_maps.py check maps.json
    tools/playlist_maps.py sign --key KEY.pem maps.json      (writes maps.json.sig)
    tools/playlist_maps.py verify [--public-key HEX] maps.json

## What the game does

When you pick a game whose playlist has a manifest, before joining:

1. It fetches the manifest and its signature from the site, checks the
   signature and the manifest, and drops it if either fails (it says so in
   debug.txt; the join goes on as before).
2. It looks for each file in its folder. A file counts as installed when it
   is there with the listed size. A file that is there with another size is
   a different version: it is never replaced, and is shown as such.
3. If files are missing, it asks: "This playlist uses N maps. M aren't
   installed (X MB): ... Download from Chupathingy?", with Yes, No and
   Don't ask again. `own` files are listed as "Needs your own copy of
   <pack>".
4. Yes downloads each missing file from its source into a temporary file,
   checks its size and SHA-256 against the manifest, and only then moves it
   into place. A file that does not match is deleted. Nothing outside the
   manifest's folders and names is ever written.
5. Then it joins the game. No still joins it, with whatever maps you have.

`network.playlist_downloads` in config.toml decides: `"ask"` (the
default), `"always"` (download without asking) or `"never"` (don't check
playlists). It is separate from `network.map_downloads`, which is about a
single missing Custom Edition map.

Desktop and Android builds do this. Console builds don't yet.
