"""Exercise the real MCC progress controller with a recording device backend."""
from pathlib import Path
import re
import subprocess

import pytest
from test_mcc_pause_runtime import _compile

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def loading_tool(tmp_path_factory):
    work = tmp_path_factory.mktemp("mcc-loading")
    implementation = (ROOT / "port/linux/game/mcc_loading.c").read_text()
    implementation = re.sub(r'^#include [<"][^\n]+[>"]\s*$', '', implementation, flags=re.M)
    source = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "mcc_loading.h"
typedef float real;
typedef uint32_t DWORD;
typedef int IDirect3DSurface8;
typedef struct {DWORD X,Y,Width,Height;float MinZ,MaxZ;} D3DVIEWPORT8;
typedef struct {DWORD Width,Height;} D3DSURFACE_DESC;
#define FALSE 0
#define _error_silent 0
static void error(int level,char const *format,...) {(void)level;(void)format;}
static int device, surface, null_renderer, native_active, starts, ends, frames, releases;
static int failure, reenter;
static DWORD clock_ms;
static real drawn;
static void *global_d3d_device=&device;
static DWORD GetTickCount(void) {return clock_ms;}
static int config_boolean(char const *name) {(void)name;return null_renderer;}
static int mcc_level_name(char const *name) {return name && !strncmp(name,"mcc_maps/",9);}
static int progress_bar_is_active(void) {return native_active;}
static void progress_bar_eachframe(void) { }
static void progress_bar_begin(int skip) {(void)skip;starts++;native_active=1;}
static void progress_bar_end(void) {ends++;native_active=0;}
static void progress_bar_display(real value) {
    assert(value>=0 && value<=1);assert(value>=drawn);drawn=value;
    if(reenter){clock_ms+=100;mcc_loading_pulse();}
}
static int IDirect3DDevice8_GetBackBuffer(void *d,int a,int b,IDirect3DSurface8 **s) {
    (void)d;(void)a;(void)b;if(failure==1)return -1;if(failure==2)return 0;*s=&surface;return 0;
}
static int IDirect3DSurface8_GetDesc(IDirect3DSurface8 *s,D3DSURFACE_DESC *desc) {
    assert(s==&surface);desc->Width=1280;desc->Height=720;return failure==3 ? -1 : 0;
}
static void IDirect3DSurface8_Release(IDirect3DSurface8 *s) {assert(s==&surface);releases++;}
static void IDirect3DDevice8_SetViewport(void *d,D3DVIEWPORT8 *v) {
    (void)d;assert(v->Width==1280 && v->Height==720 && v->MaxZ==1);
}
static void IDirect3DDevice8_BeginScene(void *d) {(void)d;}
static void IDirect3DDevice8_EndScene(void *d) {(void)d;}
static void IDirect3DDevice8_Present(void *d,void *a,void *b,void *c,void *e) {
    (void)d;(void)a;(void)b;(void)c;(void)e;frames++;
}
''' + implementation + r'''
int main(int argc,char **argv) {
    int test=argc>1 ? atoi(argv[1]) : -1, before;
    if(test<4 || test==6 || test==7) {
        if(test==1)null_renderer=1;
        if(test==2)global_d3d_device=NULL;
        if(test==3)native_active=1;
        mcc_loading_begin(test==0 ? "maps/a10" : test==6 ? "custom_maps/a10" : test==7 ? NULL : "mcc_maps/a10");
        mcc_loading_pulse();mcc_loading_stage(MCC_LOADING_AUDIO);mcc_loading_end(1);
        assert(!starts && !frames && !ends);assert(native_active==(test==3));return 0;
    }
    if(test==4)clock_ms=UINT32_MAX-20;
    if(test>=8 && test<=10)failure=test-7;
    if(test==11)reenter=1;
    mcc_loading_begin("mcc_maps/a10");assert(starts==1);
    mcc_loading_begin("mcc_maps/b30");assert(starts==1);
    if(failure) {
        assert(!frames);assert(releases==(failure==3));
        failure=0;clock_ms+=50;mcc_loading_pulse();assert(frames==1);
    }
    before=frames;mcc_loading_pulse();assert(frames==before);
    clock_ms+=50;mcc_loading_stage(MCC_LOADING_AUDIO);assert(frames==before+1);
    clock_ms+=50;mcc_loading_update(MCC_LOADING_AUDIO,5,10);assert(drawn>0.3f && drawn<0.55f);
    before=frames;clock_ms+=50;
    mcc_loading_update(MCC_LOADING_AUDIO,11,10);
    mcc_loading_update(MCC_LOADING_AUDIO,0,0);
    mcc_loading_update((enum mcc_loading_stage)-1,1,1);
    mcc_loading_update(MCC_LOADING_STAGES,1,1);
    mcc_loading_stage((enum mcc_loading_stage)-1);mcc_loading_stage(MCC_LOADING_STAGES);
    mcc_loading_stage(MCC_LOADING_TAGS);assert(frames==before);
    mcc_loading_update(MCC_LOADING_AUDIO,1,10);assert(drawn>0.3f);
    mcc_loading_end(test!=5);assert(ends==1 && !native_active && !mcc_loading.active);
    assert(test==5 ? drawn<1 : drawn==1);
    before=frames;clock_ms+=100;mcc_loading_pulse();mcc_loading_end(1);assert(frames==before && ends==1);
    drawn=0;mcc_loading_begin("mcc_maps/b30");assert(starts==2);mcc_loading_end(0);assert(ends==2);
    return 0;
}
'''
    path = work / "loading.c"
    path.write_text(source)
    output = work / "loading.exe"
    _compile(path, output)
    return output


@pytest.mark.parametrize("case", range(12))
def test_loading_lifetime_progress_and_device_failures(loading_tool, case):
    subprocess.run([str(loading_tool), str(case)], check=True, capture_output=True)


def test_loading_wraps_scenario_load_without_normal_frame_callbacks():
    from harness import function
    load = function((ROOT / "source/game/game.c").read_text(), "game_load")
    assert load.index("mcc_loading_begin") < load.index("scenario_load(") < load.index("mcc_loading_end")
    controller = (ROOT / "port/linux/game/mcc_loading.c").read_text()
    for unsafe in ["main_loop(", "main_pregame_render(", "render_frame_pregame(", "network_game_update("]:
        assert unsafe not in controller
