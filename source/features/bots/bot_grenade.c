/* Constant-air-gravity CE tag adapter; no bounce, water or guided prediction.
 * Each proposal/validation performs at most sixteen inflated capsule chords. */
#include "cseries.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "items/projectiles.h"
#include "items/projectile_definitions.h"
#include "game/game_globals.h"
#include "scenario/scenario.h"
#include "physics/physics.h"
#include "physics/collisions.h"
#include "bot_grenade.h"
#include "bot_ballistics.h"

struct bot_grenade_parameters
{
    real speed;
    real gravity;
    real radius;
    real offset[3];
};

static boolean bot_grenade_parameters(struct bot_grenade_request const *request,
    struct bot_grenade_parameters *parameters)
{
    struct unit_datum *unit = unit_try_and_get(request->unit_key);
    struct game_globals *globals = scenario_get_game_globals();
    struct unit_definition *definition;
    struct game_globals_grenade *grenade;
    struct projectile_definition *projectile;
    struct game_globals_player_information *player;
    short type;
    short index;

    if (request->target_unit_key == NONE || !unit_try_and_get(request->target_unit_key) ||
        !unit || unit->object.type != _object_type_biped ||
        TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
        unit->object.parent_object_index != NONE ||
        TEST_FLAG(unit->object.flags, _object_wholly_under_media_bit) || !globals)
        return FALSE;
    type = unit->unit.current_grenade_index;
    if (type < 0 || type >= NUMBER_OF_UNIT_GRENADE_TYPES || type >= globals->grenades.count ||
        unit->unit.grenade_counts[type] <= 0 || globals->player_information.count <= 0)
        return FALSE;
    grenade = TAG_BLOCK_GET_ELEMENT(&globals->grenades, type, struct game_globals_grenade);
    if (grenade->projectile.index == NONE)
        return FALSE;
    projectile = projectile_definition_get(grenade->projectile.index);
    definition = unit_definition_get(unit->definition_index);
    player = TAG_BLOCK_GET_ELEMENT(&globals->player_information, 0,
        struct game_globals_player_information);
    if (projectile->projectile.guided_angular_velocity != 0.f ||
        !valid_real(projectile->projectile.collision_radius) ||
        projectile->projectile.collision_radius < 0.f ||
        projectile->projectile.collision_radius > 1.f ||
        !valid_real(projectile->projectile.initial_velocity) ||
        !valid_real(projectile->projectile.final_velocity) ||
        projectile->projectile.initial_velocity != projectile->projectile.final_velocity)
        return FALSE;
    parameters->speed = definition->unit.grenade_velocity / TICKS_PER_SECOND;
    parameters->gravity = global_gravity * projectile->projectile.air_gravity_scale;
    parameters->radius = MAX(0.03f, projectile->projectile.collision_radius);
    parameters->offset[0] = player->grenade_origin.i;
    parameters->offset[1] = player->grenade_origin.j;
    parameters->offset[2] = player->grenade_origin.k;
    for (index = 0; index < 3; index++)
        if (!valid_real(parameters->offset[index]) || fabsf(parameters->offset[index]) > 2.f)
            return FALSE;
    return TRUE;
}

static boolean bot_grenade_origin(struct bot_grenade_request const *request,
    struct bot_grenade_parameters const *parameters, float const facing[2], float origin[3])
{
    real forward[3];
    real side[3];
    real up[3];
    short index;
    if (!valid_real(facing[0]) || !valid_real(facing[1]) ||
        fabsf(facing[0]) > 6.3f || fabsf(facing[1]) > 1.4f)
        return FALSE;
    forward[0] = cosf(facing[1]) * cosf(facing[0]);
    forward[1] = cosf(facing[1]) * sinf(facing[0]);
    forward[2] = sinf(facing[1]);
    side[0] = -sinf(facing[0]); side[1] = cosf(facing[0]); side[2] = 0.f;
    up[0] = -sinf(facing[1]) * cosf(facing[0]);
    up[1] = -sinf(facing[1]) * sinf(facing[0]); up[2] = cosf(facing[1]);
    for (index = 0; index < 3; index++)
        origin[index] = request->eye[index] + parameters->offset[0] * forward[index] +
            parameters->offset[1] * side[index] + parameters->offset[2] * up[index];
    return TRUE;
}

static boolean bot_grenade_trace(struct bot_grenade_request const *request,
    struct bot_grenade_parameters const *parameters, struct bot_arc const *arc)
{
    real_point3d previous;
    real radius = parameters->radius + bot_arc_chord_margin(arc);
    uint32_t segment;
    if (!valid_real(radius) || radius > 2.f)
        return FALSE;
    previous.x = arc->origin[0]; previous.y = arc->origin[1]; previous.z = arc->origin[2];
    for (segment = 1; segment <= BOT_ARC_SEGMENTS; segment++)
    {
        float point[3];
        real_vector3d delta;
        real_point3d next;
        struct collision_result collision;
        if (!bot_arc_point(arc, (float)segment / BOT_ARC_SEGMENTS, point))
            return FALSE;
        next.x = point[0]; next.y = point[1]; next.z = point[2];
        delta.i = next.x - previous.x; delta.j = next.y - previous.y; delta.k = next.z - previous.z;
        if (collision_test_pill(_collision_test_for_projectiles_flags, &previous,
            &delta, radius, request->unit_key, &collision))
        {
            real dx = collision.point.x - request->target[0];
            real dy = collision.point.y - request->target[1];
            real dz = collision.point.z - request->target[2];
            return collision.object_index == request->target_unit_key &&
                segment >= BOT_ARC_SEGMENTS - 1 && dx * dx + dy * dy + dz * dz <= 4.f;
        }
        previous = next;
    }
    return TRUE;
}

bool bot_grenade_plan(struct bot_grenade_request const *request, float aim[2])
{
    struct bot_grenade_parameters parameters;
    struct bot_arc arc;
    float facing[2];
    float origin[3];
    short pass;
    assert(request && aim);
    if (!bot_grenade_parameters(request, &parameters))
        return false;
    facing[0] = request->facing[0]; facing[1] = request->facing[1];
    for (pass = 0; pass < 2; pass++)
    {
        if (!bot_grenade_origin(request, &parameters, facing, origin) ||
            !bot_arc_plan(origin, request->target, parameters.speed, parameters.gravity, &arc))
            return false;
        facing[0] = atan2f(arc.velocity[1], arc.velocity[0]);
        facing[1] = atan2f(arc.velocity[2], hypotf(arc.velocity[0], arc.velocity[1]));
    }
    if (fabsf(facing[1]) > 1.4f || !bot_grenade_trace(request, &parameters, &arc))
        return false;
    aim[0] = facing[0]; aim[1] = facing[1];
    return true;
}

bool bot_grenade_validate(struct bot_grenade_request const *request, float landing[3])
{
    struct bot_grenade_parameters parameters;
    struct bot_arc arc;
    float origin[3];
    float point[3];
    real dx, dy, dz;
    assert(request && landing);
    if (!bot_grenade_parameters(request, &parameters) ||
        !bot_grenade_origin(request, &parameters, request->facing, origin) ||
        !bot_arc_plan(origin, request->target, parameters.speed, parameters.gravity, &arc))
        return false;
    arc.velocity[0] = parameters.speed * cosf(request->facing[1]) * cosf(request->facing[0]);
    arc.velocity[1] = parameters.speed * cosf(request->facing[1]) * sinf(request->facing[0]);
    arc.velocity[2] = parameters.speed * sinf(request->facing[1]);
    if (!bot_arc_point(&arc, 1.f, point))
        return false;
    dx = point[0] - request->target[0];
    dy = point[1] - request->target[1];
    dz = point[2] - request->target[2];
    if (dx * dx + dy * dy + dz * dz > 2.25f || !bot_grenade_trace(request, &parameters, &arc))
        return false;
    landing[0] = point[0]; landing[1] = point[1]; landing[2] = point[2];
    return true;
}

static boolean bot_danger_is_grenade(long definition_index)
{
    struct game_globals *globals = scenario_get_game_globals();
    short index;
    if (!globals)
        return FALSE;
    for (index = 0; index < NUMBER_OF_UNIT_GRENADE_TYPES &&
        index < globals->grenades.count; index++)
    {
        struct game_globals_grenade *grenade = TAG_BLOCK_GET_ELEMENT(
            &globals->grenades, index, struct game_globals_grenade);
        if (grenade->projectile.index == definition_index && definition_index != NONE)
            return TRUE;
    }
    return FALSE;
}

static boolean bot_danger_visible(struct bot_danger_request const *request,
    real_point3d const *point, long object_index)
{
    real_point3d eye;
    real_vector3d delta;
    struct collision_result collision;
    real distance;
    eye.x = request->eye[0]; eye.y = request->eye[1]; eye.z = request->eye[2];
    delta.i = point->x - eye.x; delta.j = point->y - eye.y; delta.k = point->z - eye.z;
    distance = sqrtf(delta.i * delta.i + delta.j * delta.j + delta.k * delta.k);
    if (!valid_real(distance) || distance > request->sight_range ||
        (distance > 3.f && fabsf(remainderf(atan2f(delta.j, delta.i) - request->yaw,
            6.28318530718f)) > request->half_fov_radians))
        return FALSE;
    return !collision_test_vector(_collision_test_for_line_of_sight_flags, &eye, &delta,
        request->unit_key, &collision) || collision.object_index == object_index;
}

static real bot_danger_distance(struct bot_danger_request const *request,
    real_point3d const *position, real_vector3d const *motion, real_vector3d *velocity)
{
    real x = position->x - request->origin[0];
    real y = position->y - request->origin[1];
    real z = position->z - request->origin[2];
    real dot, length, fraction;
    *velocity = *motion;
    if (!valid_real(velocity->i) || !valid_real(velocity->j) || !valid_real(velocity->k) ||
        fabsf(velocity->i) > 2.f || fabsf(velocity->j) > 2.f || fabsf(velocity->k) > 2.f)
        velocity->i = velocity->j = velocity->k = 0.f;
    dot = x * velocity->i + y * velocity->j + z * velocity->k;
    length = velocity->i * velocity->i + velocity->j * velocity->j + velocity->k * velocity->k;
    fraction = length > 0.f ? PIN(-dot / length, 0.f, 6.f) : 0.f;
    x += fraction * velocity->i; y += fraction * velocity->j; z += fraction * velocity->k;
    return x * x + y * y + z * z;
}

static boolean bot_danger_candidate(struct object_datum const *object, long key, boolean vehicle)
{
    struct unit_datum *parent;
    if (TEST_FLAG(object->object.damage_flags, _object_dead_bit))
        return FALSE;
    if (vehicle)
        return object->object.parent_object_index == NONE;
    if (!bot_danger_is_grenade(object->definition_index))
        return FALSE;
    if (object->object.parent_object_index == NONE)
        return TRUE;
    parent = unit_try_and_get(object->object.parent_object_index);
    /* A held grenade is not yet dangerous; a released sticky attached to a
       unit is. Use CE's inventory identity, not private projectile flag bits. */
    return parent && parent->unit.grenade_object_index != key;
}

static real bot_danger_escape_yaw(struct bot_danger_request const *request,
    real_point3d const *point, real_vector3d const *velocity)
{
    real x = request->origin[0] - point->x;
    real y = request->origin[1] - point->y;
    real speed = velocity->i * velocity->i + velocity->j * velocity->j;
    if (speed > 0.0025f)
    {
        real side = x * -velocity->j + y * velocity->i >= 0.f ? 1.f : -1.f;
        return atan2f(side * velocity->i, -side * velocity->j);
    }
    if (x == 0.f && y == 0.f)
        return atan2f(-sinf(request->yaw), -cosf(request->yaw));
    return atan2f(y, x);
}

bool bot_danger_observe(struct bot_danger_request const *request, struct bot_danger *danger)
{
    struct object_iterator iterator;
    uint32_t examined;
    uint32_t sights = 0;
    real best = 1e10f;
    boolean found = FALSE;
    struct unit_datum *unit;
    short axis;
    assert(request && danger);
    unit = unit_try_and_get(request->unit_key);
    if (!unit || unit->object.parent_object_index != NONE ||
        TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
        return false;
    for (axis = 0; axis < 3; axis++)
        if (!valid_real(request->eye[axis]) || !valid_real(request->origin[axis]) ||
            fabsf(request->eye[axis]) > 100000.f || fabsf(request->origin[axis]) > 100000.f)
            return false;
    if (!valid_real(request->sight_range) || request->sight_range <= 0.f ||
        request->sight_range > 1000.f || !valid_real(request->half_fov_radians) ||
        request->half_fov_radians < 0.f || request->half_fov_radians > 3.142f ||
        !valid_real(request->yaw))
        return false;
    object_iterator_new(&iterator, _object_mask_projectile | _object_mask_vehicle, 0);
    for (examined = 0; examined < 256 && sights < 4 && object_iterator_next(&iterator); examined++)
    {
        struct object_datum *object = object_get(iterator.index);
        real_vector3d velocity;
        real_point3d point;
        real squared;
        real radius;
        real speed;
        boolean vehicle = object->object.type == _object_type_vehicle;
        if (!bot_danger_candidate(object, iterator.index, vehicle))
            continue;
        object_get_origin(iterator.index, &point); /* world space, including attachments */
        squared = bot_danger_distance(request, &point,
            &object->object.translational_velocity, &velocity);
        if (vehicle && (point.x - request->origin[0]) * velocity.i +
            (point.y - request->origin[1]) * velocity.j >= 0.f)
            continue; /* stationary/receding vehicles are navigation, not danger */
        speed = velocity.i * velocity.i + velocity.j * velocity.j;
        radius = vehicle ? MIN(8.f, MAX(1.f, object->object.bounding_sphere_radius + 1.f)) : 6.f;
        if (!valid_real(radius) || !valid_real(squared) || squared > radius * radius ||
            squared >= best || (vehicle && speed < 0.0025f))
            continue;
        sights++;
        if (!bot_danger_visible(request, &point, iterator.index))
            continue;
        danger->position[0] = point.x; danger->position[1] = point.y; danger->position[2] = point.z;
        danger->escape_yaw = bot_danger_escape_yaw(request, &point, &velocity);
        best = squared;
        found = TRUE;
    }
    return found != 0;
}
