# Maps

Chupathingy plays the Xbox maps from your disc, Halo PC and Custom Edition maps, HaloMD maps and community Xbox map packs. Each kind has its own folder.

## Importing at first start

The first-start screen takes your Xbox disc image. It also accepts your Halo PC retail CD image (`.iso`), which fills
`maps_pc`, and the Halo Custom Edition installer (`halocesetup_en_1.00.exe`),
which fills `maps_ce` (not on Android yet, where **Import maps** takes
loose `.map` files or a `.zip` of them). Or choose **Make the folders** to get empty `maps`,
`maps_ce`, `maps_pc` and `maps_md` folders and copy the files in yourself.
If you already have a maps folder somewhere else, choose **I already have a maps folder** and pick it.

The folders it makes, and what goes in each, are below.

## The setup screen

On Windows, Mac and Linux the first-start screen is part of a setup screen that runs before the game.
It opens by itself when your Xbox maps are missing. To open it any other time, hold **Shift** (or **Back**/**View**
on a controller) while the game starts, or start the game with `--setup`.

**Maps** lists every kind of map you have, with its state: Ready, how many maps, or what's missing. Pick one to open
its folder, check its maps, or add maps from a disc image or installer. You can also drop a disc image on the window.

**Logs & help** opens your logs folder, copies your version, and sends your logs to dump.chupathingy.org. It asks
each time whether to send your `config.toml` too. Addresses are taken out first. You get a short code to paste in
Discord with your report.

**Play** lists the ways you can play: Vanilla Halo, and each map pack that has its own menus (a `maps_<pack>` folder
with its `ui.map`, such as Halo CE+ in `maps_cex`). Once you have more than one, the game asks once whether to pick
each time or always play the one you have. **Start in this mode** changes that later, and X makes the selected mode
the default. `--play` starts the default mode without asking, and `--mode cex` (or `--mode vanilla`) starts that one.

`launcher.show_at_start = "always"` in `config.toml` opens it every time. `launcher.hold_to_open = false` turns the held key off.

## Halo PC maps

Every Chupathingy build also plays Halo PC multiplayer maps: Windows
(32-bit and 64-bit), Mac, Linux (32-bit and 64-bit), Android and the
dedicated server. Each kind has a folder of its own beside the game's
`maps` folder:

| Folder | What goes in it | Played online as |
| --- | --- | --- |
| `maps_ce` | Halo PC Custom Edition maps, with Custom Edition's `bitmaps.map`, `sounds.map` and `loc.map` | `<name>@ce` |
| `maps_md` | HaloMD maps (below) | `<name>@md` |
| `maps_pc` | the maps of your own Halo PC (retail) disc | `<name>@pc` |

Where the game's `maps` folder is:

| Platform | The folder holding `maps`, `maps_ce`, `maps_md`, `maps_pc` and the map packs' `maps_<pack>` |
| --- | --- |
| Mac | `~/Library/Application Support/ChupathingyCE/` |
| Linux | next to the `halo` executable |
| Windows | next to `halo.exe` |
| Android | `/sdcard/Android/data/dev.horrible.chupathingyce/files` |

Copy the `.map` files from your own Halo PC install. On Android, Import
maps (hold the app's icon, or the touch controls' Options in the game)
picks `.map` files or `.zip` archives with Android's own file picker and
puts each in its folder. Include `bitmaps.map`,
`sounds.map` and `loc.map` from Custom Edition in `maps_ce`: every Halo PC
map reads them. Halo PC's own `ui.map` there adds its map names and
pictures. The maps appear in the multiplayer map list after the Xbox maps,
marked [CE], [MD] or [PC]. In Online Games, a game on a Halo PC map is
badged, and it can be joined only by players who have that map: the game
says which file is missing and where it goes. The Xbox maps from your disc
image are still needed. Chupathingy doesn't come with any of these files.

**HD HUDs on Custom Edition maps.** Custom Edition maps can draw their HUD in
high resolution. For the HD HUD on every map that uses Custom Edition's shared
HUD, use the `bitmaps.map`, `sounds.map` and `loc.map` from Aerocatia's
[halopc-restored](https://github.com/Aerocatia/halopc-restored/releases/tag/r220)
release r220 (the "custom edition full shared" package) in `maps_ce` instead of
the stock ones; keep a copy of yours. Maps with their own HUD use theirs.
HUD meters made in the Xbox's channel order, such as halopc-restored's Xbox
order meters or a custom HUD whose meters are flagged "use Xbox shading", draw
that way.

**Playing with OpenCE players.** OpenCE plays Custom Edition maps from
a `custom_maps` folder. A game on a Custom Edition map is named the same way
for both, so their players with the map join our games and we join theirs.
HaloMD and Halo PC retail games are Chupathingy's only: a player on
OpenCE is told the map is missing. Between Chupathingy players,
the map's identity (its name and a hash of its file) is checked, so a
player with a different file of the same name is told so.

**Compressed maps.** On Windows, Mac, Linux and the dedicated server, a map
compressed with Chimera's `chimera-compress` plays like any other: put it in
its folder as it is. The first time the game needs it, it unpacks a copy
into the save folder's cache (`z/cache_compressed_<name>_....map`) and
plays that copy from then on. A new version of the map gets a new copy, and
the old one is deleted. Online, the map counts as the uncompressed file, so
players with either file play together. Downloaded maps that come
compressed are unpacked before they go in `maps_ce`. Android and the
consoles don't read compressed maps yet.

**Downloading a missing map.** When a game you join is on a Custom Edition
map you don't have, the game offers to download it from the community map
repositories (the public map repositories Halo PC players have used for
years, as Chimera downloads from them: maps.halonet.net, then its mirrors
maps2 and maps1, over HTTPS): "Download <name>.map from the community map
repository?", with Yes, No and Don't ask again. The download shows its
progress and can be cancelled. Before the map goes in `maps_ce`, the game
checks it: no more than 200 MB, downloaded and unpacked; only `<name>.map`
taken from the archive, never a path from it; its size and CRC-32 as the
archive states them; a Custom Edition multiplayer map whose header names the
map asked for; and the host's version of it (the checksum in its header,
which the host advertises with its game). It never replaces a file that is
there. Then the game joins, and the map's own checks run as for any map. A
downloaded map is listed in `maps_ce/downloaded_maps.txt`: its scripts are
held to a downloaded map's tighter rules. `network.map_downloads` in
`config.toml` (or Network Settings in the PC menus) says `"ask"` (the
default), `"always"` or `"never"`. Desktop and Android builds download maps;
the dedicated server doesn't. HaloMD and Halo PC retail maps aren't
downloaded, and neither are Halo PC's own maps and files (Blood Gulch, Putput,
the campaign's levels, `ui`, `bitmaps` and the rest, also under a hashed
name), though the repositories have copies: they're Microsoft's and
Gearbox's, so the game asks you to copy them from your own Halo PC or Custom
Edition install instead ("Install this map yourself").

**When the host changes map.** If a game you're in moves on to a Custom
Edition map you don't have (a dedicated server's playlist going to its next
map, say), the game leaves it, offers the map the same way over the main
menu (or downloads it at once, with `"always"`), and then joins the same host
again, in its next lobby or the game in progress. The game's protocol gives a
machine about a minute to load a map before the host drops it, too short for
most downloads, so the player leaves and comes back rather than holding the
game up. If the map isn't downloaded (you say no, cancel it, or it fails),
the main menu says the map is missing, as it does with `"never"`.

**Maps named with a hash.** Hosts on OpenCE name a shared Custom
Edition map `<name>_<8 hex digits>` (a game on `Sneak_af352483`, say), the
digits the start of a hash of the whole map file. Chupathingy plays such a
game from your `<name>.map` (`sneak.map`) when its hash matches, and the
downloader fetches `<name>.map` for it, checking the hash too. If your
`<name>.map` is another version, the download is kept beside it as
`<name>_<digits>.map`.

**Older folders.** Earlier Chupathingy versions kept Custom Edition maps
in `maps/ce/` and HaloMD maps in `md_maps/`, and OpenCE keeps its Custom
Edition maps in `custom_maps/`. The game still plays the maps in all three.
At its first start it offers to move `maps/ce/` to `maps_ce` and `md_maps/`
to `maps_md` (each folder is moved whole: nothing is copied or deleted, and
a folder that can't be moved stays where it is). `custom_maps/` is OpenCE's
and stays where it is. `game.move_old_map_folders` in `config.toml` says
`"ask"`, `"yes"` or `"no"`; Android moves them without asking. A map file
named `<name>@ce.map` (`@md`, `@pc`), as OpenCE names them, is
found in its folder or in `maps/` itself.

**Common mistake:** don't point `maps` itself at a Halo PC maps folder, or copy
Halo PC maps straight into it. The game can't start with Halo PC's `ui.map`
in place of the Xbox one, and Halo PC maps in `maps/` itself don't play. Keep
the Xbox maps in `maps/` and the Halo PC maps in `maps_ce`, `maps_md` or
`maps_pc`.

On Android, two limits apply. The maps need a fixed 23 MB of the app's
memory, which Android's Java runtime takes on some devices with a very large
Java heap (more than about 700 MB, such as some gaming handhelds). There
the Halo PC maps are not listed, and the log says `cannot reserve Custom
Edition maps' tag cache`. And the phone's texture cache is smaller than a
computer's, so on the heaviest maps (Foundation) some surfaces can show the
wrong textures in their busiest views. See [port/android/README.md](../port/android/README.md).

Where the game keeps its logs, cache and your own fonts: [docs/install.md](install.md#where-your-files-go).

## HaloMD maps

Chupathingy also plays HaloMD's multiplayer maps (the Mac Halo community's
maps, made for Halo PC 1.0), in every build. Bring your own: download
the maps you want from HaloMD's mod list, and put their `.map` files in
`maps_md`, beside the game's `maps` folder.

They need Custom Edition's `bitmaps.map`, `sounds.map` and `loc.map` in
`maps_ce` too: a HaloMD map keeps Halo's own textures and sounds in those
shared files, and the game reads them from Custom Edition's copies.

The maps appear in the multiplayer map list after the Halo PC maps, marked
[MD], with their names from HaloMD's list. Online, a HaloMD map is played as
`<name>@md` (`bgplus_5@md`), badged HALOMD in Online Games, and joined only by
players who have the same map file.

HaloMD's plug-ins aren't part of Chupathingy. A few maps were made for one:
the visible-object and bigger-BSP limits they raised are already raised here,
and the widescreen view is the game's own, but the maps made for gameplay
plug-ins (3rd Person, Rocket Surfing, Spartan) play without them, as plain
Halo.

## Xbox map packs

Chupathingy also plays community packs of Xbox maps, such as Halo CE+ X
(Original Xbox) by VKMT, on every build, the original Xbox's too. Such a pack
is a set of Xbox-format maps, usually saved under the Xbox's own maps' file
names (CE+'s Abyss is its `bloodgulch.map`), so each pack gets a folder of
its own beside `maps`, named for the pack, and its maps never replace or get
confused with the retail ones:

| Folder | Pack | Played online as |
| --- | --- | --- |
| `maps_cex` (also `maps_ceplus`, `maps_ce+`) | Halo CE+ | `<name>@cex` (`bloodgulch@cex`) |
| `maps_<name>` | any other pack | `<file>@<name>` |

To install a pack, copy the contents of the pack's `maps` folder, its
`maplist.txt` included, into its folder: for Halo CE+, into `maps_cex`. Leave
your retail maps in `maps`. Chupathingy doesn't host or ship pack files: get
the pack from its release page. A pack isn't its folder's name: a map the
library knows is its pack's in any `maps_*` folder, so players whose folders
are called differently still play together.

The pack's maps appear in the multiplayer map lists after the others, marked
with the pack's tag ([CE+]) and named by the library of known packs
([port/assets/maps](../port/assets/maps/README.md)), by the pack's `maplist.txt`, or by their file names.
Online they show as "Abyss" with a CE+ badge, and a player joins only with
the same version of the pack's map: one without it is told which pack and
folder it needs ("This game uses Halo CE+'s Abyss (bloodgulch.map in
maps_cex)"). OpenCE and older Chupathingy builds can't join these games.

Some packs' maps were built by another build of the Xbox's tools than the
retail maps (Halo 1: NHE's say 01.10.12.2300). They play online all the same,
since every player must have the same file. A game on one is played only
between Chupathingy players: an OpenCE player who joins is let go, and the
others are told why.

The pack's own `ui.map` isn't used: the game's menus stay the game's. To play
with the pack's own menus instead, set `game.pack_menus = "cex"` in
`config.toml` and restart. A pack folder needs a name of 1 to 8 letters and
digits, other than `ce`, `md`, `pc`, `de`, `fr`, `es` and `it`.
