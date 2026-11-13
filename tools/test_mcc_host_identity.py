"""A host's MCC content identity outlives its own client and follows its map (#1129).

The host's settings for an MCC map carry its content digest; a record with an
all-zero digest is refused by every joiner, the host's own client too. These
tests compile mcc_identity.c's host side against stubs: the identity survives
the client's dispose while this machine still hosts, a map the host moves to
(a rotation's next game, a lobby's change) is held until its check is done,
and never goes out with zeros.
"""
from pathlib import Path
from harness import function
from test_mcc_identity import compile_tool
import subprocess

ROOT = Path(__file__).resolve().parents[1]
IDENTITY = ROOT / "port/linux/game/mcc_identity.c"


def build(tmp_path):
    source = IDENTITY.read_text()
    host = source[source.index("static struct {\n    char name[64];"):source.index("static int mcc_identity_error")]
    names = ["mcc_identity_disconnect", "mcc_identity_host_clear", "mcc_identity_host_current",
             "mcc_identity_host_set", "mcc_identity_prepare_host", "mcc_identity_host_ready",
             "mcc_identity_host_overlay"]
    (tmp_path / "test.c").write_text(r'''
#include "mcc_precache.h"
#include "mcc_workshop.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define _error_silent 0
static int servers, cancels, requests, errors, logs;
static unsigned char checked[2][32];   /* the finished checks: map A's, map B's */
static int checked_ok[2];
static struct mcc_precache_snapshot disk;
static void *global_network_game_server_get(void) {return servers ? (void *)&servers : NULL;}
static void error(int kind, char const *format, ...) {(void)kind;(void)format;logs++;}
static int mcc_identity_error(char const *message) {(void)message;errors++;return 0;}
static int mcc_level_name(char const *name) {return name && !strncmp(name,"mcc_maps\\",9);}
static int mcc_package_name(char const *name, char canonical[64]) {
    if (!mcc_level_name(name) || strlen(name)>63) return 0;
    strcpy(canonical,name);return 1;
}
int mcc_precache_snapshot(char const *name, struct mcc_precache_snapshot *out) {(void)name;*out=disk;return 1;}
static int slot(char const *name) {return strstr(name,"\\b") ? 1 : 0;}
static int mcc_validation_lookup(char const *name, unsigned *checksum, unsigned char digest[32]) {
    (void)checksum;
    if (!checked_ok[slot(name)]) return 0;
    if (digest) memcpy(digest,checked[slot(name)],32);
    return 1;
}
static int mcc_validation_request(char const *name, unsigned *checksum, unsigned char digest[32]) {
    requests++;return mcc_validation_lookup(name,checksum,digest) ? 1 : 0;
}
static void mcc_validation_cancel(void) {cancels++;}
static void mcc_cache_validation_clear(void) {}
static void mcc_cache_precache_clear(void) {}
int mcc_workshop_local_item(char const *name, struct mcc_workshop_item *item) {
    (void)name;memset(item,0,sizeof(*item));return 0;
}
void mcc_workshop_wire_pack(char name[128], struct mcc_workshop_item const *item) {(void)name;(void)item;}
int mcc_manifest_metadata_digest(char const *level, unsigned char digest[32]) {(void)level;memset(digest,0,32);return 0;}
''' + host + "\n" + "\n".join(function(source, name) for name in names) + r'''
/* the 13,200-byte record's digest as a joiner reads it: its last 64 bytes' first 32 */
static void sent_digest(char const *map, unsigned char out[32]) {
    static unsigned char record[13200];
    memset(record,0,sizeof(record));
    mcc_identity_host_overlay(map,13200,0,0,13200,record);
    memcpy(out,record+13200-64,32);
}
static int zero(unsigned char const d[32]) {static unsigned char z[32];return !memcmp(d,z,32);}

int main(void) {
    char const *a="mcc_maps\\pkg\\a", *b="mcc_maps\\pkg\\b";
    unsigned char got[32];
    memset(checked[0],0xA1,32);memset(checked[1],0xB2,32);

    /* the host prepares map A and hosts it */
    checked_ok[0]=1;servers=1;
    assert(mcc_identity_prepare_host(a));
    assert(mcc_identity_host_ready(a));
    sent_digest(a,got);assert(!memcmp(got,checked[0],32));

    /* its own client goes (the menus make a new one, the dedicated server's
       lobby): the host's identity stays, and its check is not cancelled */
    mcc_identity_disconnect();
    assert(cancels==0);
    checked_ok[0]=0;   /* (its check's result let go since: the identity itself must have stayed) */
    assert(mcc_identity_host_ready(a));
    checked_ok[0]=1;
    sent_digest(a,got);assert(!zero(got) && !memcmp(got,checked[0],32));

    /* the rotation's next game (or the lobby) moves to map B, not checked yet:
       its settings are held, no package work starts here, nothing goes out
       with zeros for it */
    requests=0;
    assert(!mcc_identity_host_ready(b));
    assert(requests==0);
    sent_digest(b,got);assert(zero(got));   /* (the overlay writes nothing: the record is held) */
    /* the lobby's check finishes: B's identity, taken from it */
    checked_ok[1]=1;
    assert(mcc_identity_host_ready(b));
    sent_digest(b,got);assert(!memcmp(got,checked[1],32));
    /* and back to A for the game after it */
    assert(mcc_identity_host_ready(a));
    sent_digest(a,got);assert(!memcmp(got,checked[0],32));

    /* a changed installation is checked again before its identity is used */
    checked_ok[0]=0;disk.files[0].written_low++;
    assert(!mcc_identity_host_ready(a));
    checked_ok[0]=1;assert(mcc_identity_host_ready(a));

    /* the server goes: its identity goes with it; a client leaving with no
       server cancels the check */
    mcc_identity_host_clear();
    servers=0;mcc_identity_disconnect();assert(cancels==1);
    checked_ok[0]=0;assert(!mcc_identity_host_ready(a));
    /* other maps are never held */
    assert(mcc_identity_host_ready("bloodgulch"));
    assert(errors==0);
    puts("ok");
    return 0;
}
''')
    return compile_tool(tmp_path, [tmp_path / "test.c"])


def test_host_identity_outlives_client_and_follows_map(tmp_path):
    result = subprocess.run([str(build(tmp_path))], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert result.stdout == "ok\n"


def test_settings_wait_for_identity():
    """Every host path's record (split screen, System Link, Create Game, a
    dedicated server's rotation, the next game) is sized by
    network_game_server_settings_layout: it holds an MCC map's record until
    the identity is ready, and the server's dispose lets the identity go."""
    handler = (ROOT / "source/networking/network_server_message_handler.c").read_text(encoding="latin-1")
    layout = function(handler, "network_game_server_settings_layout")
    assert "mcc_identity_host_ready(" in layout
    assert layout.index("mcc_identity_host_ready(") < layout.index("if (slot == NONE)")
    ui = (ROOT / "port/linux/game/mcc_ui_network.inl").read_text()
    assert "mcc_identity_host_clear()" in function(ui, "mcc_ui_network_server_dispose")
    disconnect = function(IDENTITY.read_text(), "mcc_identity_disconnect")
    assert "mcc_host_identity" not in disconnect
