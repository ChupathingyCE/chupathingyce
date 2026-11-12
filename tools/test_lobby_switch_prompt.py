"""The host's question before its lobby moves to or from an MCC map
(port/linux/game/invite_prompt.c's invite_prompt_ask, asked by
network_server_manager.c): it never overwrites a clipboard invite's question
waiting (it is asked once that is answered), and a question taken back
(invite_prompt_ask_cancel: the host left its lobby) is never answered. The
functions are taken from the game's source and compiled with stand-ins."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, read  # noqa: E402

SOURCE = read("port/linux/game/invite_prompt.c")
STATE = SOURCE[SOURCE.index("static struct\n{"):SOURCE.index("} invite_prompt;") + len("} invite_prompt;")]

HARNESS = r"""
#include <stdio.h>
#include <string.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define INVITE_TEXT_SIZE 256
#define INVITE_CODE_SIZE 160
#define MAXIMUM_DECLINED 8
#define PASSWORD_LENGTH 32
#define csstrncpy strncpy
static void platform_log(char const *format, ...) { (void)format; }
static int p2p_join_invite(char const *text) { (void)text; return 1; }
static void invite_code(char const *text, char *code, int size) { snprintf(code, (size_t)size, "%s", text); }
static int platform_take_clipboard_invite(char *text, int size) { (void)text; (void)size; return 0; }
static int p2p_locked_invite_waiting(void) { return 0; }
static void password_close(void) {}
static int browser_screen_invite_game(char const *a, char *b, long c, char *d, long e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
static boolean invite_declined(char const *text) { (void)text; return FALSE; }
static unsigned long system_milliseconds(void) { return 0; }
static void event_manager_flush(void) {}
struct key_stroke { int key_code; char ascii_code; };
static int input_get_key(struct key_stroke *key) { (void)key; return 0; }
static void platform_text_field(int typing) { (void)typing; }
@STATE@
@FUNCTIONS@
static int answered, said_yes;
static void answer_function(boolean yes) { answered++; said_yes = yes; }
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
	/* an invite's question waiting: the host's waits behind it */
	strcpy(invite_prompt.invite, "halo://join/abc");
	invite_prompt.pending = TRUE;
	invite_prompt_ask("MAP", "Switch to this MCC map?", "1 player without MCC maps will leave.", answer_function);
	CHECK(!invite_prompt.asking && invite_prompt.ask_waiting && !strcmp(invite_prompt.invite, "halo://join/abc"));
	answer(FALSE);
	CHECK(!answered);
	invite_prompt_update(TRUE);
	CHECK(invite_prompt.asking && invite_prompt.pending);
	answer(TRUE);
	CHECK(answered == 1 && said_yes && !invite_prompt.asking);
	/* taken back before it is answered: never answered */
	answered = 0;
	invite_prompt_ask("MAP", "Switch away from MCC?", "2 players without MCC maps will leave.", answer_function);
	CHECK(invite_prompt.asking);
	invite_prompt_ask_cancel(answer_function);
	CHECK(!invite_prompt.asking && !invite_prompt.pending);
	answer(TRUE);
	CHECK(!answered);
	/* (taken back while still waiting behind an invite) */
	invite_prompt.pending = TRUE;
	invite_prompt_ask("MAP", "Switch to this MCC map?", "", answer_function);
	invite_prompt_ask_cancel(answer_function);
	invite_prompt.pending = FALSE;
	invite_prompt_update(TRUE);
	CHECK(!invite_prompt.asking && !answered);
	printf("ok\n");
	return 0;
}
"""


def test_lobby_switch_prompt(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    functions = "\n".join(function(SOURCE, name) for name in (
        "answer", "invite_prompt_ask", "invite_prompt_ask_cancel", "invite_prompt_update"))
    functions = re.sub(r"^(void|boolean) ", r"static \1 ", functions, flags=re.M)
    source = tmp_path / "prompt.c"
    source.write_text(HARNESS.replace("@STATE@", STATE).replace("@FUNCTIONS@", functions))
    binary = tmp_path / "prompt"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", str(source),
                    "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0 and "ok" in result.stdout, result.stdout + result.stderr
