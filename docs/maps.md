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
It opens by itself when your Xbox maps are missing. To open it any other time:

- hold **Shift** (or **Back**/**View** on a controller) while the game starts,
- pick **SETUP** in the game's settings, or
- start the game with `--setup`.

In Steam Deck Game Mode it only opens when you ask for it.

**Settings** has the settings you may want before the game starts, such as the renderer and the update channel.

**Maps** lists every kind of map you have, with its state: Ready, how many maps, or what's missing. Pick one to open
its folder, check its maps, or add maps from a disc image or installer. You can also drop a disc image on the window.

**Logs & help** opens your logs folder, copies your version, and sends your logs to dump.chupathingy.org. It asks
each time whether to send your `config.toml` too. Addresses are taken out first. You get a short code to paste in
Discord with your report.

**Play** lists the ways you can play: Vanilla Halo, and each map pack that has its own menus (a `maps_<pack>` folder
with its `ui.map`, such as Halo CE+ in `maps_cex`). Stubbs the Zombie is there too, as an early test, if you put
your own Stubbs Xbox maps in `maps_stubbs`. Once you have more than one, the game asks once whether to pick
each time or always play the one you have. **Start in this mode** changes that later, and X makes the selected mode
the default. `--play` starts the default mode without asking, and `--mode cex` (or `--mode vanilla`) starts that one.

`launcher.show_at_start = "always"` in `config.toml` opens it every time. `launcher.hold_to_open = false` turns the held key off.

## Halo PC maps

Every Chupathingy build also plays Halo PC multiplayer maps: Windows
(32-bit and 64-bit), Mac, Linux (32-bit and 64-bit), Android and the
dedicated server. The original Xbox test builds play Custom Edition maps too,
and their Map Browser (Multiplayer > MAP BROWSER) downloads them. Each kind has a folder of its own beside the game's
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
| Linux | next to the `chupathingy` program; for the AppImage, `~/.local/share/chupathingyce/` |
| Windows | next to `chupathingy.exe` |
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
repository?", with Yes, No and Don't ask again. HaloNet's locator only
finds files whose names are all lowercase, so when it doesn't have the map
the game also asks for `maps/<name>.zip` by each spelling HaloNet's own
listing has (`BattleCraft.zip` for a host playing `battlecraft`). The
download shows its progress and can be cancelled. Every download is written
into `maps_quarantine` (beside `maps_ce`) and stays there until it passes.
Before the map goes in `maps_ce`, the game checks it: the quick checks a map
from a host gets (its tag data's offsets and tables), and no more than 200 MB, downloaded and unpacked; only `<name>.map`
taken from the archive, never a path from it; its size and CRC-32 as the
archive states them; a Custom Edition multiplayer map whose header names the
map asked for; and the host's version of it (the checksum in its header,
which the host advertises with its game). It never replaces a file that is
there. Then the game joins, and the map's own checks run as for any map. A
downloaded map is listed in `maps_ce/downloaded_maps.txt`: its scripts are
held to a downloaded map's tighter rules. `network.map_downloads` in
`config.toml` (or SETTINGS > Network Setup) says `"ask"` (the
default), `"always"` or `"never"`. `"never"` means no map is ever
downloaded, but the game still fetches Chupathingy maps' small signed index
for its blacklist (below) and checks your maps against it. Desktop and Android builds download maps;
the dedicated server doesn't. HaloMD and Halo PC retail maps aren't
downloaded, and neither are Halo PC's own maps and files (Blood Gulch, Putput,
the campaign's levels, `ui`, `bitmaps` and the rest, also under a hashed
name), though the repositories have copies: they're Microsoft's and
Gearbox's, so the game asks you to copy them from your own Halo PC or Custom
Edition install instead ("Install this map yourself").

**Chupathingy maps.** If the community repositories don't have the map (or
not the host's version of it), the game asks Chupathingy maps
(maps.chupathingy.org) next. It's our own map repository, for the maps HaloNet
doesn't have. When its list has the map, the question says so: "Download
<name>.map from HaloNet or Chupathingy maps?", and the download says where
it's coming from. The game downloads from the repository only when its list
of maps and its list of mirrors are signed with its key and not expired. It refetches
them about once an hour and never goes back to an older list. It finds the map
by its name and the host's version, and downloads it from the first working
mirror. The file must have the list's size and SHA-256, or the next mirror is
asked. Then the same checks run as for a map from HaloNet.

When a download fails, the screen says what it failed on in a word or two:
Not found, Blocked (on the blacklist), Doesn't match (another version of the
map, or not the file the signed list names), Damaged (it fails the checks)
or Failed (no answer). The full reason is in `logs/debug.txt`.

**From the host.** Some maps aren't on HaloNet or Chupathingy maps. The game
can get those from the game's host instead, peer to peer, but only if you
turn it on. `network.map_downloads_from_host` in `config.toml` (MAPS FROM
HOSTS in Network Settings, and in the setup screen's Network tab) is
`"off"` by default. With `"ask"`, the game asks each time: "Download
<name>.map from the host?", with this under it:

> This map comes straight from the host, not from us,
> and we haven't checked it.

Yes downloads it. Don't ask again with Yes switches the setting to
`"always"`, and with No to `"off"`. `"always"` downloads from the host
without asking. Either way the host comes last: HaloNet first, then
Chupathingy maps, then the host. The host isn't asked when the blacklist
refused the map, when you cancelled, or when `network.map_downloads` is
`"never"`. No map comes from a host until the game has loaded the
blacklist from Chupathingy maps; without it (the site not reached yet, or
`network.maps_url` set to `""`), the game says so and doesn't download.

The game joins the host's game without a player, asks for the map file
alone and downloads it from the host alone, over the host's TCP port 5153.
Before any of it comes, the map's hash is checked against the blacklist.
Each 256 KiB piece is checked against its hash, and the whole file against
the hash the host offered. The file goes into `maps_quarantine` (beside
`maps_ce`), never straight into `maps_ce`; anything a stopped run left there is
deleted when the game starts. Once it's whole, the game runs
quick checks that take a few milliseconds: a sane file name, no larger
than `network.map_downloads_from_host_limit` (512 MB by default, 16 to
768), a Custom Edition multiplayer map's header with the map's own name,
and its tag data's offsets and tables inside the file. It must be the
host's version, and neither of its hashes may be on the blacklist. A file
that fails any of these is deleted, never changed. A file that passes is
moved into `maps_ce` as a downloaded map, and the game joins again. The
game's own checks still read all of it before it's played. The download
shows its progress and B cancels it. Nothing about the host's address goes
in the log. Desktop and Android only; the consoles and the dedicated server
don't download from hosts.

The game logs the map's name, size and hashes after a download from a
host. It doesn't send them to Chupathingy maps yet: the site has no way to
take them from the game. Until it does, its map bot finds the same maps on
the hosts it sees.

**The blacklist.** Chupathingy maps also keeps a list of maps that crash
games or break the engine, by their hashes. A map on it isn't downloaded from
anywhere, HaloNet included ("<name>.map is blocked by Chupathingy maps", and
the reason). If you already have it, Chupathingy won't play it in network
games: you can't join someone else's game on it or host one on it, and the
game tells you why when you pick it. A dedicated server skips it in its
playlist and refuses it for `sv_map`, with the reason in its log. You can
still play it by yourself or in split screen. The game fetches the list when
it starts and about once an hour after that, whatever `network.map_downloads`
says: the list is a public file, and the request carries nothing about you.
With `"never"` (and on the dedicated server) only that list is fetched, not
the mirror list. Switch back to `"ask"` or `"always"` and the mirror list is
fetched the next time the game checks, not an hour later. The game keeps the last list it took in its save folder
and reads it back when it starts, so the blacklist holds before the first
fetch and while the site can't be reached. A kept list that has expired
still blocks its maps until a newer one comes, but nothing is downloaded on
its word. The game never takes a list older than one it has taken, nor a
different one with the same number. If the site can't be reached, the game
writes one line in its log and tries again later (5 minutes, then longer,
up to an hour); you see nothing and nothing waits on it. A dedicated server
waits for the list a few seconds at most as it starts. If Chupathingy maps
ever has to stop all downloads at once, the game takes nothing from it
until it's back. `network.maps_url` in `config.toml` says where the lists
come from. Set it to `""` to turn Chupathingy maps off: nothing is fetched
from it, and no map is blocked.

**Testing Chupathingy maps.** `tools/test_map_repo.py` checks the list
reader and the downloader against a fake repository on your machine. Its
last tests run the real game. Build it with a test key in
`port/linux/src/maps_key.h`, then set `HALO_MAP_REPO_GAME` (the executable),
`HALO_MAP_REPO_KEY` (the key's PEM file), `HALO_MAP_REPO_MAP` (a Custom
Edition map) and `HALO_MAP_REPO_XBOX_MAPS` (your Xbox maps folder). The game
uses its own data folder, and `debug.map_repositories` points it at the fake
repository in place of HaloNet's.

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
