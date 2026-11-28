"""The Mac's one question about Metal (game issue #2028,
port/linux/src/port_config.c and sdl_platform.c): 0.9.1's setup screen
offered Metal with no testing mark, so players who chose it are still on
display.renderer = "bgfx". At start-up, before the window opens, a Mac
player on bgfx without display.metal_asked is asked once, "Metal is still in
testing. Switch back to OpenGL?": Yes writes "gl" and the flag, No only the
flag. Windows, Linux, Android, the consoles and the server never ask.

port_config.c is built as tools/test_port_config.py builds it, with
HALO_METAL_ASK defined (as port_config.h defines it for the Mac's bgfx
build), and run on config.toml files in a scratch folder."""
import re
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
sys.path.insert(0, str(Path(__file__).resolve().parent))

import test_port_config  # noqa: E402

test_port_config.DEFINES.setdefault("metal", [*test_port_config.DEFINES["desktop"], "-DHALO_METAL_ASK"])


def compiler():
	for name in ("cc", "clang", "gcc"):
		if shutil.which(name):
			return name
	pytest.skip("needs a C compiler")


@pytest.fixture(scope="module")
def program(tmp_path_factory):
	return test_port_config.build(compiler(), "metal", tmp_path_factory.mktemp("metal_ask"))


def case(program, tmp_path, renderer=None, asked=None):
	made = test_port_config.Case(program, tmp_path / "case")
	made.get("display.vsync")
	if renderer:
		made.run("write", "display.renderer", renderer)
	if asked is not None:
		made.run("write", "display.metal_asked", "true" if asked else "false")
	return made


def asks(made, *answer):
	_, output = made.run("metal_ask", *answer)
	return int(re.search(r"^metal_ask=(\d)$", output, re.M).group(1))


def values(made):
	read, _ = made.get("display.renderer", "display.metal_asked")
	return read["display.renderer"], read["display.metal_asked"]


def test_asked_on_bgfx_without_the_flag(program, tmp_path):
	assert asks(case(program, tmp_path, "bgfx")) == 1


def test_not_asked_with_the_flag(program, tmp_path):
	assert asks(case(program, tmp_path, "bgfx", asked=True)) == 0


def test_not_asked_on_opengl(program, tmp_path):
	assert asks(case(program, tmp_path / "default")) == 0
	assert asks(case(program, tmp_path / "gl", "gl")) == 0


def test_yes_writes_opengl_and_the_flag(program, tmp_path):
	made = case(program, tmp_path, "bgfx")
	assert asks(made, "yes") == 1
	assert values(made) == ("gl", "true")
	assert asks(made) == 0


def test_no_writes_only_the_flag(program, tmp_path):
	made = case(program, tmp_path, "bgfx")
	assert asks(made, "no") == 1
	assert values(made) == ("bgfx", "true")
	# (never asked again)
	assert asks(made) == 0


def test_no_other_build_has_the_flag(tmp_path):
	for kind in ("desktop32", "android", "server", "console"):
		built = test_port_config.build(compiler(), kind, tmp_path / kind)
		_, output = test_port_config.Case(built, tmp_path / f"case_{kind}").get("display.metal_asked")
		assert "display.metal_asked: no such setting" in output, kind


def function_body(source, signature):
	start = source.index(signature)
	body = source.index("\n{", start)
	return source[body:source.index("\n}\n", body)]


def test_only_the_mac_desktop_asks():
	header = (SOURCE / "port_config.h").read_text()
	guard = re.search(r"#if (defined\(HALO_BGFX\).*?)\n#define HALO_METAL_ASK", header, re.S).group(1)
	for needed in ("defined(HALO_BGFX)", "defined(__APPLE__)", "!defined(HALO_IOS)", "!defined(HALO_ANDROID)"):
		assert needed in guard
	platform = (SOURCE / "sdl_platform.c").read_text()
	checks = function_body(platform, "static void data_check_maps(void)")
	# (before the window opens, so a yes draws with OpenGL from this start)
	assert "#ifdef HALO_METAL_ASK\n\tmetal_testing_offer(quiet);\n#endif" in checks
	assert platform.count("metal_testing_offer(") == 2


def test_the_question():
	platform = (SOURCE / "sdl_platform.c").read_text()
	offer = function_body(platform, "static void metal_testing_offer(BOOL quiet)")
	assert '"Metal is still in testing. Switch back to OpenGL?"' in platform
	assert '"Yes"' in platform and '"No"' in platform
	# (a run that cannot ask changes nothing)
	quiet = offer[offer.index("if (quiet)"):]
	quiet = quiet[:quiet.index("return;")]
	assert "config_metal_ask_answer" not in quiet and "config_write" not in quiet
	# (nor does a box that could not be shown)
	failed = offer[offer.index("if (!SDL_ShowMessageBox"):]
	failed = failed[:failed.index("return;")]
	assert "config_metal_ask_answer" not in failed
	assert "config_metal_ask_answer(answer == 1);" in offer
