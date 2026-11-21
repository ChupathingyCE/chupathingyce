"""display.hdr's hooks in the game's units (#1629).

halo_linux_source_fixups.h declares halo_screen_tone_map and
halo_hdr_draw_kind for the builds with port/linux/src/d3d8_gl.c and turns
them into no-ops elsewhere. Its test once asked for HALO_LINUX_PLATFORM_LAYER,
which only the platform units have, while the prefix headers include it in
the game's units alone: the hooks were no-ops everywhere, and the tone
mapping and glow marking never ran. The header is preprocessed here as a game
unit of each build sees it."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "port" / "linux" / "include"

PROBE = """
#include "halo_linux_source_fixups.h"
#ifdef HALO_PORT_HDR
hooks_are_calls
#else
hooks_are_no_ops
#endif
"""


def compiler():
    return shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")


def hooks(tmp_path, *defines):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    probe = tmp_path / "probe.c"
    probe.write_text(PROBE)
    result = subprocess.run([cc, "-E", "-P", f"-I{INCLUDE}", *(f"-D{name}" for name in defines), str(probe)],
                            capture_output=True, text=True, check=True)
    return "calls" if "hooks_are_calls" in result.stdout else "no-ops"


@pytest.mark.parametrize("defines", [(), ("HALO_64BIT",), ("HALO_ANDROID",), ("HALO_IOS",)],
                         ids=["desktop-32", "desktop-64", "android", "ios"])
def test_game_units_with_the_opengl_renderer_call_the_hooks(tmp_path, defines):
    assert hooks(tmp_path, *defines) == "calls"


@pytest.mark.parametrize("define", ["HALO_SERVER", "HALO_CONSOLE", "HALO_XBOX_CONSOLE", "HALO_SWITCH_CONSOLE",
                                    "HALO_WIIU_CONSOLE", "HALO_XBOX360_CONSOLE", "HALO_CONSOLE_GL_LAYER"])
def test_builds_without_it_have_no_ops(tmp_path, define):
    assert hooks(tmp_path, define) == "no-ops"


def test_the_guard_does_not_ask_for_the_platform_layer():
    text = (INCLUDE / "halo_linux_source_fixups.h").read_text()
    guard = text[:text.index("#define HALO_PORT_HDR")]
    guard = guard[guard.rindex("#if"):]
    assert "HALO_LINUX_PLATFORM_LAYER" not in guard.split("*/")[-1]
