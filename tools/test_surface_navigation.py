"""Compile/run the independent BSP polygon navigation core and its search
scheduler (no game/map required)."""
import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


WARNINGS = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion",
            "-Wsign-conversion", "-Wshadow", "-Wstrict-prototypes",
            "-Wmissing-prototypes", "-Wformat=2", "-Wundef", "-Wwrite-strings", "-Werror"]
PROGRAMS = {
    "test_surface_navigation": ["source/engine_ai/surface_navigation.c",
                                "tools/test_surface_navigation.c"],
    "test_surface_navigation_jobs": ["source/engine_ai/surface_navigation.c",
                                     "source/engine_ai/surface_navigation_jobs.c",
                                     "tools/test_surface_navigation_jobs.c"],
}


def run(cc, sanitize=False):
    with tempfile.TemporaryDirectory(prefix="surface-navigation-test-") as directory:
        for name, sources in PROGRAMS.items():
            executable = Path(directory) / name
            command = [cc, *WARNINGS, *(str(ROOT / source) for source in sources),
                       "-lm", "-o", str(executable)]
            if sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, check=True, timeout=60)
            subprocess.run([str(executable)], check=True, timeout=60)


def test_surface_navigation():
    run(os.environ.get("CC", "clang"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    if not shutil.which(args.cc):
        parser.error(f"C compiler not found: {args.cc}")
    run(args.cc, args.sanitize)
