"""A clipboard invite is asked about once (port/linux/game/invite_prompt.c):
an answer, Yes or No, is kept in private/invites_answered.txt, so the same
invite left on the clipboard is not asked about at the next start (#1818).
The functions are taken from the game's source and compiled with stand-ins;
a second run of the program is the next start."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, read  # noqa: E402

SOURCE = read("port/linux/game/invite_prompt.c")

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define INVITE_CODE_SIZE 160
#define MAXIMUM_ANSWERED 8
#define csstrncpy strncpy
enum port_folder { _port_folder_private };
static char const *folder;
static char const *platform_folder(enum port_folder which) { (void)which; return folder; }
static struct
{
	char answered[MAXIMUM_ANSWERED][INVITE_CODE_SIZE];
	short answered_next;
	boolean answered_read;
} invite_prompt;
static void invite_code(char const *text, char *code, long size)
{
	char const *search = strstr(text, "join/");
	snprintf(code, (size_t)size, "%s", search ? search + 5 : "");
}
@FUNCTIONS@
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char **argv)
{
	int index;
	char link[64];

	folder = argv[1];
	if (!strcmp(argv[2], "first"))
	{
		CHECK(!invite_answered("halo://join/aaaa"));
		invite_answered_add("halo://join/aaaa");
		CHECK(invite_answered("halo://join/aaaa"));
		CHECK(invite_answered("https://example.invalid/join/aaaa"));
		CHECK(!invite_answered("halo://join/bbbb"));
		/* (no code: never kept) */
		invite_answered_add("not an invite");
		CHECK(!invite_answered("not an invite"));
	}
	else if (!strcmp(argv[2], "next"))
	{
		/* the next start: the answered invite is not asked again; a new one is */
		CHECK(invite_answered("halo://join/aaaa"));
		CHECK(!invite_answered("halo://join/bbbb"));
		/* (the last few kept: eight more push the first out) */
		for (index = 0; index < MAXIMUM_ANSWERED; index++)
		{
			snprintf(link, sizeof(link), "halo://join/c%d", index);
			invite_answered_add(link);
		}
		CHECK(!invite_answered("halo://join/aaaa"));
		CHECK(invite_answered("halo://join/c0") && invite_answered("halo://join/c7"));
	}
	else if (!strcmp(argv[2], "last"))
	{
		CHECK(!invite_answered("halo://join/aaaa"));
		CHECK(invite_answered("halo://join/c0") && invite_answered("halo://join/c7"));
	}
	printf("ok\n");
	return 0;
}
"""


def test_invite_answered_once(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    functions = "\n".join(function(SOURCE, name) for name in (
        "invite_answered_path", "invite_answered_read", "invite_answered_add", "invite_answered"))
    source = tmp_path / "answered.c"
    source.write_text(HARNESS.replace("@FUNCTIONS@", functions))
    binary = tmp_path / "answered"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", str(source), "-o", str(binary)], check=True)
    private = tmp_path / "private"
    private.mkdir()
    for step in ("first", "next", "last"):
        result = subprocess.run([str(binary), str(private), step], capture_output=True, text=True)
        assert result.returncode == 0 and "ok" in result.stdout, step + ": " + result.stdout + result.stderr
    assert (private / "invites_answered.txt").read_text().splitlines() == ["c%d" % i for i in range(8)]
