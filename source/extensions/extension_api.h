/*
EXTENSION_API.H

port: the extension interface. The engine calls halo_extensions_* at a small,
fixed set of points; feature modules (source/features/<name>/) each provide
one struct halo_extension, which the build registers (configure.py,
tools/features.py, extension_dispatch.c). The engine never names a feature.

An extension is made of optional parts, each with its own contract:

- general hooks (struct halo_extension): notifications, called for every
  extension in registry order; and suppressions, where any TRUE wins.
- a ruleset (struct halo_ruleset): custom game rules. At most one ruleset
  owns the session: the first registered whose selector accepts its engine
  and variant. Ownership stays fixed until engine disposal or initialization.
  While it does, each hook it provides replaces the engine's own handling
  of that operation outright; a NULL hook keeps the engine's.
- a player controller (struct halo_player_controller): players this
  machine controls by program rather than by input (bots). Controllers
  update in registry order, before the players' actions are taken.
- an editor (struct halo_editor): a tool that, while active(), takes the
  local input and the director's camera. Of several, the first registered
  that is active owns updates, rendering, input and camera exclusively.
  Ownership stays with it until it closes. While idle, update callbacks may
  poll for activation in registry order; polling stops at the first claim.

Every member but name may be NULL (fields left out of a designated
initializer are). Hooks run on the game thread.
*/

#ifndef HALO_EXTENSION_API_H
#define HALO_EXTENSION_API_H
#pragma once

#include "cseries.h"
#include <stdint.h>

union real_point3d;
union real_vector3d;

struct game_engine;
struct game_variant;
struct player_action;
struct scenario_object_datum;
union real_rgb_color;
struct collision_result;
struct collision_feature_list;

/* ---------- host-local procedural world geometry (64-bit desktop only) */

#if defined(HALO_64BIT) && !defined(HALO_SERVER) && !defined(HALO_ANDROID)
#define HALO_EXTENSION_WORLD_GEOMETRY 1
#endif

/* Collision hooks run only for structure tests. Features must use NONE for
 * tag surface/material indices: external geometry has no BSP tag identity.
 * test_vector must leave result untouched on a miss and only replace a hit
 * with a nearer one (result->t is the current upper bound). radius is zero
 * for rays, positive for swept spheres. No tag storage may be modified. */
/* Stable per-map slots for host-local geometry edited by Forge. Deleted slots
 * remain addressable for undo. set may append exactly the next slot; callers
 * must preserve the extension name (not registry order) when saving edits. */
struct halo_world_edit_transform
{
	real position[3];
	real rotation[3]; /* yaw, pitch, roll; radians */
	boolean deleted;
};

struct halo_world_geometry
{
	void (*render)(void);
	boolean (*test_point)(union real_point3d const *point);
	boolean (*test_vector)(unsigned long flags, union real_point3d const *point,
		union real_vector3d const *vector, real radius, struct collision_result *result);
	void (*get_features)(union real_point3d const *center, real radius, real height,
		real width, struct collision_feature_list *features);
	/* Optional editor adapter. Ray picking returns a nearer fraction only;
	 * corners provide the exact world-space selection box, not tag objects. */
	long (*edit_count)(void);
	boolean (*edit_get)(long index, struct halo_world_edit_transform *state);
	boolean (*edit_set)(long index, struct halo_world_edit_transform const *state);
	boolean (*edit_pick)(union real_point3d const *origin, union real_vector3d const *ray,
		long ignored_index, real *t, long *index);
	boolean (*edit_corners)(long index, union real_point3d *corners);
	char const *(*edit_name)(long index);
	char const *(*edit_resource)(void); /* stable asset identity for saved poses */
};

/* ---------- rulesets */

struct halo_ruleset
{
	/* Called only during engine initialization, in registry order. TRUE
	claims the session and supplies a non-NULL engine in *selected (which
	may be original). FALSE declines, without changing engine/variant or
	activating rules. Later selectors are never called after acceptance. */
	boolean (*select_game_engine)(struct game_engine *original, struct game_variant *variant,
		struct game_engine **selected);
	/* TRUE: while owned, the game engine's state is never sent to or read
	from other machines (a local-only experimental mode is never mistaken
	for the variant it is built on) */
	boolean local_only;

	/* players.c: each player's action, before it is applied */
	void (*filter_player_action)(long player_index, struct player_action *action);
	/* game.c: each tick, after objects and players have updated (every
	death of the tick known), before the HUD */
	void (*end_tick)(void);
	/* game_engine.c: replaces the engine's death handling (scores, awards,
	respawn timers) */
	void (*player_killed)(long killing_player_index, long dead_player_index);
	boolean (*should_end_game)(void);
	boolean (*should_spawn_player)(long player_index);
	void (*player_change_color)(long player_index, union real_rgb_color *color);
	boolean (*player_state_message)(long player_index, wchar_t *message, long message_character_count);
	/* multiplies the engine's own multiplier (it is not replaced) */
	real (*damage_multiplier)(long damaging_player_index, long damaged_player_index);
	/* FALSE stops the player collecting items or ammunition */
	boolean (*can_collect_items)(long player_index);
};

/* ---------- player controllers */

struct halo_player_controller
{
	/* players.c: each tick, before the players' actions are taken: join or
	leave, and decide the actions the host sends for the controlled players */
	void (*update)(void);
	/* whether this machine controls the player by program */
	boolean (*controls_player)(long player_index);
	/* game_engine.c: a factor, 0 to 1, on how good a starting location is
	for the player (a controller's own spawning policy); NULL is 1 */
	real (*starting_location_rating)(long player_index, union real_point3d const *position);
};

/* ---------- editors */

struct halo_editor
{
	/* whether the editor is open: it owns the local input and camera */
	boolean (*active)(void);
	/* main.c: every frame, paused or not, before the director. Only the
	owner runs while an editor is open. Idle editors may poll for activation;
	an inactive callback must not edit the world unless it activates itself. */
	void (*update)(real seconds);
	/* director.c: TRUE and the camera's proc while the editor has the view;
	*reset TRUE once, the frame after it lets go, for the game's camera to
	be chosen again */
	boolean (*director_camera)(short local_player_index, void **camera_proc, boolean *reset);
	/* editor_stubs.c: the map loaded (before its objects are placed), and
	left */
	void (*initialize_for_new_map)(void);
	void (*dispose_from_old_map)(void);
	/* objects.c: an object made from a scenario placement */
	void (*object_placed_from_scenario)(long object_index, struct scenario_object_datum *scenario_object);
	/* object_types.c: a structure BSP switched to, its objects placed */
	void (*structure_bsp_reconnected)(void);
	/* editor_stubs.c: what the editor shows over the view */
	void (*render)(void);
	/* a count that changes whenever the editor changes the world's geometry
	or obstacles: derived data (navigation) built against another is stale */
	uint64_t (*world_edit_revision)(void);
};

/* ---------- extensions */

struct halo_extension
{
	char const *name;

	/* ---------- map lifecycle and frame (notifications) */

	/* game.c: the map's objects placed, before the AI is */
	void (*objects_placed)(void);
	/* game.c: the map left, before the rasterizer and game state go */
	void (*dispose_from_old_map)(void);
	/* game_state.c, scenario.c: a saved game is about to be restored, or tag
	storage about to be unloaded: drop host-local derived data */
	void (*invalidate_derived_state)(void);
	/* main.c: each unpaused frame, before the connection's frame starts */
	void (*main_frame_update)(boolean main_menu_loaded, real seconds);

	/* the game must not be reported to the game list, Delta Stats or the
	event log (any TRUE wins) */
	boolean (*suppress_game_report)(void);

	/* ---------- parts */

	struct halo_ruleset const *ruleset;
	struct halo_player_controller const *player_controller;
	struct halo_editor const *editor;
	struct halo_world_geometry const *world_geometry;
};

/* ---------- prototypes/EXTENSION_DISPATCH.C */

/* the registered extensions, in order (for logs) */
long halo_extensions_count(void);
struct halo_extension const *halo_extensions_get(long index);

void halo_extensions_objects_placed(void);
void halo_extensions_dispose_from_old_map(void);
void halo_extensions_invalidate_derived_state(void);
void halo_extensions_main_frame_update(boolean main_menu_loaded, real seconds);
struct game_engine *halo_extensions_select_game_engine(struct game_engine *original, struct game_variant *variant);
/* Clear session ownership after engine cleanup, also before initialization
 * of a non-game variant. Does not dispose the engine itself. */
void halo_extensions_end_game_session(void);
boolean halo_extensions_suppress_game_report(void);

/* rulesets: the override calls return TRUE when the active ruleset handled
the operation (its result in *result); FALSE leaves it to the engine */
struct halo_ruleset const *halo_extensions_active_ruleset(void);
void halo_extensions_filter_player_action(long player_index, struct player_action *action);
void halo_extensions_end_tick(void);
boolean halo_extensions_player_killed(long killing_player_index, long dead_player_index);
boolean halo_extensions_should_end_game(boolean *result);
boolean halo_extensions_should_spawn_player(long player_index, boolean *result);
boolean halo_extensions_player_change_color(long player_index, union real_rgb_color *color);
boolean halo_extensions_player_state_message(long player_index, wchar_t *message, long message_character_count,
	boolean *result);
real halo_extensions_damage_multiplier(long damaging_player_index, long damaged_player_index);
boolean halo_extensions_can_collect_items(long player_index);
boolean halo_extensions_suppress_network_state(void);

/* Procedural geometry is additive, independent of editor ownership. */
void halo_extensions_world_render(void);
boolean halo_extensions_world_test_point(union real_point3d const *point);
boolean halo_extensions_world_test_vector(unsigned long flags, union real_point3d const *point,
	union real_vector3d const *vector, real radius, struct collision_result *result);
void halo_extensions_world_get_features(union real_point3d const *center, real radius,
	real height, real width, struct collision_feature_list *features);

/* player controllers */
void halo_extensions_update_player_controllers(void);
boolean halo_extensions_player_is_computer_controlled(long player_index);
/* the product of the controllers' factors, 0 to 1 */
real halo_extensions_starting_location_rating(long player_index, union real_point3d const *position);

/* editors */
boolean halo_extensions_editor_active(void);
void halo_extensions_editor_update(real seconds);
boolean halo_extensions_editor_director_camera(short local_player_index, void **camera_proc, boolean *reset);
void halo_extensions_editor_initialize_for_new_map(void);
void halo_extensions_editor_dispose_from_old_map(void);
void halo_extensions_object_placed_from_scenario(long object_index, struct scenario_object_datum *scenario_object);
void halo_extensions_structure_bsp_reconnected(void);
void halo_extensions_editor_render(void);
uint64_t halo_extensions_world_edit_revision(void);
/* (the platform layer's view, port/linux/src/xinput_sdl.c: nonzero while an
editor has the keyboard and mouse) */
int halo_extensions_input_captured(void);

#endif /* HALO_EXTENSION_API_H */
