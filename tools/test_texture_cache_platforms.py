"""Check which builds take the larger texture cache and Xbox memory window
(halo_port_capacity.h's HALO_PORT_TEXTURE_CACHE_SIZE, platform.h's
PLATFORM_CONTIGUOUS_SIZE): only the 64-bit desktop clients and the native
64-bit Android build (448 MB in a 1 GB window). The 32-bit desktop builds,
the dedicated server, iOS and tvOS keep 256 MB in 512 MB; Android's guest
build, the consoles and their GL layer keep their own. Preprocesses the
headers with each build's defines; nothing is compiled or run. Also checks
that win32_memory_watch.c's copy of the window's size has the same test."""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

LARGE = ("0x1C000000", "0x40000000")
DESKTOP = ("0x10000000", "0x20000000")
VARIES = ("(platform_contiguous_size > 0x08000000UL ? platform_contiguous_size / 2 : 0x2C00000UL)",
	"(platform_contiguous_size)")

# (each build's defines, and the texture cache and window it should have;
# None: not checked, the build has its own)
BUILDS = [
	("desktop 64-bit", ["HALO_64BIT", "HALO_GAME_BROWSER", "HALO_CUSTOM_EDITION", "HALO_MAP_LIST"], LARGE),
	("desktop 32-bit", ["HALO_GAME_BROWSER", "HALO_CUSTOM_EDITION", "HALO_MAP_LIST"], DESKTOP),
	("Android 64-bit", ["HALO_ANDROID", "HALO_64BIT"], LARGE),
	("iOS", ["HALO_ANDROID", "HALO_IOS", "HALO_64BIT"], DESKTOP),
	("tvOS", ["HALO_ANDROID", "HALO_IOS", "HALO_TVOS", "HALO_64BIT"], DESKTOP),
	("dedicated server 32-bit", ["HALO_SERVER"], DESKTOP),
	("dedicated server 64-bit", ["HALO_SERVER", "HALO_64BIT"], DESKTOP),
	("Linux moved-window test", ["HALO_WINDOW_REBASE"], ("0x10000000", "(platform_contiguous_size)")),
	("Android 32-bit guest", ["HALO_ANDROID"], ("0x2C00000", "0x08000000")),
	("Xbox", ["HALO_XBOX_CONSOLE", "HALO_CONSOLE_DELTA", "HALO_MAP_LIST", "HALO_CUSTOM_EDITION"], ("0x1600000", None)),
	("Xbox 360", ["HALO_CONSOLE", "HALO_XBOX360_CONSOLE", "HALO_BIG_ENDIAN", "HALO_64BIT", "HALO_WINDOW_TRANSLATED"],
		("0x1600000", "0x08000000")),
	("Wii U", ["HALO_CONSOLE", "HALO_WIIU_CONSOLE", "HALO_BIG_ENDIAN", "HALO_WINDOW_REBASE"], VARIES),
	("Switch guest", ["HALO_ANDROID", "HALO_WINDOW_REBASE", "HALO_SWITCH_CONSOLE"], VARIES),
	("PS3", ["HALO_PS3", "HALO_PS3_CONSOLE", "HALO_BIG_ENDIAN", "HALO_CONSOLE", "HALO_64BIT"], DESKTOP),
	("a 64-bit HALO_CONSOLE port", ["HALO_CONSOLE", "HALO_64BIT"], DESKTOP),
	("a 64-bit console's GL layer", ["HALO_CONSOLE_GL_LAYER", "HALO_64BIT"], DESKTOP),
]


def window_block():
	"""platform.h's window sizes, without the host headers it includes"""
	text = (ROOT / "port/linux/src/platform.h").read_text()
	start = text.index("#ifdef HALO_WINDOW_REBASE\n#define PLATFORM_CONTIGUOUS_BASE")
	end = text.index("#define PLATFORM_ANY_PHYSICAL_ADDRESS")
	return text[start:end]


def sizes(defines):
	program = ('#include "halo_console_features.h"\n#include "halo_port_capacity.h"\n' + window_block() +
		"\nsizes HALO_PORT_TEXTURE_CACHE_SIZE @ PLATFORM_CONTIGUOUS_SIZE\n")
	with tempfile.TemporaryDirectory() as directory:
		path = os.path.join(directory, "sizes.c")
		Path(path).write_text(program)
		result = subprocess.run(
			[os.environ.get("CC", "cc"), "-E", "-P", "-I", str(ROOT / "port/linux/include"), "-I", str(ROOT / "source"),
			 *[f"-D{define}" for define in defines], path],
			check=True, capture_output=True, text=True).stdout
	line = [line for line in result.splitlines() if line.startswith("sizes ")][-1]
	texture, window = line[len("sizes "):].split(" @ ")
	return texture.strip(), window.strip().rstrip("UL")


def windows_watch_matches():
	"""win32_memory_watch.c's window: 1 GB in the 64-bit client, under the
	same test as platform.h's for Windows (never a console, iOS or tvOS)"""
	text = (ROOT / "port/windows/src/win32_memory_watch.c").read_text()
	return re.search(r"#if defined\(HALO_64BIT\) && !defined\(HALO_SERVER\)\n"
		r"#define PLATFORM_CONTIGUOUS_SIZE 0x40000000UL\n#else\n"
		r"#define PLATFORM_CONTIGUOUS_SIZE 0x20000000UL\n#endif", text) is not None


def main():
	failures = 0
	for name, defines, (texture, window) in BUILDS:
		got_texture, got_window = sizes(defines)
		ok = got_texture == texture and (window is None or got_window == window)
		print(f"{'ok  ' if ok else 'FAIL'} {name}: texture cache {got_texture}, window {got_window}")
		failures += not ok
	ok = windows_watch_matches()
	print(f"{'ok  ' if ok else 'FAIL'} win32_memory_watch.c's window has platform.h's test for Windows")
	failures += not ok
	return 1 if failures else 0


def test_texture_cache_platforms():
	assert main() == 0


if __name__ == "__main__":
	sys.exit(main())
