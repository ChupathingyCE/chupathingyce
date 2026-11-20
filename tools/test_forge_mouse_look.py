#!/usr/bin/env python3
"""Forge's monitor looks around with the mouse (#1587).

The game's own player input (player_control_update) runs before the cameras
update each frame and took the mouse's motion for the player's hidden unit,
so the monitor's camera (forge_camera_input, read by the director) never saw
any. The test starts a Forge game on Blood Gulch, moves the mouse as the
monitor (debug.test_input's "aim:<x>:<y>", the motion added where a real
mouse's is) and checks the camera turned; then the same as the Spartan,
which always worked.

Needs the macOS or Linux build (build/macos/chupathingy, build/linux64/
chupathingy, or HALO_FORGE_MOUSE_BINARY) and the stock Xbox maps, named by
HALO_FORGE_MOUSE_MAPS (a folder of them) or skipped. Each run has its own
folders, no sound and no network.

    HALO_FORGE_MOUSE_MAPS=~/halo-maps/maps python3 -m pytest tools/test_forge_mouse_look.py
"""
import math
import os
import re
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
CONSOLE_PORT = 2397
NEEDED = ("ui.map", "bloodgulch.map")
LOOKING = re.compile(r"forge: (monitor|Spartan); camera at \S+ \S+ \S+ looking (\S+) (\S+) (\S+);")


def binary():
    if os.environ.get("HALO_FORGE_MOUSE_BINARY"):
        return Path(os.environ["HALO_FORGE_MOUSE_BINARY"])
    for path in (ROOT / "build" / "macos" / "chupathingy", ROOT / "build" / "linux64" / "chupathingy"):
        if path.is_file():
            return path
    return None


def stock_maps():
    folder = os.environ.get("HALO_FORGE_MOUSE_MAPS")
    if not folder or not all((Path(folder) / name).is_file() for name in NEEDED):
        return None
    return Path(folder)


@pytest.fixture(scope="module")
def game():
    path, maps = binary(), stock_maps()
    if not path or not maps:
        pytest.skip("needs a macOS or Linux build and HALO_FORGE_MOUSE_MAPS (the stock Xbox maps)")
    return path, maps


def drive(steps, start, input_file: Path):
    """each (seconds, kind, text) that many seconds after start: a command
    to the script console (debug.telnet_console), or a debug.test_input
    "menu:" script (HALO_TEST_INPUT_FILE)"""
    for seconds, kind, text in steps:
        time.sleep(max(0.0, start + seconds - time.monotonic()))
        if kind == "input":
            input_file.write_text(text)
            continue
        try:
            with socket.create_connection(("127.0.0.1", CONSOLE_PORT), timeout=5) as console:
                console.sendall(f"{text}\r\n".encode())
                time.sleep(0.5)
        except OSError:
            pass


def play(game, tmp_path: Path, seconds: float, steps):
    """the game run for that many seconds, driven by steps: (exit status,
    its output, debug.txt)"""
    path, maps = game
    data, saves, home = tmp_path / "data", tmp_path / "saves", tmp_path / "home"
    for folder in (data, saves, home, data / "maps"):
        folder.mkdir(exist_ok=True)
    for map_file in maps.glob("*.map"):
        (data / "maps" / map_file.name).symlink_to(map_file.resolve())
    input_file = tmp_path / "input.txt"
    input_file.write_text("")
    environment = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home), "TMPDIR": str(home),
        "HALO_DATA_ROOT": str(data), "HALO_SAVE_ROOT": str(saves),
        "HALO_EXIT_AFTER": str(seconds), "HALO_HIDDEN_WINDOW": "1",
        "HALO_DISPLAY_MODE": "windowed", "HALO_WINDOW_SIZE": "640x480", "HALO_NO_AUDIO": "1",
        "SDL_AUDIO_DRIVER": "dummy", "HALO_UPDATE_AUTO": "false", "HALO_CRASH_REPORTS": "no",
        "HALO_DISCORD_APPLICATION": "", "HALO_NET_ONLINE": "false", "HALO_NET_LIST_GAMES": "false",
        "HALO_NET_REPORT_GAMES": "false", "HALO_NET_BROWSER": "", "HALO_NET_JOIN_FROM_CLIPBOARD": "false",
        "HALO_NET_ADDRESS": "127.0.0.1", "HALO_NET_BROADCAST": "127.0.0.1", "HALO_NET_ALLOW_UPNP": "false",
        "HALO_NET_PUBLIC_LOBBY": "false", "HALO_DEDICATED_PUBLIC": "false",
        "HALO_TELNET_CONSOLE": "1", "HALO_TELNET_CONSOLE_PORT": str(CONSOLE_PORT),
        "HALO_FORGE": "1", "HALO_TEST_INPUT_FILE": str(input_file),
    }
    if sys.platform != "darwin":
        environment["SDL_VIDEO_DRIVER"] = "offscreen"
    start = time.monotonic()
    driver = threading.Thread(target=drive, args=(steps, start, input_file), daemon=True)
    driver.start()
    result = subprocess.run([str(path)], cwd=str(data), env=environment, capture_output=True, text=True,
                            errors="replace", timeout=seconds + 60)
    driver.join(timeout=1)
    debug = saves / "logs" / "debug.txt"
    return result.returncode, result.stdout + result.stderr, debug.read_text(errors="replace") if debug.is_file() else ""


def yaw_of(i, j):
    return math.atan2(j, i)


def turned(before, after):
    """radians the camera turned between two "forge where" lines"""
    turn = yaw_of(after[1], after[2]) - yaw_of(before[1], before[2])
    return abs(math.atan2(math.sin(turn), math.cos(turn)))


def test_the_mouse_turns_the_monitor_and_the_spartan(game, tmp_path):
    # (the map loads in the first 15 s; Forge starts the player as the
    # monitor once it spawns. 800 pixels at sensitivity 1 is about 1.8 rad)
    steps = [
        (4, "console", "forge start bloodgulch"),
        (20, "console", "forge on"),
        (22, "console", "forge where"),
        (24, "console", "forge where"),
        (25, "input", "aim:800:0"),
        (28, "console", "forge where"),
        (29, "console", "forge off"),
        (31, "console", "forge where"),
        (32, "input", "aim:800:0"),
        (35, "console", "forge where"),
    ]
    status, output, debug = play(game, tmp_path, 38, steps)
    views = [(match.group(1), float(match.group(2)), float(match.group(3)), float(match.group(4)))
             for match in LOOKING.finditer(debug)]
    assert len(views) == 5, f"expected 5 'forge where' lines, got {views}\n{debug[-4000:]}"
    monitor_still, monitor_before, monitor_after, spartan_before, spartan_after = views
    assert [view[0] for view in views] == ["monitor"] * 3 + ["Spartan"] * 2, views
    # (still without the mouse: the comparison means something)
    assert turned(monitor_still, monitor_before) < 0.01, views
    assert turned(monitor_before, monitor_after) > 0.5, f"the monitor did not turn with the mouse: {views}"
    assert turned(spartan_before, spartan_after) > 0.5, f"the Spartan did not turn with the mouse: {views}"
