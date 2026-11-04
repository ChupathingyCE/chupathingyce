# iOS, iPadOS and tvOS

Jizzy's port of Chupathingy to iPhone, iPad and the Apple TV. The Vision Pro
runs the iPad app in compatibility mode. It is the 64-bit Android build of
the game (`HALO_ANDROID`, OpenGL ES 3, the touch controls) with `HALO_IOS`
where iOS differs, and `HALO_TVOS` for the Apple TV, linked into a Swift and
UIKit launcher that imports your Xbox disc image or a zip of your maps.

- `build.py`: the game as a static library, `ninja ios` and `ninja tvos`
  (`configure.py --console-port port/ios`)
- `src/`, `include/`: the entry point, the OpenGL ES helpers, the touch
  overlay (iPhone Duo's fold too) and the game's stack
- `app/`: the launcher (`app/project.yml`, made into an Xcode project by
  XcodeGen)

**Not in the nightlies or releases yet.** No workflow builds this port. Build
it yourself and sideload the `.ipa` (SideStore, AltStore, Sideloadly or
Xcode).

## Building

You need a Mac with Xcode (not just the command line tools), and
`brew install ninja cmake xcodegen`. Run these from the repository's root.
For the Apple TV, use `tvos`, `tvOS`, `lib-tvos` and the `ChupathingyCE-tvOS`
scheme in place of `ios`, `iOS`, `lib` and `ChupathingyCE`, and the
`appletvos` SDK (`Release-appletvos`) in place of `iphoneos`.

1. SDL3, the version `tools/android_build.py` pins, built for iOS:

   ```sh
   git clone --depth 1 --branch release-3.4.16 https://github.com/libsdl-org/SDL.git build/SDL3
   cmake -S build/SDL3 -B build/sdl3-ios-build -G Ninja \
     -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
     -DCMAKE_OSX_DEPLOYMENT_TARGET=16.4 -DCMAKE_BUILD_TYPE=Release \
     -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF \
     -DSDL_EXAMPLES=OFF -DSDL_CAMERA=OFF -DCMAKE_INSTALL_PREFIX="$PWD/build/sdl3-ios"
   ninja -C build/sdl3-ios-build install
   ```

2. The game, `build/ios/libhalo.a`:

   ```sh
   python3 configure.py --release --console-port port/ios --ios-sdl build/sdl3-ios
   ninja ios
   ```

3. The app. Copy in the game, SDL3, the broker list and a certificate
   bundle (Mozilla's, from curl), then make the project and build it:

   ```sh
   cd port/ios/app
   mkdir -p lib
   cp ../../../build/ios/libhalo.a ../../../build/sdl3-ios/lib/libSDL3.a lib/
   cp ../../../port/assets/network/brokers.txt Resources/
   curl -fsSL https://curl.se/ca/cacert.pem -o Resources/cacert.pem
   xcodegen generate
   open ChupathingyCE.xcodeproj
   ```

   Pick your team under Signing in Xcode and run it on your device, or build
   it unsigned for a sideloading tool:

   ```sh
   xcodebuild -project ChupathingyCE.xcodeproj -scheme ChupathingyCE \
     -configuration Release -sdk iphoneos -destination generic/platform=iOS \
     -derivedDataPath build CODE_SIGNING_ALLOWED=NO build
   mkdir -p Payload && cp -R build/Build/Products/Release-iphoneos/ChupathingyCE.app Payload/
   zip -qry ChupathingyCE-ios.ipa Payload
   ```

## What does not work yet

- LAN and system link: iOS blocks broadcast for apps from free accounts. Use
  invites, which work on the same Wi-Fi too.
- UPnP, for the same reason.
- No touch layout editor, gyro aiming or rumble.
- tvOS may clear the app's storage when the TV runs low on space.
- The Vision Pro only runs the iPad app: visionOS has no OpenGL ES for its
  own apps.

Delta knows these builds as `ios` (20) and `tvos` (21) (docs/delta.md).
