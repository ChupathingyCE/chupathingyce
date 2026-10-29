# Platforms

What each build of the game has. The downloads are in [docs/install.md](install.md#download). The console builds have repositories of their own: see [docs/building.md](building.md#console-ports).

| | Windows | Mac | Linux | Android |
| --- | --- | --- | --- | --- |
| The game | Yes | Yes, Apple silicon and Intel | Yes | Yes |
| Online Games, hosting, stats | Yes | Yes | Yes | Yes |
| Dedicated server | The game, for a test | The game, for a test | Yes: x86, x64 and arm64, and Docker | |
| Updates itself | Yes | Opens the download page | Yes, the AppImage too | Yes |
| Halo PC (Custom Edition) maps | Yes | Yes | Yes | Yes, see [docs/maps.md](maps.md#halo-pc-maps) |
| HaloMD maps | Yes | Yes | Yes | Yes, see [docs/maps.md](maps.md#halo-pc-maps) |
| Server Browser in the PC menus | Yes | Yes | Yes | Yes |
| Halo PC CD and CE installer import at first start | Yes | Yes | Yes | Not yet |
| Setup screen before the game (maps, logs) | Yes | Yes | Yes | Not yet (its own first-start screens) |
| Discord presence and Join | Yes | Yes | Yes | No |
| Choice of audio output, surround and Audio Setup | Yes | Yes | Yes | No (the system's) |
| Crash reports | Yes | Yes | Yes | Not yet |
| Intro, attract and cutscene movies (Bink, with FFmpeg) | Yes | Yes | Yes | Yes |

Some defaults differ by platform: anti-aliasing is MSAA 2x on 64-bit
computers and off on 32-bit ones and Android, and the Mac starts in a window
instead of fullscreen. The movies are the ones on your disc's `bink` folder,
which the first-start import copies; a movie that is missing is skipped. On a Steam Deck in Game Mode, the game
shows no system dialogs (they can crash there): the main menu's version line
says when a new version is out, and you update from Desktop Mode.

The FreeBSD build is the 64-bit Linux build's game, built for FreeBSD: see [port/unix/README.md](../port/unix/README.md).
