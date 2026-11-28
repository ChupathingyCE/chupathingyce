#!/usr/bin/env python3
"""The map's navigation for AI (port/linux/game/ai_nav.c), in the game.

A solo Slayer game is hosted by the automated network test (debug.network_test
"host:<map>:slayer") with HALO_AI_NAV_TEST set: the game makes the map's
navigation (the walkable copy, the regions, the jumps and drops) with no bot
and no other AI in it, then searches a path between the map's two starting
locations furthest apart (by the hops when they are in different regions).
The check passes when the log says "ai nav test: passed".

    python3 tools/test_ai_nav.py --binary build/linux/chupathingy --maps ~/halo/maps
    python3 tools/test_ai_nav.py --binary build/linux/chupathingy --maps ~/halo/maps --map damnation

Needs a Linux build and the stock Xbox maps. CI has no maps, so under pytest
the check is skipped unless HALO_AI_NAV_BINARY and HALO_AI_NAV_MAPS name a build
and a maps folder. Offline: no game list, no internet, its own save folders.
"""
import argparse
import os
import random
import shutil
import subprocess
import sys
import tempfile
import time

try:
    import pytest
except ImportError:  # (run as a script)
    pytest = None

TIMEOUT = 240


def run(binary, maps, work, game_map):
    """Plays the map until the test's line is logged: (passed, the test's lines)."""
    folder = os.path.join(work, game_map)
    shutil.rmtree(folder, ignore_errors=True)
    for sub in ("home", "data", "saves"):
        os.makedirs(os.path.join(folder, sub))
    os.symlink(os.path.abspath(maps), os.path.join(folder, "data", "maps"))
    address = f"127.{random.randint(64, 250)}.{random.randint(1, 250)}.{random.randint(1, 250)}"
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": os.path.join(folder, "home"),
        "HALO_DATA_ROOT": os.path.join(folder, "data"),
        "HALO_SAVE_ROOT": os.path.join(folder, "saves"),
        "HALO_UPDATE_AUTO": "false",
        "HALO_DISCORD_APPLICATION": "",
        "HALO_CRASH_REPORTS": "no",
        "HALO_NET_ONLINE": "false",
        "HALO_NET_LIST_GAMES": "false",
        "HALO_NET_REPORT_GAMES": "false",
        "HALO_NET_BROWSER": "",
        "HALO_NET_JOIN_FROM_CLIPBOARD": "false",
        "HALO_NET_ALLOW_UPNP": "false",
        "HALO_NET_MAPS_URL": "",
        "HALO_NET_ADDRESS": address,
        "HALO_NET_BROADCAST": address,
        "HALO_DEDICATED_PUBLIC": "false",
        "HALO_NETWORK_TEST": f"host:{game_map}:slayer",
        "HALO_NETWORK_TEST_START": "3",
        "HALO_SOLO_GAME": "true",
        "HALO_AI_NAV_TEST": "1",
        "SDL_VIDEO_DRIVER": "dummy",
        "SDL_AUDIO_DRIVER": "dummy",
    }
    if "LD_LIBRARY_PATH" in os.environ:
        env["LD_LIBRARY_PATH"] = os.environ["LD_LIBRARY_PATH"]
    output_path = os.path.join(folder, "output.log")
    lines = []
    with open(output_path, "wb") as output:
        process = subprocess.Popen([os.path.abspath(binary)], cwd=os.path.join(folder, "data"), env=env,
                                   stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            deadline = time.time() + TIMEOUT
            while time.time() < deadline and process.poll() is None:
                time.sleep(1)
                with open(output_path, "r", errors="replace") as f:
                    lines = [line.strip() for line in f if "ai nav" in line]
                if any("ai nav test: passed" in line or "ai nav test: failed" in line for line in lines):
                    break
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    return any("ai nav test: passed" in line for line in lines), lines


def _parametrize(function):
    return pytest.mark.parametrize("game_map", ["bloodgulch", "damnation"])(function) if pytest else function


@_parametrize
def test_ai_nav(tmp_path, game_map):
    """The navigation is made with no bot and a path found across the map."""
    binary = os.environ.get("HALO_AI_NAV_BINARY")
    maps = os.environ.get("HALO_AI_NAV_MAPS")
    if not binary or not maps or not os.path.exists(binary) or not os.path.isdir(maps):
        pytest.skip("no build or maps (HALO_AI_NAV_BINARY, HALO_AI_NAV_MAPS)")
    passed, lines = run(binary, maps, str(tmp_path), game_map)
    assert passed, "\n".join(lines[-20:])
    assert any("ai nav: the map's navigation is made" in line for line in lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--binary", required=True)
    parser.add_argument("--maps", required=True)
    parser.add_argument("--map", action="append")
    parser.add_argument("--work")
    arguments = parser.parse_args()
    work = arguments.work or tempfile.mkdtemp(prefix="ai_nav_")
    failed = 0
    for game_map in arguments.map or ["bloodgulch", "damnation"]:
        passed, lines = run(arguments.binary, arguments.maps, work, game_map)
        for line in lines:
            print(line)
        print(f"{game_map}: {'passed' if passed else 'FAILED'}")
        failed += not passed
    if not arguments.work:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
