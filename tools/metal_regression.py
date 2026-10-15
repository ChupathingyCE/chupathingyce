#!/usr/bin/env python3
"""Capture fixed menu/idle Blood Gulch scenes with fresh OpenGL and Metal builds.

Requires local game data, macOS, and explicit HALO_ALLOW_LAUNCH=1. Windows
remain hidden/background-only. All configs, saves, logs and captures live in
an isolated output directory; installed data is only symlinked. Exit 77 means
missing prerequisites (a unittest skip), 1 means rendering/capture failure.

The fixture fixes the network seed, disables input/interpolation, and uses
1x anisotropy to avoid API-specific anisotropic sampling differences. No
resizing, registration, masking, or tolerance relaxation is done. Gameplay
captures include the high-resolution HUD. Original images, exact RGB diffs,
logs and JSON metrics are retained. The menu pauses at simulation tick 30.
Gameplay is timed idle, not a frame replay: small animation differences
remain subject to the tolerance.
"""
import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

if __package__:
    from .metal_image_compare import compare, read_bmp, write_bmp
else:
    from metal_image_compare import compare, read_bmp, write_bmp

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATA = Path.home() / "Library/Application Support/ChupathingyCE"
MODES = ("off", "fxaa", "smaa", "ssaa2x", "msaa2x", "msaa4x", "msaa8x")


@dataclass(frozen=True)
class Case:
    name: str
    menu: bool = False
    aa: str = "off"
    lighting: bool = False


def cases():
    return [Case("menu", menu=True), *[Case("gameplay-" + mode, aa=mode) for mode in MODES],
            Case("gameplay-per-pixel", lighting=True)]


def prerequisites(data: Path):
    if platform.system() != "Darwin":
        return "macOS is required"
    if os.environ.get("HALO_ALLOW_LAUNCH") != "1":
        return "HALO_ALLOW_LAUNCH=1 is required for hidden/background launches"
    for name in ("bloodgulch.map", "ui.map"):
        if not (data / "maps" / name).is_file():
            return f"missing game data: {data / 'maps' / name}"
    return None


def command(argv, *, timeout=900):
    subprocess.run(argv, cwd=ROOT, check=True, timeout=timeout)


def build_pair(output: Path):
    """Always restore the configured Metal target, including after a failed GL build."""
    destination = output / "builds"
    destination.mkdir()
    apps = {}
    try:
        for renderer, flag in (("Metal", "--macos-metal"), ("OpenGL", "--no-macos-metal")):
            command([sys.executable, "configure.py", flag, "--lto=off", "--pgo=off"])
            command(["ninja", "macos"])
            app = destination / (renderer + ".app")
            shutil.copytree(ROOT / "build/macos/ChupathingyCE.app", app, symlinks=True)
            apps[renderer] = app / "Contents/MacOS/halo"
    finally:
        command([sys.executable, "configure.py", "--macos-metal", "--lto=off", "--pgo=off"])
        command(["ninja", "macos"])
    return apps


def capture(case: Case, renderer: str, app: Path, data: Path, output: Path, seconds: int):
    folder = output / case.name / renderer
    shots = folder / "shots"
    shots.mkdir(parents=True)
    # Pass environment overrides after the shell runner's defaults. In
    # particular an empty network test must not become its default host mode.
    env = {**os.environ, "HALO_APP": str(app), "HALO_SOURCE_DATA": str(data)}
    overrides = {
        "HALO_RENDER_REGRESSION": "1", "HALO_DISPLAY_MODE": "windowed", "HALO_WINDOW_SIZE": "640x480",
        "HALO_RESOLUTION_SCALING": "native", "HALO_TEST_INPUT": "", "HALO_NETWORK_TEST": "" if case.menu else "host:bloodgulch",
        "HALO_NETWORK_TEST_START": "2", "HALO_ANTI_ALIASING": case.aa,
        "HALO_PER_PIXEL_LIGHTING": str(case.lighting).lower(), "HALO_FIELD_OF_VIEW": "70",
        "HALO_ANISOTROPIC_FILTERING": "1", "HALO_INTERPOLATION": "false", "HALO_DIRECT_CAMERA": "false",
        "HALO_HIGH_RES_HUD": "true", "HALO_MATCH_TIMER": "false", "HALO_VERSION_WATERMARK": "false",
        "HALO_SCREENSHOT_DIR": str(shots), "HALO_SCREENSHOT_EVERY": "60" if case.menu else "120", "MTL_DEBUG_LAYER": "1",
    }
    argv = [str(ROOT / "tools/metal_scripted_run.sh"), str(folder), str(seconds)]
    argv.extend(f"{name}={value}" for name, value in overrides.items())
    subprocess.run(argv, cwd=ROOT, env=env, check=True, timeout=seconds + 110)
    log = (folder / "run.log").read_text(errors="replace")
    if re.search(r"failed assertion|validation error|command buffer.*failed|initialization failed|cannot compile|cannot create pipeline|pipeline failed|lighting failed", log, re.I):
        raise ValueError(f"{case.name}/{renderer}: rendering failure (see {folder / 'run.log'})")
    camera = None
    if case.menu:
        if "renderer regression menu paused at tick 30" not in log:
            raise ValueError(f"{renderer}: menu did not freeze at the fixture tick; rebuild this branch")
    else:
        if "renderer regression seed 12345678" not in log:
            raise ValueError(f"{renderer}: missing regression seed hook; rebuild this branch")
        ticks = [int(value) for value in re.findall(r"network test: tick (\d+) player", log)]
        if not ticks or max(ticks) < 150:
            raise ValueError(f"{case.name}/{renderer}: did not reach stable gameplay")
        positions = re.findall(r"local 0 camera \(([^)]+)\)", log)
        if not positions:
            raise ValueError(f"{renderer}: missing idle camera position")
        camera = positions[-1]
    images = sorted(shots.glob("frame[0-9]*.bmp"))
    if len(images) < 2:
        raise ValueError(f"{case.name}/{renderer}: no settled frame captured")
    # Keep the selected frame plus two neighbors for inspection, not hundreds
    # of redundant high-resolution BMPs from the uncapped hidden GL window.
    for image in images[:-3]:
        image.unlink()
    return images[-1], camera


def run_cases(selected, apps, data, output, seconds, *, mean_limit=3.0, fraction_limit=0.03,
              reference_name="OpenGL", candidate_name="Metal"):
    reports = []
    for case in selected:
        gl, gl_camera = capture(case, reference_name, apps[reference_name], data, output, seconds)
        metal, metal_camera = capture(case, candidate_name, apps[candidate_name], data, output, seconds)
        if gl_camera != metal_camera:
            raise ValueError(f"{case.name}: mismatched camera positions: {gl_camera} vs {metal_camera}")
        report, difference = compare(read_bmp(gl), read_bmp(metal), mean_limit=mean_limit,
                                     fraction_limit=fraction_limit)
        report.update(case=case.name, reference=str(gl), actual=str(metal), camera=gl_camera,
                      reference_renderer=reference_name, candidate_renderer=candidate_name)
        write_bmp(output / case.name / "difference.bmp", difference)
        (output / case.name / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
        reports.append(report)
        (output / "results.json").write_text(json.dumps(reports, indent=2) + "\n")
        print(f"{case.name}: {'PASS' if report['passed'] else 'FAIL'} "
              f"mean={report['mean_rgb_error_255']:.3f}/255 bad={report['bad_pixel_fraction']:.2%}", flush=True)
    return all(report["passed"] for report in reports)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, default=DEFAULT_DATA)
    parser.add_argument("--output", type=Path, help="new/empty directory; default a retained temporary directory")
    parser.add_argument("--gl-app", type=Path, help="prebuilt app executable; supply with --metal-app to skip builds")
    parser.add_argument("--metal-app", type=Path)
    parser.add_argument("--reference-name", choices=("OpenGL", "MetalV1"), default="OpenGL",
                        help="reference label; MetalV1 requires prebuilt executables")
    parser.add_argument("--candidate-name", choices=("Metal", "MetalV2"), default="Metal",
                        help="candidate label; MetalV2 requires prebuilt executables")
    parser.add_argument("--seconds", type=int, default=14)
    parser.add_argument("--case", action="append", choices=[case.name for case in cases()])
    parser.add_argument("--mean-limit", type=float, default=3.0)
    parser.add_argument("--fraction-limit", type=float, default=0.03)
    args = parser.parse_args(argv)
    reason = prerequisites(args.data_root)
    if reason:
        print(f"SKIP: {reason}")
        return 77
    if bool(args.gl_app) != bool(args.metal_app):
        parser.error("provide both --gl-app and --metal-app, or neither to build both")
    if (args.reference_name != "OpenGL" or args.candidate_name != "Metal") and not args.gl_app:
        parser.error("custom renderer names require --gl-app and --metal-app prebuilt executables")
    if not 10 <= args.seconds <= 120:
        parser.error("--seconds must be 10..120")
    if not 0 <= args.mean_limit <= 255 or not 0 <= args.fraction_limit <= 1:
        parser.error("invalid comparison tolerance")
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix="metal-regression-"))
    if output.exists() and any(output.iterdir()):
        parser.error("--output must be empty (never overwrite previous captures)")
    output.mkdir(parents=True, exist_ok=True)
    print(f"Artifacts: {output}", flush=True)
    selected = [case for case in cases() if not args.case or case.name in args.case]
    try:
        apps = ({args.reference_name: args.gl_app.resolve(), args.candidate_name: args.metal_app.resolve()}
                if args.gl_app else build_pair(output))
        for app in apps.values():
            if not app.is_file() or not os.access(app, os.X_OK):
                raise ValueError(f"missing executable: {app}")
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        (output / "fixture.json").write_text(json.dumps({"revision": revision, "apps": {k: str(v) for k, v in apps.items()},
            "seconds": args.seconds, "aniso": 1, "interpolation": False, "seed": "12345678", "menu_pause_tick": 30,
            "cases": [case.name for case in selected]}, indent=2) + "\n")
        return 0 if run_cases(selected, apps, args.data_root.resolve(), output, args.seconds,
                             mean_limit=args.mean_limit, fraction_limit=args.fraction_limit,
                             reference_name=args.reference_name, candidate_name=args.candidate_name) else 1
    except (subprocess.SubprocessError, ValueError, OSError) as error:
        print(f"FAIL: {error}; artifacts: {output}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
