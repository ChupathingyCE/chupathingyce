/*
H3_HUD.C

Halo 3's HUD drawn on a Halo 3 level (h3_level.c's direct levels), in place of
this engine's unit and weapon HUD: the map's own chud tags, read from the
player's MCC Halo 3 map as the level's HUD is first drawn - nothing of them is
kept in any file.

What is read (MCC's layouts, checked against the owner's guardian.map and
010_jungle.map with ~/halo-test-out/h3pc3-out/hud/tools/chud.py; the field
names are Assembly's Halo 3 plugins', a reference, where they still hold):
- chud_definition ('chdt'): widgets 0 (0x50 each: name string id, special HUD
  type 4, state data 8, placement data 0x14, animation data 0x20, render data
  0x2C, bitmap widgets 0x38, text widgets 0x44), low clip cutoff 0xC, low ammo
  cutoff 0x10, age cutoff 0x14;
- a bitmap widget (0x54): the same six leading fields, then the widget index
  0x38, flags 0x3C, bitmap 0x40, its sprite sequence 0x50; a text widget
  (0x48): flags 0x3C, font 0x40, string id 0x44;
- state data (0x28, twenty words: a widget is drawn when every word it sets
  shares a bit with the player's state): 0x04 the biped (1 Spartan, 2 Elite),
  0x0A the screen's layout (bit 0 a whole 720-line screen), 0x10 the sensor's
  range (bit 1 25 m), 0x16 the zoom (bit 0 none, 1 and 2 its levels), 0x18 the
  hands (bit 0 one weapon, 1 two), 0x1A the unit (bit 0 the sensor, 2..5 the
  grenade chosen, 12..14 overshields, 15 shields), 0x1E the weapon's place
  (bit 0 the right hand, 1 the left, 2 the back), 0x24 the grenade kinds
  carried (bits 8..11), 0x26 the weapon's kind of ammunition;
- placement data (0x1C): anchor 0, origin 4 (-1 its left or top edge at the
  point, 1 its right or bottom, 0 its middle), offset 0xC, scale 0x14; anchor 12
  is the parent widget's point;
- render data (0x40): shader 0, input 4, range input 6, four local colours 8
  (blue, green, red, alpha bytes), four local scalars 0x18, six output
  colours 0x28 (0..3 the locals, 14.. the globals' dynamic colours) and six
  output scalars 0x34 (0 the input, 1 the range input, 2..5 the locals);
- chud_globals ('chgd'): a block of the bipeds' HUDs (the Spartan's first):
  28 dynamic colours from 4, the HUD attributes block 0x74 (0x64 each: the
  warp's angle 4, amount 8 and direction 0xC, the resolution 0x10, the motion
  sensor's offset 0x18 and radius 0x20, the horizontal and vertical scale
  0x28), the grenades' spacing 0x1A8;
- a bitmap ('bitm'): its sprite sequences 0x54 (0x40: sprites 0x34, 0x20
  each - left 8, right 0xC, top 0x10, bottom 0x14 of the image).

How it is drawn: what each of Halo 3's chud shaders does to a sprite's texels
(the H3EK's own shader text, tags\shaders\chud_*.hlsl_include - a private
reference, read for its rules: simple, meter, shield meter, gradient meter,
crosshair, text) is worked out here on the processor into a small texture a
sprite - made again when its meter's value changes - and drawn as this
engine's screen quads, alpha blended. The numbers are this engine's stroke
font (h2_hud_font.inc): Halo 3's fonts are not in the maps.

The helmet's curve is chud_util.fx's chud_transform (h3_hud_warp, which says
what of it is the shader's rule and what was fitted to the tag test).

Not Halo 3's (stated): the widgets' animations (flashes, the sensor's ping,
the dimming); scopes; the scoreboard, medals, messages, waypoints; the numbers'
font; the blips are this engine's.

PC only (HALO_H3_HUD=0: this engine's HUD as before).
*/

#include "cseries.h"
#include "h3_level.h"
#include "h3_cache.h"
#include "h3_spartan.h"
#include "render_epoch.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "game/player_control.h"
#include "interface/hud_definitions.h"
#include "interface/interface.h"
#include "interface/hud_draw.h"
#include "interface/motion_sensor.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/dual_wield.h"
#include "bitmaps/bitmap_group.h"
#include "memory/data.h"
#include "render/render_cameras_internal.h"
#include "cutscene/cinematics.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int h3_hud_draw(short local_player_index, long unit_index);

#if defined(HALO_VITA) || defined(HALO_GXM_NULL)

/* (the Vita and the null build: Halo 3 levels are the desktop's) */
int h3_hud_draw(short local_player_index, long unit_index)
{
	(void)local_player_index;
	(void)unit_index;
	return FALSE;
}

#else

#undef malloc
#undef free
#undef calloc
#undef realloc

void platform_log(char const *format, ...);
void platform_contiguous_side_begin(void);
void platform_contiguous_side_end(void);
extern unsigned long halo_map_generation;

#include "h2_hud_font.inc"

/* ---------- constants */

enum
{
	CHUD_WIDGET_SIZE = 0x50,
	CHUD_BITMAP_WIDGET_SIZE = 0x54,
	CHUD_TEXT_WIDGET_SIZE = 0x48,
	CHUD_STATE_SIZE = 0x28,
	CHUD_STATE_WORDS = 20,
	CHUD_PLACEMENT_SIZE = 0x1C,
	CHUD_ANIMATION_SIZE = 0x78,
	CHUD_RENDER_SIZE = 0x40,
	CHUD_ATTRIBUTES_SIZE = 0x64,

	CHUD_MAXIMUM_DEFINITIONS = 64,
	CHUD_MAXIMUM_WIDGETS = 640,
	CHUD_MAXIMUM_ITEMS = 2400,
	CHUD_MAXIMUM_SOURCES = 96,

	/* the anchors */
	CHUD_ANCHOR_TOP_LEFT = 0,
	CHUD_ANCHOR_TOP_RIGHT,
	CHUD_ANCHOR_BOTTOM_RIGHT,
	CHUD_ANCHOR_BOTTOM_LEFT,
	CHUD_ANCHOR_CENTER,
	CHUD_ANCHOR_TOP_EDGE,
	CHUD_ANCHOR_GRENADE_A,
	CHUD_ANCHOR_GRENADE_D = 9,
	CHUD_ANCHOR_PARENT = 12,
	/* (the canvas's middle: the helmet's edges hang from it. Anchor 4 is the
	crosshair's point, which Halo 3 keeps under the middle - its lowered
	reticle; here the middle too, where this engine aims) */
	CHUD_ANCHOR_MIDDLE = 13,

	/* the shaders (the globals' HUD shaders' order) */
	CHUD_SHADER_SIMPLE = 0,
	CHUD_SHADER_METER,
	CHUD_SHADER_TEXT,
	CHUD_SHADER_METER_SHIELD,
	CHUD_SHADER_METER_GRADIENT,
	CHUD_SHADER_CROSSHAIR,

	/* the inputs (MCC's order: Assembly's Halo 3 names two later from 4 on) */
	CHUD_INPUT_ZERO = 0,
	CHUD_INPUT_ONE,
	CHUD_INPUT_SHIELD = 6,
	CHUD_INPUT_SHIELD_PREVIOUS,
	CHUD_INPUT_CLIP,
	CHUD_INPUT_TOTAL,
	CHUD_INPUT_HEAT,
	CHUD_INPUT_BATTERY,
	CHUD_INPUT_PICKUP,
	CHUD_INPUT_AUTOAIM,
	CHUD_INPUT_GRENADE,
	CHUD_INPUT_GRENADE_COUNT,

	/* the state words */
	CHUD_STATE_BIPED = 0x04 / 2,
	CHUD_STATE_SCREEN = 0x0A / 2,
	CHUD_STATE_SENSOR_RANGE = 0x10 / 2,
	CHUD_STATE_ZOOM = 0x16 / 2,
	CHUD_STATE_HANDS = 0x18 / 2,
	CHUD_STATE_UNIT = 0x1A / 2,
	CHUD_STATE_AUTOAIM = 0x1C / 2,
	CHUD_STATE_WEAPON_PLACE = 0x1E / 2,
	CHUD_STATE_LOCK = 0x20 / 2,
	CHUD_STATE_GRENADES = 0x24 / 2,
	CHUD_STATE_AMMUNITION = 0x26 / 2,

	CHUD_ITEM_BITMAP = 0,
	CHUD_ITEM_TEXT,
};

/* ---------- structures */

struct chud_placement
{
	boolean present;
	short anchor;
	float origin[2], offset[2], scale[2];
};

struct chud_render
{
	boolean present;
	short shader, input, range_input;
	float local_color[4][3];
	float local_scalar[4];
	short output_color[6], output_scalar[6];
};

struct chud_item
{
	char name[32];
	unsigned char kind;
	boolean hidden;             /* (its resting animation is "invisible", or the sensor's ping: not drawn) */
	unsigned short state[CHUD_STATE_WORDS];
	struct chud_placement placement;
	struct chud_render render;
	unsigned long flags;
	short font;
	/* (a bitmap's) its sprite's texels, RGBA, crop_width by crop_height (NULL: none) */
	long bitmap_tag;
	short sprite;
	unsigned char *crop;
	long crop_width, crop_height;
	/* its texture as last made: for these values, at this size */
	struct bitmap_data *texture;
	unsigned char *texels;
	long made_width, made_height, texture_width, texture_height;
	float made_key[4];
	boolean made;
};

struct chud_widget
{
	char name[32];
	short special;
	unsigned short state[CHUD_STATE_WORDS];
	struct chud_placement placement;
	long first_item, item_count;
};

struct chud_definition
{
	long tag;
	char name[64];
	long first_widget, widget_count;
	long low_clip, low_ammo;
};

struct chud_values
{
	unsigned short state[CHUD_STATE_WORDS];
	float shield, shield_previous;
	float clip, total, heat, battery;
	float autoaim;
	short grenade_counts[4];
	short grenade;              /* (the widget's own: 0..3, NONE not a grenade's) */
};

static struct
{
	long scenario_index;
	unsigned long generation;
	boolean tried, loaded;
	long definition_count, widget_count, item_count;
	struct chud_definition definitions[CHUD_MAXIMUM_DEFINITIONS];
	struct chud_widget widgets[CHUD_MAXIMUM_WIDGETS];
	struct chud_item items[CHUD_MAXIMUM_ITEMS];
	long unit_definition;
	/* the weapons' definitions: a Halo 3 weapon's name (its tag's last part) and its HUD */
	long weapon_count;
	struct { char name[48]; long definition; } weapons[CHUD_MAXIMUM_DEFINITIONS];
	/* the globals' */
	float colors[32][3];
	float warp_angle, warp_amount, warp_direction;
	float resolution[2];
	float sensor_offset[2], sensor_radius;
	float scale[2];
	float grenade_spacing;
	struct bitmap_data *white;
	/* the view it is drawn for */
	float view_scale, view_width, view_height;
	float reticle_drop;         /* (the crosshair's drop under the middle, canvas units; 0 none) */
} h3_hud;

/* ---------- private code: bytes */

static unsigned long get32(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static short get16(unsigned char const *p)
{
	return (short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static float getf(unsigned char const *p)
{
	float f;

	memcpy(&f, p, 4);
	return f == f && fabsf(f) < 1.0e6f ? f : 0.0f;
}

static char const *h3_hud_setting(char const *name)
{
	return getenv(name);
}

/* ---------- private code: textures */

static struct bitmap_data *texture_new(long width, long height, unsigned char const *bgra)
{
	struct bitmap_data *bitmap = malloc(sizeof(*bitmap));
	boolean made;

	if (!bitmap)
		return NULL;
	memset(bitmap, 0, sizeof(*bitmap));
	bitmap->signature = BITMAP_GROUP_TAG;
	bitmap->width = (short)width;
	bitmap->height = (short)height;
	bitmap->depth = 1;
	bitmap->type = 0;
	bitmap->format = 11;   /* (A8R8G8B8) */
	bitmap->flags = FLAG(0);   /* (power of two) */
	bitmap->tag_index = NONE;
	bitmap->cache_block_index = NONE;
	bitmap->base_address = (void *)bgra;
	platform_contiguous_side_begin();
	made = rasterizer_bitmap_new(bitmap) && bitmap->hardware_format;
	if (made)
		rasterizer_bitmap_changed(bitmap);
	platform_contiguous_side_end();
	bitmap->base_address = NULL;
	if (!made)
	{
		free(bitmap);
		return NULL;
	}
	return bitmap;
}

static void texture_update(struct bitmap_data *bitmap, unsigned char const *bgra)
{
	bitmap->base_address = (void *)bgra;
	rasterizer_bitmap_changed(bitmap);
	bitmap->base_address = NULL;
}

static void texture_free(struct bitmap_data *bitmap)
{
	if (!bitmap)
		return;
	if (bitmap->hardware_format)
		rasterizer_bitmap_delete(bitmap);
	free(bitmap);
}

static void h3_hud_discard(void)
{
	long i;

	for (i = 0; i < h3_hud.item_count; i++)
	{
		texture_free(h3_hud.items[i].texture);
		free(h3_hud.items[i].texels);
		free(h3_hud.items[i].crop);
	}
	texture_free(h3_hud.white);
	memset(&h3_hud, 0, sizeof(h3_hud));
	h3_hud.scenario_index = NONE;
}

/* ---------- private code: the tags read */

static void state_read(struct h3_cache *cache, unsigned long field, unsigned short *state)
{
	long count;
	unsigned long elements;
	unsigned char const *p;
	int k;

	memset(state, 0, CHUD_STATE_WORDS * sizeof(*state));
	if (!h3_cache_block(cache, field, CHUD_STATE_SIZE, &count, &elements) || count < 1 ||
		!(p = h3_cache_at(cache, elements, CHUD_STATE_SIZE)))
	{
		return;
	}
	for (k = 0; k < CHUD_STATE_WORDS; k++)
		state[k] = (unsigned short)get16(p + 2 * k);
}

static void placement_read(struct h3_cache *cache, unsigned long field, struct chud_placement *placement)
{
	long count;
	unsigned long elements;
	unsigned char const *p;

	memset(placement, 0, sizeof(*placement));
	placement->scale[0] = placement->scale[1] = 1.0f;
	if (!h3_cache_block(cache, field, CHUD_PLACEMENT_SIZE, &count, &elements) || count < 1 ||
		!(p = h3_cache_at(cache, elements, CHUD_PLACEMENT_SIZE)))
	{
		return;
	}
	placement->present = TRUE;
	placement->anchor = get16(p);
	placement->origin[0] = getf(p + 4);
	placement->origin[1] = getf(p + 8);
	placement->offset[0] = getf(p + 0xC);
	placement->offset[1] = getf(p + 0x10);
	placement->scale[0] = getf(p + 0x14);
	placement->scale[1] = getf(p + 0x18);
}

static void render_read(struct h3_cache *cache, unsigned long field, struct chud_render *render)
{
	long count;
	unsigned long elements;
	unsigned char const *p;
	int k;

	memset(render, 0, sizeof(*render));
	if (!h3_cache_block(cache, field, CHUD_RENDER_SIZE, &count, &elements) || count < 1 ||
		!(p = h3_cache_at(cache, elements, CHUD_RENDER_SIZE)))
	{
		return;
	}
	render->present = TRUE;
	render->shader = get16(p);
	render->input = get16(p + 4);
	render->range_input = get16(p + 6);
	for (k = 0; k < 4; k++)
	{
		render->local_color[k][0] = (float)p[8 + 4 * k + 2] / 255.0f;
		render->local_color[k][1] = (float)p[8 + 4 * k + 1] / 255.0f;
		render->local_color[k][2] = (float)p[8 + 4 * k] / 255.0f;
		render->local_scalar[k] = getf(p + 0x18 + 4 * k);
	}
	for (k = 0; k < 6; k++)
	{
		render->output_color[k] = get16(p + 0x28 + 2 * k);
		render->output_scalar[k] = get16(p + 0x34 + 2 * k);
	}
}

/* whether a widget rests unseen: its second animation (the one it plays at
rest; the others are its states' - a warning, the shields out) is the tag
"invisible", or it is the sensor's ping or a voice's ring, which only an
animation shows right */
static boolean animation_hides(struct h3_cache *cache, unsigned long field)
{
	long count, tag;
	unsigned long elements;
	char const *name;

	if (!h3_cache_block(cache, field, CHUD_ANIMATION_SIZE, &count, &elements) || count < 1)
		return FALSE;
	tag = h3_cache_reference(cache, elements + 0x14 + 4);
	name = tag != NONE ? h3_cache_tag_name(cache, tag) : NULL;
	return name && (strstr(name, "invisible") || strstr(name, "motion_tracker_ping") || strstr(name, "\\vox"));
}

static void name_read(struct h3_cache *cache, unsigned long string_id, char *out, unsigned long size)
{
	char const *text = h3_cache_string(cache, string_id);

	snprintf(out, size, "%s", text ? text : "");
}

/* a definition read: its index, NONE */
static long definition_read(struct h3_cache *cache, long tag)
{
	struct h3_cache_tag t;
	struct chud_definition *d;
	long count, i, k;
	unsigned long widgets;

	for (i = 0; i < h3_hud.definition_count; i++)
	{
		if (h3_hud.definitions[i].tag == tag)
			return i;
	}
	if (h3_hud.definition_count >= CHUD_MAXIMUM_DEFINITIONS || !h3_cache_tag_get(cache, tag, &t) || t.group_tag != 'chdt' ||
		!h3_cache_block(cache, t.address, CHUD_WIDGET_SIZE, &count, &widgets))
	{
		return NONE;
	}
	d = &h3_hud.definitions[h3_hud.definition_count];
	memset(d, 0, sizeof(*d));
	d->tag = tag;
	snprintf(d->name, sizeof(d->name), "%s", h3_cache_tag_name(cache, tag));
	d->first_widget = h3_hud.widget_count;
	d->low_clip = (long)h3_cache_read32(cache, t.address + 0xC);
	d->low_ammo = (long)h3_cache_read32(cache, t.address + 0x10);
	for (i = 0; i < count && h3_hud.widget_count < CHUD_MAXIMUM_WIDGETS; i++)
	{
		unsigned long at = widgets + CHUD_WIDGET_SIZE * (unsigned long)i, elements;
		struct chud_widget *w = &h3_hud.widgets[h3_hud.widget_count];
		long item_count;
		int pass;

		memset(w, 0, sizeof(*w));
		name_read(cache, h3_cache_read32(cache, at), w->name, sizeof(w->name));
		w->special = h3_cache_read16(cache, at + 4);
		state_read(cache, at + 8, w->state);
		placement_read(cache, at + 0x14, &w->placement);
		w->first_item = h3_hud.item_count;
		for (pass = 0; pass < 2; pass++)
		{
			unsigned long size = pass ? CHUD_TEXT_WIDGET_SIZE : CHUD_BITMAP_WIDGET_SIZE;

			if (!h3_cache_block(cache, at + (pass ? 0x44 : 0x38), size, &item_count, &elements))
				continue;
			for (k = 0; k < item_count && h3_hud.item_count < CHUD_MAXIMUM_ITEMS; k++)
			{
				unsigned long ia = elements + size * (unsigned long)k;
				struct chud_item *item = &h3_hud.items[h3_hud.item_count++];

				memset(item, 0, sizeof(*item));
				item->kind = pass ? CHUD_ITEM_TEXT : CHUD_ITEM_BITMAP;
				name_read(cache, h3_cache_read32(cache, ia), item->name, sizeof(item->name));
				state_read(cache, ia + 8, item->state);
				placement_read(cache, ia + 0x14, &item->placement);
				item->hidden = animation_hides(cache, ia + 0x20);
				render_read(cache, ia + 0x2C, &item->render);
				item->flags = h3_cache_read32(cache, ia + 0x3C);
				item->bitmap_tag = NONE;
				if (pass)
					item->font = h3_cache_read16(cache, ia + 0x40);
				else
				{
					unsigned char const *sprite = h3_cache_at(cache, ia + 0x50, 1);

					item->bitmap_tag = h3_cache_reference(cache, ia + 0x40);
					item->sprite = sprite ? (short)sprite[0] : 0;
				}
			}
		}
		w->item_count = h3_hud.item_count - w->first_item;
		h3_hud.widget_count++;
	}
	d->widget_count = h3_hud.widget_count - d->first_widget;
	return h3_hud.definition_count++;
}

/* whether a widget's anchor is one this draws (the screen's corners, edges
and middle, the grenades') */
static boolean anchor_known(short anchor)
{
	return (anchor >= CHUD_ANCHOR_TOP_LEFT && anchor <= CHUD_ANCHOR_GRENADE_D) || anchor == CHUD_ANCHOR_MIDDLE;
}

/* whether an item can come to be drawn at all (what its sprite is read for) */
static boolean item_wanted(struct chud_widget const *w, struct chud_item const *item)
{
	short anchor = item->placement.present ? item->placement.anchor : CHUD_ANCHOR_PARENT;

	if (item->hidden || !item->render.present)
		return FALSE;
	/* (a scope's: the zoomed states alone) */
	if ((w->state[CHUD_STATE_ZOOM] && !(w->state[CHUD_STATE_ZOOM] & 1)) || (item->state[CHUD_STATE_ZOOM] && !(item->state[CHUD_STATE_ZOOM] & 1)))
		return FALSE;
	if (w->state[CHUD_STATE_SCREEN] && !(w->state[CHUD_STATE_SCREEN] & 1))
		return FALSE;
	if (item->state[CHUD_STATE_SCREEN] && !(item->state[CHUD_STATE_SCREEN] & 1))
		return FALSE;
	if (anchor == CHUD_ANCHOR_PARENT)
		return w->placement.present && anchor_known(w->placement.anchor);
	return anchor_known(anchor);
}

/* the sprites' texels: every wanted bitmap item's bitmap read (each tag
once), its sprite cut out of it */
static void sprites_read(struct h3_cache *cache)
{
	long tags[CHUD_MAXIMUM_SOURCES], count = 0, d, w, i, k;
	unsigned char *pixels[CHUD_MAXIMUM_SOURCES];
	unsigned long widths[CHUD_MAXIMUM_SOURCES], heights[CHUD_MAXIMUM_SOURCES];

	for (d = 0; d < h3_hud.definition_count; d++)
	{
		for (w = h3_hud.definitions[d].first_widget; w < h3_hud.definitions[d].first_widget + h3_hud.definitions[d].widget_count; w++)
		{
			struct chud_widget const *widget = &h3_hud.widgets[w];

			for (i = widget->first_item; i < widget->first_item + widget->item_count; i++)
			{
				struct chud_item const *item = &h3_hud.items[i];

				if (item->kind != CHUD_ITEM_BITMAP || item->bitmap_tag == NONE || !item_wanted(widget, item))
					continue;
				for (k = 0; k < count && tags[k] != item->bitmap_tag; k++)
					;
				if (k == count && count < CHUD_MAXIMUM_SOURCES)
					tags[count++] = item->bitmap_tag;
			}
		}
	}
	h3_bitmaps_read_rgba(cache, tags, count, pixels, widths, heights);
	for (d = 0; d < h3_hud.definition_count; d++)
	{
		for (w = h3_hud.definitions[d].first_widget; w < h3_hud.definitions[d].first_widget + h3_hud.definitions[d].widget_count; w++)
		{
			struct chud_widget const *widget = &h3_hud.widgets[w];

			for (i = widget->first_item; i < widget->first_item + widget->item_count; i++)
			{
				struct chud_item *item = &h3_hud.items[i];
				struct h3_cache_tag t;
				long sequence_count = 0, sprite_count = 0, x0, y0, x1, y1, y;
				unsigned long sequences = 0, sprites = 0;
				float rect[4] = { 0.0f, 1.0f, 0.0f, 1.0f };

				if (item->kind != CHUD_ITEM_BITMAP || item->bitmap_tag == NONE || !item_wanted(widget, item))
					continue;
				for (k = 0; k < count && tags[k] != item->bitmap_tag; k++)
					;
				if (k == count || !pixels[k] || !h3_cache_tag_get(cache, item->bitmap_tag, &t))
					continue;
				if (h3_cache_block(cache, t.address + 0x54, 0x40, &sequence_count, &sequences) && item->sprite < sequence_count &&
					h3_cache_block(cache, sequences + 0x40 * (unsigned long)item->sprite + 0x34, 0x20, &sprite_count, &sprites) && sprite_count > 0)
				{
					unsigned char const *p = h3_cache_at(cache, sprites, 0x20);

					if (p)
					{
						rect[0] = getf(p + 8);
						rect[1] = getf(p + 0xC);
						rect[2] = getf(p + 0x10);
						rect[3] = getf(p + 0x14);
					}
				}
				x0 = (long)(rect[0] * (float)widths[k] + 0.5f);
				x1 = (long)(rect[1] * (float)widths[k] + 0.5f);
				y0 = (long)(rect[2] * (float)heights[k] + 0.5f);
				y1 = (long)(rect[3] * (float)heights[k] + 0.5f);
				if (x0 < 0 || y0 < 0 || x1 > (long)widths[k] || y1 > (long)heights[k] || x1 <= x0 || y1 <= y0)
					continue;
				item->crop_width = x1 - x0;
				item->crop_height = y1 - y0;
				item->crop = malloc((size_t)item->crop_width * (size_t)item->crop_height * 4);
				if (!item->crop)
					continue;
				for (y = 0; y < item->crop_height; y++)
					memcpy(item->crop + 4 * y * item->crop_width, pixels[k] + 4 * ((y0 + y) * (long)widths[k] + x0), (size_t)item->crop_width * 4);
			}
		}
	}
	for (k = 0; k < count; k++)
		h3_bitmap_free(pixels[k]);
	platform_log("h3 hud: %ld bitmaps read for the sprites", count);
}

static void globals_read(struct h3_cache *cache)
{
	long tag = h3_cache_find_tag(cache, 'chgd', NULL), count, attribute_count, k;
	struct h3_cache_tag t;
	unsigned long bipeds, attributes;
	unsigned char const *p;

	h3_hud.resolution[0] = 1280.0f;
	h3_hud.resolution[1] = 720.0f;
	h3_hud.scale[0] = h3_hud.scale[1] = 0.87f;
	h3_hud.grenade_spacing = 53.0f;
	h3_hud.sensor_radius = 58.0f;
	for (k = 0; k < 32; k++)
		h3_hud.colors[k][0] = h3_hud.colors[k][1] = h3_hud.colors[k][2] = 1.0f;
	if (tag == NONE || !h3_cache_tag_get(cache, tag, &t) || !h3_cache_block(cache, t.address, 0x1C8, &count, &bipeds) || count < 1)
	{
		platform_log("h3 hud: no chud globals: its defaults");
		return;
	}
	if ((p = h3_cache_at(cache, bipeds + 4, 28 * 4)) != NULL)
	{
		for (k = 0; k < 28; k++)
		{
			h3_hud.colors[k][0] = (float)p[4 * k + 2] / 255.0f;
			h3_hud.colors[k][1] = (float)p[4 * k + 1] / 255.0f;
			h3_hud.colors[k][2] = (float)p[4 * k] / 255.0f;
		}
	}
	if (h3_cache_block(cache, bipeds + 0x74, CHUD_ATTRIBUTES_SIZE, &attribute_count, &attributes) && attribute_count > 0 &&
		(p = h3_cache_at(cache, attributes, CHUD_ATTRIBUTES_SIZE)) != NULL)
	{
		h3_hud.warp_angle = getf(p + 4);
		h3_hud.warp_amount = getf(p + 8);
		h3_hud.warp_direction = getf(p + 0xC);
		if (get32(p + 0x10) >= 320 && get32(p + 0x10) <= 8192 && get32(p + 0x14) >= 240 && get32(p + 0x14) <= 8192)
		{
			h3_hud.resolution[0] = (float)get32(p + 0x10);
			h3_hud.resolution[1] = (float)get32(p + 0x14);
		}
		h3_hud.sensor_offset[0] = getf(p + 0x18);
		h3_hud.sensor_offset[1] = getf(p + 0x1C);
		if (getf(p + 0x20) > 1.0f)
			h3_hud.sensor_radius = getf(p + 0x20);
		if (getf(p + 0x28) > 0.3f && getf(p + 0x28) <= 1.0f && getf(p + 0x2C) > 0.3f && getf(p + 0x2C) <= 1.0f)
		{
			h3_hud.scale[0] = getf(p + 0x28);
			h3_hud.scale[1] = getf(p + 0x2C);
		}
	}
	if (h3_cache_readf(cache, bipeds + 0x1A8) > 8.0f && h3_cache_readf(cache, bipeds + 0x1A8) < 400.0f)
		h3_hud.grenade_spacing = h3_cache_readf(cache, bipeds + 0x1A8);
	platform_log("h3 hud: globals: warp angle %.4f amount %.2f direction %.2f, %gx%g, scale %.2f %.2f, sensor %.0f %.0f radius %.0f, grenades %.0f apart",
		h3_hud.warp_angle, h3_hud.warp_amount, h3_hud.warp_direction, h3_hud.resolution[0], h3_hud.resolution[1], h3_hud.scale[0],
		h3_hud.scale[1], h3_hud.sensor_offset[0], h3_hud.sensor_offset[1], h3_hud.sensor_radius, h3_hud.grenade_spacing);
}

char const *h3_maps_directory_of(char const *name);

/* (each load: what is worked out of the globals once - the curve - is worked out again) */
static unsigned long h3_hud_load_serial;

static void h3_hud_load(void)
{
	char const *scenario = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	char const *setting = h3_hud_setting("HALO_H3_HUD");
	struct h3_cache *cache;
	char why[H3_CACHE_WHY_SIZE];
	long count, i, unit_tag;

	if (h3_hud.tried && h3_hud.scenario_index == global_scenario_index && h3_hud.generation == halo_map_generation)
		return;
	h3_hud_discard();
	h3_hud_load_serial++;
	h3_hud.tried = TRUE;
	h3_hud.scenario_index = global_scenario_index;
	h3_hud.generation = halo_map_generation;
	if (!scenario || strncmp(scenario, "h3\\", 3) || !h3_level_direct_loaded() || (setting && !atoi(setting)))
		return;
	why[0] = 0;
	cache = h3_cache_open(h3_maps_directory_of(scenario + 3), scenario + 3, why, sizeof(why));
	if (!cache)
	{
		platform_log("h3 hud: %s: %s: this engine's HUD", scenario + 3, why);
		return;
	}
	globals_read(cache);
	/* the unit's: the Spartan's */
	unit_tag = h3_cache_find_tag(cache, 'chdt', "ui\\chud\\spartan");
	h3_hud.unit_definition = unit_tag != NONE ? definition_read(cache, unit_tag) : NONE;
	/* each weapon's: the HUD its tag names */
	count = h3_cache_tag_count(cache);
	for (i = 0; i < count && h3_hud.weapon_count < CHUD_MAXIMUM_DEFINITIONS; i++)
	{
		struct h3_cache_tag t;
		unsigned long at;
		char const *name, *last;
		long definition = NONE;

		if (!h3_cache_tag_get(cache, i, &t) || t.group_tag != 'weap' || !t.address)
			continue;
		for (at = 0; at < 0x580 && definition == NONE; at += 4)
		{
			unsigned char const *p = h3_cache_at(cache, t.address + at, 0x10);
			long tag;
			struct h3_cache_tag referenced;

			if (!p)
				break;
			if (get32(p) != 'chdt')
				continue;
			tag = h3_cache_tag_from_datum(cache, get32(p + 0xC));
			if (tag != NONE && h3_cache_tag_get(cache, tag, &referenced) && referenced.group_tag == 'chdt')
				definition = definition_read(cache, tag);
		}
		if (definition == NONE)
			continue;
		name = h3_cache_tag_name(cache, i);
		last = strrchr(name, '\\');
		snprintf(h3_hud.weapons[h3_hud.weapon_count].name, sizeof(h3_hud.weapons[0].name), "%s", last ? last + 1 : name);
		h3_hud.weapons[h3_hud.weapon_count++].definition = definition;
	}
	sprites_read(cache);
	h3_cache_close(cache);
	{
		static unsigned char const white[4 * 4 * 4] =
		{
			255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
			255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		};

		h3_hud.white = texture_new(4, 4, white);
	}
	h3_hud.loaded = h3_hud.unit_definition != NONE;
	platform_log("h3 hud: %s: %ld HUDs (%ld weapons'), %ld widgets, %ld bitmaps and texts%s", scenario + 3, h3_hud.definition_count,
		h3_hud.weapon_count, h3_hud.widget_count, h3_hud.item_count, h3_hud.loaded ? "" : " - no Spartan HUD: this engine's HUD");
}

/* ---------- private code: where things go */

/* ---------- the helmet's curve

chud_util.fx's chud_transform: a point of the canvas (0..1 across and down)
is a direction between four basis vectors - the canvas's corners -, turned
from one to the next along great circles (its normals_interpolate), and is seen
from a point chud_project_scale_and_offset.z behind the sphere's middle:
x = centre.x * scale / (offset_z + centre.z). The shader has the rule; the
basis vectors and the projection's numbers are the game's own, made from the
globals' HUD attributes in code this has not seen. What is used for them,
found by fitting the rule to the tag test's frames at 1280x720 (17 points of
the HUD, ~/halo-test-out/h3pc3-out/NOTES-hud.md): the corners yawed by the warp
angle times the warp amount (0.0785 x 1.2 = 0.0942 rad; the fit alone gave
0.0938) and pitched by that times the canvas's height over its width, the
offset the warp direction (-0.95), and the whole scaled so that the canvas's
corners land on the globals' horizontal and vertical scale (0.87) of the
view. HALO_H3_HUD_WARP=0: flat (the corners where they are, straight between) */

static struct
{
	boolean made, flat;
	unsigned long serial;
	float corners[4][3];        /* top left, top right, bottom left, bottom right */
	float offset_z, project[2];
} h3_hud_curve;

static void chud_normals_interpolate(float const *a, float const *b, float t, float *out)
{
	float aa = a[0] * a[0] + a[1] * a[1] + a[2] * a[2], bb = b[0] * b[0] + b[1] * b[1] + b[2] * b[2], ab = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	float c = 2.0f * ab * ab / (aa * bb) - 1.0f, angle, sine, cosine, n[3], length, dot;

	c = c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c;
	angle = 0.5f * acosf(c) * t;
	n[0] = a[1] * b[2] - a[2] * b[1];
	n[1] = a[2] * b[0] - a[0] * b[2];
	n[2] = a[0] * b[1] - a[1] * b[0];
	length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (length < 1.0e-9f)
	{
		out[0] = a[0], out[1] = a[1], out[2] = a[2];
		return;
	}
	n[0] /= length, n[1] /= length, n[2] /= length;
	sine = sinf(angle);
	cosine = cosf(angle);
	dot = (1.0f - cosine) * (a[0] * n[0] + a[1] * n[1] + a[2] * n[2]);
	out[0] = cosine * a[0] + dot * n[0] - sine * (a[1] * n[2] - a[2] * n[1]);
	out[1] = cosine * a[1] + dot * n[1] - sine * (a[2] * n[0] - a[0] * n[2]);
	out[2] = cosine * a[2] + dot * n[2] - sine * (a[0] * n[1] - a[1] * n[0]);
}

static void h3_hud_warp(float x, float y, float *out_x, float *out_y)
{
	float w = h3_hud.view_width, h = h3_hud.resolution[1];

	if (!h3_hud_curve.made || h3_hud_curve.serial != h3_hud_load_serial)
	{
		char const *setting = h3_hud_setting("HALO_H3_HUD_WARP");
		float yaw = h3_hud.warp_angle * h3_hud.warp_amount, pitch = yaw * h3_hud.resolution[1] / h3_hud.resolution[0];
		int k;

		h3_hud_curve.made = TRUE;
		h3_hud_curve.serial = h3_hud_load_serial;
		h3_hud_curve.flat = (setting && !atoi(setting)) || yaw <= 0.0f || yaw > 1.0f;
		for (k = 0; k < 4; k++)
		{
			h3_hud_curve.corners[k][0] = (k & 1 ? 1.0f : -1.0f) * sinf(yaw) * cosf(pitch);
			h3_hud_curve.corners[k][1] = (k & 2 ? -1.0f : 1.0f) * sinf(pitch);
			h3_hud_curve.corners[k][2] = cosf(yaw) * cosf(pitch);
		}
		h3_hud_curve.offset_z = h3_hud.warp_direction;
		if (!h3_hud_curve.flat && fabsf(h3_hud_curve.offset_z + h3_hud_curve.corners[1][2]) < 1.0e-4f)
			h3_hud_curve.flat = TRUE;
		if (!h3_hud_curve.flat)
		{
			h3_hud_curve.project[0] = (h3_hud_curve.offset_z + h3_hud_curve.corners[1][2]) / h3_hud_curve.corners[1][0];
			h3_hud_curve.project[1] = (h3_hud_curve.offset_z + h3_hud_curve.corners[1][2]) / h3_hud_curve.corners[1][1];
		}
	}
	if (h3_hud_curve.flat)
	{
		x = 0.5f * w + (x - 0.5f * w) * h3_hud.scale[0];
		y = 0.5f * h + (y - 0.5f * h) * h3_hud.scale[1];
	}
	else
	{
		float top[3], bottom[3], centre[3], length, denominator;

		chud_normals_interpolate(h3_hud_curve.corners[0], h3_hud_curve.corners[1], x / w, top);
		chud_normals_interpolate(h3_hud_curve.corners[2], h3_hud_curve.corners[3], x / w, bottom);
		chud_normals_interpolate(top, bottom, y / h, centre);
		length = sqrtf(centre[0] * centre[0] + centre[1] * centre[1] + centre[2] * centre[2]);
		denominator = h3_hud_curve.offset_z + centre[2] / length;
		if (fabsf(denominator) < 1.0e-5f)
			denominator = 1.0e-5f;
		x = 0.5f * w + 0.5f * w * h3_hud.scale[0] * (centre[0] / length) * h3_hud_curve.project[0] / denominator;
		y = 0.5f * h - 0.5f * h * h3_hud.scale[1] * (centre[1] / length) * h3_hud_curve.project[1] / denominator;
	}
	*out_x = x * h3_hud.view_scale;
	*out_y = y * h3_hud.view_scale;
}

/* an anchor's point on the canvas: its corners, edges and middle, the
grenades' along the top from its left corner (the globals' spacing) */
static boolean anchor_point(short anchor, float *x, float *y)
{
	float w = h3_hud.view_width, h = h3_hud.resolution[1];

	switch (anchor)
	{
	case CHUD_ANCHOR_TOP_LEFT: *x = 0.0f, *y = 0.0f; break;
	case CHUD_ANCHOR_TOP_RIGHT: *x = w, *y = 0.0f; break;
	case CHUD_ANCHOR_BOTTOM_RIGHT: *x = w, *y = h; break;
	case CHUD_ANCHOR_BOTTOM_LEFT: *x = 0.0f, *y = h; break;
	case CHUD_ANCHOR_CENTER:
		/* (the crosshair's point: under the middle by Halo 3's lowered
		reticle where the view has it - field_of_view.c -, the canvas's point
		whose place through the curve is that far down) */
		*x = 0.5f * w, *y = 0.5f * h;
		if (h3_hud.reticle_drop > 0.0f)
		{
			float low = 0.5f * h, high = 0.5f * h + 2.0f * h3_hud.reticle_drop, ox, oy;
			int k;

			for (k = 0; k < 20; k++)
			{
				float middle = 0.5f * (low + high);

				h3_hud_warp(0.5f * w, middle, &ox, &oy);
				if (oy / h3_hud.view_scale < 0.5f * h + h3_hud.reticle_drop)
					low = middle;
				else
					high = middle;
			}
			*y = 0.5f * (low + high);
			if (h3_hud_setting("HALO_H3_HUD_LOG"))
			{
				static int logged;

				if (logged++ < 3)
				{
					h3_hud_warp(0.5f * w, *y, &ox, &oy);
					platform_log("h3 hud: the crosshair's point: drop %.2f, canvas y %.2f -> view %.2f %.2f (view scale %.3f, canvas %.0f x %.0f)",
						h3_hud.reticle_drop, *y, ox, oy, h3_hud.view_scale, w, h);
				}
			}
		}
		break;
	case CHUD_ANCHOR_MIDDLE: *x = 0.5f * w, *y = 0.5f * h; break;
	case CHUD_ANCHOR_TOP_EDGE: *x = 0.5f * w, *y = 0.0f; break;
	default:
		if (anchor >= CHUD_ANCHOR_GRENADE_A && anchor <= CHUD_ANCHOR_GRENADE_D)
		{
			*x = h3_hud.grenade_spacing * (float)(anchor - CHUD_ANCHOR_GRENADE_A);
			*y = 0.0f;
			break;
		}
		return FALSE;
	}
	return TRUE;
}

/* an item's point (its placement's, on its widget's) and scale */
static boolean item_point(struct chud_widget const *w, struct chud_item const *item, float *x, float *y, float scale[2])
{
	float px = 0.0f, py = 0.0f, sx = 1.0f, sy = 1.0f;
	short anchor = item->placement.present ? item->placement.anchor : CHUD_ANCHOR_PARENT;

	if (anchor == CHUD_ANCHOR_PARENT)
	{
		if (!w->placement.present || !anchor_point(w->placement.anchor, &px, &py))
			return FALSE;
		px += w->placement.offset[0];
		py += w->placement.offset[1];
		sx = w->placement.scale[0];
		sy = w->placement.scale[1];
	}
	else if (!anchor_point(anchor, &px, &py))
		return FALSE;
	*x = px + item->placement.offset[0] * sx;
	*y = py + item->placement.offset[1] * sy;
	scale[0] = sx * (item->placement.present ? item->placement.scale[0] : 1.0f);
	scale[1] = sy * (item->placement.present ? item->placement.scale[1] : 1.0f);
	return TRUE;
}

/* ---------- private code: the shaders */

static float input_value(short input, struct chud_values const *values)
{
	switch (input)
	{
	case CHUD_INPUT_ONE: return 1.0f;
	case CHUD_INPUT_SHIELD: return 255.0f * values->shield;
	case CHUD_INPUT_SHIELD_PREVIOUS: return 255.0f * values->shield_previous;
	case CHUD_INPUT_CLIP: return values->clip;
	case CHUD_INPUT_TOTAL: return values->total;
	case CHUD_INPUT_HEAT: return 255.0f * values->heat;
	case CHUD_INPUT_BATTERY: return 100.0f * values->battery;
	case CHUD_INPUT_AUTOAIM: return values->autoaim;
	case CHUD_INPUT_GRENADE_COUNT: return values->grenade != NONE ? (float)values->grenade_counts[values->grenade] : 0.0f;
	default: return 0.0f;
	}
}

static float output_scalar(struct chud_render const *render, int k, struct chud_values const *values)
{
	short which = render->output_scalar[k];

	if (which == 0)
		return input_value(render->input, values);
	if (which == 1)
		return input_value(render->range_input, values);
	if (which >= 2 && which <= 5)
		return render->local_scalar[which - 2];
	return 0.0f;
}

static float const *output_color(struct chud_render const *render, int k)
{
	static float const white[3] = { 1.0f, 1.0f, 1.0f };
	short which = render->output_color[k];

	if (which >= 0 && which <= 3)
		return render->local_color[which];
	if (which >= 14 && which < 14 + 28)
		return h3_hud.colors[which - 14];
	return white;
}

static boolean h3_hud_gamma(void)
{
	static int on = -1;

	if (on < 0)
		on = !(h3_hud_setting("HALO_H3_HUD_GAMMA") && !atoi(h3_hud_setting("HALO_H3_HUD_GAMMA")));
	return on;
}

/* a texel through the item's shader (the H3EK's chud_*.hlsl): t the sprite's
RGBA (0..1), u its place across the sprite; out its colour and alpha */
static void shade(struct chud_render const *render, float const scalar[6], float const *t, float u, float out[4])
{
	float const *a = output_color(render, 0), *b = output_color(render, 1), *c = output_color(render, 2), *d = output_color(render, 3);
	int k;

	switch (render->shader)
	{
	case CHUD_SHADER_METER:
		/* (chud_meter: blue the texel's meter value - a round's number; filled
		under the amount: the primary and secondary colours by green, else the
		empty colour; scalar C the filled alpha's scale, D the empty's) */
		if (255.0f * t[2] - scalar[0] < 0.5f)
		{
			for (k = 0; k < 3; k++)
				out[k] = a[k] * t[1] + b[k] * (1.0f - t[1]);
			out[3] = t[3] * scalar[2];
		}
		else
		{
			for (k = 0; k < 3; k++)
				out[k] = c[k];
			out[3] = t[3] * scalar[3];
		}
		break;
	case CHUD_SHADER_METER_SHIELD:
	{
		/* (chud_meter_shield: the meter's value across the sprite; up to the
		amount the primary and secondary colours, from there to the amount it
		had - the damage just taken - the flash colour coming in over a quarter
		of the gap, past that the empty colour at scalar D's alpha) */
		float const *e = output_color(render, 4);
		float value = 255.0f * u, regular = scalar[0], previous = scalar[1], radius = 0.25f * (previous - regular);
		float flash = 0.0f, flash_amount = scalar[4];

		if (value > regular)
			flash = radius > 0.0f && value < regular + radius ? (value - regular) / radius : 1.0f;
		if (value <= previous)
		{
			for (k = 0; k < 3; k++)
				out[k] = (a[k] * (1.0f - t[1]) + b[k] * t[1]) * (1.0f - flash) + c[k] * flash;
			out[3] = t[3] * (1.0f - flash) + t[3] * scalar[2] * flash;
		}
		else
		{
			for (k = 0; k < 3; k++)
				out[k] = d[k] * (1.0f - flash_amount) + e[k] * flash_amount;
			out[3] = t[3] * ((1.0f - flash_amount) * scalar[3] + flash_amount);
		}
		break;
	}
	case CHUD_SHADER_METER_GRADIENT:
	{
		/* (chud_meter_gradient: the value across the sprite to scalar E, the
		meter's most; a gradient of scalar B's width up to the amount in the
		gradient colour, the empty colour past it) */
		float most = scalar[4] > 0.0f ? scalar[4] : 255.0f, value = most * u, edge = scalar[0], width = scalar[1];
		float low = edge - width, high = edge, blend = 0.25f * width, flash = 0.0f;

		if (most - edge < blend)
			low += blend - (most - edge), high += blend - (most - edge);
		if (value > low)
			flash = value <= high && high > low ? (value - low) / (high - low) : 1.0f;
		flash = flash * flash * flash * flash;
		if (value <= edge)
		{
			for (k = 0; k < 3; k++)
				out[k] = a[k] * (1.0f - t[1]) + b[k] * t[1] + c[k] * flash;
			out[3] = t[3] * (1.0f - flash) + t[3] * scalar[2] * flash;
		}
		else
		{
			for (k = 0; k < 3; k++)
				out[k] = d[k];
			out[3] = t[3] * scalar[3];
		}
		break;
	}
	case CHUD_SHADER_CROSSHAIR:
		/* (chud_crosshair: the neutral colour, the hostile at 1, the friendly at -1) */
		a = scalar[0] >= 1.0f ? b : scalar[0] <= -1.0f ? c : a;
		for (k = 0; k < 3; k++)
			out[k] = a[k];
		out[3] = t[3];
		break;
	default:
		/* (chud_simple: the primary and secondary background colours by green,
		the highlight colour by blue) */
		for (k = 0; k < 3; k++)
			out[k] = a[k] * t[1] + b[k] * (1.0f - t[1]) + c[k] * t[2];
		out[3] = t[3];
		break;
	}
	for (k = 0; k < 4; k++)
		out[k] = out[k] < 0.0f ? 0.0f : out[k] > 1.0f ? 1.0f : out[k];
	/* (chud_compute_result_pixel: the colour goes to the gamma 2 frame as its
	square root - render_target.fx; HALO_H3_HUD_GAMMA=0: as it is) */
	if (h3_hud_gamma())
	{
		for (k = 0; k < 3; k++)
			out[k] = sqrtf(out[k]);
	}
}

/* an item's texture made for these values at this size on the view (pixels):
each texel of it the mean of the sprite's texels under it, shaded */
static boolean item_bake(struct chud_item *item, struct chud_values const *values, long width, long height)
{
	float scalar[6], key[4];
	long tw = 4, th = 4, x, y;
	int k;
	boolean dynamic = item->render.shader == CHUD_SHADER_METER || item->render.shader == CHUD_SHADER_METER_SHIELD ||
		item->render.shader == CHUD_SHADER_METER_GRADIENT || item->render.shader == CHUD_SHADER_CROSSHAIR;

	if (!item->crop || width < 1 || height < 1)
		return FALSE;
	if (width > 1024)
		width = 1024;
	if (height > 1024)
		height = 1024;
	for (k = 0; k < 6; k++)
		scalar[k] = output_scalar(&item->render, k, values);
	/* (what the texture depends on: a meter's amounts, by the 255th) */
	key[0] = dynamic ? floorf(scalar[0] + 0.5f) : 0.0f;
	key[1] = dynamic ? floorf(scalar[1] + 0.5f) : 0.0f;
	key[2] = dynamic ? floorf(scalar[4] * 16.0f + 0.5f) : 0.0f;
	key[3] = 0.0f;
	if (item->made && item->made_width == width && item->made_height == height && !memcmp(key, item->made_key, sizeof(key)))
		return item->texture != NULL;
	scalar[0] = dynamic ? key[0] : scalar[0];
	scalar[1] = dynamic ? key[1] : scalar[1];
	while (tw < width + 1)
		tw *= 2;
	while (th < height + 1)
		th *= 2;
	if (!item->texture || item->texture_width != tw || item->texture_height != th)
	{
		texture_free(item->texture);
		free(item->texels);
		item->texture = NULL;
		item->texels = calloc((size_t)tw * (size_t)th, 4);
		item->texture_width = tw;
		item->texture_height = th;
		if (!item->texels)
			return FALSE;
	}
	else
		memset(item->texels, 0, (size_t)tw * (size_t)th * 4);
	for (y = 0; y < height; y++)
	{
		long sy0 = y * item->crop_height / height, sy1 = (y + 1) * item->crop_height / height;

		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		for (x = 0; x < width; x++)
		{
			long sx0 = x * item->crop_width / width, sx1 = (x + 1) * item->crop_width / width, sx, sy;
			float sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			unsigned char *o = item->texels + 4 * (y * tw + x);

			if (sx1 <= sx0)
				sx1 = sx0 + 1;
			for (sy = sy0; sy < sy1 && sy < item->crop_height; sy++)
			{
				for (sx = sx0; sx < sx1 && sx < item->crop_width; sx++)
				{
					unsigned char const *s = item->crop + 4 * (sy * item->crop_width + sx);
					float t[4], out[4];

					for (k = 0; k < 4; k++)
						t[k] = (float)s[k] / 255.0f;
					shade(&item->render, scalar, t, ((float)sx + 0.5f) / (float)item->crop_width, out);
					/* (the colours weighed by their alpha: a clear texel's colour is nothing) */
					sum[0] += out[0] * out[3];
					sum[1] += out[1] * out[3];
					sum[2] += out[2] * out[3];
					sum[3] += out[3];
				}
			}
			if (sum[3] > 0.0f)
			{
				float n = (float)((sy1 - sy0) * (sx1 - sx0));

				o[0] = (unsigned char)(255.0f * sum[2] / sum[3] + 0.5f);
				o[1] = (unsigned char)(255.0f * sum[1] / sum[3] + 0.5f);
				o[2] = (unsigned char)(255.0f * sum[0] / sum[3] + 0.5f);
				o[3] = (unsigned char)(255.0f * sum[3] / n + 0.5f);
			}
		}
	}
	if (!item->texture)
		item->texture = texture_new(tw, th, item->texels);
	else
		texture_update(item->texture, item->texels);
	item->made = TRUE;
	item->made_width = width;
	item->made_height = height;
	memcpy(item->made_key, key, sizeof(key));
	return item->texture != NULL;
}

/* ---------- private code: drawing */

static void quad_draw(struct bitmap_data *bitmap, float const (*corners)[2], float u0, float v0, float u1, float v1, pixel32 color)
{
	struct dynamic_screen_vertex vertices[4];
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	short k;

	/* (top left, top right, bottom right, bottom left) */
	for (k = 0; k < 4; k++)
	{
		vertices[k].position.x = corners[k][0];
		vertices[k].position.y = corners[k][1];
		vertices[k].texture_coordinates.x = k == 1 || k == 2 ? u1 : u0;
		vertices[k].texture_coordinates.y = k >= 2 ? v1 : v0;
		vertices[k].color = color;
	}
	csmemset(&parameters, 0, sizeof(parameters));
	parameters.map_texture_scale[0].i = parameters.map_texture_scale[0].j = 1.0f;
	parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
	parameters.framebuffer_blend_function = 0;
	parameters.map[0] = bitmap;
	rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
}

/* a rectangle of the canvas drawn with an item's texture, in strips across
so that it follows the curve */
static void item_draw_rectangle(struct chud_item const *item, float x0, float y0, float x1, float y1, boolean mirror_x, boolean mirror_y,
	float alpha)
{
	long strips = (long)(fabsf(x1 - x0) / 48.0f) + 1, s;
	float u_most = (float)item->made_width / (float)item->texture_width, v_most = (float)item->made_height / (float)item->texture_height;
	pixel32 color = ((pixel32)(255.0f * (alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha) + 0.5f) << 24) | 0xFFFFFFUL;

	if (strips > 16)
		strips = 16;
	for (s = 0; s < strips; s++)
	{
		float f0 = (float)s / (float)strips, f1 = (float)(s + 1) / (float)strips;
		float xa = x0 + (x1 - x0) * f0, xb = x0 + (x1 - x0) * f1;
		float corners[4][2];
		float ua = (mirror_x ? 1.0f - f0 : f0) * u_most, ub = (mirror_x ? 1.0f - f1 : f1) * u_most;

		h3_hud_warp(xa, y0, &corners[0][0], &corners[0][1]);
		h3_hud_warp(xb, y0, &corners[1][0], &corners[1][1]);
		h3_hud_warp(xb, y1, &corners[2][0], &corners[2][1]);
		h3_hud_warp(xa, y1, &corners[3][0], &corners[3][1]);
		quad_draw(item->texture, (float const (*)[2])corners, ua, mirror_y ? v_most : 0.0f, ub, mirror_y ? 0.0f : v_most, color);
	}
}

/* a line of the stroke font: its cap top at the canvas's (x, y), its cap
height (canvas units) */
static void text_draw(char const *text, float x, float y, float height, float const *rgb, float alpha)
{
	boolean gamma = h3_hud_gamma();
	pixel32 color = ((pixel32)(255.0f * alpha + 0.5f) << 24) | ((pixel32)(255.0f * (gamma ? sqrtf(rgb[0]) : rgb[0]) + 0.5f) << 16) |
		((pixel32)(255.0f * (gamma ? sqrtf(rgb[1]) : rgb[1]) + 0.5f) << 8) | (pixel32)(255.0f * (gamma ? sqrtf(rgb[2]) : rgb[2]) + 0.5f);
	float half = 0.5f * H2_FONT_STROKE * height;

	if (!h3_hud.white)
		return;
	for (; *text; text++)
	{
		long glyph = NONE, i, s;

		for (i = 0; i < (long)NUMBEROF(h2_font_glyphs); i++)
		{
			if (h2_font_glyphs[i].ch == (unsigned char)*text)
				glyph = i;
		}
		if (glyph == NONE)
			continue;
		for (s = h2_font_glyphs[glyph].first_stroke; s < h2_font_glyphs[glyph].first_stroke + h2_font_glyphs[glyph].stroke_count; s++)
		{
			long n = h2_font_strokes[s].count, p;

			for (p = 0; p < (h2_font_strokes[s].closed ? n : n - 1); p++)
			{
				float const *a = h2_font_points[h2_font_strokes[s].first + p], *b = h2_font_points[h2_font_strokes[s].first + (p + 1) % n];
				float ax = x + a[0] * height, ay = y + a[1] * height, bx = x + b[0] * height, by = y + b[1] * height;
				float dx = bx - ax, dy = by - ay, length = sqrtf(dx * dx + dy * dy), nx, ny;
				float corners[4][2];

				if (length < 1.0e-4f)
					continue;
				dx /= length, dy /= length;
				nx = -dy * half, ny = dx * half;
				/* (the ends a half width longer: the joins closed) */
				ax -= dx * half, ay -= dy * half, bx += dx * half, by += dy * half;
				h3_hud_warp(ax + nx, ay + ny, &corners[0][0], &corners[0][1]);
				h3_hud_warp(bx + nx, by + ny, &corners[1][0], &corners[1][1]);
				h3_hud_warp(bx - nx, by - ny, &corners[2][0], &corners[2][1]);
				h3_hud_warp(ax - nx, ay - ny, &corners[3][0], &corners[3][1]);
				/* (clockwise on the screen) */
				if ((corners[1][0] - corners[0][0]) * (corners[2][1] - corners[0][1]) - (corners[1][1] - corners[0][1]) * (corners[2][0] - corners[0][0]) < 0.0f)
				{
					float swap[2];

					memcpy(swap, corners[0], sizeof(swap)), memcpy(corners[0], corners[3], sizeof(swap)), memcpy(corners[3], swap, sizeof(swap));
					memcpy(swap, corners[1], sizeof(swap)), memcpy(corners[1], corners[2], sizeof(swap)), memcpy(corners[2], swap, sizeof(swap));
				}
				quad_draw(h3_hud.white, (float const (*)[2])corners, 0.5f, 0.5f, 0.5f, 0.5f, color);
			}
		}
		x += h2_font_glyphs[glyph].advance * height;
	}
}

static boolean state_holds(unsigned short const *wanted, unsigned short const *state)
{
	int k;

	for (k = 0; k < CHUD_STATE_WORDS; k++)
	{
		if (wanted[k] && !(wanted[k] & state[k]))
			return FALSE;
	}
	return TRUE;
}

/* a HUD's widgets drawn for the player's values */
static void definition_draw(short local_player_index, long definition, struct chud_values *values)
{
	struct chud_definition const *d = &h3_hud.definitions[definition];
	boolean log = h3_hud_setting("HALO_H3_HUD_LOG") && (game_time_get() % 60) == 0;
	long w, i;

	for (w = d->first_widget; w < d->first_widget + d->widget_count; w++)
	{
		struct chud_widget const *widget = &h3_hud.widgets[w];

		/* (the HUD's start-up wipe: lines only its animation shows right) */
		if (!strncmp(widget->name, "init_wipe", 9))
			continue;
		if (!state_holds(widget->state, values->state))
		{
			if (log)
				platform_log("h3 hud: %s/%s: not in this state", d->name, widget->name);
			continue;
		}
		values->grenade = widget->placement.present && widget->placement.anchor >= CHUD_ANCHOR_GRENADE_A &&
			widget->placement.anchor <= CHUD_ANCHOR_GRENADE_D ? (short)(widget->placement.anchor - CHUD_ANCHOR_GRENADE_A) : NONE;
		for (i = widget->first_item; i < widget->first_item + widget->item_count; i++)
		{
			struct chud_item *item = &h3_hud.items[i];
			float x, y, scale[2], alpha = 1.0f;

			if (!item_wanted(widget, item) || !state_holds(item->state, values->state) || !item_point(widget, item, &x, &y, scale))
				continue;
			/* (a grenade kind the player has none of: dimmed - Halo 3's
			"dim_lines" animation, here a plain third) */
			if (values->grenade != NONE && values->grenade_counts[values->grenade] <= 0)
				alpha = 0.35f;
			if (item->kind == CHUD_ITEM_TEXT)
			{
				char text[16];
				float value = input_value(item->render.input, values);

				/* (a number: the strings' "chud_out_a" is the input's value; the
				pickups' counts are not shown) */
				if (item->render.input == CHUD_INPUT_PICKUP || item->render.input == CHUD_INPUT_ZERO)
					continue;
				snprintf(text, sizeof(text), "%ld", (long)(value + 0.5f));
				/* (Halo 3's font 8 - the HUD's small numbers - has its cap top 6
				units under the text's point and caps 9 units high: the tag
				test's frames) */
				text_draw(text, x, y + 6.0f * scale[1], 9.0f * fabsf(scale[1]), output_color(&item->render, 0), alpha);
				if (log)
					platform_log("h3 hud: %s/%s/%s: \"%s\" at %.0f %.0f", d->name, widget->name, item->name, text, x, y);
			}
			else
			{
				float wide = (float)item->crop_width * fabsf(scale[0]), high = (float)item->crop_height * fabsf(scale[1]);
				float x0 = x - 0.5f * (item->placement.origin[0] + 1.0f) * wide, y0 = y - 0.5f * (item->placement.origin[1] + 1.0f) * high;

				if (!item->crop || !item_bake(item, values, (long)(wide * h3_hud.view_scale + 0.5f), (long)(high * h3_hud.view_scale + 0.5f)))
					continue;
				item_draw_rectangle(item, x0, y0, x0 + wide, y0 + high, scale[0] < 0.0f, scale[1] < 0.0f, alpha);
				/* (flag 0: drawn again mirrored about its point - the helmet's
				other corner) */
				if (item->flags & 1)
					item_draw_rectangle(item, 2.0f * x - x0 - wide, y0, 2.0f * x - x0, y0 + high, !(scale[0] < 0.0f), scale[1] < 0.0f, alpha);
				if (log)
					platform_log("h3 hud: %s/%s/%s: shader %d at %.1f %.1f, %.0f x %.0f (sprite %ldx%ld)", d->name, widget->name, item->name,
						(int)item->render.shader, x0, y0, wide, high, item->crop_width, item->crop_height);
				/* this engine's blips on Halo 3's motion sensor: its disc's middle */
				if (widget->special == 6 && i == widget->first_item)
				{
					point2d point;
					float sx, sy;

					h3_hud_warp(x0 + 0.5f * wide, y0 + 0.5f * high, &sx, &sy);
					point.x = (short)(sx + 0.5f);
					point.y = (short)(sy + 0.5f);
					motion_sensor_draw_screen(local_player_index, local_player_count() > 1, &point);
				}
			}
		}
	}
}

/* the HUD of a weapon of this engine's: its tag's name holds Halo 3's
("h2\\weapons\\h3_assault_rifle ...": h3_weapons.c); NONE none */
static long weapon_definition(long definition_index)
{
	char const *name = definition_index != NONE ? tag_get_name(definition_index) : NULL, *at;
	char token[48];
	long k, n = 0;

	if (!name || !(at = strstr(name, "h3_")))
		return NONE;
	for (at += 3; *at && *at != ' ' && *at != '\\' && n < (long)sizeof(token) - 1; at++)
		token[n++] = *at;
	token[n] = 0;
	for (k = 0; k < h3_hud.weapon_count; k++)
	{
		if (!strcmp(h3_hud.weapons[k].name, token))
			return h3_hud.weapons[k].definition;
	}
	return NONE;
}

/* (FALSE: the weapon has no Halo 3 HUD - a stand-in of the kit's) */
static boolean weapon_draw(short local_player_index, long weapon_index, struct chud_values *values, unsigned short place)
{
	struct weapon_datum *weapon = weapon_index != NONE ? weapon_try_and_get(weapon_index) : NULL;
	struct weapon_definition *definition;
	long hud;
	boolean magazine;

	if (!weapon || (hud = weapon_definition(weapon->definition_index)) == NONE)
		return FALSE;
	definition = weapon_definition_get(weapon->definition_index);
	magazine = definition->weapon.magazines.count > 0 && definition->weapon.heat_loss_per_second <= 0.0f;
	values->clip = (float)weapon->weapon.magazines[0].rounds_loaded;
	values->total = (float)weapon->weapon.magazines[0].rounds_total;
	values->heat = weapon->weapon.heat;
	values->battery = 1.0f - weapon->weapon.age;
	values->state[CHUD_STATE_WEAPON_PLACE] = place;
	/* (the weapon's kind of ammunition: rounds - bits 0 and 1 -, or a battery
	- 4, 5 and 6; what Halo 3 sets of these when is the game's own) */
	values->state[CHUD_STATE_AMMUNITION] = magazine ? 0x0003 : 0x0070;
	definition_draw(local_player_index, hud, values);
	return TRUE;
}

/* ---------- public code */

int h3_hud_draw(
	short local_player_index,
	long unit_index)
{
	struct unit_datum *unit;
	struct chud_values values;
	long weapon_index = NONE, left_weapon_index;
	float width, height;
	boolean weapon_drawn;
	boolean campaign = !game_engine_running();

	h3_hud_load();
	if (!h3_hud.loaded || unit_index == NONE || !(unit = unit_try_and_get(unit_index)))
		return FALSE;
	width = (float)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0);
	height = (float)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);
	if (width < 16.0f || height < 16.0f)
		return FALSE;
	h3_hud.view_scale = height / h3_hud.resolution[1];
	h3_hud.view_width = width / h3_hud.view_scale;
	h3_hud.view_height = h3_hud.resolution[1];
	/* (the crosshair under the middle where the view's reticle is lowered:
	in the canvas's units) */
	{
		extern short field_of_view_h3_reticle_pixels(short local_player_index, short half_height);

		h3_hud.reticle_drop = (float)field_of_view_h3_reticle_pixels(local_player_index, (short)(0.5f * height)) / h3_hud.view_scale;
	}

	memset(&values, 0, sizeof(values));
	values.grenade = NONE;
	values.shield = PIN(unit->object.shield_vitality, 0.0f, 1.0f);
	values.shield_previous = PIN(unit->object.shield_vitality + unit->object.recent_shield_damage, values.shield, 1.0f);
	values.grenade_counts[0] = unit->unit.grenade_counts[0];
	values.grenade_counts[1] = unit->unit.grenade_counts[1];
	values.autoaim = player_control_get_target_object_index(local_player_index) != NONE &&
		player_control_get_autoaim_level(local_player_index) > 0.0f ? 1.0f : 0.0f;
	left_weapon_index = dual_wield_left_weapon(unit_index);
	if (unit->unit.current_weapon_index != NONE)
		weapon_index = unit->unit.weapon_object_indices[unit->unit.current_weapon_index];

	values.state[CHUD_STATE_BIPED] = 0x0001;
	values.state[CHUD_STATE_SCREEN] = 0x0001;
	values.state[CHUD_STATE_SENSOR_RANGE] = 0x0002;
	/* (bit 0 not zoomed, 1 and 2 the zoom's levels. The scopes are not drawn
	yet - their masks and rulers -, so a zoomed weapon keeps its plain
	crosshair: always "not zoomed". HALO_H3_HUD_ZOOM_STATE=1: Halo 3's states,
	nothing drawn while zoomed) */
	values.state[CHUD_STATE_ZOOM] = (unsigned short)(unit->unit.current_zoom_level == NONE || !h3_hud_setting("HALO_H3_HUD_ZOOM_STATE") ? 1 :
		unit->unit.current_zoom_level == 0 ? 2 : 4);
	/* (bit 0 no weapon in hand - the unarmed crosshair -, bit 1 one) */
	values.state[CHUD_STATE_HANDS] = (unsigned short)(weapon_index != NONE ? 2 : 1);
	/* (the unit: the sensor, the grenade chosen, shields; bit 7 is set of
	everything the Spartan's HUD shows in play; bit 8 the helmet's edges, which
	the tag test shows in the campaign and not in a multiplayer game) */
	values.state[CHUD_STATE_UNIT] = (unsigned short)(0x0001 | 0x0080 | 0x8000 | (campaign ? 0x0100 : 0) |
		(unit->unit.current_grenade_index >= 0 && unit->unit.current_grenade_index < 4 ? 0x0004 << unit->unit.current_grenade_index : 0));
	values.state[CHUD_STATE_AUTOAIM] = 0x0001;
	values.state[CHUD_STATE_LOCK] = 0x0001;
	/* (the grenade kinds' places: all four in a multiplayer game, the frag's
	and the plasma's in the campaign's first missions - the tag test's frames -;
	and the bit the shield's parts ask) */
	values.state[CHUD_STATE_GRENADES] = (unsigned short)(campaign ? 0x0302 : 0x0F02);

	/* (tests: HALO_H3_HUD_BACKDROP=RRGGBB, the view filled with a colour
	under the HUD - two of them give the HUD's own colour and alpha a pixel,
	~/halo-test-out/h3pc3-out/hud/tools/hudlayer.py) */
	{
		char const *backdrop = h3_hud_setting("HALO_H3_HUD_BACKDROP");

		if (backdrop && h3_hud.white)
		{
			float corners[4][2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } };

			corners[1][0] = corners[2][0] = width;
			corners[2][1] = corners[3][1] = height;
			quad_draw(h3_hud.white, (float const (*)[2])corners, 0.5f, 0.5f, 0.5f, 0.5f, 0xFF000000UL | (strtoul(backdrop, NULL, 16) & 0xFFFFFFUL));
		}
	}
	definition_draw(local_player_index, h3_hud.unit_definition, &values);
	weapon_drawn = weapon_draw(local_player_index, weapon_index, &values, 0x0001);
	if (left_weapon_index != NONE)
		weapon_draw(local_player_index, left_weapon_index, &values, 0x0002);
	/* (the weapon on the back: its HUD's backpack widgets) */
	{
		short slot;

		for (slot = 0; slot < (short)NUMBEROF(unit->unit.weapon_object_indices); slot++)
		{
			long holstered = unit->unit.weapon_object_indices[slot];

			if (holstered != NONE && holstered != weapon_index && holstered != left_weapon_index)
			{
				weapon_draw(local_player_index, holstered, &values, 0x0004);
				break;
			}
		}
	}
	/* (2: the weapon in hand has no Halo 3 HUD - this engine's weapon HUD,
	its crosshair, is drawn for it) */
	return weapon_drawn || weapon_index == NONE ? 1 : 2;
}

#endif
