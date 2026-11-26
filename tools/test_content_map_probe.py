"""content_map_header.h's content_map_probe (port/linux/game): whether bytes
start as a cache file's header, and what it says (version, Chimera's
compressed mark, 'foot', OpenSauce's 'yelo'), and content_bytes.h's readers
under it. Built with the build machine's C compiler, under the sanitizers
where it has them, and as C89."""
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "port" / "linux" / "game"

DRIVER = r"""
#include <stdio.h>
#include <stdlib.h>
#include "content_map_header.h"

int main(void)
{
	static unsigned char bytes[CONTENT_MAP_HEADER_SIZE];
	unsigned long size;
	struct content_map_info info;
	unsigned char word[4] = { 0x01, 0x02, 0x03, 0x84 };

	printf("%x %x %x %x %d\n", (unsigned)content_u16le(word), (unsigned)content_u16be(word),
		(unsigned)content_u32le(word), (unsigned)content_u32be(word), content_u32le(word) == 0x84030201UL);
	while (scanf("%lu", &size) == 1)
	{
		unsigned long index;

		for (index = 0; index < size; index++)
		{
			unsigned int value;

			if (scanf("%2x", &value) != 1)
				return 1;
			bytes[index] = (unsigned char)value;
		}
		if (!content_map_probe(size ? bytes : NULL, (size_t)size, &info))
			printf("no\n");
		else
			printf("%lu %lu %d %d %d\n", (unsigned long)info.version, (unsigned long)info.cache_version,
				info.compressed, info.footer, info.opensauce);
	}
	return 0;
}
"""


def compiler():
    return next((shutil.which(name) for name in ("clang", "cc", "gcc") if shutil.which(name)), None)


def build(tmp_path, standard, extra):
    source = tmp_path / "probe.c"
    source.write_text(DRIVER)
    binary = tmp_path / f"probe_{standard}"
    result = subprocess.run([compiler(), f"-std={standard}", "-pedantic", "-Wall", "-Wextra", "-Werror", "-O1", *extra,
                             "-iquote", str(GAME), str(source), "-o", str(binary)], capture_output=True, text=True)
    return binary, result


def header(version, size=0x800, foot=True, yelo=False):
    data = bytearray(0x800)
    data[0:4] = b"daeh"
    struct.pack_into("<I", data, 4, version)
    if foot:
        data[0x7FC:0x800] = b"toof"
    if yelo:
        data[0x70:0x74] = b"oley"
    return bytes(data[:size])


CASES = [
    (header(5), "5 5 0 1 0"),
    (header(7), "7 7 0 1 0"),
    (header(13), "13 13 0 1 0"),
    (header(609, yelo=True), "609 609 0 1 1"),
    (header(0x861A0000 | 609), f"{0x861A0000 | 609} 609 1 1 0"),
    (header(0x871A0261), f"{0x871A0261} {0x871A0261} 0 1 0"),
    (header(609, foot=False), "609 609 0 0 0"),
    (header(5, size=8), "5 5 0 -1 -1"),
    (header(5, size=0x74), "5 5 0 -1 0"),
    (header(5, size=7), "no"),
    (b"head" + bytes(0x7FC), "no"),
    (b"", "no"),
]


@pytest.mark.parametrize("standard", ["c89", "c99"])
def test_probe(tmp_path, standard):
    if not compiler():
        pytest.skip("no C compiler")
    binary, result = build(tmp_path, standard, ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"])
    if result.returncode != 0:
        binary, result = build(tmp_path, standard, [])
    assert result.returncode == 0, result.stdout + result.stderr
    feed = "".join(f"{len(data)} {data.hex()}\n" for data, _ in CASES)
    run = subprocess.run([str(binary)], input=feed, capture_output=True, text=True, timeout=60)
    assert run.returncode == 0, run.stdout + run.stderr
    lines = run.stdout.splitlines()
    assert lines[0] == "201 102 84030201 1020384 1"
    assert lines[1:] == [expected for _, expected in CASES]
