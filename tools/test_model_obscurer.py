"""The production model debug query must not affect ordinary rendering."""
from pathlib import Path
import subprocess

from test_load_trace import build

ROOT = Path(__file__).resolve().parents[1]


def test_model_obscurer_only_queries_for_debug_selection(tmp_path):
    text = (ROOT / "source/rasterizer/rasterizer.c").read_text()
    function = text[text.index("void rasterizer_models_begin("):text.index("void rasterizer_debug_immediate_point(")]
    source = r'''
#include <assert.h>
typedef int boolean;
typedef struct { float i, j, k; } real_vector3d;
struct collision_result { int object_index; };
static struct { struct { real_vector3d position, forward; } camera; } global_window_parameters;
static struct { int local_player_index; } render;
static int rasterizer_model_obscurer_object_index;
static int rasterizer_debug_model_vertices_enabled;
static int global_current_collision_user_depth = 2;
static int global_current_collision_users[8];
static int calls, hit, backend_calls, backend_flag;
#define MAXIMUM_COLLISION_USER_STACK_DEPTH 8
#define NONE (-1)
#define match_assert(file, line, condition) assert(condition)
static int collision_test_vector(int flags, const real_vector3d *position,
    const real_vector3d *vector, int player, struct collision_result *result)
{
    assert(flags == 0xFFF80 && position == &global_window_parameters.camera.position);
    assert(vector->i == 10000.f && vector->j == 20000.f && vector->k == 30000.f);
    assert(player == 9 && global_current_collision_user_depth == 3);
    assert(global_current_collision_users[2] == 21);
    calls++;
    result->object_index = 42;
    return hit;
}
static void _rasterizer_models_begin(boolean flag)
{
    backend_calls++;
    backend_flag = flag;
    assert(global_current_collision_user_depth == 2);
}
''' + function + r'''
int main(void)
{
    global_window_parameters.camera.forward.i = 1.f;
    global_window_parameters.camera.forward.j = 2.f;
    global_window_parameters.camera.forward.k = 3.f;
    render.local_player_index = 9;
    rasterizer_model_obscurer_object_index = 17;
    rasterizer_models_begin(0);
    assert(calls == 0 && backend_calls == 1 && backend_flag == 0);
    assert(rasterizer_model_obscurer_object_index == NONE);
    rasterizer_debug_model_vertices_enabled = 1;
    hit = 1;
    rasterizer_models_begin(0);
    assert(calls == 1 && backend_calls == 2 && backend_flag == 0);
    assert(rasterizer_model_obscurer_object_index == 42);
    hit = 0;
    rasterizer_models_begin(0);
    assert(calls == 2 && backend_calls == 3 && backend_flag == 0);
    assert(rasterizer_model_obscurer_object_index == 42); /* original miss semantics */
    rasterizer_models_begin(1);
    assert(calls == 2 && backend_calls == 4 && backend_flag == 1);
    assert(rasterizer_model_obscurer_object_index == NONE);
    rasterizer_debug_model_vertices_enabled = 0;
    rasterizer_models_begin(1);
    assert(calls == 2 && backend_calls == 5 && backend_flag == 1);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)
