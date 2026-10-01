# Halo: Combat Evolved for macOS, Linux, Windows and Android

[![Join our Discord](https://invidget.switchblade.xyz/9gqcHyr5km)](https://discord.gg/9gqcHyr5km)

This project is a port of the Halo: Combat Evolved decompilation to macOS,
Linux, Windows and Android, with online play built around a community game
list, [halo.milenko.org](https://halo.milenko.org). The decompilation is of
the Xbox build 2342 (`cachebeta.exe`, SHA-256
`4cc87b45f721270392a96f1674ed2b5cd4a7bb4355faeab4531d1cf1884d9520`).

<img width="1289" height="995" alt="The game on Linux" src="https://github.com/user-attachments/assets/0d3ad50f-f8b8-46cf-aef8-e3661da2a7d7" />

> **This is a fork of [cybersecurity/halo-ce-universal](https://github.com/cybersecurity/halo-ce-universal).**
> We merge its changes regularly, so that the two stay consistent (the same
> netcode and network version: players of both can play together), and we
> send our fixes for the shared code back to it as pull requests. What this
> fork adds is below.

The port starts from the decompilation of [bnunu/halo-1](https://github.com/bnunu/halo-1).
That project is a fork of [punpckhdq/halo](https://github.com/punpckhdq/halo).

## What this fork adds

- **A native macOS build**: 64-bit code for Apple silicon, as an
  application, from the same sources. Refer to
  [port/macos/README.md](port/macos/README.md).
- **An in-game game browser**: ONLINE PLAY (System Link's new name) lists
  the games hosted on the internet next to the local network's, with
  paging, and X opens the whole list. Picking a game joins it through its
  invite, as before.
- **[halo.milenko.org](https://halo.milenko.org)**, the community's game
  list: the games being hosted, with Join buttons that open the game; a
  carnage report for every game that ends, with medals; service records,
  leaderboards and profiles.
- **Confirmed players**: each copy of the game keeps a private player key,
  which confirms its player's games, so a service record follows a player
  whatever name they use. A profile on the site keeps an encrypted backup
  of the key (the site cannot read it), to restore it to a reinstalled
  game. In the game list, Y opens your profile.
- **Dedicated servers**, which anyone can run: the game hosts a playlist
  by itself, with no window and no player, and lists it on the game list.
  Refer to [server/README.md](server/README.md).

The game browser, the game list and dedicated servers are built in by
default (`HALO_GAME_BROWSER`); `python configure.py --no-game-browser`
leaves them out, as upstream's builds are.

### Status

| Platform | The game | Game browser, game list, dedicated servers |
| --- | --- | --- |
| macOS (arm64) | Yes | Yes |
| Linux (32-bit x86) | Yes | Yes |
| Windows (32-bit x86) | Yes | Not yet: in progress |
| Android (arm64) | Yes | Not yet: in progress (testers welcome) |

Players of every platform can play together, and any build, upstream's
included, can join a listed game through its Join button on the site.

## Download

This fork has no release builds yet: build the game as below. Upstream's
releases (without this fork's additions) are on its
[Releases](https://github.com/cybersecurity/halo-ce-universal/releases)
page.

## Game data

The port does not include the game data. Download an Xbox disc image
(`.xiso` or `.iso`) of Halo: Combat Evolved. All versions of the game
operate. The maps of the European (PAL) version were made for a slower
console. The port changes them to play as the North American (NTSC) maps do,
so players of the two versions can play together.

1. Start the game.
2. At the first start, the game asks for the disc image. Select it.
3. The game extracts the `maps/` folder. Then the game starts.

On Linux and Windows, the game puts `maps/` next to the executable. On macOS,
put `maps/` in the data folder (refer to the macOS README). On
Android, copy the disc image to the phone first. The app puts `maps/` in its
data folder. Refer to [port/android/README.md](port/android/README.md).

## Platforms

Each platform has its own instructions:

| Platform | Instructions |
| --- | --- |
| macOS (arm64 application, OpenGL 4.1, SDL3) | [port/macos/README.md](port/macos/README.md) |
| Linux (32-bit x86 executable, OpenGL 4.5, SDL3) | [port/linux/README.md](port/linux/README.md) |
| Windows (32-bit x86 executable, OpenGL 4.5, SDL3) | [port/windows/README.md](port/windows/README.md) |
| Android (arm64 app, OpenGL ES 3, SDL3) | [port/android/README.md](port/android/README.md) |

The Linux README also gives the controls, the settings and the multiplayer
functions. These are almost the same on all platforms.

## Multiplayer

The game can play system link games on a local network and on the internet:

- A system link game can have up to 128 players on up to 128 machines.
- Linux, Windows and Android machines can play in the same game.
- An invite link lets a machine join a game on the internet.
- A game hosted with a game browser build is listed on
  [halo.milenko.org](https://halo.milenko.org) while it runs (the
  `network.list_hosted_games` setting turns this off). Players find it in
  ONLINE PLAY, or on the site. A game that ends is kept as a carnage report.
- The invite still does the joining: no game's traffic goes through the
  game list.
- The netcode is new. Each machine moves its own player at once,
  and the host makes the decisions for the game. Refer to
  [port/linux/NETCODE.md](port/linux/NETCODE.md).

## Build the game

You do not need the Xbox SDK. The port supplies the SDK declarations that
the game uses. Refer to [port/include/xdk](port/include/xdk/README.md).

To build the game:

1. Install Python and [ninja](https://ninja-build.org/).
2. Install the tools for your platform. Refer to the README for the
   platform.
3. In the root folder of the repository, enter `python configure.py`.
4. Enter `ninja` with the target for the platform:

| Target | Result |
| --- | --- |
| `ninja macos` (on a Mac) | `build/macos/Halo.app` |
| `ninja linux` | `build/linux/halo` |
| `ninja windows` (on Windows) | `build/windows/halo.exe` and `SDL3.dll` |
| `ninja android_apk` | `port/android/app/build/outputs/apk/debug/app-debug.apk` |

If you enter `ninja` without a target, ninja builds the game for the
computer that you use.

`tools/ci_build.py` makes the same builds as GitHub Actions. For example,
enter `python tools/ci_build.py linux release`.

### Build options

Give these options to `configure.py`:

| Option | Result |
| --- | --- |
| (none) | A debug build. A failed assertion stops the game. |
| `--release` | A release build. The game does not examine assertions, as in the retail game. |
| `--portable` | The Linux and Windows builds operate on all x86-64 processors. Use this option for builds that you give to other persons. |
| `--lto=thin`, `--lto=off` | Less link-time optimization. The link is faster. |
| `--pgo=off` | No profile-guided optimization. |
| `--pgo=train` | Records a new optimization profile. Refer to "Optimization profiles". |
| `--no-game-browser` | Leaves out the game browser, the game list and dedicated servers (as upstream's builds). |

Without `--portable`, the Linux and Windows builds use all the instructions
of the processor that builds them (`-march=native`). Such a build does not
always start on a different computer.

### Optimization profiles

The builds use profiles of the game to optimize the code:

- `pgo/halo_linux.profdata` for Linux and Android.
- `pgo/halo_windows.profdata` for Windows.

The profiles need clang 22 or later. With an older clang, the builds do not
use the profiles.

To record a new profile:

1. Delete the profile.
2. Enter `python configure.py --pgo=train`.
3. Enter `ninja linux` or `ninja windows`.

The build then plays the main menu and the first minute of each campaign
level. This procedure continues for approximately 15 minutes. The game
data must be in `assets/`.
