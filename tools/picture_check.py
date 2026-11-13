"""Picture checks: our Linux build's view from fixed camera spots, compared
with a reference, and holes in the level flagged.

    python3 tools/picture_check.py --build build/linux64/chupathingy --out /tmp/shots \\
        --reference-build ~/old/chupathingy

For each map in the pose list (tools/picture_check_poses.txt) the build is
started on its own Xvfb display in a solo game, the HUD hidden, and for each
pose the debug camera is put there (camera.txt, debug_camera_load) and the
screen captured. Then each capture is compared with the reference's capture
of the same pose:

- holes: pixels that look like the sky (the pose's sky sample, within a
  tolerance) where the reference shows something else. A missing cliff or
  piece of ground shows the sky through it. More than --max-holes such
  pixels fails the pose, and a picture of where they are is saved
  (<map>_<pose>_holes.png, holes in red).
- changed: the share of pixels that differ by more than 40 in any channel.
  More than --tolerance fails the pose (other picture regressions).

The reference is one of:
- --reference-build: another build (a release you trust), run the same way;
- --reference-env: the same build again with these settings, such as
  HALO_STRUCTURE_CULLING=false where the build has that switch;
- --reference DIR: captures kept from an earlier run (<map>_<pose>.png).

Without a reference it only captures, so a run makes the reference for the
next. Maps come only from a data root of links, made by the script that
HALO_DATA_ROOT_SCRIPT names (it takes the folder to make); never copies.
--maps-ce links a folder of Custom Edition maps into it when the data root
script does not. Saves and settings go to a scratch folder, never the
player's. Needs Xvfb, ImageMagick (import, convert, compare) and Python 3
alone. Documented in docs/picture-checks.md.

More for a regression run over many maps:
- --reference-release v0.9.0d: the reference is that release's Linux build,
  downloaded from GitHub and checked against the release's SHA256SUMS.
- --report: report.md and report.html in --out, each spot's reference,
  capture and difference side by side, the failures first.
- A map that doesn't load (the game stops, its console never answers) is
  reported as FAILED and the run goes on with the next map. A Custom
  Edition map is skipped when there are no Custom Edition maps to link.
- --find-poses FILE: no pictures; each --map is loaded and the player's
  spawn view is written to FILE as spots (looking ahead, left and back,
  a little above the player's eyes), to add to the pose list."""
import argparse
import hashlib
import html
import math
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
POSES = ROOT / "tools" / "picture_check_poses.txt"
DATA_ROOT_SCRIPT = os.environ.get("HALO_DATA_ROOT_SCRIPT", "")
WIDTH, HEIGHT = 1280, 720
CHANGED = 40
RELEASES = "https://github.com/ChupathingyCE/chupathingyce/releases/download"
RELEASE_ZIP = "chupathingyce-linux64-release.zip"


class Pose:
    def __init__(self, line):
        fields = line.split()
        if len(fields) not in (7, 9):
            raise ValueError(f"pose line needs: map name x y z yaw pitch [sky_x sky_y]: {line}")
        self.map, self.name = fields[0], fields[1]
        self.x, self.y, self.z, self.yaw, self.pitch = map(float, fields[2:7])
        # (where in the picture the sky is, to know its color: by default
        # the middle of the top rows)
        self.sky = (int(fields[7]), int(fields[8])) if len(fields) == 9 else (WIDTH // 2, 8)

    def key(self):
        return f"{self.map}_{self.name}"

    def camera_text(self):
        yaw, pitch = math.radians(self.yaw), math.radians(self.pitch)
        forward = (math.cos(yaw) * math.cos(pitch), math.sin(yaw) * math.cos(pitch), math.sin(pitch))
        up = (-math.cos(yaw) * math.sin(pitch), -math.sin(yaw) * math.sin(pitch), math.cos(pitch))
        # (director.c's camera.txt: position, forward, up, field of view)
        return "\n".join(["%f %f %f" % (self.x, self.y, self.z), "%f %f %f" % forward, "%f %f %f" % up,
                          "%f" % math.radians(70.0)]) + "\n"


def read_poses(path, maps):
    poses = []
    for line in Path(path).read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            pose = Pose(line)
            if not maps or pose.map in maps:
                poses.append(pose)
    return poses


# ---------- pictures (binary PPM, read with Python alone)

def read_ppm(path):
    data = Path(path).read_bytes()
    fields, offset = [], 0
    while len(fields) < 4:
        while data[offset:offset + 1].isspace():
            offset += 1
        if data[offset:offset + 1] == b"#":
            offset = data.index(b"\n", offset)
            continue
        end = offset
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[offset:end])
        offset = end
    if fields[0] != b"P6" or fields[3] != b"255":
        raise ValueError(f"{path}: not an 8-bit binary PPM")
    width, height = int(fields[1]), int(fields[2])
    pixels = data[offset + 1:offset + 1 + width * height * 3]
    return width, height, pixels


def to_ppm(png, ppm):
    # (8-bit color always: ImageMagick writes a gray PPM for a picture with
    # no color in it, a black cutscene frame)
    subprocess.run(["convert", str(png), "-alpha", "off", "-type", "TrueColor", "-depth", "8", f"ppm:{ppm}"],
                   check=True)


def pixels(path, work):
    """(width, height, RGB bytes) of a capture: PPM as it is, others through ImageMagick"""
    path = Path(path)
    if path.suffix != ".ppm":
        converted = work / (path.stem + ".ppm")
        to_ppm(path, converted)
        path = converted
    return read_ppm(path)


EDGE = 4


def near_sky(reference, width, height, pixel, sky_color, sky_tolerance):
    """Whether the reference has sky within EDGE pixels: a skyline a pixel or
    two off is no hole"""
    x, y = pixel % width, pixel // width
    for near_y in range(max(y - EDGE, 0), min(y + EDGE + 1, height)):
        for near_x in range(max(x - EDGE, 0), min(x + EDGE + 1, width)):
            at = (near_y * width + near_x) * 3
            if max(abs(reference[at] - sky_color[0]), abs(reference[at + 1] - sky_color[1]),
                   abs(reference[at + 2] - sky_color[2])) <= sky_tolerance:
                return True
    return False


def compare(test_png, reference_png, sky, sky_tolerance, work):
    """(changed share, hole pixels, hole mask as PPM bytes or None)"""
    width, height, test = pixels(test_png, work)
    reference_width, reference_height, reference = pixels(reference_png, work)
    if (width, height) != (reference_width, reference_height):
        raise ValueError(f"{test_png.name}: {width}x{height}, the reference {reference_width}x{reference_height}")
    sky_x, sky_y = min(sky[0], width - 1), min(sky[1], height - 1)
    at = (sky_y * width + sky_x) * 3
    sky_color = reference[at:at + 3]
    changed = holes = 0
    mask = bytearray(test)
    for index in range(0, len(test), 3):
        t0, t1, t2 = test[index], test[index + 1], test[index + 2]
        r0, r1, r2 = reference[index], reference[index + 1], reference[index + 2]
        if max(abs(t0 - r0), abs(t1 - r1), abs(t2 - r2)) <= CHANGED:
            continue
        changed += 1
        test_sky = max(abs(t0 - sky_color[0]), abs(t1 - sky_color[1]), abs(t2 - sky_color[2])) <= sky_tolerance
        reference_sky = max(abs(r0 - sky_color[0]), abs(r1 - sky_color[1]), abs(r2 - sky_color[2])) <= sky_tolerance
        if test_sky and not reference_sky and not near_sky(reference, width, height, index // 3, sky_color,
                                                          sky_tolerance):
            holes += 1
            mask[index:index + 3] = b"\xff\x00\x00"
    header = b"P6\n%d %d\n255\n" % (width, height)
    return changed / (width * height), holes, header + bytes(mask) if holes else None


# ---------- running a build

def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def console(connection, commands, wait=0.4):
    for command in commands:
        connection.sendall((command + "\n").encode())
        time.sleep(wait)
    # (what the game answered, read so that its buffer never fills)
    try:
        connection.recv(65536)
    except OSError:
        pass


def console_connect(port, deadline):
    """One connection to the game's telnet console, kept for the whole run,
    once the game has greeted it. The game reads its first client's commands
    only, and drops a connection it has no room for, so a new connection for
    each command (or for each try while the map loads) ran out of room on a
    busy machine: the connection was reset."""
    while time.time() < deadline:
        try:
            connection = socket.create_connection(("127.0.0.1", port), timeout=2)
        except OSError:
            time.sleep(2)
            continue
        # (the greeting comes once the game's loop runs: the map can still
        # be loading)
        connection.settimeout(max(1.0, min(60.0, deadline - time.time())))
        try:
            if b"play a game" in connection.recv(256):
                connection.settimeout(1.0)
                return connection
        except OSError:
            pass
        connection.close()
        time.sleep(2)
    return None


def make_data_root(directory, maps_ce):
    script = Path(DATA_ROOT_SCRIPT).expanduser() if DATA_ROOT_SCRIPT else None
    if not script or not script.exists():
        sys.exit("set HALO_DATA_ROOT_SCRIPT to a script that makes a data root of links to your maps: maps are only used through one")
    subprocess.run(["bash", str(script), str(directory)], check=True, stdout=subprocess.DEVNULL)
    if maps_ce and not (directory / "maps_ce").exists():
        (directory / "maps_ce").symlink_to(Path(maps_ce).resolve())


def stage(build, scratch):
    """The build in a folder of its own: the game keeps config.toml next to its
    executable, so a run must neither read nor change the one a build has.
    The executable is linked (or copied), the rest of its folder linked."""
    folder = scratch / f"build-{len(list(scratch.glob('build-*')))}"
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


class Skipped(Exception):
    """a map this run can't load, such as a Custom Edition map with no
    Custom Edition maps linked"""


class Run:
    """The build running one map in a solo game on its own Xvfb display, with
    its telnet console on a free port. Its logs are kept in out, and its data
    root and saves are deleted when it stops."""

    def __init__(self, build, map_name, out, display, settle, environment, maps_ce, scratch):
        self.build, self.map_name, self.out, self.display = build, map_name, out, display
        self.settle, self.environment, self.maps_ce, self.scratch = settle, environment, maps_ce, scratch
        self.data = scratch / f"data-{map_name}"
        self.save = scratch / f"save-{map_name}"
        self.port = free_port()
        self.xvfb = self.game = self.log = self.connection = None

    def __enter__(self):
        try:
            self.start()
        except BaseException:
            self.stop()
            raise
        return self

    def __exit__(self, *_):
        self.stop()

    def start(self):
        shutil.rmtree(self.data, ignore_errors=True)
        shutil.rmtree(self.save, ignore_errors=True)
        make_data_root(self.data, self.maps_ce)
        if self.map_name.endswith("@ce") and not (self.data / "maps_ce").exists():
            raise Skipped("no Custom Edition maps linked (--maps-ce)")
        self.save.mkdir(parents=True)
        (self.data / "init.txt").write_text(f"map_name {self.map_name}\n")
        self.xvfb = subprocess.Popen(["Xvfb", self.display, "-screen", "0", f"{WIDTH}x{HEIGHT}x24", "-nolisten", "tcp"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.log = open(self.out / f"{self.map_name}.log", "w")
        time.sleep(1.5)
        if self.xvfb.poll() is not None:
            raise RuntimeError(f"Xvfb could not start on {self.display} (in use? pick another with --display)")
        env = dict(os.environ, DISPLAY=self.display, HOME=str(self.save), HALO_SAVE_ROOT=str(self.save),
                   HALO_DATA_ROOT=str(self.data), HALO_UPDATE_AUTO="false", HALO_DISPLAY_MODE="windowed",
                   HALO_WINDOW_SIZE=f"{WIDTH}x{HEIGHT}", HALO_EXIT_AFTER="900", SDL_AUDIODRIVER="dummy",
                   HALO_TELNET_CONSOLE="true", HALO_TELNET_CONSOLE_PORT=str(self.port), HALO_SOLO_GAME="1",
                   HALO_SKIP_INTRO="true",
                   # (bgfx on Mesa's software Vulkan, which the blocklist
                   # would otherwise skip for OpenGL)
                   HALO_VULKAN_BLOCKLIST="false")
        env.update(self.environment)
        self.game = subprocess.Popen([str(self.build)], cwd=str(self.data), env=env, stdout=self.log,
                                     stderr=subprocess.STDOUT)
        self.connection = console_connect(self.port, time.time() + 180)
        if not self.connection:
            raise RuntimeError("the game's console never answered" if self.game.poll() is None else
                               f"the game stopped while it started (exit {self.game.returncode})")
        # (the map loads, the player spawns, the textures stream in)
        time.sleep(self.settle)
        self.check()

    def check(self):
        if self.game.poll() is not None:
            raise RuntimeError(f"the game stopped (exit {self.game.returncode})")

    def stop(self):
        if self.connection:
            self.connection.close()
        if self.game:
            self.game.terminate()
            try:
                self.game.wait(15)
            except subprocess.TimeoutExpired:
                self.game.kill()
                self.game.wait()
        if self.xvfb:
            self.xvfb.terminate()
            # (the display is free again before the next map's run)
            try:
                self.xvfb.wait(15)
            except subprocess.TimeoutExpired:
                self.xvfb.kill()
                self.xvfb.wait()
        if self.log:
            self.log.close()
        for debug in (self.save / "debug.txt", self.data / "debug.txt"):
            if debug.exists():
                shutil.copy(debug, self.out / f"{self.map_name}.debug.txt")
        # (the save root's logs/ since #1148, the data root's before: run_logs.py)
        for logs in (self.save / "logs", self.data / "logs"):
            if logs.is_dir():
                shutil.copytree(logs, self.out / f"{self.map_name}-logs", dirs_exist_ok=True)
        shutil.rmtree(self.data, ignore_errors=True)
        shutil.rmtree(self.save, ignore_errors=True)


def write_camera(pose, save, data):
    """camera.txt for debug_camera_load: in the save root (director.c, since
    the debug camera's file became the player's), and in the data root for a
    build from before that"""
    for folder in (save, data):
        (folder / "camera.txt").write_text(pose.camera_text())


def capture(build, poses, out, display, settle, environment, maps_ce, scratch):
    """Run the build once per map and capture each pose: ({key: png}, {map:
    why it has no pictures}, {key: a note}). A map that fails is reported,
    and the run goes on with the next."""
    shots, failures, notes = {}, {}, {}
    build = stage(build, scratch)
    out.mkdir(parents=True, exist_ok=True)
    for map_name in dict.fromkeys(pose.map for pose in poses):
        try:
            with Run(build, map_name, out, display, settle, environment, maps_ce, scratch) as run:
                # (a first load to settle the camera: the player's spawn can
                # still take the view back from the first one)
                first = next(pose for pose in poses if pose.map == map_name)
                write_camera(first, run.save, run.data)
                console(run.connection, ["(show_hud 0)", "(set cheat_deathless_player true)", "debug_camera_load"])
                time.sleep(4.0)
                for pose in poses:
                    if pose.map != map_name:
                        continue
                    run.check()
                    write_camera(pose, run.save, run.data)
                    console(run.connection, ["(show_hud 0)", "(show_hud_help_text 0)", "(set cheat_deathless_player true)"])
                    png = out / f"{pose.key()}.png"
                    # (the camera loaded again before each capture, until two
                    # in a row match: early on, the player's spawn can take
                    # the view back, and a loaded camera can still drift for
                    # a moment)
                    previous = None
                    for attempt in range(6):
                        console(run.connection, ["debug_camera_load"])
                        time.sleep(2.0)
                        shot = scratch / f"attempt-{attempt % 2}.png"
                        subprocess.run(["import", "-display", display, "-window", "root", str(shot)], check=True)
                        if previous and compare(shot, previous, (0, 0), 0, scratch)[0] < 0.002:
                            break
                        previous = shot
                    else:
                        notes[pose.key()] = "the picture never held still"
                        print(f"  {pose.key()}: the picture never held still")
                    shutil.copy(shot, png)
                    shots[pose.key()] = png
                    print(f"  {pose.key()}: captured")
        except Skipped as reason:
            failures[map_name] = f"skipped: {reason}"
            print(f"  {map_name}: skipped: {reason}")
        except (RuntimeError, OSError, subprocess.CalledProcessError) as reason:
            failures[map_name] = f"{reason} (logs: {map_name}.log, {map_name}.debug.txt)"
            print(f"  {map_name}: FAILED: {reason}")
    return shots, failures, notes


def find_poses(build, maps, poses_file, out, display, settle, environment, maps_ce, scratch):
    """Load each map and write the player's spawn view as spots: ahead, to
    the left and back, a little above the eyes, so that the player's own
    body stays out of the picture."""
    build = stage(build, scratch)
    out.mkdir(parents=True, exist_ok=True)
    lines = []
    for map_name in maps:
        try:
            with Run(build, map_name, out, display, settle, environment, maps_ce, scratch) as run:
                console(run.connection, ["debug_camera_save"])
                time.sleep(1.0)
                # (the save root's camera.txt since the debug camera's file became
                # the player's, the data root's before)
                saved = next((folder / "camera.txt" for folder in (run.save, run.data)
                              if (folder / "camera.txt").exists()), run.save / "camera.txt")
                fields = saved.read_text().split()
        except (Skipped, RuntimeError, OSError) as reason:
            print(f"  {map_name}: no spawn: {reason}")
            continue
        x, y, z, forward_x, forward_y = map(float, fields[:5])
        yaw = math.degrees(math.atan2(forward_y, forward_x))
        for name, turn in (("spawn", 0), ("spawn_left", 90), ("spawn_back", 180)):
            lines.append(f"{map_name} {name} {x:.2f} {y:.2f} {z + 0.25:.2f} {(yaw + turn + 180) % 360 - 180:.0f} 0")
        print(f"  {map_name}: spawn at {x:.2f} {y:.2f} {z:.2f}, facing {yaw:.0f}")
    Path(poses_file).write_text("\n".join(lines) + "\n")
    print(f"{len(lines)} spots in {poses_file}")


def release_build(tag, scratch):
    """A release's Linux build (x86-64), downloaded and checked against the
    release's SHA256SUMS: its chupathingy executable."""
    folder = scratch / f"release-{tag}"
    folder.mkdir()
    archive = folder / RELEASE_ZIP
    for name in (RELEASE_ZIP, "SHA256SUMS"):
        url = f"{RELEASES}/{tag}/{name}"
        print(f"downloading {url}")
        with urllib.request.urlopen(url, timeout=120) as response, open(folder / name, "wb") as file:
            shutil.copyfileobj(response, file)
    expected = None
    for line in (folder / "SHA256SUMS").read_text().splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[1].lstrip("*") == RELEASE_ZIP:
            expected = fields[0].lower()
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != expected:
        sys.exit(f"{RELEASE_ZIP} of {tag}: sha256 {digest}, SHA256SUMS says {expected}")
    build = folder / "build"
    with zipfile.ZipFile(archive) as files:
        files.extractall(build)
    archive.unlink()
    # (zipfile keeps no file modes)
    for name in ("chupathingy", "halo"):
        if (build / name).exists():
            (build / name).chmod(0o755)
    if not (build / "chupathingy").exists():
        sys.exit(f"{RELEASE_ZIP} of {tag} has no chupathingy")
    return build / "chupathingy"


def settings(pairs):
    environment = {}
    for pair in pairs or []:
        name, _, value = pair.partition("=")
        environment[name] = value
    return environment


# ---------- the report

def diff_picture(test, reference, path):
    """ImageMagick's picture of the difference: the reference faded, the
    pixels that differ by more than CHANGED in red"""
    # (compare exits with 1 when the pictures differ)
    subprocess.run(["compare", "-fuzz", f"{CHANGED * 100 / 255:.1f}%", str(reference), str(test), str(path)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return path if path.exists() else None


ORDER = {"FAILED": 0, "HOLES": 1, "CHANGED": 2, "MISSING": 3, "SKIPPED": 4, "ok": 5}


def write_report(out, rows, checked, reference, arguments):
    """report.md and report.html in out: each spot's reference, capture and
    difference, the failures first"""
    rows = sorted(rows, key=lambda row: (ORDER[row["verdict"]], -row.get("changed", 0)))
    counts = {}
    for row in rows:
        counts[row["verdict"]] = counts.get(row["verdict"], 0) + 1
    summary = ", ".join(f"{count} {verdict}" for verdict, count in sorted(counts.items(), key=lambda i: ORDER[i[0]]))
    limits = (f"a spot fails with more than {arguments.tolerance * 100:g}% of its pixels changed by more than "
              f"{CHANGED} in a color, or more than {arguments.max_holes} sky-colored pixels where the reference "
              "has none")

    def link(path):
        return os.path.relpath(path, out) if path else None

    def numbers(row):
        if "changed" not in row:
            return ""
        return f"{row['changed'] * 100:.2f}% changed, {row['holes']} hole pixels"

    md = [f"# Picture check: {checked} against {reference}", "", f"{len(rows)} spots: {summary}.", "",
          f"{limits[0].upper()}{limits[1:]}.", "", "| Spot | Result | Numbers | Note |", "|---|---|---|---|"]
    for row in rows:
        md.append(f"| {row['key']} | {row['verdict']} | {numbers(row)} | {row.get('note', '')} |")
    for row in rows:
        if row["verdict"] in ("ok", "SKIPPED"):
            continue
        md += ["", f"## {row['key']}: {row['verdict']}", ""]
        if row.get("note"):
            md += [row["note"], ""]
        pictures = [("reference", row.get("reference")), ("checked", row.get("test")), ("difference", row.get("diff")),
                    ("holes", row.get("holes_png"))]
        md.append(" ".join(f"![{name}]({link(path)})" for name, path in pictures if path))
    (out / "report.md").write_text("\n".join(md) + "\n")

    def figure(name, path):
        if not path:
            return f'<figure class="none"><figcaption>{name}</figcaption><div>none</div></figure>'
        href = html.escape(link(path))
        return (f'<figure><a href="{href}"><img loading="lazy" src="{href}" alt="{name}"></a>'
                f"<figcaption>{name}</figcaption></figure>")

    sections = []
    for row in rows:
        verdict = row["verdict"]
        pictures = "".join(figure(name, row.get(key)) for name, key in
                           (("reference", "reference"), ("checked", "test"), ("difference", "diff")))
        if row.get("holes_png"):
            pictures += figure("holes", row["holes_png"])
        body = (f'<h2><span class="verdict v-{verdict.lower()}">{verdict}</span> {html.escape(row["key"])}</h2>'
                f'<p>{html.escape(numbers(row))} {html.escape(row.get("note", ""))}</p>'
                f'<div class="pictures">{pictures}</div>')
        if verdict in ("ok", "SKIPPED"):
            sections.append(f'<details><summary>{verdict} {html.escape(row["key"])} '
                            f'{html.escape(numbers(row) or row.get("note", ""))}</summary>{body}</details>')
        else:
            sections.append(f"<section>{body}</section>")
    page = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Picture check</title>
<style>
:root {{ --bg: #fff; --fg: #1d1d1f; --muted: #6e6e73; --line: #d2d2d7; --bad: #c4291c; --warn: #b26b00; --good: #2a7a2a; }}
@media (prefers-color-scheme: dark) {{
  :root {{ --bg: #161617; --fg: #f0f0f2; --muted: #a1a1a6; --line: #3a3a3c; --bad: #ff6b5e; --warn: #ffb340;
           --good: #6fcf6f; }}
}}
body {{ background: var(--bg); color: var(--fg); font: 15px/1.45 system-ui, sans-serif; margin: 0 auto;
        max-width: 1500px; padding: 16px; }}
h1 {{ font-size: 22px; }} h2 {{ font-size: 17px; margin: 0 0 4px; }}
p {{ color: var(--muted); margin: 0 0 8px; }}
section, details {{ border-top: 1px solid var(--line); padding: 12px 0; }}
summary {{ cursor: pointer; color: var(--muted); }}
.pictures {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 8px; }}
figure {{ margin: 0; }} img {{ width: 100%; height: auto; display: block; border: 1px solid var(--line); }}
figcaption {{ color: var(--muted); font-size: 13px; }}
.none div {{ border: 1px dashed var(--line); padding: 40px 0; text-align: center; color: var(--muted); }}
.verdict {{ font-size: 12px; font-weight: 600; padding: 2px 6px; border-radius: 4px; border: 1px solid; }}
.v-failed, .v-holes {{ color: var(--bad); }} .v-changed, .v-missing {{ color: var(--warn); }}
.v-ok {{ color: var(--good); }} .v-skipped {{ color: var(--muted); }}
</style></head><body>
<h1>Picture check: {html.escape(str(checked))} against {html.escape(str(reference))}</h1>
<p>{len(rows)} spots: {html.escape(summary)}. {html.escape(limits[0].upper() + limits[1:])}.
The difference shows the reference faded, with the pixels that changed in red.</p>
{"".join(sections)}
</body></html>
"""
    (out / "report.html").write_text(page)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--build", required=True, help="the Linux build to check (its chupathingy executable)")
    parser.add_argument("--out", required=True, type=Path, help="where the captures and report go")
    parser.add_argument("--reference", type=Path, help="captures from an earlier run to compare with")
    parser.add_argument("--reference-build", help="a build to run at the same poses and compare with")
    parser.add_argument("--reference-release", metavar="TAG",
                        help="a release's Linux build (v0.9.0d), downloaded, to run at the same poses and compare with")
    parser.add_argument("--reference-env", action="append", metavar="NAME=VALUE",
                        help="settings for the reference run (the same build, unless --reference-build)")
    parser.add_argument("--env", action="append", metavar="NAME=VALUE", help="settings for the checked build's run")
    parser.add_argument("--poses", default=str(POSES), help="the pose list (default tools/picture_check_poses.txt)")
    parser.add_argument("--map", action="append", dest="maps", help="only this map's poses (repeatable)")
    parser.add_argument("--maps-ce", help="a folder of Custom Edition maps to link in as maps_ce")
    parser.add_argument("--display", default=":91", help="the Xvfb display to use (default :91)")
    parser.add_argument("--settle", type=float, default=45.0, help="seconds after the console answers (default 45)")
    parser.add_argument("--tolerance", type=float, default=0.03, help="the share of pixels that may change (0.03)")
    parser.add_argument("--max-holes", type=int, default=150, help="sky-colored pixels allowed where the reference "
                        "has none (150)")
    parser.add_argument("--sky-tolerance", type=int, default=18, help="how close to the sky sample is sky (18)")
    parser.add_argument("--report", action="store_true", help="also write report.md and report.html, with "
                        "pictures of the differences")
    parser.add_argument("--label", help="the checked build's name in the report (default its path)")
    parser.add_argument("--find-poses", metavar="FILE", help="no pictures: write each --map's spawn view to FILE "
                        "as spots")
    arguments = parser.parse_args()

    for tool in ("Xvfb", "import", "convert") + (("compare",) if arguments.report else ()):
        if not shutil.which(tool):
            sys.exit(f"{tool} is needed")
    out = arguments.out.resolve()
    scratch = Path(tempfile.mkdtemp(prefix="picture-check-"))
    try:
        if arguments.find_poses:
            if not arguments.maps:
                sys.exit("--find-poses needs the maps (--map)")
            find_poses(Path(arguments.build).resolve(), arguments.maps, arguments.find_poses, out, arguments.display,
                       arguments.settle, settings(arguments.env), arguments.maps_ce, scratch)
            return 0
        poses = read_poses(arguments.poses, arguments.maps)
        if not poses:
            sys.exit("no poses")
        reference_label = None
        if arguments.reference_release:
            # (first, so that a release that isn't there stops the run early)
            reference_build = release_build(arguments.reference_release, scratch)
            reference_label = arguments.reference_release
        print(f"checked build: {arguments.build}")
        shots, failures, notes = capture(Path(arguments.build).resolve(), poses, out, arguments.display,
                                         arguments.settle, settings(arguments.env), arguments.maps_ce, scratch)
        reference_failures = {}
        if arguments.reference_release or arguments.reference_build or arguments.reference_env:
            if not arguments.reference_release:
                reference_build = Path(arguments.reference_build or arguments.build).resolve()
                reference_label = f"{reference_build} {' '.join(arguments.reference_env or [])}".strip()
            print(f"reference build: {reference_label}")
            references, reference_failures, reference_notes = capture(
                reference_build, poses, out / "reference", arguments.display, arguments.settle,
                settings(arguments.reference_env), arguments.maps_ce, scratch)
            for key, note in reference_notes.items():
                notes[key] = "; ".join(filter(None, (notes.get(key), f"reference: {note}")))
        elif arguments.reference:
            references = {pose.key(): arguments.reference / f"{pose.key()}.png" for pose in poses
                          if (arguments.reference / f"{pose.key()}.png").exists()}
            reference_label = str(arguments.reference)
        else:
            print(f"captured {len(shots)} poses in {out}; no reference to compare with")
            for map_name, reason in failures.items():
                print(f"{map_name}: {reason}")
            return 1 if any(not reason.startswith("skipped") for reason in failures.values()) else 0
        failed = 0
        lines = []
        rows = []
        for pose in poses:
            key = pose.key()
            row = {"key": key, "test": shots.get(key), "reference": references.get(key)}
            if key in notes:
                row["note"] = notes[key]
            reasons = [reason for reason in (failures.get(pose.map), reference_failures.get(pose.map)) if reason]
            if reasons and all(reason.startswith("skipped") for reason in reasons):
                row.update(verdict="SKIPPED", note=reasons[0])
                lines.append(f"{key}: SKIPPED ({reasons[0]})")
                rows.append(row)
                continue
            if pose.map in failures or pose.map in reference_failures:
                note = "; ".join(filter(None, (failures.get(pose.map) and f"checked: {failures[pose.map]}",
                                               reference_failures.get(pose.map) and
                                               f"reference: {reference_failures[pose.map]}")))
                row.update(verdict="FAILED", note=note)
                lines.append(f"{key}: FAILED ({note})")
                failed += 1
                rows.append(row)
                continue
            if key not in shots or key not in references:
                row.update(verdict="MISSING", note="no capture or no reference")
                lines.append(f"{key}: MISSING (no capture or no reference)")
                failed += 1
                rows.append(row)
                continue
            changed, holes, mask = compare(shots[key], references[key], pose.sky, arguments.sky_tolerance, scratch)
            verdict = "ok"
            if holes > arguments.max_holes:
                verdict = "HOLES"
            elif changed > arguments.tolerance:
                verdict = "CHANGED"
            if mask:
                (scratch / "holes.ppm").write_bytes(mask)
                subprocess.run(["convert", str(scratch / "holes.ppm"), str(out / f"{key}_holes.png")], check=True)
                row["holes_png"] = out / f"{key}_holes.png"
            if arguments.report:
                row["diff"] = diff_picture(shots[key], references[key], out / f"{key}_diff.png")
            row.update(verdict=verdict, changed=changed, holes=holes)
            rows.append(row)
            failed += verdict != "ok"
            lines.append(f"{key}: {verdict}: {changed * 100:.2f}% changed, {holes} sky-colored pixels where the "
                         "reference has none")
        report = "\n".join(lines) + f"\n{len(poses) - failed} of {len(poses)} poses ok\n"
        (out / "report.txt").write_text(report)
        print(report, end="")
        if arguments.report:
            write_report(out, rows, arguments.label or arguments.build, reference_label, arguments)
            print(f"report: {out / 'report.html'}")
        return 1 if failed else 0
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
