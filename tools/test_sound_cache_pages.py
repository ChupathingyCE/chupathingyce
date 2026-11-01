"""Check that the sound cache's page count follows the cache's size.

Compiles physical_memory_map.h and xbox_sound_cache.c's page constants with
the Xbox's size and with smaller platform sizes: the pages must cover the
cache exactly, never more.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def page_enum():
	source = (ROOT / "source/cache/xbox_sound_cache.c").read_text()
	start = source.index("SOUND_CACHE_PAGE_SIZE_BITS =")
	return source[source.rindex("enum", 0, start):source.index("};", start) + 2]


def run(size):
	program = f'''
#include <stdio.h>
#include "cache/physical_memory_map.h"
{page_enum()}
int main(void)
{{
	printf("%lu %lu\\n", (unsigned long)SOUND_CACHE_PAGE_COUNT,
		(unsigned long)SOUND_CACHE_PAGE_COUNT << SOUND_CACHE_PAGE_SIZE_BITS);
	return 0;
}}
'''
	with tempfile.TemporaryDirectory() as directory:
		c_path = os.path.join(directory, "pages.c")
		exe_path = os.path.join(directory, "pages")
		Path(c_path).write_text(program)
		defines = [] if size is None else [f"-DSOUND_CACHE_SIZE={size:#x}"]
		subprocess.run(
			[os.environ.get("CC", "cc"), "-I", str(ROOT / "source"), *defines, c_path, "-o", exe_path],
			check=True)
		pages, covered = subprocess.run([exe_path], check=True, capture_output=True, text=True).stdout.split()
		return int(pages), int(covered)


def main():
	failures = 0
	for size, expected_pages in ((None, 1024), (0x200000, 512), (0x100000, 256)):
		pages, covered = run(size)
		expected_size = 0x400000 if size is None else size
		ok = pages == expected_pages and covered == expected_size
		print(f"{'ok  ' if ok else 'FAIL'} size {expected_size:#x}: {pages} pages, {covered:#x} bytes")
		failures += not ok
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
