"""MCC menu admission reuses only successful, unchanged package checksums."""
from pathlib import Path
import re
import subprocess
from harness import function
from test_mcc_identity import compile_tool

ROOT = Path(__file__).resolve().parents[1]


def test_menu_validation_cache(tmp_path):
    source = (ROOT / "port/linux/game/mcc_cache.c").read_text()
    state = re.search(r"static struct \{.*?\} mcc_validation;", source, re.S)
    declarations = state.group()
    names = ["mcc_cache_validation_clear", "mcc_cache_probe"]
    helpers = "\n".join(function(source, name) for name in names)
    (tmp_path / "cseries.h").write_text("typedef int boolean;\ntypedef unsigned char byte;\n#define TRUE 1\n#define FALSE 0\n")
    test = r'''
#include "cseries.h"
#include "mcc_precache.h"
#include "mcc_cache_format.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
static int probes, valid=1, stamped=1, changing, prepares, identity_ok=1;
static struct mcc_precache_snapshot files;
int mcc_precache_snapshot(char const *name, struct mcc_precache_snapshot *out) {
    (void)name; *out=files; return stamped;
}
static char const *mcc_identity_local_name(char const *name) {return name;}
static int mcc_identity_prepare_host(char const *name) {(void)name; prepares++;return identity_ok;}
static int mcc_identity_check(char const *name) {(void)name;return identity_ok;}
static FILE *mcc_open(char const *name, struct mcc_cache_source *s, struct mcc_cache_identity *i) {
    (void)s;(void)i;return name && !strncmp(name,"mcc_maps/",9) ? (FILE *)1 : NULL;
}
static int close_stub(FILE *f) {(void)f;return 0;}
#define fclose close_stub
static int mcc_resources_probe(char const *name,struct mcc_cache_source const *s,uint32_t *checksum) {
    (void)name;(void)s;probes++;*checksum=123;
    if(changing)files.files[0].written_low++;
    return valid;
}
''' + declarations + "\n" + helpers + "\n" + "\n".join(function(source, name) for name in
        ["mcc_cache_checksum", "mcc_cache_present"]) + r'''
int main(void) {
    char message[256];int i,old;
    char const *name="mcc_maps/package/a10";
    assert(mcc_cache_checksum(name)==123);assert(probes==1 && prepares==1);
    for(i=0;i<100;i++)assert(mcc_cache_present(name,123,message,sizeof(message)));
    assert(probes==1);
    assert(!mcc_cache_present(name,124,message,sizeof(message)));assert(probes==1);
    assert(mcc_cache_checksum(name)==123);assert(probes==1 && prepares==2);
    identity_ok=0;assert(!mcc_cache_present(name,123,message,sizeof(message)));
    assert(!mcc_cache_checksum(name));identity_ok=1;
    for(i=0;i<MCC_PRECACHE_FILES;i++) {
        old=probes;files.files[i].present++;
        assert(mcc_cache_present(name,123,message,sizeof(message)));assert(probes==old+1);
        files.files[i].written_low++;
        assert(mcc_cache_present(name,123,message,sizeof(message)));assert(probes==old+2);
    }
    files.files[0].written_low++;valid=0;old=probes;
    assert(!mcc_cache_present(name,123,message,sizeof(message)));
    assert(!mcc_cache_present(name,123,message,sizeof(message)));assert(probes==old+2);
    valid=1;assert(mcc_cache_checksum(name)==123);
    stamped=0;old=probes;
    assert(mcc_cache_present(name,123,message,sizeof(message)));
    assert(mcc_cache_present(name,123,message,sizeof(message)));assert(probes==old+2);
    stamped=1;changing=1;
    assert(!mcc_cache_present(name,123,message,sizeof(message)));
    changing=0;assert(mcc_cache_present(name,123,message,sizeof(message)));
    old=probes;assert(mcc_cache_checksum("mcc_maps/other/a10")==123);assert(probes==old+1);
    assert(mcc_cache_checksum(name)==123);assert(probes==old+2);
    mcc_cache_validation_clear();
    old=probes;assert(mcc_cache_checksum(name)==123);assert(probes==old+1);
    return 0;
}
'''
    path = tmp_path / "validation.c"
    path.write_text(test)
    binary = compile_tool(tmp_path, [path])
    subprocess.run([str(binary)], check=True, capture_output=True)
