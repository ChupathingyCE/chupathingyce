# Halo on iOS

A native arm64 build of the 64-bit port for iPhone (iOS 26), drawn with
OpenGL ES 3.0. It shares the Android port's OpenGL ES paths (`HALO_GLES`)
and its phone behaviour (`HALO_MOBILE`: saves, the data folder, the
fullscreen window); `port/linux/src` is the platform layer, as on macOS.
Tested on an iPhone 17 Pro Max (iOS 26): the menus and the campaign's
first level. Internet and system link play are untested on iOS.

## Build

On a Mac with Xcode, Homebrew's `ninja`, `cmake` and `xcodegen`:

1. SDL3 3.4.16, static, for iOS and for the simulator:

   ```sh
   git clone --depth 1 --branch release-3.4.16 https://github.com/libsdl-org/SDL.git
   cmake -S SDL -B SDL/build-ios -G Ninja -DCMAKE_SYSTEM_NAME=iOS \
     -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
     -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON \
     -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX=$HOME/dev/sdl3-ios
   cmake --build SDL/build-ios && cmake --install SDL/build-ios
   ```

   The simulator's is the same with `-DCMAKE_OSX_SYSROOT=iphonesimulator`,
   `-B SDL/build-ios-sim` and `-DCMAKE_INSTALL_PREFIX=$HOME/dev/sdl3-ios-sim`.
   Elsewhere: `HALO_IOS_SDL3` / `HALO_IOS_SIMULATOR_SDL3` for the build, and
   `HALO_SDL3_INCLUDE` in `Local.xcconfig` for Xcode.

2. The game, as a static library (`tools/ios_build.py`):

   ```sh
   python3 configure.py
   ninja ios        # build/ios/libhalo.a, for the phone
   ninja ios-sim    # build/ios-sim/libhalo.a, for the simulator
   ```

3. The app (`port/ios/app`): copy `Local.xcconfig.example` to
   `Local.xcconfig` with your Apple team and a bundle identifier of your
   own, then

   ```sh
   cd port/ios/app && xcodegen generate
   open Halo.xcodeproj
   ```

   and run it on the phone. It needs the increased memory limit and
   extended virtual addressing entitlements (the Xbox address space is
   reserved where iOS allows, 4 GB aligned, not at 1 TB).

## The game's data

The maps go in the app's Documents, `maps/` (the Files app: On My iPhone,
Halo), as `assets/maps` does on the desktop. Saves and the cache are in
the app's Library/Application Support, because the top of the app's
container is read-only on a device. `debug.txt` is in Documents.

## Controls

On-screen dual sticks (Apple's TouchController): the left moves, the right
looks, and 12 buttons cover the controller's. A game controller works too,
with Halo's own look.

Halo's stick curve and acceleration suit a stick, not a thumb that is at
full tilt at once, so while the touch controls are on the right stick turns
the view through the mouse look path instead (`ios_look.m`): a rate from the
tilt to a power, with no acceleration. The gear at the top of the screen sets
the look speed, the curve, inverted look, and aim assist.

## Invites

A `halo://join/...` link opened on the phone (Safari, Notes)
starts or switches to the game, which joins it. Joining an invite read from
the clipboard is off on iOS (`network.join_from_clipboard`), because reading
the clipboard at launch makes iOS ask for permission to paste whatever is
on it.

## Not yet

- Bink videos (the build has no FFmpeg: `bink_null.c`).
- The game browser (`HALO_GAME_BROWSER`).
- Menus by touch: the touch controls drive them as a controller does.
- The simulator's software OpenGL ES is too slow for a level; the menus run.
