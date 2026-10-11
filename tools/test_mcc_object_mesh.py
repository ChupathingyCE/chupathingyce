"""Exercise upstream mesh extraction with MCC palettes and native node indices."""
from pathlib import Path
import shutil
import subprocess
import sys

import pytest
from harness import function

ROOT = Path(__file__).resolve().parents[1]


def test_mesh_node_palettes(tmp_path):
    compiler = shutil.which("clang")
    if not compiler:
        pytest.skip("clang required")
    code = function((ROOT / "port/linux/game/object_mesh.c").read_text(), "object_mesh_add_part")
    source = tmp_path / "mesh.c"
    source.write_text(r'''
#include <stdlib.h>
#include <string.h>
typedef unsigned char byte, BYTE;
typedef int boolean;
typedef float real;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define NULL ((void *)0)
#define VALID_INDEX(n,c) ((n)>=0 && (n)<(c))
#define TEST_FLAG(v,b) ((v)&(1u<<(b)))
#define PIN(v,a,b) ((v)<(a)?(a):(v)>(b)?(b):(v))
#define TAG_BLOCK_GET_ELEMENT(b,i,t) (((t *)(b)->address)+(i))
#define OBJECT_MESH_MAXIMUM_VERTICES 16
#define OBJECT_MESH_MAXIMUM_TRIANGLES 16
#define D3DLOCK_READONLY 0
enum {_object_mesh_part_stripped_bit, _object_mesh_part_local_nodes_bit};
enum {_rasterizer_vertex_type_model_compressed, _rasterizer_vertex_type_model_uncompressed};
struct point {real x,y,z;};
struct model_vertex_compressed {struct point position;short nodes[2],node_weight;};
struct model_vertex_uncompressed {struct point position;short nodes[2];real node_weights[2];};
struct vertex_buffer {short type;long count;void *hardware_format;};
struct triangle_buffer {short type;long count;void *hardware_format;};
struct object_mesh_vertex {struct point position;byte nodes[2];short weight;};
struct object_mesh {long vertex_count,triangle_count;short node_count;struct object_mesh_vertex vertices[16];unsigned short triangles[16][3];};
struct object_mesh_model_geometry_part {unsigned flags;short shader_index,centroid_primary_node_index;struct vertex_buffer vertex_buffer;struct triangle_buffer triangle_buffer;};
struct object_mesh_model_shader_reference {struct {long index;} shader;};
struct model {struct {long count;void *address;} shaders;};
struct shader {struct {short type;} base;};
typedef void IDirect3DVertexBuffer8,IDirect3DIndexBuffer8;
static int palette_enabled;
static byte palette[2]={40,7};
static struct shader shader;
static short mcc_cache_part_palette(void const *buffer,byte const **nodes) {(void)buffer;*nodes=palette;return palette_enabled?2:0;}
static struct shader *shader_definition_get(long index) {(void)index;return &shader;}
static int shader_type_is_valid_for_model(short type) {(void)type;return 1;}
static int shader_type_is_transparent(short type) {(void)type;return 0;}
static int valid_real(real n) {return n==n && n<1e30f && n>-1e30f;}
static void IDirect3DVertexBuffer8_Lock(void *p,int a,int b,byte **out,int f) {(void)a;(void)b;(void)f;*out=p;}
static void IDirect3DIndexBuffer8_Lock(void *p,int a,int b,byte **out,int f) {IDirect3DVertexBuffer8_Lock(p,a,b,out,f);}
static void IDirect3DVertexBuffer8_Unlock(void *p) {(void)p;}
static void IDirect3DIndexBuffer8_Unlock(void *p) {(void)p;}
''' + code + r'''
int main(void) {
    struct object_mesh mesh;
    struct model_vertex_compressed vertices[3]={{{0,0,0},{0,3},20000},{{1,0,0},{3,0},20000},{{0,1,0},{0,3},20000}};
    unsigned short indices[3]={0,1,2};
    struct object_mesh_model_shader_reference ref={{1}};
    struct model model={{1,&ref}};
    struct object_mesh_model_geometry_part part={0,0,0,{0,3,vertices},{1,1,indices}};
    memset(&mesh,0,sizeof(mesh));mesh.node_count=64;
    if(!object_mesh_add_part(&mesh,&model,&part)||mesh.vertices[0].nodes[0]!=0||mesh.vertices[0].nodes[1]!=1) return 1;
    memset(&mesh,0,sizeof(mesh));mesh.node_count=64;palette_enabled=1;
    if(!object_mesh_add_part(&mesh,&model,&part)||mesh.vertices[0].nodes[0]!=40||mesh.vertices[0].nodes[1]!=7||mesh.triangle_count!=1) return 2;
    memset(&mesh,0,sizeof(mesh));mesh.node_count=64;vertices[0].nodes[1]=6;
    if(!object_mesh_add_part(&mesh,&model,&part)||mesh.vertices[0].nodes[1]!=40||mesh.vertices[0].weight!=32767) return 3;
    return 0;
}
''')
    binary = tmp_path / ("mesh.exe" if sys.platform == "win32" else "mesh")
    target = ["--target=i686-pc-windows-msvc", "-fuse-ld=lld"] if sys.platform == "win32" else []
    result = subprocess.run([compiler] + target + [str(source), "-o", str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert subprocess.run([str(binary)], timeout=10).returncode == 0
