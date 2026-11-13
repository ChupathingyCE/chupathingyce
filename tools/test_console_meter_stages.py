"""Checks that a HUD meter's extra combiner stages stay out of the models.

The smoothed meter edges (#1068) and a Halo PC meter (#961) draw HUD meters
with stages past the Xbox's four, in the shared pixel_shader
(source/rasterizer/xbox/rasterizer_xbox_dynavobgeom.c). The model shader
reuses that struct and sets only the color half of stages 4 and 5
(rasterizer_xbox_models.c), so a meter's alpha there reached the next frame's
models and made their reflection mask four times the base map's alpha: every
model drawn as chrome on the original Xbox's widescreen HUD (test build
20261111-1).

Two checks: every console build leaves the smoothed edges out
(halo_port_features.h, HALO_CONSOLE_NO_SMOOTH_METER_EDGES, whichever console
switch it sets: the original Xbox doesn't set HALO_CONSOLE), and the meter
takes its extra stages back out of the shared struct after it draws."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
FEATURES = ROOT / "port" / "linux" / "include" / "halo_port_features.h"
DYNAVOBGEOM = ROOT / "source" / "rasterizer" / "xbox" / "rasterizer_xbox_dynavobgeom.c"
MODELS = ROOT / "source" / "rasterizer" / "xbox" / "rasterizer_xbox_models.c"
GATE = "HALO_CONSOLE_NO_SMOOTH_METER_EDGES"

CONSOLE_SWITCHES = [
    "HALO_CONSOLE",
    "HALO_XBOX_CONSOLE",
    "HALO_XBOX360_CONSOLE",
    "HALO_WIIU_CONSOLE",
    "HALO_SWITCH_CONSOLE",
    "HALO_CONSOLE_GL_LAYER",
]


def defined_macros(*switches):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    command = [compiler, "-E", "-dM", "-x", "c", "-"]
    command += ["-D" + switch for switch in switches]
    source = '#include "{}"\n'.format(FEATURES.as_posix())
    result = subprocess.run(command, input=source, capture_output=True, text=True, check=True)
    return {line.split()[1] for line in result.stdout.splitlines() if line.startswith("#define ")}


@pytest.mark.parametrize("switch", CONSOLE_SWITCHES)
def test_every_console_leaves_the_smoothed_edges_out(switch):
    assert GATE in defined_macros(switch)


def test_the_desktop_keeps_the_smoothed_edges():
    assert GATE not in defined_macros()
    assert GATE not in defined_macros("HALO_GAME_BROWSER")


def function_body(text, name):
    start = text.index("\n" + name + "(")
    end = text.index("\n}\n", start)
    return text[start:end]


def test_the_meter_takes_its_extra_stages_back():
    body = function_body(DYNAVOBGEOM.read_text(), "void _rasterizer_psuedo_dynamic_screen_quad_draw")
    draw_end = body.rindex("IDirect3DDevice8_End(")
    after = body[draw_end:]
    clear = re.search(r"if \(meter\)\s*\{\s*for \(combiner_index = 4; combiner_index < 6; combiner_index\+\+\)", after)
    assert clear, "the meter's stages 4 and 5 must be cleared after its draw"
    for field in ("alpha_inputs", "alpha_outputs", "rgb_inputs", "rgb_outputs"):
        assert "pixel_shader.{}[combiner_index] = 0;".format(field) in after[clear.start():]
    # (no meter stage past 5 that the clear would miss)
    for stage in re.findall(r"pixel_shader\.(?:alpha|rgb)_(?:inputs|outputs)\[(\d+)\]", body):
        assert int(stage) < 6


def test_the_models_still_set_only_the_first_alpha_stages():
    """What makes the clear needed: if the model shader sets its own alpha
    stages past 2 one day, this check can go."""
    stages = {int(stage) for stage in re.findall(r"pixel_shader\.alpha_(?:inputs|outputs)\[(\d+)\]", MODELS.read_text())}
    assert stages <= {0, 1, 2}
