#!/bin/bash
# Builds libbinkplayer.so, the open-source Bink player of the Android port.
#
# The port has no Bink decoder (the RAD SDK is proprietary) and Android's own
# player cannot read a .bik, so this links FFmpeg's open-source Bink demuxer
# and decoders (binkvideo, binkaudio) with the player in binkplayer.c: the
# movie a Halo disc carries is decoded and drawn to a Surface. The result is
# copied where the APK's build packages it.
#
#   ANDROID_NDK=/path/to/ndk FFMPEG_SOURCE=/path/to/ffmpeg ./build.sh
#
# FFmpeg must have been built for Android arm64 first (see FFMPEG_NOTES at the
# bottom), static, with only what a movie needs.
set -e

here="$(cd "$(dirname "$0")" && pwd)"
ndk="${ANDROID_NDK:-$HOME/Android/Sdk/ndk/30.0.16248370}"
ff="${FFMPEG_SOURCE:?set FFMPEG_SOURCE to the built FFmpeg source tree}"
api="${ANDROID_API:-28}"
toolchain="$ndk/toolchains/llvm/prebuilt/linux-x86_64"
cc="$toolchain/bin/aarch64-linux-android$api-clang"

"$cc" -shared -fPIC -O2 -o "$here/libbinkplayer.so" "$here/binkplayer.c" \
    -I"$ff" \
    -Wl,--start-group \
    "$ff/libavformat/libavformat.a" "$ff/libavcodec/libavcodec.a" \
    "$ff/libswscale/libswscale.a" "$ff/libswresample/libswresample.a" \
    "$ff/libavutil/libavutil.a" \
    -Wl,--end-group \
    -laaudio -llog -landroid -lm \
    -Wl,-z,max-page-size=16384 -Wl,--no-undefined

"$toolchain/bin/llvm-strip" "$here/libbinkplayer.so"

# where the APK's build stages the native libraries
staged="$here/../../../build/android/jniLibs/arm64-v8a"
if [ -d "$staged" ]; then
    cp "$here/libbinkplayer.so" "$staged/"
    echo "staged in $staged"
fi

ls -l "$here/libbinkplayer.so"

# ---------------------------------------------------------------------------
# FFMPEG_NOTES: how the FFmpeg this links was built (aarch64, static, LGPL)
#
#   ./configure --target-os=android --arch=aarch64 --cpu=armv8-a \
#     --enable-cross-compile --enable-pic --disable-asm \
#     --cc=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang \
#     --ar=.../llvm-ar --nm=.../llvm-nm --ranlib=.../llvm-ranlib --strip=.../llvm-strip \
#     --sysroot=.../sysroot --enable-small --disable-everything \
#     --disable-programs --disable-doc --disable-avdevice --disable-avfilter \
#     --disable-postproc --disable-network --enable-avformat --enable-avcodec \
#     --enable-avutil --enable-swscale --enable-swresample \
#     --enable-demuxer=bink,binka --enable-decoder=binkvideo,binkaudio_dct,binkaudio_rdft \
#     --enable-protocol=file --enable-static --disable-shared \
#     --disable-iconv --disable-zlib --disable-bzlib --disable-lzma --disable-xlib \
#     --disable-autodetect --extra-cflags="-O2"
#
# (--disable-asm: the aarch64 assembly is not PIC and cannot be linked into a
#  shared library. The C decoders are fast enough for a 640x480 movie.)
# ---------------------------------------------------------------------------
