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
| Android | `chupathingyce-android64-release.zip` | Android 9 or later, 64-bit. It installs over an older Android download and keeps your data. See [port/android/README.md](../port/android/README.md#the-native-64-bit-build). |
| Mac | `chupathingyce-macos-release.zip` | macOS 13 or later, Apple silicon or Intel. |
| FreeBSD (64-bit) | `chupathingyce-freebsd64-release.zip` | FreeBSD 14 or later, amd64. See [port/unix/README.md](../port/unix/README.md). |

The program is `chupathingy.exe` on Windows and `chupathingy` on Linux and
FreeBSD. Until 0.9.0 it was `halo.exe` and `halo`. The downloads still have a
small `halo.exe` (`halo` on Linux and FreeBSD) that starts `chupathingy`, so
shortcuts, Steam entries and scripts made for the old name keep working for
now. Point them at `chupathingy` when you can: the small one goes away in a
later release. The Mac app is still ChupathingyCE.

The game checks for new releases when it starts and asks before updating. The Linux AppImage updates itself too: the new AppImage replaces the old file.
The Windows and Linux builds are for x86 and x64 PCs; there are no ARM builds of
the game for them yet.

Your graphics need OpenGL 3.3 or later (OpenGL ES 3 on Android).

The builds aren't code signed yet, so the first start on Windows and on a
Mac needs one extra step. See [Getting started](#getting-started).

## Package managers

A build from a package manager marks the game as a packaged install. Then
the game never updates itself, and tells you to update Chupathingy with
your package manager when a new version is out. On Linux, your maps, settings and logs go in
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

## Getting started

### Windows

1. Download `chupathingyce-windows64-release.zip` and unzip it into a
   folder you can write to, such as `Documents\Chupathingy`. Not
   `Program Files`.
2. Start `chupathingy.exe`. If Windows warns about an unknown publisher,
   choose **More info**, then **Run anyway**.
3. Pick your Xbox disc image.

### Mac

1. Download `chupathingyce-macos-release.zip` and open it.
2. Move ChupathingyCE to Applications and open it.
3. If macOS won't open it, go to **System Settings > Privacy & Security**
   and choose **Open Anyway**. You only do this once.
4. Pick your Xbox disc image.

On a Mac, F3 is Mission Control. Use the backslash key to open the Delta Guide.

### Linux

1. Download `chupathingyce-linux64-release.AppImage`.
2. Make it executable: right-click it, open **Properties** and allow it to
   run as a program. Or run `chmod +x chupathingyce-linux64-release.AppImage`.
3. Start it and pick your Xbox disc image.

### Steam Deck

Do the first start in Desktop Mode. Game Mode can't show the file picker.

1. In Desktop Mode, download `chupathingyce-linux64-release.AppImage`.
2. Make it executable: right-click it, open **Properties**, then
   **Permissions**, and check **Is executable**.
3. Copy your Xbox disc image to the Deck.
4. Start the AppImage and pick your disc image. Wait for the maps to copy.
5. In Steam, choose **Games > Add a Non-Steam Game to My Library** and pick
   the AppImage.
6. Go back to Game Mode and play it from your library.

Updates work in Game Mode. The main menu says when a new version is out,
and you install it in Settings > Online > Updates.

### Android

1. Download `chupathingyce-android64-release.zip` and unzip it. Install the
   `.apk` inside. Your phone may ask you to allow installs from your browser
   or file manager first.
2. Copy your Xbox disc image to the phone.
3. Start Chupathingy and pick the disc image.

You need Android 9 or later on a 64-bit phone or tablet. A controller
works, and so do touch controls. More in
[port/android/README.md](../port/android/README.md).

### FreeBSD

Download `chupathingyce-freebsd64-release.zip`, unzip it, and start
`chupathingy`. See [port/unix/README.md](../port/unix/README.md).

## Where your files go

Your maps, settings (`config.toml`) and logs go in one folder. Your saves go
in another. On most systems they are the same folder.

| Platform | Maps, settings and logs | Saves |
| --- | --- | --- |
| Windows | next to `chupathingy.exe` | `%APPDATA%\ChupathingyCE` |
| Mac | `~/Library/Application Support/ChupathingyCE` | the same folder |
| Linux, the AppImage | `~/.local/share/chupathingyce` | the same folder |
| Linux, the zip | next to the `chupathingy` program | `~/.local/share/chupathingyce` |
| Steam Deck | `~/.local/share/chupathingyce` | the same folder |
| FreeBSD | next to the `chupathingy` program | `~/.local/share/chupathingyce` |
| Android | `/sdcard/Android/data/dev.horrible.chupathingyce/files` | the same folder |

On Linux and FreeBSD, `~/.local/share` is `$XDG_DATA_HOME` if you set it. The
setup screen's SETTINGS has a PATHS tab that shows each folder.

The first start copies your profiles, saved games and game types from the
folder earlier versions used (`halo-linux`, on Linux and on a Mac, or
`%APPDATA%\halo` on Windows), which is left as it was. The 64-bit and
32-bit builds use the same places, so switching from one to the other keeps
your saves and settings.

**Common mistake:** the `maps` folder must hold the Xbox maps from your disc
image, not Halo PC's. Halo PC maps go in folders of their own beside it:
`maps_ce`, `maps_md` and `maps_pc` ([docs/maps.md](maps.md)). On Windows, Mac
and Linux, if `maps` holds Halo PC maps or has no `ui.map`, the game opens
the setup screen on MAPS and says what's wrong.

Beside the maps folders, the game keeps folders of its own:

| Folder | What is in it |
| --- | --- |
| `logs` (beside `maps`) | The game's logs: `debug.txt`, crash reports waiting to be sent, and on Windows `halo.log`. Attach these to a bug report ([Send your logs](help.md#send-your-logs)). If you set a custom save folder (`paths.saves`), the logs go in its `logs` folder instead. |
| `screenshots` | Your screenshots (F9). |
| `clips` | Your clips (F10, when clips are on). |
| `data` (beside `maps`) | Things you add that are not maps: your own font in `data/fonts` (`display.high_res_font`). |
| `cache` (in the save folder) | What the game can make again; safe to delete. |

Until 0.8.0, `debug.txt` was beside `maps` (and on Windows `halo.log` beside
the program). The game leaves an old one where it is.

## Which build

- **Releases** are the stable builds, on the [Releases page](https://github.com/ChupathingyCE/chupathingyce/releases/latest). The game updates itself from them.
- **Nightly** is the [`nightly` pre-release](https://github.com/ChupathingyCE/chupathingyce/releases/tag/nightly): main, built every day at 04:00 UTC when it has changed. For early birds.
- **Testing** builds come before a release, for testers. They're pre-releases named "(test builds)" on the [Releases page](https://github.com/ChupathingyCE/chupathingyce/releases).

### Update channels

The update channel picks which builds the game updates to. Set it in Settings > Online > Updates, on the setup screen's Updates tab, or as `channel` in the `[update]` section of `config.toml`:

| Channel | Gets |
|---|---|
| `release` (the default) | releases |
| `testing` | test builds and releases |
| `nightly` | nightlies, test builds and releases |

A test build or a nightly starts on its own channel. Each channel offers the newest build it can get, and never one older than the build you have. So switching from Testing back to Release keeps your test build until the next release that is newer. The consoles don't update themselves and have no channels.

What's coming is on the [milestones](https://github.com/ChupathingyCE/chupathingyce/milestones). Past releases' notes are in [release-notes](release-notes).
