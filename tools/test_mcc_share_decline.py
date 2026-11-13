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


ADMISSION = r'''
#include "mcc_share_rules.h"
#include <stdio.h>
#include <string.h>
int mcc_package_name(char const *name, char canonical[64]) { (void)name; (void)canonical; return 0; }
static int failures;
#define CHECK(c) do { if (!(c)) { printf("failed line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static unsigned char host[MCC_ADMISSION_IDENTITY_SIZE], other[MCC_ADMISSION_IDENTITY_SIZE];
static void body(unsigned char out[MCC_ADMISSION_SIZE], unsigned op, unsigned generation, unsigned char const *identity)
{
	int i;
	for (i = 0; i < 4; i++) { out[i] = (unsigned char)(op >> (8 * i)); out[4 + i] = (unsigned char)(generation >> (8 * i)); }
	memcpy(out + 8, identity, MCC_ADMISSION_IDENTITY_SIZE);
}
int main(void)
{
	struct mcc_admission hold;
	unsigned char b[MCC_ADMISSION_SIZE];
	unsigned long t = 1000;
	memset(&hold, 0, sizeof(hold));
	strcpy((char *)host, "mcc_maps\\2900197929\\desert");
	host[64] = 'W'; host[65] = 'S'; host[66] = 1;
	memset(host + 128, 0x33, 32); memset(host + 160, 0x7f, 32);
	memcpy(other, host, sizeof(other)); other[200 - 8] = 0; other[130] = 0x44;
	/* begin with the host's identity: held and acked; ping renews */
	body(b, 1, 7, host);
	CHECK(mcc_admission_take(&hold, 10, b, sizeof(b), host, t) == 5);
	CHECK(mcc_admission_holding(&hold, 10, t + 1000));
	body(b, 2, 7, host);
	CHECK(mcc_admission_take(&hold, 10, b, sizeof(b), host, t + 15000) == 5);
	CHECK(mcc_admission_holding(&hold, 10, t + 30000));
	/* quiet for 20 s: let go */
	CHECK(!mcc_admission_holding(&hold, 10, t + 35000));
	/* the checksum (the last 4 bytes) isn't compared */
	memset(&hold, 0, sizeof(hold));
	memcpy(b + 8, host, sizeof(host)); b[8 + 195] = 9; b[0] = 1;
	CHECK(mcc_admission_take(&hold, 10, b, sizeof(b), host, t) == 5);
	/* ready: the 15 s grace, then let go; a later begin is refused */
	body(b, 3, 7, host);
	CHECK(mcc_admission_take(&hold, 10, b, sizeof(b), host, t + 2000) == 0);
	CHECK(mcc_admission_holding(&hold, 10, t + 16000) && !mcc_admission_holding(&hold, 10, t + 17001));
	body(b, 1, 8, host);
	CHECK(mcc_admission_take(&hold, 10, b, sizeof(b), host, t + 18000) == 6);
	/* another identity never opens or keeps a hold, however often it begins */
	memset(&hold, 0, sizeof(hold));
	body(b, 1, 1, other);
	CHECK(mcc_admission_take(&hold, 20, b, sizeof(b), host, t) == 6);
	CHECK(!mcc_admission_holding(&hold, 20, t + 1));
	body(b, 1, 2, host);
	CHECK(mcc_admission_take(&hold, 20, b, sizeof(b), host, t + 10) == 5);
	body(b, 1, 3, other);
	CHECK(mcc_admission_take(&hold, 20, b, sizeof(b), host, t + 20) == 6);
	/* (the hold it had keeps its own limit: 120 s at most, pinged or not) */
	body(b, 2, 2, host);
	{
		unsigned long at;
		for (at = 15000; at < 120000; at += 15000)
		{
			CHECK(mcc_admission_take(&hold, 20, b, sizeof(b), host, t + at) == 5);
			CHECK(mcc_admission_holding(&hold, 20, t + at + 1));
		}
	}
	CHECK(!mcc_admission_holding(&hold, 20, t + 120010));
	CHECK(mcc_admission_take(&hold, 20, b, sizeof(b), host, t + 121000) == 6);
	/* no identity on the host: refused; another generation: refused */
	memset(&hold, 0, sizeof(hold));
	body(b, 1, 4, host);
	CHECK(mcc_admission_take(&hold, 30, b, sizeof(b), NULL, t) == 6);
	CHECK(mcc_admission_take(&hold, 30, b, sizeof(b), host, t) == 5);
	body(b, 2, 5, host);
	CHECK(mcc_admission_take(&hold, 30, b, sizeof(b), host, t + 1) == 6);
	/* cancel closes it; another machine on the slot starts anew */
	body(b, 4, 4, host);
	CHECK(mcc_admission_take(&hold, 30, b, sizeof(b), host, t + 2) == 0 && !mcc_admission_holding(&hold, 30, t + 3));
	body(b, 1, 9, host);
	CHECK(mcc_admission_take(&hold, 31, b, sizeof(b), host, t + 4) == 5 && mcc_admission_holding(&hold, 31, t + 5));
	CHECK(!mcc_admission_holding(&hold, 30, t + 5));
	/* the host on another map: the old hold goes */
	other[130] = 0x55;
	CHECK(mcc_admission_take(&hold, 31, b, sizeof(b), other, t + 6) == 6 && !mcc_admission_holding(&hold, 31, t + 7));
	/* not a 105: the wrong size, generation 0, a name without its zero, not an MCC map's */
	CHECK(mcc_admission_take(&hold, 31, b, sizeof(b) - 1, host, t) == -1);
	body(b, 1, 0, host);
	CHECK(mcc_admission_take(&hold, 31, b, sizeof(b), host, t) == -1);
	body(b, 1, 1, host); memset(b + 8, 'x', 64);
	CHECK(mcc_admission_take(&hold, 31, b, sizeof(b), host, t) == -1);
	body(b, 1, 1, host); memcpy(b + 8, "levels\\test", 12);
	CHECK(mcc_admission_take(&hold, 31, b, sizeof(b), host, t) == -1);
	printf(failures ? "admission: failed\n" : "admission: ok\n");
	return failures != 0;
}
'''


def test_admission_hold_rules(tmp_path):
    """OpenCE's 105 on our host: only the host's own identity opens or keeps
    a hold; ack, refused, the ready grace, the 120 s and 20 s limits, and
    what isn't a 105 at all (mcc_share_rules.c)"""
    clang = shutil.which("clang")
    if not clang:
        pytest.skip("clang required")
    (tmp_path / "driver.c").write_text(ADMISSION)
    exe = tmp_path / ("admission.exe" if sys.platform == "win32" else "admission")
    flags = ["--target=i686-pc-windows-msvc", "-fuse-ld=lld", "-D_CRT_SECURE_NO_WARNINGS"] if sys.platform == "win32" else []
    result = subprocess.run([clang, *flags, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-I", str(GAME),
                             str(tmp_path / "driver.c"), str(GAME / "mcc_share_rules.c"), "-o", str(exe)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0 and "admission: ok" in run.stdout, run.stdout
