"""Check which builds take a larger sound cache (halo_port_capacity.h's
HALO_PORT_SOUND_CACHE_SIZE and _SOUNDS): only the desktop builds (32 MB,
2048 sounds) and Android's 32-bit guest build (16 MB, 1024). Every console
and the dedicated server keep the Xbox's 4 MB and 512 sounds (a console's
memory class, physical_memory_map.h, sizes its own). Preprocesses the
headers with each build's defines; nothing is compiled or run."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# (each build's defines, and the size and sounds it should have)
BUILDS = [
	("desktop 64-bit", ["HALO_64BIT"], (0x2000000, 2048)),
	("desktop 32-bit", [], (0x2000000, 2048)),
	("Android 64-bit", ["HALO_ANDROID", "HALO_64BIT"], (0x2000000, 2048)),
	("Android 32-bit guest", ["HALO_ANDROID"], (0x1000000, 1024)),
	("dedicated server", ["HALO_SERVER"], (0x400000, 512)),
	("Xbox", ["HALO_XBOX_CONSOLE"], (0x400000, 512)),
	("Xbox 360", ["HALO_XBOX360_CONSOLE"], (0x400000, 512)),
	("Wii U", ["HALO_WIIU_CONSOLE"], (0x400000, 512)),
	("Wii U window", ["HALO_WIIU_CONSOLE", "HALO_CONSOLE_WINDOW_MAXIMUM"], (0x400000, 512)),
	("Switch guest", ["HALO_ANDROID", "HALO_WINDOW_REBASE", "HALO_SWITCH_CONSOLE"], (0x400000, 512)),
	("a HALO_CONSOLE port", ["HALO_CONSOLE"], (0x400000, 512)),
	("a console's GL layer", ["HALO_CONSOLE_GL_LAYER"], (0x400000, 512)),
]


def sizes(defines):
	program = ('#include "halo_port_capacity.h"\n#include "cache/physical_memory_map.h"\n'
		"#ifdef HALO_CONSOLE_MEMORY_CLASS\nsound_cache memory_class SOUND_CACHE_MAXIMUM_SOUNDS\n"
		"#else\nsound_cache SOUND_CACHE_SIZE SOUND_CACHE_MAXIMUM_SOUNDS\n#endif\n")
	with tempfile.TemporaryDirectory() as directory:
		path = os.path.join(directory, "sizes.c")
		Path(path).write_text(program)
		result = subprocess.run(
			[os.environ.get("CC", "cc"), "-E", "-P", "-I", str(ROOT / "port/linux/include"), "-I", str(ROOT / "source"),
			 *[f"-D{define}" for define in defines], path],
			check=True, capture_output=True, text=True).stdout
	line = next(line for line in result.splitlines() if line.startswith("sound_cache "))
	_, size, sounds = line.split()
	return (None if size == "memory_class" else eval(size, {}, {})), eval(sounds, {}, {})


def main():
	failures = 0
	for name, defines, (size, sounds) in BUILDS:
		got_size, got_sounds = sizes(defines)
		# (a console's memory class sizes its own at run time: never the port's)
		ok = (got_size == size or (got_size is None and size == 0x400000)) and got_sounds == sounds
		shown = "memory class" if got_size is None else f"{got_size:#x}"
		print(f"{'ok  ' if ok else 'FAIL'} {name}: {shown}, {got_sounds} sounds")
		failures += not ok
	return 1 if failures else 0


def test_sound_cache_platforms():
	assert main() == 0


if __name__ == "__main__":
	sys.exit(main())
