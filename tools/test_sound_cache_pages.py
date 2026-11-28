"""Check that the sound cache's page count follows the cache's size.

Compiles physical_memory_map.h and xbox_sound_cache.c's page constants with
the Xbox's size and with smaller platform sizes: the pages must cover the
cache exactly, never more.
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def xbox_defines():
	"""the Xbox's sizes from halo_port_capacity.h, which the builds' prefix
	header gives every unit"""
	capacity = (ROOT / "port/linux/include/halo_port_capacity.h").read_text()
	names = ("HALO_PORT_XBOX_SOUND_CACHE_SIZE", "HALO_PORT_XBOX_SOUND_CACHE_SOUNDS",
		"HALO_PORT_CONSOLE_SOUND_CACHE_MAXIMUM_SIZE")
	values = [re.search(r"#define " + name + r" (\w+)", capacity).group(1) for name in names]
	return [f"-D{name}={value}" for name, value in zip(names, values)]


def page_enum():
	source = (ROOT / "source/cache/xbox_sound_cache.c").read_text()
	start = source.index("SOUND_CACHE_PAGE_SIZE_BITS =")
	return source[source.rindex("enum", 0, start):source.index("};", start) + 2]


def run(size, platform=None):
	program = f'''
#include <stdio.h>
#include "cache/physical_memory_map.h"
{page_enum()}
int main(void)
{{
	printf("%lu %lu %lu\\n", (unsigned long)SOUND_CACHE_PAGE_COUNT,
		(unsigned long)SOUND_CACHE_PAGE_COUNT << SOUND_CACHE_PAGE_SIZE_BITS, (unsigned long)SOUND_CACHE_SOUND_COUNT);
	return 0;
}}
'''
	with tempfile.TemporaryDirectory() as directory:
		c_path = os.path.join(directory, "pages.c")
		exe_path = os.path.join(directory, "pages")
		Path(c_path).write_text(program)
		defines = [] if size is None else [f"-DSOUND_CACHE_SIZE={size:#x}"]
		if platform:
			# (a platform's own, halo_port_capacity.h's: its size and sounds)
			defines = [f"-DHALO_PORT_SOUND_CACHE_SIZE={platform[0]:#x}", f"-DHALO_PORT_SOUND_CACHE_SOUNDS={platform[1]}"]
		subprocess.run(
			[os.environ.get("CC", "cc"), "-I", str(ROOT / "source"), *xbox_defines(), *defines, c_path, "-o", exe_path],
			check=True)
		pages, covered, sounds = subprocess.run([exe_path], check=True, capture_output=True, text=True).stdout.split()
		return int(pages), int(covered), int(sounds)


def main():
	failures = 0
	for size, expected_pages in ((None, 1024), (0x200000, 512), (0x100000, 256)):
		pages, covered, sounds = run(size)
		expected_size = 0x400000 if size is None else size
		ok = pages == expected_pages and covered == expected_size and sounds == 512
		print(f"{'ok  ' if ok else 'FAIL'} size {expected_size:#x}: {pages} pages, {covered:#x} bytes, {sounds} sounds")
		failures += not ok
	# the desktop's and Android's guest build's (halo_port_capacity.h)
	for size, count in ((0x2000000, 2048), (0x1000000, 1024)):
		pages, covered, sounds = run(None, (size, count))
		ok = pages == size >> 12 and covered == size and sounds == count
		print(f"{'ok  ' if ok else 'FAIL'} platform {size:#x}: {pages} pages, {covered:#x} bytes, {sounds} sounds")
		failures += not ok
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
