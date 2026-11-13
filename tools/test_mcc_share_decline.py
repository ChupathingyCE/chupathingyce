"""OpenCE's MCC package sharing (their messages 103 and 104; port/linux/game/
mcc_share_rules.h), which this build never does: a host of ours declines a
44-style request with a 104 of result 1 (sharing off), nothing else answered,
and a client of ours drops any 104 unread (the layout's dropped kinds). The
request fixture is bytes as OpenCE's 44 client sends them, per the behavior
their 44 hosts and clients use: op 1 (manifest), file 0, flags 0, reserved 0,
session 0, offset 0, length 0, then the level name in 64 bytes.

The C is compiled with clang (skipped without one); the loopback run of the
probe is tools/test_net43_host.py's.
"""
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

import delta_table

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "port/linux/game"

DRIVER = r'''
#include "mcc_share_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* (mcc_share_decide's, not used here: mcc_package.c pulls the whole map code in) */
int mcc_package_name(char const *name, char canonical[64]) { (void)name; (void)canonical; return 0; }
int main(int argc, char **argv)
{
    unsigned char request[4096], reply[MCC_SHARE_RESPONSE_SIZE];
    size_t size, i;
    FILE *f;
    if (argc != 2) return 2;
    f = fopen(argv[1], "rb");
    if (!f) return 3;
    size = fread(request, 1, sizeof(request), f);
    fclose(f);
    if (!mcc_share_decline(request, size, reply)) { printf("dropped\n"); return 0; }
    printf("declined result %d ", mcc_share_answer_result(reply, sizeof(reply)));
    for (i = 0; i < sizeof(reply); i++) printf("%02x", reply[i]);
    printf("\n");
    return 0;
}
'''


@pytest.fixture(scope="module")
def decline(tmp_path_factory):
    clang = shutil.which("clang")
    if not clang:
        pytest.skip("clang required")
    work = tmp_path_factory.mktemp("mcc-share-decline")
    (work / "driver.c").write_text(DRIVER)
    exe = work / ("decline.exe" if sys.platform == "win32" else "decline")
    flags = ["--target=i686-pc-windows-msvc", "-fuse-ld=lld", "-D_CRT_SECURE_NO_WARNINGS"] if sys.platform == "win32" else []
    result = subprocess.run([clang, *flags, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-I", str(GAME),
                             str(work / "driver.c"), str(GAME / "mcc_share_rules.c"), "-o", str(exe)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr

    def run(request: bytes):
        path = work / "request.bin"
        path.write_bytes(request)
        return subprocess.run([str(exe), str(path)], capture_output=True, text=True, check=True).stdout.strip()
    return run


def request_44(op=1, file=0, flags=0, session=0, offset=0, length=0, level=b"mcc_maps\\123456789\\coldsnap"):
    """OpenCE 44's request body (80 bytes), as the spec lays it out"""
    assert len(level) < 64
    return struct.pack("<BBBBIII", op, file, flags, 0, session, offset, length) + level.ljust(64, b"\0")


def test_manifest_request_declined(decline):
    """the 44-style manifest request: a 104 of the same op, result 1 (off),
    no entries, no length, the session and the level echoed, no credit"""
    output = decline(request_44())
    assert output.startswith("declined result 1 ")
    reply = bytes.fromhex(output.split()[-1])
    assert len(reply) == 144
    op, result, file, count, session, offset, length = struct.unpack("<BBBBIII", reply[:16])
    assert (op, result, file, count, session, offset, length) == (1, 1, 0, 0, 0, 0, 0)
    assert reply[16:80] == b"mcc_maps\\123456789\\coldsnap".ljust(64, b"\0")
    assert reply[80:144] == bytes(64)


def test_every_op_declined_with_its_fields_echoed(decline):
    """chunk, done and have requests (a client past the manifest, which ours
    never answers with one): declined the same way, their session, file and
    offset echoed so the client matches the answer to its request"""
    for op in (2, 3, 4):
        output = decline(request_44(op=op, file=3, session=0x11223344, offset=6144, length=3072))
        reply = bytes.fromhex(output.split()[-1])
        assert struct.unpack("<BBBBIII", reply[:16]) == (op, 1, 3, 0, 0x11223344, 6144, 0)


@pytest.mark.parametrize("bad", [
    b"", request_44()[:79], request_44() + b"\0", request_44(op=0), request_44(op=5),
    request_44(level=b"x" * 63) [:16] + b"x" * 64,
])
def test_anything_else_dropped(decline, bad):
    """not a request (short, long, an op this build doesn't know, a level
    without its zero): nothing answered"""
    assert decline(bad) == "dropped"


def test_a_client_of_ours_drops_answers():
    """the layouts with MCC's messages (42 on, 44 built in) drop 103 and 104
    unread for a client: an answer never asked for goes nowhere"""
    for version in (42, 43, 44, 45):
        dropped = delta_table.built_in_layout(version)["dropped_messages"]
        assert 103 in dropped and 104 in dropped, version
    assert 103 not in delta_table.built_in_layout(41)["dropped_messages"]
