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
next. Maps come only from a data root of links (~/halo-maps/new-data-root.sh
on the Mac, ~/emulators/new-data-root.sh on boogerflix-1); never copies.
--maps-ce links a folder of Custom Edition maps into it when the data root
script does not. Saves and settings go to a scratch folder, never the
player's. Needs Xvfb, ImageMagick (import, convert) and Python 3 alone.
Documented in docs/picture-checks.md."""
import argparse
import math
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
POSES = ROOT / "tools" / "picture_check_poses.txt"
DATA_ROOT_SCRIPTS = [Path.home() / "halo-maps" / "new-data-root.sh", Path.home() / "emulators" / "new-data-root.sh"]
WIDTH, HEIGHT = 1280, 720
CHANGED = 40


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


def console(port, commands, wait=0.4):
    with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
        for command in commands:
            connection.sendall((command + "\n").encode())
            time.sleep(wait)
        # (the game reads the commands while the connection is open)
        connection.settimeout(1.0)
        try:
            connection.recv(65536)
        except OSError:
            pass


def console_ready(port, deadline):
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=2):
                return True
        except OSError:
            time.sleep(2)
    return False


def make_data_root(directory, maps_ce):
    script = next((path for path in DATA_ROOT_SCRIPTS if path.exists()), None)
    if not script:
        sys.exit("no new-data-root.sh in ~/halo-maps or ~/emulators: maps are only used through one")
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


def capture(build, poses, out, display, settle, environment, maps_ce, scratch):
    """Run the build once per map and capture each pose: {key: png}."""
    shots = {}
    build = stage(build, scratch)
    out.mkdir(parents=True, exist_ok=True)
    for map_name in dict.fromkeys(pose.map for pose in poses):
        data = scratch / f"data-{map_name}"
        save = scratch / f"save-{map_name}"
        shutil.rmtree(data, ignore_errors=True)
        shutil.rmtree(save, ignore_errors=True)
        make_data_root(data, maps_ce)
        save.mkdir(parents=True)
        (data / "init.txt").write_text(f"map_name {map_name}\n")
        port = free_port()
        xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", f"{WIDTH}x{HEIGHT}x24", "-nolisten", "tcp"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        log = open(out / f"{map_name}.log", "w")
        game = None
        try:
            time.sleep(1.5)
            if xvfb.poll() is not None:
                raise RuntimeError(f"Xvfb could not start on {display} (in use? pick another with --display)")
            env = dict(os.environ, DISPLAY=display, HOME=str(save), HALO_SAVE_ROOT=str(save), HALO_DATA_ROOT=str(data),
                       HALO_UPDATE_AUTO="false", HALO_DISPLAY_MODE="windowed", HALO_WINDOW_SIZE=f"{WIDTH}x{HEIGHT}",
                       HALO_EXIT_AFTER="900", SDL_AUDIODRIVER="dummy", HALO_TELNET_CONSOLE="true",
                       HALO_TELNET_CONSOLE_PORT=str(port), HALO_SOLO_GAME="1", HALO_SKIP_INTRO="true",
                       # (bgfx on Mesa's software Vulkan, which the blocklist
                       # would otherwise skip for OpenGL)
                       HALO_VULKAN_BLOCKLIST="false")
            env.update(environment)
            game = subprocess.Popen([str(build)], cwd=str(data), env=env, stdout=log, stderr=subprocess.STDOUT)
            if not console_ready(port, time.time() + 180):
                raise RuntimeError(f"{map_name}: the game's console never answered (log: {out / (map_name + '.log')})")
            # (the map loads, the player spawns, the textures stream in)
            time.sleep(settle)
            if game.poll() is not None:
                raise RuntimeError(f"{map_name}: the game stopped (logs: {out / map_name}.log, .debug.txt)")
            # (a first load to settle the camera: the player's spawn can still
            # take the view back from the first one)
            first = next(pose for pose in poses if pose.map == map_name)
            (data / "camera.txt").write_text(first.camera_text())
            console(port, ["(show_hud 0)", "(set cheat_deathless_player true)", "debug_camera_load"])
            time.sleep(4.0)
            for pose in poses:
                if pose.map != map_name:
                    continue
                (data / "camera.txt").write_text(pose.camera_text())
                console(port, ["(show_hud 0)", "(show_hud_help_text 0)", "(set cheat_deathless_player true)"])
                png = out / f"{pose.key()}.png"
                # (the camera loaded again before each capture, until two in a
                # row match: early on, the player's spawn can take the view
                # back, and a loaded camera can still drift for a moment)
                previous = None
                for attempt in range(6):
                    console(port, ["debug_camera_load"])
                    time.sleep(2.0)
                    shot = scratch / f"attempt-{attempt % 2}.png"
                    subprocess.run(["import", "-display", display, "-window", "root", str(shot)], check=True)
                    if previous and compare(shot, previous, (0, 0), 0, scratch)[0] < 0.002:
                        break
                    previous = shot
                else:
                    print(f"  {pose.key()}: the picture never held still")
                shutil.copy(shot, png)
                shots[pose.key()] = png
                print(f"  {pose.key()}: captured")
        finally:
            if game:
                game.terminate()
                try:
                    game.wait(15)
                except subprocess.TimeoutExpired:
                    game.kill()
            xvfb.terminate()
            # (the display is free again before the next map's run)
            try:
                xvfb.wait(15)
            except subprocess.TimeoutExpired:
                xvfb.kill()
                xvfb.wait()
            log.close()
            for debug in (save / "debug.txt", data / "debug.txt"):
                if debug.exists():
                    shutil.copy(debug, out / f"{map_name}.debug.txt")
            if (data / "logs").is_dir():
                shutil.copytree(data / "logs", out / f"{map_name}-logs", dirs_exist_ok=True)
            shutil.rmtree(data, ignore_errors=True)
            shutil.rmtree(save, ignore_errors=True)
    return shots


def settings(pairs):
    environment = {}
    for pair in pairs or []:
        name, _, value = pair.partition("=")
        environment[name] = value
    return environment


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--build", required=True, help="the Linux build to check (its chupathingy executable)")
    parser.add_argument("--out", required=True, type=Path, help="where the captures and report go")
    parser.add_argument("--reference", type=Path, help="captures from an earlier run to compare with")
    parser.add_argument("--reference-build", help="a build to run at the same poses and compare with")
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
    arguments = parser.parse_args()

    poses = read_poses(arguments.poses, arguments.maps)
    if not poses:
        sys.exit("no poses")
    for tool in ("Xvfb", "import", "convert"):
        if not shutil.which(tool):
            sys.exit(f"{tool} is needed")
    out = arguments.out.resolve()
    scratch = Path(tempfile.mkdtemp(prefix="picture-check-"))
    try:
        print(f"checked build: {arguments.build}")
        shots = capture(Path(arguments.build).resolve(), poses, out, arguments.display, arguments.settle,
                        settings(arguments.env), arguments.maps_ce, scratch)
        if arguments.reference_build or arguments.reference_env:
            reference_build = Path(arguments.reference_build or arguments.build).resolve()
            print(f"reference build: {reference_build} {' '.join(arguments.reference_env or [])}")
            references = capture(reference_build, poses, out / "reference", arguments.display, arguments.settle,
                                 settings(arguments.reference_env), arguments.maps_ce, scratch)
        elif arguments.reference:
            references = {pose.key(): arguments.reference / f"{pose.key()}.png" for pose in poses
                          if (arguments.reference / f"{pose.key()}.png").exists()}
        else:
            print(f"captured {len(shots)} poses in {out}; no reference to compare with")
            return 0
        failed = 0
        lines = []
        for pose in poses:
            key = pose.key()
            if key not in shots or key not in references:
                lines.append(f"{key}: MISSING (no capture or no reference)")
                failed += 1
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
            failed += verdict != "ok"
            lines.append(f"{key}: {verdict}: {changed * 100:.2f}% changed, {holes} sky-colored pixels where the "
                         "reference has none")
        report = "\n".join(lines) + f"\n{len(poses) - failed} of {len(poses)} poses ok\n"
        (out / "report.txt").write_text(report)
        print(report, end="")
        return 1 if failed else 0
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
