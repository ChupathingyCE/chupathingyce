"""A clipboard invite answered No is not asked about again
(port/linux/game/invite_prompt.c): its hash is kept in
private/invites_declined.txt, so the same invite left on the clipboard does
not ask at the next start (#1818). The code itself joins the game, so the
file never holds it; and a Yes is not kept, so an invite copied again after
a join that failed asks again. The functions are taken from the game's
source and compiled with stand-ins (and Monocypher); a second run of the
program is the next start."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, read  # noqa: E402

SOURCE = read("port/linux/game/invite_prompt.c")
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monocypher.h"
#define csmemcpy memcpy
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define INVITE_TEXT_SIZE 256
#define INVITE_CODE_SIZE 160
#define MAXIMUM_DECLINED 8
#define INVITE_HASH_SIZE 32
#define INVITE_HASH_TEXT_SIZE 65
enum port_folder { _port_folder_private };
static char const *folder;
static char const *platform_folder(enum port_folder which) { (void)which; return folder; }
static void platform_log(char const *format, ...) { (void)format; }
static int joined;
static int p2p_join_invite(char const *text) { (void)text; joined++; return 1; }
static struct
{
	boolean pending, active, asking;
	char invite[INVITE_TEXT_SIZE];
	void (*ask_answer)(boolean yes);
	char declined[MAXIMUM_DECLINED][INVITE_HASH_TEXT_SIZE];
	short declined_next;
	boolean declined_read;
} invite_prompt;
static void invite_code(char const *text, char *code, long size)
{
	char const *search = strstr(text, "join/");
	snprintf(code, (size_t)size, "%s", search ? search + 5 : "");
}
@FUNCTIONS@
static void asked(char const *link, boolean join)
{
	snprintf(invite_prompt.invite, sizeof(invite_prompt.invite), "%s", link);
	invite_prompt.pending = TRUE;
	answer(join);
}
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char **argv)
{
	int index;
	char link[64];

	folder = argv[1];
	if (!strcmp(argv[2], "first"))
	{
		CHECK(!invite_declined("halo://join/zzzz"));
		asked("halo://join/zzzz", FALSE);
		CHECK(invite_declined("halo://join/zzzz"));
		CHECK(invite_declined("https://example.invalid/join/zzzz"));
		CHECK(!invite_declined("halo://join/bbbb"));
		/* a Yes is not kept */
		asked("halo://join/yyyy", TRUE);
		CHECK(joined == 1 && !invite_declined("halo://join/yyyy"));
		/* (no code: never kept) */
		asked("not an invite", FALSE);
		CHECK(!invite_declined("not an invite"));
	}
	else if (!strcmp(argv[2], "next"))
	{
		/* the next start: the declined invite is not asked again; the one
		joined and a new one are */
		CHECK(invite_declined("halo://join/zzzz"));
		CHECK(!invite_declined("halo://join/yyyy"));
		CHECK(!invite_declined("halo://join/bbbb"));
		/* (the last few kept: eight more push the first out) */
		for (index = 0; index < MAXIMUM_DECLINED; index++)
		{
			snprintf(link, sizeof(link), "halo://join/q%d", index);
			asked(link, FALSE);
		}
		CHECK(!invite_declined("halo://join/zzzz"));
	}
	else if (!strcmp(argv[2], "last"))
	{
		CHECK(!invite_declined("halo://join/zzzz"));
		CHECK(invite_declined("halo://join/q0") && invite_declined("halo://join/q7"));
	}
	printf("ok\n");
	return 0;
}
"""


def test_invite_declined_once(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    functions = "\n".join(function(SOURCE, name) for name in (
        "invite_hash", "invite_declined_path", "invite_declined_read", "invite_declined_add", "invite_declined",
        "answer"))
    source = tmp_path / "declined.c"
    source.write_text(HARNESS.replace("@FUNCTIONS@", functions))
    binary = tmp_path / "declined"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", f"-I{MONOCYPHER}", str(source),
                    str(MONOCYPHER / "monocypher.c"), "-o", str(binary)], check=True)
    private = tmp_path / "private"
    private.mkdir()
    for step in ("first", "next", "last"):
        result = subprocess.run([str(binary), str(private), step], capture_output=True, text=True)
        assert result.returncode == 0 and "ok" in result.stdout, step + ": " + result.stdout + result.stderr
    kept = (private / "invites_declined.txt").read_text()
    lines = kept.splitlines()
    assert len(lines) == 8 and all(len(line) == 64 and int(line, 16) >= 0 for line in lines), kept
    # (hashes only, in hex, which has no z, y or q: no code, nor any part of one, is ever written)
    for code in ["zzzz", "yyyy"] + ["q%d" % i for i in range(8)]:
        assert code not in kept, code
