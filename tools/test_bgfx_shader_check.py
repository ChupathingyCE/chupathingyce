"""Source checks for the bgfx shader check (#1833,
port/linux/src/d3d8_bgfx_check.inl, tools/bgfx_shader_check.py).

The check compiles every shader bgfx can be asked for. A shader added to the
renderer without being added to the check would only be found broken on a
machine that draws with it again, so each source bgfx's renderer gives
bg_shader_get must be one the check compiles, and CI must run it on each
64-bit desktop build.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"


def test_every_source_is_checked():
	renderer = (SOURCE / "d3d8_bgfx.inl").read_text(encoding="utf-8")
	check = (SOURCE / "d3d8_bgfx_check.inl").read_text(encoding="utf-8")
	sources = set(re.findall(r"bg_shader_get\((bg_\w+_source)\b", renderer))
	assert sources, "no bg_shader_get of a source found"
	missing = sorted(source for source in sources if not re.search(rf"bg_check_shader\({source}\b", check))
	assert not missing, f"bgfx shaders the check doesn't compile: {missing}"
	# (the translators' and the anti-aliasing passes')
	assert "nv2a_vertex_shader_to_glsl(" in check and "nv2a_pixel_shader_to_glsl(" in check
	assert "bg_anti_alias_text(pass)" in check and "bg_anti_alias_text(pass)" in renderer


def test_check_runs_before_any_window():
	device = (SOURCE / "d3d8_gl.c").read_text(encoding="utf-8")
	check = device.index("bg_shader_check_if_asked();")
	assert check < device.index("platform_video_initialize(width, height))", check)


def test_ci_runs_it_on_each_desktop_build():
	workflow = WORKFLOW.read_text(encoding="utf-8")
	for binary in ("dist/chupathingyce-linux64-release/chupathingy ",
	               "dist/chupathingyce-windows64-release/chupathingy.exe",
	               "dist/chupathingyce-macos-release/ChupathingyCE.app/Contents/MacOS/chupathingy"):
		assert f"tools/bgfx_shader_check.py --binary {binary}".rstrip() in workflow, binary
