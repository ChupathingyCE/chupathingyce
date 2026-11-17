#!/usr/bin/env python3
"""A map that can't be loaded, named at the script console (map_name) or in
init.txt, is refused once: the game logs why, stays where it was (the main
menu here) and still loads the next map it's given. The Xbox's precache took
a missing map for a damaged disc and stopped the game for good
(display_error_damaged_media, main_loop_of_death); a map file that didn't
copy, or whose tags didn't load, stopped it too (main_new_map's
_error_immediate).

The names: a stock map's typo, no name, a Custom Edition map that isn't
there, a file that is no map, a map file cut short (its copy into the cache
fails: game_precache_new_map_refusable) and a map whose tags are damaged
(game_load fails: main_new_map). Then Blood Gulch must load.

Based on goatbutt's test in OpenH1 (the OpenH1 team), reworked for our build.

Needs the macOS or Linux build (build/macos/chupathingy, build/linux64/
chupathingy, or HALO_MAP_NAME_BINARY) and the stock Xbox maps, named by HALO_MAP_NAME_MAPS (a folder
of them) or skipped. Each run has its own folders, no sound and no network.

    HALO_MAP_NAME_MAPS=~/halo-maps/maps python3 -m pytest tools/test_map_name.py
"""
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
CONSOLE_PORT = 2391
BAD_NAMES = [
    "levels\\test\\bloodgluch\\bloodgluch",
    "",
    "not_there@ce",
    "levels\\test\\junk\\junk",
    "levels\\test\\prisoner\\prisoner",
    "levels\\test\\putput\\putput",
]
GOOD_NAME = "levels\\test\\bloodgulch\\bloodgulch"
NEEDED = ("ui.map", "bloodgulch.map", "prisoner.map", "putput.map")


def binary():
    if os.environ.get("HALO_MAP_NAME_BINARY"):
        return Path(os.environ["HALO_MAP_NAME_BINARY"])
    for path in (ROOT / "build" / "macos" / "chupathingy", ROOT / "build" / "linux64" / "chupathingy"):
        if path.is_file():
            return path
    return None


def stock_maps():
    folder = os.environ.get("HALO_MAP_NAME_MAPS")
    if not folder or not all((Path(folder) / name).is_file() for name in NEEDED):
        return None
    return Path(folder)


@pytest.fixture(scope="module")
def game():
    path, maps = binary(), stock_maps()
    if not path or not maps:
        pytest.skip("needs a macOS or Linux build and HALO_MAP_NAME_MAPS (the stock Xbox maps)")
    return path, maps


def damaged_maps(maps: Path, folder: Path):
    """the stock maps, but junk.map (no map), prisoner.map cut short and
    putput.map with its tags' header damaged"""
    folder.mkdir()
    for path in maps.glob("*.map"):
        if path.name not in ("prisoner.map", "putput.map"):
            (folder / path.name).symlink_to(path.resolve())
    (folder / "junk.map").write_bytes(bytes(range(256)) * 8)
    with open(maps / "prisoner.map", "rb") as source:
        (folder / "prisoner.map").write_bytes(source.read(3 << 20))
    # (an Xbox map: a 2 KB header, then the rest deflated; the tags' header
    # at the header's offset 16)
    data = (maps / "putput.map").read_bytes()
    header, body = data[:2048], bytearray(zlib.decompress(data[2048:]))
    tags = int.from_bytes(header[16:20], "little") - 2048
    body[tags:tags + 64] = b"\xff" * 64
    (folder / "putput.map").write_bytes(header + zlib.compress(bytes(body), 1))


def send(commands, start):
    """each (seconds, command) to the script console (debug.telnet_console)
    that many seconds after start"""
    for seconds, command in commands:
        time.sleep(max(0.0, start + seconds - time.monotonic()))
        try:
            with socket.create_connection(("127.0.0.1", CONSOLE_PORT), timeout=5) as console:
                console.sendall(f"{command}\r\n".encode())
                time.sleep(0.5)
        except OSError:
            pass


def play(game, tmp_path: Path, seconds: float, commands=(), init=None):
    """the game run for that many seconds, sent commands: (exit status, its
    output, debug.txt)"""
    path, maps = game
    data, saves, home = tmp_path / "data", tmp_path / "saves", tmp_path / "home"
    for folder in (data, saves, home):
        folder.mkdir()
    damaged_maps(maps, data / "maps")
    if init is not None:
        (data / "init.txt").write_text(init)
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
    }
    if sys.platform != "darwin":
        environment["SDL_VIDEO_DRIVER"] = "offscreen"
    start = time.monotonic()
    sender = threading.Thread(target=send, args=(commands, start), daemon=True)
    sender.start()
    result = subprocess.run([str(path)], cwd=str(data), env=environment, capture_output=True, text=True,
                            errors="replace", timeout=seconds + 60)
    sender.join(timeout=1)
    debug = saves / "logs" / "debug.txt"
    return result.returncode, result.stdout + result.stderr, debug.read_text(errors="replace") if debug.is_file() else ""


def loaded(debug, map_name):
    """whether the map was loaded: its precache started and its scenario's
    scripts read after it"""
    short = map_name.split("\\")[-1]
    lines = debug.splitlines()
    for index, line in enumerate(lines):
        if f"starting precaching of map '{short}'" in line:
            return any("scenario scripts:" in later for later in lines[index:])
    return False


def test_map_name_refuses_a_map_it_cannot_load(game, tmp_path):
    commands = [(10 + 3 * index, f'map_name "{name}"') for index, name in enumerate(BAD_NAMES)]
    commands.append((10 + 3 * len(BAD_NAMES) + 2, f'map_name "{GOOD_NAME}"'))
    status, output, debug = play(game, tmp_path, 50, commands)
    assert status == 0, output[-3000:]
    for name in BAD_NAMES:
        assert f"can't load map '{name}'" in debug, f"map_name {name!r} not refused:\n{debug[-3000:]}"
    assert "EXCEPTION" not in debug, debug[-3000:]
    assert loaded(debug, GOOD_NAME), f"bloodgulch did not load after the refused maps:\n{debug[-3000:]}"


def test_init_txt_map_name_refused_for_the_main_menu(game, tmp_path):
    status, output, debug = play(game, tmp_path, 30, [(18, f'map_name "{GOOD_NAME}"')],
                                 init='map_name "levels\\test\\bloodgluch\\bloodgluch"\n')
    assert status == 0, output[-3000:]
    assert "EXCEPTION" not in debug, debug[-3000:]
    assert "can't load map 'levels\\test\\bloodgluch\\bloodgluch'" in debug, debug[-3000:]
    assert loaded(debug, GOOD_NAME), f"bloodgulch did not load after:\n{debug[-3000:]}"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
