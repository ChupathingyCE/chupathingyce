/* port: local players implicitly call out sustained visual contacts. Damage
   provides only a short-lived bearing, not an invisible attacker's position. */
#include "cseries.h"
#include "game.h"
#include "game_engine.h"
#include "players.h"
#include "camera/observer.h"
#include "camera/director.h"
#include "units/units.h"
#include "physics/collisions.h"
#include "bot_manager.h"
#include "bots.h"
#include "human_callouts.h"

#ifdef HALO_FEATURE_BOTS
int config_boolean(char const *name);

#define HUMAN_SIGHT_TICKS TICKS_PER_SECOND
#define HUMAN_ALERT_TICKS (2 * TICKS_PER_SECOND)
#define HUMAN_SIGHT_RANGE 60.f
#define HUMAN_ALERT_RANGE 32.f

static struct human_contact
{
    long player_index, unit_index, since, last_tick;
} human_contacts[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS][HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
static struct human_reporter
{
    long player_index, unit_index, team_index;
} human_reporters[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
static struct human_alert
{
    boolean valid;
    long player_index, unit_index, team_index, time;
    real_point3d origin, point;
} human_alerts[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];

void human_callouts_reset(void)
{
    csmemset(human_contacts, 0, sizeof(human_contacts));
    csmemset(human_alerts, 0, sizeof(human_alerts));
    csmemset(human_reporters, NONE, sizeof(human_reporters));
}

static boolean human_callouts_enabled(void)
{
    return config_boolean("bots.human_callouts") && bots_game_had_bots() &&
        game_engine_has_teams() && game_engine_running() && !game_engine_showing_postgame();
}

static boolean human_target_visible(struct observer_result const *camera,
    long observer_unit, long target_unit, real_point3d *point)
{
    struct unit_datum const *target = unit_try_and_get(target_unit);
    real_vector3d delta;
    real distance;
    struct collision_result collision;

    if (!target || TEST_FLAG(target->object.damage_flags, _object_dead_bit) ||
        TEST_FLAG(target->unit.flags, _unit_active_camouflaged_bit) ||
        TEST_FLAG(target->unit.flags, _unit_super_camouflaged_bit) ||
        target->unit.active_camouflage > 0.f || target->unit.active_camouflage_super_amount > 0.f)
        return FALSE;
    if (!valid_real(camera->field_of_view) || camera->field_of_view <= 0.f ||
        camera->field_of_view >= (real)M_PI)
        return FALSE;
    unit_get_camera_position(target_unit, point);
    vector_from_points3d(&camera->position, point, &delta);
    distance = magnitude3d(&delta);
    if (distance <= 0.01f || distance > HUMAN_SIGHT_RANGE)
        return FALSE;
    /* A conservative cone inside the camera view, including zoom and pitch.
       There is no behind-the-camera hearing exception as in bot perception. */
    if (dot_product3d(&delta, &camera->forward) < distance * cosine(camera->field_of_view * 0.5f))
        return FALSE;
    return !collision_test_vector(_collision_test_for_line_of_sight_flags,
        &camera->position, &delta, observer_unit, &collision);
}

void human_callouts_update(void)
{
    short local_index;
    long now = game_time_get();

    if (!human_callouts_enabled())
    {
        human_callouts_reset();
        return;
    }
    /* At most four local views times the bounded player capacity LOS tests.
       Invisible, camouflaged and out-of-cone targets never reach the LOS test. */
    for (local_index = 0; local_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_index++)
    {
        long reporter_index = local_player_get_player_index(local_index);
        struct player_datum const *reporter = reporter_index != NONE ? player_try_and_get(reporter_index) : NULL;
        struct unit_datum const *unit = reporter && reporter->unit_index != NONE ? unit_try_and_get(reporter->unit_index) : NULL;
        struct human_reporter *previous = &human_reporters[local_index];
        struct observer_result const *camera;
        struct data_iterator iterator;
        struct player_datum *enemy;
        short examined = 0;

        if (!reporter || !unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
            reporter->team_index < 0 || reporter->team_index >= BOT_MANAGER_TEAMS ||
            director_get_perspective(local_index) == _director_perspective_scripted ||
            director_get_perspective(local_index) == _director_perspective_neutral)
        {
            csmemset(human_contacts[local_index], 0, sizeof(human_contacts[local_index]));
            human_alerts[local_index].valid = FALSE;
            previous->player_index = NONE;
            continue;
        }
        if (previous->player_index != reporter_index || previous->unit_index != reporter->unit_index ||
            previous->team_index != reporter->team_index)
        {
            csmemset(human_contacts[local_index], 0, sizeof(human_contacts[local_index]));
            human_alerts[local_index].valid = FALSE;
            previous->player_index = reporter_index;
            previous->unit_index = reporter->unit_index;
            previous->team_index = reporter->team_index;
        }
        camera = observer_get_camera(local_index);
        if (!camera)
            continue;
        data_iterator_new(&iterator, player_data);
        while (examined++ < HALO_PORT_MAXIMUM_NETWORK_PLAYERS &&
            (enemy = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
        {
            long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
            struct human_contact *contact;
            real_point3d point;
            boolean visible;

            if (absolute_index < 0 || absolute_index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
                continue;
            contact = &human_contacts[local_index][absolute_index];
            visible = enemy->team_index != reporter->team_index && enemy->unit_index != NONE &&
                human_target_visible(camera, reporter->unit_index, enemy->unit_index, &point);
            if (!visible)
            {
                contact->last_tick = NONE;
                continue;
            }
            if (contact->player_index != iterator.datum_index || contact->unit_index != enemy->unit_index ||
                contact->last_tick == NONE || now < contact->last_tick || now - contact->last_tick > 1)
            {
                contact->since = now;
                contact->player_index = iterator.datum_index;
                contact->unit_index = enemy->unit_index;
            }
            contact->last_tick = now;
            if (now - contact->since >= HUMAN_SIGHT_TICKS)
                bot_manager_report_sighting(reporter->team_index, iterator.datum_index, 0, &point);
        }
    }
}

void human_callouts_damage(long unit_index, long enemy_player_index, real_vector3d const *incoming)
{
    struct unit_datum const *unit;
    struct player_datum const *reporter;
    struct player_datum const *enemy;
    struct human_alert *alert;
    real_vector3d bearing;
    short local_index;

    if (!human_callouts_enabled() || !incoming)
        return;
    unit = unit_try_and_get(unit_index);
    reporter = unit && unit->unit.player_index != NONE ? player_try_and_get(unit->unit.player_index) : NULL;
    enemy = enemy_player_index != NONE ? player_try_and_get(enemy_player_index) : NULL;
    if (!reporter || !enemy || reporter->unit_index != unit_index ||
        TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
        reporter->team_index == enemy->team_index || reporter->team_index < 0 || reporter->team_index >= BOT_MANAGER_TEAMS)
        return;
    local_index = reporter->local_player_index;
    if (local_index < 0 || local_index >= MAXIMUM_NUMBER_OF_LOCAL_PLAYERS ||
        local_player_get_player_index(local_index) != unit->unit.player_index)
        return;
    /* The damage direction points into the victim. Reverse it, discard
       elevation/range, and mark an approximate area eight metres away. */
    bearing.i = -incoming->i; bearing.j = -incoming->j; bearing.k = 0.f;
    if (!valid_real(bearing.i) || !valid_real(bearing.j) || normalize3d(&bearing) <= 0.01f)
        return;
    alert = &human_alerts[local_index];
    alert->valid = TRUE;
    alert->player_index = unit->unit.player_index;
    alert->unit_index = unit_index;
    alert->team_index = reporter->team_index;
    alert->time = game_time_get();
    object_get_origin(unit_index, &alert->origin);
    point_from_line3d(&alert->origin, &bearing, 8.f, &alert->point);
}

boolean human_callouts_nearest_alert(long team_index, real_point3d const *from, real_point3d *point)
{
    long now = game_time_get();
    short local_index;
    real best_distance = HUMAN_ALERT_RANGE;
    boolean found = FALSE;

    if (!human_callouts_enabled() || !from || !point)
        return FALSE;
    for (local_index = 0; local_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_index++)
    {
        struct human_alert *alert = &human_alerts[local_index];
        struct player_datum const *reporter;
        struct unit_datum const *unit;
        real distance;

        if (!alert->valid || alert->team_index != team_index)
            continue;
        reporter = player_try_and_get(alert->player_index);
        unit = reporter && reporter->unit_index == alert->unit_index ? unit_try_and_get(alert->unit_index) : NULL;
        if (!unit || reporter->team_index != team_index ||
            TEST_FLAG(unit->object.damage_flags, _object_dead_bit) || now < alert->time || now - alert->time > HUMAN_ALERT_TICKS)
        {
            alert->valid = FALSE;
            continue;
        }
        distance = distance3d(from, &alert->origin);
        if (distance <= best_distance)
        {
            *point = alert->point;
            best_distance = distance;
            found = TRUE;
        }
    }
    return found;
}
#endif
