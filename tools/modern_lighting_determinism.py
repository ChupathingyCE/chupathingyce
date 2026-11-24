#!/usr/bin/env python3
"""tools/test_determinism.py with ray tracing and glow on. The recording is
made as that test makes it (no window, the dummy video driver); the two
replays draw, headless on the GPU (SDL's offscreen video, EGL), with
display.ray_tracing "ao" (at display.ray_tracing_quality --quality),
display.hdr and display.glow "shafts". Every tick's hashes must agree: the
passes are drawing only. Each replay's log must show that the passes ran
(Vulkan's side path and OpenGL's targets). SDL_HINT_EGL_DEVICE picks the GPU
by its index (eglinfo -B lists them).

    unshare -rn sh -c 'ip link set lo up && python3 tools/modern_lighting_determinism.py \\
        --binary build/linux64/chupathingy --maps ~/maps --work /tmp/det'

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import test_determinism as determinism  # noqa: E402

LIGHTING = {
    "SDL_VIDEO_DRIVER": "offscreen",
    "SDL_VIDEODRIVER": "offscreen",
    "SDL_HINT_EGL_DEVICE": os.environ.get("SDL_HINT_EGL_DEVICE", "0"),
    "HALO_RAY_TRACING": "ao",
    "HALO_HDR": "true",
    "HALO_GLOW": "shafts",
    "HALO_DISPLAY_MODE": "windowed",
    "HALO_WINDOW_SIZE": "1280x720",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--maps", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--ticks", type=int, default=determinism.DEFAULT_TICKS)
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("--quality", default="auto", choices=("auto", "low", "medium", "high"))
    args = parser.parse_args()
    LIGHTING["HALO_RAY_TRACING_QUALITY"] = args.quality
    original_play = determinism.play

    def play(binary, maps, work, name, mode, inputs, game_map, ticks, seed, local_players=1, bot=True, extra_env=None):
        if mode == "replay":
            extra_env = dict(extra_env or {}, **LIGHTING)
        return original_play(binary, maps, work, name, mode, inputs, game_map, ticks, seed, local_players, bot,
                             extra_env)

    determinism.play = play
    problems, count = determinism.run_check(args.binary, args.maps, args.work, ticks=args.ticks)
    drew = []
    for name in ("replay-a", "replay-b"):
        log = os.path.join(args.work, name, "out.log")
        text = open(log, "rb").read() if os.path.exists(log) else b""
        for folder in ("saves", "home"):
            debug = os.path.join(args.work, name, folder, "logs", "debug.txt")
            if os.path.exists(debug):
                text += open(debug, "rb").read()
        # (and the trace against the render meshes, round 2's: the level's
        # render geometry made)
        ran = b"ray tracing: Vulkan side path" in text and b"modern lighting: targets" in text and \
            b"the level's render geometry" in text
        drew.append(ran)
        print(f"{name}: lighting passes ran (render meshes): {ran}")
    for problem in problems:
        print(problem)
    ok = not problems and all(drew)
    print(("passed" if ok else "FAILED") + f": {count} ticks compared, record (no window) against two replays "
          f"drawn with ray tracing ao ({args.quality}), HDR and glow")
    if not args.keep:
        shutil.rmtree(args.work, ignore_errors=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
