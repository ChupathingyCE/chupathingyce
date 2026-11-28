#!/usr/bin/env python3
"""Screenshots and frame times of display.ray_tracing and display.glow
(port/linux/src/modern_lighting.h), each view off against traced, from one
run of each map, the modes switched by the lab file (HALO_ML_LAB) so that
every mode sees the same moment (the game paused with game_speed 0 for the
fixed views).

    python3 tools/modern_lighting_lab.py --build build/linux64/chupathingy \\
        --maps ~/maps --out ~/rtx-proto-out --views tools/modern_lighting_views.txt

Each run is headless (SDL's offscreen video, EGL on the GPU DRI_PRIME names),
off the network (a network namespace of its own: unshare -rn, with only its
loopback for the telnet console), with its own data, save and home folders,
which are deleted when it ends. Pictures: <out>/<map>-<view>-<mode>.png
(the window as shown, 1920x1080). Frame times: <out>/frame_times.tsv.

--self-test: the white-frame check (a frame the ray-traced composite
brightened, or one near white against its neighbors, fails it; exit 1):

    python3 tools/modern_lighting_lab.py --self-test --build build/linux64/chupathingy \\
        --maps ~/maps --out /tmp/ml-self-test [--map a10]

The views file: "map view x y z yaw pitch [hud]" (a debug camera, as
tools/picture_check_poses.txt), or "map view player [hud]" for the player's
own view; "hud" keeps the HUD.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import math
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

MODES = {
    # name: lab commands
    "off": ["rt off", "hdr off"],
    "trackA": ["rt ao", "hdr off"],
    "glow": ["rt off", "hdr on", "glow shafts"],
    # (round 2: the trace's scene, the collision models (round 1's) against
    # the render meshes)
    "rt_collision": ["rt ao", "hdr off", "geometry collision"],
    "rt_meshes": ["rt ao", "hdr off", "geometry render"],
    "rt_shadows_collision": ["rt shadows", "hdr off", "geometry collision"],
    "rt_shadows_meshes": ["rt shadows", "hdr off", "geometry render"],
    # (the trace's own factor, debug view 1)
    "rt_collision_factor": ["rt ao", "hdr off", "geometry collision", "debug 1"],
    "rt_meshes_factor": ["rt ao", "hdr off", "geometry render", "debug 1"],
}
# (each mode's commands start from none: the glow off unless asked, the
# trace's scene the setting's)
for _name, _commands in MODES.items():
    if not any(c.startswith("glow") for c in _commands):
        _commands.append("glow off")
    if not any(c.startswith("geometry") for c in _commands):
        _commands.append("geometry config")
    if not any(c.startswith("debug") for c in _commands):
        _commands.append("debug 0")

GPU_ENV = {
    "SDL_VIDEODRIVER": "offscreen",
    "SDL_HINT_EGL_DEVICE": "1",
    "DRI_PRIME": "pci-0000_03_00_0",
}


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class Run:
    def __init__(self, build, maps, map_name, work, size, extra_env):
        self.build, self.maps, self.map_name, self.work = build, maps, map_name, work
        self.size, self.extra_env = size, extra_env
        self.root = work / f"run-{map_name}"
        self.data, self.save = self.root / "data", self.root / "save"
        self.lab = self.root / "lab.cmd"
        self.game = None
        self.connection = None

    def start(self):
        shutil.rmtree(self.root, ignore_errors=True)
        self.data.mkdir(parents=True)
        self.save.mkdir(parents=True)
        (self.data / "maps").symlink_to(self.maps)
        (self.data / "init.txt").write_text(f"map_name {self.map_name}\n")
        port = 2323
        env = {k: v for k, v in os.environ.items() if k != "DISPLAY"}
        env.update(GPU_ENV)
        env.update({
            "HOME": str(self.save), "HALO_SAVE_ROOT": str(self.save), "HALO_DATA_ROOT": str(self.data),
            "HALO_UPDATE_AUTO": "false", "HALO_DISPLAY_MODE": "windowed", "HALO_WINDOW_SIZE": self.size,
            "HALO_EXIT_AFTER": "1500", "SDL_AUDIODRIVER": "dummy", "HALO_TELNET_CONSOLE": "true",
            "HALO_TELNET_CONSOLE_PORT": str(port), "HALO_SOLO_GAME": "1", "HALO_SKIP_INTRO": "true",
            "HALO_NET_ONLINE": "false", "HALO_NET_LIST_GAMES": "false", "HALO_NET_REPORT_GAMES": "false",
            "HALO_NET_DOORBELL": "false", "HALO_DISCORD_APPLICATION": "", "HALO_CRASH_REPORTS": "no",
            "HALO_NO_VSYNC": "1", "HALO_MAX_FPS": "-1", "HALO_VRR": "off", "HALO_RESOLUTION_SCALING": "native",
            "HALO_ML_LAB": str(self.lab),
        })
        env.update(self.extra_env)
        self.log = open(self.root / "game.log", "w")
        # (a network namespace of its own: no service is reachable, and the
        # console's port is the namespace's; this script talks to it from
        # inside through a helper)
        script = "ip link set lo up && exec \"$@\""
        self.game = subprocess.Popen(["unshare", "-rn", "sh", "-c", script, "sh", str(self.build)],
                                     cwd=str(self.data), env=env, stdout=self.log, stderr=subprocess.STDOUT)
        self.pid = self.game.pid
        # (the console is in the namespace: commands go through nsenter of
        # the game's own namespace, a helper started per command)
        deadline = time.time() + 240
        while time.time() < deadline:
            if self.game.poll() is not None:
                raise RuntimeError(f"the game stopped (exit {self.game.returncode})")
            if self.say("where", wait=4.0, quiet=True):
                return
            time.sleep(2)
        raise RuntimeError("the lab never answered")

    def console(self, commands):
        """hs commands through the telnet console, inside the game's network
        namespace (nsenter --net of the game's process, as its user namespace's
        root)"""
        child = self.namespace_pid()
        text = "\n".join(commands) + "\n"
        helper = ("import socket,sys,time\n"
                  "s=socket.create_connection(('127.0.0.1',2323),timeout=5)\n"
                  "s.recv(256)\n"
                  "for line in sys.stdin.read().splitlines():\n"
                  "    s.sendall((line+'\\n').encode()); time.sleep(0.4)\n"
                  "time.sleep(0.5)\n")
        subprocess.run(["nsenter", "-t", str(child), "-U", "-n", "--preserve-credentials", "python3", "-c", helper],
                       input=text.encode(), check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=60)

    def namespace_pid(self):
        # the unshare'd sh execs the game: the same pid
        return self.pid

    def lab_commands(self, commands, wait=10.0):
        out = Path(str(self.lab) + ".out")
        before = out.read_text().count("done") if out.exists() else 0
        # (written whole, then renamed into place: the game polls the file
        # each frame, and at a thousand frames a second it took a file half
        # written, its commands lost)
        part = Path(str(self.lab) + ".part")
        part.write_text("\n".join(commands) + "\n")
        os.replace(part, self.lab)
        deadline = time.time() + wait
        while time.time() < deadline:
            if out.exists() and out.read_text().count("done") > before:
                return True
            time.sleep(0.1)
        return False

    def say(self, command, wait=10.0, quiet=False):
        out = Path(str(self.lab) + ".out")
        if not self.lab_commands([command], wait):
            return None
        lines = [line for line in out.read_text().splitlines() if line.startswith(command.split()[0])]
        return lines[-1] if lines else None

    def shot(self, destination, attempts=3):
        for attempt in range(attempts):
            if self.shot_once(destination):
                return True
        return False

    def shot_once(self, destination):
        shots = self.data / "screenshots"
        before = set(shots.glob("*.png")) if shots.exists() else set()
        self.lab_commands(["shot"])
        deadline = time.time() + 8
        while time.time() < deadline:
            now = set(shots.glob("*.png")) if shots.exists() else set()
            new = now - before
            if new:
                path = sorted(new)[-1]
                # (written on a thread: wait for its size to hold)
                size = -1
                while path.stat().st_size != size:
                    size = path.stat().st_size
                    time.sleep(0.3)
                shutil.move(str(path), str(destination))
                return True
            time.sleep(0.2)
        return False

    def stop(self):
        if self.game and self.game.poll() is None:
            self.game.terminate()
            try:
                self.game.wait(20)
            except subprocess.TimeoutExpired:
                self.game.kill()
                self.game.wait()
        if getattr(self, "log", None):
            self.log.close()

    def keep_logs(self, out):
        for path in (self.save / "logs" / "debug.txt", self.root / "game.log", Path(str(self.lab) + ".out"),
                     Path(str(self.lab) + ".frames")):
            if path.exists():
                shutil.copy(path, out / f"log-{self.map_name}-{path.name}")

    def clean(self):
        shutil.rmtree(self.root, ignore_errors=True)


def camera_text(x, y, z, yaw, pitch, fov=70.0):
    yaw, pitch = math.radians(yaw), math.radians(pitch)
    forward = (math.cos(yaw) * math.cos(pitch), math.sin(yaw) * math.cos(pitch), math.sin(pitch))
    up = (-math.cos(yaw) * math.sin(pitch), -math.sin(yaw) * math.sin(pitch), math.cos(pitch))
    return "\n".join(["%f %f %f" % (x, y, z), "%f %f %f" % forward, "%f %f %f" % up,
                      "%f" % math.radians(fov)]) + "\n"


def read_views(path, maps, fast):
    views = []
    for line in Path(path).read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        if maps and fields[0] not in maps:
            continue
        if fields[1] == "@fast":
            fast[fields[0]] = float(fields[2])
            continue
        hud = fields[-1] == "hud"
        if hud:
            fields = fields[:-1]
        if fields[2] == "player":
            views.append({"map": fields[0], "name": fields[1], "player": True, "hud": hud})
        elif fields[2] == "rel":
            x, y, z, yaw, pitch = map(float, fields[3:8])
            views.append({"map": fields[0], "name": fields[1], "player": False, "hud": hud,
                          "pose": (x, y, z, yaw, pitch), "relative": True})
        else:
            x, y, z, yaw, pitch = map(float, fields[2:7])
            views.append({"map": fields[0], "name": fields[1], "player": False, "hud": hud,
                          "pose": (x, y, z, yaw, pitch)})
    return views


def white_frames(trace_text, jump=60.0, white=200.0):
    """frames of a lab trace whose view (before the HUD) is near white and
    far brighter than the frames around it, or that the composite brightened
    (it multiplies the view by the lighting: it can only darken)"""
    finals, flagged = [], []
    lines = [line for line in trace_text.splitlines() if line.startswith("frame=")]
    for line in lines:
        fields = dict(field.split("=", 1) for field in line.split() if "=" in field)
        finals.append(float(fields.get("final", "-1")))
    for index, line in enumerate(lines):
        # (the frames up to 5 s around at 60 fps: a white run of 2 s is
        # still far brighter than them)
        around = sorted(finals[max(0, index - 300):index] + finals[index + 1:index + 301])
        median = around[len(around) // 2] if around else finals[index]
        if finals[index] >= white and finals[index] - median >= jump:
            flagged.append(line + " WHITE")
        elif "BRIGHTER" in line:
            flagged.append(line)
    return len(lines), flagged


def self_test(args, extra_env):
    """the white-frame check: display.ray_tracing "ao" and display.glow on,
    the game's blend equation left as each of its draws can leave it (the
    lab's "equation": a decal's or particle's "component max", min, reverse
    subtract), at the player's view and a paused debug camera; fails on any
    frame the composite brightened or that goes white against its
    neighbors. Exit 1 on a failure."""
    work = args.out / "work"
    work.mkdir(parents=True, exist_ok=True)
    failures = []
    map_name = args.map[0] if args.map else "a10"
    run = Run(args.build.resolve(), args.maps.resolve(), map_name, work, args.size, extra_env)
    trace = Path(str(run.lab) + ".trace")
    try:
        run.start()
        if args.self_test_fast:
            run.console(["(game_speed 10)"])
            time.sleep(args.self_test_fast)
            run.console(["(game_speed 1)"])
        time.sleep(5)
        run.console(["(cinematic_show_letterbox 0)", "(camera_control 0)"])
        run.lab_commands(["rt ao", "hdr off", "glow on", "trace on"])
        time.sleep(3)
        for equation in ("none", "max", "min", "revsub"):
            # (the frames before the switch left out: the trace and the
            # counts started again once it is in force)
            run.lab_commands([f"equation {equation}"])
            time.sleep(1.0)
            if trace.exists():
                trace.unlink()
            run.lab_commands(["check"])
            time.sleep(args.self_test_seconds)
            said = run.say(f"say equation_{equation}") or ""
            fields = dict(f.split("=", 1) for f in said.split() if "=" in f)
            frames = int(fields.get("check_frames", "0"))
            failed = int(fields.get("check_failures", "-1"))
            count, white = white_frames(trace.read_text() if trace.exists() else "")
            # (the composite's every frame darkened, none white; and traced:
            # a frame the composite never ran in is no test)
            ok = frames > 0 and failed == 0 and not white
            # (and none the composite blacked out: a reverse subtract)
            black = []
            for line in (trace.read_text().splitlines() if trace.exists() else []):
                fields = dict(f.split("=", 1) for f in line.split() if "=" in f)
                before, after = float(fields.get("before", "-1")), float(fields.get("after", "-1"))
                if before > 8.0 and 0.0 <= after < 0.4 * before:
                    black.append(line + " BLACK")
            ok = ok and not black
            print(f"self-test {map_name} equation {equation}: {frames} frames checked, {failed} brightened by the "
                  f"composite, {len(white)} white, {len(black)} black: {'PASS' if ok else 'FAIL'}", flush=True)
            for line in (white + black)[:5]:
                print(f"    {line}", flush=True)
            if not ok:
                failures.append(equation)
        run.lab_commands(["equation none", "trace off"])
    except (RuntimeError, OSError, subprocess.SubprocessError) as reason:
        print(f"self-test {map_name}: FAILED to run: {reason}", flush=True)
        failures.append("run")
    finally:
        run.stop()
        run.keep_logs(args.out)
        if not args.keep:
            run.clean()
    shutil.rmtree(work, ignore_errors=True)
    print(f"self-test {map_name}: {'FAIL ' + ','.join(failures) if failures else 'PASS'}", flush=True)
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--maps", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--views", type=Path)
    parser.add_argument("--self-test", action="store_true",
                        help="the white-frame check (no views): see self_test; --map picks the map (a10)")
    parser.add_argument("--self-test-seconds", type=float, default=12.0)
    parser.add_argument("--self-test-fast", type=float, default=40.0, help="the opening cutscene at game_speed 10")
    parser.add_argument("--map", action="append", default=[])
    parser.add_argument("--modes", default="off,trackA,glow")
    parser.add_argument("--size", default="1920x1080")
    parser.add_argument("--settle", type=float, default=20.0)
    parser.add_argument("--time-frames", type=int, default=600)
    parser.add_argument("--no-time", action="store_true")
    parser.add_argument("--pause", action="store_true", help="game_speed 0 for the fixed views")
    parser.add_argument("--env", action="append", default=[])
    parser.add_argument("--keep", action="store_true", help="keep the run folders")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    work = args.out / "work"
    work.mkdir(exist_ok=True)
    extra_env = dict(item.split("=", 1) for item in args.env)
    if args.self_test:
        return self_test(args, extra_env)
    if not args.views:
        parser.error("--views is needed (or --self-test)")
    modes = args.modes.split(",")
    fast = {}
    views = read_views(args.views, set(args.map), fast)
    times = args.out / "frame_times.tsv"
    if not times.exists():
        times.write_text("map\tview\tmode\tframes\tavg_ms\tp50_ms\tp99_ms\tworst_ms\tgpu_frame_ms\tml_gpu_ms\trt_gpu_ms\tcpu_ml_ms\tcpu_trace_ms\tcpu_scene_ms\n")
    for map_name in dict.fromkeys(view["map"] for view in views):
        run = Run(args.build.resolve(), args.maps.resolve(), map_name, work, args.size, extra_env)
        try:
            print(f"{map_name}: starting", flush=True)
            run.start()
            if fast.get(map_name):
                # (a campaign's opening cutscene, run through at ten times)
                run.console(["(game_speed 10)"])
                time.sleep(fast[map_name])
                run.console(["(game_speed 1)"])
            time.sleep(args.settle)
            run.console(["(cinematic_show_letterbox 0)"])
            where = run.say('where')
            print(f"{map_name}: {where}", flush=True)
            origin = tuple(map(float, where.split()[1:4])) if where else (0.0, 0.0, 0.0)
            for view in (v for v in views if v["map"] == map_name):
                commands = ["(show_hud 1)" if view["hud"] else "(show_hud 0)", "(show_hud_help_text 0)",
                            "(set cheat_deathless_player true)", "(set cheat_infinite_ammo true)"]
                if not view["player"]:
                    pose = list(view["pose"])
                    if view.get("relative"):
                        pose[0] += origin[0]
                        pose[1] += origin[1]
                        pose[2] += origin[2]
                    (run.save / "camera.txt").write_text(camera_text(*pose))
                    (run.data / "camera.txt").write_text(camera_text(*pose))
                    commands += ["debug_camera_load"]
                    if args.pause:
                        commands += ["(game_speed 0)"]
                else:
                    commands += ["(game_speed 1)", "(camera_control 0)", "(cinematic_show_letterbox 0)"]
                run.console(commands)
                time.sleep(3.0)
                if not view["player"]:
                    run.console(["debug_camera_load"])
                    time.sleep(2.0)
                print(f"  {view['name']}: {run.say('where')}", flush=True)
                for mode in modes:
                    run.lab_commands(MODES[mode])
                    time.sleep(2.5)
                    png = args.out / f"{map_name}-{view['name']}-{mode}.png"
                    ok = run.shot(png)
                    print(f"  {view['name']} {mode}: {'saved' if ok else 'NO PICTURE'}", flush=True)
                    if mode.startswith("rt") or mode == "trackA":
                        print(f"    {run.say(f'say {mode}')}", flush=True)
                    if not args.no_time:
                        label = f"{map_name}/{view['name']}/{mode}"
                        out = Path(str(run.lab) + ".out")
                        before = out.read_text().count("time ") if out.exists() else 0
                        run.lab_commands([f"time {label} {args.time_frames}"])
                        deadline = time.time() + 60
                        while time.time() < deadline:
                            text = out.read_text()
                            if text.count("time ") > before:
                                break
                            time.sleep(0.25)
                        line = [l for l in out.read_text().splitlines() if l.startswith(f"time {label} ")]
                        if line:
                            fields = dict(f.split("=", 1) for f in line[-1].split()[2:])
                            with times.open("a") as tsv:
                                tsv.write("\t".join([map_name, view["name"], mode] + [fields.get(k, "") for k in (
                                    "frames", "avg_ms", "p50_ms", "p99_ms", "worst_ms", "gpu_frame_ms", "ml_gpu_ms",
                                    "rt_gpu_ms", "cpu_ml_ms", "cpu_trace_ms", "cpu_scene_ms")]) + "\n")
                            print(f"    {line[-1]}", flush=True)
                run.lab_commands(["rt config", "hdr config", "glow config", "geometry config"])
                if args.pause and not view["player"]:
                    run.console(["(game_speed 1)"])
            print(f"  {run.say('say end')}", flush=True)
        except (RuntimeError, OSError, subprocess.SubprocessError) as reason:
            print(f"{map_name}: FAILED: {reason}", flush=True)
        finally:
            run.stop()
            run.keep_logs(args.out)
            if not args.keep:
                run.clean()
    shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
