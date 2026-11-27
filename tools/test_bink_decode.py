# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""The movies' decoder plays a Bink file (port/linux/src/bink_ffmpeg.c over the
FFmpeg the build made, tools/ffmpeg_build.py).

The clip is made here, not taken from a disc or a mod: a Bink 1 file
(revision k) of three 32x16 frames, each a single gray (revision k's whole
plane fill), so that what comes out is known. The game's own entry points are
driven as bink_playback.c drives them (BinkOpen, BinkDoFrame,
BinkCopyToBuffer, BinkNextFrame, BinkClose), with a stand-in for the
platform layer, its DirectSound and SDL's clock; a file that isn't Bink and a
missing one are skipped. It runs against the FFmpeg of whichever build is in
build/ (the 32-bit Linux one in CI, with the game's ABI flags), and is skipped
with none, except where HALO_FFMPEG_REQUIRED=1 (CI), where it fails.
"""
import os
import platform
import re
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BINK_FFMPEG_C = ROOT / "port/linux/src/bink_ffmpeg.c"
LIBRARIES = ["libavformat", "libavcodec", "libswscale", "libswresample", "libavutil"]
WIDTH, HEIGHT = 32, 16
GRAYS = [40, 128, 220]
# (what the game gets: bink_ffmpeg.c converts as video range, 16 to 235)
EXPECTED = [max(0, min(255, round((gray - 16) * 255 / 219))) for gray in GRAYS]


def bink_clip(grays, width=WIDTH, height=HEIGHT):
    """A Bink 1 file, revision k: a frame a gray. Each frame's video is a
    32-bit word the decoder skips, then each plane (Y, then the two chroma
    planes) as one word: bit 0 set (the plane is filled), bits 1-8 the value
    (FFmpeg's bink.c reads the bits from the low end)."""
    frames = []
    for gray in grays:
        planes = [1 | (gray << 1), 1 | (128 << 1), 1 | (128 << 1)]
        frames.append(struct.pack("<4I", 0, *planes))
    header_size = 4 + 4 * 11 + 4 * len(frames)
    offsets, position = [], header_size
    for frame in frames:
        offsets.append(position)
        position += len(frame)
    file_size = position
    header = b"BIKk" + struct.pack(
        "<11I",
        file_size - 8,             # the file's size less the first 8 bytes
        len(frames),               # frames
        max(len(f) for f in frames),  # the largest frame
        0,                         # (unused)
        width, height,
        30, 1,                     # frames per second
        0,                         # flags: no alpha
        0,                         # audio tracks
        0)                         # (revision k's extra field)
    # (the index: each frame's offset, bit 0 marking a key frame)
    index = struct.pack(f"<{len(offsets)}I", *[offsets[0] | 1, *offsets[1:]])
    return header + index + b"".join(frames)


PLATFORM_H = r"""
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define __stdcall
typedef struct IDirectSound *LPDIRECTSOUND;
typedef struct IDirectSoundStream *LPDIRECTSOUNDSTREAM;
typedef struct { unsigned short wFormatTag, nChannels; unsigned long nSamplesPerSec, nAvgBytesPerSec;
    unsigned short nBlockAlign, wBitsPerSample; } tWAVEFORMATEX;
typedef struct { unsigned long dwMaxAttachedPackets; tWAVEFORMATEX *lpwfxFormat; } DSSTREAMDESC;
typedef struct { void *pvBuffer; unsigned long dwMaxSize; } XMEDIAPACKET;
#define WAVE_FORMAT_PCM 1
#define XMO_STATUSF_ACCEPT_INPUT_DATA 1
#define IDirectSound_CreateSoundStream(d, desc, s, u) (-1)
#define IDirectSoundStream_GetStatus(s, status) (-1)
#define IDirectSoundStream_Process(s, p, c) (-1)
#define IDirectSoundStream_Release(s) ((void)0)
static int log_lines;
static void platform_log(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    printf("\n");
    log_lines++;
}
static void platform_translate_path(const char *name, char *path, unsigned long size)
{
    snprintf(path, size, "%s", name);
}
static LPDIRECTSOUND dsound_get(void) { return NULL; }
"""

SDL_H = r"""
#include <stdint.h>
static uint64_t SDL_GetTicks(void) { return 0; }
static void SDL_Delay(uint32_t milliseconds) { (void)milliseconds; }
"""

MAIN = r"""
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(int argc, char **argv)
{
    static const int grays[] = { EXPECTED };
    unsigned char pixels[HEIGHT][WIDTH * 4];
    HBINK bink;
    int frame;

    /* the clip: its size and length, then each frame's gray, as the game copies it */
    bink = BinkOpen(argv[1], 0);
    CHECK(bink != NULL);
    CHECK(bink->Width == WIDTH && bink->Height == HEIGHT);
    CHECK(bink->Frames == sizeof(grays) / sizeof(grays[0]));
    for (frame = 0; frame < (int)bink->Frames; frame++)
    {
        int channel;

        CHECK(BinkDoFrame(bink) == 0);
        memset(pixels, 0, sizeof(pixels));
        CHECK(BinkCopyToBuffer(bink, pixels, WIDTH * 4, HEIGHT, 0, 0, 0) == 0);
        for (channel = 0; channel < 3; channel++)
        {
            int got = pixels[HEIGHT / 2][(WIDTH / 2) * 4 + channel];
            int corner = pixels[HEIGHT - 1][(WIDTH - 1) * 4 + channel];

            if (got < grays[frame] - 4 || got > grays[frame] + 4 || corner != got)
            {
                printf("frame %d channel %d: %d (corner %d), wanted %d\n", frame, channel, got, corner, grays[frame]);
                return 1;
            }
        }
        BinkNextFrame(bink);
    }
    BinkClose(bink);

    /* not a Bink file, and no file: skipped, with a line each */
    log_lines = 0;
    CHECK(BinkOpen(argv[2], 0) == NULL);
    CHECK(BinkOpen(argv[3], 0) == NULL);
    CHECK(log_lines == 2);
    /* (an empty name: no movie at all, said already) */
    CHECK(BinkOpen("", 0) == NULL && log_lines == 2);
    printf("bink_decode_test: ok\n");
    return 0;
}
"""


def built_ffmpeg():
    """The FFmpeg of a build in build/ that this machine can link a test
    program to, and the flags for it: (folder, compiler flags)"""
    machine = platform.machine().lower()
    system = platform.system()
    if system == "Darwin":
        arch = "arm64" if machine in ("arm64", "aarch64") else "x86_64"
        return ROOT / "build/macos/third_party" / f"ffmpeg-{arch}", [f"-arch", arch]
    if system == "Linux" and machine in ("x86_64", "amd64"):
        from tools.linux_build import FFMPEG_ABI_FLAGS
        candidates = [(ROOT / "build/linux64/ffmpeg", []),
                      (ROOT / "build/linux/ffmpeg", ["-m32", "-no-pie", *FFMPEG_ABI_FLAGS])]
        for folder, flags in candidates:
            if (folder / "build/libavcodec/libavcodec.a").is_file():
                return folder, flags
    return None, []


def extra_libraries(folder):
    """what FFmpeg's own configure says its libraries link with (the system's
    math library, threads, and on macOS VideoToolbox's frameworks)"""
    config = (folder / "build/ffbuild/config.mak").read_text(encoding="utf-8", errors="replace")
    flags = []
    for library in ("avformat", "avcodec", "swscale", "swresample", "avutil"):
        match = re.search(rf"^EXTRALIBS-{library}=(.*)$", config, re.M)
        if match:
            flags += match.group(1).split()
    return flags


def test_bink_clip_decodes(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("no C compiler")
    folder, flags = built_ffmpeg()
    if not folder or not (folder / "build/libavcodec/libavcodec.a").is_file():
        # (CI's tests run on a build that ships FFmpeg: a missing one there is
        # a broken build, not a reason to skip; .github/workflows/build.yml)
        if os.environ.get("HALO_FFMPEG_REQUIRED") == "1":
            pytest.fail("HALO_FFMPEG_REQUIRED: no build's FFmpeg in build/")
        pytest.skip("no build's FFmpeg in build/ (ninja macos, linux or linux64 first)")

    (tmp_path / "SDL3").mkdir()
    (tmp_path / "SDL3/SDL.h").write_text(SDL_H, encoding="utf-8")
    (tmp_path / "platform.h").write_text(PLATFORM_H, encoding="utf-8")
    # (a copy beside the stand-in platform.h, which its own include then finds)
    shutil.copy(BINK_FFMPEG_C, tmp_path / "bink_ffmpeg.c")
    program = tmp_path / "bink_decode_test.c"
    program.write_text(
        f"#define WIDTH {WIDTH}\n#define HEIGHT {HEIGHT}\n#define EXPECTED {', '.join(map(str, EXPECTED))}\n"
        '#include "bink_ffmpeg.c"\n' + MAIN, encoding="utf-8")
    clip = tmp_path / "clip.bik"
    clip.write_bytes(bink_clip(GRAYS))
    not_bink = tmp_path / "not_bink.bik"
    not_bink.write_bytes(b"RIFF" + bytes(60))
    binary = tmp_path / "bink_decode_test"
    archives = [str(folder / "build" / library / f"{library}.a") for library in LIBRARIES]
    link_group = (archives if platform.system() == "Darwin"
                  else ["-Wl,--start-group", *archives, "-Wl,--end-group"])
    build = subprocess.run(
        [cc, "-std=gnu11", "-DHALO_BINK_FFMPEG", *flags, f"-I{tmp_path}", f"-I{folder / 'src'}",
         f"-I{folder / 'build'}", str(program), "-o", str(binary), *link_group, *extra_libraries(folder), "-lm"],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(binary), str(clip), str(not_bink), str(tmp_path / "missing.bik")],
                         capture_output=True, text=True, timeout=60)
    assert run.returncode == 0 and "bink_decode_test: ok" in run.stdout, run.stdout + run.stderr
    assert 'Bink video: playing "' in run.stdout and "(32x16, 3 frames)" in run.stdout
    assert "(not a Bink movie)" in run.stdout and "(not found)" in run.stdout
