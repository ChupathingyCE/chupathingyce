"""The host's game settings, again: a client that resolved the host's MCC
package takes the same session (name and digest, its package's files
unchanged) again without hashing it (port/linux/game/mcc_identity.c,
mcc_identity_accept and mcc_identity_seen).

The settings come with every lobby change, and a host may send them as often
as it likes, each with another map version (which makes the client check the
record again): each check hashed the whole package on the game thread, and a
host could hold its clients' games still that way.

The real mcc_identity_seen, mcc_identity_matches and mcc_identity_accept
are compiled on their own over a fake package whose hashing is counted.
Needs only a C compiler.
"""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
IDENTITY_C = ROOT / "port/linux/game/mcc_identity.c"


def function(source, signature):
    """The definition that starts with signature, to its closing brace."""
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


PRELUDE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define _error_silent 0
#define csstrcasecmp strcasecmp
#define FALSE 0
#define MCC_IDENTITY_MAX_CANDIDATES 16
struct mcc_precache_snapshot { long size, time; };
struct network_game { struct { char name[128]; unsigned long version; } map; };
static struct {
    char name[64];
    unsigned char digest[32];
    struct mcc_precache_snapshot files;
    int valid;
} mcc_host_identity;
struct mcc_client_binding {
    char session[64], local[64];
    unsigned char digest[32];
    struct mcc_precache_snapshot files;
};
static struct mcc_client_binding mcc_client_identity;
static unsigned char host_digest[32] = { 7 };
static int probes;
static long package_time = 1;
static void error(int priority, char const *format, ...) { (void)priority; (void)format; }
static unsigned char const *mcc_identity_session_digest(struct network_game const *game) { (void)game; return host_digest; }
static void mcc_cache_precache_clear(void) {}
static int mcc_level_name(char const *name) { return !strncmp(name, "mcc_maps\\", 9); }
static int mcc_package_name(char const *name, char *session) { strcpy(session, name); return 1; }
static int mcc_identity_error(char const *message) { (void)message; return 0; }
static int mcc_identity_refusal(struct network_game const *game, char const *session) { (void)game; (void)session; return 0; }
static void mcc_maps_rescan(void) {}
static short mcc_maps_count(int all) { (void)all; return 1; }
static char const *mcc_maps_level_name(short index) { (void)index; return "mcc_maps\\pack\\level"; }
static int mcc_precache_snapshot(char const *name, struct mcc_precache_snapshot *files)
{
    (void)name;
    files->size = 100;
    files->time = package_time;
    return 1;
}
static int mcc_content_probe(char const *name, unsigned char *actual)
{
    (void)name;
    probes++;
    memcpy(actual, host_digest, 32);
    return 1;
}
"""

MAIN = r"""
int main(void)
{
    struct network_game game;
    int round;

    memset(&game, 0, sizeof(game));
    strcpy(game.map.name, "mcc_maps\\pack\\level");
    /* the host's settings, each with another version */
    for (round = 0; round < 5; round++)
    {
        game.map.version = (unsigned long)round;
        if (!mcc_identity_accept(&game)) { printf("round %d refused\n", round); return 1; }
    }
    if (probes != 1) { printf("the package was hashed %d times\n", probes); return 1; }
    /* its files changed: hashed again */
    package_time = 2;
    if (!mcc_identity_accept(&game) || probes != 2) { printf("a changed package was not hashed again\n"); return 1; }
    /* another digest: hashed again, and refused */
    host_digest[0] = 8;
    memset(&mcc_host_identity, 0, sizeof(mcc_host_identity));
    {
        int before = probes;

        mcc_identity_accept(&game);
        if (probes == before) { printf("another digest was taken without a check\n"); return 1; }
    }
    printf("mcc_identity_reuse_test: ok\n");
    return 0;
}
"""


def test_a_resolved_session_is_not_hashed_again(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("no C compiler")
    source = IDENTITY_C.read_text(encoding="utf-8")
    program = (PRELUDE +
               function(source, "static int mcc_identity_matches(") +
               function(source, "static int mcc_identity_seen(") +
               function(source, "int mcc_identity_accept(struct network_game const *game)\n") +
               MAIN)
    path = tmp_path / "mcc_identity_reuse_test.c"
    path.write_text(program, encoding="utf-8")
    binary = tmp_path / "mcc_identity_reuse_test"
    build = subprocess.run([cc, "-std=gnu11", "-w", str(path), "-o", str(binary)], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0 and "mcc_identity_reuse_test: ok" in run.stdout, run.stdout + run.stderr
