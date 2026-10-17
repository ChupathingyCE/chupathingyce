"""map_report.c (port/linux/src/map_report.h): a map's identity and shape as
JSON for the site's map catalog. Built alone with the build machine's C
compiler (with delta_wire.c, whose map name rule it shares), under the
sanitizers where it has them, and as C89; the harness's output is parsed
as JSON and checked here."""
import json
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [ROOT / "port" / "linux" / "tests" / "map_report_test.c", ROOT / "port" / "linux" / "src" / "map_report.c",
           ROOT / "port" / "linux" / "src" / "delta_wire.c"]
INCLUDE = ROOT / "port" / "linux" / "include"
SRC = ROOT / "port" / "linux" / "src"


def compiler():
    return next((shutil.which(name) for name in ("clang", "cc", "gcc") if shutil.which(name)), None)


def build(tmp_path, extra):
    binary = tmp_path / "map_report_test"
    result = subprocess.run([compiler(), "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *extra,
                             "-iquote", str(INCLUDE), "-iquote", str(SRC), *map(str, SOURCES), "-o", str(binary)],
                            capture_output=True, text=True)
    return binary, result


def test_json(tmp_path):
    if not compiler():
        pytest.skip("no C compiler")
    binary, result = build(tmp_path, ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"])
    if result.returncode != 0:
        binary, result = build(tmp_path, [])
    assert result.returncode == 0, result.stdout + result.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    assert run.returncode == 0, run.stdout + run.stderr
    lines = run.stdout.splitlines()
    assert lines[:3] == ["0", "1", "0"]
    whole = json.loads(lines[3])
    assert whole == {
        "family": "ce", "file": "extinction", "size": 97888771, "blake2b": bytes(range(32)).hex(),
        "cache_version": 609, "header_checksum": "c5372723", "build": "01.00.00.0609",
        "scenario": "levels\\extinction\\\"x\"\x01\xe9", "gametypes": ["slayer", "ctf", "race"], "spawns": 61,
        "bounds": {"x": [-10.5, 20.25], "y": [-3.0, 4.0], "z": [0.0, 0.0]}}
    assert lines[4] == "refused"
    over = json.loads(lines[5])
    assert over["size"] == 2 ** 32 + 97888771 and "bounds" not in over
    assert lines[6] == "refused"
    xbox = json.loads(lines[7])
    assert xbox["family"] == "xbox" and "blake2b" not in xbox and "size" not in xbox
    assert lines[8:] == ["refused", "refused"]


def test_c89(tmp_path):
    if not compiler():
        pytest.skip("no C compiler")
    result = subprocess.run([compiler(), "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-iquote", str(INCLUDE),
                             "-iquote", str(SRC), "-c", str(SOURCES[1]), "-o", str(tmp_path / "map_report.o")],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
