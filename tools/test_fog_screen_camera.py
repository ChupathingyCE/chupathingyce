"""Fog screens keep each window's last camera matrix in previous_camera_matrix[]
(#1415). _rasterizer_environment_fog_screen_begin had a local pointer with the
array's own name, so its two copies into the array indexed the pointer
(&array[window]) instead: the window index was added twice. In split screen
the first fog screen's camera copy ran past the four windows onto
global_rasterizer_model_ambient_reflection_tint, which sits right after the
array, and the next model draw read through the camera's position (a30,
Windows and Linux 0.9.1a). The Xbox indexes the array itself."""
import re
import shlex
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


# the flags that name a file the compile reads, and a system root it needs
# (an include folder that isn't there the compiler passes over)
FILE_FLAGS = ("-include", "-imacros")
FOLDER_FLAGS = ("--sysroot", "-isysroot")


def flag_paths(command):
    """(flag, path) for each file or folder the command names by a flag:
    "-include path" or "--sysroot=path" """
    words = shlex.split(command)
    flags = FILE_FLAGS + FOLDER_FLAGS
    for index, word in enumerate(words):
        flag = next((flag for flag in flags if word.startswith(flag)), None)
        if not flag:
            continue
        if word == flag:
            if index + 1 < len(words):
                yield flag, words[index + 1]
        else:
            yield flag, word[len(flag):].removeprefix("=")


def ninja_inputs(output):
    """the inputs ninja lists for an output, explicit and implicit (not the
    order-only ones, which a compile doesn't read)"""
    ninja = shutil.which("ninja")
    if not ninja:
        return []
    result = subprocess.run([ninja, "-C", str(ROOT), "-t", "query", output], capture_output=True, text=True)
    if result.returncode != 0:
        return []
    inputs, section = [], None
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if stripped.startswith("input:"):
            section = "input"
        elif stripped.startswith("outputs:") or stripped.startswith("validations:"):
            section = None
        elif section == "input" and line.startswith("    ") and not stripped.startswith("||"):
            path = stripped.lstrip("|").strip()
            if path:
                inputs.append(path)
    return inputs


def missing_inputs(command, source):
    """what the fog file's compile needs that this checkout's build doesn't
    have (a build downloaded or made only in part): the generated source, the
    files it includes by flag (the prefix header), its system root, and the
    inputs ninja lists for its object"""
    missing = [] if (ROOT / source).is_file() else [source]
    for flag, path in flag_paths(command):
        found = (ROOT / path).is_file() if flag in FILE_FLAGS else (ROOT / path).is_dir()
        if not found:
            missing.append(path)
    output = re.search(r" -o (\S+)", command)
    if output:
        missing += [path for path in ninja_inputs(output.group(1)) if not (ROOT / path).exists()]
    return sorted(set(missing))


def test_no_shadowed_array_when_compiled():
    command = fog_compile_command()
    if not command:
        pytest.skip("no configured build (build.ninja with a desktop target)")
    source = re.search(r" -c (\S+)", command).group(1)
    missing = missing_inputs(command, source)
    if missing:
        pytest.skip(f"the build is incomplete (build the game first): missing {', '.join(missing[:5])}"
                    + (f" and {len(missing) - 5} more" if len(missing) > 5 else ""))
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
