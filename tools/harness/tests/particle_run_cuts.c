/* A fake transparent queue and camera; the production depth list, the cut
of a run of particles, the room it may take, the sprites' sort depth and the
queue's sort below. (From OpenH1's test of the cut, by erin.cena.) */
#include "harness.h"
#include <math.h>
#include <stddef.h>

typedef unsigned short word;
typedef struct { short x, y; } point2d;
typedef struct { real x, y; } real_point2d;
typedef struct { real i, j; } real_vector2d;
typedef struct { real red, green, blue; } real_rgb_color;
typedef struct { real alpha; real_rgb_color rgb; } real_argb_color;
typedef struct { real_vector3d n; real d; } real_plane3d;
#define TAG_STRING_LENGTH 31
#define VALID_INDEX(i, n) ((i) >= 0 && (i) < (n))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define __cdecl
#include "types.inc"

#define MAXIMUM_GROUPS 512
#define MAXIMUM_PARTICLES 256
#define SHORT_MAX_FOR_TEST 32767
#define SHORT_MAX SHORT_MAX_FOR_TEST

static struct transparent_geometry_group storage[MAXIMUM_GROUPS];
static struct transparent_geometry_group *transparent_geometry_groups = storage;
static long transparent_geometry_group_count;
static struct { struct { real_point3d position; real_vector3d forward; } camera; } global_window_parameters;
static struct shader_transparent_generic_definition generic, chicago, water_decal, water;
/* a particle's shader: shader_effect_definition's first fields */
static struct effect { struct shader shader; unsigned short flags; } effect;

static void vector_from_points3d(real_point3d const *a, real_point3d const *b, real_vector3d *result)
{
    result->i = b->x - a->x;
    result->j = b->y - a->y;
    result->k = b->z - a->z;
}

static real dot_product3d(real_vector3d const *a, real_vector3d const *b)
{
    return a->i * b->i + a->j * b->j + a->k * b->k;
}

#define SHADER_GET_TRANSPARENT_GENERIC(s) (&((struct shader_transparent_generic_definition *)(s))->generic)
#define SHADER_GET_TRANSPARENT_CHICAGO(s) SHADER_GET_TRANSPARENT_GENERIC(s)
#define _shader_transparent_draw_before_water_bit _shader_transparent_flag_draw_before_water_bit
#define SHADER_GET_EFFECT(s) ((struct effect const *)(s))

#include "under_test.inc"

/* a particle run, as render_particles hands it to render_particles_cut_run */
static real_point3d positions[MAXIMUM_PARTICLES];
static real particle_depths[MAXIMUM_PARTICLES];
static boolean valid[MAXIMUM_PARTICLES];
static short run_keys[MAXIMUM_PARTICLES];
static short order[MAXIMUM_PARTICLES], part_ends[MAXIMUM_PARTICLES];
static short counts[MAXIMUM_GROUPS + 1];
static real depths[MAXIMUM_GROUPS];
static short particle_part[MAXIMUM_PARTICLES], part_group[MAXIMUM_PARTICLES];
static short sorted[MAXIMUM_GROUPS], sorted_position[MAXIMUM_GROUPS];
static long depth_count;
static short particle_count, part_count, object_count;

static void reset(void)
{
    memset(storage, 0, sizeof(storage));
    transparent_geometry_group_count = 0;
    particle_count = part_count = object_count = 0;
    generic.shader.type = _shader_type_transparent_generic;
    chicago.shader.type = _shader_type_transparent_chicago;
    water_decal.shader.type = _shader_type_transparent_chicago;
    water_decal.generic.flags = FLAG(_shader_transparent_flag_draw_before_water_bit);
    water.shader.type = _shader_type_transparent_water;
    effect.shader.base.type = _shader_type_effect;
    effect.flags = 0;
    global_window_parameters.camera.position = (real_point3d){0, 0, 0};
    global_window_parameters.camera.forward = (real_vector3d){0, 1, 0};
}

/* a group queued before the particles: an object's part, the structure's */
static short queue(void *shader, real distance, unsigned long flags)
{
    struct transparent_geometry_group *group = storage + transparent_geometry_group_count;
    CHECK(transparent_geometry_group_count < MAXIMUM_GROUPS, "room in the queue");
    group->shader = shader;
    group->z_sort = -distance;
    group->geometry_flags = flags;
    group->object_index = 100 + object_count++;
    return (short)transparent_geometry_group_count++;
}

static void particle(real distance, real across)
{
    CHECK(particle_count < MAXIMUM_PARTICLES, "room in the run");
    positions[particle_count] = (real_point3d){across, distance, across * 0.5f};
    valid[particle_count] = TRUE;
    particle_depths[particle_count] = rasterizer_dynamic_unlit_geometry_z_sort(&effect.shader, &positions[particle_count]);
    particle_count++;
}

/* the run cut, each part queued at its centroid as build_sprites_end does */
static void cut(short maximum_part_count)
{
    short part, index = 0, i;
    depth_count = rasterizer_transparent_geometry_sort_depths(depths, MAXIMUM_GROUPS);
    /* (as render_particles keys them: NONE, a particle not drawn) */
    for (i = 0; i < particle_count; i++)
    {
        run_keys[i] = valid[i] ? render_particles_run_key(particle_depths[i], depths, depth_count) : NONE;
    }
    part_count = render_particles_cut_run(run_keys, particle_count, depth_count,
        maximum_part_count, counts, order, part_ends);
    for (part = 0; part < part_count; part++)
    {
        real_point3d centroid = {0, 0, 0};
        short first = index;
        CHECK(part_ends[part] > first, "part %d holds a particle", part);
        for (; index < part_ends[part]; index++)
        {
            CHECK(VALID_INDEX(order[index], particle_count) && valid[order[index]], "order[%d] a valid particle", index);
            particle_part[order[index]] = part;
            centroid.x += positions[order[index]].x;
            centroid.y += positions[order[index]].y;
            centroid.z += positions[order[index]].z;
        }
        centroid.x /= index - first;
        centroid.y /= index - first;
        centroid.z /= index - first;
        part_group[part] = queue(&effect, 0, 0);
        storage[part_group[part]].z_sort = rasterizer_dynamic_unlit_geometry_z_sort(&effect.shader, &centroid);
        storage[part_group[part]].object_index = 0;
    }
}

static void sort(void)
{
    short i;
    for (i = 0; i < transparent_geometry_group_count; i++)
    {
        sorted[i] = i;
    }
    qsort(sorted, transparent_geometry_group_count, sizeof(*sorted), group_sorted_indices_cmpfn);
    for (i = 0; i < transparent_geometry_group_count; i++)
    {
        sorted_position[sorted[i]] = i;
    }
}

static boolean sorts_by_depth(short group)
{
    struct transparent_geometry_group const *g = storage + group;
    return g->shader != (void *)&water && g->shader != (void *)&water_decal &&
        !TEST_FLAG(g->geometry_flags, _rasterizer_geometry_first_person_bit);
}

/* every particle drawn before each group the queue sorts by depth that is
nearer than it, and after each that is farther, as it would be alone */
static void expect_each_particle_as_alone(void)
{
    short p, g;
    sort();
    for (p = 0; p < particle_count; p++)
    {
        if (!valid[p])
        {
            continue;
        }
        for (g = 0; g < transparent_geometry_group_count; g++)
        {
            boolean drawn_before;
            if (storage[g].shader == (void *)&effect || !sorts_by_depth(g) || storage[g].z_sort == particle_depths[p])
            {
                continue;
            }
            drawn_before = sorted_position[part_group[particle_part[p]]] < sorted_position[g];
            CHECK(drawn_before == (particle_depths[p] < storage[g].z_sort),
                "particle %d at %.3f drawn %s the group at %.3f", p, particle_depths[p],
                drawn_before ? "before" : "after", storage[g].z_sort);
        }
    }
}

static unsigned long random_state = 677;

static real random_real(real low, real high)
{
    random_state = random_state * 1103515245UL + 12345UL;
    return low + (high - low) * (real)((random_state >> 8) & 0xFFFF) / 65535.0f;
}

int main(int argc, char **argv)
{
    char const *case_name;
    short i;
    CHECK(argc == 2, "one named case");
    case_name = argv[1];
    reset();

    CASE("mist")
    {
        /* a waterfall drawn before water, a pickup behind the mist in front
        of it, a light nearer than all of it; the mist's particles from 16
        to 29 units away, a little over half of them past the pickup */
        short waterfall = queue(&water_decal, 24.7f, 0);
        short pickup = queue(&generic, 20.0f, 0);
        short light = queue(&chicago, 10.0f, 0);
        real distances[] = {29, 16, 25, 19, 23, 18, 21, 27};
        for (i = 0; i < 8; i++)
        {
            particle(distances[i], (real)(i - 4));
        }
        cut(SHORT_MAX_FOR_TEST);
        CHECK(depth_count == 2 && depths[0] == -20.0f && depths[1] == -10.0f, "the pickup's and the light's depths (%ld)", depth_count);
        CHECK(part_count == 2, "the mist cut once, at the pickup (%d parts)", part_count);
        CHECK(part_ends[0] == 5 && part_ends[1] == 8, "five particles past the pickup, three before it");
        {
            short expected[] = {0, 2, 4, 6, 7, 1, 3, 5};
            for (i = 0; i < 8; i++)
            {
                CHECK(order[i] == expected[i], "order[%d]=%d, expected %d (each part in the run's order)", i, order[i], expected[i]);
            }
        }
        sort();
        CHECK(sorted_position[waterfall] == 0, "the waterfall first, before water");
        CHECK(sorted_position[part_group[0]] < sorted_position[pickup] &&
            sorted_position[pickup] < sorted_position[part_group[1]] &&
            sorted_position[part_group[1]] < sorted_position[light], "far mist, pickup, near mist, light");
        expect_each_particle_as_alone();
        return 0;
    }
    CASE("depths")
    {
        /* only the groups the sort puts in order of depth, ascending, once */
        queue(&water, 5.0f, 0);
        queue(&water_decal, 6.0f, 0);
        queue(&generic, 7.0f, FLAG(_rasterizer_geometry_first_person_bit));
        storage[queue(&generic, 8.0f, 0)].cortana_hack = TRUE;
        queue(&generic, 9.0f, 0);
        queue(&chicago, 3.0f, 0);
        queue(&generic, 9.0f, 0);
        queue(NULL, 4.0f, 0);
        depth_count = rasterizer_transparent_geometry_sort_depths(depths, MAXIMUM_GROUPS);
        CHECK(depth_count == 3, "three depths, not %ld", depth_count);
        CHECK(depths[0] == -9.0f && depths[1] == -4.0f && depths[2] == -3.0f, "ascending and once: %g %g %g",
            depths[0], depths[1], depths[2]);
        return 0;
    }
    CASE("whole")
    {
        /* nothing queued among the particles: one part, in the run's order,
        as the Xbox built it */
        queue(&generic, 5.0f, 0);
        queue(&generic, 40.0f, 0);
        for (i = 0; i < 6; i++)
        {
            particle(10.0f + 3 * ((i * 7) % 6), 0);
        }
        cut(SHORT_MAX_FOR_TEST);
        CHECK(part_count == 1 && part_ends[0] == 6, "one part of six");
        for (i = 0; i < 6; i++)
        {
            CHECK(order[i] == i, "the run's order");
        }
        expect_each_particle_as_alone();
        return 0;
    }
    CASE("budget")
    {
        /* more parts than the queue's room allows: the run whole */
        queue(&generic, 20.0f, 0);
        particle(25, 0);
        particle(15, 0);
        particle(26, 0);
        cut(1);
        CHECK(part_count == 1 && part_ends[0] == 3, "one part of three");
        CHECK(order[0] == 0 && order[1] == 1 && order[2] == 2, "the run's order");
        return 0;
    }
    CASE("invalid")
    {
        /* a particle whose object is gone is in no part */
        queue(&generic, 20.0f, 0);
        particle(25, 0);
        particle(15, 0);
        particle(26, 0);
        particle(14, 0);
        valid[1] = FALSE;
        cut(SHORT_MAX_FOR_TEST);
        CHECK(part_count == 2 && part_ends[0] == 2 && part_ends[1] == 3, "two parts of the three valid particles");
        CHECK(order[0] == 0 && order[1] == 2 && order[2] == 3, "particle 1 left out");
        expect_each_particle_as_alone();
        return 0;
    }
    CASE("bias")
    {
        /* a shader biased to sort nearer: its particles cut, and its parts
        sorted, at the biased depth */
        effect.flags = FLAG(_shader_effect_sort_bias_bit);
        queue(&generic, 20.0f, 0);
        particle(20.1f, 0);
        particle(30.0f, 0);
        CHECK(fabsf(particle_depths[0] - (-20.1f + SHADER_EFFECT_SORT_BIAS)) < 0.0001f, "the bias in the particle's depth");
        cut(SHORT_MAX_FOR_TEST);
        CHECK(part_count == 2 && part_ends[0] == 1 && order[0] == 1, "the biased particle before the object's depth");
        expect_each_particle_as_alone();
        return 0;
    }
    CASE("random")
    {
        short scene;
        for (scene = 0; scene < 300; scene++)
        {
            short objects = (short)random_real(0, 40), particles = (short)random_real(1, 120);
            reset();
            effect.flags = random_real(0, 1) < 0.5f ? FLAG(_shader_effect_sort_bias_bit) : 0;
            global_window_parameters.camera.position = (real_point3d){random_real(-5, 5), random_real(-5, 5), random_real(-5, 5)};
            global_window_parameters.camera.forward = (real_vector3d){0.6f, 0.8f, 0};
            for (i = 0; i < objects; i++)
            {
                real r = random_real(0, 1);
                queue(r < 0.1f ? (void *)&water_decal : r < 0.2f ? (void *)&water : (void *)&generic, random_real(1, 60),
                    random_real(0, 1) < 0.1f ? FLAG(_rasterizer_geometry_first_person_bit) : 0);
            }
            for (i = 0; i < particles; i++)
            {
                particle(random_real(1, 60), random_real(-10, 10));
                valid[i] = random_real(0, 1) < 0.95f;
            }
            cut(SHORT_MAX_FOR_TEST);
            expect_each_particle_as_alone();
        }
        return 0;
    }
    CASE("room")
    {
        /* the parts a run may be cut into: the fewer of the queue's free
        groups and the free vertex buffers, less the run whole and the later
        runs whole, within the frame's budget, a part charged its pages */
        CHECK(render_particles_maximum_part_count(2000, 1000, 1, 100, 32767) == 900,
            "the vertex buffers, fewer than the groups (%d)", render_particles_maximum_part_count(2000, 1000, 1, 100, 32767));
        CHECK(render_particles_maximum_part_count(1000, 2000, 1, 100, 32767) == 900, "the groups, fewer");
        CHECK(render_particles_maximum_part_count(1000, 1000, 4, 96, 40) == 11,
            "a part of four pages charged four (%d)", render_particles_maximum_part_count(1000, 1000, 4, 96, 40));
        CHECK(render_particles_maximum_part_count(1000, 1000, 1, 0, 10) == 11, "the frame's budget");
        CHECK(render_particles_maximum_part_count(100, 100, 1, 99, 50) == 1, "no room past the whole runs: the run whole");
        CHECK(render_particles_maximum_part_count(0, 0, 1, 0, 50) == 1, "a full queue: the run whole");
        CHECK(render_particles_maximum_part_count(1000, 1000, 2, 0, 3) == 2, "a budget short of a second part's pages");
        CHECK(render_particles_maximum_part_count(1000, 1000, 2, 0, 1) == 1, "a budget short of any part's pages");
        return 0;
    }
    fprintf(stderr, "no case %s\n", case_name);
    return 2;
}
