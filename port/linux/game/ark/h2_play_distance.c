/*
H2_PLAY_DISTANCE.C

(port) Halo 2's effect play distances, for a Halo 2 level's projectile
impacts. Halo 2's effect tag has an "always play distance" and a "never
play distance" (MCC cache 0x28 and 0x2C; the converter keeps them in this
engine's effect, h2_effects.c: unused00c[0] and [1]). Halo 2's effect_new
(effects.cpp) asks, before it makes an effect whose creation data carries a
collision (a projectile's impact material effects - projectiles.cpp clears
the "always" creation flag for those; its detonation effects set it):
- no definition, no collision, its location in no cluster, or a cinematic
  playing: made;
- either distance 0 ("does not have play distances set"): made;
- the nearest local observer camera (up to four) closer than the always
  play distance: made; farther than the never play distance: NOT made;
- between the two: made only when the impact's cluster is one the renderer
  found visible (render_visibility_collection's cluster bit vector, the last
  frame drawn).
In the MCC maps the bullet and plasma impacts have it (10 and 50 world
units); explosions, detonations and every other effect do not.

Here: the renderer notes its primary windows' visible clusters each frame
(h2_play_distance_note_visible: the union of the local players' windows -
Halo 2 keeps the last window's; with one window the same), and the tick
takes them once at objects_update's start (h2_play_distance_tick_begin);
no frame drawn yet, or another BSP's, counts as visible. HALO_H2_PLAY_DISTANCE=0: every
impact made, as before (re-read at @set). HALO_H2_PLAY_DISTANCE_LOG=1: a
line every 300 ticks with what the rule decided.
*/

/* ---------- headers */

#include "cseries.h"
#include "real_math.h"
#include "game/game.h"
#include "game/players.h"
#include "camera/observer.h"
#include "cutscene/cinematics.h"
#include "effects/effect_definitions.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "structures/structures.h"
#include "tag_files/tag_groups.h"

#include <stdlib.h>
#include <string.h>

/* ---------- prototypes */

boolean h2_rules_level(void);
void platform_log(char const *format, ...);
boolean h2_play_distance_allows(long effect_definition_index, struct collision_result const *collision);
void h2_play_distance_note_visible(short local_player_index, unsigned long const *cluster_flags, long cluster_count);
void h2_play_distance_tick_begin(void);

/* ---------- globals */

enum
{
	H2_PLAY_DISTANCE_LOG_TICKS = 300,
	H2_PLAY_DISTANCE_WINDOWS = 4,
};

/* (written by the renderer, read by the tick: each word is whole; a frame
half old is a frame the rule could have seen anyway) */
static struct
{
	short bsp_index[H2_PLAY_DISTANCE_WINDOWS];
	unsigned long frame[H2_PLAY_DISTANCE_WINDOWS];
	unsigned long flags[H2_PLAY_DISTANCE_WINDOWS][MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS];
} visible = {{NONE, NONE, NONE, NONE}};

static struct
{
	long tick;
	unsigned long asked, unset, cinematic, near, visible, hidden, far;
} census = {NONE};

/* ---------- code */

static boolean play_distance_enabled(
	void)
{
	extern volatile unsigned long halo_settings_generation;
	static unsigned long settings_seen;
	static int enabled = -1;

	if (enabled < 0 || settings_seen != halo_settings_generation)
	{
		char const *setting = getenv("HALO_H2_PLAY_DISTANCE");

		settings_seen = halo_settings_generation;
		enabled = setting ? atoi(setting) != 0 : 1;
	}
	return enabled;
}

void h2_play_distance_note_visible(
	short local_player_index,
	unsigned long const *cluster_flags,
	long cluster_count)
{
	if (local_player_index < 0 || local_player_index >= H2_PLAY_DISTANCE_WINDOWS ||
		cluster_count <= 0 || cluster_count > MAXIMUM_CLUSTERS_PER_STRUCTURE)
		return;
	memcpy(visible.flags[local_player_index], cluster_flags, BIT_VECTOR_SIZE_IN_BYTES(cluster_count));
	visible.bsp_index[local_player_index] = global_structure_bsp_index_get();
	visible.frame[local_player_index]++;
}

/* (the tick's view of it, taken once at objects_update's start on the
tick's thread - h2_play_distance_tick_begin: every impact of a tick, a
parallel tick's speculated ones too, asks the same frame's clusters) */
static struct
{
	boolean any;
	unsigned long flags[MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS];
	/* (the local observers' camera positions, taken with them) */
	short camera_count;
	real_point3d cameras[H2_PLAY_DISTANCE_WINDOWS];
} tick_visible;

void h2_play_distance_tick_begin(
	void)
{
	short bsp_index = global_structure_bsp_index_get();
	short window;
	long word;

	memset(&tick_visible, 0, sizeof(tick_visible));
	for (window = 0; window < H2_PLAY_DISTANCE_WINDOWS && window < local_player_count(); window++)
	{
		struct observer_result const *camera;

		if (local_player_get_player_index(window) == NONE || !(camera = observer_get_camera(window)))
			continue;
		tick_visible.cameras[tick_visible.camera_count++] = camera->position;
	}
	for (window = 0; window < H2_PLAY_DISTANCE_WINDOWS; window++)
	{
		if (!visible.frame[window] || visible.bsp_index[window] != bsp_index)
			continue;
		tick_visible.any = TRUE;
		for (word = 0; word < MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS; word++)
			tick_visible.flags[word] |= visible.flags[window][word];
	}
}

static boolean play_distance_cluster_visible(
	short cluster_index)
{
	/* (nothing drawn of this BSP yet: visible) */
	return !tick_visible.any || BIT_VECTOR_TEST_FLAG(tick_visible.flags, cluster_index);
}

static void play_distance_census(
	void)
{
	static int log_enabled = -1;
	long now = game_time_get();

	if (log_enabled < 0)
		log_enabled = getenv("HALO_H2_PLAY_DISTANCE_LOG") != NULL;
	if (!log_enabled)
		return;
	if (census.tick == NONE || now < census.tick)
		census.tick = now;
	if (now - census.tick >= H2_PLAY_DISTANCE_LOG_TICKS)
	{
		platform_log("h2 play distance (%ld ticks to tick %ld): impacts %lu - no distances %lu, cinematic %lu, near %lu, "
			"visible %lu, NOT made: hidden %lu, far %lu",
			now - census.tick, now, census.asked, census.unset, census.cinematic, census.near, census.visible,
			census.hidden, census.far);
		memset(&census, 0, sizeof(census));
		census.tick = now;
	}
}

boolean h2_play_distance_allows(
	long effect_definition_index,
	struct collision_result const *collision)
{
	struct effect_definition const *definition;
	real always, never, nearest = REAL_MAX;
	short local_player_index, cluster_index;

	if (effect_definition_index == NONE || !collision || !play_distance_enabled() || !h2_rules_level())
		return TRUE;
	play_distance_census();
	census.asked++;
	cluster_index = collision->location.cluster_index;
	if (cluster_index == NONE || cluster_index < 0 || cluster_index >= MAXIMUM_CLUSTERS_PER_STRUCTURE)
		return TRUE;
	if (cinematic_in_progress())
	{
		census.cinematic++;
		return TRUE;
	}
	definition = (struct effect_definition const *)tag_get(EFFECT_DEFINITION_TAG, effect_definition_index);
	always = definition->unused00c[0];
	never = definition->unused00c[1];
	if (always == 0.0f || never == 0.0f)
	{
		census.unset++;
		return TRUE;
	}
	for (local_player_index = 0; local_player_index < tick_visible.camera_count; local_player_index++)
	{
		real distance_squared = distance_squared3d(&tick_visible.cameras[local_player_index], &collision->point);

		if (distance_squared < nearest)
			nearest = distance_squared;
	}
	if (always * always > nearest)
	{
		census.near++;
		return TRUE;
	}
	if (nearest > never * never)
	{
		census.far++;
		return FALSE;
	}
	if (play_distance_cluster_visible(cluster_index))
	{
		census.visible++;
		return TRUE;
	}
	census.hidden++;
	return FALSE;
}
