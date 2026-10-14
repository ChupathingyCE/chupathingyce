#include "extensions/extension_api.h"
#include <assert.h>

union real_point3d { real n[3]; };
union real_vector3d { real n[3]; };
struct collision_result { real t; };
struct collision_feature_list { int count; };

static int render_count;
static void draw(void) { render_count++; }
static boolean contains(union real_point3d const *point) { return point->n[0] == 1; }
static void features(union real_point3d const *center, real radius, real height,
    real width, struct collision_feature_list *list)
{
    assert(center->n[0] == 1 && radius == 2 && height == 3 && width == 4);
    list->count++;
}
static boolean far_hit(unsigned long flags, union real_point3d const *point,
    union real_vector3d const *vector, real radius, struct collision_result *result)
{
    assert(flags == 123 && point->n[0] == 1 && vector->n[2] == -1 && radius == 0.5f);
    if (result->t <= 0.6f) return FALSE;
    result->t = 0.6f;
    return TRUE;
}
static boolean near_hit(unsigned long flags, union real_point3d const *point,
    union real_vector3d const *vector, real radius, struct collision_result *result)
{
    assert(flags == 123 && point->n[0] == 1 && vector->n[2] == -1 && radius == 0.5f);
    if (result->t <= 0.2f) return FALSE;
    result->t = 0.2f;
    return TRUE;
}
static struct halo_world_geometry const far_geometry = {
    .render = draw, .test_point = contains, .test_vector = far_hit, .get_features = features,
};
static struct halo_world_geometry const near_geometry = {
    .render = draw, .test_vector = near_hit, .get_features = features,
};
struct halo_extension const first_extension = { .name = "first", .world_geometry = &far_geometry };
struct halo_extension const second_extension = { .name = "second", .world_geometry = &near_geometry };

int main(void)
{
    union real_point3d point = {{1, 0, 0}};
    union real_vector3d vector = {{0, 0, -1}};
    struct collision_result result = {1};
    struct collision_feature_list list = {7};
    halo_extensions_world_render();
    assert(render_count == 2);
    assert(halo_extensions_world_test_point(&point));
    point.n[0] = 0;
    assert(!halo_extensions_world_test_point(&point));
    point.n[0] = 1;
    assert(halo_extensions_world_test_vector(123, &point, &vector, 0.5f, &result));
    assert(result.t == 0.2f);
    result.t = 0.1f;
    assert(!halo_extensions_world_test_vector(123, &point, &vector, 0.5f, &result));
    assert(result.t == 0.1f); /* preserve nearer vanilla hit */
    halo_extensions_world_get_features(&point, 2, 3, 4, &list);
    assert(list.count == 9); /* append, do not erase engine features */
    return 0;
}
