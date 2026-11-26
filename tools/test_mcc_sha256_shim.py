"""mcc_sha256.h's names over the game's one SHA-256 (content_sha256.c): the
digests are SHA-256's, and a size of zero or less adds nothing, as the old
mcc_sha256_add did. Needs a C compiler."""

import hashlib
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "port" / "linux" / "game"

DRIVER = r"""
#include <stdio.h>
#include "mcc_sha256.h"
int main(void)
{
	struct mcc_sha256 context;
	unsigned char digest[32];
	int index;

	mcc_sha256_begin(&context);
	mcc_sha256_add(&context, "ab", 2);
	mcc_sha256_add(&context, "zzzz", 0);
	mcc_sha256_add(&context, "zzzz", -1);
	mcc_sha256_add(&context, "zzzz", -2147483647 - 1);
	mcc_sha256_add(&context, "c", 1);
	mcc_sha256_end(&context, digest);
	for (index = 0; index < 32; index++)
		printf("%02x", digest[index]);
	printf("\n");
	return 0;
}
"""


def test_sizes_of_zero_or_less_add_nothing(tmp_path):
    cc = next((name for name in (os.environ.get("CC"), "clang", "cc", "gcc") if name and shutil.which(name)), None)
    if not cc:
        pytest.skip("no C compiler")
    (tmp_path / "driver.c").write_text(DRIVER)
    binary = tmp_path / "driver"
    subprocess.run([cc, "-std=gnu89", "-Wall", "-Werror", f"-I{GAME}", str(tmp_path / "driver.c"),
                    str(GAME / "content_sha256.c"), "-o", str(binary)], check=True)
    out = subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout.strip()
    assert out == hashlib.sha256(b"abc").hexdigest()
