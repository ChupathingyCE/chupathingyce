# Installing Chupathingy

How to get the game, where it keeps your files, and which build to pick. The short version is on the [front page](../README.md#get-it).

## Download

Get the latest release from the [Releases page](https://github.com/ChupathingyCE/chupathingyce/releases/latest):

| Platform | Download | Notes |
| --- | --- | --- |
| Windows (64-bit) | `chupathingyce-windows64-release.zip` | Windows 10 or later, 64-bit. |
| Windows (32-bit, older PCs) | `chupathingyce-windows-release.zip` | Windows 10 or later. |
| Linux (64-bit), AppImage | `chupathingyce-linux64-release.AppImage` | One file: make it executable and run it. glibc 2.31 or later. Updates itself. |
| Linux (64-bit) | `chupathingyce-linux64-release.zip` | glibc 2.31 or later (Debian 11, Ubuntu 20.04). Brings its own SDL3. See [port/linux/README.md](../port/linux/README.md). |
| Linux (32-bit, older systems) | `chupathingyce-linux-release.zip` | glibc 2.31 or later (32-bit). Brings its own SDL3. See [port/linux/README.md](../port/linux/README.md). |
| Android | `chupathingyce-android-release.zip` | Android 9 or later, 64-bit. See [port/android/README.md](../port/android/README.md). |
| Mac | `chupathingyce-macos-release.zip` | macOS 13 or later, Apple silicon or Intel. |
| FreeBSD (64-bit) | `chupathingyce-freebsd64-release.zip` | FreeBSD 14 or later, amd64. See [port/unix/README.md](../port/unix/README.md). |

The game checks for new releases when it starts and asks before updating. The Linux AppImage updates itself too: the new AppImage replaces the old file.
The Windows and Linux builds are for x86 and x64 PCs; there are no ARM builds of
the game for them yet.

We don't pay for code signing yet, so the first start needs one extra step:

- **Windows** may warn about an unknown publisher: choose **More info → Run anyway**.
- **Mac**: move ChupathingyCE to Applications and open it. If macOS won't open
  it, go to **System Settings → Privacy & Security**, and choose **Open
  Anyway** next to ChupathingyCE. You only do this once.

## Package managers

On Arch Linux, ugoboom keeps a community package on the AUR:
[chupathingy-bin](https://aur.archlinux.org/packages/chupathingy-bin), built
from our Linux (64-bit) release. It isn't ours, so report package problems to
him.

A package marks the game as a packaged install. Then the game never updates
itself, and tells you to update Chupathingy with your package manager when a
new version is out. On Linux, your maps, settings and logs go in
`~/.local/share/chupathingyce` (or `$XDG_DATA_HOME/chupathingyce`) with your
saves, since the game's own folder can't be written. Packagers can turn it on
without rebuilding our signed executable: put a file named `packaged` next to
the executable, or start the game with `HALO_PACKAGED=1`. A build of your own
can use `configure.py --packaged`.

## You need your own copy of Halo

Chupathingy doesn't include the game's maps, sounds or art. You need an Xbox
disc image (`.iso` or `.xiso`) of Halo: Combat Evolved. Any region works.

1. Start Chupathingy.
2. The first time, it asks where to get the maps. The default is **Xbox disc
   image**: pick your image.
3. It copies the game's `maps` folder out of the image (about 2 GB), then starts.

The same screen takes the Halo PC disc and the Custom Edition installer: see [docs/maps.md](maps.md#importing-at-first-start).

## Where your files go

On Android, copy the disc image to your phone first; the maps, settings and
saves go in `/sdcard/Android/data/dev.horrible.chupathingyce/files`. On a
Mac, the maps, settings and saves go in
`~/Library/Application Support/ChupathingyCE`.
On Linux, the maps and settings (`config.toml`) go next to the `halo`
executable, and the saves in `~/.local/share/chupathingyce` (or
`$XDG_DATA_HOME/chupathingyce`). On Windows, the saves go in
`%APPDATA%\ChupathingyCE`. The first start copies your profiles, saved games
and game types from the folder earlier versions used (`halo-linux`, on Linux
and on a Mac, or `%APPDATA%\halo` on Windows), which is left as it was. The 64-bit and
32-bit builds use the same places, so switching from one to the other keeps
your saves and settings.

**Common mistake:** the `maps` folder must hold the Xbox maps from your disc
image, not Halo PC's. Halo PC maps go in folders of their own beside it:
`maps_ce`, `maps_md` and `maps_pc` ([docs/maps.md](maps.md)). On a Mac, Linux and
Windows, if `maps` holds Halo PC maps or has no `ui.map`, the game says so
and quits.

Beside the maps folders, the game keeps folders of its own:

| Folder | What is in it |
| --- | --- |
| `logs` (beside `maps`) | The game's logs: `debug.txt`, crash reports waiting to be sent, and on Windows `halo.log`. Attach these to a bug report. |
| `data` (beside `maps`) | Things you add that are not maps: your own font in `data/fonts` (`display.high_res_font`). |
| `cache` (in the save folder) | What the game can make again; safe to delete. |

Until 0.8.0, `debug.txt` was beside `maps` (and on Windows `halo.log` beside
`halo.exe`). The game leaves an old one where it is.

## Which build

- **Releases** are the stable builds, on the [Releases page](https://github.com/ChupathingyCE/chupathingyce/releases/latest). The game updates itself from them.
- **Nightly** is the [`nightly` pre-release](https://github.com/ChupathingyCE/chupathingyce/releases/tag/nightly): main, built every day at 04:00 UTC when it has changed. For early birds; it doesn't update itself.
- **Testing** builds are hand-rolled builds of a branch, published as pre-releases named "(test builds)" on the [Releases page](https://github.com/ChupathingyCE/chupathingyce/releases).
- **What's coming** is on the [milestones](https://github.com/ChupathingyCE/chupathingyce/milestones) and in the [release notes](release-notes) of the next version.
