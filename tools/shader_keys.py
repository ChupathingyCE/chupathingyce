#!/usr/bin/env python3
"""The stock maps' shader key list (port/assets/shaders/stock.keys): what
each map's draws' shaders are made from, which the game makes at the map's
loading screen (display.prepare_shaders; port/linux/src/d3d8_shader_prep.inl).

    python3 tools/shader_keys.py record --build build/macos/chupathingy
    python3 tools/shader_keys.py merge OUT.keys FOLDER...
    python3 tools/shader_keys.py check port/assets/shaders/stock.keys

record runs the build on each stock map in turn (the Xbox maps in the data
root's maps/ folder, the menus' too), each in a save root of its own, with
debug.draw_every_shader: the map's first frame is drawn first through every
shader its tags hold (port/linux/game/render_enumerate.c), and each draw's key
goes into the save root's cache/shader_keys/<map>.keys, as in any game. Then
it merges the maps' lists into one (merge), each pixel shader key given once
for all the maps. The keys are the translators' input, so one run (OpenGL,
any machine) makes the list for every renderer and platform; compiled
shaders can't be shipped, each driver makes its own.

Maps come only from a data root of links, made by the script that
HALO_DATA_ROOT_SCRIPT names (it takes the folder to make); never copies. Saves
and settings go to a scratch folder, never the player's. On Linux without a
display, Xvfb is started (--display). The list's format is in
port/assets/shaders/README.md.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STOCK = ROOT / "port" / "assets" / "shaders" / "stock.keys"
HEADER = "chupathingy shader keys 1"
DATA_ROOT_SCRIPT = os.environ.get("HALO_DATA_ROOT_SCRIPT", "")
CAMPAIGN = re.compile(r"[a-d]\d\d")
# the maps' files a stock install has that are not played
NOT_PLAYED = {"fallback_ui"}
# (after the map's first frame, drawn through every shader: a few seconds of
# play, its first effects)
SETTLE_SECONDS = 12


def read_keys(text, only_map=None):
    """{map or None: set of (vertex, flags, packed, pixel)} of a list: pixel
    is the pixel key's slots as a tuple of (slot, value) pairs."""
    lines = text.splitlines()
    if not lines or not lines[0].startswith(HEADER):
        raise ValueError("not a shader key list of this format")
    numbers, maps, current = {}, {}, None
    for number, line in enumerate(lines[1:], 2):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        if fields[0] == "p":
            slots = []
            for field in fields[2:]:
                slot, value = field.split(":")
                if int(value, 16):
                    slots.append((int(slot), int(value, 16)))
            numbers[int(fields[1])] = tuple(sorted(slots))
        elif fields[0] == "map":
            current = " ".join(fields[1:]).lower()
        elif fields[0] == "k":
            if len(fields) != 5 or int(fields[4]) not in numbers:
                raise ValueError(f"line {number}: a key without its pixel key: {line}")
            maps.setdefault(current, set()).add((int(fields[1], 16), int(fields[2], 16), int(fields[3], 16),
                                                 numbers[int(fields[4])]))
        else:
            raise ValueError(f"line {number}: not a record: {line}")
    return maps


def write_keys(maps):
    """the list's text: every pixel key once, numbered, then each map's keys."""
    pixels = sorted({key[3] for keys in maps.values() for key in keys})
    number = {pixel: index for index, pixel in enumerate(pixels)}
    out = [HEADER,
           "# The stock maps' shader keys (tools/shader_keys.py; the format: port/assets/shaders/README.md).",
           "# Made by the game at each map's loading screen (port/linux/src/d3d8_shader_prep.inl)."]
    for pixel in pixels:
        out.append(f"p {number[pixel]} " + " ".join(f"{slot}:{value:x}" for slot, value in pixel))
    for name in sorted(maps):
        out.append(f"map {name}")
        for vertex, flags, packed, pixel in sorted(maps[name], key=lambda key: (key[0], key[1], key[2],
                                                                                 number[key[3]])):
            out.append(f"k {vertex:016x} {flags:x} {packed:x} {number[pixel]}")
    return "\n".join(out) + "\n"


def merge(output, folders):
    maps = {}
    for folder in folders:
        for path in sorted(Path(folder).glob("*.keys")):
            for name, keys in read_keys(path.read_text()).items():
                maps.setdefault(name or path.stem.lower(), set()).update(keys)
    text = write_keys(maps)
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text(text)
    print(f"{output}: {len(maps)} maps, {sum(len(keys) for keys in maps.values())} keys, "
          f"{len({key[3] for keys in maps.values() for key in keys})} pixel keys, {len(text) // 1024} KB")


def check(path):
    maps = read_keys(Path(path).read_text())
    if None in maps:
        sys.exit(f"{path}: keys outside a map")
    print(f"{path}: {len(maps)} maps, {sum(len(keys) for keys in maps.values())} keys")


def stage(build, scratch):
    """the build in a folder of its own (the game keeps config.toml next to
    its executable on Linux and Windows): linked, the rest of its folder too."""
    folder = scratch / "build"
    folder.mkdir()
    for entry in build.parent.iterdir():
        if entry.name == "config.toml":
            continue
        if entry == build:
            try:
                os.link(entry, folder / entry.name)
            except OSError:
                shutil.copy2(entry, folder / entry.name)
        else:
            (folder / entry.name).symlink_to(entry)
    return folder / build.name


def record(args):
    build = Path(args.build).resolve()
    script = Path(DATA_ROOT_SCRIPT).expanduser() if DATA_ROOT_SCRIPT else None
    if not script or not script.exists():
        sys.exit("set HALO_DATA_ROOT_SCRIPT to a script that makes a data root of links to your maps: maps are only used through one")
    scratch = Path(tempfile.mkdtemp(prefix="shader-keys-", dir=args.scratch))
    xvfb = None
    try:
        data = scratch / "data"
        subprocess.run(["bash", str(script), str(data)], check=True, stdout=subprocess.DEVNULL)
        names = sorted(path.stem.lower() for path in (data / "maps").glob("*.map") if path.stem.lower() not in NOT_PLAYED)
        if args.maps:
            names = [name for name in names if name in args.maps]
        if not names:
            sys.exit(f"no maps in {data / 'maps'}")
        if sys.platform.startswith("linux") and not os.environ.get("DISPLAY"):
            xvfb = subprocess.Popen(["Xvfb", args.display, "-screen", "0", "1280x720x24", "-nolisten", "tcp"],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1.5)
        staged = stage(build, scratch)
        lists = scratch / "lists"
        lists.mkdir()
        for name in names:
            save = scratch / f"save-{name}"
            save.mkdir()
            init = data / "init.txt"
            if name == "ui":
                init.unlink(missing_ok=True)
            else:
                init.write_text(f"map_name levels\\{name}\\{name}\n" if CAMPAIGN.fullmatch(name) else f"map_name {name}\n")
            env = dict(os.environ, HALO_SAVE_ROOT=str(save), HALO_DATA_ROOT=str(data), HOME=str(save),
                       HALO_UPDATE_AUTO="false", HALO_DISPLAY_MODE="windowed", HALO_WINDOW_SIZE="1280x720",
                       HALO_SOLO_GAME="1", HALO_SKIP_INTRO="true", HALO_DRAW_EVERY_SHADER="1",
                       HALO_PREPARE_SHADERS="false", HALO_RENDERER="gl", HALO_VOLUME="0", HALO_NET_ONLINE="0",
                       HALO_EXIT_AFTER=str(args.seconds), SDL_AUDIODRIVER="dummy")
            if xvfb:
                env["DISPLAY"] = args.display
            log = save / "run.log"
            with open(log, "w") as output:
                game = subprocess.Popen([str(staged)], cwd=str(data), env=env, stdout=output, stderr=subprocess.STDOUT)
                try:
                    game.wait(args.seconds + 60)
                except subprocess.TimeoutExpired:
                    game.kill()
                    game.wait()
            keys = save / "cache" / "shader_keys" / f"{name}.keys"
            debug_logs = (log, save / "debug.txt", data / "debug.txt", data / "logs" / "debug.txt")
            drawn = any("drawn through every shader" in path.read_text(errors="replace")
                        for path in debug_logs if path.exists())
            if not keys.exists():
                print(f"  {name}: no keys (log: {log})")
                continue
            shutil.copy(keys, lists / keys.name)
            count = sum(1 for line in keys.read_text().splitlines() if line.startswith("k "))
            print(f"  {name}: {count} keys" + ("" if drawn or name == "ui" else " (its first frame was not drawn "
                                                                              "through every shader)"))
            for debug in debug_logs[2:]:
                if debug.exists():
                    shutil.copy(debug, save / f"{debug.parent.name}-debug.txt")
                    debug.unlink()
        merge(args.output, [lists])
    finally:
        if xvfb:
            xvfb.terminate()
            xvfb.wait()
        if args.keep:
            print(f"kept {scratch}")
        else:
            shutil.rmtree(scratch, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    recording = commands.add_parser("record", help="run the build on each stock map and make the list")
    recording.add_argument("--build", required=True, help="the build's executable (chupathingy)")
    recording.add_argument("--output", default=str(STOCK), help="the list to write (default: the stock list)")
    recording.add_argument("--maps", nargs="*", help="only these maps (their files' names)")
    recording.add_argument("--seconds", type=int, default=60, help="each map's run, from the start (default 60)")
    recording.add_argument("--display", default=":7", help="the Xvfb display, on Linux without one")
    recording.add_argument("--scratch", help="where the scratch folder goes (default: the system's)")
    recording.add_argument("--keep", action="store_true", help="keep the scratch folder (its logs)")
    merging = commands.add_parser("merge", help="merge maps' own lists (cache/shader_keys folders) into one")
    merging.add_argument("output")
    merging.add_argument("folders", nargs="+")
    checking = commands.add_parser("check", help="read a list, as the game would")
    checking.add_argument("path")
    args = parser.parse_args()
    if args.command == "record":
        record(args)
    elif args.command == "merge":
        merge(args.output, args.folders)
    else:
        check(args.path)


if __name__ == "__main__":
    main()
