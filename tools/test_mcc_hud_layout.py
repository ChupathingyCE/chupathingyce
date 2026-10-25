"""Bounded stock-layout recognition using the production MCC visitor, no map assets."""
from pathlib import Path
import shutil
import subprocess
import sys

import pytest
from harness import function

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def layout_tool(tmp_path_factory):
    compiler = shutil.which("clang")
    if not compiler:
        pytest.skip("clang is needed")
    work = tmp_path_factory.mktemp("mcc-hud-layout")
    source = (ROOT / "port/linux/game/mcc_hud.c").read_text()
    code = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define NUMBEROF(x) (sizeof(x)/sizeof((x)[0]))
struct bitmap_group {short type,format,usage;unsigned short flags;char rest[100];};
struct mcc_runtime {unsigned char *tag_index;struct {uint32_t tag_count;} report;unsigned used;unsigned char *tags;};
static unsigned char arena[8192];
static void *mcc_runtime_pointer(struct mcc_runtime *r,uint32_t p,uint32_t bytes) {
    (void)r;return p<=sizeof(arena)&&bytes<=sizeof(arena)-p ? arena+p : NULL;
}
''' + function(source, "mcc_hud_word") + '\n' + function(source, "mcc_hud_block") + '\n'
    code += (ROOT / "port/linux/game/mcc_hud_layout.inl").read_text() + r'''
static void word(unsigned at,uint32_t value) {memcpy(arena+at,&value,4);}
int main(int argc,char **argv) {
    struct mcc_runtime r={arena,{2},sizeof(arena),arena};
    uint32_t entry[8]={'wphi',0,0,0,0,256,0,0};
    uint64_t baseline,modified;int test;
    unsigned p=256,e=1024,b=2048;
    short mark=0x4d53,stored;
    if(argc!=2)return 1;test=atoi(argv[1]);
    /* One corner static element, one sprite bitmap and empty other blocks. */
    word(p+0x60,1);word(p+0x64,e);
    word(e+0x24+0x30,0xe0000001);
    word(32,'bitm');word(32+12,0xe0000001);word(32+16,4000);word(32+20,b);
    memcpy(arena+4000,"test\\meter",11);
    word(b+0x60,1);word(b+0x64,3000);
    word(b+0x54,1);word(b+0x58,3100);
    word(3100+0x34,1);word(3100+0x38,3200);
    baseline=mcc_hud_layout_visit(&r,entry,0);
    if(!baseline || mcc_hud_layout_stock(baseline))return 2;
    if(test==0) { /* Colors, padding and crosshairs do not select the corner frame. */
        word(e+0x24+0x34,0xff123456);word(p+0x84,999);word(p+0x88,0xffffffff);
        word(e+0x24+0x5c,0xffffffff); /* empty overlay pointer is not layout */
        memcpy(arena+e+0x24+14,&mark,2);
    } else if(test==1)word(e+0x24,20); /* placement */
    else if(test==2)word(e+4,0x10000); /* child anchor */
    else if(test==3)word(e+0x24+0x54,2); /* sequence */
    else if(test==4)word(3000+4,64); /* dimensions */
    else if(test==5)word(3200+8,0x3f800000); /* sprite bounds */
    else if(test==6)arena[4000]='X'; /* bitmap identity */
    else if(test==7)word(p+0x64,8190); /* truncated element */
    else if(test==8)word(b+0x60,0xffffffff); /* block overflow */
    else if(test==9)word(e+0x24+0x30,0xe0010001); /* stale bitmap handle */
    else if(test==10)word(32+16,0xffffffff); /* invalid name */
    else if(test==11)word(3100+0x38,8190); /* truncated sprite */
    else if(test==12) {word(32+16,4096);memset(arena+4096,'x',256);} /* unterminated name */
    else if(test==13)word(3000+16,7); /* bitmap registration point */
    else if(test==14)word(e+0x24+0x58,1); /* authored overlay count */
    modified=mcc_hud_layout_visit(&r,entry,0);
    if(test==0 && modified!=baseline)return 3;
    if(test>=1 && test<=6 && (!modified || modified==baseline))return 4;
    if(test>=7 && test<=12 && modified)return 5;
    if(test>=13 && (!modified || modified==baseline))return 11;
    memcpy(&stored,arena+p+0x3e,2);if(stored)return 6;
    if(test==0) {
        memcpy(&stored,arena+e+0x24+14,2);if(stored)return 7;
        if(mcc_hud_layout_visit(&r,entry,mark)!=baseline)return 8;
        memcpy(&stored,arena+e+0x24+14,2);if(stored!=mark)return 9;
        memcpy(&stored,arena+p+0x3e,2);if(stored!=mark)return 10;
    }
    return 0;
}
'''
    path = work / "layout.c"
    path.write_text(code)
    binary = work / "layout.exe"
    target = ["--target=i686-pc-windows-msvc", "-fuse-ld=lld"] if sys.platform == "win32" else ["-m32"]
    result = subprocess.run([compiler, *target, "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                             "-Wno-multichar", str(path), "-o", str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return binary


@pytest.mark.parametrize("case", range(15))
def test_layout_identity_bounds_and_owned_markers(layout_tool, case):
    result = subprocess.run([str(layout_tool), str(case)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
