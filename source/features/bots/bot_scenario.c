/*
BOT_SCENARIO.C

port: controlled bot behavior scenarios (debug.bot_scenario;
docs/bot-scenarios.md).

A flat 32 x 24 world-unit arena, walled, with a central wall, wall returns,
pillars and low cover (halo_bot_arena_prototype's layout at half scale), is
built host-locally above an existing multiplayer map: solid to movement,
sight and shots through the world-geometry hooks (extension_api.h), drawn
in the world, and walkable for the bots' routes as polygons after the BSP's
(navigation_world.c). Each trial places a subject bot and its scripted
targets, sets their shields, ammunition and skill, lets the subject decide
for itself (or walk to a goal), and writes every tick of its decisions,
knowledge, aim and movement to a JSON-lines log, then the trial's result.
A free camera follows the subject, with its scores, target knowledge and
route drawn over the view.

Scope: the host's own private game (bots.c's gates), Team Slayer, on 64-bit
desktop builds (host-local world geometry). Nothing is sent to another
machine; no map file or tag is changed. The host's own player and the bots
no trial uses stay where they spawned, unseen by the subjects.
*/

/* ---------- headers */

#include "cseries.h"
#include "bot_scenario.h"
#include "extensions/extension_api.h"

#ifdef HALO_FEATURE_BOTS

#include "engine_ai/surface_navigation.h"

/* port/linux/src/port_config.c's */
long config_integer(char const *name);
int config_boolean(char const *name);
char const *config_string(char const *name);
/* port/linux/src/platform.h's */
void platform_log(char const *format, ...);

#ifdef HALO_EXTENSION_WORLD_GEOMETRY

#include "bot_navigation.h"
#include "navigation_world.h"
#include "cache/cache_files.h"
#include "camera/flying_camera.h"
#include "camera/static_camera.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/hud_messaging.h"
#include "interface/interface.h"
#include "items/weapons.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "physics/collision_features.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "render/render.h"
#include "render/render_debug.h"
#include "scenario/scenario.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "units/units.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	BOT_SCENARIO_ROLES_MAX = BOT_SCENARIO_BOTS_MAX,
	BOT_SCENARIO_BOXES_MAX = 32,
	BOT_SCENARIO_STEPS_MAX = 6,
	BOT_SCENARIO_SKILLS_MAX = 4,
	BOT_SCENARIO_TRIALS_MAX = 100,
	/* the floor's walkable cells: ARENA / CELL each way */
	BOT_SCENARIO_CELLS_X = 16,
	BOT_SCENARIO_CELLS_Y = 12,
	BOT_SCENARIO_CELLS = BOT_SCENARIO_CELLS_X * BOT_SCENARIO_CELLS_Y,
	/* ground samples under the arena, each way */
	BOT_SCENARIO_SAMPLES_X = 9,
	BOT_SCENARIO_SAMPLES_Y = 7,
	BOT_SCENARIO_SETTLE_TICKS = TICKS_PER_SECOND / 2,
	BOT_SCENARIO_RESULT_TICKS = 2 * TICKS_PER_SECOND,
	BOT_SCENARIO_GATHER_TICKS = 20 * TICKS_PER_SECOND,
	BOT_SCENARIO_NAVIGATION_TICKS = 90 * TICKS_PER_SECOND,
	/* a step that waits to be seen gives up after this long */
	BOT_SCENARIO_SEEN_WAIT_TICKS = 8 * TICKS_PER_SECOND,
	BOT_SCENARIO_LINE_MAX = 160,
};

#define BOT_SCENARIO_HALF_WIDTH 16.f
#define BOT_SCENARIO_HALF_DEPTH 12.f
#define BOT_SCENARIO_CELL 2.f
#define BOT_SCENARIO_FLOOR_THICKNESS 0.5f
/* the floor's top above the highest ground or scenery found beneath it */
#define BOT_SCENARIO_FLOOR_CLEARANCE 2.f
/* a ground probe's length below its start */
#define BOT_SCENARIO_PROBE_DEPTH 80.f
/* a scripted target is at its step this near */
#define BOT_SCENARIO_STEP_REACHED 0.3f
/* a subject has its aim on its target within this (degrees) */
#define BOT_SCENARIO_ON_TARGET_DEGREES 5.f

typedef char bot_scenario_cells_check[
	BOT_SCENARIO_CELLS_X * BOT_SCENARIO_CELL == 2.f * BOT_SCENARIO_HALF_WIDTH &&
	BOT_SCENARIO_CELLS_Y * BOT_SCENARIO_CELL == 2.f * BOT_SCENARIO_HALF_DEPTH ? 1 : -1];
typedef char bot_scenario_scores_check[BOT_SCENARIO_SCORES == 6 ? 1 : -1];

/* the scenarios, in their prototype's order */
enum
{
	_scenario_open_field = 0, /* S01 */
	_scenario_line_of_sight, /* S02 */
	_scenario_route, /* S03 */
	_scenario_low_shields, /* S04 */
	_scenario_pickup, /* S05 */
	_scenario_team, /* S06 */
	_scenario_strafe, /* S07 */
	_scenario_objective, /* S08: needs CTF, not run */
	_scenario_hidden_attacker, /* S09 */
	_scenario_crowd, /* S10 */
	BOT_SCENARIO_COUNT
};

enum
{
	_phase_off = 0,
	_phase_arena, /* the arena built; waiting for routes over it */
	_phase_gather, /* waiting for the trial's bots to be alive */
	_phase_settle, /* placed, held a moment */
	_phase_run,
	_phase_result, /* held a moment, the result shown */
	_phase_done,
};

enum
{
	_camera_off = 0,
	_camera_follow,
	_camera_overview,
	_camera_first_person,
};

enum
{
	_box_floor = 0,
	_box_wall,
	_box_cover,
	_box_pillar,
};

/* a scripted target's step: go to (x, y), then stay there this long */
enum
{
	_step_fire = FLAG(0), /* shooting at the nearest subject it can see */
	_step_until_seen = FLAG(1), /* stay until a subject has seen it this long */
	_step_crouch = FLAG(2),
};

/* ---------- structures */

struct bot_scenario_box
{
	char const *name;
	short kind;
	real_point3d minimum;
	real_point3d maximum;
};

struct bot_scenario_box_definition
{
	char const *name;
	short kind;
	real x0, y0, x1, y1; /* arena coordinates */
	real height; /* above the floor */
};

struct bot_scenario_step
{
	real x, y;
	real seconds;
	unsigned short flags;
};

struct bot_scenario_definition
{
	char const *id;
	char const *name;
	char const *measures;
	short variants;
	boolean supported;
};

/* one bot's part in a trial */
struct bot_scenario_role
{
	boolean used;
	boolean puppet; /* a scripted target; otherwise a subject */
	boolean sustain; /* its shields and body are restored every tick */
	boolean hold_until_hurt; /* a subject that stands where placed until first hurt */
	real x, y;
	real yaw;
	real shield;
	short ammunition; /* NONE: as spawned; else rounds in the readied weapon, none spare */
	short step_count;
	boolean loop;
	struct bot_scenario_step steps[BOT_SCENARIO_STEPS_MAX];
};

struct bot_scenario_setup
{
	short scenario;
	short variant;
	char variant_name[48];
	short role_count;
	struct bot_scenario_role roles[BOT_SCENARIO_ROLES_MAX];
	boolean goal_valid;
	real goal_x, goal_y;
	boolean pickup_wanted;
	real pickup_x, pickup_y;
	long duration_ticks;
	short camera;
};

/* what happened to a subject this trial: ticks since the trial began, NONE
for what did not happen */
struct bot_scenario_metrics
{
	long first_visible;
	long first_on_target;
	long first_fire;
	long first_hit;
	long first_shared;
	long first_retreat;
	long lost_sight;
	long reacquired;
	long unseen_since;
	long arrived;
	long died;
	long killed_target;
	long visible_ticks;
	long fire_ticks;
	long hits;
	long crouch_ticks;
	long jumps;
	long strafe_changes;
	long leaf_changes;
	long retreat_ticks;
	long hidden_updates;
	long unseen_exact_ticks; /* unseen, yet its idea of the target is where it is */
	long stalled_ticks;
	long aim_samples;
	long kills;
	long route_ticks;
	real damage_dealt;
	real damage_taken;
	real max_retreat_score;
	real distance;
	real hidden_drift;
	real aim_error_sum;
	/* the last tick's, for changes */
	boolean have_last;
	boolean was_visible;
	boolean last_jump;
	boolean belief_valid;
	char const *last_leaf;
	real last_strafe;
	long last_target;
	real_point3d last_position;
	real_point3d last_belief;
};

/* a bot's state in the trial that is not its decisions' */
struct bot_scenario_agent
{
	long player_index;
	long unit_index;
	real vitality; /* shields + body after last tick's restore */
	short step;
	boolean holding;
	long hold_start;
	long seen_since;
	boolean fired;
	boolean dead;
	boolean hurt;
	real_euler_angles2d aim_error;
	long aim_error_time;
};

/* ---------- globals */

static struct bot_scenario_definition const bot_scenario_definitions[BOT_SCENARIO_COUNT] =
{
	{ "S01", "Open-field first contact", "first sight, aim, fire, hits, time to kill", 3, TRUE },
	{ "S02", "Line of sight broken", "last sighting, pursuit, reacquisition", 1, TRUE },
	{ "S03", "Route around a wall", "route, arrival, path length, stalls", 2, TRUE },
	{ "S04", "Low-shield disengage", "retreat score, retreat, survival", 3, TRUE },
	{ "S05", "Pickup during combat", "scavenge score, pickup obtained", 2, TRUE },
	{ "S06", "Team awareness, 2v2", "shared sighting, both engaged", 1, TRUE },
	{ "S07", "Strafe by difficulty", "strafe changes, crouches, jumps, hits", 2, TRUE },
	{ "S08", "CTF priority", "needs a CTF variant and flags: not run", 1, FALSE },
	{ "S09", "Unseen attacker relocates", "updates of the target's position while unseen", 1, TRUE },
	{ "S10", "Crowd and routes", "AI time per tick, route failures, stalls", 1, TRUE },
};

static char const *const bot_scenario_skill_names[BOT_SCENARIO_SKILLS_MAX] =
{
	"recruit", "marine", "odst", "spartan"
};

/* The layout of halo_bot_arena_prototype/geometry_manifest.json at half
scale (its 64 x 48 units are 32 x 24 here: about 98 by 73 metres), with x
east and y north (the prototype's z is south). The perimeter is raised to 2
units so that nobody jumps out. */
static struct bot_scenario_box_definition const bot_scenario_box_definitions[] =
{
	{ "perimeter north", _box_wall, -16.f, 11.75f, 16.f, 12.f, 2.f },
	{ "perimeter south", _box_wall, -16.f, -12.f, 16.f, -11.75f, 2.f },
	{ "perimeter west", _box_wall, -16.f, -11.75f, -15.75f, 11.75f, 2.f },
	{ "perimeter east", _box_wall, 15.75f, -11.75f, 16.f, 11.75f, 2.f },
	{ "central wall", _box_wall, -0.5f, -3.5f, 0.5f, 3.5f, 1.5f },
	{ "north return", _box_wall, -0.5f, 3.25f, 4.f, 3.75f, 1.5f },
	{ "south return", _box_wall, -4.f, -3.75f, 0.5f, -3.25f, 1.5f },
	{ "east pillar a", _box_pillar, 6.25f, -2.25f, 7.75f, -0.75f, 1.f },
	{ "east pillar b", _box_pillar, 9.75f, 2.25f, 11.25f, 3.75f, 1.f },
	{ "east route block", _box_wall, 7.75f, -6.8f, 11.25f, -6.2f, 1.5f },
	{ "west cover a", _box_cover, -8.25f, -4.5f, -6.75f, -3.5f, 0.75f },
	{ "west cover b", _box_cover, -11.5f, -7.25f, -10.5f, -5.75f, 0.75f },
	{ "west pillar", _box_pillar, -4.5f, 7.5f, -3.5f, 8.5f, 1.6f },
	{ "south cover left", _box_cover, -7.5f, -9.875f, -5.5f, -9.125f, 0.65f },
	{ "south cover right", _box_cover, 5.5f, -9.875f, 7.5f, -9.125f, 0.65f },
};
typedef char bot_scenario_boxes_check[
	NUMBEROF(bot_scenario_box_definitions) + 1 <= BOT_SCENARIO_BOXES_MAX ? 1 : -1];

/* corner c is (c & 1 ? maximum : minimum).x, (c & 2) y, (c & 4) z; faces
wind counterclockwise seen from outside, about their normals */
static unsigned char const bot_scenario_box_faces[6][4] =
{
	{ 4, 5, 7, 6 }, { 0, 2, 3, 1 }, { 1, 3, 7, 5 }, { 0, 4, 6, 2 }, { 2, 6, 7, 3 }, { 0, 1, 5, 4 }
};
static real const bot_scenario_box_normals[6][3] =
{
	{ 0.f, 0.f, 1.f }, { 0.f, 0.f, -1.f }, { 1.f, 0.f, 0.f }, { -1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, -1.f, 0.f }
};
static unsigned char const bot_scenario_box_edges[12][2] =
{
	{ 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
};

static struct
{
	/* the settings, read with the map */
	boolean requested;
	boolean enabled;
	boolean exit_when_done;
	short camera;
	short scenarios[BOT_SCENARIO_COUNT];
	short scenario_count;
	short skills[BOT_SCENARIO_SKILLS_MAX];
	short skill_count;
	short trials;
	uint32_t seed;
	short crowd;
	short bot_count;
	char log_path[512];

	/* the arena */
	boolean arena_active;
	uint64_t revision;
	real_point3d center; /* on the floor's top */
	short box_count;
	struct bot_scenario_box boxes[BOT_SCENARIO_BOXES_MAX];

	/* the run */
	short phase;
	long phase_time;
	short scenario_cursor;
	short skill_cursor;
	short trial_cursor;
	long trial_number;
	long trial_start;
	uint32_t trial_seed;
	uint32_t random;
	struct bot_scenario_setup setup;
	struct bot_scenario_agent agents[BOT_SCENARIO_BOTS_MAX];
	struct bot_scenario_metrics metrics[BOT_SCENARIO_BOTS_MAX];
	long pickup_weapon_index;
	char const *end_reason;
	long end_tick;
	double milliseconds_sum;
	double milliseconds_maximum;
	long milliseconds_ticks;
	FILE *log;
	char status[BOT_SCENARIO_LINE_MAX];

	/* what the overlay shows of the first subject */
	boolean observed;
	struct bot_scenario_observation observation;

	/* the camera */
	boolean camera_valid;
	real_point3d camera_position;
	real_point3d camera_focus;
	struct flying_camera flying;
} bot_scenario;

/* ---------- private code: small things */

static uint32_t bot_scenario_next_random(void)
{
	/* xorshift32: the fixture's own numbers, never the game's */
	uint32_t x = bot_scenario.random ? bot_scenario.random : 0x9e3779b9u;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	bot_scenario.random = x;
	return x;
}

static real bot_scenario_random_range(real minimum, real maximum)
{
	return minimum + (maximum - minimum) * (real)((double)bot_scenario_next_random() / 4294967296.0);
}

static long bot_scenario_ticks(real seconds)
{
	return (long)(seconds * TICKS_PER_SECOND + 0.5f);
}

static real bot_scenario_angle_normalize(real angle)
{
	if (!valid_real(angle))
		return 0.f;
	angle = (real)fmod(angle, 2.0 * M_PI);
	if (angle > (real)M_PI)
		angle -= 2.f * (real)M_PI;
	if (angle < -(real)M_PI)
		angle += 2.f * (real)M_PI;
	return angle;
}

static void bot_scenario_world_point(real x, real y, real_point3d *point)
{
	point->x = bot_scenario.center.x + x;
	point->y = bot_scenario.center.y + y;
	point->z = bot_scenario.center.z;
}

static boolean bot_scenario_active_trial(void)
{
	return bot_scenario.enabled && bot_scenario.arena_active &&
		(bot_scenario.phase == _phase_settle || bot_scenario.phase == _phase_run ||
		bot_scenario.phase == _phase_result);
}

static struct unit_datum *bot_scenario_living_unit(long unit_index)
{
	struct unit_datum *unit;

	if (unit_index == NONE)
		return NULL;
	unit = unit_try_and_get(unit_index);
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return NULL;
	return unit;
}

/* the living unit of the bot in the slot, NULL if it has none */
static struct unit_datum *bot_scenario_slot_unit(short slot, long *unit_index)
{
	long player_index = bots_scenario_player_index(slot);
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	struct unit_datum *unit = player ? bot_scenario_living_unit(player->unit_index) : NULL;

	if (unit_index)
		*unit_index = unit ? player->unit_index : NONE;
	return unit;
}

static short bot_scenario_slot_of_player(long player_index)
{
	short slot;

	if (player_index == NONE)
		return NONE;
	for (slot = 0; slot < bot_scenario.bot_count; slot++)
	{
		if (bots_scenario_player_index(slot) == player_index)
			return slot;
	}
	return NONE;
}

static void bot_scenario_chest(long unit_index, real_point3d *point)
{
	unit_get_head_position(unit_index, point);
	point->z -= 0.12f;
}

static boolean bot_scenario_line_clear(real_point3d const *from, real_point3d const *to, long ignore_index)
{
	struct collision_result collision;
	real_vector3d vector;

	vector.i = to->x - from->x;
	vector.j = to->y - from->y;
	vector.k = to->z - from->z;
	return !collision_test_vector(_collision_test_for_line_of_sight_flags, from, &vector, ignore_index, &collision);
}

static real bot_scenario_horizontal_distance(real_point3d const *a, real_point3d const *b)
{
	real i = b->x - a->x;
	real j = b->y - a->y;

	return square_root(i * i + j * j);
}

/* ---------- private code: the log */

static void bot_scenario_log_real(real value)
{
	if (valid_real(value))
		fprintf(bot_scenario.log, "%.4f", value);
	else
		fputs("null", bot_scenario.log);
}

static void bot_scenario_log_point(real_point3d const *point)
{
	fputc('[', bot_scenario.log);
	bot_scenario_log_real(point->x);
	fputc(',', bot_scenario.log);
	bot_scenario_log_real(point->y);
	fputc(',', bot_scenario.log);
	bot_scenario_log_real(point->z);
	fputc(']', bot_scenario.log);
}

static void bot_scenario_log_tick(long tick)
{
	if (tick == NONE)
		fputs("null", bot_scenario.log);
	else
		fprintf(bot_scenario.log, "%ld", tick);
}

static void bot_scenario_log_open(void)
{
	short index;

	if (bot_scenario.log || !bot_scenario.log_path[0])
		return;
	bot_scenario.log = fopen(bot_scenario.log_path, "w");
	if (!bot_scenario.log)
	{
		platform_log("bot scenario: cannot write %s; no telemetry", bot_scenario.log_path);
		bot_scenario.log_path[0] = 0;
		return;
	}
	/* (a map's name comes from its file: only plain characters are written) */
	{
		char const *name = cache_file_loaded_map_name();
		char safe[64];
		short length = 0;

		while (name && name[length] && length < (short)sizeof(safe) - 1)
		{
			char character = name[length];

			safe[length++] = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
				(character >= '0' && character <= '9') || character == '_' || character == '-' ? character : '_';
		}
		safe[length] = 0;
		fprintf(bot_scenario.log, "{\"type\":\"run\",\"version\":1,\"map\":\"%s\",\"center\":", safe);
	}
	bot_scenario_log_point(&bot_scenario.center);
	fprintf(bot_scenario.log, ",\"seed\":%lu,\"trials\":%d,\"ticks_per_second\":%d,\"skills\":[",
		(unsigned long)bot_scenario.seed, bot_scenario.trials, TICKS_PER_SECOND);
	for (index = 0; index < bot_scenario.skill_count; index++)
		fprintf(bot_scenario.log, "%s\"%s\"", index ? "," : "", bot_scenario_skill_names[bot_scenario.skills[index]]);
	fputs("],\"scenarios\":[", bot_scenario.log);
	for (index = 0; index < bot_scenario.scenario_count; index++)
		fprintf(bot_scenario.log, "%s\"%s\"", index ? "," : "",
			bot_scenario_definitions[bot_scenario.scenarios[index]].id);
	fputs("],\"boxes\":[", bot_scenario.log);
	for (index = 0; index < bot_scenario.box_count; index++)
	{
		fprintf(bot_scenario.log, "%s{\"name\":\"%s\",\"min\":", index ? "," : "", bot_scenario.boxes[index].name);
		bot_scenario_log_point(&bot_scenario.boxes[index].minimum);
		fputs(",\"max\":", bot_scenario.log);
		bot_scenario_log_point(&bot_scenario.boxes[index].maximum);
		fputc('}', bot_scenario.log);
	}
	fputs("]}\n", bot_scenario.log);
	fflush(bot_scenario.log);
}

static void bot_scenario_log_close(void)
{
	if (bot_scenario.log)
	{
		fclose(bot_scenario.log);
		bot_scenario.log = NULL;
	}
}

/* ---------- private code: the settings */

static boolean bot_scenario_token_equal(char const *token, long length, char const *name)
{
	long index;

	for (index = 0; index < length; index++)
	{
		char a = token[index];
		char b = name[index];

		if (!b)
			return FALSE;
		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = (char)(b - 'A' + 'a');
		if (a != b)
			return FALSE;
	}
	return name[length] == 0;
}

/* comma-separated names: each listed once, in the order given; "all" lists
every one. At most 256 characters are read. */
static short bot_scenario_parse_list(char const *text, char const *const *names, short name_count,
	boolean const *allowed, short *list, short maximum)
{
	short count = 0;
	long position = 0;

	if (!text)
		return 0;
	while (text[position] && position < 256 && count < maximum)
	{
		long start;
		long length;
		short name_index;

		while (text[position] == ',' || text[position] == ' ')
			position++;
		start = position;
		while (text[position] && text[position] != ',' && text[position] != ' ' && position < 256)
			position++;
		length = position - start;
		if (length == 0)
			continue;
		if (bot_scenario_token_equal(text + start, length, "all"))
		{
			count = 0;
			for (name_index = 0; name_index < name_count && count < maximum; name_index++)
				if (!allowed || allowed[name_index])
					list[count++] = name_index;
			return count;
		}
		for (name_index = 0; name_index < name_count; name_index++)
		{
			if (bot_scenario_token_equal(text + start, length, names[name_index]))
			{
				short existing;
				boolean listed = FALSE;

				for (existing = 0; existing < count; existing++)
					listed |= list[existing] == name_index;
				if (!listed)
					list[count++] = name_index;
				break;
			}
		}
		if (name_index == name_count)
			platform_log("bot scenario: unknown name '%.*s' ignored", (int)length, text + start);
	}
	return count;
}

/* the arena's center, x and y, and the height its ground probes start at:
debug.bot_scenario_origin, else the map's own */
static boolean bot_scenario_origin(real_point3d *origin)
{
	char const *text = config_string("debug.bot_scenario_origin");
	char const *map_name = cache_file_loaded_map_name();
	double x, y, z;

	if (text && text[0])
	{
		int fields = sscanf(text, "%lf,%lf,%lf", &x, &y, &z);

		if (fields < 2 || !(x > -5000.0 && x < 5000.0) || !(y > -5000.0 && y < 5000.0))
		{
			platform_log("bot scenario: debug.bot_scenario_origin must be x,y or x,y,z");
			return FALSE;
		}
		if (fields < 3 || !(z > -5000.0 && z < 5000.0))
			z = 20.0;
		origin->x = (real)x;
		origin->y = (real)y;
		origin->z = (real)z;
		return TRUE;
	}
	/* (Blood Gulch: midway between the bases, under open sky) */
	if (map_name && !strcmp(map_name, "bloodgulch"))
	{
		set_real_point3d(origin, 69.f, -118.f, 20.f);
		return TRUE;
	}
	platform_log("bot scenario: no arena place for %s; set debug.bot_scenario_origin", map_name ? map_name : "?");
	return FALSE;
}

/* ---------- private code: the arena */

static void bot_scenario_box_corner(struct bot_scenario_box const *box, short corner, real_point3d *point)
{
	point->x = corner & 1 ? box->maximum.x : box->minimum.x;
	point->y = corner & 2 ? box->maximum.y : box->minimum.y;
	point->z = corner & 4 ? box->maximum.z : box->minimum.z;
}

static boolean bot_scenario_box_contains(struct bot_scenario_box const *box, real_point3d const *point)
{
	return point->x >= box->minimum.x && point->x <= box->maximum.x &&
		point->y >= box->minimum.y && point->y <= box->maximum.y &&
		point->z >= box->minimum.z && point->z <= box->maximum.z;
}

static boolean bot_scenario_box_near(struct bot_scenario_box const *box, real_point3d const *center, real radius)
{
	return center->x >= box->minimum.x - radius && center->x <= box->maximum.x + radius &&
		center->y >= box->minimum.y - radius && center->y <= box->maximum.y + radius &&
		center->z >= box->minimum.z - radius && center->z <= box->maximum.z + radius;
}

static void bot_scenario_add_box(char const *name, short kind, real_point3d const *minimum,
	real_point3d const *maximum)
{
	struct bot_scenario_box *box;

	assert(bot_scenario.box_count < BOT_SCENARIO_BOXES_MAX);
	box = &bot_scenario.boxes[bot_scenario.box_count++];
	box->name = name;
	box->kind = kind;
	box->minimum = *minimum;
	box->maximum = *maximum;
}

/* The floor floats clear of the map's ground: its top is above the highest
ground and scenery found beneath it, so the arena is flat whatever the
terrain, and the map's own floor is not walkable from it. FALSE (no arena)
when nothing is found below the probes' start. */
static boolean bot_scenario_build_arena(void)
{
	real_point3d origin;
	real highest = -5000.f;
	short hits = 0;
	short sample_x;
	short sample_y;
	short index;

	if (!bot_scenario_origin(&origin))
		return FALSE;
	for (sample_x = 0; sample_x < BOT_SCENARIO_SAMPLES_X; sample_x++)
	{
		for (sample_y = 0; sample_y < BOT_SCENARIO_SAMPLES_Y; sample_y++)
		{
			struct collision_result collision;
			real_point3d from;
			real_vector3d down;

			from.x = origin.x - (BOT_SCENARIO_HALF_WIDTH + 1.f) +
				(2.f * BOT_SCENARIO_HALF_WIDTH + 2.f) * (real)sample_x / (real)(BOT_SCENARIO_SAMPLES_X - 1);
			from.y = origin.y - (BOT_SCENARIO_HALF_DEPTH + 1.f) +
				(2.f * BOT_SCENARIO_HALF_DEPTH + 2.f) * (real)sample_y / (real)(BOT_SCENARIO_SAMPLES_Y - 1);
			from.z = origin.z;
			set_real_vector3d(&down, 0.f, 0.f, -BOT_SCENARIO_PROBE_DEPTH);
			if (collision_test_vector(FLAG(_collision_test_structure_bit) |
				FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_objects_bit) |
				FLAG(_collision_test_objects_scenery_bit) | FLAG(_collision_test_objects_machines_bit),
				&from, &down, NONE, &collision) && valid_real(collision.point.z))
			{
				hits++;
				highest = MAX(highest, collision.point.z);
			}
		}
	}
	if (hits < BOT_SCENARIO_SAMPLES_X * BOT_SCENARIO_SAMPLES_Y / 2 ||
		highest + BOT_SCENARIO_FLOOR_CLEARANCE + 3.f >= origin.z)
	{
		platform_log("bot scenario: no ground under the arena at (%.1f %.1f) below z %.1f (%d of %d probes hit)",
			origin.x, origin.y, origin.z, hits, BOT_SCENARIO_SAMPLES_X * BOT_SCENARIO_SAMPLES_Y);
		return FALSE;
	}
	bot_scenario.center.x = origin.x;
	bot_scenario.center.y = origin.y;
	bot_scenario.center.z = highest + BOT_SCENARIO_FLOOR_CLEARANCE;
	bot_scenario.box_count = 0;
	{
		real_point3d minimum;
		real_point3d maximum;

		set_real_point3d(&minimum, origin.x - BOT_SCENARIO_HALF_WIDTH, origin.y - BOT_SCENARIO_HALF_DEPTH,
			bot_scenario.center.z - BOT_SCENARIO_FLOOR_THICKNESS);
		set_real_point3d(&maximum, origin.x + BOT_SCENARIO_HALF_WIDTH, origin.y + BOT_SCENARIO_HALF_DEPTH,
			bot_scenario.center.z);
		bot_scenario_add_box("floor", _box_floor, &minimum, &maximum);
	}
	for (index = 0; index < (short)NUMBEROF(bot_scenario_box_definitions); index++)
	{
		struct bot_scenario_box_definition const *definition = &bot_scenario_box_definitions[index];
		real_point3d minimum;
		real_point3d maximum;

		/* (sunk a little into the floor: no seam to see or catch on) */
		set_real_point3d(&minimum, origin.x + definition->x0, origin.y + definition->y0, bot_scenario.center.z - 0.05f);
		set_real_point3d(&maximum, origin.x + definition->x1, origin.y + definition->y1,
			bot_scenario.center.z + definition->height);
		bot_scenario_add_box(definition->name, definition->kind, &minimum, &maximum);
	}
	bot_scenario.arena_active = TRUE;
	/* (routes built before it are stale: navigation_world.c builds again
	with the floor's polygons) */
	bot_scenario.revision++;
	platform_log("bot scenario: arena at (%.2f %.2f %.2f), %d boxes, ground highest %.2f (%d probes)",
		bot_scenario.center.x, bot_scenario.center.y, bot_scenario.center.z, bot_scenario.box_count,
		highest, hits);
	return TRUE;
}

/* ---------- private code: the arena as world geometry */

static boolean bot_scenario_test_point(real_point3d const *point)
{
	short index;

	if (!bot_scenario.arena_active)
		return FALSE;
	for (index = 0; index < bot_scenario.box_count; index++)
	{
		if (bot_scenario_box_contains(&bot_scenario.boxes[index], point))
			return TRUE;
	}
	return FALSE;
}

/* Every box's corners, edges and faces as the engine's rounded collision
features, as platform.c's board does: 8 spheres, 12 cylinders and 6 prisms
a box, appended only while they fit (the engine's own are never evicted).
NONE surface identity: no BSP surface or material is indexed. */
static void bot_scenario_get_features(real_point3d const *center, real radius,
	real height, real width, struct collision_feature_list *features)
{
	short index;

	if (!bot_scenario.arena_active)
		return;
	for (index = 0; index < bot_scenario.box_count; index++)
	{
		struct bot_scenario_box const *box = &bot_scenario.boxes[index];
		real_point3d corners[8];
		short corner;
		short edge;
		short face;

		if (!bot_scenario_box_near(box, center, radius))
			continue;
		if (features->count[_collision_feature_sphere] + 16 > MAXIMUM_COLLISION_FEATURES_PER_TEST ||
			features->count[_collision_feature_cylinder] + 32 > MAXIMUM_COLLISION_FEATURES_PER_TEST ||
			features->count[_collision_feature_prism] + 30 > MAXIMUM_COLLISION_FEATURES_PER_TEST)
		{
			return;
		}
		for (corner = 0; corner < 8; corner++)
			bot_scenario_box_corner(box, corner, &corners[corner]);
		for (corner = 0; corner < 8; corner++)
			collision_features_from_point(&corners[corner], height, width,
				NONE, NONE, 0, (byte)NONE, NONE, features);
		for (edge = 0; edge < 12; edge++)
		{
			real_vector3d vector;

			vector_from_points3d(&corners[bot_scenario_box_edges[edge][0]],
				&corners[bot_scenario_box_edges[edge][1]], &vector);
			collision_features_from_line(&corners[bot_scenario_box_edges[edge][0]], &vector, height, width,
				NONE, NONE, 0, (byte)NONE, NONE, features);
		}
		for (face = 0; face < 6; face++)
		{
			real_point3d points[4];
			real_plane3d plane;
			short vertex;

			for (vertex = 0; vertex < 4; vertex++)
				points[vertex] = corners[bot_scenario_box_faces[face][vertex]];
			set_real_vector3d(&plane.n, bot_scenario_box_normals[face][0], bot_scenario_box_normals[face][1],
				bot_scenario_box_normals[face][2]);
			plane.d = dot_product3d(&plane.n, (real_vector3d const *)&points[0]);
			collision_features_from_polygon(4, points, &plane, height, width,
				NONE, NONE, 0, (byte)NONE, NONE, features);
		}
	}
}

/* a ray against one box (slabs): the fraction and face where it enters
(front) or, from inside, leaves (back); FALSE for a miss */
static boolean bot_scenario_box_ray(struct bot_scenario_box const *box, real_point3d const *point,
	real_vector3d const *vector, boolean front, boolean back, real *t, short *face, boolean *leaving)
{
	real enter = -1.e30f;
	real leave = 1.e30f;
	short enter_face = NONE;
	short leave_face = NONE;
	short axis;

	for (axis = 0; axis < 3; axis++)
	{
		real origin = point->n[axis];
		real direction = vector->n[axis];
		real minimum = box->minimum.n[axis];
		real maximum = box->maximum.n[axis];

		if (direction == 0.f)
		{
			if (origin < minimum || origin > maximum)
				return FALSE;
		}
		else
		{
			real t0 = (minimum - origin) / direction;
			real t1 = (maximum - origin) / direction;
			/* faces: +z 0, -z 1, +x 2, -x 3, +y 4, -y 5 */
			short minimum_face = axis == 0 ? 3 : axis == 1 ? 5 : 1;
			short maximum_face = axis == 0 ? 2 : axis == 1 ? 4 : 0;
			short face0 = minimum_face;
			short face1 = maximum_face;

			if (t0 > t1)
			{
				real swap = t0;

				t0 = t1;
				t1 = swap;
				face0 = maximum_face;
				face1 = minimum_face;
			}
			if (t0 > enter)
			{
				enter = t0;
				enter_face = face0;
			}
			if (t1 < leave)
			{
				leave = t1;
				leave_face = face1;
			}
			if (enter > leave)
				return FALSE;
		}
	}
	if (front && enter >= 0.f && enter <= 1.f && enter_face != NONE && enter < *t)
	{
		*t = enter;
		*face = enter_face;
		*leaving = FALSE;
		return TRUE;
	}
	if (back && enter < 0.f && leave >= 0.f && leave <= 1.f && leave_face != NONE && leave < *t)
	{
		*t = leave;
		*face = leave_face;
		*leaving = TRUE;
		return TRUE;
	}
	return FALSE;
}

static void bot_scenario_collision_result(real_point3d const *point, real_vector3d const *vector,
	real t, real_plane3d const *plane, struct collision_result *result)
{
	/* (no object or BSP surface to dereference: platform.c's mesh result) */
	memset(result, 0, sizeof(*result));
	result->type = _collision_result_mesh;
	result->t = t;
	result->plane = *plane;
	point_from_line3d(point, vector, t, &result->point);
	result->object_index = NONE;
	result->region_index = NONE;
	result->node_index = NONE;
	result->bsp_index = NONE;
	result->surface_index = NONE;
	result->plane_designator = NONE;
	result->material_type = NONE;
	result->material_index = NONE;
	result->breakable_surface_index = (byte)NONE;
	scenario_location_from_point(&result->start_location, point);
	scenario_location_from_point(&result->location, &result->point);
}

static boolean bot_scenario_test_vector(unsigned long flags, real_point3d const *point,
	real_vector3d const *vector, real radius, struct collision_result *result)
{
	real t = result->t;
	real_plane3d plane;

	if (!bot_scenario.arena_active)
		return FALSE;
	if (radius > 0.f)
	{
		/* (a swept sphere: the rounded features movement uses) */
		struct collision_feature_list features;
		struct collision_plane hit;
		real_point3d center;

		point_from_line3d(point, vector, 0.5f, &center);
		collision_features_new(&features);
		bot_scenario_get_features(&center, magnitude3d(vector) * 0.5f + radius, 0.f, radius, &features);
		if (!collision_features_test_vector(&features, point, vector, &hit) || hit.t >= t)
			return FALSE;
		t = hit.t;
		plane = hit.plane;
	}
	else
	{
		boolean front = TEST_FLAG(flags, _collision_test_front_facing_surfaces_bit);
		boolean back = TEST_FLAG(flags, _collision_test_back_facing_surfaces_bit);
		short hit_face = NONE;
		boolean hit_leaving = FALSE;
		short hit_box = NONE;
		short index;

		if (!front && !back)
			front = back = TRUE;
		for (index = 0; index < bot_scenario.box_count; index++)
		{
			short face;
			boolean leaving;

			if (bot_scenario_box_ray(&bot_scenario.boxes[index], point, vector, front, back, &t, &face, &leaving))
			{
				hit_box = index;
				hit_face = face;
				hit_leaving = leaving;
			}
		}
		if (hit_box == NONE)
			return FALSE;
		set_real_vector3d(&plane.n, bot_scenario_box_normals[hit_face][0], bot_scenario_box_normals[hit_face][1],
			bot_scenario_box_normals[hit_face][2]);
		{
			real_point3d corner;

			bot_scenario_box_corner(&bot_scenario.boxes[hit_box], bot_scenario_box_faces[hit_face][0], &corner);
			plane.d = dot_product3d(&plane.n, (real_vector3d const *)&corner);
		}
		/* (a face left from inside faces the ray) */
		if (hit_leaving)
		{
			scale_vector3d(&plane.n, -1.f, &plane.n);
			plane.d = -plane.d;
		}
	}
	bot_scenario_collision_result(point, vector, t, &plane, result);
	return TRUE;
}

static void bot_scenario_color(real_argb_color *color, real red, real green, real blue, real shade)
{
	color->alpha = 1.f;
	color->red = PIN(red * shade, 0.f, 1.f);
	color->green = PIN(green * shade, 0.f, 1.f);
	color->blue = PIN(blue * shade, 0.f, 1.f);
}

static void bot_scenario_draw_quad(real_point3d const points[4], real_argb_color const *color)
{
	rasterizer_debug_triangle(&points[0], &points[1], &points[2], color);
	rasterizer_debug_triangle(&points[0], &points[2], &points[3], color);
}

/* Flat colors, shaded by each face's facing to a fixed light: the floor in
two-tone 2-unit tiles (to see movement), walls grey-blue, cover orange,
pillars teal. Opaque, depth-tested triangles in the rasterizer's debug
geometry (drawn with the window's other debug geometry: the immediate
debug path draws nothing at this point of the frame on the GL renderer).
About 600 triangles a frame, well within its 8192 primitives. */
static void bot_scenario_render(void)
{
	static real const light[3] = { 0.37f, 0.28f, 0.886f };
	short index;

	if (!bot_scenario.arena_active)
		return;
	for (index = 0; index < bot_scenario.box_count; index++)
	{
		struct bot_scenario_box const *box = &bot_scenario.boxes[index];
		real_point3d corners[8];
		short corner;
		short face;
		real base[3];

		switch (box->kind)
		{
		case _box_floor: base[0] = 0.46f; base[1] = 0.48f; base[2] = 0.52f; break;
		case _box_cover: base[0] = 0.86f; base[1] = 0.52f; base[2] = 0.22f; break;
		case _box_pillar: base[0] = 0.25f; base[1] = 0.62f; base[2] = 0.62f; break;
		default: base[0] = 0.56f; base[1] = 0.6f; base[2] = 0.72f; break;
		}
		for (corner = 0; corner < 8; corner++)
			bot_scenario_box_corner(box, corner, &corners[corner]);
		for (face = 0; face < 6; face++)
		{
			real_point3d points[4];
			real_argb_color color;
			real shade = 0.45f + 0.55f * MAX(0.f, bot_scenario_box_normals[face][0] * light[0] +
				bot_scenario_box_normals[face][1] * light[1] + bot_scenario_box_normals[face][2] * light[2]);
			short vertex;

			/* (the floor's top is drawn as tiles below) */
			if (box->kind == _box_floor && face == 0)
				continue;
			for (vertex = 0; vertex < 4; vertex++)
				points[vertex] = corners[bot_scenario_box_faces[face][vertex]];
			bot_scenario_color(&color, base[0], base[1], base[2], shade);
			bot_scenario_draw_quad(points, &color);
		}
	}
	{
		short cell_x;
		short cell_y;

		for (cell_x = 0; cell_x < BOT_SCENARIO_CELLS_X; cell_x++)
		{
			for (cell_y = 0; cell_y < BOT_SCENARIO_CELLS_Y; cell_y++)
			{
				real_point3d points[4];
				real_argb_color color;
				real x0 = bot_scenario.center.x - BOT_SCENARIO_HALF_WIDTH + cell_x * BOT_SCENARIO_CELL;
				real y0 = bot_scenario.center.y - BOT_SCENARIO_HALF_DEPTH + cell_y * BOT_SCENARIO_CELL;
				real shade = (cell_x + cell_y) & 1 ? 0.92f : 1.08f;

				set_real_point3d(&points[0], x0, y0, bot_scenario.center.z);
				set_real_point3d(&points[1], x0 + BOT_SCENARIO_CELL, y0, bot_scenario.center.z);
				set_real_point3d(&points[2], x0 + BOT_SCENARIO_CELL, y0 + BOT_SCENARIO_CELL, bot_scenario.center.z);
				set_real_point3d(&points[3], x0, y0 + BOT_SCENARIO_CELL, bot_scenario.center.z);
				bot_scenario_color(&color, 0.5f, 0.53f, 0.56f, shade);
				bot_scenario_draw_quad(points, &color);
			}
		}
	}
}

struct halo_world_geometry const bot_scenario_geometry =
{
	.render = bot_scenario_render,
	.test_point = bot_scenario_test_point,
	.test_vector = bot_scenario_test_vector,
	.get_features = bot_scenario_get_features,
};

/* ---------- private code: the trials' setups */

static void bot_scenario_role(short slot, boolean puppet, real x, real y, real yaw_degrees)
{
	struct bot_scenario_role *role;

	assert(slot >= 0 && slot < BOT_SCENARIO_ROLES_MAX);
	role = &bot_scenario.setup.roles[slot];
	csmemset(role, 0, sizeof(*role));
	role->used = TRUE;
	role->puppet = puppet;
	role->x = x;
	role->y = y;
	role->yaw = DEGREES_TO_RADIANS(yaw_degrees);
	role->shield = 1.f;
	role->ammunition = NONE;
	if (slot + 1 > bot_scenario.setup.role_count)
		bot_scenario.setup.role_count = (short)(slot + 1);
}

static void bot_scenario_step(short slot, real x, real y, real seconds, unsigned short flags)
{
	struct bot_scenario_role *role = &bot_scenario.setup.roles[slot];
	struct bot_scenario_step *step;

	assert(role->used && role->step_count < BOT_SCENARIO_STEPS_MAX);
	step = &role->steps[role->step_count++];
	step->x = x;
	step->y = y;
	step->seconds = seconds;
	step->flags = flags;
}

/* Each scenario's places, scripts and conditions, in arena coordinates (x
east, y north, the center 0, 0; yaw degrees from east). Slot n is on team
n & 1: subjects on even slots, their targets on odd ones. */
static void bot_scenario_set_up(short scenario, short variant)
{
	struct bot_scenario_setup *setup = &bot_scenario.setup;

	csmemset(setup, 0, sizeof(*setup));
	setup->scenario = scenario;
	setup->variant = variant;
	setup->camera = bot_scenario.camera;
	setup->duration_ticks = 15 * TICKS_PER_SECOND;
	switch (scenario)
	{
	case _scenario_open_field:
	{
		/* a still, unarmed target in the open, 5, 10 or 15 units ahead */
		static real const distances[3] = { 5.f, 10.f, 15.f };
		real distance = distances[variant % 3];

		snprintf(setup->variant_name, sizeof(setup->variant_name), "distance %.0f", distance);
		bot_scenario_role(0, FALSE, -12.5f, 6.f, 0.f);
		bot_scenario_role(1, TRUE, -12.5f + distance, 6.f, 180.f);
		bot_scenario_step(1, -12.5f + distance, 6.f, 60.f, 0);
		setup->duration_ticks = 12 * TICKS_PER_SECOND;
		break;
	}
	case _scenario_line_of_sight:
		/* The target shows itself in the open north until seen, walks round
		the north return behind the central wall, waits, then moves along
		behind it: does the bot pursue where it last saw it, and find it? */
		snprintf(setup->variant_name, sizeof(setup->variant_name), "hide and relocate");
		bot_scenario_role(0, FALSE, -6.f, 0.f, 0.f);
		bot_scenario_role(1, TRUE, -2.f, 8.5f, 180.f);
		setup->roles[1].sustain = TRUE;
		bot_scenario_step(1, -2.f, 8.5f, 1.5f, _step_until_seen);
		bot_scenario_step(1, 5.f, 5.f, 0.f, 0);
		bot_scenario_step(1, 5.f, 2.f, 0.f, 0);
		bot_scenario_step(1, 2.5f, 1.5f, 3.f, 0);
		bot_scenario_step(1, 2.5f, -1.5f, 60.f, 0);
		setup->duration_ticks = 25 * TICKS_PER_SECOND;
		break;
	case _scenario_route:
		/* no enemy: walk its own route round the central wall and returns */
		if (variant % 2 == 0)
		{
			snprintf(setup->variant_name, sizeof(setup->variant_name), "west to east");
			bot_scenario_role(0, FALSE, -9.f, 0.f, 0.f);
			setup->goal_x = 9.f;
		}
		else
		{
			snprintf(setup->variant_name, sizeof(setup->variant_name), "east to west");
			bot_scenario_role(0, FALSE, 9.f, 0.f, 180.f);
			setup->goal_x = -9.f;
		}
		setup->goal_valid = TRUE;
		setup->goal_y = 0.f;
		setup->duration_ticks = 25 * TICKS_PER_SECOND;
		break;
	case _scenario_low_shields:
	{
		/* a target that stands and shoots; the bot starts with full, 40% or
		10% shields, low cover beside it */
		static real const shields[3] = { 0.1f, 0.4f, 1.f };

		snprintf(setup->variant_name, sizeof(setup->variant_name), "shields %.0f%%", shields[variant % 3] * 100.f);
		bot_scenario_role(0, FALSE, -10.5f, -5.2f, 0.f);
		setup->roles[0].shield = shields[variant % 3];
		bot_scenario_role(1, TRUE, -5.5f, -5.2f, 180.f);
		bot_scenario_step(1, -5.5f, -5.2f, 0.5f, 0);
		bot_scenario_step(1, -5.5f, -5.2f, 60.f, _step_fire);
		break;
	}
	case _scenario_pickup:
		/* a weapon on the floor beside a bot whose own are nearly empty (or,
		the second variant, full), an unarmed target in sight */
		bot_scenario_role(0, FALSE, -13.f, -8.f, 0.f);
		if (variant % 2 == 0)
		{
			snprintf(setup->variant_name, sizeof(setup->variant_name), "low ammunition");
			setup->roles[0].ammunition = 4;
		}
		else
			snprintf(setup->variant_name, sizeof(setup->variant_name), "full ammunition");
		bot_scenario_role(1, TRUE, -8.5f, -8.f, 180.f);
		setup->roles[1].sustain = TRUE;
		bot_scenario_step(1, -8.5f, -8.f, 60.f, 0);
		setup->pickup_wanted = TRUE;
		setup->pickup_x = -11.f;
		setup->pickup_y = -9.2f;
		break;
	case _scenario_team:
		/* bot A sees the first target; its teammate B faces away, and must
		learn of it (Spartans share sightings) or find it */
		snprintf(setup->variant_name, sizeof(setup->variant_name), "one sees, one does not");
		bot_scenario_role(0, FALSE, -11.5f, 7.f, 0.f);
		bot_scenario_role(1, TRUE, -2.f, 6.f, 180.f);
		bot_scenario_step(1, -2.f, 6.f, 60.f, 0);
		bot_scenario_role(2, FALSE, -10.f, 9.5f, 90.f);
		bot_scenario_role(3, TRUE, 8.5f, 7.f, 180.f);
		bot_scenario_step(3, 8.5f, 7.f, 60.f, 0);
		break;
	case _scenario_strafe:
		/* both kept alive: fifteen seconds of close combat against a target
		that shoots, standing still or stepping side to side */
		bot_scenario_role(0, FALSE, -12.5f, 6.f, 0.f);
		setup->roles[0].sustain = TRUE;
		bot_scenario_role(1, TRUE, -6.5f, 6.f, 180.f);
		setup->roles[1].sustain = TRUE;
		if (variant % 2 == 0)
		{
			snprintf(setup->variant_name, sizeof(setup->variant_name), "target still");
			bot_scenario_step(1, -6.5f, 6.f, 60.f, _step_fire);
		}
		else
		{
			snprintf(setup->variant_name, sizeof(setup->variant_name), "target strafing");
			bot_scenario_step(1, -6.5f, 4.5f, 0.f, _step_fire);
			bot_scenario_step(1, -6.5f, 7.5f, 0.f, _step_fire);
			setup->roles[1].loop = TRUE;
		}
		break;
	case _scenario_hidden_attacker:
		/* The target shoots the bot in the back for a second from beside the
		central wall's north end, then steps east behind the wall and on:
		while it is unseen, does the bot's idea of where it is move with it? */
		snprintf(setup->variant_name, sizeof(setup->variant_name), "shoot, hide, relocate");
		bot_scenario_role(0, FALSE, -6.f, -1.f, 180.f);
		/* (it keeps watch westward until shot: not turning about first) */
		setup->roles[0].hold_until_hurt = TRUE;
		bot_scenario_role(1, TRUE, -0.6f, 4.6f, -45.f);
		setup->roles[1].sustain = TRUE;
		bot_scenario_step(1, -0.6f, 4.6f, 0.3f, 0);
		bot_scenario_step(1, -0.6f, 4.6f, 1.f, _step_fire);
		bot_scenario_step(1, 1.4f, 4.25f, 0.f, 0);
		bot_scenario_step(1, 3.5f, 4.4f, 60.f, 0);
		setup->duration_ticks = 12 * TICKS_PER_SECOND;
		break;
	case _scenario_crowd:
	{
		/* everybody fights: two teams from either end */
		short slot;

		snprintf(setup->variant_name, sizeof(setup->variant_name), "%d bots", bot_scenario.crowd);
		for (slot = 0; slot < bot_scenario.crowd; slot++)
		{
			short rank = (short)(slot / 2);
			short rows = (short)((bot_scenario.crowd + 1) / 2);
			real y = rows > 1 ? -9.f + 18.f * (real)rank / (real)(rows - 1) : 0.f;

			if (slot & 1)
				bot_scenario_role(slot, FALSE, 13.f - (rank % 2) * 1.5f, y, 180.f);
			else
				bot_scenario_role(slot, FALSE, -13.f + (rank % 2) * 1.5f, y, 0.f);
		}
		setup->duration_ticks = 30 * TICKS_PER_SECOND;
		if (setup->camera == _camera_follow)
			setup->camera = _camera_overview;
		break;
	}
	default:
		break;
	}
}

static short bot_scenario_role_count(short scenario)
{
	switch (scenario)
	{
	case _scenario_route: return 1;
	case _scenario_team: return 4;
	case _scenario_crowd: return bot_scenario.crowd;
	case _scenario_objective: return 0;
	default: return 2;
	}
}

/* ---------- private code: placing a trial */

static void bot_scenario_hold_action(short slot, struct player_action *action)
{
	long unit_index;
	struct unit_datum *unit = bot_scenario_slot_unit(slot, &unit_index);

	csmemset(action, 0, sizeof(*action));
	action->desired_weapon_index = NONE;
	action->desired_grenade_index = NONE;
	action->desired_zoom_level = NONE;
	if ((bot_scenario.phase == _phase_settle || bot_scenario.phase == _phase_run) &&
		slot >= 0 && slot < BOT_SCENARIO_ROLES_MAX && bot_scenario.setup.roles[slot].used)
	{
		action->desired_facing.yaw = bot_scenario.setup.roles[slot].yaw;
		action->desired_facing.pitch = 0.f;
	}
	else if (unit)
		euler_angles2d_from_vector3d(&action->desired_facing, &unit->unit.desired_aiming_vector);
}

/* a loose weapon (nobody's, on the map) of a kind the subject does not carry,
moved to the pickup's place; NONE if there is none (at most 256 examined) */
static long bot_scenario_place_pickup(struct unit_datum const *subject)
{
	struct object_iterator iterator;
	short examined = 0;
	long best = NONE;

	object_iterator_new(&iterator, _object_mask_weapon, 0);
	while (examined++ < 256 && object_iterator_next(&iterator))
	{
		struct weapon_datum *weapon = weapon_try_and_get(iterator.index);
		boolean carried = FALSE;
		short slot;

		if (!weapon || weapon->object.parent_object_index != NONE)
			continue;
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			struct weapon_datum *own = weapon_try_and_get(subject->unit.weapon_object_indices[slot]);

			if (own && own->definition_index == weapon->definition_index)
				carried = TRUE;
		}
		if (!carried)
		{
			best = iterator.index;
			break;
		}
	}
	if (best != NONE)
	{
		real_point3d point;
		struct weapon_datum *weapon = weapon_try_and_get(best);

		bot_scenario_world_point(bot_scenario.setup.pickup_x, bot_scenario.setup.pickup_y, &point);
		point.z += 0.15f;
		object_set_position(best, &point, NULL, NULL);
		weapon->object.translational_velocity = *global_zero_vector3d;
		weapon->object.angular_velocity = *global_zero_vector3d;
	}
	return best;
}

/* the readied weapon down to `rounds`, none spare; the other weapon empty */
static void bot_scenario_set_ammunition(struct unit_datum *unit, short rounds)
{
	short slot;

	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		struct weapon_datum *weapon = weapon_try_and_get(unit->unit.weapon_object_indices[slot]);
		short magazine;

		if (!weapon)
			continue;
		for (magazine = 0; magazine < (short)NUMBEROF(weapon->weapon.magazines); magazine++)
		{
			weapon->weapon.magazines[magazine].rounds_loaded =
				(short)(slot == unit->unit.current_weapon_index && magazine == 0 ? rounds : 0);
			weapon->weapon.magazines[magazine].rounds_total = 0;
		}
	}
}

static void bot_scenario_reset_metrics(void)
{
	short slot;

	for (slot = 0; slot < BOT_SCENARIO_BOTS_MAX; slot++)
	{
		struct bot_scenario_metrics *metrics = &bot_scenario.metrics[slot];

		csmemset(metrics, 0, sizeof(*metrics));
		metrics->first_visible = metrics->first_on_target = metrics->first_fire = NONE;
		metrics->first_hit = metrics->first_shared = metrics->first_retreat = NONE;
		metrics->lost_sight = metrics->reacquired = metrics->arrived = NONE;
		metrics->unseen_since = NONE;
		metrics->died = metrics->killed_target = NONE;
		metrics->last_target = NONE;
	}
	bot_scenario.milliseconds_sum = 0.0;
	bot_scenario.milliseconds_maximum = 0.0;
	bot_scenario.milliseconds_ticks = 0;
	bot_scenario.observed = FALSE;
}

static void bot_scenario_log_trial(void)
{
	struct bot_scenario_setup const *setup = &bot_scenario.setup;
	short slot;

	if (!bot_scenario.log)
		return;
	fprintf(bot_scenario.log, "{\"type\":\"trial\",\"trial\":%ld,\"scenario\":\"%s\",\"name\":\"%s\","
		"\"skill\":\"%s\",\"variant\":%d,\"variant_name\":\"%s\",\"seed\":%lu,\"tick\":%ld,"
		"\"duration_ticks\":%ld,\"roles\":[",
		bot_scenario.trial_number, bot_scenario_definitions[setup->scenario].id,
		bot_scenario_definitions[setup->scenario].name,
		bot_scenario_skill_names[bot_scenario.skills[bot_scenario.skill_cursor]], setup->variant,
		setup->variant_name, (unsigned long)bot_scenario.trial_seed, bot_scenario.trial_start,
		setup->duration_ticks);
	for (slot = 0; slot < setup->role_count; slot++)
	{
		struct bot_scenario_role const *role = &setup->roles[slot];

		if (!role->used)
			continue;
		fprintf(bot_scenario.log, "%s{\"slot\":%d,\"kind\":\"%s\",\"team\":%d,\"x\":%.2f,\"y\":%.2f,"
			"\"yaw\":%.1f,\"shield\":%.2f,\"sustain\":%d,\"ammunition\":%d}",
			slot ? "," : "", slot, role->puppet ? "target" : "subject", slot & 1, role->x, role->y,
			RADIANS_TO_DEGREES(role->yaw), role->shield, role->sustain ? 1 : 0, role->ammunition);
	}
	fputs("],\"goal\":", bot_scenario.log);
	if (setup->goal_valid)
	{
		real_point3d goal;

		bot_scenario_world_point(setup->goal_x, setup->goal_y, &goal);
		bot_scenario_log_point(&goal);
	}
	else
		fputs("null", bot_scenario.log);
	fprintf(bot_scenario.log, ",\"pickup_weapon\":%ld}\n",
		bot_scenario.pickup_weapon_index == NONE ? -1L : (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(bot_scenario.pickup_weapon_index));
}

/* The last trials' bodies and dropped weapons on the arena go, so that no
trial starts with something to pick up or step round that it did not place
(at most 512 objects examined; a held weapon is never one). */
static void bot_scenario_clear_arena(void)
{
	struct object_iterator iterator;
	short examined = 0;
	short deleted = 0;

	object_iterator_new(&iterator, _object_mask_weapon | _object_mask_biped, 0);
	while (examined++ < 512 && object_iterator_next(&iterator))
	{
		struct object_datum *object = object_try_and_get(iterator.index);
		real_point3d origin;

		if (!object || object->object.parent_object_index != NONE)
			continue;
		if (object->object.type == _object_type_biped &&
			!TEST_FLAG(object->object.damage_flags, _object_dead_bit))
		{
			continue;
		}
		object_get_origin(iterator.index, &origin);
		if (origin.x < bot_scenario.center.x - BOT_SCENARIO_HALF_WIDTH ||
			origin.x > bot_scenario.center.x + BOT_SCENARIO_HALF_WIDTH ||
			origin.y < bot_scenario.center.y - BOT_SCENARIO_HALF_DEPTH ||
			origin.y > bot_scenario.center.y + BOT_SCENARIO_HALF_DEPTH ||
			origin.z < bot_scenario.center.z - 1.f || origin.z > bot_scenario.center.z + 4.f)
		{
			continue;
		}
		object_delete(iterator.index);
		deleted++;
	}
	if (deleted > 0)
		platform_log("bot scenario: cleared %d bodies and weapons from the arena", deleted);
}

/* every used slot's bot alive: place them, set them up, and start holding */
static boolean bot_scenario_place(void)
{
	struct bot_scenario_setup *setup = &bot_scenario.setup;
	long now = game_time_get();
	short slot;

	for (slot = 0; slot < setup->role_count; slot++)
	{
		if (setup->roles[slot].used && !bot_scenario_slot_unit(slot, NULL))
			return FALSE;
	}
	bot_scenario.trial_seed = bot_scenario.seed * 2654435761u + (uint32_t)bot_scenario.trial_number * 40503u;
	bot_scenario.random = bot_scenario.trial_seed | 1u;
	if (!bots_scenario_set_skill(bot_scenario_skill_names[bot_scenario.skills[bot_scenario.skill_cursor]]))
		platform_log("bot scenario: skill not set");
	bot_scenario_reset_metrics();
	bots_scenario_forget();
	bot_scenario_clear_arena();
	bot_scenario.pickup_weapon_index = NONE;
	for (slot = 0; slot < BOT_SCENARIO_BOTS_MAX; slot++)
	{
		struct bot_scenario_agent *agent = &bot_scenario.agents[slot];

		csmemset(agent, 0, sizeof(*agent));
		agent->player_index = NONE;
		agent->unit_index = NONE;
		agent->hold_start = NONE;
		agent->seen_since = NONE;
	}
	for (slot = 0; slot < setup->role_count; slot++)
	{
		struct bot_scenario_role const *role = &setup->roles[slot];
		struct bot_scenario_agent *agent = &bot_scenario.agents[slot];
		struct unit_datum *unit;
		real_point3d point;
		real_vector3d forward;
		real_vector3d up;

		if (!role->used)
			continue;
		unit = bot_scenario_slot_unit(slot, &agent->unit_index);
		agent->player_index = bots_scenario_player_index(slot);
		/* (a seat would carry the unit with its vehicle) */
		if (unit->object.parent_object_index != NONE)
			unit_try_and_exit_seat(agent->unit_index);
		bot_scenario_world_point(role->x, role->y, &point);
		point.z += 0.05f;
		set_real_vector3d(&forward, cosine(role->yaw), sine(role->yaw), 0.f);
		set_real_vector3d(&up, 0.f, 0.f, 1.f);
		object_set_position(agent->unit_index, &point, &forward, &up);
		unit->object.translational_velocity = *global_zero_vector3d;
		unit->unit.desired_aiming_vector = forward;
		unit->object.body_vitality = 1.f;
		unit->object.shield_vitality = PIN(role->shield, 0.f, 1.f);
		agent->vitality = unit->object.body_vitality + unit->object.shield_vitality;
		agent->aim_error_time = 0;
		if (role->ammunition != NONE)
			bot_scenario_set_ammunition(unit, role->ammunition);
		bots_scenario_prepare(slot, role->yaw, bot_scenario.trial_seed + (uint32_t)slot * 7919u);
		bot_scenario.metrics[slot].last_position = point;
	}
	if (setup->pickup_wanted)
	{
		struct unit_datum *subject = bot_scenario_slot_unit(0, NULL);

		bot_scenario.pickup_weapon_index = subject ? bot_scenario_place_pickup(subject) : NONE;
		if (bot_scenario.pickup_weapon_index == NONE)
			platform_log("bot scenario: no loose weapon on the map for the pickup");
	}
	bot_scenario.trial_start = now + BOT_SCENARIO_SETTLE_TICKS;
	bot_scenario.end_reason = NULL;
	bot_scenario.end_tick = NONE;
	bot_scenario.camera_valid = FALSE;
	bot_scenario_log_trial();
	platform_log("bot scenario: trial %ld %s (%s), %s, %s, seed %lu", bot_scenario.trial_number,
		bot_scenario_definitions[setup->scenario].id, bot_scenario_definitions[setup->scenario].name,
		bot_scenario_skill_names[bot_scenario.skills[bot_scenario.skill_cursor]], setup->variant_name,
		(unsigned long)bot_scenario.trial_seed);
	return TRUE;
}

/* ---------- private code: the scripted targets */

/* the nearest living subject the target at `eye` can see, NONE if none */
static short bot_scenario_target_subject(short slot, real_point3d const *eye, long ignore_index,
	real_point3d *chest, boolean *visible)
{
	short best = NONE;
	short other;
	real best_distance = 0.f;
	boolean best_visible = FALSE;

	for (other = 0; other < bot_scenario.setup.role_count; other++)
	{
		struct bot_scenario_role const *role = &bot_scenario.setup.roles[other];
		long unit_index;
		real_point3d point;
		real distance;
		boolean clear;

		if (other == slot || !role->used || role->puppet || (other & 1) == (slot & 1) ||
			!bot_scenario_slot_unit(other, &unit_index))
		{
			continue;
		}
		bot_scenario_chest(unit_index, &point);
		distance = distance3d(eye, &point);
		clear = bot_scenario_line_clear(eye, &point, ignore_index);
		/* (one in sight before any other; then the nearest) */
		if (best == NONE || (clear && !best_visible) || (clear == best_visible && distance < best_distance))
		{
			best = other;
			best_distance = distance;
			best_visible = clear;
			*chest = point;
		}
	}
	*visible = best_visible;
	return best;
}

/* whether any subject of the other team sees the target (its decisions say so) */
static boolean bot_scenario_seen_by_subject(long player_index)
{
	short slot;

	for (slot = 0; slot < bot_scenario.setup.role_count; slot++)
	{
		struct bot_scenario_metrics const *metrics = &bot_scenario.metrics[slot];

		if (bot_scenario.setup.roles[slot].used && !bot_scenario.setup.roles[slot].puppet &&
			metrics->have_last && metrics->was_visible && metrics->last_target == player_index)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* A scripted target's tick: it walks straight to its step's place (the
scripts' paths are clear of the walls), stays as long as the step says, and
always faces the nearest subject, shooting when the step says so and it has
a clear line to it, with a small aim error. */
static void bot_scenario_puppet_action(short slot, struct player_action *action)
{
	struct bot_scenario_role const *role = &bot_scenario.setup.roles[slot];
	struct bot_scenario_agent *agent = &bot_scenario.agents[slot];
	long now = game_time_get();
	long unit_index;
	struct unit_datum *unit = bot_scenario_slot_unit(slot, &unit_index);
	struct bot_scenario_step const *step;
	real_point3d origin;
	real_point3d eye;
	real_point3d chest;
	real_point3d goal;
	boolean visible = FALSE;
	short subject;
	real move_yaw = 0.f;
	boolean moving = FALSE;

	csmemset(action, 0, sizeof(*action));
	action->desired_weapon_index = NONE;
	action->desired_grenade_index = NONE;
	action->desired_zoom_level = NONE;
	if (!unit || role->step_count <= 0)
		return;
	if (agent->step >= role->step_count)
		agent->step = (short)(role->loop ? 0 : role->step_count - 1);
	step = &role->steps[agent->step];
	object_get_origin(unit_index, &origin);
	unit_get_camera_position(unit_index, &eye);
	bot_scenario_world_point(step->x, step->y, &goal);
	if (!agent->holding)
	{
		if (bot_scenario_horizontal_distance(&origin, &goal) <= BOT_SCENARIO_STEP_REACHED)
		{
			agent->holding = TRUE;
			agent->hold_start = now;
			agent->seen_since = NONE;
		}
		else
		{
			moving = TRUE;
			move_yaw = arctangent(goal.y - origin.y, goal.x - origin.x);
		}
	}
	if (agent->holding)
	{
		boolean done;

		if ((step->flags & _step_until_seen) != 0)
		{
			if (bot_scenario_seen_by_subject(agent->player_index))
			{
				if (agent->seen_since == NONE)
					agent->seen_since = now;
			}
			else
				agent->seen_since = NONE;
			done = (agent->seen_since != NONE && now - agent->seen_since >= bot_scenario_ticks(step->seconds)) ||
				now - agent->hold_start >= BOT_SCENARIO_SEEN_WAIT_TICKS;
		}
		else
			done = now - agent->hold_start >= bot_scenario_ticks(step->seconds);
		if (done && (agent->step + 1 < role->step_count || role->loop))
		{
			agent->step = (short)((agent->step + 1) % role->step_count);
			agent->holding = FALSE;
		}
	}
	subject = bot_scenario_target_subject(slot, &eye, unit_index, &chest, &visible);
	if (subject != NONE)
	{
		real_vector3d vector;

		if (now >= agent->aim_error_time)
		{
			agent->aim_error.yaw = DEGREES_TO_RADIANS(bot_scenario_random_range(-1.5f, 1.5f));
			agent->aim_error.pitch = DEGREES_TO_RADIANS(bot_scenario_random_range(-1.f, 1.f));
			agent->aim_error_time = now + bot_scenario_ticks(0.4f);
		}
		vector_from_points3d(&eye, &chest, &vector);
		euler_angles2d_from_vector3d(&action->desired_facing, &vector);
		action->desired_facing.yaw = bot_scenario_angle_normalize(action->desired_facing.yaw + agent->aim_error.yaw);
		action->desired_facing.pitch += agent->aim_error.pitch;
	}
	else
	{
		action->desired_facing.yaw = role->yaw;
		action->desired_facing.pitch = 0.f;
	}
	if (moving)
	{
		real relative = move_yaw - action->desired_facing.yaw;

		action->throttle.i = cosine(relative);
		action->throttle.j = sine(relative);
	}
	if ((step->flags & _step_crouch) != 0)
		action->control_flags |= FLAG(_unit_control_crouch_modifier_bit);
	agent->fired = (step->flags & _step_fire) != 0 && subject != NONE && visible;
	if (agent->fired)
	{
		action->primary_trigger = 1.f;
		action->control_flags |= FLAG(_unit_control_weapon_primary_trigger_bit);
	}
}

/* ---------- private code: a running trial */

static void bot_scenario_log_event(char const *type, long attacker, short victim, real amount)
{
	if (!bot_scenario.log)
		return;
	fprintf(bot_scenario.log, "{\"type\":\"%s\",\"trial\":%ld,\"t\":%ld,\"attacker\":%d,\"victim\":%d,\"amount\":",
		type, bot_scenario.trial_number, game_time_get() - bot_scenario.trial_start,
		(int)bot_scenario_slot_of_player(attacker), victim);
	bot_scenario_log_real(amount);
	fputs("}\n", bot_scenario.log);
}

/* the player who hurt the unit last, NONE if nobody */
static long bot_scenario_last_attacker(struct unit_datum const *unit)
{
	unsigned long latest = 0;
	long player_index = NONE;
	short index;

	for (index = 0; index < MAXIMUM_ATTACKERS_PER_UNIT; index++)
	{
		struct unit_attacker const *attacker = &unit->unit.attackers[index];

		if (attacker->player_index != NONE && attacker->game_time_stamp >= latest)
		{
			latest = attacker->game_time_stamp;
			player_index = attacker->player_index;
		}
	}
	return player_index;
}

/* each running tick, before the bots decide: damage and deaths since last
tick (attributed to the unit's last attacker), sustained bots restored */
static void bot_scenario_track_damage(void)
{
	long now = game_time_get();
	short slot;

	for (slot = 0; slot < bot_scenario.setup.role_count; slot++)
	{
		struct bot_scenario_role const *role = &bot_scenario.setup.roles[slot];
		struct bot_scenario_agent *agent = &bot_scenario.agents[slot];
		struct unit_datum *unit;
		long unit_index;
		real vitality;

		if (!role->used || agent->dead)
			continue;
		unit = bot_scenario_slot_unit(slot, &unit_index);
		if (!unit || unit_index != agent->unit_index)
		{
			/* (dead: its old unit tells who killed it) */
			struct unit_datum *corpse = agent->unit_index != NONE ? unit_try_and_get(agent->unit_index) : NULL;
			long killer = corpse ? bot_scenario_last_attacker(corpse) : NONE;
			short killer_slot = bot_scenario_slot_of_player(killer);

			agent->dead = TRUE;
			bot_scenario.metrics[slot].died = now - bot_scenario.trial_start;
			if (killer_slot != NONE && killer_slot < BOT_SCENARIO_BOTS_MAX)
			{
				bot_scenario.metrics[killer_slot].kills++;
				if (bot_scenario.metrics[killer_slot].killed_target == NONE)
					bot_scenario.metrics[killer_slot].killed_target = now - bot_scenario.trial_start;
			}
			bot_scenario_log_event("death", killer, slot, agent->vitality);
			continue;
		}
		vitality = unit->object.body_vitality + unit->object.shield_vitality;
		if (vitality < agent->vitality - 0.001f)
		{
			long attacker = bot_scenario_last_attacker(unit);
			short attacker_slot = bot_scenario_slot_of_player(attacker);
			real amount = agent->vitality - vitality;

			bot_scenario.metrics[slot].damage_taken += amount;
			agent->hurt = TRUE;
			if (attacker_slot != NONE && attacker_slot < BOT_SCENARIO_BOTS_MAX)
			{
				struct bot_scenario_metrics *metrics = &bot_scenario.metrics[attacker_slot];

				metrics->hits++;
				metrics->damage_dealt += amount;
				if (metrics->first_hit == NONE)
					metrics->first_hit = now - bot_scenario.trial_start;
			}
			bot_scenario_log_event("hit", attacker, slot, amount);
		}
		if (role->sustain)
		{
			unit->object.body_vitality = 1.f;
			unit->object.shield_vitality = 1.f;
		}
		agent->vitality = unit->object.body_vitality + unit->object.shield_vitality;
	}
}

static void bot_scenario_log_puppets(void)
{
	short slot;

	if (!bot_scenario.log)
		return;
	for (slot = 0; slot < bot_scenario.setup.role_count; slot++)
	{
		long unit_index;
		real_point3d origin;

		if (!bot_scenario.setup.roles[slot].used || !bot_scenario.setup.roles[slot].puppet ||
			!bot_scenario_slot_unit(slot, &unit_index))
		{
			continue;
		}
		bot_scenario_chest(unit_index, &origin);
		fprintf(bot_scenario.log, "{\"type\":\"target\",\"trial\":%ld,\"t\":%ld,\"slot\":%d,\"chest\":",
			bot_scenario.trial_number, game_time_get() - bot_scenario.trial_start, slot);
		bot_scenario_log_point(&origin);
		fprintf(bot_scenario.log, ",\"step\":%d,\"fire\":%d}\n", bot_scenario.agents[slot].step,
			bot_scenario.agents[slot].fired ? 1 : 0);
	}
}

/* why the trial ends now, NULL to go on */
static char const *bot_scenario_end_condition(void)
{
	struct bot_scenario_setup const *setup = &bot_scenario.setup;
	long elapsed = game_time_get() - bot_scenario.trial_start;
	struct bot_scenario_metrics const *subject = &bot_scenario.metrics[0];
	short slot;
	short puppets = 0;
	short dead_puppets = 0;

	if (elapsed >= setup->duration_ticks)
		return "timeout";
	if (setup->scenario == _scenario_crowd)
		return NULL;
	for (slot = 0; slot < setup->role_count; slot++)
	{
		if (!setup->roles[slot].used)
			continue;
		if (setup->roles[slot].puppet)
		{
			puppets++;
			if (bot_scenario.agents[slot].dead)
				dead_puppets++;
		}
		else if (bot_scenario.agents[slot].dead)
			return "subject died";
	}
	if (puppets > 0 && dead_puppets == puppets)
		return "targets killed";
	switch (setup->scenario)
	{
	case _scenario_route:
		if (subject->arrived != NONE)
			return "arrived";
		break;
	case _scenario_line_of_sight:
	case _scenario_hidden_attacker:
		if (subject->reacquired != NONE && elapsed - subject->reacquired >= 2 * TICKS_PER_SECOND)
			return "reacquired";
		break;
	case _scenario_pickup:
		if (bot_scenario.pickup_weapon_index != NONE)
		{
			struct unit_datum *unit = bot_scenario_slot_unit(0, NULL);

			if (unit)
			{
				short weapon_slot;

				for (weapon_slot = 0; weapon_slot < MAXIMUM_WEAPONS_PER_UNIT; weapon_slot++)
				{
					if (unit->unit.weapon_object_indices[weapon_slot] == bot_scenario.pickup_weapon_index)
						return "picked up";
				}
			}
		}
		break;
	}
	return NULL;
}

static void bot_scenario_log_result(void)
{
	struct bot_scenario_setup const *setup = &bot_scenario.setup;
	short slot;
	boolean first = TRUE;

	if (!bot_scenario.log)
		return;
	fprintf(bot_scenario.log, "{\"type\":\"result\",\"trial\":%ld,\"scenario\":\"%s\",\"skill\":\"%s\","
		"\"variant\":%d,\"variant_name\":\"%s\",\"reason\":\"%s\",\"ticks\":%ld,\"ai_ms_mean\":",
		bot_scenario.trial_number, bot_scenario_definitions[setup->scenario].id,
		bot_scenario_skill_names[bot_scenario.skills[bot_scenario.skill_cursor]], setup->variant,
		setup->variant_name, bot_scenario.end_reason ? bot_scenario.end_reason : "?",
		bot_scenario.end_tick - bot_scenario.trial_start);
	bot_scenario_log_real(bot_scenario.milliseconds_ticks ?
		(real)(bot_scenario.milliseconds_sum / (double)bot_scenario.milliseconds_ticks) : 0.f);
	fputs(",\"ai_ms_max\":", bot_scenario.log);
	bot_scenario_log_real((real)bot_scenario.milliseconds_maximum);
	fputs(",\"subjects\":[", bot_scenario.log);
	for (slot = 0; slot < setup->role_count; slot++)
	{
		struct bot_scenario_metrics const *m = &bot_scenario.metrics[slot];

		if (!setup->roles[slot].used || setup->roles[slot].puppet)
			continue;
		fprintf(bot_scenario.log, "%s{\"slot\":%d,\"first_visible\":", first ? "" : ",", slot);
		first = FALSE;
		bot_scenario_log_tick(m->first_visible);
		fputs(",\"first_on_target\":", bot_scenario.log);
		bot_scenario_log_tick(m->first_on_target);
		fputs(",\"first_fire\":", bot_scenario.log);
		bot_scenario_log_tick(m->first_fire);
		fputs(",\"first_hit\":", bot_scenario.log);
		bot_scenario_log_tick(m->first_hit);
		fputs(",\"first_shared\":", bot_scenario.log);
		bot_scenario_log_tick(m->first_shared);
		fputs(",\"first_retreat\":", bot_scenario.log);
		bot_scenario_log_tick(m->first_retreat);
		fputs(",\"lost_sight\":", bot_scenario.log);
		bot_scenario_log_tick(m->lost_sight);
		fputs(",\"reacquired\":", bot_scenario.log);
		bot_scenario_log_tick(m->reacquired);
		fputs(",\"arrived\":", bot_scenario.log);
		bot_scenario_log_tick(m->arrived);
		fputs(",\"died\":", bot_scenario.log);
		bot_scenario_log_tick(m->died);
		fputs(",\"killed_target\":", bot_scenario.log);
		bot_scenario_log_tick(m->killed_target);
		fprintf(bot_scenario.log, ",\"visible_ticks\":%ld,\"fire_ticks\":%ld,\"hits\":%ld,\"kills\":%ld,"
			"\"crouch_ticks\":%ld,\"jumps\":%ld,\"strafe_changes\":%ld,\"leaf_changes\":%ld,\"retreat_ticks\":%ld,"
			"\"hidden_updates\":%ld,\"unseen_exact_ticks\":%ld,\"stalled_ticks\":%ld,\"route_ticks\":%ld,"
			"\"damage_dealt\":",
			m->visible_ticks, m->fire_ticks, m->hits, m->kills, m->crouch_ticks, m->jumps, m->strafe_changes,
			m->leaf_changes, m->retreat_ticks, m->hidden_updates, m->unseen_exact_ticks, m->stalled_ticks,
			m->route_ticks);
		bot_scenario_log_real(m->damage_dealt);
		fputs(",\"damage_taken\":", bot_scenario.log);
		bot_scenario_log_real(m->damage_taken);
		fputs(",\"max_retreat_score\":", bot_scenario.log);
		bot_scenario_log_real(m->max_retreat_score);
		fputs(",\"distance\":", bot_scenario.log);
		bot_scenario_log_real(m->distance);
		fputs(",\"hidden_drift\":", bot_scenario.log);
		bot_scenario_log_real(m->hidden_drift);
		fputs(",\"mean_aim_error\":", bot_scenario.log);
		if (m->aim_samples > 0)
			bot_scenario_log_real(m->aim_error_sum / (real)m->aim_samples);
		else
			fputs("null", bot_scenario.log);
		fputc('}', bot_scenario.log);
	}
	fputs("]}\n", bot_scenario.log);
	fflush(bot_scenario.log);
}

/* the next scenario, skill or trial; FALSE when every one has run */
static boolean bot_scenario_advance(void)
{
	bot_scenario.trial_cursor++;
	if (bot_scenario.trial_cursor < bot_scenario.trials)
		return TRUE;
	bot_scenario.trial_cursor = 0;
	bot_scenario.skill_cursor++;
	if (bot_scenario.skill_cursor < bot_scenario.skill_count)
		return TRUE;
	bot_scenario.skill_cursor = 0;
	bot_scenario.scenario_cursor++;
	return bot_scenario.scenario_cursor < bot_scenario.scenario_count;
}

static void bot_scenario_set_phase(short phase)
{
	bot_scenario.phase = phase;
	bot_scenario.phase_time = game_time_get();
}

static void bot_scenario_begin_trial(void)
{
	short scenario = bot_scenario.scenarios[bot_scenario.scenario_cursor];

	bot_scenario.trial_number++;
	bot_scenario_set_up(scenario, bot_scenario.trial_cursor);
	bot_scenario_set_phase(_phase_gather);
}

/* ---------- public code */

void bot_scenario_initialize_for_new_map(void)
{
	static boolean const supported[BOT_SCENARIO_COUNT] =
		{ TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, TRUE, TRUE };
	char const *ids[BOT_SCENARIO_COUNT];
	char const *path = config_string("debug.bot_scenario_log");
	char const *camera = config_string("debug.bot_scenario_camera");
	long trials = config_integer("debug.bot_scenario_trials");
	long crowd = config_integer("debug.bot_scenario_crowd");
	short index;
	short maximum = 0;

	bot_scenario_log_close();
	csmemset(&bot_scenario, 0, sizeof(bot_scenario));
	bot_scenario.pickup_weapon_index = NONE;
	for (index = 0; index < BOT_SCENARIO_COUNT; index++)
		ids[index] = bot_scenario_definitions[index].id;
	bot_scenario.requested = config_string("debug.bot_scenario")[0] != 0;
	if (!bot_scenario.requested)
		return;
	bot_scenario.scenario_count = bot_scenario_parse_list(config_string("debug.bot_scenario"), ids,
		BOT_SCENARIO_COUNT, supported, bot_scenario.scenarios, BOT_SCENARIO_COUNT);
	bot_scenario.skill_count = bot_scenario_parse_list(config_string("debug.bot_scenario_skills"),
		bot_scenario_skill_names, BOT_SCENARIO_SKILLS_MAX, NULL, bot_scenario.skills, BOT_SCENARIO_SKILLS_MAX);
	if (bot_scenario.skill_count == 0)
	{
		bot_scenario.skills[0] = 3;
		bot_scenario.skill_count = 1;
	}
	bot_scenario.trials = (short)PIN(trials, 1, BOT_SCENARIO_TRIALS_MAX);
	bot_scenario.seed = (uint32_t)config_integer("debug.bot_scenario_seed");
	bot_scenario.crowd = (short)PIN(crowd, 2, BOT_SCENARIO_BOTS_MAX);
	bot_scenario.exit_when_done = config_boolean("debug.bot_scenario_exit") != 0;
	bot_scenario.camera = _camera_follow;
	if (camera && !strcmp(camera, "overview"))
		bot_scenario.camera = _camera_overview;
	else if (camera && !strcmp(camera, "first_person"))
		bot_scenario.camera = _camera_first_person;
	else if (camera && !strcmp(camera, "off"))
		bot_scenario.camera = _camera_off;
	/* (in the data root, as every game file is; never a map's name) */
	if (path && path[0])
	{
		size_t length = strlen(path);

		if (length >= sizeof(bot_scenario.log_path) || length < 7 || strcmp(path + length - 6, ".jsonl"))
			platform_log("bot scenario: debug.bot_scenario_log must name a .jsonl file; no telemetry");
		else
		{
			csstrncpy(bot_scenario.log_path, path, sizeof(bot_scenario.log_path) - 1);
			bot_scenario.log_path[sizeof(bot_scenario.log_path) - 1] = 0;
		}
	}
	for (index = 0; index < bot_scenario.scenario_count; index++)
		maximum = MAX(maximum, bot_scenario_role_count(bot_scenario.scenarios[index]));
	bot_scenario.bot_count = maximum;
	bot_scenario.enabled = bot_scenario.scenario_count > 0 && maximum > 0;
	if (!bot_scenario.enabled)
	{
		platform_log("bot scenario: no runnable scenario in '%s' (S08 needs CTF and is not run)",
			config_string("debug.bot_scenario"));
		return;
	}
	platform_log("bot scenario: %d scenarios x %d skills x %d trials, %d bots, seed %lu, camera %s",
		bot_scenario.scenario_count, bot_scenario.skill_count, bot_scenario.trials, bot_scenario.bot_count,
		(unsigned long)bot_scenario.seed, camera && camera[0] ? camera : "follow");
}

void bot_scenario_dispose_from_old_map(void)
{
	bot_scenario_log_close();
	bot_scenario.arena_active = FALSE;
	bot_scenario.enabled = FALSE;
	bot_scenario.phase = _phase_off;
}

boolean bot_scenario_enabled(void)
{
	return bot_scenario.enabled;
}

short bot_scenario_bot_count(void)
{
	return bot_scenario.enabled ? bot_scenario.bot_count : 0;
}

char bot_scenario_team(short slot)
{
	return (char)(slot & 1);
}

boolean bot_scenario_ignores_player(long player_index)
{
	short slot;

	if (!bot_scenario.enabled)
		return FALSE;
	slot = bot_scenario_slot_of_player(player_index);
	/* (the host's own, and the bots on the bench) */
	return slot == NONE || slot >= bot_scenario.setup.role_count || !bot_scenario.setup.roles[slot].used;
}

void bot_scenario_update(void)
{
	long now = game_time_get();

	if (!bot_scenario.enabled || !game_engine_running())
		return;
	switch (bot_scenario.phase)
	{
	case _phase_off:
		if (!bot_scenario_build_arena())
		{
			bot_scenario.enabled = FALSE;
			return;
		}
		snprintf(bot_scenario.status, sizeof(bot_scenario.status), "building routes over the arena");
		bot_scenario_set_phase(_phase_arena);
		break;
	case _phase_arena:
	{
		struct sn_resource const *resource = navigation_world_resource();

		if (resource && resource->key.overlay_revision == halo_extensions_world_edit_revision())
		{
			bot_scenario_log_open();
			platform_log("bot scenario: routes ready (%u polygons, arena's %d)", resource->polygon_count,
				BOT_SCENARIO_CELLS);
			bot_scenario_begin_trial();
		}
		else if (now - bot_scenario.phase_time > BOT_SCENARIO_NAVIGATION_TICKS)
		{
			platform_log("bot scenario: routes over the arena were not built; trials run without them");
			bot_scenario_log_open();
			bot_scenario_begin_trial();
		}
		break;
	}
	case _phase_gather:
		snprintf(bot_scenario.status, sizeof(bot_scenario.status), "waiting for the trial's bots to spawn");
		if (bot_scenario_place())
			bot_scenario_set_phase(_phase_settle);
		else if (now - bot_scenario.phase_time > BOT_SCENARIO_GATHER_TICKS)
		{
			platform_log("bot scenario: trial %ld skipped: its bots did not spawn", bot_scenario.trial_number);
			if (bot_scenario_advance())
				bot_scenario_begin_trial();
			else
				bot_scenario_set_phase(_phase_done);
		}
		break;
	case _phase_settle:
		if (now >= bot_scenario.trial_start)
			bot_scenario_set_phase(_phase_run);
		break;
	case _phase_run:
		bot_scenario_track_damage();
		bot_scenario_log_puppets();
		bot_scenario.end_reason = bot_scenario_end_condition();
		if (bot_scenario.end_reason)
		{
			bot_scenario.end_tick = now;
			bot_scenario_log_result();
			platform_log("bot scenario: trial %ld %s ended: %s after %.1f s", bot_scenario.trial_number,
				bot_scenario_definitions[bot_scenario.setup.scenario].id, bot_scenario.end_reason,
				(real)(now - bot_scenario.trial_start) / TICKS_PER_SECOND);
			bot_scenario_set_phase(_phase_result);
		}
		break;
	case _phase_result:
		if (now - bot_scenario.phase_time >= BOT_SCENARIO_RESULT_TICKS)
		{
			if (bot_scenario_advance())
				bot_scenario_begin_trial();
			else
			{
				platform_log("bot scenario: complete, %ld trials", bot_scenario.trial_number);
				bot_scenario_log_close();
				bot_scenario_set_phase(_phase_done);
				if (bot_scenario.exit_when_done)
				{
					platform_log("bot scenario: exiting (debug.bot_scenario_exit)");
					exit(EXIT_SUCCESS);
				}
			}
		}
		break;
	default:
		break;
	}
}

short bot_scenario_control(short slot, struct player_action *action, real_point3d *goal)
{
	struct bot_scenario_role const *role;

	if (!bot_scenario.enabled)
		return _bot_scenario_control_ai;
	if (bot_scenario.phase != _phase_run || slot < 0 || slot >= bot_scenario.setup.role_count ||
		!bot_scenario.setup.roles[slot].used)
	{
		bot_scenario_hold_action(slot, action);
		return _bot_scenario_control_action;
	}
	role = &bot_scenario.setup.roles[slot];
	if (role->hold_until_hurt && !bot_scenario.agents[slot].hurt)
	{
		bot_scenario_hold_action(slot, action);
		return _bot_scenario_control_action;
	}
	if (role->puppet)
	{
		bot_scenario_puppet_action(slot, action);
		return _bot_scenario_control_action;
	}
	if (bot_scenario.setup.goal_valid)
	{
		bot_scenario_world_point(bot_scenario.setup.goal_x, bot_scenario.setup.goal_y, goal);
		return _bot_scenario_control_goal;
	}
	return _bot_scenario_control_ai;
}

/* a subject's tick: its metrics, and a line of telemetry */
void bot_scenario_observe(struct bot_scenario_observation const *observation)
{
	short slot = observation->slot;
	long now = game_time_get();
	long elapsed = now - bot_scenario.trial_start;
	struct bot_scenario_metrics *metrics;
	long unit_index;
	struct unit_datum *unit;
	struct player_datum *target;
	long target_unit_index = NONE;
	real_point3d origin;
	real_point3d eye;
	real_point3d truth;
	boolean have_truth = FALSE;
	real aim_error = -1.f;
	boolean jump;
	boolean firing;
	boolean crouching;
	short target_slot;

	if (!bot_scenario.enabled || bot_scenario.phase != _phase_run || slot < 0 || slot >= BOT_SCENARIO_BOTS_MAX)
		return;
	unit = bot_scenario_slot_unit(slot, &unit_index);
	if (!unit)
		return;
	metrics = &bot_scenario.metrics[slot];
	object_get_origin(unit_index, &origin);
	unit_get_camera_position(unit_index, &eye);
	target = observation->target_player_index != NONE ? player_try_and_get(observation->target_player_index) : NULL;
	if (target && bot_scenario_living_unit(target->unit_index))
	{
		target_unit_index = target->unit_index;
		bot_scenario_chest(target_unit_index, &truth);
		have_truth = TRUE;
	}
	target_slot = bot_scenario_slot_of_player(observation->target_player_index);
	if (have_truth && observation->target_visible)
	{
		real_vector3d facing;
		real_vector3d toward;
		real cosine_error;

		vector3d_from_euler_angles2d(&facing, &observation->facing);
		vector_from_points3d(&eye, &truth, &toward);
		if (normalize3d(&toward) > 0.f)
		{
			cosine_error = PIN(dot_product3d(&facing, &toward), -1.f, 1.f);
			aim_error = RADIANS_TO_DEGREES((real)acos(cosine_error));
			metrics->aim_error_sum += aim_error;
			metrics->aim_samples++;
			if (aim_error <= BOT_SCENARIO_ON_TARGET_DEGREES && metrics->first_on_target == NONE)
				metrics->first_on_target = elapsed;
		}
	}
	firing = observation->trigger > 0.f ||
		TEST_FLAG(observation->control_flags, _unit_control_weapon_primary_trigger_bit);
	jump = TEST_FLAG(observation->control_flags, _unit_control_jump_bit);
	crouching = TEST_FLAG(observation->control_flags, _unit_control_crouch_modifier_bit);

	/* (sight lost means unseen for a second at least, not a flicker at a
	wall's edge; reacquired is the first sight after that) */
	if (observation->target_visible)
	{
		metrics->visible_ticks++;
		if (metrics->first_visible == NONE)
			metrics->first_visible = elapsed;
		if (metrics->lost_sight != NONE && metrics->reacquired == NONE)
			metrics->reacquired = elapsed;
		metrics->unseen_since = NONE;
	}
	else
	{
		if (metrics->have_last && metrics->was_visible)
			metrics->unseen_since = elapsed;
		if (metrics->unseen_since != NONE && metrics->lost_sight == NONE &&
			elapsed - metrics->unseen_since >= TICKS_PER_SECOND)
		{
			metrics->lost_sight = metrics->unseen_since;
		}
	}
	if (observation->target_source == _bot_scenario_target_shared && metrics->first_shared == NONE)
		metrics->first_shared = elapsed;
	if (firing)
	{
		metrics->fire_ticks++;
		if (metrics->first_fire == NONE)
			metrics->first_fire = elapsed;
	}
	if (crouching)
		metrics->crouch_ticks++;
	if (jump && !metrics->last_jump)
		metrics->jumps++;
	if (observation->leaf_name && !strcmp(observation->leaf_name, "retreat"))
	{
		metrics->retreat_ticks++;
		if (metrics->first_retreat == NONE)
			metrics->first_retreat = elapsed;
	}
	metrics->max_retreat_score = MAX(metrics->max_retreat_score, observation->scores[0]);
	if (metrics->have_last)
	{
		real moved = bot_scenario_horizontal_distance(&origin, &metrics->last_position);

		metrics->distance += moved;
		if (observation->has_move && observation->move_speed > 0.5f && moved < 0.01f)
			metrics->stalled_ticks++;
		if (observation->leaf_name != metrics->last_leaf)
			metrics->leaf_changes++;
		if (observation->leaf_name && !strcmp(observation->leaf_name, "fight") &&
			observation->strafe_sign != metrics->last_strafe)
		{
			metrics->strafe_changes++;
		}
		/* (its idea of an unseen target's place moved: it was not seen there) */
		if (!observation->target_visible && metrics->belief_valid &&
			observation->target_player_index == metrics->last_target &&
			(observation->target_source == _bot_scenario_target_attacker ||
			observation->target_source == _bot_scenario_target_remembered))
		{
			real drift = distance3d(&observation->target_belief, &metrics->last_belief);

			if (drift > 0.05f)
			{
				metrics->hidden_updates++;
				metrics->hidden_drift += drift;
			}
		}
	}
	/* (a player shot by someone unseen knows where the shots came from, not
	where the shooter is now: an exact idea of an unseen target's place is
	knowledge no player has) */
	if (have_truth && !observation->target_visible &&
		observation->target_source == _bot_scenario_target_attacker &&
		distance3d(&observation->target_belief, &truth) < 0.05f)
	{
		metrics->unseen_exact_ticks++;
	}
	if (observation->route_following || observation->route_searching)
		metrics->route_ticks++;
	if (bot_scenario.setup.goal_valid && metrics->arrived == NONE &&
		observation->navigation_result == BOT_NAVIGATION_ARRIVED)
	{
		metrics->arrived = elapsed;
	}
	metrics->have_last = TRUE;
	metrics->was_visible = observation->target_visible;
	metrics->last_jump = jump;
	metrics->last_leaf = observation->leaf_name;
	metrics->last_strafe = observation->strafe_sign;
	metrics->last_target = observation->target_player_index;
	metrics->last_position = origin;
	metrics->belief_valid = observation->target_player_index != NONE;
	metrics->last_belief = observation->target_belief;
	if (slot == 0)
	{
		bot_scenario.observation = *observation;
		bot_scenario.observed = TRUE;
	}

	if (!bot_scenario.log)
		return;
	fprintf(bot_scenario.log, "{\"type\":\"tick\",\"trial\":%ld,\"t\":%ld,\"slot\":%d,\"pos\":",
		bot_scenario.trial_number, elapsed, slot);
	bot_scenario_log_point(&origin);
	fprintf(bot_scenario.log, ",\"yaw\":%.2f,\"pitch\":%.2f,\"leaf\":\"%s\",\"scores\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f],"
		"\"confidence\":%.3f,\"target\":%d,\"source\":%d,\"visible\":%d,\"aware\":%d,\"belief\":",
		RADIANS_TO_DEGREES(observation->facing.yaw), RADIANS_TO_DEGREES(observation->facing.pitch),
		observation->leaf_name ? observation->leaf_name : "none",
		observation->scores[0], observation->scores[1], observation->scores[2], observation->scores[3],
		observation->scores[4], observation->scores[5], observation->confidence, (int)target_slot,
		observation->target_source, observation->target_visible ? 1 : 0, observation->aware ? 1 : 0);
	if (observation->target_player_index != NONE)
		bot_scenario_log_point(&observation->target_belief);
	else
		fputs("null", bot_scenario.log);
	fputs(",\"truth\":", bot_scenario.log);
	if (have_truth)
		bot_scenario_log_point(&truth);
	else
		fputs("null", bot_scenario.log);
	fputs(",\"aim_error\":", bot_scenario.log);
	if (aim_error >= 0.f)
		bot_scenario_log_real(aim_error);
	else
		fputs("null", bot_scenario.log);
	fputs(",\"look\":", bot_scenario.log);
	if (observation->has_look)
		fprintf(bot_scenario.log, "[%.2f,%.2f]", RADIANS_TO_DEGREES(observation->look.yaw),
			RADIANS_TO_DEGREES(observation->look.pitch));
	else
		fputs("null", bot_scenario.log);
	fputs(",\"move\":", bot_scenario.log);
	if (observation->has_move)
		fprintf(bot_scenario.log, "[%.2f,%.2f]", RADIANS_TO_DEGREES(observation->move_yaw), observation->move_speed);
	else
		fputs("null", bot_scenario.log);
	fprintf(bot_scenario.log, ",\"fire\":%d,\"crouch\":%d,\"jump\":%d,\"strafe\":%.0f,\"route\":[%d,%d,%d,%d],"
		"\"nav\":%d,\"shield\":%.3f,\"body\":%.3f}\n",
		firing ? 1 : 0, crouching ? 1 : 0, jump ? 1 : 0, observation->strafe_sign,
		observation->route_searching ? 1 : 0, observation->route_following ? 1 : 0,
		observation->route_waypoint, observation->route_count, observation->navigation_result,
		unit->object.shield_vitality, unit->object.body_vitality);
	(void)target_unit_index;
}

void bot_scenario_end_tick(double milliseconds)
{
	if (!bot_scenario.enabled || bot_scenario.phase != _phase_run || !(milliseconds >= 0.0 && milliseconds < 1.0e6))
		return;
	bot_scenario.milliseconds_sum += milliseconds;
	bot_scenario.milliseconds_maximum = MAX(bot_scenario.milliseconds_maximum, milliseconds);
	bot_scenario.milliseconds_ticks++;
	if (bot_scenario.log && bot_scenario.setup.scenario == _scenario_crowd)
		fprintf(bot_scenario.log, "{\"type\":\"perf\",\"trial\":%ld,\"t\":%ld,\"ms\":%.4f}\n",
			bot_scenario.trial_number, game_time_get() - bot_scenario.trial_start, milliseconds);
}

/* ---------- navigation_world.c: the floor's polygons */

uint32_t bot_scenario_navigation_surface_count(void)
{
	return bot_scenario.arena_active ? (uint32_t)BOT_SCENARIO_CELLS : 0;
}

/* Cell (x, y) of the floor's 2-unit grid, counterclockwise from above, its
edges' neighbors the cells beside it: south, east, north, west. */
int bot_scenario_navigation_surface(uint32_t index, uint32_t first_source_index, struct sn_polygon *out)
{
	uint32_t cell_x;
	uint32_t cell_y;
	real x0;
	real y0;
	real z = bot_scenario.center.z;

	if (!bot_scenario.arena_active || index >= (uint32_t)BOT_SCENARIO_CELLS)
		return -1;
	cell_x = index % BOT_SCENARIO_CELLS_X;
	cell_y = index / BOT_SCENARIO_CELLS_X;
	x0 = bot_scenario.center.x - BOT_SCENARIO_HALF_WIDTH + (real)cell_x * BOT_SCENARIO_CELL;
	y0 = bot_scenario.center.y - BOT_SCENARIO_HALF_DEPTH + (real)cell_y * BOT_SCENARIO_CELL;
	csmemset(out, 0, sizeof(*out));
	out->normal.x = 0.f;
	out->normal.y = 0.f;
	out->normal.z = 1.f;
	/* (clearance unknown: every route is checked with the live capsule, which
	sees the walls) */
	out->width = 0.f;
	out->height = 0.f;
	out->vertex_count = 4;
	out->vertices[0].x = x0; out->vertices[0].y = y0; out->vertices[0].z = z;
	out->vertices[1].x = x0 + BOT_SCENARIO_CELL; out->vertices[1].y = y0; out->vertices[1].z = z;
	out->vertices[2].x = x0 + BOT_SCENARIO_CELL; out->vertices[2].y = y0 + BOT_SCENARIO_CELL; out->vertices[2].z = z;
	out->vertices[3].x = x0; out->vertices[3].y = y0 + BOT_SCENARIO_CELL; out->vertices[3].z = z;
	out->neighbors[0] = cell_y > 0 ? first_source_index + index - BOT_SCENARIO_CELLS_X : SN_NONE;
	out->neighbors[1] = cell_x + 1 < BOT_SCENARIO_CELLS_X ? first_source_index + index + 1 : SN_NONE;
	out->neighbors[2] = cell_y + 1 < BOT_SCENARIO_CELLS_Y ? first_source_index + index + BOT_SCENARIO_CELLS_X : SN_NONE;
	out->neighbors[3] = cell_x > 0 ? first_source_index + index - 1 : SN_NONE;
	return 1;
}

/* ---------- the camera and overlay (an editor: extension_api.h) */

static boolean bot_scenario_camera_active(void)
{
	return bot_scenario.enabled && bot_scenario.arena_active && bot_scenario.camera != _camera_off;
}

/* where the camera wants to be and to look this frame */
static void bot_scenario_camera_goal(real_point3d *position, real_point3d *focus, boolean *snap)
{
	short camera = bot_scenario.phase == _phase_arena || bot_scenario.phase == _phase_done ?
		_camera_overview : bot_scenario.setup.camera;
	long subject_index;
	struct unit_datum *subject = bot_scenario_slot_unit(0, &subject_index);

	*snap = FALSE;
	if (camera != _camera_overview && subject && bot_scenario.setup.roles[0].used && !bot_scenario.setup.roles[0].puppet)
	{
		real_point3d origin;
		real_point3d other;
		long other_index;
		boolean have_other = FALSE;
		real_vector3d along;
		real length;

		object_get_origin(subject_index, &origin);
		if (camera == _camera_first_person)
		{
			real_vector3d facing;

			unit_get_camera_position(subject_index, position);
			if (bot_scenario.observed)
				vector3d_from_euler_angles2d(&facing, &bot_scenario.observation.facing);
			else
				set_real_vector3d(&facing, cosine(bot_scenario.setup.roles[0].yaw), sine(bot_scenario.setup.roles[0].yaw), 0.f);
			/* (just ahead of its eyes, clear of its own head) */
			position->x += facing.i * 0.3f;
			position->y += facing.j * 0.3f;
			position->z += facing.k * 0.3f;
			focus->x = position->x + facing.i * 10.f;
			focus->y = position->y + facing.j * 10.f;
			focus->z = position->z + facing.k * 10.f;
			*snap = TRUE;
			return;
		}
		if (bot_scenario.setup.goal_valid)
		{
			bot_scenario_world_point(bot_scenario.setup.goal_x, bot_scenario.setup.goal_y, &other);
			have_other = TRUE;
		}
		else if (bot_scenario.setup.role_count > 1 && bot_scenario_slot_unit(1, &other_index))
		{
			object_get_origin(other_index, &other);
			have_other = TRUE;
		}
		if (have_other)
			vector_from_points3d(&origin, &other, &along);
		else
			set_real_vector3d(&along, 1.f, 0.f, 0.f);
		along.k = 0.f;
		length = normalize3d(&along);
		if (!(length > 0.01f))
			set_real_vector3d(&along, 1.f, 0.f, 0.f);
		length = have_other ? MIN(length, 14.f) : 4.f;
		/* (behind and above the subject, a little to its right, looking past
		it toward what it faces) */
		position->x = origin.x - along.i * 6.f + along.j * 2.5f;
		position->y = origin.y - along.j * 6.f - along.i * 2.5f;
		position->z = origin.z + 4.5f;
		focus->x = origin.x + along.i * length * 0.45f;
		focus->y = origin.y + along.j * length * 0.45f;
		focus->z = origin.z + 0.4f;
		return;
	}
	/* (the whole arena from the south, high) */
	position->x = bot_scenario.center.x;
	position->y = bot_scenario.center.y - 25.f;
	position->z = bot_scenario.center.z + 20.f;
	*focus = bot_scenario.center;
	focus->y += 1.f;
}

static boolean bot_scenario_editor_active(void)
{
	return bot_scenario_camera_active();
}

static void bot_scenario_editor_update(real seconds)
{
	real_point3d position;
	real_point3d focus;
	boolean snap;
	real blend;
	real_vector3d forward;

	if (!bot_scenario_camera_active())
		return;
	bot_scenario_camera_goal(&position, &focus, &snap);
	if (!valid_real_point3d(&position) || !valid_real_point3d(&focus))
		return;
	blend = snap || !bot_scenario.camera_valid ? 1.f : PIN(seconds * 3.f, 0.f, 1.f);
	bot_scenario.camera_position.x += (position.x - bot_scenario.camera_position.x) * blend;
	bot_scenario.camera_position.y += (position.y - bot_scenario.camera_position.y) * blend;
	bot_scenario.camera_position.z += (position.z - bot_scenario.camera_position.z) * blend;
	bot_scenario.camera_focus.x += (focus.x - bot_scenario.camera_focus.x) * blend;
	bot_scenario.camera_focus.y += (focus.y - bot_scenario.camera_focus.y) * blend;
	bot_scenario.camera_focus.z += (focus.z - bot_scenario.camera_focus.z) * blend;
	bot_scenario.camera_valid = TRUE;
	vector_from_points3d(&bot_scenario.camera_position, &bot_scenario.camera_focus, &forward);
	if (!(normalize3d(&forward) > 0.f))
		set_real_vector3d(&forward, 1.f, 0.f, 0.f);
	flying_camera_new_from_point_and_vector(&bot_scenario.flying, &bot_scenario.camera_position, &forward);
}

/* the director's camera proc while the scenario watches */
static void bot_scenario_camera_update(void *data, void *controls, void *result)
{
	struct flying_camera_action action;
	struct camera_command *command = (struct camera_command *)result;

	(void)data;
	(void)controls;
	csmemset(&action, 0, sizeof(action));
	flying_camera_update(&bot_scenario.flying, &action, command);
	command->timer = 0.f;
	command->velocity = *global_zero_vector3d;
}

static boolean bot_scenario_director_camera(short local_player_index, void **camera_proc, boolean *reset)
{
	*camera_proc = (void *)bot_scenario_camera_update;
	*reset = FALSE;
	return bot_scenario_camera_active() && bot_scenario.camera_valid && local_player_index == 0;
}

static uint64_t bot_scenario_world_edit_revision(void)
{
	return bot_scenario.revision;
}

/* the console's small terminal font, else the HUD's */
static long bot_scenario_font(void)
{
	long font_index = interface_get_tag_index(_interface_font_terminal);

	return font_index != NONE ? font_index : hud_get_font_index();
}

static void bot_scenario_draw_text(char const *text, short x, short y, real_argb_color const *color)
{
	long font_index = bot_scenario_font();
	wchar_t wide[256];
	long index;
	rectangle2d bounds;

	if (font_index == NONE)
		return;
	for (index = 0; text[index] && index < NUMBEROF(wide) - 1; index++)
		wide[index] = (wchar_t)(unsigned char)text[index];
	wide[index] = 0;
	bounds = render.camera.window_bounds;
	bounds.x0 = (short)(bounds.x0 + x);
	bounds.y0 = (short)(bounds.y0 + y);
	draw_string_set_draw_mode(font_index, NONE, 0, 0, color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, wide);
}

static char const *bot_scenario_source_name(short source)
{
	switch (source)
	{
	case _bot_scenario_target_seen: return "seen";
	case _bot_scenario_target_attacker: return "attacker";
	case _bot_scenario_target_remembered: return "remembered";
	case _bot_scenario_target_shared: return "shared";
	default: return "none";
	}
}

static void bot_scenario_format_tick(char *text, long size, char const *label, long tick)
{
	if (tick == NONE)
		snprintf(text, (size_t)size, "%s -", label);
	else
		snprintf(text, (size_t)size, "%s %.2fs", label, (real)tick / TICKS_PER_SECOND);
}

/* the subject's knowledge and route in the world, and its numbers over the view */
static void bot_scenario_editor_render(void)
{
	real_argb_color const white = { 1.f, 1.f, 1.f, 1.f };
	real_argb_color const yellow = { 1.f, 1.f, 0.85f, 0.2f };
	real_argb_color const green = { 1.f, 0.3f, 1.f, 0.35f };
	real_argb_color const red = { 1.f, 1.f, 0.3f, 0.25f };
	real_argb_color const cyan = { 1.f, 0.3f, 0.9f, 1.f };
	real_argb_color const magenta = { 1.f, 1.f, 0.35f, 1.f };
	real_argb_color const grey = { 1.f, 0.75f, 0.75f, 0.75f };
	struct bot_scenario_setup const *setup = &bot_scenario.setup;
	struct bot_scenario_observation const *observation = &bot_scenario.observation;
	struct bot_scenario_metrics const *metrics = &bot_scenario.metrics[0];
	char text[BOT_SCENARIO_LINE_MAX];
	short line_height = 16;
	short y = 12;
	long subject_index;
	long font_index;

	if (!bot_scenario_camera_active() || render.local_player_index != 0)
		return;
	/* (in the world: the line to its target, green while seen; where it
	thinks the target is; its route; the goal and the pickup) */
	if (bot_scenario_active_trial() && bot_scenario_slot_unit(0, &subject_index))
	{
		real_point3d eye;
		short index;

		unit_get_camera_position(subject_index, &eye);
		if (bot_scenario.observed && observation->target_player_index != NONE)
		{
			struct player_datum *target = player_try_and_get(observation->target_player_index);

			if (target && bot_scenario_living_unit(target->unit_index))
			{
				real_point3d chest;

				bot_scenario_chest(target->unit_index, &chest);
				render_debug_line(TRUE, &eye, &chest, observation->target_visible ? &green : &red);
			}
			render_debug_sphere(TRUE, &observation->target_belief, 0.25f, &yellow);
		}
		if (bot_scenario.observed)
		{
			for (index = 0; index + 1 < observation->route_count; index++)
				render_debug_line(TRUE, &observation->route[index], &observation->route[index + 1], &cyan);
		}
		if (setup->goal_valid)
		{
			real_point3d goal;

			bot_scenario_world_point(setup->goal_x, setup->goal_y, &goal);
			goal.z += 0.3f;
			render_debug_sphere(TRUE, &goal, 0.4f, &magenta);
		}
		if (bot_scenario.pickup_weapon_index != NONE && object_try_and_get(bot_scenario.pickup_weapon_index))
		{
			real_point3d point;

			object_get_origin(bot_scenario.pickup_weapon_index, &point);
			render_debug_sphere(TRUE, &point, 0.3f, &magenta);
		}
	}

	/* (the arena and the debug geometry so far drawn now, under the text: the
	window's debug geometry is otherwise drawn last, over it) */
	rasterizer_debug_draw();
	rasterizer_debug_begin();
	font_index = bot_scenario_font();
	if (font_index != NONE)
	{
		struct font_header *font = font_definition_get(font_index);

		line_height = (short)(font->ascending_height + font->descending_height + 2);
	}
	if (bot_scenario.phase == _phase_arena || bot_scenario.phase == _phase_gather || bot_scenario.phase == _phase_done)
	{
		snprintf(text, sizeof(text), "BOT SCENARIOS  %s", bot_scenario.phase == _phase_done ? "complete" : bot_scenario.status);
		bot_scenario_draw_text(text, 16, y, &yellow);
		return;
	}
	/* (short lines: they fit a 4:3 window) */
	snprintf(text, sizeof(text), "BOT SCENARIO %s  %s", bot_scenario_definitions[setup->scenario].id,
		bot_scenario_definitions[setup->scenario].name);
	bot_scenario_draw_text(text, 16, y, &yellow);
	y = (short)(y + line_height);
	snprintf(text, sizeof(text), "trial %d/%d  %s  %s", bot_scenario.trial_cursor + 1, bot_scenario.trials,
		bot_scenario_skill_names[bot_scenario.skills[bot_scenario.skill_cursor]], setup->variant_name);
	bot_scenario_draw_text(text, 16, y, &yellow);
	y = (short)(y + line_height);
	snprintf(text, sizeof(text), "%s  %.1f / %.0f s%s%s",
		bot_scenario.phase == _phase_settle ? "placing" : bot_scenario.phase == _phase_run ? "running" : "result",
		(real)MAX(0, game_time_get() - bot_scenario.trial_start) / TICKS_PER_SECOND,
		(real)setup->duration_ticks / TICKS_PER_SECOND,
		bot_scenario.end_reason ? "  -  " : "", bot_scenario.end_reason ? bot_scenario.end_reason : "");
	bot_scenario_draw_text(text, 16, y, &white);
	y = (short)(y + line_height);
	if (bot_scenario.observed)
	{
		snprintf(text, sizeof(text), "Bot 1: %s  confidence %.2f  target %s%s",
			observation->leaf_name ? observation->leaf_name : "none", observation->confidence,
			bot_scenario_source_name(observation->target_source),
			observation->target_visible ? " (in sight)" : "");
		bot_scenario_draw_text(text, 16, y, &cyan);
		y = (short)(y + line_height);
		snprintf(text, sizeof(text), "retreat %.2f  fight %.2f  roam %.2f  scavenge %.2f",
			observation->scores[0], observation->scores[1], observation->scores[2], observation->scores[3]);
		bot_scenario_draw_text(text, 16, y, &cyan);
		y = (short)(y + line_height);
	}
	{
		char a[40], b[40], c[40], d[40];

		bot_scenario_format_tick(a, sizeof(a), "seen", metrics->first_visible);
		bot_scenario_format_tick(b, sizeof(b), "on target", metrics->first_on_target);
		bot_scenario_format_tick(c, sizeof(c), "fired", metrics->first_fire);
		bot_scenario_format_tick(d, sizeof(d), "hit", metrics->first_hit);
		snprintf(text, sizeof(text), "%s  %s  %s  %s", a, b, c, d);
		bot_scenario_draw_text(text, 16, y, &white);
		y = (short)(y + line_height);
		snprintf(text, sizeof(text), "hits %ld  damage %.2f  taken %.2f  aim error %.1f deg",
			metrics->hits, metrics->damage_dealt, metrics->damage_taken,
			metrics->aim_samples > 0 ? metrics->aim_error_sum / (real)metrics->aim_samples : 0.f);
		bot_scenario_draw_text(text, 16, y, &white);
		y = (short)(y + line_height);
		bot_scenario_format_tick(a, sizeof(a), "lost sight", metrics->lost_sight);
		bot_scenario_format_tick(b, sizeof(b), "reacquired", metrics->reacquired);
		bot_scenario_format_tick(c, sizeof(c), "shared", metrics->first_shared);
		bot_scenario_format_tick(d, sizeof(d), "arrived", metrics->arrived);
		snprintf(text, sizeof(text), "%s  %s  %s  %s", a, b, c, d);
		bot_scenario_draw_text(text, 16, y, &white);
		y = (short)(y + line_height);
		snprintf(text, sizeof(text), "unseen target: known exactly %.1fs, moved %ld times",
			(real)metrics->unseen_exact_ticks / TICKS_PER_SECOND, metrics->hidden_updates);
		bot_scenario_draw_text(text, 16, y, &white);
		y = (short)(y + line_height);
		snprintf(text, sizeof(text), "strafe changes %ld  crouch %.1fs  jumps %ld",
			metrics->strafe_changes, (real)metrics->crouch_ticks / TICKS_PER_SECOND, metrics->jumps);
		bot_scenario_draw_text(text, 16, y, &white);
		y = (short)(y + line_height);
		snprintf(text, sizeof(text), "retreat %.1fs (score max %.2f)  walked %.1f  stalled %.1fs",
			(real)metrics->retreat_ticks / TICKS_PER_SECOND, metrics->max_retreat_score, metrics->distance,
			(real)metrics->stalled_ticks / TICKS_PER_SECOND);
		bot_scenario_draw_text(text, 16, y, &white);
		y = (short)(y + line_height);
	}
	snprintf(text, sizeof(text), "line: green in sight, red not  ball: where it thinks the target is  cyan: route");
	bot_scenario_draw_text(text, 16, y, &grey);
}

struct halo_editor const bot_scenario_editor =
{
	.active = bot_scenario_editor_active,
	.update = bot_scenario_editor_update,
	.director_camera = bot_scenario_director_camera,
	.render = bot_scenario_editor_render,
	.world_edit_revision = bot_scenario_world_edit_revision,
};

#else /* HALO_EXTENSION_WORLD_GEOMETRY */

/* No host-local world geometry here (32-bit, Android, the web, the
dedicated server): there is no arena, and the fixture does not run. */

void bot_scenario_initialize_for_new_map(void)
{
	char const *requested = config_string("debug.bot_scenario");

	if (requested && requested[0])
		platform_log("bot scenario: needs a 64-bit desktop build; not run");
}

void bot_scenario_dispose_from_old_map(void) {}
boolean bot_scenario_enabled(void) { return FALSE; }
short bot_scenario_bot_count(void) { return 0; }
char bot_scenario_team(short slot) { return (char)(slot & 1); }
boolean bot_scenario_ignores_player(long player_index) { (void)player_index; return FALSE; }
void bot_scenario_update(void) {}
short bot_scenario_control(short slot, struct player_action *action, real_point3d *goal)
{
	(void)slot;
	(void)action;
	(void)goal;
	return _bot_scenario_control_ai;
}
void bot_scenario_observe(struct bot_scenario_observation const *observation) { (void)observation; }
void bot_scenario_end_tick(double milliseconds) { (void)milliseconds; }
uint32_t bot_scenario_navigation_surface_count(void) { return 0; }
int bot_scenario_navigation_surface(uint32_t index, uint32_t first_source_index, struct sn_polygon *out)
{
	(void)index;
	(void)first_source_index;
	(void)out;
	return -1;
}

#endif /* HALO_EXTENSION_WORLD_GEOMETRY */

#endif /* HALO_FEATURE_BOTS */
