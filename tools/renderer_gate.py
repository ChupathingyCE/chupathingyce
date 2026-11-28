#!/usr/bin/env python3
"""The renderer gate: bgfx against OpenGL on one machine, on looks and on
frame time, with a PASS or FAIL and the reasons.

bgfx becomes a platform's default only where it draws the same picture as
OpenGL and draws it faster. This tool checks both halves on the machine it
runs on:

1. Pictures. For each map in the view list (tools/renderer_gate_views.txt)
   the build is started once per renderer in a solo game that stops at a
   fixed tick (debug.freeze_at_tick), so every run shows the same moment.
   Each view is either the player's own (HUD and weapon, as a player sees
   it), or the debug camera at a fixed spot or turned from the spawn. The
   game saves each picture itself (the console's screenshot command, the
   same read-back as the screenshot key), so no screen capture is needed
   and it runs the same on macOS, Linux and Windows. A view is taken again
   until two pictures in a row are the same (textures and bgfx's shaders
   still arriving), then the two renderers' pictures are compared:
   - SSIM of the brightness (structure: missing or changed detail),
   - the share of pixels whose color moved by more than --pixel-delta
     after a 2x2 average (so that edge anti-aliasing alone does not count),
   - the change in average brightness (a picture that is darker or
     brighter overall).
   A view matches when all three are inside the limits. A difference image
   (OpenGL faded, changes in red) and a side by side are saved.
   --variant runs the pictures again with settings changed on both
   renderers (FXAA, anisotropic filtering, per-pixel lighting...), for the
   parity table; only the default variant decides the verdict.
2. Frame time. tools/bench.py plays the same recorded game on both
   renderers (--bench-map, more than once: each) in a window with V-Sync
   off and no frame cap, and its numbers are compared: bgfx passes when its
   mean frame time is at least --bench-margin better, its p99 is no worse
   and its 1% low is no worse, on every map, without falling back to
   OpenGL.

It prints PASS or FAIL for the platform with the reasons, and writes
report.md, report.html and results.json to --out with the pictures side by
side.

    python3 tools/renderer_gate.py --binary build/macos/chupathingy --out /tmp/gate --bench
    python3 tools/renderer_gate.py --binary build/linux64/chupathingy --out /tmp/gate --xvfb :93 \\
        --env HALO_VULKAN_BLOCKLIST=false        # Linux, Mesa's software Vulkan included
    python3 tools/renderer_gate.py ... --variant fxaa:HALO_ANTI_ALIASING=fxaa \\
        --variant aniso16:HALO_ANISOTROPIC_FILTERING=16

Every copy runs off the network with its own home, save and data folders
(a data root of links made by the script HALO_DATA_ROOT_SCRIPT names, as
for tools/picture_check.py: maps are never copied), at a fixed window size,
V-Sync off, sound muted. Custom Edition maps (name@ce) need --maps-ce and
MCC maps (mcc_maps\\<item>\\<level>) need --maps-mcc, a folder of Workshop
items. Needs Python 3 with numpy and Pillow. Documented in
docs/renderer-gate.md.
"""
import argparse
import html
import json
import math
import os
import platform
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
VIEWS = ROOT / "tools" / "renderer_gate_views.txt"
DATA_ROOT_SCRIPT = os.environ.get("HALO_DATA_ROOT_SCRIPT", "")
DEFAULT_TICK = 300
FIELD_OF_VIEW = 70.0

# the limits of a matching view (the defaults of the options)
MIN_SSIM = 0.97
MAX_CHANGED = 0.01
PIXEL_DELTA = 24
MAX_BRIGHTNESS = 1.5
MAX_TILE = 0.08


def log(text):
    print(f"gate: {text}", flush=True)


# ---------- views

class View:
    """A line of the view list: `map name player`, `map name spawn <turn>
    [pitch]` or `map name camera x y z yaw pitch`; a map's own options go on
    a `map <map> tick=<n>` line."""

    def __init__(self, fields):
        self.map, self.name, self.kind = fields[0], fields[1], fields[2]
        self.values = [float(value) for value in fields[3:]]
        if self.kind == "player" and self.values:
            raise ValueError("player takes nothing more")
        if self.kind == "spawn" and len(self.values) not in (1, 2):
            raise ValueError("spawn takes a turn in degrees and a pitch")
        if self.kind == "camera" and len(self.values) != 5:
            raise ValueError("camera takes x y z yaw pitch")
        if self.kind not in ("player", "spawn", "camera"):
            raise ValueError(f"no such view kind: {self.kind}")

    def key(self):
        return f"{safe(self.map)}_{self.name}"


def safe(text):
    return "".join(c if c.isalnum() or c in "-_" else "-" for c in text.replace("@", "-at-"))


def read_views(path, maps):
    views, ticks = [], {}
    for number, line in enumerate(Path(path).read_text().splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        if fields[0] == "map":
            for option in fields[2:]:
                name, _, value = option.partition("=")
                if name != "tick":
                    raise ValueError(f"{path}:{number}: no such map option {name}")
                ticks[fields[1]] = int(value)
            continue
        try:
            view = View(fields)
        except (ValueError, IndexError) as error:
            raise ValueError(f"{path}:{number}: {error}")
        if not maps or view.map in maps:
            views.append(view)
    return views, ticks


def camera_text(x, y, z, yaw, pitch):
    """director.c's camera.txt: position, forward, up, field of view"""
    yaw, pitch = math.radians(yaw), math.radians(pitch)
    forward = (math.cos(yaw) * math.cos(pitch), math.sin(yaw) * math.cos(pitch), math.sin(pitch))
    up = (-math.cos(yaw) * math.sin(pitch), -math.sin(yaw) * math.sin(pitch), math.cos(pitch))
    return "\n".join(["%f %f %f" % (x, y, z), "%f %f %f" % forward, "%f %f %f" % up,
                      "%f" % math.radians(FIELD_OF_VIEW)]) + "\n"


# ---------- pictures

def load(path):
    from PIL import Image
    import numpy
    return numpy.asarray(Image.open(path).convert("RGB"), dtype=numpy.float64)


def half(picture):
    """a 2x2 average: what anti-aliasing changes at an edge mostly averages out"""
    height, width = picture.shape[0] // 2 * 2, picture.shape[1] // 2 * 2
    picture = picture[:height, :width]
    return (picture[0::2, 0::2] + picture[1::2, 0::2] + picture[0::2, 1::2] + picture[1::2, 1::2]) / 4.0


def brightness(picture):
    return picture[..., 0] * 0.299 + picture[..., 1] * 0.587 + picture[..., 2] * 0.114


def box(values, size):
    """the mean over each size by size window (valid windows only), from the
    summed-area table"""
    import numpy
    # (in float64: float32's summed-area table loses the low digits of a
    # 1280x720 picture's sums, and SSIM came out past 1)
    table = numpy.pad(numpy.asarray(values, dtype=numpy.float64), ((1, 0), (1, 0))).cumsum(0).cumsum(1)
    return (table[size:, size:] - table[:-size, size:] - table[size:, :-size] + table[:-size, :-size]) / (size * size)


def ssim(first, second, size=8):
    """the mean structural similarity of two brightness pictures (0 to 1)"""
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    mean_a, mean_b = box(first, size), box(second, size)
    var_a = box(first * first, size) - mean_a * mean_a
    var_b = box(second * second, size) - mean_b * mean_b
    covariance = box(first * second, size) - mean_a * mean_b
    value = ((2 * mean_a * mean_b + c1) * (2 * covariance + c2)) / \
        ((mean_a ** 2 + mean_b ** 2 + c1) * (var_a + var_b + c2))
    return float(value.mean())


TILES = (16, 9)


def tile_shares(moved):
    """the changed share of each of a 16 by 9 grid of tiles, row by row: a
    HUD meter or a weapon drawn differently is a small part of the picture,
    but much of its tile"""
    rows, columns = moved.shape
    shares = []
    for row in range(TILES[1]):
        for column in range(TILES[0]):
            tile = moved[row * rows // TILES[1]:(row + 1) * rows // TILES[1],
                         column * columns // TILES[0]:(column + 1) * columns // TILES[0]]
            shares.append(float(tile.mean()) if tile.size else 0.0)
    return shares


def compare(first_path, second_path, pixel_delta):
    """(numbers, the per-pixel change at full size) of two pictures"""
    import numpy
    first, second = load(first_path), load(second_path)
    if first.shape != second.shape:
        raise ValueError(f"{first_path.name} is {first.shape[1]}x{first.shape[0]}, "
                         f"{second_path.name} {second.shape[1]}x{second.shape[0]}")
    small_first, small_second = half(first), half(second)
    change = numpy.abs(small_first - small_second).max(axis=2)
    numbers = {
        "ssim": ssim(brightness(small_first), brightness(small_second)),
        "changed": float((change > pixel_delta).mean()),
        "brightness": float(brightness(second).mean() - brightness(first).mean()),
        "mean_delta": float(numpy.abs(first - second).mean()),
        "tiles": tile_shares(change > pixel_delta),
    }
    numbers["worst_tile"] = max(numbers["tiles"])
    return numbers, numpy.abs(first - second).max(axis=2)


def difference_picture(reference_path, change, path, pixel_delta):
    """the reference faded to gray, the pixels that changed by more than
    pixel_delta in red (brighter for more)"""
    import numpy
    from PIL import Image
    reference = load(reference_path)
    gray = brightness(reference)[..., None] * 0.35 + 40
    picture = numpy.repeat(gray, 3, axis=2)
    moved = change > pixel_delta
    strength = numpy.clip(change / 128.0, 0.4, 1.0)
    picture[moved] = numpy.stack([255 * strength[moved], 30 * numpy.ones(moved.sum()),
                                  30 * numpy.ones(moved.sum())], axis=1)
    Image.fromarray(picture.clip(0, 255).astype(numpy.uint8)).save(path)


def side_by_side(paths, labels, path):
    from PIL import Image, ImageDraw
    pictures = [Image.open(p).convert("RGB") for p in paths]
    width, height = pictures[0].size
    sheet = Image.new("RGB", (width * len(pictures), height + 28), (16, 16, 16))
    draw = ImageDraw.Draw(sheet)
    for index, (picture, label) in enumerate(zip(pictures, labels)):
        sheet.paste(picture, (index * width, 28))
        draw.text((index * width + 8, 8), label, fill=(235, 235, 235))
    sheet.save(path)


# ---------- running the game

def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class Game:
    """The build on one map in a solo game that stops at a tick, with its
    telnet console; its own home, save and data folders."""

    def __init__(self, binary, game_map, renderer, folder, size, tick, settings, maps_ce, maps_mcc, display,
                 inputs=None):
        self.binary, self.map, self.renderer, self.folder = binary, game_map, renderer, folder
        self.inputs = inputs
        self.size, self.tick, self.settings = size, tick, settings
        self.maps_ce, self.maps_mcc, self.display = maps_ce, maps_mcc, display
        self.process = self.connection = self.xvfb = self.log = None

    def __enter__(self):
        try:
            self.start()
        except BaseException:
            self.stop()
            raise
        return self

    def __exit__(self, *_):
        self.stop()

    def data_root(self):
        data = self.folder / "data"
        script = Path(DATA_ROOT_SCRIPT).expanduser() if DATA_ROOT_SCRIPT else None
        if not script or not script.exists():
            sys.exit("set HALO_DATA_ROOT_SCRIPT to a script that makes a data root of links to your maps")
        subprocess.run(["bash", str(script), str(data)], check=True, stdout=subprocess.DEVNULL)
        # (links only, and only in this run's own data root)
        for name, target in (("maps_ce", self.maps_ce), ("maps_mcc", self.maps_mcc)):
            if target:
                link = data / name
                if link.is_symlink() or link.exists():
                    link.unlink()
                link.symlink_to(Path(target).expanduser().resolve())
        return data

    def start(self):
        shutil.rmtree(self.folder, ignore_errors=True)
        for sub in ("home", "saves"):
            (self.folder / sub).mkdir(parents=True)
        data = self.data_root()
        if not self.inputs:
            (data / "init.txt").write_text(f"map_name {self.map}\n")
        self.port = free_port()
        environment = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": str(self.folder / "home"),
            "HALO_SAVE_ROOT": str(self.folder / "saves"),
            "HALO_DATA_ROOT": str(data),
            "HALO_UPDATE_AUTO": "false",
            "HALO_DISCORD_APPLICATION": "",
            "HALO_CRASH_REPORTS": "no",
            "HALO_NET_ONLINE": "false",
            "HALO_NET_LIST_GAMES": "false",
            "HALO_NET_REPORT_GAMES": "false",
            "HALO_NET_BROWSER": "",
            "HALO_NET_JOIN_FROM_CLIPBOARD": "false",
            "HALO_NET_ALLOW_UPNP": "false",
            "HALO_NET_ADDRESS": "127.0.0.1",
            "HALO_NET_BROADCAST": "127.0.0.1",
            "HALO_DEDICATED_PUBLIC": "false",
            "HALO_STEAM_DISCOVERY": "false",
            # (nothing fetched from Chupathingy maps either)
            "HALO_NET_MAPS_URL": "",
            "SDL_AUDIO_DRIVER": "dummy",
            "HALO_RENDERER": self.renderer,
            "HALO_DISPLAY_MODE": "windowed",
            "HALO_WINDOW_SIZE": self.size,
            "HALO_RESOLUTION_SCALING": "native",
            "HALO_NO_VSYNC": "1",
            "HALO_MAX_FPS": "-1",
            "HALO_VRR": "off",
            "HALO_TELNET_CONSOLE": "true",
            "HALO_TELNET_CONSOLE_PORT": str(self.port),
            "HALO_SOLO_GAME": "1",
            "HALO_SKIP_INTRO": "true",
            "HALO_FREEZE_AT_TICK": str(self.tick),
            # (the frames between ticks draw the camera of the last tick, and a
            # stopped game has no next one: the debug camera would never move)
            "HALO_INTERPOLATION": "false",
            "HALO_EXIT_AFTER": "900",
        }
        for name in ("LD_LIBRARY_PATH", "VK_ICD_FILENAMES", "XDG_RUNTIME_DIR", "WAYLAND_DISPLAY", "DISPLAY",
                     "SYSTEMROOT", "TEMP", "TMP"):
            if name in os.environ:
                environment[name] = os.environ[name]
        if self.display:
            width, height = self.size.split("x")
            self.xvfb = subprocess.Popen(["Xvfb", self.display, "-screen", "0", f"{width}x{height}x24",
                                          "-nolisten", "tcp"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1.5)
            if self.xvfb.poll() is not None:
                raise RuntimeError(f"Xvfb could not start on {self.display}")
            environment["DISPLAY"] = self.display
        if self.inputs:
            # (played as bench.py plays it, frames between ticks on: an action
            # view has no debug camera to move; one Mac replay with them off
            # fell out of step with its recording)
            environment["HALO_INTERPOLATION"] = "true"
            # (an action view: the recorded game played back, as
            # tools/test_determinism.py does, so that both renderers see the
            # same shots fired at the same tick)
            import test_determinism as determinism
            environment.update({
                "HALO_NETWORK_TEST": f"host:{self.map}:slayer",
                "HALO_NETWORK_TEST_START": str(determinism.HOST_START + 1),
                "HALO_NETWORK_TEST_LOCAL_PLAYERS": "1",
                "HALO_SOLO_GAME": "true",
                "HALO_DETERMINISM": f"replay:{self.inputs}",
                "HALO_DETERMINISM_SEED": str(determinism.DEFAULT_SEED),
            })
        environment.update(self.settings)
        self.log = open(self.folder / "out.log", "w")
        self.process = subprocess.Popen([str(self.binary)], cwd=str(data), env=environment, stdout=self.log,
                                        stderr=subprocess.STDOUT)
        # (the load, then the ticks up to the stop: a software renderer can
        # run the game at a fraction of its speed)
        deadline = time.time() + 240 + self.tick * 0.2
        while time.time() < deadline and not self.connection:
            if self.process.poll() is not None:
                raise RuntimeError(f"the game stopped while it started (exit {self.process.returncode})")
            try:
                connection = socket.create_connection(("127.0.0.1", self.port), timeout=2)
                connection.settimeout(30)
                if b"play a game" in connection.recv(256):
                    connection.settimeout(1.0)
                    self.connection = connection
                    break
                connection.close()
            except OSError:
                time.sleep(1)
        if not self.connection:
            raise RuntimeError("the game's console never answered")
        # (the map loads and plays up to the tick, then holds it; the main
        # menu's own scene has a game time too, so the map must have loaded)
        while time.time() < deadline:
            answer = self.command("(game_time)")
            numbers = [int(word) for word in answer.split() if word.isdigit()]
            if numbers and numbers[-1] >= self.tick and self.map_loaded():
                return
            if self.process.poll() is not None:
                raise RuntimeError(f"the game stopped (exit {self.process.returncode})")
            time.sleep(1)
        raise RuntimeError(f"the game never reached tick {self.tick}")

    def map_loaded(self):
        """whether a map has loaded: the [tuning] line each map's load logs
        (the main menu's scene is "at=boot")"""
        text = (self.folder / "out.log").read_text(errors="replace")
        return "tuning: at=map " in text

    def command(self, text, wait=0.4):
        self.connection.sendall((text + "\n").encode())
        time.sleep(wait)
        answer = b""
        try:
            while True:
                part = self.connection.recv(65536)
                if not part:
                    break
                answer += part
        except OSError:
            pass
        return answer.decode(errors="replace")

    def screenshots(self):
        # (capture.c's folder: the data root's screenshots)
        return self.folder / "data" / "screenshots"

    def shoot(self, name, timeout=20):
        path = self.screenshots() / f"{name}.png"
        if path.exists():
            path.unlink()
        self.command(f"screenshot {name}")
        deadline = time.time() + timeout
        while time.time() < deadline:
            if path.exists():
                time.sleep(0.2)
                return path
            if self.process.poll() is not None:
                raise RuntimeError(f"the game stopped (exit {self.process.returncode})")
            time.sleep(0.2)
        raise RuntimeError(f"no screenshot {name} came")

    def backend(self):
        """what drew: 'OpenGL', or bgfx's own line ('Metal on Apple M4 ...')"""
        text = (self.folder / "out.log").read_text(errors="replace")
        for line in text.splitlines():
            if "bgfx: " in line and " on " in line and ("Metal" in line or "Vulkan" in line or "Direct3D" in line):
                return line.split("bgfx: ", 1)[1].split(",")[0].strip()
        return "OpenGL"

    def stop(self):
        if self.connection:
            self.connection.close()
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(15)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        if self.xvfb:
            self.xvfb.terminate()
            try:
                self.xvfb.wait(15)
            except subprocess.TimeoutExpired:
                self.xvfb.kill()
                self.xvfb.wait()
        if self.log:
            self.log.close()


def settled_shot(game, name, scratch, attempts, still):
    """The view's picture once two in a row match (still: the share of pixels
    that may change between them): (path, note)"""
    import numpy
    previous = None
    for attempt in range(attempts):
        shot = game.shoot(f"{name}-try{attempt}")
        if previous is not None:
            change = numpy.abs(load(shot) - load(previous)).max(axis=2)
            if float((change > 8).mean()) <= still:
                previous.unlink()
                return shot, ""
            previous.unlink()
        previous = shot
        time.sleep(1.5)
    return previous, "the picture never held still"


RECORDINGS = {}
GAME_PORT = 5150


def wait_for_game_port(minutes=20):
    """The recorded game is hosted on 127.0.0.1's game port: another copy of
    the game on the machine that holds it (a test of something else) stops
    the host, so wait for it to let go"""
    deadline = time.time() + minutes * 60
    told = False
    while True:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            try:
                probe.bind(("127.0.0.1", GAME_PORT))
                return True
            except OSError:
                pass
        if time.time() > deadline:
            log(f"another program still holds 127.0.0.1:{GAME_PORT}: going on anyway")
            return False
        if not told:
            log(f"another program holds 127.0.0.1:{GAME_PORT} (another copy of the game?): waiting for it")
            told = True
        time.sleep(10)


def recording(binary, game_map, tick, scratch, arguments):
    """An action view's game: a solo Slayer game on an Xbox map whose player
    is the scripted bot, recorded once (headless) and kept for every run"""
    key = (game_map, tick)
    if key not in RECORDINGS:
        import test_determinism as determinism
        work = scratch / f"record-{safe(game_map)}"
        inputs = work / "inputs.bin"
        log(f"recording the action on {game_map} ({tick + 30} ticks)")
        original = determinism.copy_environment

        def with_other_maps(folder, maps, address):
            # (a Custom Edition or MCC map's folder linked in too)
            environment = original(folder, maps, address)
            environment["HALO_NET_MAPS_URL"] = ""
            for name, target in (("maps_ce", arguments.maps_ce), ("maps_mcc", arguments.maps_mcc)):
                if target:
                    os.symlink(Path(target).expanduser().resolve(), os.path.join(folder, "data", name))
            return environment

        determinism.copy_environment = with_other_maps
        try:
            determinism.play(str(binary), arguments.maps, str(work), "record", "record", str(inputs), game_map,
                             tick + 30, determinism.DEFAULT_SEED)
        finally:
            determinism.copy_environment = original
        RECORDINGS[key] = inputs
    return RECORDINGS[key]


def capture(binary, renderer, views, ticks, out, scratch, arguments, settings, label):
    """Runs the build once per map on a renderer and saves each view as
    out/<view key>.png. Returns ({key: path}, {map: failure}, {key: note},
    {map: backend})."""
    shots, failures, notes, backends = {}, {}, {}, {}
    out.mkdir(parents=True, exist_ok=True)
    for game_map in dict.fromkeys(view.map for view in views):
        folder = scratch / f"{label}-{safe(game_map)}"
        tick = ticks.get(game_map, arguments.tick)
        log(f"{label}: {game_map} (tick {tick})")
        real_map, inputs = game_map, None
        if game_map.startswith("action:"):
            real_map = game_map.split(":", 1)[1]
            wait_for_game_port()
            try:
                inputs = recording(binary, real_map, tick, scratch, arguments)
            except (RuntimeError, OSError, subprocess.CalledProcessError) as reason:
                # (another copy of the game holding 127.0.0.1's game port, say:
                # the map's views fail, and the rest of the gate goes on)
                failures[game_map] = f"its recording could not be made: {reason}"
                log(f"  {game_map}: FAILED: {failures[game_map]}")
                continue
        for attempt in range(2):
            if inputs:
                wait_for_game_port()
            try:
                capture_map(binary, renderer, game_map, real_map, inputs, views, tick, folder, out, scratch,
                            arguments, settings, shots, notes, backends)
                failures.pop(game_map, None)
                break
            except (RuntimeError, OSError, subprocess.CalledProcessError, ValueError) as reason:
                failures[game_map] = str(reason)
                log(f"  {game_map}: FAILED: {reason}{': once more' if not attempt else ''}")
            finally:
                if (folder / "out.log").exists():
                    shutil.copy(folder / "out.log", out / f"{safe(game_map)}.log")
                if not arguments.keep:
                    shutil.rmtree(folder, ignore_errors=True)
    return shots, failures, notes, backends


def capture_map(binary, renderer, game_map, real_map, inputs, views, tick, folder, out, scratch, arguments,
                settings, shots, notes, backends):
    """One run of the build on a map, its views saved in out"""
    with Game(binary, real_map, renderer, folder, arguments.size, tick, settings, arguments.maps_ce,
              arguments.maps_mcc, arguments.xvfb, inputs) as game:
        backends[game_map] = game.backend()
        time.sleep(arguments.settle)
        mine = [view for view in views if view.map == game_map]
        # (the player's own views first: the debug camera keeps the view
        # once it is loaded)
        mine.sort(key=lambda view: view.kind != "player")
        spawn = None
        for view in mine:
            if view.kind == "player":
                game.command("(show_hud 1)")
            else:
                if view.kind == "spawn" and spawn is None:
                    game.command("debug_camera_save", wait=1.0)
                    saved = folder / "saves" / "camera.txt"
                    if not saved.exists():
                        saved = folder / "data" / "camera.txt"
                    fields = [float(word) for word in saved.read_text().split()]
                    spawn = (fields[0], fields[1], fields[2] + 0.25,
                             math.degrees(math.atan2(fields[4], fields[3])))
                if view.kind == "spawn":
                    x, y, z, yaw = spawn
                    pose = (x, y, z, (yaw + view.values[0] + 180) % 360 - 180,
                            view.values[1] if len(view.values) > 1 else 0.0)
                else:
                    pose = tuple(view.values)
                text = camera_text(*pose)
                for where in ("saves", "data"):
                    (folder / where / "camera.txt").write_text(text)
                game.command("(show_hud 0)")
                game.command("(show_hud_help_text 0)")
                game.command("debug_camera_load", wait=1.0)
            shot, note = settled_shot(game, view.key(), scratch, arguments.attempts, arguments.still)
            target = out / f"{view.key()}.png"
            shutil.move(str(shot), target)
            shots[view.key()] = target
            if note:
                notes[view.key()] = note
            log(f"  {view.key()}: saved{' (' + note + ')' if note else ''}")


GAP_MARKS = ("is not in the bgfx renderer yet", "(not drawn yet)")


def gap_lines(folder, game_map):
    """what the renderer's log says it left out on a map: a setting it has
    no pass for ("anti-aliasing: fxaa is not in the bgfx renderer yet, so
    msaa2x"), a kind of draw it skips ("bgfx: wireframe (not drawn yet)")"""
    path = folder / f"{safe(game_map)}.log"
    if not path.exists():
        return []
    lines = [line.split("chupathingyce: ", 1)[-1] for line in path.read_text(errors="replace").splitlines()
             if any(mark in line for mark in GAP_MARKS)]
    return list(dict.fromkeys(lines))


def kept_pictures(views, folder):
    """capture()'s answer from an earlier run's pictures and logs in folder
    (--from-pictures)"""
    shots, failures, backends = {}, {}, {}
    for view in views:
        path = folder / f"{view.key()}.png"
        if path.exists():
            shots[view.key()] = path
        else:
            failures.setdefault(view.map, "no picture kept")
    for game_map in dict.fromkeys(view.map for view in views):
        log_path = folder / f"{safe(game_map)}.log"
        backend = "OpenGL"
        if log_path.exists():
            for line in log_path.read_text(errors="replace").splitlines():
                if "bgfx: " in line and " on " in line and ("Metal" in line or "Vulkan" in line or "Direct3D" in line):
                    backend = line.split("bgfx: ", 1)[1].split(",")[0].strip()
                    break
            backends[game_map] = backend
    return shots, failures, {}, backends


def kept_bench(out):
    results = {}
    for path in sorted(out.glob("bench-*.json")):
        data = json.loads(path.read_text())
        results[data.get("map", path.stem[len("bench-"):])] = {row["renderer"]: row for row in data["results"]}
    return results


# ---------- frame time

def bench(binary, renderers, arguments, out):
    """tools/bench.py on each --bench-map: {map: {renderer: numbers}}"""
    results = {}
    for game_map in arguments.bench_map:
        json_path = out / f"bench-{game_map}.json"
        command = [sys.executable, str(ROOT / "tools" / "bench.py"), "--binary", str(binary),
                   "--maps", arguments.maps, "--map", game_map, "--runs", str(arguments.bench_runs),
                   "--ticks", str(arguments.bench_ticks), "--warmup", str(arguments.bench_warmup),
                   "--size", arguments.size, "--work", str(out / f"bench-{game_map}"), "--json", str(json_path)]
        for renderer in renderers:
            command += ["--renderer", renderer]
        for pair in ["HALO_NET_MAPS_URL=", *arguments.env]:
            command += ["--env", pair]
        log(f"bench: {game_map}")
        wait_for_game_port()
        load_before = os.getloadavg()[0] if hasattr(os, "getloadavg") else -1
        environment = dict(os.environ)
        xvfb = None
        if arguments.xvfb:
            # (a display of its own for bench.py's copies, as the pictures' runs have)
            width, height = arguments.size.split("x")
            xvfb = subprocess.Popen(["Xvfb", arguments.xvfb, "-screen", "0", f"{width}x{height}x24", "-nolisten", "tcp"],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(1.5)
            environment["DISPLAY"] = arguments.xvfb
        try:
            completed = subprocess.run(command, env=environment, capture_output=True, text=True)
        finally:
            if xvfb:
                xvfb.terminate()
                xvfb.wait(15)
        (out / f"bench-{game_map}.txt").write_text(completed.stdout + completed.stderr)
        if not json_path.exists():
            results[game_map] = {"error": f"bench.py failed (bench-{game_map}.txt)"}
            continue
        data = json.loads(json_path.read_text())
        results[game_map] = {row["renderer"]: row for row in data["results"]}
        load_after = os.getloadavg()[0] if hasattr(os, "getloadavg") else -1
        results[game_map]["load"] = {"before": load_before, "after": load_after, "cpus": os.cpu_count()}
        if not idle(results[game_map]["load"]):
            log(f"bench: {game_map}: the machine was busy (load {load_before:.1f} -> {load_after:.1f} on "
                f"{os.cpu_count()} CPUs, {IDLE_LOAD} at most): the frame times are not to be trusted")
        if not arguments.keep:
            shutil.rmtree(out / f"bench-{game_map}", ignore_errors=True)
    return results


# the most the load average may be, before the bench and after it, for its
# frame times to count: another program's threads take the CPU from the game
# and its renderer's (a software renderer's own threads load the machine
# too, so its frame times never count, as docs/renderer-gate.md says)
IDLE_LOAD = 1.0


def idle(load):
    """whether the machine was idle enough for a bench (its load before and after)"""
    return 0 <= load["before"] < IDLE_LOAD and 0 <= load["after"] < IDLE_LOAD


def bench_verdict(results, first, second, margin):
    """(passed, [reasons]) for the frame-time half"""
    reasons, passed = [], True
    if not results:
        return None, ["frame time not measured (--bench)"]
    for game_map, rows in results.items():
        if "error" in rows:
            return False, [f"{game_map}: {rows['error']}"]
        load = rows.get("load")
        if load and not idle(load):
            passed = False
            reasons.append(f"{game_map}: the machine was busy (load {load['before']:.1f} -> {load['after']:.1f} on "
                           f"{load['cpus']} CPUs, {IDLE_LOAD} at most): measure again with nothing else running")
        elif load:
            reasons.append(f"{game_map}: idle (load {load['before']:.1f} -> {load['after']:.1f})")
        a, b = rows.get(first), rows.get(second)
        if not a or not b or "mean_ms" not in a or "mean_ms" not in b:
            return False, [f"{game_map}: no frames logged for one renderer"]
        if b.get("fell_back_to_gl"):
            passed = False
            reasons.append(f"{game_map}: {second} fell back to OpenGL")
        speedup = a["mean_ms"] / b["mean_ms"] if b["mean_ms"] else 0
        line = (f"{game_map}: mean {a['mean_ms']:.2f} -> {b['mean_ms']:.2f} ms ({speedup:.2f}x), "
                f"p99 {a['p99_ms']:.2f} -> {b['p99_ms']:.2f} ms, 1% low {a['low_1pct_fps']:.0f} -> "
                f"{b['low_1pct_fps']:.0f} fps, CPU {a['cpu_ms']:.2f} -> {b['cpu_ms']:.2f} ms")
        if b["mean_ms"] > a["mean_ms"] * (1 - margin):
            passed = False
            line += f": mean not {margin * 100:.0f}% better"
        if b["p99_ms"] > a["p99_ms"]:
            passed = False
            line += ": p99 worse"
        if b["low_1pct_fps"] < a["low_1pct_fps"]:
            passed = False
            line += ": 1% low worse"
        reasons.append(line)
    return passed, reasons


# ---------- the report

def beyond_noise(numbers, noise, arguments):
    """whether the renderers differ by clearly more than the reference
    differs from itself: past a limit and past twice the reference's own"""
    return (numbers["ssim"] < min(arguments.min_ssim, 1 - 2 * (1 - noise["ssim"])) or
            numbers["changed"] > max(arguments.max_changed, 2 * noise["changed"]) or
            any(share > max(arguments.max_tile, 2 * own) for share, own in zip(numbers["tiles"], noise["tiles"])) or
            abs(numbers["brightness"]) > max(arguments.max_brightness, 2 * abs(noise["brightness"])))


def verdict_of(numbers, arguments):
    reasons = []
    if numbers["ssim"] < arguments.min_ssim:
        reasons.append(f"SSIM {numbers['ssim']:.3f} < {arguments.min_ssim}")
    if numbers["changed"] > arguments.max_changed:
        reasons.append(f"{numbers['changed'] * 100:.2f}% of pixels changed > {arguments.max_changed * 100:g}%")
    if numbers.get("worst_tile", 0) > arguments.max_tile:
        reasons.append(f"{numbers['worst_tile'] * 100:.1f}% of one part of the picture changed "
                       f"> {arguments.max_tile * 100:g}%")
    if abs(numbers["brightness"]) > arguments.max_brightness:
        reasons.append(f"brightness {numbers['brightness']:+.2f} (limit {arguments.max_brightness})")
    return ("match" if not reasons else "DIFFERS"), reasons


def write_report(out, summary, rows, bench_results, arguments):
    def link(path):
        return os.path.relpath(path, out) if path else ""

    md = [f"# Renderer gate: {summary['platform']}", "",
          f"**{summary['verdict']}**: {summary['first']} ({summary['first_backend']}) against "
          f"{summary['second']} ({summary['second_backend']}), build `{summary['binary']}`, "
          f"{arguments.size}, {summary['date']}.", ""]
    md += [f"- {reason}" for reason in summary["reasons"]] + [""]
    md += [f"A view matches with SSIM >= {arguments.min_ssim}, at most {arguments.max_changed * 100:g}% of pixels "
           f"changed by more than {arguments.pixel_delta} (after a 2x2 average), at most {arguments.max_tile * 100:g}% "
           f"in any part of a 16x9 grid, and brightness within {arguments.max_brightness}.", "",
           "| Variant | View | Result | SSIM | Changed | Worst part | Brightness | Note |", "|---|---|---|---|---|---|---|---|"]
    for row in rows:
        n = row.get("numbers") or {}
        md.append(f"| {row['variant']} | {row['key']} | {row['verdict']} | "
                  f"{n.get('ssim', 0):.3f} | {n.get('changed', 0) * 100:.2f}% | {n.get('worst_tile', 0) * 100:.1f}% | "
                  f"{n.get('brightness', 0):+.2f} | {'; '.join(row.get('reasons', []) + ([row['note']] if row.get('note') else []))} |")
    if bench_results:
        md += ["", "## Frame time (tools/bench.py)", "",
               "| Map | Renderer | Frames | Mean | p50 | p95 | p99 | FPS | 1% low | CPU/frame | Fell back |",
               "|---|---|---|---|---|---|---|---|---|---|---|"]
        for game_map, rows_by_renderer in bench_results.items():
            for renderer, r in rows_by_renderer.items():
                if renderer == "load":
                    md.append(f"| {game_map} | (load average {r['before']:.1f} -> {r['after']:.1f}, {r['cpus']} CPUs) "
                              "|||||||||")
                    continue
                if not isinstance(r, dict) or "mean_ms" not in r:
                    md.append(f"| {game_map} | {renderer} | {r} |||||||||")
                    continue
                md.append(f"| {game_map} | {renderer} | {r['frames']:.0f} | {r['mean_ms']:.2f} ms | "
                          f"{r['p50_ms']:.2f} ms | {r['p95_ms']:.2f} ms | {r['p99_ms']:.2f} ms | {r['fps']:.0f} | "
                          f"{r['low_1pct_fps']:.0f} | {r['cpu_ms']:.2f} ms | {r.get('fell_back_to_gl')} |")
    for row in rows:
        if row.get("sheet"):
            md += ["", f"### {row['variant']} / {row['key']}: {row['verdict']}", "", f"![]({link(row['sheet'])})"]
    (out / "report.md").write_text("\n".join(md) + "\n")

    sections = []
    for row in rows:
        n = row.get("numbers") or {}
        numbers = (f"SSIM {n['ssim']:.3f}, {n['changed'] * 100:.2f}% changed (worst part "
                   f"{n.get('worst_tile', 0) * 100:.1f}%), brightness {n['brightness']:+.2f}" if n else "")
        figures = "".join(
            f'<figure><a href="{html.escape(link(path))}"><img loading="lazy" src="{html.escape(link(path))}" '
            f'alt="{name}"></a><figcaption>{name}</figcaption></figure>'
            for name, path in ((summary["first"], row.get("first")), (summary["second"], row.get("second")),
                               ("difference", row.get("diff"))) if path)
        body = (f'<h2><span class="v v-{row["verdict"].split()[0].lower()}">{row["verdict"]}</span> '
                f'{html.escape(row["variant"])} / {html.escape(row["key"])}</h2>'
                f'<p>{html.escape(numbers)} {html.escape("; ".join(row.get("reasons", [])))} '
                f'{html.escape(row.get("note", ""))}</p><div class="pictures">{figures}</div>')
        sections.append(f"<section>{body}</section>" if row["verdict"] != "match" else
                        f"<details><summary>match {html.escape(row['variant'])} / {html.escape(row['key'])} "
                        f"{html.escape(numbers)}</summary>{body}</details>")
    bench_html = ""
    if bench_results:
        lines = []
        for game_map, rows_by_renderer in bench_results.items():
            for renderer, r in rows_by_renderer.items():
                if isinstance(r, dict) and "mean_ms" in r:
                    lines.append(f"<tr><td>{html.escape(game_map)}</td><td>{html.escape(renderer)}</td>"
                                 f"<td>{r['mean_ms']:.2f}</td><td>{r['p95_ms']:.2f}</td><td>{r['p99_ms']:.2f}</td>"
                                 f"<td>{r['fps']:.0f}</td><td>{r['low_1pct_fps']:.0f}</td><td>{r['cpu_ms']:.2f}</td>"
                                 f"<td>{r.get('fell_back_to_gl')}</td></tr>")
        bench_html = ("<h2>Frame time</h2><table><tr><th>Map</th><th>Renderer</th><th>Mean ms</th><th>p95 ms</th>"
                      "<th>p99 ms</th><th>FPS</th><th>1% low</th><th>CPU ms</th><th>Fell back</th></tr>"
                      + "".join(lines) + "</table>")
    page = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Renderer gate</title>
<style>
:root {{ --bg: #fff; --fg: #1d1d1f; --muted: #6e6e73; --line: #d2d2d7; --bad: #c4291c; --good: #2a7a2a; }}
@media (prefers-color-scheme: dark) {{
  :root {{ --bg: #161617; --fg: #f0f0f2; --muted: #a1a1a6; --line: #3a3a3c; --bad: #ff6b5e; --good: #6fcf6f; }}
}}
body {{ background: var(--bg); color: var(--fg); font: 15px/1.45 system-ui, sans-serif; margin: 0 auto;
        max-width: 1600px; padding: 16px; }}
h1 {{ font-size: 22px; }} h2 {{ font-size: 17px; margin: 0 0 4px; }}
p {{ color: var(--muted); margin: 0 0 8px; }}
section, details {{ border-top: 1px solid var(--line); padding: 12px 0; }}
summary {{ cursor: pointer; color: var(--muted); }}
.pictures {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 8px; }}
figure {{ margin: 0; }} img {{ width: 100%; height: auto; display: block; border: 1px solid var(--line); }}
figcaption {{ color: var(--muted); font-size: 13px; }}
table {{ border-collapse: collapse; margin: 8px 0 16px; }} td, th {{ border: 1px solid var(--line); padding: 3px 8px; }}
.v {{ font-size: 12px; font-weight: 600; padding: 2px 6px; border-radius: 4px; border: 1px solid; }}
.v-differs, .v-failed, .v-not {{ color: var(--bad); }} .v-match {{ color: var(--good); }} .v-unsteady {{ color: var(--muted); }}
</style></head><body>
<h1>Renderer gate: {html.escape(summary['platform'])}: {html.escape(summary['verdict'])}</h1>
<p>{html.escape(summary['first'])} ({html.escape(summary['first_backend'])}) against {html.escape(summary['second'])}
({html.escape(summary['second_backend'])}), {html.escape(arguments.size)}, {html.escape(summary['date'])}.</p>
<ul>{"".join(f"<li>{html.escape(reason)}</li>" for reason in summary['reasons'])}</ul>
{bench_html}
{"".join(sections)}
</body></html>
"""
    (out / "report.html").write_text(page)


# ---------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", required=True, help="the build to check (its chupathingy executable)")
    parser.add_argument("--out", required=True, type=Path, help="where the pictures and the report go")
    parser.add_argument("--first", default="gl", help="the renderer that is the reference (default gl)")
    parser.add_argument("--second", default="bgfx", help="the renderer checked against it (default bgfx; "
                        "d3d11 on Windows)")
    parser.add_argument("--views", default=str(VIEWS), help="the view list (default tools/renderer_gate_views.txt)")
    parser.add_argument("--map", action="append", dest="maps_wanted", help="only this map's views (repeatable)")
    parser.add_argument("--maps", default=os.path.expanduser("~/halo-maps/maps"),
                        help="the stock Xbox maps, for the frame-time half (bench.py)")
    parser.add_argument("--maps-ce", help="a folder of Custom Edition maps, linked as maps_ce")
    parser.add_argument("--maps-mcc", help="a folder of MCC Workshop items, linked as maps_mcc")
    parser.add_argument("--size", default="1280x720", help="the window's size (default 1280x720)")
    parser.add_argument("--tick", type=int, default=DEFAULT_TICK, help=f"the tick the game stops at (default "
                        f"{DEFAULT_TICK}; a map's own in the view list)")
    parser.add_argument("--settle", type=float, default=8.0, help="seconds after the tick before the first view")
    parser.add_argument("--attempts", type=int, default=8, help="pictures of a view at most, until two match")
    parser.add_argument("--still", type=float, default=0.0005, help="the share of pixels that may change between "
                        "two pictures of a view that is still")
    parser.add_argument("--env", action="append", default=[], metavar="NAME=VALUE",
                        help="a setting for every run (both renderers, every variant)")
    parser.add_argument("--variant", action="append", default=[], metavar="NAME:NAME=VALUE[,NAME=VALUE]",
                        help="the pictures again with these settings on both renderers (the parity table)")
    parser.add_argument("--variant-maps", help="the maps the variants run on, comma separated (default: all)")
    parser.add_argument("--repeat-first", action="store_true",
                        help="run each renderer twice: a view either draws differently from itself is UNSTEADY, not counted")
    parser.add_argument("--min-ssim", type=float, default=MIN_SSIM)
    parser.add_argument("--max-changed", type=float, default=MAX_CHANGED)
    parser.add_argument("--pixel-delta", type=int, default=PIXEL_DELTA)
    parser.add_argument("--max-brightness", type=float, default=MAX_BRIGHTNESS)
    parser.add_argument("--max-tile", type=float, default=MAX_TILE,
                        help="the changed share allowed in any one of a 16x9 grid's parts")
    parser.add_argument("--bench", action="store_true", help="also measure frame time (tools/bench.py)")
    parser.add_argument("--bench-map", action="append", help="the maps bench.py plays (default bloodgulch)")
    parser.add_argument("--bench-runs", type=int, default=3)
    parser.add_argument("--bench-ticks", type=int, default=1800)
    parser.add_argument("--bench-warmup", type=int, default=300)
    parser.add_argument("--bench-margin", type=float, default=0.05,
                        help="how much lower bgfx's mean frame time must be (default 0.05: 5%%)")
    parser.add_argument("--platform", help="the platform's name in the report (default: this machine's)")
    parser.add_argument("--xvfb", metavar="DISPLAY", help="Linux: run each copy on its own Xvfb display")
    parser.add_argument("--from-pictures", action="store_true",
                        help="run nothing: compare and report the pictures (and bench-*.json) already in --out")
    parser.add_argument("--keep", action="store_true", help="keep each run's folders (logs, saves)")
    arguments = parser.parse_args()
    arguments.bench_map = arguments.bench_map or ["bloodgulch"]
    try:
        import numpy  # noqa: F401
        import PIL  # noqa: F401
    except ImportError:
        sys.exit("the gate needs numpy and Pillow (python3 -m pip install numpy pillow)")

    binary = Path(arguments.binary).resolve()
    out = arguments.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    views, ticks = read_views(arguments.views, arguments.maps_wanted)
    if not views:
        sys.exit("no views")
    common = dict(pair.split("=", 1) for pair in arguments.env)
    variants = [("default", {})]
    for text in arguments.variant:
        name, _, pairs = text.partition(":")
        variants.append((name, dict(pair.split("=", 1) for pair in pairs.split(",") if pair)))
    variant_maps = set(arguments.variant_maps.split(",")) if arguments.variant_maps else None
    scratch = Path(tempfile.mkdtemp(prefix="renderer-gate-", dir=str(out)))
    rows, backends_seen = [], {arguments.first: set(), arguments.second: set()}
    all_failures = []
    try:
        for variant, settings in variants:
            these = views if variant == "default" or not variant_maps else \
                [view for view in views if view.map in variant_maps]
            runs = [(arguments.first, "first"), (arguments.second, "second")]
            if arguments.repeat_first and variant == "default":
                # (and the renderer checked: a view either renderer can't
                # draw the same twice says nothing of the renderers)
                runs += [(arguments.first, "first-again"), (arguments.second, "second-again")]
            captured = {}
            for renderer, role in runs:
                folder = out / variant / role
                if arguments.from_pictures:
                    # (an earlier run made before the second renderer ran twice)
                    if role == "second-again" and not folder.is_dir():
                        continue
                    captured[role] = kept_pictures(these, folder)
                else:
                    captured[role] = capture(binary, renderer, these, ticks, folder, scratch, arguments,
                                             {**common, **settings}, f"{variant}-{role}")
                for backend in captured[role][3].values():
                    backends_seen[renderer].add(backend)
                for game_map, reason in captured[role][1].items():
                    all_failures.append(f"{variant}/{role}: {game_map}: {reason}")
            for view in these:
                key = view.key()
                row = {"variant": variant, "key": key, "map": view.map}
                first, second = captured["first"][0].get(key), captured["second"][0].get(key)
                notes = [note for note in (captured["first"][2].get(key), captured["second"][2].get(key)) if note]
                if notes:
                    row["note"] = "; ".join(notes)
                if not first or not second:
                    row.update(verdict="FAILED", reasons=["no picture from " +
                                                          (arguments.first if not first else arguments.second)])
                    rows.append(row)
                    continue
                numbers, change = compare(first, second, arguments.pixel_delta)
                verdict, reasons = verdict_of(numbers, arguments)
                # (a setting the renderer has no pass for is a gap whatever the
                # pictures say: an anti-aliasing pass changes little that the
                # 2x2 average keeps)
                gaps = gap_lines(out / variant / "second", view.map)
                if gaps:
                    verdict = "NOT IN " + arguments.second.upper()
                    reasons = reasons + gaps
                diff = out / variant / f"{key}_diff.png"
                difference_picture(first, change, diff, arguments.pixel_delta)
                sheet = out / variant / f"{key}_sbs.png"
                side_by_side([first, second, diff], [arguments.first, arguments.second, "difference"], sheet)
                row.update(verdict=verdict, reasons=reasons, numbers=numbers, first=first, second=second, diff=diff,
                           sheet=sheet)
                unsteady = []
                if any("never held still" in note for note in notes):
                    unsteady.append("the picture never held still")
                for role, renderer in (("first-again", arguments.first), ("second-again", arguments.second)):
                    again = captured.get(role, ({},))[0].get(key)
                    if not again:
                        continue
                    noise, _ = compare(first if role == "first-again" else second, again, arguments.pixel_delta)
                    if role == "first-again":
                        row["noise"] = noise
                    if noise["ssim"] < 0.995 or noise["changed"] > arguments.max_changed / 4 or \
                            noise["worst_tile"] > arguments.max_tile / 2 or \
                            abs(noise["brightness"]) > arguments.max_brightness / 2:
                        unsteady.append(f"{renderer} differs from itself too (SSIM {noise['ssim']:.3f}, "
                                        f"{noise['changed'] * 100:.2f}% changed, brightness {noise['brightness']:+.2f})")
                if unsteady:
                    # (the view isn't still: a run differs from another run of
                    # the same renderer, so a difference here says nothing of the
                    # renderers; it is shown and not counted)
                    row["reasons"] = row.get("reasons", []) + unsteady
                    if verdict == "DIFFERS":
                        verdict = "UNSTEADY"
                        row["verdict"] = verdict
                rows.append(row)
                log(f"{variant}/{key}: {verdict} SSIM {numbers['ssim']:.4f}, {numbers['changed'] * 100:.2f}% "
                    f"changed, brightness {numbers['brightness']:+.2f}")

        if arguments.bench:
            bench_results = bench(binary, [arguments.first, arguments.second], arguments, out)
        elif arguments.from_pictures:
            bench_results = kept_bench(out)
        else:
            bench_results = {}

        reasons = []
        default_rows = [row for row in rows if row["variant"] == "default"]
        looks_failed = [row for row in default_rows if row["verdict"] not in ("match", "UNSTEADY")]
        unsteady = [row for row in default_rows if row["verdict"] == "UNSTEADY"]
        second_backends = backends_seen[arguments.second]
        fell_back = arguments.second != "gl" and (not second_backends or "OpenGL" in second_backends)
        if fell_back:
            reasons.append(f"{arguments.second} fell back to OpenGL on some maps: the pictures are not of it")
        looks_ok = not looks_failed and not fell_back and bool(default_rows)
        counted = len(default_rows) - len(unsteady)
        reasons.append(f"looks: {counted - len(looks_failed)} of {counted} views match" +
                       ("" if not looks_failed else ": " + ", ".join(
                           f"{row['key']} ({'; '.join(row.get('reasons', []))})" for row in looks_failed)))
        if unsteady:
            reasons.append("not counted, the reference differs from itself there: " +
                           ", ".join(row["key"] for row in unsteady))
        speed_ok, speed_reasons = bench_verdict(bench_results, arguments.first, arguments.second,
                                                arguments.bench_margin)
        reasons += [f"frame time: {reason}" for reason in speed_reasons]
        for row in rows:
            if row["variant"] != "default":
                reasons.append(f"variant {row['variant']}/{row['key']}: {row['verdict']}"
                               + (f" ({'; '.join(row.get('reasons', []))})" if row.get("reasons") else ""))
        reasons += [f"failed: {failure}" for failure in all_failures]
        if looks_ok and speed_ok:
            verdict = "PASS"
        elif speed_ok is None and looks_ok:
            verdict = "PASS (looks only; frame time not measured)"
        else:
            verdict = "FAIL"
        summary = {
            "platform": arguments.platform or f"{platform.system()} {platform.machine()}",
            "verdict": verdict, "reasons": reasons, "first": arguments.first, "second": arguments.second,
            "first_backend": ", ".join(sorted(backends_seen[arguments.first])) or "?",
            "second_backend": ", ".join(sorted(second_backends)) or "?",
            "binary": str(binary), "date": time.strftime("%Y-%m-%d %H:%M"),
        }
        write_report(out, summary, rows, bench_results, arguments)
        (out / "results.json").write_text(json.dumps({
            "summary": summary,
            "views": [{key: (str(value) if isinstance(value, Path) else value) for key, value in row.items()}
                      for row in rows],
            "bench": bench_results}, indent=2))
        print(f"\n{summary['platform']}: {verdict}")
        for reason in reasons:
            print(f"  {reason}")
        print(f"report: {out / 'report.html'}")
        return 0 if verdict.startswith("PASS") else 1
    finally:
        if not arguments.keep:
            shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
