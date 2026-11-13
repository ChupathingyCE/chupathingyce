"""MCC content matching uses all digest bytes and never rewrites wire paths."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import sys
import pytest
from harness import function, structure

ROOT = Path(__file__).resolve().parents[1]


def compile_tool(work, sources):
    clang = shutil.which("clang")
    if not clang:
        pytest.skip("clang required")
    exe = work / ("test.exe" if sys.platform == "win32" else "test")
    flags = ["--target=i686-pc-windows-msvc", "-fuse-ld=lld", "-D_CRT_SECURE_NO_WARNINGS"] if sys.platform == "win32" else []
    result = subprocess.run([clang, *flags, "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                             "-I", str(work), "-I", str(ROOT / "port/linux/game"),
                             *map(str, sources), "-o", str(exe)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return exe


@pytest.fixture(scope="module")
def sha_tool(tmp_path_factory):
    work = tmp_path_factory.mktemp("mcc-sha256")
    source = work / "test.c"
    source.write_text(r'''
#include "mcc_sha256.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
 struct mcc_sha256 hash;unsigned char data[32768],digest[32];size_t count;unsigned i;
 FILE *f;if(argc!=3)return 2;f=fopen(argv[1],"rb");if(!f)return 3;
 count=(size_t)atoi(argv[2]);if(!count || count>sizeof(data))return 4;
 mcc_sha256_begin(&hash);
 while((i=(unsigned)fread(data,1,count,f))!=0)mcc_sha256_add(&hash,data,(int)i);
 if(ferror(f))return 5;fclose(f);mcc_sha256_end(&hash,digest);
 for(i=0;i<32;i++)printf("%02x",digest[i]);return 0;
}
''')
    return compile_tool(work, [source, ROOT / "port/linux/game/mcc_sha256.c"])


@pytest.mark.parametrize("data", [b"", b"abc", b"a" * 55, b"a" * 56, b"a" * 64,
                                  b"a" * 65, bytes(range(256)) * 4096],
                         ids=["empty", "abc", "55-bytes", "56-bytes", "64-bytes", "65-bytes", "1-MiB"])
@pytest.mark.parametrize("chunk", [1, 63, 32768])
def test_sha256_vectors(sha_tool, tmp_path, data, chunk):
    path = tmp_path / "bytes"
    path.write_bytes(data)
    result = subprocess.run([str(sha_tool), str(path), str(chunk)], capture_output=True, text=True)
    assert result.returncode == 0
    assert result.stdout == hashlib.sha256(data).hexdigest()
