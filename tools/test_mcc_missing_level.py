"""An MCC level that isn't installed fails cleanly instead of hanging (#1149).

`map_name mcc_maps\\<item>\\<level>` for a level that isn't there used to wait
for ever: game_precache_new_map waited for a copy that never started, then
finished it, reading the header of no cache slot, whose read could never be
issued and was tried again for ever (cached_map_issue_async_request); the
stuck main thread never read SIGTERM. Each piece is taken from the game's
source and run here under a time limit, so that a hang fails the test:

- game_precache_new_map (source/game/game.c) gives up on an MCC level that
  can't be loaded, without waiting, and a wait for a copy that isn't going
  ends;
- cached_map_issue_async_request (source/cache/cache_files_windows.c) fails a
  read that can't be issued, and stops retrying one the system is short of
  resources for;
- the quit watchdog (port/linux/src/sdl_platform.c) ends a process whose
  main thread is stuck after a SIGTERM, and never one that is still running
  or saving as it quits."""
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, mutated, read  # noqa: E402

TIMEOUT = 20


@pytest.fixture(scope="module")
def cc():
    compiler = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    return compiler


def build(cc, tmp_path, name, source, flags=()):
    (tmp_path / f"{name}.c").write_text(source)
    binary = tmp_path / name
    subprocess.run([cc, "-std=gnu99", "-w", "-O0", *flags, "-o", str(binary), str(tmp_path / f"{name}.c"),
                    "-lpthread"], check=True, timeout=120)
    return binary


def run(binary, *arguments):
    try:
        result = subprocess.run([str(binary), *arguments], capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        pytest.fail(f"{binary.name} {' '.join(arguments)} hung (more than {TIMEOUT} seconds)")
    return result


# ---------- game_precache_new_map

PRECACHE_PRELUDE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean;
typedef float real;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
enum { _cached_map_file_in_progress = 0, _cached_map_file_success = 1, _cached_map_file_failed = 2 };
enum { _error_silent = 0 };
struct game_runtime_globals_prefix { boolean map_load_in_progress; real loading_progress; };
static struct game_runtime_globals_prefix globals_storage;
static struct game_runtime_globals_prefix *game_globals = &globals_storage;
static int global_scenario_index = NONE, player_spawn_count = 1;
static int mcc_present, status_asks, ends, begins;
static void error(int kind, char const *format, ...) { (void)kind; (void)format; }
#define match_assert(f, l, c) ((void)(f), (void)(l), (void)(c))
#define match_vassert(f, l, c, m) ((void)(f), (void)(l), (void)(c), (void)(m))
static boolean mcc_level_name(char const *name) { return !strncmp(name, "mcc_maps\\", 9); }
/* (the MCC level isn't there; no stock map is being copied) */
static boolean cache_files_precache_map_loaded(char const *name) { return mcc_level_name(name) && mcc_present; }
static boolean copy_going;
static boolean cache_files_precache_in_progress(void) { return copy_going; }
static boolean cache_files_precache_is_copying_map(char const *name) { (void)name; return copy_going; }
static boolean cache_files_precache_map_begin(char const *name, boolean copy) {
    (void)copy; begins++; return !mcc_level_name(name) || mcc_present; }
/* (a copy that never started never finishes) */
static short cache_files_precache_map_status(real *progress) { (void)progress; status_asks++; return _cached_map_file_in_progress; }
static void cache_files_precache_map_end(void) { ends++; }
static void cache_files_precache_map_queue_end(void) {}
static void cache_files_precache_set_priority(boolean b) { (void)b; }
static void main_queue_map_name(char const *n) { (void)n; }
static void ui_widget_load_progress_widget(void) {}
static void progress_bar_begin(boolean b) { (void)b; }
static void progress_bar_end(void) {}
static void ui_widgets_close_all(void) {}
static void display_error_damaged_media(void) {}
static void main_save_current_solo_map(char const *n) { (void)n; }
static void player_ui_remember_player1_profile(boolean b) { (void)b; }
static unsigned long game_loading_screen_hold_milliseconds(char const *n) { (void)n; return 0; }
static unsigned long system_milliseconds(void) { return 0; }
static boolean loading_screen_skip_pressed(void) { return FALSE; }
static void game_loading_screen_hold_progress(unsigned long a, unsigned long b, boolean c) { (void)a; (void)b; (void)c; }
static void game_loading_screen_hold(unsigned long a, unsigned long b) { (void)a; (void)b; }
static void main_pregame_render(void) {}
static void main_rasterizer_throttle(void) {}
static void main_present_frame(void) {}
"""

PRECACHE_MAIN = r"""
int main(int argc, char **argv)
{
    char name[64];
    long result;
    snprintf(name, sizeof(name), "%s", argv[1]);
    copy_going = argc > 2;
    result = (long)game_precache_new_map(name, TRUE);
    printf("result %ld begins %d status %d ends %d\n", result, begins, status_asks, ends);
    return 0;
}
"""


def precache_source(game_c):
    body = function(game_c, "game_precache_new_map")
    # (a void return, as before the fix, still compiles: its result is then 1)
    if body.startswith("void game_precache_new_map("):
        body = body.replace("void game_precache_new_map(", "void precache_void(", 1)
        body += "\n#define game_precache_new_map(a, b) (precache_void(a, b), 1)\n"
    return PRECACHE_PRELUDE + body + PRECACHE_MAIN


def test_missing_mcc_level_is_not_waited_for(cc, tmp_path):
    binary = build(cc, tmp_path, "precache", precache_source(read("source/game/game.c")))
    result = run(binary, "mcc_maps\\2886943774\\a30")
    assert result.stdout.split() == ["result", "0", "begins", "1", "status", "0", "ends", "0"], result.stdout


def test_a_copy_that_is_not_going_is_not_waited_for(cc, tmp_path):
    # (whatever made precaching stop meanwhile: the wait ends, failed)
    game_c = read("source/game/game.c").replace(
        "if (mcc_level_name(map_name))\n\t\t\t{", "if (0)\n\t\t\t{", 1)
    binary = build(cc, tmp_path, "precache_no_copy", precache_source(game_c))
    result = run(binary, "mcc_maps\\2886943774\\a30")
    assert "status 0" in result.stdout, result.stdout


def test_negative_control_waits_for_ever(cc, tmp_path):
    # (the fault this test is for: the old wait, which asked a copy that
    # isn't going for its status until it finished)
    game_c = mutated(read("source/game/game.c"), "if (mcc_level_name(map_name))\n\t\t\t{", "if (0)\n\t\t\t{")
    game_c = mutated(game_c, "map_status = cache_files_precache_in_progress() ?",
                     "map_status = 1 ?")
    binary = build(cc, tmp_path, "precache_old", precache_source(game_c))
    with pytest.raises(subprocess.TimeoutExpired):
        subprocess.run([str(binary), "mcc_maps\\2886943774\\a30"], capture_output=True, timeout=3)


# ---------- cached_map_issue_async_request

ISSUE_PRELUDE = r"""
#include <stdio.h>
#include <string.h>
typedef int boolean;
typedef int BOOL;
typedef void *HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define TRUE 1
#define FALSE 0
#define WINAPI
#define CALLBACK
#define ERROR_SUCCESS 0
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_INVALID_USER_BUFFER 1784
#define ERROR_NO_SYSTEM_RESOURCES 1450
#define WAIT_IO_COMPLETION 0xC0
enum { _error_silent = 0 };
typedef struct { unsigned long Internal, InternalHigh, Offset, OffsetHigh; HANDLE hEvent; } OVERLAPPED;
typedef void (*LPOVERLAPPED_COMPLETION_ROUTINE)(unsigned long, unsigned long, OVERLAPPED *);
static char temporary[256];
static void error(int kind, char const *format, ...) { (void)kind; (void)format; }
#define match_assert(f, l, c) ((void)(f), (void)(l), (void)(c))
#define match_vassert(f, l, c, m) ((void)(f), (void)(l), (void)(c), (void)(m))
static char const *csprintf(char *b, char const *f, ...) { (void)f; return b; }
static unsigned long last_error, ticks, tries, fail_with;
static void SetLastError(unsigned long e) { last_error = e; }
static unsigned long GetLastError(void) { return last_error; }
static unsigned long GetTickCount(void) { return ticks += 100; }
static void Sleep(unsigned long m) { (void)m; }
static unsigned long SleepEx(unsigned long m, BOOL a) { (void)m; (void)a; return 0; }
/* (a file that can't be read: a closed or bad handle, or one the system is
short of resources for) */
static BOOL WINAPI ReadFileEx(HANDLE f, void *b, unsigned long s, OVERLAPPED *o, LPOVERLAPPED_COMPLETION_ROUTINE r)
{
    (void)f; (void)b; (void)s; (void)o; (void)r;
    tries++;
    SetLastError(fail_with);
    return FALSE;
}
static void CALLBACK routine(unsigned long e, unsigned long n, OVERLAPPED *o) { (void)e; (void)n; (void)o; }
typedef BOOL (WINAPI *cached_map_async_request_proc)(HANDLE, void *, unsigned long, OVERLAPPED *,
    LPOVERLAPPED_COMPLETION_ROUTINE);
#define CACHED_MAP_ISSUE_RETRY_MILLISECONDS 5000
"""

ISSUE_MAIN = r"""
int main(int argc, char **argv)
{
    OVERLAPPED overlapped;
    volatile boolean flag = FALSE;
    char buffer[16];
    long issued;
    (void)argc;
    sscanf(argv[1], "%lu", &fail_with);
    issued = (long)cached_map_issue_async_request(ReadFileEx, (HANDLE)1, &overlapped, buffer, sizeof(buffer), 0,
        &flag, routine);
    printf("issued %ld flag %d error %lu\n", issued, (int)flag, overlapped.Internal);
    return 0;
}
"""


def issue_source(cache_c):
    body = function(cache_c, "cached_map_issue_async_request")
    # (a void one, as before the fix, still compiles: its result is then 1)
    if "static void cached_map_issue_async_request(" in body:
        body = body.replace("static void cached_map_issue_async_request(", "static void issue_void(", 1)
        body += "\n#define cached_map_issue_async_request(...) (issue_void(__VA_ARGS__), 1)\n"
    return ISSUE_PRELUDE + body + ISSUE_MAIN


@pytest.mark.parametrize("error_code", [0, 6, 8, 1450, 1784])
def test_a_read_that_cannot_be_issued_fails(cc, tmp_path, error_code):
    binary = build(cc, tmp_path, "issue", issue_source(read("source/cache/cache_files_windows.c")))
    result = run(binary, str(error_code))
    words = result.stdout.split()
    assert words[:4] == ["issued", "0", "flag", "0"], result.stdout
    assert words[5] != "0", result.stdout


# ---------- the quit watchdog

WATCHDOG_MAIN = r"""
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define QUIT_WATCHDOG_SECONDS 2
static volatile sig_atomic_t quit_seen;
/* (SDL's: it only notes the quit, which a stuck main thread never reads) */
static void sdl_handler(int signal_number) { (void)signal_number; quit_seen = 1; }
%s
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
int main(int argc, char **argv)
{
    double until;
    (void)argc;
    signal(SIGTERM, sdl_handler);
    quit_watchdog_start();
    printf("ready\n");
    fflush(stdout);
    while (!quit_seen)
        if (!strcmp(argv[1], "running")) quit_watchdog_beat();
    if (!strcmp(argv[1], "stuck"))
        for (;;)
            ;
    if (!strcmp(argv[1], "saving"))
    {
        /* (a normal quit whose saves take longer than the watchdog's time:
        the events are no longer read meanwhile) */
        quit_watchdog_stand_down();
        for (until = now() + 3 * QUIT_WATCHDOG_SECONDS; now() < until; )
            ;
        printf("saved\n");
        return 0;
    }
    /* (running: the game goes on reading its events a while before it quits) */
    for (until = now() + 3 * QUIT_WATCHDOG_SECONDS; now() < until; )
        quit_watchdog_beat();
    printf("quit\n");
    return 0;
}
"""


def watchdog(cc, tmp_path):
    source = read("port/linux/src/sdl_platform.c")
    start = source.index("#ifndef QUIT_WATCHDOG_SECONDS")
    end = source.index("#endif", source.index("static void quit_watchdog_start(void)"))
    return build(cc, tmp_path, "watchdog", WATCHDOG_MAIN % source[start:end])


def quit_with_sigterm(binary, mode):
    process = subprocess.Popen([str(binary), mode], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        assert process.stdout.readline().strip() == "ready"
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            pytest.fail(f"the game ({mode}) ignored SIGTERM")
        return process.returncode, process.stdout.read(), process.stderr.read()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def test_sigterm_ends_a_stuck_game(cc, tmp_path):
    code, _, errors = quit_with_sigterm(watchdog(cc, tmp_path), "stuck")
    assert code == 128 + signal.SIGTERM
    assert "stuck" in errors


@pytest.mark.parametrize("mode, said", [("saving", "saved"), ("running", "quit")])
def test_sigterm_never_cuts_a_quit_short(cc, tmp_path, mode, said):
    # (a quit under way, saving, and a game still reading its events, are
    # left to finish however long they take)
    code, output, _ = quit_with_sigterm(watchdog(cc, tmp_path), mode)
    assert (code, output.strip()) == (0, said)
