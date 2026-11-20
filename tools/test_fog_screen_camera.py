"""Fog screens keep each window's last camera matrix in previous_camera_matrix[]
(#1415). _rasterizer_environment_fog_screen_begin had a local pointer with the
array's own name, so its two copies into the array indexed the pointer
(&array[window]) instead: the window index was added twice. In split screen
the first fog screen's camera copy ran past the four windows onto
global_rasterizer_model_ambient_reflection_tint, which sits right after the
array, and the next model draw read through the camera's position (a30,
Windows and Linux 0.9.1a). The Xbox indexes the array itself."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "source" / "rasterizer" / "xbox" / "rasterizer_xbox_environment_fog.c"
FUNCTION = "void _rasterizer_environment_fog_screen_begin("


def begin_body():
    text = SOURCE.read_text()
    start = text.index(FUNCTION)
    return text[start:text.index("\n}\n", start)]


def test_no_local_takes_the_array_name():
    body = begin_body()
    # (a local named previous_camera_matrix hides the array from the copies)
    assert not re.search(r"\*\s*previous_camera_matrix\s*=", body)
    assert "real_matrix4x3 *window_previous_camera_matrix =" in body
    assert "matrix4x3_inverse(window_previous_camera_matrix, &matrix);" in body


def test_copies_index_the_array():
    body = re.sub(r"\s+", " ", begin_body())
    # the first fog screen: every window's slot from the camera
    assert ("for (window_index = 0; window_index < MAXIMUM_WINDOWS; window_index++)" in body)
    assert "csmemcpy( &previous_camera_matrix[ window_index], &global_window_parameters.camera," in body
    # each frame: this window's slot from its world-to-view matrix
    assert ("csmemcpy( &previous_camera_matrix[ global_window_parameters.window_index], "
            "&global_window_parameters.frustum.world_to_view," in body)


def fog_compile_command():
    """The configured build's command for the fog file, if there is one."""
    ninja = shutil.which("ninja")
    if not ninja or not (ROOT / "build.ninja").is_file():
        return None
    for target in ("build/macos/chupathingy", "build/linux64/chupathingy", "build/linux/chupathingy"):
        result = subprocess.run([ninja, "-C", str(ROOT), "-t", "commands", target],
                                capture_output=True, text=True)
        if result.returncode != 0:
            continue
        for line in result.stdout.splitlines():
            if "rasterizer_xbox_environment_fog.c" in line and " -c " in line and " -o " in line:
                return line
    return None


def test_no_shadowed_array_when_compiled():
    command = fog_compile_command()
    if not command:
        pytest.skip("no configured build (build.ninja with a desktop target)")
    source = re.search(r" -c (\S+)", command).group(1)
    if not (ROOT / source).is_file():
        pytest.skip(f"{source} not generated yet (build the game first)")
    command = re.sub(r" -MMD -MF \S+", "", command)
    command = re.sub(r" -o \S+", "", command)
    command = command.replace("-Wno-everything", "")
    command = command.replace(f" -c {source}", f" -fsyntax-only -Wshadow {source}")
    result = subprocess.run(command, shell=True, cwd=ROOT, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr[-4000:]
    lines = SOURCE.read_text().splitlines()
    for match in re.finditer(r"rasterizer_xbox_environment_fog\.c:(\d+):\d+: warning: declaration shadows a "
                             r"variable in the global scope", result.stderr):
        line = lines[int(match.group(1)) - 1]
        assert "previous_camera_matrix" not in line, line
