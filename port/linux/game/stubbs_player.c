/*
STUBBS_PLAYER.C

Stubbs mode's player (stubbs_player.h): the camera behind Stubbs and his
HUD. Called only in Stubbs mode; each body does nothing until it is
written.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "cseries.h"
#include "stubbs_mode.h"
#include "stubbs_player.h"
#include "stubbs_engine.h"

#include "bitmaps/bitmaps.h"
#include "camera/director.h"
#include "models/model_animation_definitions.h"
#include "interface/hud_messaging.h"
#include "interface/hud_unit.h"
#include "game/game.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "tag_files/tag_groups.h"
#include "units/unit_definitions.h"
#include "units/units.h"

#include <stdio.h>
#include <string.h>

#ifndef HALO_CONSOLE_NO_STUBBS /* (the header's empty ones on a console) */

/* ---------- the camera

Stubbs is played from behind, in third person. The camera is Halo's
following camera (following_camera.c, as a vehicle's third-person seats
use), on the camera tracks of Stubbs's own unit tag (its "loose" track
first: Sigmmma's reclaimer, GPL-3.0, stubbs/defs/unit.py), which
stubbs_mode.c copies to where Halo has them. Only a living unit on foot:
in a seat the seat's own camera is Halo's choice, and a dead one gets
Halo's death camera. director_desired_perspective asks this for drawing and
for aiming alike (director_camera_deterministic), so both agree. */

boolean stubbs_player_camera_following(
	long unit_index,
	short *perspective)
{
	struct unit_datum *unit;
	struct unit_definition *definition;

	if (unit_index == NONE)
		return FALSE;
	unit = unit_try_and_get(unit_index);
	if (!unit || unit->object.parent_object_index != NONE ||
		TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
	{
		return FALSE;
	}
	/* (the camera needs a track: a unit without one keeps Halo's first
	person, since the globals' default track is cleared in Stubbs's maps) */
	definition = unit_definition_get(unit->definition_index);
	if (definition->unit.camera.unit_camera_tracks.count <= 0)
		return FALSE;
	{
		struct tag_reference const *track = (struct tag_reference const *)TAG_BLOCK_GET_ELEMENT(
			&definition->unit.camera.unit_camera_tracks, 0, struct tag_reference);

		if (!track || track->index == NONE)
			return FALSE;
	}
	*perspective = _director_perspective_third_person;
	return TRUE;
}

/* ---------- the melee

Stubbs fights with his hands: the player's melee button when Halo would
not swing (no weapon in hand, or one with no melee). Halo's player melee is
the weapon's (its first-person animation sets the blow's timing, bipeds.c),
and an unarmed Halo player can't melee, so in Stubbs mode the blow is
Stubbs's own punch from his animation graph, played as Halo plays a
player's third-person swing (the unit's action animation), and timed by it.
Its damage is then Halo's player melee (unit_cause_player_melee_damage):
Stubbs's unit melee_damage (his first blow's, stubbs_mode.c), along his
aim.

His punch is his weapon class's melee attack, which stubbs_mode.c puts in
Halo's slot (Stubbs's own order has his second combo blow, a lunge, there). */

enum
{
	STUBBS_ANIMATION_MELEE = 39, /* (Halo's _unit_weapon_class_animation_melee_attack) */
	STUBBS_UNIT_ANIMATION_ACTION_MELEE = 7, /* (unit_animation_start_action's) */
	STUBBS_MELEE_LONGEST_TICKS = 120, /* (bipeds.c counts them down in a char) */
};

boolean stubbs_player_melee_begin(
	long biped_index,
	short *ticks,
	short *attack_tick)
{
	struct unit_datum *unit = unit_try_and_get(biped_index);
	struct unit_definition *definition;
	struct animation_graph *graph;
	struct animation_graph_unit_seat *seat;
	struct animation_graph_weapon_class *weapon_class;
	struct animation *animation;
	short animation_index;
	short frame_count, key_frame;

	if (!unit || unit->unit.current_zoom_level != NONE)
		return FALSE;
	definition = unit_definition_get(unit->definition_index);
	if (definition->object.animation_graph.index == NONE)
		return FALSE;
	graph = animation_graph_definition_get(definition->object.animation_graph.index);
	if (unit->unit.animation.seat_index < 0 || unit->unit.animation.seat_index >= graph->unit_seats.count)
		return FALSE;
	seat = TAG_BLOCK_GET_ELEMENT(&graph->unit_seats, unit->unit.animation.seat_index, struct animation_graph_unit_seat);
	if (unit->unit.animation.weapon_index < 0 || unit->unit.animation.weapon_index >= seat->weapon_classes.count)
		return FALSE;
	weapon_class = TAG_BLOCK_GET_ELEMENT(&seat->weapon_classes, unit->unit.animation.weapon_index,
		struct animation_graph_weapon_class);
	if (weapon_class->animations.count <= STUBBS_ANIMATION_MELEE)
		return FALSE;
	animation_index = animation_graph_animation_index_get(&weapon_class->animations)[STUBBS_ANIMATION_MELEE].animation_index;
	if (animation_index == NONE || animation_index >= graph->animations.count)
	{
		static boolean reported;

		if (!reported)
		{
			reported = TRUE;
			stubbs_mode_log("the player's unit has no punch animation: no melee");
		}
		return FALSE;
	}
	animation_index = animation_choose_random_permutation_internal(TRUE, definition->object.animation_graph.index,
		animation_index);
	animation = TAG_BLOCK_GET_ELEMENT(&graph->animations, animation_index, struct animation);
	frame_count = MIN(animation->frame_count, STUBBS_MELEE_LONGEST_TICKS);
	if (frame_count <= 1)
		return FALSE;
	key_frame = animation->private_key_frame_index;
	if (key_frame <= 0 || key_frame >= frame_count)
		key_frame = frame_count / 2;
	unit->unit.animation.action_animation.index = animation_index;
	unit->unit.animation.action_animation.frame_index = 0;
	unit->unit.animation.action = STUBBS_UNIT_ANIMATION_ACTION_MELEE;
	*ticks = frame_count;
	*attack_tick = (short)(frame_count - key_frame);
	{
		static boolean reported;

		if (!reported)
		{
			reported = TRUE;
			stubbs_mode_log("the player's punch: %d ticks, the blow at %d, damage effect %08lX",
				frame_count, key_frame, (unsigned long)definition->unit.melee_damage.index);
		}
	}
	return TRUE;
}

/* ---------- the sprint

Stubbs runs while the player holds Halo's crouch button (the keyboard's
crouch key, a pad's left stick click): his "sprint f" animation plays in
place of his walk, and as all his movement comes from his animations'
frame data (bipeds.c), he moves at its pace (about half as fast again). It
only takes him forward, as in the retail game; sideways and back are his
walks. stubbs_mode.c puts the sprint in Halo's sliding-front slot, which
Halo itself never plays, so the slide-front state is his sprint state. A
unit without one (the hand, a possessed body) walks. Crouching itself is
his sprint button too, so a Stubbs player never crouches. */

boolean stubbs_player_sprint(
	long biped_index,
	short *state_desired)
{
	struct unit_datum *unit = unit_try_and_get(biped_index);
	struct unit_definition *definition;
	struct animation_graph *graph;
	struct animation_graph_unit_seat *seat;
	struct animation_graph_weapon_class *weapon_class;
	short animation_index;

	if (!unit || unit->unit.player_index == NONE || *state_desired != _unit_state_move_front ||
		!TEST_FLAG(unit->unit.control_flags, _unit_control_crouch_modifier_bit) ||
		unit->object.parent_object_index != NONE)
	{
		return FALSE;
	}
	definition = unit_definition_get(unit->definition_index);
	if (definition->object.animation_graph.index == NONE)
		return FALSE;
	graph = animation_graph_definition_get(definition->object.animation_graph.index);
	if (unit->unit.animation.seat_index < 0 || unit->unit.animation.seat_index >= graph->unit_seats.count)
		return FALSE;
	seat = TAG_BLOCK_GET_ELEMENT(&graph->unit_seats, unit->unit.animation.seat_index, struct animation_graph_unit_seat);
	if (unit->unit.animation.weapon_index < 0 || unit->unit.animation.weapon_index >= seat->weapon_classes.count)
		return FALSE;
	weapon_class = TAG_BLOCK_GET_ELEMENT(&seat->weapon_classes, unit->unit.animation.weapon_index,
		struct animation_graph_weapon_class);
	if (weapon_class->animations.count <= _unit_weapon_class_animation_sliding_front)
		return FALSE;
	animation_index = animation_graph_animation_index_get(&weapon_class->animations)[
		_unit_weapon_class_animation_sliding_front].animation_index;
	if (animation_index == NONE || animation_index >= graph->animations.count)
		return FALSE;
	*state_desired = _unit_state_slide_front;
	return TRUE;
}

boolean stubbs_player_never_crouches(
	long biped_index)
{
	struct unit_datum *unit = unit_try_and_get(biped_index);

	return unit && unit->unit.player_index != NONE;
}

/* ---------- the HUD

Stubbs's own HUD is not Halo's unit HUD (its unhi tags are laid out
differently), so a plain health meter is drawn here instead: a bar in the
top left, its fill the unit's body vitality, flashing red for a moment when
he is hurt, hidden and blinking as the scripts' hud_show_health and
hud_blink_health say. Drawn over Halo's HUD in each player's view, never in a
cinematic (hud.c). */

enum
{
	STUBBS_HUD_BITMAP_SIZE = 8,
	STUBBS_HUD_FORMAT_A8R8G8B8 = 11, /* (bitmaps.c's _bitmap_format_a8r8g8b8) */
	STUBBS_HUD_BLEND_ALPHA = 0, /* (_shader_framebuffer_blend_function_alpha_blend) */
};

boolean rasterizer_bitmap_new(struct bitmap_data *bitmap);
void rasterizer_bitmap_changed(struct bitmap_data *bitmap);

/* a white texture the bars are drawn with, tinted by their vertices' color */
static struct bitmap_data *stubbs_hud_white(
	void)
{
	static struct bitmap_data *white;
	static boolean attempted;

	if (!attempted)
	{
		attempted = TRUE;
		white = bitmap_2d_new(STUBBS_HUD_BITMAP_SIZE, STUBBS_HUD_BITMAP_SIZE, 0, STUBBS_HUD_FORMAT_A8R8G8B8);
		if (white)
		{
			memset(bitmap_mipmap_address(white, 0), 0xFF, STUBBS_HUD_BITMAP_SIZE * STUBBS_HUD_BITMAP_SIZE * 4);
			if (!rasterizer_bitmap_new(white))
			{
				bitmap_delete(white);
				white = NULL;
			}
			else
				rasterizer_bitmap_changed(white);
		}
	}
	return white;
}

static void stubbs_hud_rectangle(
	real x0,
	real y0,
	real x1,
	real y1,
	pixel32 color)
{
	struct bitmap_data *white = stubbs_hud_white();
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	struct dynamic_screen_vertex vertices[4];
	short index;

	if (!white || x1 <= x0 || y1 <= y0)
		return;
	memset(&parameters, 0, sizeof(parameters));
	parameters.map[0] = white;
	parameters.map_texture_scale[0].i = parameters.map_texture_scale[0].j = 1.0f;
	parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
	parameters.framebuffer_blend_function = STUBBS_HUD_BLEND_ALPHA;
	for (index = 0; index < 4; index++)
	{
		boolean right = ((index + 1) & 2) != 0;
		boolean bottom = index > 1;

		vertices[index].position.x = right ? x1 : x0;
		vertices[index].position.y = bottom ? y1 : y0;
		vertices[index].texture_coordinates.x = right ? 1.0f : 0.0f;
		vertices[index].texture_coordinates.y = bottom ? 1.0f : 0.0f;
		vertices[index].color = color;
	}
	rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
}

static pixel32 stubbs_hud_color(
	real alpha,
	real red,
	real green,
	real blue)
{
	return ((pixel32)(PIN(alpha, 0.0f, 1.0f) * 255.0f) << 24) |
		((pixel32)(PIN(red, 0.0f, 1.0f) * 255.0f) << 16) |
		((pixel32)(PIN(green, 0.0f, 1.0f) * 255.0f) << 8) |
		(pixel32)(PIN(blue, 0.0f, 1.0f) * 255.0f);
}

static unsigned long stubbs_hud_special_attacks_shown;

void stubbs_player_hud_draw(
	short local_player_index,
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	rectangle2d window = render.camera.window_bounds;
	real width = (real)(window.x1 - window.x0);
	real height = (real)(window.y1 - window.y0);
	real scale, x0, y0, bar_width, bar_height, border, health, hurt;
	long font_index;
	long script_flags;

	/* (not while a script holds the camera, as Halo's own HUD) */
	{
		director_perspective perspective = director_get_perspective(local_player_index);

		if (perspective == _director_perspective_scripted || perspective == _director_perspective_neutral)
			return;
	}
	/* (the scripts' hud_show_health and hud_blink_health, as Halo's health
	panel: hud_unit.c's script flags, bit 0 hides it and bit 1 blinks it;
	a10's tutorial shows it only once it has been explained) */
	script_flags = hud_unit_port_script_flags();
	if (TEST_FLAG(script_flags, 0))
		return;
	if (TEST_FLAG(script_flags, 1) && (game_time_get() / 8) % 2)
		return;
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit) || width <= 0.0f || height <= 0.0f)
		return;
	/* (sized from the view's height, 480 the Xbox's, so a split screen's
	view gets a smaller meter) */
	scale = height / 480.0f;
	border = MAX(1.0f, 2.0f * scale);
	bar_width = 150.0f * scale;
	bar_height = 12.0f * scale;
	/* (drawn in the view's own coordinates, so a split screen's second
	view starts at its own top left; a short view keeps room for the label
	above the bar) */
	x0 = 36.0f * scale;
	y0 = MAX(40.0f * scale, 30.0f);
	health = PIN(unit->object.body_vitality, 0.0f, 1.0f);
	/* (recent damage fades over about a second: the flash) */
	hurt = PIN(unit->object.recent_body_damage * 4.0f, 0.0f, 1.0f);

	stubbs_hud_rectangle(x0 - border, y0 - border, x0 + bar_width + border, y0 + bar_height + border,
		stubbs_hud_color(0.75f, 0.05f, 0.06f, 0.04f));
	stubbs_hud_rectangle(x0, y0, x0 + bar_width * health, y0 + bar_height,
		stubbs_hud_color(0.9f,
			0.35f + 0.6f * (1.0f - health) + 0.5f * hurt,
			0.75f * health + 0.15f - 0.4f * hurt,
			0.15f));

	font_index = hud_get_font_index();
	if (font_index != NONE)
	{
		struct font_header *font = font_definition_get(font_index);
		real_argb_color color = *global_real_argb_white;
		rectangle2d bounds;
		short line_height = (short)(font->ascending_height + font->descending_height);

		bounds.x0 = (short)x0;
		bounds.x1 = (short)(x0 + bar_width * 2.0f);
		bounds.y1 = (short)(y0 - border - 1.0f);
		bounds.y0 = (short)(bounds.y1 - line_height);
		color.red = 0.82f;
		color.green = 0.9f;
		color.blue = 0.6f;
		draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
		rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, L"HEALTH");

		/* (the special attacks the scripts have shown, under the bar: each
		named, with how many the body has left; one he has none of is dim) */
		{
			static char const *const names[] = { "GAS", "GRENADE", "HAND", "HEAD" };
			long origin_unit_index = unit->unit.player_index != NONE ?
				stubbs_engine_player_origin_unit(unit->unit.player_index) : unit_index;
			short index;
			real x = x0;

			bounds.y0 = (short)(y0 + bar_height + border + 4.0f * scale);
			bounds.y1 = (short)(bounds.y0 + line_height);
			for (index = 0; index < (short)NUMBEROF(names); index++)
			{
				wchar_t text[32];
				char narrow[32];
				short count, character;

				/* (as the retail HUD: only an attack the scripts showed and
				the unit can hold at all, its most above none) */
				if (!TEST_FLAG(stubbs_hud_special_attacks_shown, index) ||
					stubbs_engine_special_attack_get_maximum(origin_unit_index, index) <= 0)
				{
					continue;
				}
				count = stubbs_engine_special_attack_get_count(origin_unit_index, index);
				if (count > 1)
					snprintf(narrow, sizeof(narrow), "%s %d", names[index], (int)count);
				else
					snprintf(narrow, sizeof(narrow), "%s", names[index]);
				for (character = 0; character < (short)NUMBEROF(text); character++)
				{
					text[character] = (wchar_t)narrow[character];
					if (!narrow[character])
						break;
				}
				text[NUMBEROF(text) - 1] = 0;
				color.red = count > 0 ? 0.82f : 0.45f;
				color.green = count > 0 ? 0.9f : 0.45f;
				color.blue = count > 0 ? 0.6f : 0.4f;
				bounds.x0 = (short)x;
				bounds.x1 = (short)(x + bar_width * 2.0f);
				draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
				rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
				x += (real)(strlen(narrow) + 2) * (real)line_height * 0.7f;
			}
		}
	}
}

/* the scripts' (hud_show_special_attack <index> <shown>): which of the
special attacks the HUD names (a10 shows the gut grenade only once its
tutorial reaches it) */

void stubbs_player_hud_show_special_attack(
	short special_attack,
	boolean show)
{
	if (special_attack >= 0 && special_attack < 32)
		SET_FLAG(stubbs_hud_special_attacks_shown, special_attack, show);
}

#endif
