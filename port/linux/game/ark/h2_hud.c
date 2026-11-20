/*
H2_HUD.C

Halo 2's HUD drawn on a Halo 2 level (h2_hud.h): its widgets, as the
level's HUD file has them (h2_hud_make.c), drawn with this engine's HUD
drawing (hud_draw.c: the HUD's 640x480 placement, its corners and scale) in
place of this engine's unit and weapon HUD: the grenades (top left), the
weapon's panel, ammunition meter and count (top right), the shield meter
over the motion sensor (bottom left; this engine's blips on it), the
reticle and scope. A widget is drawn when Halo 2's "draw if" flags hold of
the player's state and its "don't draw if" flags do not; its shader's kind
decides how (a meter filled by its input, the scope's mask stretched over
the screen in four mirrored quarters, dim, red).
*/

#include "cseries.h"
#include "h2_hud.h"
#include "render_epoch.h"
#include "h2_emblem.h"
#include "h2_player_models.h"
#include "h2_level.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "interface/hud_definitions.h"
#include "interface/interface.h"
#include "interface/hud_draw.h"
#include "interface/motion_sensor.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "shaders/shader_definitions.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/dual_wield.h"
#include "bitmaps/bitmap_group.h"
#include "text/draw_string.h"
#include "memory/data.h"
#include "render/render_cameras_internal.h"
#include "h2_projectiles.h"
#include "h2_territories.h"
#include "h2_cache.h"
#include "h2_build_heap.h"
#include "cutscene/cinematics.h"
#include "interface/hud_messaging.h"

#include <math.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* (perf/cairo-30fps-2) the HUD's settings and log switches read once until
the settings change ("@set", the settings panel: halo_settings_generation),
not each frame and widget - getenv walks the whole environment (HALO_HUD_GETENV=0:
every time, as before) */
extern volatile unsigned long halo_settings_generation;

static char const *h2_hud_getenv(char const *name)
{
	static struct
	{
		char const *name;
		char const *value;
		unsigned long generation;
	} kept[48];
	static int count, on = -1;
	int k;

	if (on < 0)
		on = !(getenv("HALO_HUD_GETENV") && atoi(getenv("HALO_HUD_GETENV")) == 0);
	if (!on)
		return getenv(name);
	for (k = 0; k < count; k++)
	{
		if (kept[k].name == name)
		{
			if (kept[k].generation != halo_settings_generation)
			{
				kept[k].value = getenv(name);
				kept[k].generation = halo_settings_generation;
			}
			return kept[k].value;
		}
	}
	if (count < (int)(sizeof(kept) / sizeof(kept[0])))
	{
		kept[count].name = name;
		kept[count].value = getenv(name);
		kept[count].generation = halo_settings_generation;
		return kept[count++].value;
	}
	return getenv(name);
}
#define getenv(name) h2_hud_getenv(name)

/* (perf/cairo-30fps-2) floor of a screen coordinate: below 2^23 a float
at or above 0 is floored by truncation (and is an integer from there on), the
same value as the library's floor, without its call (NaN and the negative
ones: the library's) */
static __inline real h2_hud_floor(real value)
{
	return value >= 0.0f && value < 8388608.0f ? (real)(long)value : (real)floor(value);
}

/* (the textures a level's HUD makes at most: Relic's 25 HUDs come to 257
images, 06b's and 07b's more - every HUD's images and the white texel -; at
256 the last was refused and the whole HUD with it, the Halo 1 HUD drawn on a
Halo 2 map, insider.6) */
enum { H2_HUD_LOADED_MAXIMUM = 512 };

/* ---------- constants */

enum
{
	H2_HUD_MESSAGE_TOP = 162,
	H2_HUD_MAXIMUM_HUDS = 32,
	H2_HUD_MAXIMUM_WIDGETS = 640,
	/* (a text's digits: this engine's HUD digits) */
	H2_HUD_TEXT_DIGITS = 3,
};

/* ---------- structures */

struct h2_hud_widget
{
	struct h2_hud_file_widget file;
	long bitmap_tag;            /* the image's bitmap tag (NONE: none) */
	long bitmap_fg_tag;         /* (a panel's) its foreground image's */
	long bitmap_alt_tag, bitmap_alt_fg_tag;   /* (the Elite's image of it, and its foreground) */
};

struct h2_hud_loaded
{
	char owner[64];
	long owner_tag;             /* (a weapon's) its tag (NONE: the unit's) */
	short low_clip, low_total;
	long first, count;          /* its widgets */
};

/* (this engine's, hud_nav_points.c's: a number element of a HUD) */
struct h2_hud_number_element
{
	struct hud_placement_definition placement;
	struct hud_color_definition colors;
	byte digits;
	byte number_flags;
	byte fractional_digits;
	byte pad;
	long unused[3];
};

/* ---------- globals */

static struct
{
	char level[64];             /* the level loaded ("" none tried) */
	long scenario_index;
	boolean loaded;
	/* (the HUD globals' motion sensor: 0 none read) */
	float motion_range, motion_minimum_speed;
	long hud_count;
	struct h2_hud_loaded huds[H2_HUD_MAXIMUM_HUDS];
	long widget_count;
	struct h2_hud_widget widgets[H2_HUD_MAXIMUM_WIDGETS];
	/* the waypoint arrows (the nav points' looks) and their images' tags */
	short waypoint_count;
	struct h2_hud_file_waypoint waypoints[H2_HUD_MAXIMUM_WAYPOINTS];
	long waypoint_tags[H2_HUD_MAXIMUM_WAYPOINTS][H2_HUD_WAYPOINT_IMAGES];
	/* the meters' rounds (h2_hud_file_cell) */
	long cell_count;
	struct h2_hud_file_cell cells[H2_HUD_MAXIMUM_CELLS];
	/* the postgame strings (h2_hud_file_word) */
	long word_count;
	struct h2_hud_file_word words[H2_HUD_MAXIMUM_WORDS];
} h2_hud;

void platform_log(char const *format, ...);
unsigned long long vita_host_time_us(void);
void platform_heap_usage(unsigned long *in_use, unsigned long *capacity);
void platform_memory_log(const char *when, const char *map_name);
void platform_contiguous_side_begin(void);
void platform_contiguous_side_end(void);

/* ---------- private code */

/* ---------- the HUD's images made at load (HALO_H2_HUD_AT_LOAD, h2_hud_make_loaded) */

/* an image's stand-in tag: a salt no tag has (the menu's is 0x7FFF0000) and its place in the table */
#define H2_HUD_LOADED_TAG 0x7FFE0000L

static struct
{
	long count;
	char paths[H2_HUD_LOADED_MAXIMUM][256];
	struct bitmap_data *bitmaps[H2_HUD_LOADED_MAXIMUM];
	unsigned long bytes;   /* (their pixels', DXT5) */
} h2_hud_loaded;
/* (h2_hud_tag's memo: bumped as the table above changes) */
static unsigned long h2_hud_loaded_serial = 1;

/* (the textures made for a level let go with it: the next level's are made afresh) */
static void h2_hud_loaded_discard(void)
{
	long k;

	for (k = 0; k < h2_hud_loaded.count; k++)
	{
		if (h2_hud_loaded.bitmaps[k])
		{
			rasterizer_bitmap_delete(h2_hud_loaded.bitmaps[k]);
			free(h2_hud_loaded.bitmaps[k]);
			h2_hud_loaded.bitmaps[k] = NULL;
		}
	}
	h2_hud_loaded.count = 0;
	h2_hud_loaded.bytes = 0;
	h2_hud_loaded_serial++;
}

/* (cache_files.c, as a map's tags go) the level's HUD textures let go with
it, not held until the next Halo 2 level's HUD is made: their user RAM (the
side blocks) the next level's make's (an insider.5 tester's Vita: Turf's
held into Zanzibar's make, which ran the system out of user RAM) */
void h2_hud_map_unloaded(void)
{
	if (h2_hud_loaded.count)
		platform_log("h2 hud: %ld textures (%lu KB) let go with the map", h2_hud_loaded.count, h2_hud_loaded.bytes / 1024);
	h2_hud_loaded_discard();
}

/* the widgets made at load read (the HUD file's bytes, h2_hud.h's structures one after another) */
struct h2_hud_reader
{
	unsigned char const *data;
	unsigned long size, at;
};

static boolean h2_hud_read(struct h2_hud_reader *reader, void *out, unsigned long count)
{
	if (reader->at + count > reader->size)
		return FALSE;
	memcpy(out, reader->data + reader->at, count);
	reader->at += count;
	return TRUE;
}

/* (perf/vita-draw-cost) h2_hud_tag's answers kept: the made-at-load
images' table (256-byte paths, each compare a cache line of its own) and the
tags are the same until an image is made or let go, or the map changes; the
HUD asks for its white texel's tag every string and widget. The made-at-load
table's changes bump h2_hud_loaded_serial. HALO_HUD_TAG_MEMO=0 off (@set). */
static long h2_hud_tag_lookup(char const *name)
{
	long k;

	for (k = 0; k < h2_hud_loaded.count; k++)
	{
		if (!strcmp(h2_hud_loaded.paths[k], name))
			return H2_HUD_LOADED_TAG + k;
	}
	return tag_loaded('bitm', name);
}

/* a HUD image's tag: the made-at-load one named so, else the level's */
static long h2_hud_tag(char const *name)
{
	extern unsigned long halo_map_generation;
	enum { H2_HUD_TAG_MEMO = 8, H2_HUD_TAG_MEMO_NAME = 96 };
	static struct { char name[H2_HUD_TAG_MEMO_NAME]; unsigned long serial, generation; long tag; } memo[H2_HUD_TAG_MEMO];
	static unsigned long memo_next;
	char const *setting = HALO_ENV_ONCE("HALO_HUD_TAG_MEMO");
	unsigned long k;
	long tag;

	if ((setting && atoi(setting) == 0) || strlen(name) >= H2_HUD_TAG_MEMO_NAME || halo_epoch_on_mutator())
		return h2_hud_tag_lookup(name);
	for (k = 0; k < H2_HUD_TAG_MEMO; k++)
	{
		if (memo[k].serial == h2_hud_loaded_serial && memo[k].generation == halo_map_generation && !strcmp(memo[k].name, name))
		{
			if (atoi(setting ? setting : "1") == 2)
			{
				/* (the check: the lookup made too) */
				static unsigned long checked, differed;

				if (h2_hud_tag_lookup(name) != memo[k].tag)
					differed++;
				if (!(++checked & 0x3fff))
					platform_log("h2 hud: tag memo checked %lu, %lu different", checked, differed);
			}
			return memo[k].tag;
		}
	}
	tag = h2_hud_tag_lookup(name);
	k = memo_next++ % H2_HUD_TAG_MEMO;
	strcpy(memo[k].name, name);
	memo[k].serial = h2_hud_loaded_serial;
	memo[k].generation = halo_map_generation;
	memo[k].tag = tag;
	return tag;
}

/* a HUD image's bitmap: a made-at-load one's, else the tag's first */
static struct bitmap_data *h2_hud_bitmap(long tag)
{
	if (tag >= H2_HUD_LOADED_TAG && tag < H2_HUD_LOADED_TAG + h2_hud_loaded.count)
		return h2_hud_loaded.bitmaps[tag - H2_HUD_LOADED_TAG];
	return tag == NONE ? NULL : bitmap_group_get_bitmap_from_sequence(tag, 0, 0);
}

/* (h2_hud_make_loaded) one image made a texture, as it is made: DXT5, its
memory the side blocks' (xbox_memory.c: user RAM on the Vita, outside the
memory window and the C heap; the window on the PC tiers) */
static boolean h2_hud_loaded_texture(void *context, char const *path, unsigned short width, unsigned short height,
	unsigned char const *pixels, unsigned long size)
{
	struct bitmap_data *bitmap;
	boolean made;

	(void)context;
	/* (malloc'd: cseries.h's free takes what its malloc gives, not calloc's) */
	if (h2_hud_loaded.count >= H2_HUD_LOADED_MAXIMUM || !(bitmap = malloc(sizeof(*bitmap))))
		return FALSE;
	memset(bitmap, 0, sizeof(*bitmap));
	bitmap->signature = BITMAP_GROUP_TAG;
	bitmap->width = (short)width;
	bitmap->height = (short)height;
	bitmap->depth = 1;
	bitmap->type = 0;
	bitmap->format = 16;
	bitmap->flags = FLAG(0) | FLAG(1);   /* (power of two, compressed) */
	bitmap->tag_index = NONE;
	bitmap->cache_block_index = NONE;
	bitmap->base_address = (void *)pixels;
	platform_contiguous_side_begin();
	made = rasterizer_bitmap_new(bitmap) && bitmap->hardware_format;
	if (made)
		rasterizer_bitmap_changed(bitmap);
	platform_contiguous_side_end();
	bitmap->base_address = NULL;
	if (!made)
	{
		free(bitmap);
		return FALSE;
	}
	snprintf(h2_hud_loaded.paths[h2_hud_loaded.count], 256, "%s", path);
	h2_hud_loaded.bitmaps[h2_hud_loaded.count++] = bitmap;
	h2_hud_loaded_serial++;
	h2_hud_loaded.bytes += size;
	return TRUE;
}

/* (h2_menu.c's postgame report) the white texel's bitmap of the level's
HUD: made at load, or the level's own; NULL none */
struct bitmap_data *h2_hud_white_bitmap(void)
{
	return h2_hud_bitmap(h2_hud_tag("h2\\hud\\white"));
}

/* the level's HUD made from its MCC map, in memory: its widgets (the HUD
file's bytes, the caller's to read and h2_hud_loaded_free) and its images
made textures as they are made (FALSE: the level's own file and bitmaps are
used). The make's own memory is the builder's heap (user RAM, not the C
heap: h2_build_heap.h) - the caller ends it (*heap TRUE) once the widgets
are read and let go */
static boolean h2_hud_make_at_load(char const *level, unsigned char **widgets, unsigned long *widgets_size, boolean *heap)
{
	long count;
	unsigned long long started = vita_host_time_us();
	unsigned long heap_before, heap_after, capacity;

	*widgets = NULL;
	*widgets_size = 0;
	*heap = FALSE;
	h2_hud_loaded_discard();
	platform_heap_usage(&heap_before, &capacity);
	if (!h2_build_heap_active())
	{
		h2_build_heap_begin();
		*heap = TRUE;
	}
	if (!h2_hud_make_loaded(level, widgets, widgets_size, h2_hud_loaded_texture, NULL, &count))
	{
		platform_log("h2 hud: %s: not made at load: the level's own HUD file and bitmaps are used", level);
		h2_hud_loaded_discard();
		h2_hud_loaded_free(*widgets);
		*widgets = NULL;
		*widgets_size = 0;
		if (*heap)
			h2_build_heap_end();
		*heap = FALSE;
		return FALSE;
	}
	platform_heap_usage(&heap_after, &capacity);
	platform_log("h2 hud: %ld images made textures (%lu KB) and %lu bytes of widgets made at load from the MCC map in %.0f ms (no file; "
		"C heap %+ld KB)", count, h2_hud_loaded.bytes / 1024, *widgets_size, (vita_host_time_us() - started) / 1000.0,
		((long)heap_after - (long)heap_before) / 1024);
	return TRUE;
}

/* the level's HUD file read (once a level) */
static void h2_hud_load(void)
{
	char const *scenario = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	struct h2_hud_reader reader;
	unsigned char *widgets = NULL;
	unsigned long widgets_size = 0;
	boolean heap = FALSE;
	struct h2_hud_file_header header;
	long h;

	if (!scenario || strncmp(scenario, "h2\\", 3))
	{
		h2_hud.loaded = FALSE;
		h2_hud.level[0] = 0;
		return;
	}
	if (h2_hud.scenario_index == global_scenario_index && !strcmp(h2_hud.level, scenario + 3))
		return;
	h2_hud.scenario_index = global_scenario_index;
	snprintf(h2_hud.level, sizeof(h2_hud.level), "%s", scenario + 3);
	h2_hud.loaded = FALSE;
	h2_hud.hud_count = 0;
	h2_hud.widget_count = 0;
	h2_hud.motion_range = h2_hud.motion_minimum_speed = 0.0f;
	h2_hud.waypoint_count = 0;
	h2_hud.cell_count = 0;
	h2_hud.word_count = 0;
	memset(&reader, 0, sizeof(reader));
	/* (made from the MCC map now, in memory: no HUD file, the level builder's, any more) */
	if (h2_hud_make_at_load(h2_hud.level, &widgets, &widgets_size, &heap))
	{
		reader.data = widgets;
		reader.size = widgets_size;
		/* (a test: HALO_H2_HUD_WIDGETS_DUMP=path writes the widgets made, to compare with the level's .h2hud) */
		if (getenv("HALO_H2_HUD_WIDGETS_DUMP"))
		{
			FILE *dump = fopen(getenv("HALO_H2_HUD_WIDGETS_DUMP"), "wb");

			if (dump)
			{
				fwrite(widgets, 1, (size_t)widgets_size, dump);
				fclose(dump);
			}
		}
	}
	else
	{
		platform_log("h2 hud: %s: its HUD could not be made from the map: no HUD", h2_hud.level);
		h2_hud_loaded_discard();
		return;
	}
	if (!h2_hud_read(&reader, &header, sizeof(header)) || header.signature != H2_HUD_FILE_SIGNATURE ||
		header.version != H2_HUD_FILE_VERSION || header.hud_count < 1 || header.hud_count > H2_HUD_MAXIMUM_HUDS)
	{
		h2_hud_loaded_free(widgets);
		if (heap)
			h2_build_heap_end();
		h2_hud_loaded_discard();
		return;
	}
	h2_hud.motion_range = header.motion_sensor_range;
	h2_hud.motion_minimum_speed = header.motion_sensor_minimum_speed;
	for (h = 0; h < header.hud_count; h++)
	{
		struct h2_hud_file_hud hud;
		struct h2_hud_loaded *loaded = &h2_hud.huds[h2_hud.hud_count];
		long w;

		if (!h2_hud_read(&reader, &hud, sizeof(hud)) || hud.widget_count < 0 ||
			h2_hud.widget_count + hud.widget_count > H2_HUD_MAXIMUM_WIDGETS)
		{
			break;
		}
		hud.owner[sizeof(hud.owner) - 1] = 0;
		snprintf(loaded->owner, sizeof(loaded->owner), "%s", hud.owner);
		loaded->owner_tag = hud.owner[0] && strncmp(hud.owner, "unit:", 5) ? tag_loaded('weap', hud.owner) : NONE;
		loaded->low_clip = hud.low_clip;
		loaded->low_total = hud.low_total;
		loaded->first = h2_hud.widget_count;
		loaded->count = 0;
		for (w = 0; w < hud.widget_count; w++)
		{
			struct h2_hud_widget *widget = &h2_hud.widgets[h2_hud.widget_count];

			if (!h2_hud_read(&reader, &widget->file, sizeof(widget->file)))
				break;
			widget->file.bitmap[sizeof(widget->file.bitmap) - 1] = 0;
			widget->file.bitmap_fg[sizeof(widget->file.bitmap_fg) - 1] = 0;
			widget->bitmap_tag = widget->file.bitmap[0] ? h2_hud_tag(widget->file.bitmap) : NONE;
			widget->bitmap_fg_tag = widget->file.bitmap_fg[0] ? h2_hud_tag(widget->file.bitmap_fg) : NONE;
			widget->file.bitmap_alt[sizeof(widget->file.bitmap_alt) - 1] = 0;
			widget->file.bitmap_alt_fg[sizeof(widget->file.bitmap_alt_fg) - 1] = 0;
			widget->bitmap_alt_tag = widget->file.bitmap_alt[0] ? h2_hud_tag(widget->file.bitmap_alt) : NONE;
			widget->bitmap_alt_fg_tag = widget->file.bitmap_alt_fg[0] ? h2_hud_tag(widget->file.bitmap_alt_fg) : NONE;
			if (getenv("HALO_H2_HUD_LOG") && widget->file.bitmap[0] && widget->bitmap_tag == NONE)
				platform_log("h2 hud: %s %s: image %s is not in the level", hud.owner, widget->file.name, widget->file.bitmap);
			h2_hud.widget_count++;
			loaded->count++;
		}
		h2_hud.hud_count++;
	}
	/* the waypoint arrows */
	if (h2_hud.hud_count == header.hud_count)
	{
		for (h = 0; h < header.waypoint_count && h < H2_HUD_MAXIMUM_WAYPOINTS; h++)
		{
			struct h2_hud_file_waypoint *waypoint = &h2_hud.waypoints[h];
			short k;

			if (!h2_hud_read(&reader, waypoint, sizeof(*waypoint)))
				break;
			waypoint->name[sizeof(waypoint->name) - 1] = 0;
			for (k = 0; k < H2_HUD_WAYPOINT_IMAGES; k++)
			{
				waypoint->bitmap[k][sizeof(waypoint->bitmap[k]) - 1] = 0;
				h2_hud.waypoint_tags[h][k] = waypoint->bitmap[k][0] ? h2_hud_tag(waypoint->bitmap[k]) : NONE;
			}
			h2_hud.waypoint_count++;
		}
		/* the meters' rounds (after every waypoint arrow of the file) */
		if (h2_hud.waypoint_count == header.waypoint_count && header.cell_count > 0 && header.cell_count <= H2_HUD_MAXIMUM_CELLS &&
			h2_hud_read(&reader, h2_hud.cells, sizeof(h2_hud.cells[0]) * (unsigned long)header.cell_count))
		{
			h2_hud.cell_count = header.cell_count;
		}
		/* the postgame strings (after the cells) */
		if (h2_hud.cell_count == header.cell_count && header.word_count > 0 && header.word_count <= H2_HUD_MAXIMUM_WORDS &&
			h2_hud_read(&reader, h2_hud.words, sizeof(h2_hud.words[0]) * (unsigned long)header.word_count))
		{
			h2_hud.word_count = header.word_count;
			for (h = 0; h < h2_hud.word_count; h++)
			{
				h2_hud.words[h].id[sizeof(h2_hud.words[h].id) - 1] = 0;
				h2_hud.words[h].text[sizeof(h2_hud.words[h].text) - 1] = 0;
			}
		}
	}
	/* (a widget's rounds within the file's) */
	for (h = 0; h < h2_hud.widget_count; h++)
	{
		struct h2_hud_file_widget *f = &h2_hud.widgets[h].file;

		if (f->cell_count && (f->cell_first < 0 || f->cell_first + f->cell_count > h2_hud.cell_count))
			f->cell_count = 0;
	}
	h2_hud_loaded_free(widgets);
	if (heap)
		h2_build_heap_end();
	h2_hud.loaded = h2_hud.hud_count > 0;
	platform_log("h2 hud: %s: %ld HUDs, %ld widgets, %d waypoint arrows (%s)", h2_hud.level, h2_hud.hud_count, h2_hud.widget_count,
		(int)h2_hud.waypoint_count, "made at load, no file");
	/* (the C heap and the window with the level's HUD up: file and made at load compared) */
	platform_memory_log("h2 hud loaded", h2_hud.level);
	/* (a campaign level's: the difficulty played and its scales, Halo 2's
	globals' - h2_biped.c) */
	if (!game_engine_running())
		platform_log("h2 difficulty: %s on difficulty %d: enemy damage x%.2f, enemy recharge x%.2f, rate of fire x%.2f", h2_hud.level,
			(int)game_difficulty_level_get(), game_difficulty_get_value(_game_difficulty_value_enemy_damage),
			game_difficulty_get_value(_game_difficulty_value_enemy_recharge), game_difficulty_get_value(_game_difficulty_value_rate_of_fire));
}

/* (motion_sensor.c) a Halo 2 level's motion sensor: its range and the slowest
speed a unit moves at to show on it, Halo 2's HUD globals' (FALSE: not a Halo 2
level, or none read - this engine's HUD globals' are the sensor's) */
int h2_hud_motion_sensor(real *range, real *minimum_speed)
{
	h2_hud_load();
	if (!h2_hud.loaded || h2_hud.motion_range <= 0.0f)
		return FALSE;
	if (range)
		*range = h2_hud.motion_range;
	if (minimum_speed)
		*minimum_speed = h2_hud.motion_minimum_speed;
	return TRUE;
}

/* (game_engine.c) the seconds left of the game's time limit, NONE where it has none */
long game_engine_time_left_seconds(void);

/* the player's state, as Halo 2's flags have it */
struct h2_hud_state
{
	unsigned short unit;        /* draw flags' unit bits */
	real camo;                  /* (the camouflage meter's: 1 full) */
	unsigned char extra;
	/* (its bits 0x100 a lockable target in the reticle, 0x200 locking,
	0x400 locked: the rocket launcher's lock, h2_projectiles.c) */
	unsigned short weapon;
	unsigned short engine;
	real shield, body;
	short frag_count, plasma_count;
	short clip, total, clip_maximum;
	real heat, battery;
	/* (the scoreboard's) the player's (team's) score, the best other's, the
	game's score to win */
	long score, other_score, score_limit;
	boolean other_score_valid;
	/* (the score panel's rows, Halo 2's: the leader's - this player while
	he leads or ties - on top, the other below; each row's bar and emblem
	in its owner's colour - a free for all's player colour, a team game's
	team colour -, the number in the HUD's) */
	boolean leading;
	long player_index, rival_player_index;
	real self_rgb[3], rival_rgb[3];
	/* (King's, Oddball's and Territories' scores are times: Halo 2 writes
	them m:ss, ":05" under a minute - game_engine_format_time, modes 3, 4, 8) */
	boolean time_scores;
	/* (Halo 2's s_player_score_display, unknown_157450.cpp function_15f3a0:)
	the charge - the bomb's arming, or the player's taking of the territory
	he stands in - the "player is arming bomb" flag's meter shows (its
	fraction), and a Territories game's territories' bars: each one's colour
	(its holder's or contender's side's) and fill */
	boolean charge;
	real charge_fraction;
	long zone_count;
	real zone_rgb[8][3];
	real zone_fraction[8];
	/* (the time left, seconds, where the game has a time limit: the "timed
	game" flag's text; NONE untimed) */
	long time_left;
	/* (the flashes: the shield's - 1 damaged, 2 recharging -, the weapon's
	panel when its clip is low) */
	short shield_flash;
	boolean weapon_hud;
	/* (the lock: its fraction done, its target's place on the screen -
	the viewport's pixels -, if it is on it) */
	real lock_fraction;
	boolean lock_on_screen;
	real lock_point[2];
	/* (an enemy in the weapon's autoaim range and cone: its reticle red) */
	boolean target;
};

/* the HUD's colours, the local player's species' (Halo 2's: the Spartan's
blue, the Elite's purple; sampled from the owner's MCC frames): [0] its
panels' fill (the fill image's grey is 0.62 to 1 of it), [1] its marks' -
outlines, icons, the reticle -, [2] the meters' lit marks, [3] the shield
bar, [4] the motion sensor's disc */
static real h2_hud_tint[5][3];
static boolean h2_hud_elite;

static void h2_hud_tint_for(long unit_index)
{
	/* (the motion sensor's disc: MCC's 3,54,101 at 0.23 - h2_hud_make.c) */
	static real const spartan[5][3] = { { 0.020f, 0.486f, 0.941f }, { 0.424f, 0.698f, 0.973f }, { 0.250f, 0.590f, 0.920f }, { 0.094f, 0.550f, 1.000f }, { 0.012f, 0.212f, 0.396f } };
	static real const elite[5][3] = { { 0.643f, 0.341f, 0.824f }, { 0.737f, 0.502f, 0.816f }, { 0.700f, 0.430f, 0.860f }, { 0.690f, 0.375f, 0.840f }, { 0.250f, 0.090f, 0.290f } };
	struct unit_datum *unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;
	char const *name = unit ? tag_get_name(unit->definition_index) : "";
	boolean is_elite = (name && (strstr(name, "elite") || strstr(name, "dervish"))) || getenv("HALO_H2_HUD_ELITE") ||
		/* (no unit yet: the species the local player plays, h2_player_models.h) */
		(!unit && h2_player_models_local_species(0) == H2_PLAYER_MODEL_ELITE);

	memcpy(h2_hud_tint, is_elite ? elite : spartan, sizeof(h2_hud_tint));
	h2_hud_elite = is_elite;
}

/* a colour (its alpha kept) times one of the HUD's tints */
static pixel32 h2_hud_tinted(pixel32 color, int tint)
{
	unsigned long r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;

	r = (unsigned long)(r * h2_hud_tint[tint][0] + 0.5f);
	g = (unsigned long)(g * h2_hud_tint[tint][1] + 0.5f);
	b = (unsigned long)(b * h2_hud_tint[tint][2] + 0.5f);
	return (color & 0xFF000000UL) | (r << 16) | (g << 8) | b;
}

static boolean h2_hud_shown(struct h2_hud_file_widget const *widget, struct h2_hud_state const *state)
{
	/* (Halo 2's Blind skull, h2_skulls.c: no HUD) */
	{
		extern boolean h2_skull_on(short skull);

		if (h2_skull_on(6))
			return FALSE;
	}

	unsigned char const *f = widget->draw_flags;
	unsigned short do_unit = (unsigned short)(f[0] | (f[1] << 8));
	unsigned short do_extra = (unsigned short)(f[2] | (f[3] << 8));
	unsigned short do_weapon = (unsigned short)(f[4] | (f[5] << 8));
	unsigned short do_engine = (unsigned short)(f[6] | (f[7] << 8));
	unsigned short dont_unit = (unsigned short)(f[8] | (f[9] << 8));
	unsigned short dont_extra = (unsigned short)(f[10] | (f[11] << 8));
	unsigned short dont_weapon = (unsigned short)(f[12] | (f[13] << 8));
	unsigned short dont_engine = (unsigned short)(f[14] | (f[15] << 8));

	if ((do_unit && !(do_unit & state->unit)) || (do_extra && !(do_extra & state->extra)) ||
		(do_weapon && !(do_weapon & state->weapon)) || (do_engine && !(do_engine & state->engine)))
	{
		return FALSE;
	}
	return !(dont_unit & state->unit) && !(dont_extra & state->extra) && !(dont_weapon & state->weapon) &&
		!(dont_engine & state->engine);
}

/* an input's value (Halo 2's input numbers as its HUD files use them) */
static real h2_hud_input(unsigned char input, struct h2_hud_state const *state)
{
	switch (input)
	{
		case 0x01: return 1.0f;
		case 0x10: case 0x16: return state->shield;
		case 0x11: return state->body;
		/* (the Arbiter's camouflage meter, "unit camo value": its energy -
		full while he is seen; this engine's camouflage drawing it down) */
		case 0x18: return state->camo;
		case 0x14: return (real)state->frag_count;
		case 0x15: return (real)state->plasma_count;
		/* (Halo 2's weapon inputs: 0x30 clip ammo, 0x31 heat - the plasma
		rifle's heat meter's -, 0x32 battery, 0x33 total ammo, 0x36 clip
		ammo fraction, 0x37 time on overheat, 0x38 battery fraction, 0x39
		locking fraction) */
		case 0x30: return (real)state->clip;
		case 0x31: return state->heat;
		/* (a battery weapon's charge, a percent) */
		case 0x32: return 100.0f * state->battery;
		case 0x33: return (real)state->total;
		case 0x34: return (real)state->total;
		case 0x28: return state->heat;
		case 0x36: return state->clip_maximum > 0 ? (real)state->clip / (real)state->clip_maximum : 0.0f;
		case 0x38: return state->battery;
		/* (the lock's progress) */
		case 0x39: return state->lock_fraction;
		case 0x40: return state->score_limit > 0 ? PIN((real)state->score / (real)state->score_limit, 0.0f, 1.0f) : 0.0f;
		case 0x41: return state->score_limit > 0 ? PIN((real)state->other_score / (real)state->score_limit, 0.0f, 1.0f) : 0.0f;
		/* (the scoreboard's bomb_defusal meter's input, 67: the charge's
		fraction - the bomb's arming, a territory's taking) */
		case 0x43: return state->charge ? state->charge_fraction : 0.0f;
		default: return 0.0f;
	}
}

/* this engine's corner of one of Halo 2's anchors, and the placement of
an offset from it */
static short h2_hud_corner(unsigned char anchor)
{
	switch (anchor)
	{
		case H2_HUD_ANCHOR_TOP_RIGHT: return _hud_anchor_top_right;
		case H2_HUD_ANCHOR_TOP_LEFT: return _hud_anchor_top_left;
		case H2_HUD_ANCHOR_BOTTOM_LEFT: return _hud_anchor_bottom_left;
		case H2_HUD_ANCHOR_BOTTOM_RIGHT: return _hud_anchor_bottom_right;
		default: return _hud_anchor_center;
	}
}

/* a widget's anchor point on the screen (the viewport's pixels from its
corner) and the HUD's scale: Halo 2's 640x480 layout over the screen's
height, its corners a small margin in from the screen's (Halo 2's own,
narrower than this engine's title-safe one) */
double config_real(const char *name);
extern volatile unsigned long halo_settings_generation;

/* the player's HUD size (display.hud_scale, HALO_HUD_SCALE: 1 Halo 2's own,
0.75 to 1.5), read again as the settings change */
static real h2_hud_user_scale(void)
{
	static real value = -1.0f;
	static unsigned long settings_seen;

	if (value < 0.0f || settings_seen != halo_settings_generation)
	{
		char const *setting = getenv("HALO_HUD_SCALE");

		settings_seen = halo_settings_generation;
		value = setting && *setting ? (real)atof(setting) : (real)config_real("display.hud_scale");
		if (!(value >= 0.5f && value <= 2.0f))
			value = 1.0f;
	}
	return value;
}

/* the HUD's scale (the view's units a unit of Halo 2's 640x480 layout, the
player's size times) on a view so tall: MCC's classic HUD is the display's
height over 960 display pixels a unit on a display 960 lines or more (the
owner's 1080-line frames: 1.125, the weapon panel 231 pixels of its 206
units) and one pixel a unit on a shorter one - not made smaller (MCC in a
1280x720 window, 696 lines: the panel 204 pixels, the motion sensor and the
reticle as big against 1080 lines' as 1/1.125). A view shared by players'
(a split screen) keeps its height's, and so does the Vita's 544-line screen
(MCC has no such small screen: there it would be 1.76 times its size now) */
float halo_ui_pixel_scale(void);

static real h2_hud_view_scale(real height)
{
	real s = height / 960.0f, pixels = (real)halo_ui_pixel_scale();

#if !defined(HALO_VITA) && !defined(HALO_GXM_NULL)
	if (pixels > 0.0f && s * pixels < 1.0f && local_player_count() <= 1)
		s = 1.0f / pixels;
#else
	(void)pixels;
#endif
	return s * h2_hud_user_scale();
}

static void h2_hud_point(short local_player_index, struct h2_hud_file_widget const *widget, point2d *point, real *scale)
{
	/* (the whole view, not this engine's title-safe window: Halo 2's HUD
	sits about 5% of the view's height in from its edges, at any shape -
	the owner's MCC frames, 16:9 and 4:3 alike) */
	real x0 = 0.0f, y0 = 0.0f;
	real width = (real)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0);
	real height = (real)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);
	/* (MCC's classic HUD: h2_hud_view_scale; its corners 5% of the view's
	height in at any size - MCC's 696 and 1080-line frames) */
	real s = h2_hud_view_scale(height), margin = 0.05f * height, x, y;

	(void)local_player_index;
	switch (widget->anchor)
	{
		case H2_HUD_ANCHOR_TOP_RIGHT: x = x0 + width - margin; y = y0 + margin; break;
		case H2_HUD_ANCHOR_TOP_LEFT: x = x0 + margin; y = y0 + margin; break;
		case H2_HUD_ANCHOR_BOTTOM_LEFT: x = x0 + margin; y = y0 + height - margin; break;
		case H2_HUD_ANCHOR_BOTTOM_RIGHT: x = x0 + width - margin; y = y0 + height - margin; break;
		default: x = x0 + 0.5f * width; y = y0 + 0.5f * height; break;
	}
	point->x = (short)(x + widget->offset[0] * s);
	point->y = (short)(y + widget->offset[1] * s);
	*scale = s;
}

/* a bitmap's rectangle (texture coordinates u0..u1, v0..v1: reversed ones
mirror it) drawn over the screen's x0..x1, y0..y1 (hud_draw.c's
hud_draw_bitmap_internal, with any texture rectangle) */
static void h2_hud_quad_shaded(struct bitmap_data *bitmap, real x0, real y0, real x1, real y1, real u0, real v0, real u1, real v1,
	pixel32 left, pixel32 right, short blend)
{
	struct dynamic_screen_vertex vertices[4];
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	short k;

	for (k = 0; k < 4; k++)
	{
		boolean right_side = ((k + 1) & 2) != 0, bottom = k > 1;

		vertices[k].position.x = right_side ? x1 : x0;
		vertices[k].position.y = bottom ? y1 : y0;
		vertices[k].texture_coordinates.x = right_side ? u1 : u0;
		vertices[k].texture_coordinates.y = bottom ? v1 : v0;
		vertices[k].color = right_side ? right : left;
	}
	csmemset(&parameters, 0, sizeof(parameters));
	parameters.map_texture_scale[0].i = parameters.map_texture_scale[0].j = 1.0f;
	parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
	parameters.framebuffer_blend_function = blend; /* (7 alpha multiply add: hud_draw.c's; 0 alpha blend) */
	parameters.map[0] = bitmap;
	rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
}

static void h2_hud_quad_blend(struct bitmap_data *bitmap, real x0, real y0, real x1, real y1, real u0, real v0, real u1, real v1,
	pixel32 color, short blend)
{
	h2_hud_quad_shaded(bitmap, x0, y0, x1, y1, u0, v0, u1, v1, color, color, blend);
}

static void h2_hud_quad(struct bitmap_data *bitmap, real x0, real y0, real x1, real y1, real u0, real v0, real u1, real v1,
	pixel32 color)
{
	h2_hud_quad_blend(bitmap, x0, y0, x1, y1, u0, v0, u1, v1, color, 7);
}

/* an image (the part of it clip says) drawn alpha blended - Halo 2's HUD
blends, it does not add - over the rectangle from the point, the image's
own size (the widget's, the scale) a rectangle of the canvas; its edges on
whole pixels */
static void h2_hud_image(struct bitmap_data *bitmap, struct h2_hud_file_widget const *f, point2d const *point, real scale,
	real_rectangle2d const *clip, pixel32 color)
{
	real x0 = (real)point->x, y0 = (real)point->y;
	real wide = (real)fabs(clip->x1 - clip->x0) * (real)f->canvas_width * scale;
	real high = (real)fabs(clip->y1 - clip->y0) * (real)f->canvas_height * scale;

	h2_hud_quad_blend(bitmap, x0, y0, h2_hud_floor(x0 + wide + 0.5f), h2_hud_floor(y0 + high + 0.5f), clip->x0, clip->y0, clip->x1,
		clip->y1, color, 0);
}

/* this engine's HUD bitmap draws a bitmap's texels at the scale: an image
the PC tier's sidecar made full size (h2_texture_sidecar.c: its canvas the
file's times its halvings) has more texels than the widget's canvas - drawn
at the canvas' size, not twice or four times it */
static real h2_hud_texel_scale(struct h2_hud_file_widget const *f, struct bitmap_data const *bitmap)
{
	return bitmap->width > 0 && f->canvas_width > 0 && bitmap->width != f->canvas_width ?
		(real)f->canvas_width / (real)bitmap->width : 1.0f;
}

/* a meter of rounds (its image's marks, h2_hud_file_cell) drawn from the
point: each round lit while the meter's fraction is at least its place in
the order they empty in (Halo 2's meter gradient: the SMG's top row from
the left first), else dim - MCC's frames: a spent round a darker shade of
the lit one's, about half over the view (0.55 of 0.65 of it) */
static void h2_hud_draw_rounds(struct bitmap_data *bitmap, struct h2_hud_file_widget const *f, point2d const *point, real scale,
	real fraction, pixel32 color)
{
	real lit = fraction * 255.0f + 0.5f;
	pixel32 dim = h2_hud_tinted(0x8CA6A6A6UL, 2);
	long c;

	/* (a flash - the reticle's red and the like - lights the spent ones as
	the rest) */
	if ((color & 0x00FFFFFFUL) != (h2_hud_tinted(0xFFFFFFFFUL, 2) & 0x00FFFFFFUL))
		dim = (color & 0x00FFFFFFUL) | 0x8C000000UL;
	for (c = f->cell_first; c < f->cell_first + f->cell_count; c++)
	{
		struct h2_hud_file_cell const *cell = &h2_hud.cells[c];
		real x0 = (real)cell->x0, x1 = (real)cell->x1 + 1.0f, y0 = (real)cell->y0, y1 = (real)cell->y1 + 1.0f;
		real u0 = x0 / (real)f->canvas_width, u1 = x1 / (real)f->canvas_width;
		real v0 = y0 / (real)f->canvas_height, v1 = y1 / (real)f->canvas_height;
		real sx0, sx1, sy0, sy1;

		/* (flipped: mirrored in the image's own rectangle) */
		if (f->flags & 1)
		{
			real t = u0;

			x0 = (real)f->width - ((real)cell->x1 + 1.0f), x1 = (real)f->width - (real)cell->x0;
			u0 = u1, u1 = t;
		}
		if (f->flags & 2)
		{
			real t = v0;

			y0 = (real)f->height - ((real)cell->y1 + 1.0f), y1 = (real)f->height - (real)cell->y0;
			v0 = v1, v1 = t;
		}
		sx0 = h2_hud_floor(point->x + x0 * scale + 0.5f), sx1 = h2_hud_floor(point->x + x1 * scale + 0.5f);
		sy0 = h2_hud_floor(point->y + y0 * scale + 0.5f), sy1 = h2_hud_floor(point->y + y1 * scale + 0.5f);
		h2_hud_quad_blend(bitmap, sx0, sy0, sx1, sy1, u0, v0, u1, v1, (real)cell->threshold <= lit ? color : dim, 0);
	}
}

/* a scoreboard row's emblem, left of its meter (Halo 2's scoreboard: the
top row -68, the second -32 - what the widgets' offsets and their effects come
to with hud_input2);
a free for all's the emblem (waypoint_emblem), a team game's a plain square
of the player's team's colour */
static void h2_hud_draw_emblem(short local_player_index, struct h2_hud_file_widget const *f, struct h2_hud_state const *state)
{
	long player_index = f->special == 2 ? state->rival_player_index : state->player_index;
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	struct h2_hud_file_widget at = *f;
	point2d point;
	real scale, size;

	if (!player)
		return;
	/* (the rows swap with the lead, as the score bars' rows: the leader's on
	top, the player's while it leads or ties) */
	at.offset[1] = (f->special == 2) == state->leading ? -32 : -68;
	h2_hud_point(local_player_index, &at, &point, &scale);
	size = (real)f->width * scale;
	if (f->justification)
	{
		long white = h2_hud_tag("h2\\hud\\white");
		struct bitmap_data *bitmap = white != NONE ? h2_hud_bitmap(white) : NULL;

		if (bitmap)
			h2_hud_quad(bitmap, (real)point.x, (real)point.y, (real)point.x + size, (real)point.y + size, 0.0f, 0.0f, 0.0f, 0.0f,
				0xFF000000UL | h2_emblem_team_rgb((short)player->team_index));
	}
	else
	{
		struct h2_emblem emblem;
		struct bitmap_data *bitmap;

		h2_emblem_player(player_index, &emblem);
		if ((bitmap = h2_emblem_bitmap(&emblem, H2_EMBLEM_WHOLE)) != NULL)
			h2_hud_quad(bitmap, (real)point.x, (real)point.y, (real)point.x + size, (real)point.y + size, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFUL);
	}
}

static void h2_hud_draw_bitmap(short local_player_index, struct h2_hud_widget const *widget, struct h2_hud_state const *state)
{
	struct h2_hud_file_widget const *f = &widget->file;
	struct h2_hud_file_widget elite;
	long tag = widget->bitmap_tag, foreground_tag = widget->bitmap_fg_tag;
	struct bitmap_data *bitmap;
	point2d point;
	real scale, u1, v1;
	real_rectangle2d clip;
	pixel32 color = 0xFFFFFFFF;
	boolean flashing = FALSE, rounds = FALSE;
	real rounds_fraction = 0.0f;

	/* (the Elite's panels: the sequence's second image, its own size) */
	if (h2_hud_elite && widget->bitmap_alt_tag != NONE && widget->file.alt_canvas_width)
	{
		elite = widget->file;
		elite.width = elite.alt_width, elite.height = elite.alt_height;
		elite.canvas_width = elite.alt_canvas_width, elite.canvas_height = elite.alt_canvas_height;
		elite.unused[0] = elite.alt_shift;
		f = &elite;
		tag = widget->bitmap_alt_tag;
		foreground_tag = widget->bitmap_alt_fg_tag;
	}
	if (tag == NONE || !f->canvas_width || !f->canvas_height ||
		!(bitmap = h2_hud_bitmap(tag)))
	{
		return;
	}
	u1 = (real)f->width / (real)f->canvas_width;
	v1 = (real)f->height / (real)f->canvas_height;
	if (getenv("HALO_H2_HUD_LOG"))
	{
		static int logged;

		if (logged++ < 40)
			platform_log("h2 hud: %s: %dx%d of %dx%d, shift %d, anchor %d, offset %d %d; bitmap %dx%d", f->bitmap, (int)f->width, (int)f->height,
				(int)f->canvas_width, (int)f->canvas_height, (int)f->unused[0], (int)f->anchor, (int)f->offset[0], (int)f->offset[1],
				(int)bitmap->width, (int)bitmap->height);
	}
	if (f->shader == H2_HUD_SHADER_SCOPE)
	{
		/* the scope's mask: its quarter (registered at its far corner) at
		the middle of the screen, mirrored about it into the four quarters,
		its outermost texel stretched over the rest of the screen (Halo 2's
		"scope stretch"); drawn at the HUD's own size (the owner's MCC frames:
		the sniper's 392 unit quarter 437 pixels wide at 1080 lines) */
		rectangle2d window = render.camera.window_bounds;
		/* (the whole view, past the window's safe area) */
		real x0 = 0.0f, y0 = 0.0f;
		real x1 = (real)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0);
		real y1 = (real)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);
		real cx = (real)(window.x0 - render.camera.viewport_bounds.x0) + 0.5f * (real)(window.x1 - window.x0);
		real cy = (real)(window.y0 - render.camera.viewport_bounds.y0) + 0.5f * (real)(window.y1 - window.y0);
		real w, h, e = 0.5f / (real)f->canvas_width, ev = 0.5f / (real)f->canvas_height;

		h2_hud_point(local_player_index, f, &point, &scale);
		scale *= (real)(1 << (f->unused[0] < 4 ? f->unused[0] : 0));
		w = (real)f->width * scale;
		h = (real)f->height * scale;
		h2_hud_quad(bitmap, cx - w, cy - h, cx, cy, 0.0f, 0.0f, u1, v1, color);
		h2_hud_quad(bitmap, cx, cy - h, cx + w, cy, u1, 0.0f, 0.0f, v1, color);
		h2_hud_quad(bitmap, cx - w, cy, cx, cy + h, 0.0f, v1, u1, 0.0f, color);
		h2_hud_quad(bitmap, cx, cy, cx + w, cy + h, u1, v1, 0.0f, 0.0f, color);
		if (cy - h > y0)
			h2_hud_quad(bitmap, x0, y0, x1, cy - h, e, ev, e, ev, color);
		if (cy + h < y1)
			h2_hud_quad(bitmap, x0, cy + h, x1, y1, e, ev, e, ev, color);
		if (cx - w > x0)
			h2_hud_quad(bitmap, x0, cy - h, cx - w, cy + h, e, ev, e, ev, color);
		if (cx + w < x1)
			h2_hud_quad(bitmap, cx + w, cy - h, x1, cy + h, e, ev, e, ev, color);
		return;
	}
	h2_hud_point(local_player_index, f, &point, &scale);
	/* (the lock's marks: on the target, as Halo 2 draws them) */
	if (state->lock_on_screen && (!strcmp(f->name, "locking") || !strcmp(f->name, "locked") || !strcmp(f->name, "lockable")))
	{
		point.x = (short)(state->lock_point[0] + 0.5f);
		point.y = (short)(state->lock_point[1] + 0.5f);
	}
	/* (its image halved: drawn bigger) */
	if (f->unused[0] && f->unused[0] < 4)
		scale *= (real)(1 << f->unused[0]);
	/* (the reticles' images: drawn at half the size of the rest - the
	owner's frames: the ring 104 pixels across on a 220 pixel image) */
	if (strstr(f->bitmap, "hud_reticles"))
		scale *= 0.5f;
	/* (an enemy in the weapon's autoaim range - Halo 2's weapon tag's, in
	the weapon this engine aims with - and cone: the reticle red) */
	if (strstr(f->bitmap, "hud_reticles") && state->target)
		color = 0xFFFF3B30UL, flashing = TRUE;
	clip.x0 = 0.0f;
	clip.x1 = u1;
	clip.y0 = 0.0f;
	clip.y1 = v1;
	/* flips */
	if (f->flags & 1)
		clip.x0 = u1, clip.x1 = 0.0f;
	if (f->flags & 2)
		clip.y0 = v1, clip.y1 = 0.0f;
	/* Halo 2's Territories bars (the scoreboard's territory meter, special
	type 7, at the crosshair anchor under the reticle): a bar a territory in
	play, side by side about the widget's point, each the image dim and
	filled from the left to its fraction in its holder's (or contender's)
	side's colour - unknown_157450.cpp function_15f3a0's zone_colors and
	zone_progress. (The bars' spacing is not in the decompilation: the
	image's own width here) */
	if (f->special == 7)
	{
		real w = (real)f->width * scale, h = (real)f->height * scale;
		real x = (real)point.x - 0.5f * w * (real)state->zone_count, y = (real)point.y - f->registration[1] * h;
		long z;

		for (z = 0; z < state->zone_count; z++)
		{
			point2d at;
			real_rectangle2d part = clip;
			real fraction = PIN(state->zone_fraction[z], 0.0f, 1.0f);
			real const *rgb = state->zone_rgb[z];
			unsigned long r = (unsigned long)(rgb[0] * 255.0f + 0.5f), g = (unsigned long)(rgb[1] * 255.0f + 0.5f), b = (unsigned long)(rgb[2] * 255.0f + 0.5f);

			at.x = (short)(x + (real)z * w + 0.5f);
			at.y = (short)(y + 0.5f);
			h2_hud_image(bitmap, f, &at, scale, &clip, h2_hud_tinted(0x60FFFFFFUL, 1));
			if (fraction > 0.0f && (r | g | b))
			{
				part.x1 = part.x0 + (part.x1 - part.x0) * fraction;
				h2_hud_image(bitmap, f, &at, scale, &part, 0xFF000000UL | (r << 16) | (g << 8) | b);
			}
		}
		return;
	}
	/* a meter: filled by its input from the left - or, a meter of rounds
(a magazine's), each round lit or dim by its input */
	if (f->shader == H2_HUD_SHADER_METER || f->shader == H2_HUD_SHADER_SHIELD || f->special == 5)
	{
		real fraction = f->special == 5 || f->shader == H2_HUD_SHADER_SHIELD ? state->shield : h2_hud_input(f->inputs[0], state);

		if (fraction < 0.0f)
			fraction = 0.0f;
		if (fraction > 1.0f)
			fraction = 1.0f;
		if (f->cell_count && f->shader == H2_HUD_SHADER_METER)
		{
			rounds = TRUE;
			rounds_fraction = fraction;
		}
		/* the score panel's bars (Halo 2's scoreboard: player_score the top
		row, other_player_score the one under it; the MCC Vista footage at
		0-0, 6-0, 1-0: the leader's row on top - this player's while he
		leads or ties -, each bar outlined in its owner's colour over a dark
		translucent inside, filled from the left in that colour to the
		owner's score over the score to win - or the higher score where
		there is none to win, unknown_157450.cpp -; the number in the HUD's
		own colour) */
		if (f->inputs[0] == 0x40 || f->inputs[0] == 0x41)
		{
			point2d at = point;
			boolean top = f->inputs[0] == 0x40;
			boolean self_row = top == state->leading;
			real const *rgb = self_row ? state->self_rgb : state->rival_rgb;
			long owner_score = self_row ? state->score : state->other_score;
			long limit = state->score_limit;
			unsigned long r = (unsigned long)(rgb[0] * 255.0f + 0.5f), g = (unsigned long)(rgb[1] * 255.0f + 0.5f), b = (unsigned long)(rgb[2] * 255.0f + 0.5f);

			if (limit <= 0)
				limit = MAX(MAX(state->score, state->other_score), 1);
			fraction = PIN((real)owner_score / (real)limit, 0.0f, 1.0f);
			at.x = (short)(at.x - f->registration[0] * f->width * scale);
			at.y = (short)(at.y - f->registration[1] * f->height * scale);
			/* (the outline and the inside: the owner's colour, darkened and
			translucent - one image here, not Halo 2's outline over a dark
			inside) */
			h2_hud_image(bitmap, f, &at, scale, &clip, 0x90000000UL | ((r * 5 / 8) << 16) | ((g * 5 / 8) << 8) | (b * 5 / 8));
			/* (the fill: flat, the owner's colour, inside the outline - the
			footage's; the image's gradient is Halo 2's threshold, not a shade) */
			if (fraction > 0.0f)
			{
				long white_tag = h2_hud_tag("h2\\hud\\white");
				struct bitmap_data *white = white_tag != NONE ? h2_hud_bitmap(white_tag) : NULL;
				real inset = 2.0f * scale;
				real x0 = (real)at.x + inset, y0 = (real)at.y + inset;
				real x1 = (real)at.x + ((real)f->width * scale - inset) * fraction + (fraction < 1.0f ? inset * fraction : 0.0f);
				real y1 = (real)at.y + (real)f->height * scale - inset;

				if (white && x1 > x0 + 1.0f)
					h2_hud_quad_blend(white, x0, y0, h2_hud_floor(x1 + 0.5f), h2_hud_floor(y1 + 0.5f), 0.0f, 0.0f, 1.0f, 1.0f,
						0xD0000000UL | (r << 16) | (g << 8) | b, 0);
			}
			return;
		}
		if (!rounds)
		{
			if (fraction <= 0.0f)
				return;
			clip.x1 = clip.x0 + (clip.x1 - clip.x0) * fraction;
		}
	}
	if (f->shader == H2_HUD_SHADER_DIM)
		color = 0x80FFFFFF;   /* (half) */
	/* the flashes (Halo 2's HUD shaders' flash colours, approximated): the
	shield meter red while it is hit, brightening as it recharges. (A
	weapon's panel and meter do not flash with its clip low: their shaders'
	flash amount is their input 1, zero in every weapon's HUD - MCC's
	frames, a battle rifle's last rounds and none left to load) */
	{
		real t = (real)game_time_get() / (real)TICKS_PER_SECOND;
		boolean shield = f->shader == H2_HUD_SHADER_SHIELD || f->special == 5;

		if (shield && state->shield_flash == 1)
			color = (fmod(t * 6.0f, 1.0f) < 0.5f) ? 0xFFFF6060 : 0xFFFFA0A0, flashing = TRUE;
		else if (shield && state->shield_flash == 2)
			color = 0xFF000000 | (0x010101 * (unsigned long)(0xC0 + 0x3F * (0.5f + 0.5f * (real)sin(t * 12.0f))));
	}
	/* (the registration point: its place in the bitmap) */
	point.x = (short)(point.x - f->registration[0] * f->width * scale + 0.5f);
	point.y = (short)(point.y - f->registration[1] * f->height * scale + 0.5f);
	/* (the species' colour: a panel's fill its fill colour, then its
	foreground image - outline and icons - the marks' colour; meters, the
	reticle and the like the marks') */
	/* (a scope's marks: dark, as the mask they are drawn with) */
	if (f->shader == H2_HUD_SHADER_LIGHT)
		color = 0xF0202020UL;
	else if (!flashing)
	{
		boolean panel = f->shader == H2_HUD_SHADER_NORMAL || f->shader == H2_HUD_SHADER_DIM;

		color = h2_hud_tinted(color, strstr(f->bitmap, "motion_sensor") ? 4 : f->shader == H2_HUD_SHADER_SHIELD ? 3 : panel ? 0 : f->shader == H2_HUD_SHADER_METER ? 2 : 1);
	}
	if (rounds)
	{
		h2_hud_draw_rounds(bitmap, f, &point, scale, rounds_fraction, color);
		return;
	}
	/* (a heat meter - the plasma rifle's, input 0x31 -: its gradient's
	colour from a light blue at none to red at full, as far as it is filled
	- MCC's frames: about 100,162,226 at its left end, 145,130,170 at a
	third) */
	if (f->shader == H2_HUD_SHADER_METER && f->inputs[0] == 0x31 && !flashing && clip.x1 != clip.x0)
	{
		real fraction = (real)fabs(clip.x1 - clip.x0) * (real)f->canvas_width / (real)(f->width ? f->width : 1);
		real x0 = (real)point.x, y0 = (real)point.y;
		real wide = (real)fabs(clip.x1 - clip.x0) * (real)f->canvas_width * scale;
		real high = (real)fabs(clip.y1 - clip.y0) * (real)f->canvas_height * scale;
		unsigned long r = (unsigned long)(100.0f + 129.0f * fraction + 0.5f), g = (unsigned long)(162.0f - 91.0f * fraction + 0.5f),
			b = (unsigned long)(226.0f - 160.0f * fraction + 0.5f);

		h2_hud_quad_shaded(bitmap, x0, y0, h2_hud_floor(x0 + wide + 0.5f), h2_hud_floor(y0 + high + 0.5f), clip.x0, clip.y0, clip.x1, clip.y1,
			0xFF64A2E2UL, 0xFF000000UL | (r << 16) | (g << 8) | b, 0);
		return;
	}
	h2_hud_image(bitmap, f, &point, scale, &clip, color);
	if (foreground_tag != NONE)
	{
		struct bitmap_data *foreground = h2_hud_bitmap(foreground_tag);

		if (foreground)
			h2_hud_image(foreground, f, &point, scale, &clip, h2_hud_tinted((color & 0xFF000000UL) | 0xFFFFFFUL, 1));
	}
}

/* (this engine's HUD digits: hud_draw.c's struct) */
struct h2_hud_digits
{
	struct tag_reference number_bitmap;
	char character_width;
	char screen_width;
	char x_offset;
	char y_offset;
	char decimal_point_width;
	char colon_width;
	short pad;
	long unused[19];
};

/* (rasterizer's) */
void rasterizer_draw_unicode_string(rectangle2d const *bounds, rectangle2d const *clip, point2d *cursor_reference, short height_adjust,
	wchar_t const *string);
/* (game_engine.c's: the score held open, its title) */

/* (hud_messaging.c's) */
long hud_get_font_index(void);
extern struct game_variant global_variant;
enum { H2_HUD_JUSTIFY_LEFT = 0, H2_HUD_JUSTIFY_RIGHT = 1, H2_HUD_JUSTIFY_CENTER = 2 };

#include "h2_hud_font.inc"

/* the stroke font's glyph of a character (capitals for lower case; NULL: none) */
static long h2_hud_font_glyph(int c)
{
	long i;

	if (c >= 'a' && c <= 'z')
		c -= 32;
	for (i = 0; i < (long)NUMBEROF(h2_font_glyphs); i++)
	{
		if (h2_font_glyphs[i].ch == c)
			return i;
	}
	return NONE;
}

/* a text's width at a cap height (pixels) */
static real h2_hud_font_width(char const *text, real height, real tracking)
{
	real width = 0.0f;
	long glyph;

	for (; *text; text++)
	{
		if ((glyph = h2_hud_font_glyph((unsigned char)*text)) != NONE)
			width += h2_font_glyphs[glyph].advance + tracking;
	}
	return (width > 0.11f + tracking ? width - 0.11f - tracking : width) * height;
}

/* (HALO_GPU_STATS: the HUD's stroke text a frame - the scoreboard, the
time left -, d3d8_gxm.c's "text a frame" line) */
unsigned long h2_hud_text_stats_strings, h2_hud_text_stats_quads, h2_hud_text_stats_draws;

#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
enum { H2_HUD_FONT_QUADS = 256 };
/* (the batches drawn so far: h2_hud_font_draw's cache keeps a string only
if none was drawn while its quads were worked out) */
static unsigned long h2_hud_font_flush_count;
static struct
{
	struct bitmap_data *white;
	long count;
	struct dynamic_screen_vertex vertices[H2_HUD_FONT_QUADS * 4];
} h2_hud_font_quads;

/* the strokes gathered so far, drawn */
static void h2_hud_font_flush(void)
{
	if (h2_hud_font_quads.count)
	{
		struct rasterizer_dynamic_screen_geometry_parameters parameters;

		csmemset(&parameters, 0, sizeof(parameters));
		parameters.map_texture_scale[0].i = parameters.map_texture_scale[0].j = 1.0f;
		parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
		parameters.framebuffer_blend_function = 0;
		parameters.map[0] = h2_hud_font_quads.white;
		rasterizer_psuedo_dynamic_screen_quads_draw(&parameters, h2_hud_font_quads.vertices, h2_hud_font_quads.count);
		h2_hud_font_flush_count++;
		h2_hud_text_stats_quads += h2_hud_font_quads.count;
		h2_hud_text_stats_draws++;
	}
	h2_hud_font_quads.white = NULL;
	h2_hud_font_quads.count = 0;
}
#else
#define h2_hud_font_flush()
#endif

/* a stroke drawn as quads that meet along their joins' miters (alpha
blended, so no overlap but a stroke's crossings): the points in pixels */
static void h2_hud_font_stroke(struct bitmap_data *white, real const (*points)[2], long count, boolean closed, real width, pixel32 color)
{
	real left[64][2], right[64][2];
	long i, n = count;

	if (count < 2 || count > 64)
		return;
	for (i = 0; i < n; i++)
	{
		real px = points[(i + n - 1) % n][0], py = points[(i + n - 1) % n][1];
		real cx = points[i][0], cy = points[i][1];
		real nx = points[(i + 1) % n][0], ny = points[(i + 1) % n][1];
		real ax = cx - px, ay = cy - py, bx = nx - cx, by = ny - cy;
		real la, lb, n1x, n1y, n2x, n2y, mx, my, ml, scale;

		if (!closed && i == 0)
			ax = bx, ay = by;
		if (!closed && i == n - 1)
			bx = ax, by = ay;
		la = (real)sqrt(ax * ax + ay * ay), lb = (real)sqrt(bx * bx + by * by);
		if (la < 1e-6f || lb < 1e-6f)
		{
			la = lb = 1.0f, ax = bx = 1.0f, ay = by = 0.0f;
		}
		n1x = -ay / la, n1y = ax / la, n2x = -by / lb, n2y = bx / lb;
		mx = n1x + n2x, my = n1y + n2y;
		ml = (real)sqrt(mx * mx + my * my);
		if (ml < 1e-3f)
			mx = n1x, my = n1y, ml = 1.0f;
		mx /= ml, my /= ml;
		scale = 0.5f * width / (mx * n1x + my * n1y > 0.55f ? mx * n1x + my * n1y : 0.55f);
		left[i][0] = cx + mx * scale, left[i][1] = cy + my * scale;
		right[i][0] = cx - mx * scale, right[i][1] = cy - my * scale;
	}
	for (i = 0; i < (closed ? n : n - 1); i++)
	{
		struct dynamic_screen_vertex vertices[4];
		struct rasterizer_dynamic_screen_geometry_parameters parameters;
		long j = (i + 1) % n, k;
		real const *quad[4] = { left[i], left[j], right[j], right[i] };
		real const *swapped[4] = { right[i], right[j], left[j], left[i] };
		real cross = (quad[1][0] - quad[0][0]) * (quad[2][1] - quad[0][1]) - (quad[1][1] - quad[0][1]) * (quad[2][0] - quad[0][0]);

		/* (clockwise on the screen: the rasterizer culls the other way round) */
		if (cross < 0.0f)
			memcpy(quad, swapped, sizeof(quad));

		for (k = 0; k < 4; k++)
		{
			vertices[k].position.x = quad[k][0];
			vertices[k].position.y = quad[k][1];
			vertices[k].texture_coordinates.x = vertices[k].texture_coordinates.y = 0.5f;
			vertices[k].color = color;
		}
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
		/* (one state, one draw: h2_hud_font_flush. Each quad its own draw
		was 11 ms of the Vita's frame: 330 of them a frame) */
		if (h2_hud_font_quads.white && (h2_hud_font_quads.white != white || h2_hud_font_quads.count >= H2_HUD_FONT_QUADS))
			h2_hud_font_flush();
		h2_hud_font_quads.white = white;
		memcpy(&h2_hud_font_quads.vertices[h2_hud_font_quads.count++ * 4], vertices, sizeof(vertices));
#else
		csmemset(&parameters, 0, sizeof(parameters));
		parameters.map_texture_scale[0].i = parameters.map_texture_scale[0].j = 1.0f;
		parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
		parameters.framebuffer_blend_function = 0;
		parameters.map[0] = white;
		rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
#endif
	}
}

#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
/* (perf/vita-draw-cost) a string's stroke quads kept while it is drawn the
same: the same text, point, height, justification, colour, tracking and
white bitmap make the same quads (h2_hud_font_draw is a pure function of
them), so they are copied into the batch instead of worked out again (the
miters' square roots and divides, ~250 quads a frame in a game's HUD).
Only a string whose quads fit one batch is kept: drawn from the copy it is
the one draw it was. HALO_HUD_TEXT_CACHE=0 works every string out as
before (re-read at @set); =2 works it out and compares it with the copy
(logged when they differ, then the worked-out quads are drawn). */
enum { H2_HUD_TEXT_CACHE_ENTRIES = 8, H2_HUD_TEXT_CACHE_QUADS = 128, H2_HUD_TEXT_CACHE_TEXT = 32 };
struct h2_hud_text_cache_entry
{
	char text[H2_HUD_TEXT_CACHE_TEXT];
	unsigned long x, y, height, tracking;    /* (the reals' bits) */
	long justification;
	pixel32 color;
	struct bitmap_data *white;
	unsigned long generation;               /* (the map's: h2_hud_text_cache_generation) */
	unsigned long used;                     /* (LRU clock; 0 = empty) */
	long count;
	struct dynamic_screen_vertex vertices[H2_HUD_TEXT_CACHE_QUADS * 4];
};
static struct h2_hud_text_cache_entry *h2_hud_text_cache;
static unsigned long h2_hud_text_cache_clock;
/* (HALO_GPU_STATS's "text a frame": strings drawn from the copy, and the
check mode's strings compared / different) */
unsigned long h2_hud_text_cache_hits, h2_hud_text_cache_checked, h2_hud_text_cache_differed;
/* (HALO_GPU_STATS: the stroke font's time, h2_hud_font_draw's calls) */
unsigned long long h2_hud_text_stats_us;
unsigned long h2_hud_text_profile_strings, h2_hud_text_profile_kept;
unsigned long long vita_host_time_us(void);

static unsigned long h2_hud_real_bits(real value)
{
	unsigned long bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int h2_hud_text_cache_mode(void)
{
	char const *setting = HALO_ENV_ONCE("HALO_HUD_TEXT_CACHE");

	return setting ? atoi(setting) : 1;
}

static struct h2_hud_text_cache_entry *h2_hud_text_cache_find(char const *text, real x, real y, real height, long justification,
	pixel32 color, real tracking, struct bitmap_data *white, boolean *found)
{
	extern unsigned long halo_map_generation;
	struct h2_hud_text_cache_entry *entry, *oldest = NULL;
	unsigned long bx = h2_hud_real_bits(x), by = h2_hud_real_bits(y), bh = h2_hud_real_bits(height), bt = h2_hud_real_bits(tracking);
	long i;

	*found = FALSE;
	if (strlen(text) >= H2_HUD_TEXT_CACHE_TEXT)
		return NULL;
	if (!h2_hud_text_cache && !(h2_hud_text_cache = calloc(H2_HUD_TEXT_CACHE_ENTRIES, sizeof(*h2_hud_text_cache))))
		return NULL;
	for (i = 0; i < H2_HUD_TEXT_CACHE_ENTRIES; i++)
	{
		entry = &h2_hud_text_cache[i];
		if (entry->used && entry->generation == halo_map_generation && entry->white == white && entry->x == bx &&
			entry->y == by && entry->height == bh && entry->tracking == bt && entry->justification == justification &&
			entry->color == color && !strcmp(entry->text, text))
		{
			entry->used = ++h2_hud_text_cache_clock;
			*found = TRUE;
			return entry;
		}
		if (!oldest || entry->used < oldest->used)
			oldest = entry;
	}
	/* (the oldest is made this string's: filled as its quads are worked out) */
	strcpy(oldest->text, text);
	oldest->x = bx, oldest->y = by, oldest->height = bh, oldest->tracking = bt;
	oldest->justification = justification;
	oldest->color = color;
	oldest->white = white;
	oldest->generation = halo_map_generation;
	oldest->used = 0;
	oldest->count = 0;
	return oldest;
}
#endif

/* a text in the stroke font (tracking: more space between letters, in cap heights): its cap top at y, its cap height in pixels, justified
(0 left, 1 centred, 2 right) about x; color's alpha its */
static void h2_hud_font_draw_timed(char const *text, real x, real y, real height, long justification, pixel32 color, real tracking);

static void h2_hud_font_draw(char const *text, real x, real y, real height, long justification, pixel32 color, real tracking)
{
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	if (HALO_ENV_ONCE("HALO_GPU_STATS") || HALO_ENV_ONCE("HALO_RENDER_PROFILE"))
	{
		unsigned long long from = vita_host_time_us();
		unsigned long hits = h2_hud_text_cache_hits;

		h2_hud_font_draw_timed(text, x, y, height, justification, color, tracking);
		h2_hud_text_stats_us += vita_host_time_us() - from;
		h2_hud_text_profile_strings++;
		h2_hud_text_profile_kept += h2_hud_text_cache_hits - hits;
		return;
	}
#endif
	h2_hud_font_draw_timed(text, x, y, height, justification, color, tracking);
}

static void h2_hud_font_draw_timed(char const *text, real x, real y, real height, long justification, pixel32 color, real tracking)
{
	long white_tag = h2_hud_tag("h2\\hud\\white");
	struct bitmap_data *white = white_tag != NONE ? h2_hud_bitmap(white_tag) : NULL;
	real width;
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	int cache_mode = white ? h2_hud_text_cache_mode() : 0;
	boolean cached = FALSE;
	struct h2_hud_text_cache_entry *entry = cache_mode ?
		h2_hud_text_cache_find(text, x, y, height, justification, color, tracking, white, &cached) : NULL;

	if (entry && cached && cache_mode != 2)
	{
		/* (the batch is empty here: every drawer of strokes flushes after) */
		h2_hud_font_flush();
		h2_hud_text_stats_strings++;
		h2_hud_text_cache_hits++;
		h2_hud_font_quads.white = white;
		memcpy(h2_hud_font_quads.vertices, entry->vertices, (size_t)entry->count * 4 * sizeof(struct dynamic_screen_vertex));
		h2_hud_font_quads.count = entry->count;
		h2_hud_font_flush();
		return;
	}
	unsigned long flushes_from;

	if (entry)
		h2_hud_font_flush();
	flushes_from = h2_hud_font_flush_count;
#endif
	width = h2_hud_font_width(text, height, tracking);

	if (getenv("HALO_H2_HUD_LOG") && (game_time_get() % 30) == 0)
		platform_log("h2 hud font: \"%s\" at %.1f %.1f height %.1f width %.1f white %ld %s", text, x, y, height, width, white_tag, white ? "ok" : "NO BITMAP");
	if (!white)
		return;
	h2_hud_text_stats_strings++;
	if (justification == 2)
		x -= width;
	else if (justification == 1)
		x -= 0.5f * width;
	for (; *text; text++)
	{
		long glyph = h2_hud_font_glyph((unsigned char)*text), s;

		if (glyph == NONE)
			continue;
		for (s = h2_font_glyphs[glyph].first_stroke; s < h2_font_glyphs[glyph].first_stroke + h2_font_glyphs[glyph].stroke_count; s++)
		{
			real points[64][2];
			long p;

			for (p = 0; p < h2_font_strokes[s].count && p < 64; p++)
			{
				points[p][0] = x + h2_font_points[h2_font_strokes[s].first + p][0] * height;
				points[p][1] = y + h2_font_points[h2_font_strokes[s].first + p][1] * height;
			}
			h2_hud_font_stroke(white, (real const (*)[2])points, h2_font_strokes[s].count, h2_font_strokes[s].closed, H2_FONT_STROKE * height, color);
		}
		x += (h2_font_glyphs[glyph].advance + tracking) * height;
	}
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	/* (the quads worked out: kept while they are the one batch they began
	as - none flushed on the way, so the batch holds them all) */
	if (entry && h2_hud_font_flush_count == flushes_from && h2_hud_font_quads.count <= H2_HUD_TEXT_CACHE_QUADS)
	{
		if (cached)
		{
			h2_hud_text_cache_checked++;
			if (entry->count != h2_hud_font_quads.count || memcmp(entry->vertices, h2_hud_font_quads.vertices,
				(size_t)entry->count * 4 * sizeof(struct dynamic_screen_vertex)))
			{
				if (h2_hud_text_cache_differed++ < 8)
					platform_log("h2 hud text cache: \"%s\" differs from its copy (%ld quads, kept %ld)", entry->text,
						h2_hud_font_quads.count, entry->count);
			}
		}
		else
		{
			memcpy(entry->vertices, h2_hud_font_quads.vertices, (size_t)h2_hud_font_quads.count * 4 * sizeof(struct dynamic_screen_vertex));
			entry->count = h2_hud_font_quads.count;
			entry->used = ++h2_hud_text_cache_clock;
		}
	}
#endif
	h2_hud_font_flush();
}

/* a scoreboard's string (the variant's name, the time left) in this
engine's HUD font, justified at the widget's point */
static void h2_hud_draw_string(short local_player_index, struct h2_hud_file_widget const *f, struct h2_hud_state const *state)
{
	char text[64];
	point2d point;
	real scale;
	long i;

	text[0] = 0;
	/* (the time left of a timed game: Halo 2's game_engine_format_time,
	unknown_1600f0.cpp - m:ss, under a minute " :ss") */
	if (f->text_input == H2_HUD_TEXT_TIME_LEFT)
	{
		if (state->time_left == NONE)
			return;
		if (state->time_left >= 60)
			snprintf(text, sizeof(text), "%ld:%02ld", state->time_left / 60, state->time_left % 60);
		else
			snprintf(text, sizeof(text), ":%02ld", state->time_left);
	}
	if (f->text_input == H2_HUD_TEXT_VARIANT_NAME)
	{
		for (i = 0; i < (long)NUMBEROF(global_variant.human_readable_game_description) && i < (long)sizeof(text) - 1 &&
			global_variant.human_readable_game_description[i]; i++)
		{
			wchar_t c = global_variant.human_readable_game_description[i];

			text[i] = c < 0x80 ? (char)c : '?';
		}
		text[i] = 0;
		/* (a variant without a name: its game's, as Halo 2 names them) */
		if (!text[0])
		{
			static char const *const names[] = { "", "CTF", "Slayer", "Oddball", "King of the Hill", "Race" };
			long engine = global_variant.game_engine_index;

			snprintf(text, sizeof(text), "%s", engine >= 0 && engine < (long)NUMBEROF(names) ? names[engine] : "");
			if (global_variant.universal_variant.teams && text[0])
				snprintf(text, sizeof(text), "Team %s", names[engine]);
		}
	}
	if (getenv("HALO_H2_HUD_LOG") && (game_time_get() % 30) == 0)
		platform_log("h2 hud: %s \"%s\"", f->name, text);
	if (!text[0])
		return;
	h2_hud_point(local_player_index, f, &point, &scale);
	/* (the scoreboard's: the stroke font, 20 units of cap height, its top
	about 11 units under the widget's point, its right edge 3.5 in - the owner's frames) */
	h2_hud_font_draw(text, (real)point.x - (f->justification == 2 ? 3.5f * scale : 0.0f), (real)point.y + 10.1f * scale, 20.0f * scale,
		f->justification, h2_hud_tinted(0xCCFFFFFFUL, 1), 0.14f);
}

static void h2_hud_draw_text(short local_player_index, struct h2_hud_widget const *widget, struct h2_hud_state const *state)
{
	struct h2_hud_file_widget const *f = &widget->file;
	point2d point;
	real scale;
	char text[16];
	long value;

	if (!f->text_input || f->text_input > H2_HUD_TEXT_TIME_LEFT)
		return;
	/* (the variant's name: in this engine's HUD font - Halo 2's fonts are
	not read) */
	if (f->text_input == H2_HUD_TEXT_VARIANT_NAME || f->text_input == H2_HUD_TEXT_TIME_LEFT)
	{
		h2_hud_draw_string(local_player_index, f, state);
		return;
	}
	/* (the score panel's numbers: the top row's the leader's - Halo 2's
	rows, h2_hud_draw_bitmap -, the second row 36 units under the first: its
	bar's) */
	value = f->text_input == H2_HUD_TEXT_PLAYER_SCORE ? (state->leading ? state->score : state->other_score) :
		f->text_input == H2_HUD_TEXT_OTHER_SCORE ? (state->leading ? state->other_score : state->score) :
		(long)h2_hud_input(f->inputs[f->text_input - 1], state);
	if (value < 0)
		value = 0;
	if (state->time_scores && (f->text_input == H2_HUD_TEXT_PLAYER_SCORE || f->text_input == H2_HUD_TEXT_OTHER_SCORE))
	{
		/* (Halo 2's time: ":05", "1:05" - the walkthroughs' Oddball ":00",
		the Vista footage's ":05") */
		if (value >= 60)
			snprintf(text, sizeof(text), "%ld:%02ld", value / 60, value % 60);
		else
			snprintf(text, sizeof(text), ":%02ld", value);
	}
	else
	snprintf(text, sizeof(text), "%ld", value);
	h2_hud_point(local_player_index, f, &point, &scale);
	/* (the stroke font, h2_hud_font.inc: a counter's digits 21.8 units tall,
	the scoreboard's 20, their tops 10.2 and 11.6 under the widget's point -
	the owner's MCC frames; a score's right edge 8 units in) */
	{
		boolean score = f->text_input == H2_HUD_TEXT_PLAYER_SCORE || f->text_input == H2_HUD_TEXT_OTHER_SCORE;
		pixel32 color = h2_hud_tinted(f->shader == H2_HUD_SHADER_DIM ? 0x73FFFFFFUL : 0xFFFFFFFFUL, 1);

		real row = f->text_input == H2_HUD_TEXT_OTHER_SCORE ? 36.0f : 0.0f;

		h2_hud_font_draw(text, (real)point.x - (score && f->justification == 2 ? 8.0f * scale : 0.0f),
			(real)point.y + ((score ? 11.6f : 10.2f) + row) * scale, (score ? 20.0f : 22.4f) * scale, f->justification, color, 0.0f);
	}
}

/* the motion sensor's ping: a ring from its middle to its edge, fading,
each sweep of the sensor (motion_sensor.c's: 2.1 seconds, as MCC's frames -
the ring out to the edge in about 0.9 of them, soft-edged, then nothing),
drawn as three strokes of the HUD's mark colour about the point, the disc's
radius (pixels) */
static void h2_hud_motion_ping(point2d const *center, real radius)
{
	long white_tag = h2_hud_tag("h2\\hud\\white");
	struct bitmap_data *white = white_tag != NONE ? h2_hud_bitmap(white_tag) : NULL;
	real t = (real)fmod((real)game_time_get() / (real)TICKS_PER_SECOND, 2.1f) / 0.9f;
	real points[48][2];
	/* (perf) the ring's 48 directions once, not 288 double sines and
	cosines a frame (the same expression: the same values) */
	static real unit[48][2];
	static boolean unit_made;
	long band, k;

	if (!white || t >= 1.0f || radius <= 2.0f)
		return;
	if (!unit_made)
	{
		for (k = 0; k < 48; k++)
		{
			unit[k][0] = (real)cos(k * 2.0 * 3.14159265 / 48.0);
			unit[k][1] = (real)sin(k * 2.0 * 3.14159265 / 48.0);
		}
		unit_made = TRUE;
	}
	for (band = 0; band < 3; band++)
	{
		real r = radius * (0.06f + 0.88f * t) + (real)(band - 1) * radius * 0.035f;
		real opacity = (1.0f - t) * (band == 1 ? 0.55f : 0.25f);
		pixel32 color = h2_hud_tinted(((unsigned long)(255.0f * opacity + 0.5f) << 24) | 0xFFFFFFUL, 1);

		if (r <= 0.0f)
			continue;
		for (k = 0; k < 48; k++)
		{
			points[k][0] = (real)center->x + r * unit[k][0];
			points[k][1] = (real)center->y + r * unit[k][1];
		}
		h2_hud_font_stroke(white, (real const (*)[2])points, 48, TRUE, radius * 0.04f, color);
	}
	h2_hud_font_flush();
}

/* ---------- public code */

/* (hud_messaging.c) a Halo 2 level's pickup prompt and messages: where Halo
2 puts them - 58 of 1080 lines in from the left, the first line's text
centred 190 lines down (the owner's MCC frames; the point is the first line's top) */
int h2_hud_message_point(point2d *point)
{
	real height = (real)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);

	if (!h2_hud.loaded || local_player_count() > 1)
		return FALSE;
	point->x = (short)(51.0f / 1080.0f * height + 0.5f);
	point->y = (short)(H2_HUD_MESSAGE_TOP / 1080.0f * height + 0.5f);
	return TRUE;
}

/* the words of Halo 2's dual wield prompt after its button ("Hold [Y] to
dual-wield"), read from the player's own Halo: The Master Chief Collection
files each run (Data/UI/Localization/EN_Halo2.bin beside the maps' folder;
its prompts are a button glyph or two and a line, and the second line of the
swap prompt is the one: "<glyphs> <words>"), as nothing of Halo 2's text is
kept here; our own words when the file is not there or has no such line.
(hud_messaging.c: the second line of a pickup prompt) */
#ifdef __linux__
char *realpath(char const *path, char *resolved);
#endif
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);

/* the collection's folder three names up from a file in its maps folder
(maps folder/file <- halo2 <- the collection): its localization file's path */
static boolean h2_hud_localization_path(char *file, char *path, long path_size)
{
	char *slash = strrchr(file, '/');
	long up;

	for (up = 0; up < 3 && slash; up++)
	{
		*slash = 0;
		slash = strrchr(file, '/');
	}
	if (up < 3)
		return FALSE;
	snprintf(path, (size_t)path_size, "%s/Data/UI/Localization/EN_Halo2.bin", file);
	return TRUE;
}

/* (the collection's files are the host's: fopen here is the host's, not the
game's d:\ files') */
#pragma push_macro("fopen")
#undef fopen

/* Halo 2's weapons' words (their tags h2\weapons\<name>, h2_weapons.c):
our own name for each, Halo 2's own spelling and its "Picked up a ..." lines
taken from the player's localization file when it has them */
#define H2_HUD_WEAPON_WORDS 48

static struct
{
	char const *tag;            /* (after h2\weapons\) */
	char const *ours;
	wchar_t name[H2_HUD_WEAPON_WORDS];
	wchar_t picked_up[H2_HUD_WEAPON_WORDS];
	/* "Picked up <count> rounds for ..." (one, several): the words before
	the count and after it ("" none read) */
	wchar_t ammo_before[2][16], ammo_after[2][H2_HUD_WEAPON_WORDS];
	/* (its pickup line Halo 2's: read from the file) */
	boolean read;
} h2_hud_weapon_words[] =
{
	{ "battle_rifle", "Battle Rifle" },
	{ "smg", "SMG" },
	{ "plasma_rifle", "Plasma Rifle" },
	{ "needler", "Needler" },
	{ "shotgun", "Shotgun" },
	{ "sniper_rifle", "Sniper Rifle" },
	{ "rocket_launcher", "Rocket Launcher" },
	{ "magnum", "Magnum" },
	{ "plasma_pistol", "Plasma Pistol" },
	{ "brute_shot", "Brute Shot" },
	{ "energy_blade", "Energy Sword" },
	{ "covenant_carbine", "Covenant Carbine" },
	{ "beam_rifle", "Beam Rifle" },
	{ "brute_plasma_rifle", "Brute Plasma Rifle" },
	{ "sentinel_aggressor_beam", "Sentinel Beam" },
};
#define H2_HUD_WEAPON_WORD_COUNT ((long)(sizeof(h2_hud_weapon_words) / sizeof(h2_hud_weapon_words[0])))

static wchar_t h2_hud_dual_words[48];
static wchar_t h2_hud_ally_words[48];      /* (h2_hud_ally_swap_words) */

/* an ASCII run of the file as wide characters (FALSE: not all ASCII, or
too long) */
static boolean h2_hud_ascii(unsigned char const *from, long length, wchar_t *out, long out_count)
{
	long i;

	if (length < 0 || length >= out_count)
		return FALSE;
	for (i = 0; i < length; i++)
	{
		if (from[i] < ' ' || from[i] >= 0x7F)
			return FALSE;
		out[i] = (wchar_t)from[i];
	}
	out[length] = 0;
	return TRUE;
}

static boolean h2_hud_same_words(unsigned char const *a, long length, char const *b)
{
	long i;

	for (i = 0; i < length && b[i]; i++)
	{
		int x = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i], y = b[i] >= 'A' && b[i] <= 'Z' ? b[i] + 32 : b[i];

		if (x != y)
			return FALSE;
	}
	return i == length && !b[i];
}

/* Halo 2's multiplayer event lines (its game's text, read from the same file
each run; none of it is kept here): what the local player is told of a kill.
Each is a template with the players' names in it (#cause_player,
#effect_player) and sometimes a medal's glyph before it (not drawn: this
engine's font has none) */
#define H2_HUD_EVENT_WORDS 96

enum
{
	H2_HUD_EVENT_YOU_KILLED,        /* (effect: the dead) */
	H2_HUD_EVENT_KILLED_BY,         /* (cause: the killer) */
	H2_HUD_EVENT_BETRAYED_YOU,      /* (cause) */
	H2_HUD_EVENT_YOU_BETRAYED,      /* (effect) */
	H2_HUD_EVENT_SUICIDE,
	H2_HUD_EVENT_DOUBLE_KILL,
	H2_HUD_EVENT_TRIPLE_KILL,
	H2_HUD_EVENT_KILLTACULAR,
	H2_HUD_EVENT_KILLING_SPREE,
	H2_HUD_EVENT_RUNNING_RIOT,
	H2_HUD_EVENT_COUNT
};

static char h2_hud_event_text_read[H2_HUD_EVENT_COUNT][H2_HUD_EVENT_WORDS];

/* a string of the file without its leading glyphs and spaces ("" if it is
not plain ASCII) */
static char const *h2_hud_event_strip(unsigned char const *string, long length)
{
	long from = 0, i;

	while (from < length && (string[from] == 0xEE || string[from] == ' '))
		from += string[from] == 0xEE ? 3 : 1;
	if (from >= length)
		return "";
	for (i = from; i < length; i++)
	{
		if (string[i] < ' ' || string[i] >= 0x7F)
			return "";
	}
	return (char const *)string + from;
}

static boolean h2_hud_event_starts(char const *text, char const *prefix)
{
	return !strncmp(text, prefix, strlen(prefix));
}

static void h2_hud_event_scan(unsigned char const *data, long size)
{
	static char const *const medals[5] = { "double kill", "triple kill", "killtacular", "killing spree", "running riot" };
	long at = 0;

	while (at < size)
	{
		long length = (long)strnlen((char const *)data + at, (size_t)(size - at));
		char const *text = length > 0 && length < H2_HUD_EVENT_WORDS ? h2_hud_event_strip(data + at, length) : "";
		long event = NONE;

		if (text[0])
		{
			boolean cause = strstr(text, "#cause_player") != NULL, effect = strstr(text, "#effect_player") != NULL;

			if (h2_hud_event_starts(text, "You killed ") && effect && !cause)
				event = H2_HUD_EVENT_YOU_KILLED;
			else if (h2_hud_event_starts(text, "You were killed") && cause && !effect)
				event = H2_HUD_EVENT_KILLED_BY;
			else if (cause && !effect && strstr(text, " betrayed you"))
				event = H2_HUD_EVENT_BETRAYED_YOU;
			else if (h2_hud_event_starts(text, "You betrayed ") && effect && !cause)
				event = H2_HUD_EVENT_YOU_BETRAYED;
			else if (!strcmp(text, "You committed suicide"))
				event = H2_HUD_EVENT_SUICIDE;
			else
			{
				char plain[H2_HUD_EVENT_WORDS];
				long k, n = (long)strlen(text);

				/* (a medal's name: its words, a "!" after them or not) */
				if (n > 0 && text[n - 1] == '!')
					n--;
				for (k = 0; k < n && k < H2_HUD_EVENT_WORDS - 1; k++)
					plain[k] = text[k] >= 'A' && text[k] <= 'Z' ? text[k] + 32 : text[k];
				plain[k] = 0;
				for (k = 0; k < 5; k++)
				{
					if (!strcmp(plain, medals[k]))
					{
						/* (the ones without a "!" are the medals' own lines) */
						if (text[strlen(text) - 1] != '!' || !h2_hud_event_text_read[H2_HUD_EVENT_DOUBLE_KILL + k][0])
						{
							snprintf(h2_hud_event_text_read[H2_HUD_EVENT_DOUBLE_KILL + k], H2_HUD_EVENT_WORDS, "%.*s", (int)n, text);
						}
						break;
					}
				}
			}
			if (event != NONE && !h2_hud_event_text_read[event][0])
				snprintf(h2_hud_event_text_read[event], H2_HUD_EVENT_WORDS, "%s", text);
		}
		at += length + 1;
	}
}

/* the localization file's lines this file uses: the dual wield prompt's
words, the weapons' names and pickup lines */
static void h2_hud_localization_scan(unsigned char const *data, long size)
{
	long at;
	boolean dual_found = FALSE;
	static unsigned char const count_glyph[3] = { 0xEE, 0x90, 0xA2 };

	for (at = 2; at + 12 < size; at++)
	{
		/* (the dual wield prompt: a line after a line break - glyphs and
		spaces, then words with the word "dual" in them, then the string's
		end) */
		if (!dual_found && data[at - 2] == '\r' && data[at - 1] == '\n')
		{
			long from = at, length;

			while (from < size && (data[from] == 0xEE || data[from] == ' '))
				from += data[from] == 0xEE ? 3 : 1;
			length = from < size ? (long)strlen((char const *)data + from) : 0;
			if (from > at && length > 0 && length < 40 && strstr((char const *)data + from, "dual") &&
				h2_hud_ascii(data + from, length, h2_hud_dual_words, NUMBEROF(h2_hud_dual_words)))
			{
				dual_found = TRUE;
			}
		}
		/* (the take ally's weapon prompt: glyphs, then "to take ally's") */
		if (!h2_hud_ally_words[0] && data[at - 1] == ' ' && !memcmp(data + at, "to take ally", 12))
		{
			long length = 0;

			while (at + length < size && data[at + length] >= ' ' && data[at + length] < 0x7F && length < 40)
				length++;
			while (length > 0 && data[at + length - 1] == ' ')
				length--;
			h2_hud_ascii(data + at, length, h2_hud_ally_words, NUMBEROF(h2_hud_ally_words));
		}
		/* (a string's start: "Picked up a(n) <weapon>", "Picked up <count>
		<rounds> for <weapon>") */
		if (data[at - 1] == 0 && !memcmp(data + at, "Picked up ", 10))
		{
			unsigned char const *line = data + at;
			long length = (long)strlen((char const *)line), k;

			for (k = 0; k < H2_HUD_WEAPON_WORD_COUNT; k++)
			{
				long article = !memcmp(line + 10, "a ", 2) ? 12 : !memcmp(line + 10, "an ", 3) ? 13 : 0;

				if (article && !h2_hud_weapon_words[k].read &&
					h2_hud_same_words(line + article, length - article, h2_hud_weapon_words[k].ours) &&
					h2_hud_ascii(line, length, h2_hud_weapon_words[k].picked_up, H2_HUD_WEAPON_WORDS))
				{
					h2_hud_weapon_words[k].read = TRUE;
					h2_hud_ascii(line + article, length - article, h2_hud_weapon_words[k].name, H2_HUD_WEAPON_WORDS);
				}
				else if (!memcmp(line + 10, count_glyph, 3) && length > 20)
				{
					char const *for_word = strstr((char const *)line + 13, " for ");
					long weapon_at = for_word ? (long)(for_word - (char const *)line) + 5 : 0;

					if (weapon_at && h2_hud_same_words(line + weapon_at, length - weapon_at, h2_hud_weapon_words[k].ours))
					{
						/* (several: its unit's word ends in s) */
						int plural = for_word[-1] == 's';

						if (!h2_hud_weapon_words[k].ammo_after[plural][0] &&
							h2_hud_ascii(line, 10, h2_hud_weapon_words[k].ammo_before[plural], 16))
						{
							h2_hud_ascii(line + 13, length - 13, h2_hud_weapon_words[k].ammo_after[plural], H2_HUD_WEAPON_WORDS);
						}
					}
				}
			}
		}
	}
}

/* the words read (once): ours first, Halo 2's from the file where it has them */
static void h2_hud_localization_read(void)
{
	static boolean tried = FALSE;
	char path[1100];
	char const *maps;
	char const *override = getenv("HALO_H2_LOCALIZATION");
	FILE *file = NULL;
	long attempt, k;

	if (tried)
		return;
	tried = TRUE;
	{
		/* (ours) */
		char const *own = "to dual wield";
		long i;

		for (i = 0; own[i]; i++)
			h2_hud_dual_words[i] = (wchar_t)own[i];
		h2_hud_dual_words[i] = 0;
	}
	for (k = 0; k < H2_HUD_WEAPON_WORD_COUNT; k++)
	{
		char text[H2_HUD_WEAPON_WORDS];
		char const *ours = h2_hud_weapon_words[k].ours;

		h2_hud_ascii((unsigned char const *)ours, (long)strlen(ours), h2_hud_weapon_words[k].name, H2_HUD_WEAPON_WORDS);
		snprintf(text, sizeof(text), "Picked up %s %s", strchr("AEIOU", ours[0]) || !strcmp(ours, "SMG") ? "an" : "a", ours);
		h2_hud_ascii((unsigned char const *)text, (long)strlen(text), h2_hud_weapon_words[k].picked_up, H2_HUD_WEAPON_WORDS);
		h2_hud_weapon_words[k].ammo_before[0][0] = h2_hud_weapon_words[k].ammo_before[1][0] = 0;
		h2_hud_weapon_words[k].ammo_after[0][0] = h2_hud_weapon_words[k].ammo_after[1][0] = 0;
	}
	maps = h2_level_maps_directory();
	for (attempt = 0; attempt < 3 && !file; attempt++)
	{
		char host[1024];

		path[0] = 0;
		if (attempt == 0 && override)
			snprintf(path, sizeof(path), "%s", override);
		else if (attempt >= 1 && maps[0])
		{
			char guest[600];

			snprintf(guest, sizeof(guest), "%sshared.map", maps);
			host[0] = 0;
			platform_translate_path(guest, host, sizeof(host));
			if (host[0])
			{
				char resolved[1024];

				/* (attempt 2: the file the maps folder links to) */
#ifdef __linux__
				if (attempt == 2 && realpath(host, resolved))
					h2_hud_localization_path(resolved, path, sizeof(path));
				else
#endif
				if (attempt == 1)
					h2_hud_localization_path(host, path, sizeof(path));
			}
		}
		if (path[0])
			file = fopen(path, "rb");
		if ((getenv("HALO_LOG_DUAL_WIELD") || getenv("HALO_LOG_H2_WORDS")) && path[0])
			platform_log("h2 words: from %s: %s", path, file ? "opened" : "no");
	}
	if (file)
	{
		long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : 0;
		unsigned char *data = size > 0 && size < 8 * 1024 * 1024 && fseek(file, 0, SEEK_SET) == 0 ? malloc((size_t)size + 1) : NULL;

		if (data && fread(data, 1, (size_t)size, file) == (size_t)size)
		{
			data[size] = 0;
			h2_hud_localization_scan(data, size);
			h2_hud_event_scan(data, size);
		}
		free(data);
		fclose(file);
	}
	if (getenv("HALO_LOG_H2_WORDS"))
	{
		for (k = 0; k < H2_HUD_WEAPON_WORD_COUNT; k++)
		{
			char a[H2_HUD_WEAPON_WORDS], b[H2_HUD_WEAPON_WORDS], c[H2_HUD_WEAPON_WORDS];
			long i;

			for (i = 0; i < H2_HUD_WEAPON_WORDS; i++)
			{
				a[i] = (char)h2_hud_weapon_words[k].picked_up[i];
				b[i] = (char)h2_hud_weapon_words[k].ammo_after[0][i];
				c[i] = (char)h2_hud_weapon_words[k].ammo_after[1][i];
			}
			platform_log("h2 words: %s: \"%s\", ammo \"<n>%s\" / \"<n>%s\"", h2_hud_weapon_words[k].tag, a, b, c);
		}
	}
}

/* Halo 2's "take ally's weapon" prompt's words (HUD string ps_swap_ai:
"<button> to take ally's <weapon>"): Halo 2's own from the localization
file when it has them, else ours */
wchar_t const *h2_hud_ally_swap_words(void)
{
	h2_hud_localization_read();
	if (!h2_hud_ally_words[0])
	{
		char const *own = "to take ally's";
		long i;

		for (i = 0; own[i]; i++)
			h2_hud_ally_words[i] = (wchar_t)own[i];
		h2_hud_ally_words[i] = 0;
	}
	return h2_hud_ally_words;
}

wchar_t const *h2_hud_dual_wield_words(void)
{
	h2_hud_localization_read();
	return h2_hud_dual_words;
}

/* (game_engine.c) a Halo 2 level's multiplayer event line, Halo 2's words
from the player's localization file with the players' names put in (cause,
effect: NULL where it takes none); FALSE if this is not a Halo 2 level, or
the file has no such line (this engine's line is then used). event: one of
H2_HUD_EVENT_... as game_engine.c numbers them (0 you killed, 1 killed by,
2 betrayed you, 3 you betrayed, 4 suicide, 5 double kill, 6 triple kill,
7 killtacular, 8 killing spree, 9 running riot) */
int h2_hud_event_text(int event, wchar_t const *cause, wchar_t const *effect, wchar_t *out, long out_count)
{
	char const *template_text;
	long length = 0;

	h2_hud_load();
	if (!h2_hud.loaded || event < 0 || event >= H2_HUD_EVENT_COUNT || out_count < 2)
		return FALSE;
	h2_hud_localization_read();
	template_text = h2_hud_event_text_read[event];
	if (!template_text[0])
		return FALSE;
	while (*template_text && length < out_count - 1)
	{
		wchar_t const *name = NULL;
		long skip = 0;

		if (!strncmp(template_text, "#cause_player", 13))
			name = cause ? cause : L"", skip = 13;
		else if (!strncmp(template_text, "#effect_player", 14))
			name = effect ? effect : L"", skip = 14;
		if (name)
		{
			while (*name && length < out_count - 1)
				out[length++] = *name++;
			template_text += skip;
		}
		else
			out[length++] = (wchar_t)(unsigned char)*template_text++;
	}
	out[length] = 0;
	return TRUE;
}

/* ---------- Halo 2's multiplayer events */

/* Halo 2's multiplayer events, as its multiplayer globals list them (mulg,
the runtime block's general, flavor and game type event blocks): each an
event of its kind told to an audience - the cause player, his team, the
effect player, his team, or everyone - as a line of its display string
(the players' names put in for #cause_player and #effect_player, the teams'
for #cause_team and #effect_team) and the announcer's line. Read from the
player's own map when a Halo 2 level's first event comes (MCC's shared.map:
the runtime block at mulg 0x8, its event blocks at 0x98 on, 0xA8 bytes an
event - event at 4, audience at 6, display string at 0xC, the English
sound's reference at 0x44 (the sound flags at 0x40); the texts the globals' language pack,
h2_cache_localized_string); nothing of Halo 2's is kept here. Halo 2's
rules of the kill events: the decompilation's unknown_157450.cpp (the
player's death: the kill event by its damage, then one flavor event - the
chain's, else the streak's, else the broken spree's). */
enum
{
	H2_HUD_EVENTS_GENERAL = 0,
	H2_HUD_EVENTS_FLAVOR,
	H2_HUD_EVENTS_SLAYER,
	H2_HUD_EVENTS_CTF,
	H2_HUD_EVENTS_ODDBALL,
	H2_HUD_EVENTS_KING,
	H2_HUD_EVENTS_JUGGERNAUT,
	H2_HUD_EVENTS_TERRITORIES,
	H2_HUD_EVENTS_ASSAULT,
	H2_HUD_EVENTS_KINDS,

	/* Halo 2's audiences */
	H2_HUD_AUDIENCE_CAUSE_PLAYER = 0,
	H2_HUD_AUDIENCE_CAUSE_TEAM,
	H2_HUD_AUDIENCE_EFFECT_PLAYER,
	H2_HUD_AUDIENCE_EFFECT_TEAM,
	H2_HUD_AUDIENCE_ALL,

	/* Halo 2's general events used here */
	H2_HUD_GENERAL_KILL = 0,
	H2_HUD_GENERAL_SUICIDE = 1,
	H2_HUD_GENERAL_KILL_TEAMMATE = 2,
	H2_HUD_GENERAL_KILLED_BY_UNKNOWN = 13,
	H2_HUD_GENERAL_KILL_FALLING = 34,
	H2_HUD_GENERAL_KILL_COLLISION = 35,
	H2_HUD_GENERAL_KILL_MELEE = 36,
	H2_HUD_GENERAL_KILL_STICKY = 41,
	H2_HUD_GENERAL_KILL_SNIPER = 42,
	H2_HUD_GENERAL_KILL_SNEAK_MELEE = 43,
	/* its flavor events */
	H2_HUD_FLAVOR_DOUBLE_KILL = 0,
	H2_HUD_FLAVOR_TRIPLE_KILL = 1,
	H2_HUD_FLAVOR_KILLTACULAR = 2,
	H2_HUD_FLAVOR_KILLING_SPREE = 3,
	H2_HUD_FLAVOR_RUNNING_RIOT = 4,
	H2_HUD_FLAVOR_BROKE_SPREE = 6,
	H2_HUD_FLAVOR_KILL_FRENZY = 7,
	H2_HUD_FLAVOR_KILLTROCITY = 8,
	H2_HUD_FLAVOR_KILLIMANJARO = 9,
	H2_HUD_FLAVOR_15_IN_A_ROW = 10,
	H2_HUD_FLAVOR_20_IN_A_ROW = 11,
	H2_HUD_FLAVOR_25_IN_A_ROW = 12,

	H2_HUD_MP_EVENTS = 192,
	H2_HUD_MP_EVENT_TEXT = 96,
	H2_HUD_MP_EVENT_SOUND = 96,
	H2_HUD_MP_STRING_IDS = 640,
	/* the weapons' prompts (a weap tag's ten message string ids, 0xCC on:
	h2_hud_weapon_message's H2_HUD_PROMPT_...) */
	H2_HUD_MP_WEAPONS = 40,
	H2_HUD_MP_WEAPON_MESSAGES = 10,
	H2_HUD_MP_PROMPT_TEXT = 96,

};

static void h2_hud_wide_append(wchar_t *out, long out_count, wchar_t const *text);
static void h2_hud_prompt_plain(wchar_t const *text, long count, wchar_t *out, long out_count);

struct h2_hud_mp_event
{
	unsigned char kind, audience;
	short event;
	unsigned long string_id;                /* (its display string, while read) */
	wchar_t text[H2_HUD_MP_EVENT_TEXT];     /* ("" none: a sound alone) */
	char sound[H2_HUD_MP_EVENT_SOUND];      /* (the English sound's tag name; "" none) */
};

static struct
{
	char level[64];             /* (the level read for; "" none) */
	long count;
	struct h2_hud_mp_event events[H2_HUD_MP_EVENTS];
	/* (the teams' names, "Red Team" and "Blue Team": the pack's "red" and "blue") */
	wchar_t team_names[2][32];
	/* the globals' player information's sneak forward speed (world units a
	second; 0 none read): a player's unit shows on the motion sensor moving
	1.05 times it or faster */
	float sneak_forward;
	/* the multiplayer globals' team colours (the universal block's, 0x10:
	red, blue, yellow, green, purple, orange, brown, pink, grey) */
	long team_color_count;
	float team_colors[16][3];
	/* the weapons' prompts: each weap tag of the map (its name's last part,
	"battle_rifle") and the pack's texts of its ten message string ids
	(0xCC: pickup, swap, pickup or dual, swap or dual, dual-only, picked
	up, singular quantity, plural quantity, switch-to, switch-to from ai),
	their glyphs kept (the private-use characters MCC's font draws as the
	buttons and the weapons' pictures); "" none */
	long weapon_count;
	struct h2_hud_mp_weapon
	{
		char name[32];
		unsigned long ids[H2_HUD_MP_WEAPON_MESSAGES];
		wchar_t text[H2_HUD_MP_WEAPON_MESSAGES][H2_HUD_MP_PROMPT_TEXT];
	} weapons[H2_HUD_MP_WEAPONS];
} h2_hud_mp;

/* (h2_hud_draw) a team's colour of Halo 2's multiplayer globals (FALSE: none read) */
static boolean h2_hud_mp_team_rgb(long team, real rgb[3])
{
	if (team < 0 || team >= h2_hud_mp.team_color_count)
		return FALSE;
	rgb[0] = h2_hud_mp.team_colors[team][0];
	rgb[1] = h2_hud_mp.team_colors[team][1];
	rgb[2] = h2_hud_mp.team_colors[team][2];
	return TRUE;
}

char const *cache_files_map_directory(void);
void game_engine_play_h2_announcement(char const *h2_name);

/* a pack string's UTF-8 as the game's characters, without Halo 2's medal
glyphs (its private-use characters, a font this engine has not) and the
spaces before the words */
static void h2_hud_mp_text(char const *words, wchar_t *out, long out_count)
{
	unsigned char const *in = (unsigned char const *)words;
	long length = 0;

	for (; *in && length < out_count - 1; in++)
	{
		unsigned long c = *in;

		if (c >= 0xE0 && in[1] && in[2])
			c = (c & 0x0F) << 12 | (in[1] & 0x3F) << 6 | (in[2] & 0x3F), in += 2;
		else if (c >= 0xC0 && in[1])
			c = (c & 0x1F) << 6 | (in[1] & 0x3F), in++;
		if (c >= 0xE000 && c <= 0xF8FF)
			continue;
		if (c == ' ' && length == 0)
			continue;
		out[length++] = (wchar_t)c;
	}
	out[length] = 0;
}

/* a pack string as wide characters, its glyphs kept (a prompt's) */
static void h2_hud_mp_text_glyphs(char const *words, wchar_t *out, long out_count)
{
	unsigned char const *in = (unsigned char const *)words;
	long length = 0;

	for (; *in && length < out_count - 1; in++)
	{
		unsigned long c = *in;

		if (c >= 0xE0 && in[1] && in[2])
			c = (c & 0x0F) << 12 | (in[1] & 0x3F) << 6 | (in[2] & 0x3F), in += 2;
		else if (c >= 0xC0 && in[1])
			c = (c & 0x1F) << 6 | (in[1] & 0x3F), in++;
		out[length++] = (wchar_t)c;
	}
	out[length] = 0;
}

/* the events read from a cache that has the multiplayer globals (FALSE: it
has not) */
static boolean h2_hud_mp_read(struct h2_cache *cache)
{
	static unsigned long const block_offsets[H2_HUD_EVENTS_KINDS] = { 0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC8, 0xD8, 0xE0, 0xE8 };
	long tag = h2_cache_tag_find(cache, 'mulg', "multiplayer\\multiplayer_globals");
	struct h2_cache_tag mulg;
	unsigned long runtime, elements;
	long count, kind, k, index, left, misses;
	unsigned long wanted[H2_HUD_MP_STRING_IDS];
	long wanted_count = 0;

	if (tag == NONE || !h2_cache_tag_get(cache, tag, &mulg) || mulg.group_tag != 'mulg' ||
		!h2_cache_block(cache, mulg.address + 8, 0x540, &count, &runtime) || count < 1)
	{
		return FALSE;
	}
	h2_hud_mp.count = 0;
	/* the team colours */
	h2_hud_mp.team_color_count = 0;
	{
		unsigned long universal, colors;
		long count2, k;

		if (h2_cache_block(cache, mulg.address, 0x1C, &count2, &universal) && count2 > 0 &&
			h2_cache_block(cache, universal + 0x10, 12, &count2, &colors) && count2 > 0)
		{
			for (k = 0; k < count2 && k < 16; k++)
			{
				unsigned char const *p = h2_cache_at(cache, colors + 12 * (unsigned long)k, 12);

				if (!p)
					break;
				memcpy(h2_hud_mp.team_colors[k], p, 12);
				h2_hud_mp.team_color_count = k + 1;
			}
		}
	}
	/* (the globals' player information, matg 0x130: sneak forward at 0x3C
	- the player's crouched walk, 0.9 - after walking 0x24, run forward
	0x2C, run backward, run sideways, run acceleration) */
	{
		long i;

		for (i = 0; i < h2_cache_tag_count(cache); i++)
		{
			struct h2_cache_tag matg;
			long count2;
			unsigned long element;
			unsigned char const *p;

			if (!h2_cache_tag_get(cache, i, &matg) || matg.group_tag != 'matg')
				continue;
			if (h2_cache_block(cache, matg.address + 0x130, 0x50, &count2, &element) && count2 > 0 && (p = h2_cache_at(cache, element + 0x3C, 4)) != NULL)
			{
				float speed;

				memcpy(&speed, p, 4);
				if (speed > 0.0f && speed < 10.0f)
					h2_hud_mp.sneak_forward = speed;
			}
			break;
		}
	}
	for (kind = 0; kind < H2_HUD_EVENTS_KINDS; kind++)
	{
		if (!h2_cache_block(cache, runtime + block_offsets[kind], 0xA8, &count, &elements))
			continue;
		for (k = 0; k < count && h2_hud_mp.count < H2_HUD_MP_EVENTS; k++)
		{
			unsigned char const *e = h2_cache_at(cache, elements + 0xA8 * (unsigned long)k, 0xA8);
			struct h2_hud_mp_event *event = &h2_hud_mp.events[h2_hud_mp.count];
			long sound;

			if (!e)
				break;
			memset(event, 0, sizeof(*event));
			event->kind = (unsigned char)kind;
			event->event = (short)(e[4] | (e[5] << 8));
			event->audience = e[6];
			event->string_id = (unsigned long)e[0xC] | (unsigned long)e[0xD] << 8 | (unsigned long)e[0xE] << 16 | (unsigned long)e[0xF] << 24;
			sound = h2_cache_reference(cache, elements + 0xA8 * (unsigned long)k + 0x44);
			if (sound != NONE)
			{
				char const *name = h2_cache_tag_name(cache, sound);

				snprintf(event->sound, sizeof(event->sound), "%s", !strncmp(name, "sound\\", 6) ? name + 6 : name);
			}
			if (event->string_id && event->string_id != 0xFFFFFFFFUL && wanted_count < H2_HUD_MP_STRING_IDS)
				wanted[wanted_count++] = event->string_id;
			h2_hud_mp.count++;
		}
	}
	/* the weapons' prompts' string ids (weap 0xCC: the ten messages after
	the on-ground scales at 0xC4, 0xC8 - H2EK's item block, checked against
	shared.map's battle rifle: br_pickup, br_swap, -, -, -, br_picked_up,
	br_ammo_singular, br_ammo_plural, br_switch_to, br_swap_ai) */
	h2_hud_mp.weapon_count = 0;
	for (index = 0; index < h2_cache_tag_count(cache) && h2_hud_mp.weapon_count < H2_HUD_MP_WEAPONS; index++)
	{
		struct h2_cache_tag weap;
		struct h2_hud_mp_weapon *w = &h2_hud_mp.weapons[h2_hud_mp.weapon_count];
		char const *name, *slash;
		unsigned char const *p;
		long m;

		if (!h2_cache_tag_get(cache, index, &weap) || weap.group_tag != 'weap' || !(p = h2_cache_at(cache, weap.address + 0xCC, 4 * H2_HUD_MP_WEAPON_MESSAGES)))
			continue;
		name = h2_cache_tag_name(cache, index);
		slash = name ? strrchr(name, '\\') : NULL;
		if (!slash)
			continue;
		memset(w, 0, sizeof(*w));
		snprintf(w->name, sizeof(w->name), "%s", slash + 1);
		for (m = 0; m < H2_HUD_MP_WEAPON_MESSAGES; m++)
		{
			w->ids[m] = (unsigned long)p[4 * m] | (unsigned long)p[4 * m + 1] << 8 | (unsigned long)p[4 * m + 2] << 16 | (unsigned long)p[4 * m + 3] << 24;
			if (w->ids[m] && w->ids[m] != 0xFFFFFFFFUL && wanted_count < H2_HUD_MP_STRING_IDS)
				wanted[wanted_count++] = w->ids[m];
			else
				w->ids[m] = 0;
		}
		h2_hud_mp.weapon_count++;
	}
	/* their texts (the pack's strings with their ids), and the teams' names */
	h2_hud_mp.team_names[0][0] = h2_hud_mp.team_names[1][0] = 0;
	left = wanted_count + 2;
	for (index = 0, misses = 0; left > 0 && index < 8192 && misses < 64; index++)
	{
		unsigned long id = 0xFFFFFFFFUL;
		char words[4 * H2_HUD_MP_EVENT_TEXT];
		boolean ok = h2_cache_localized_string(cache, 0, index, &id, words, sizeof(words));

		if (!ok)
		{
			/* (past the pack's end: no id read) */
			if (id == 0xFFFFFFFFUL)
				misses++;
			continue;
		}
		misses = 0;
		/* (the teams' names: the strings named red and blue) */
		if (!strcmp(words, "Red Team") && !h2_hud_mp.team_names[0][0])
			h2_hud_mp_text(words, h2_hud_mp.team_names[0], NUMBEROF(h2_hud_mp.team_names[0])), left--;
		else if (!strcmp(words, "Blue Team") && !h2_hud_mp.team_names[1][0])
			h2_hud_mp_text(words, h2_hud_mp.team_names[1], NUMBEROF(h2_hud_mp.team_names[1])), left--;
		for (k = 0; k < h2_hud_mp.count; k++)
		{
			if (h2_hud_mp.events[k].string_id == id && !h2_hud_mp.events[k].text[0])
			{
				h2_hud_mp_text(words, h2_hud_mp.events[k].text, H2_HUD_MP_EVENT_TEXT);
				left--;
			}
		}
		for (k = 0; k < h2_hud_mp.weapon_count; k++)
		{
			long m;

			for (m = 0; m < H2_HUD_MP_WEAPON_MESSAGES; m++)
			{
				if (h2_hud_mp.weapons[k].ids[m] == id && !h2_hud_mp.weapons[k].text[m][0])
				{
					h2_hud_mp_text_glyphs(words, h2_hud_mp.weapons[k].text[m], H2_HUD_MP_PROMPT_TEXT);
					left--;
				}
			}
		}
	}
	return h2_hud_mp.count > 0;
}

/* the level's events read once (shared.map, else the level's map) */
static void h2_hud_mp_load(void)
{
	char const *directory;
	struct h2_cache *cache;
	char why[160];
	boolean read = FALSE;
	long k, with_text = 0, with_sound = 0;
	short attempt;

	h2_hud_load();
	if (!h2_hud.loaded || !strcmp(h2_hud_mp.level, h2_hud.level))
		return;
	snprintf(h2_hud_mp.level, sizeof(h2_hud_mp.level), "%s", h2_hud.level);
	h2_hud_mp.count = 0;
	h2_hud_mp.sneak_forward = 0.0f;
	/* (perf: the maps folder found - a file opened to see - only when the
	level's events are read, not at every HUD draw) */
	directory = h2_maps_directory();
	if (!directory)
		return;
	/* (a multiplayer map's globals are shared.map's, read there: its sounds'
	references are that file's handles; a level's own otherwise) */
	for (attempt = 0; attempt < 2 && !read; attempt++)
	{
		if ((cache = h2_cache_open(directory, attempt == 0 ? "shared" : h2_hud.level, why, sizeof(why))) == NULL)
		{
			platform_log("h2 hud: %s: %s%s.map not read for the events: %s", h2_hud.level, directory,
				attempt == 0 ? "shared" : h2_hud.level, why);
			continue;
		}
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
		/* (the Vita, as the sound loader's reads, h2_sound.c: the map's
		inflated pieces to a budget, in the memory window - kept whole in
		the C heap, the walk over shared.map's tags and its strings ran out
		of it with a level loaded: insider.2's events had no text, 27 of
		the 40 weapons' prompts and none of their lines, and the pause
		screen's boxes were empty) */
		h2_cache_segment_memory(cache, h2_level_window_alloc, h2_level_window_free);
		h2_cache_segment_budget(cache, 2048UL * 1024UL);
#endif
		read = h2_hud_mp_read(cache);
		h2_cache_close(cache);
	}
	for (k = 0; k < h2_hud_mp.count; k++)
	{
		with_text += h2_hud_mp.events[k].text[0] != 0;
		with_sound += h2_hud_mp.events[k].sound[0] != 0;
	}
	platform_log("h2 hud: %s: %ld multiplayer events read (%ld with text, %ld with a sound); players show on the motion sensor from %.3f wu/s", h2_hud.level, h2_hud_mp.count, with_text, with_sound, 1.05f * h2_hud_mp.sneak_forward);
	{
		long w, m, with_prompt = 0;

		for (w = 0; w < h2_hud_mp.weapon_count; w++)
		{
			for (m = 0; m < H2_HUD_MP_WEAPON_MESSAGES; m++)
				with_prompt += h2_hud_mp.weapons[w].text[m][0] != 0;
		}
		platform_log("h2 hud: %s: %ld weapons' prompts read from the map's pack (%ld lines)", h2_hud.level, h2_hud_mp.weapon_count, with_prompt);
	}
	if (getenv("HALO_H2_EVENT_LOG"))
	{
		for (k = 0; k < h2_hud_mp.count; k++)
		{
			struct h2_hud_mp_event const *event = &h2_hud_mp.events[k];
			char text[H2_HUD_MP_EVENT_TEXT];
			long i;

			for (i = 0; i < H2_HUD_MP_EVENT_TEXT - 1 && event->text[i]; i++)
				text[i] = event->text[i] < 0x80 ? (char)event->text[i] : '?';
			text[i] = 0;
			platform_log("h2 hud: event kind %d event %d audience %d: \"%s\" sound %s", event->kind, event->event, event->audience, text,
				event->sound[0] ? event->sound : "-");
		}
	}
}

/* (a level's load) the events read now: at the game's first event they cost
the game thread a second at the start of every game */
void h2_hud_mp_preload(void)
{
	h2_hud_mp_load();
}

static void h2_hud_mp_append(wchar_t *out, long out_count, wchar_t const *text)
{
	h2_hud_wide_append(out, out_count, text);
}

/* an event's line with its names put in */
/* (effect_team: the team #effect_team names - a flag's -; NONE the effect player's) */
/* (h2_hud_mp_event_named) the name an event's "#territory_name" stands for
while it is told ("" none) */
static wchar_t h2_hud_mp_extra_name[32];

static void h2_hud_mp_line(struct h2_hud_mp_event const *event, long cause_player_index, long effect_player_index, long effect_team, wchar_t *out, long out_count)
{
	wchar_t const *text = event->text;
	struct player_datum *cause = cause_player_index != NONE ? player_try_and_get(cause_player_index) : NULL;
	struct player_datum *effect = effect_player_index != NONE ? player_try_and_get(effect_player_index) : NULL;
	long length = 0;

	if (effect_team == NONE && effect)
		effect_team = effect->team_index;

	out[0] = 0;
	while (*text && length < out_count - 1)
	{
		wchar_t const *name = NULL;
		long skip = 0;

		if (!wcsncmp(text, L"#cause_player", 13))
			name = cause ? cause->name : L"", skip = 13;
		else if (!wcsncmp(text, L"#effect_player", 14))
			name = effect ? effect->name : L"", skip = 14;
		else if (!wcsncmp(text, L"#cause_team", 11))
			name = cause && cause->team_index >= 0 && cause->team_index < 2 ? h2_hud_mp.team_names[cause->team_index] : L"", skip = 11;
		else if (!wcsncmp(text, L"#effect_team", 12))
			name = effect_team >= 0 && effect_team < 2 ? h2_hud_mp.team_names[effect_team] : L"", skip = 12;
		else if (!wcsncmp(text, L"#territory_name", 15))
			name = h2_hud_mp_extra_name, skip = 15;
		if (name)
		{
			out[length] = 0;
			h2_hud_mp_append(out, out_count, name);
			length = (long)wcslen(out);
			text += skip;
		}
		else
			out[length++] = *text++;
	}
	out[length] = 0;
}

/* (motion_sensor.c) the slowest speed a player's unit moves at to show on
a Halo 2 level's motion sensor, world units a tick: 1.05 times the globals'
player information's sneak forward speed (a crouched walk shows not;
decompilation unknown_1a1c3e.cpp motion_sensor_object_moving: a biped or
vehicle with a player against the globals' 0x134 block's +0x3C times 1.05,
others against the HUD globals' minimum). FALSE: not a Halo 2 level or none
read */
int h2_hud_motion_sensor_player_speed(real *per_tick)
{
	h2_hud_load();
	if (!h2_hud.loaded)
		return FALSE;
	h2_hud_mp_load();
	if (h2_hud_mp.sneak_forward <= 0.0f)
		return FALSE;
	if (per_tick)
		*per_tick = 1.05f * h2_hud_mp.sneak_forward / (real)TICKS_PER_SECOND;
	return TRUE;
}

/* (game_engine.c) the line of one of Halo 2's events for one audience
(H2_HUD_AUDIENCE_...) - the game types' events this engine's own game
engines raise as their messages (a flag taken, a hill moved, the ball
picked up), each told to a player as this engine routes it; effect_team the
flag's team (NONE: the effect player's). FALSE: no such line (this engine's
text stands) */
int h2_hud_mp_event_line(int kind, int event, int audience, long cause_player_index, long effect_player_index, long effect_team,
	wchar_t *out, long out_count)
{
	long k;

	h2_hud_mp_load();
	for (k = 0; k < h2_hud_mp.count; k++)
	{
		struct h2_hud_mp_event const *e = &h2_hud_mp.events[k];

		if (e->kind == kind && e->event == event && e->audience == audience)
		{
			h2_hud_mp_line(e, cause_player_index, effect_player_index, effect_team, out, out_count);
			return TRUE;
		}
	}
	return FALSE;
}

/* (game_engine_multiplayer_sounds.c) the announcer's line of one of Halo
2's events, any audience's (NULL: none) */
char const *h2_hud_mp_event_sound(int kind, int event)
{
	long k;

	h2_hud_mp_load();
	for (k = 0; k < h2_hud_mp.count; k++)
	{
		struct h2_hud_mp_event const *e = &h2_hud_mp.events[k];

		if (e->kind == kind && e->event == event && e->sound[0])
			return e->sound;
	}
	return NULL;
}

/* (game_engine.c) a Halo 2 multiplayer event (kind H2_HUD_EVENTS_..., the
block's event number) told as Halo 2 tells it: each local player the line
of the most particular audience he is in - his own as the cause or effect
player, else his team's (a team game's, not as the player it names), else
everyone's -, and the announcer's line of the audience's event once if a
local player is in it. cause/effect NONE where the event has none. */
void h2_hud_mp_event(int kind, int event, long cause_player_index, long effect_player_index)
{
	short local;
	char const *sound = NULL;
	boolean teams = game_engine_has_teams();
	struct player_datum *cause = cause_player_index != NONE ? player_try_and_get(cause_player_index) : NULL;
	struct player_datum *effect = effect_player_index != NONE ? player_try_and_get(effect_player_index) : NULL;

	h2_hud_mp_load();
	if (!h2_hud_mp.count)
		return;
	for (local = 0; local < 4; local++)
	{
		long player_index = local_player_get_player_index(local);
		struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
		unsigned char audiences[5];
		long audience_count = 0, a, k;
		struct h2_hud_mp_event const *found = NULL;

		if (!player)
			continue;
		if (cause && player_index == cause_player_index)
			audiences[audience_count++] = H2_HUD_AUDIENCE_CAUSE_PLAYER;
		if (effect && player_index == effect_player_index)
			audiences[audience_count++] = H2_HUD_AUDIENCE_EFFECT_PLAYER;
		if (teams && cause && player_index != cause_player_index && player_index != effect_player_index && player->team_index == cause->team_index)
			audiences[audience_count++] = H2_HUD_AUDIENCE_CAUSE_TEAM;
		if (teams && effect && player_index != cause_player_index && player_index != effect_player_index && player->team_index == effect->team_index)
			audiences[audience_count++] = H2_HUD_AUDIENCE_EFFECT_TEAM;
		audiences[audience_count++] = H2_HUD_AUDIENCE_ALL;
		for (a = 0; a < audience_count && !found; a++)
		{
			for (k = 0; k < h2_hud_mp.count && !found; k++)
			{
				struct h2_hud_mp_event const *e = &h2_hud_mp.events[k];

				if (e->kind == kind && e->event == event && e->audience == audiences[a])
					found = e;
			}
		}
		if (!found)
			continue;
		if (found->text[0])
		{
			wchar_t line[256];

			h2_hud_mp_line(found, cause_player_index, effect_player_index, NONE, line, NUMBEROF(line));
			if (line[0])
				hud_print_message(local, line);
			if (getenv("HALO_H2_EVENT_LOG"))
			{
				char text[256];
				long i;

				for (i = 0; i < 255 && line[i]; i++)
					text[i] = line[i] < 0x80 ? (char)line[i] : '?';
				text[i] = 0;
				platform_log("h2 hud: event kind %d event %d to local player %d (audience %d): \"%s\"%s%s", kind, event, (int)local,
					(int)found->audience, text, found->sound[0] ? " sound " : "", found->sound[0] ? found->sound : "");
			}
		}
		if (found->sound[0] && !sound)
			sound = found->sound;
	}
	if (sound)
	{
		if (getenv("HALO_H2_EVENT_LOG"))
		{
			extern long h2_sound_vessel(char const *h2_name);

			platform_log("h2 hud: event kind %d event %d: announcer %s (%s)", kind, event, sound, h2_sound_vessel(sound) != NONE ? "in the level" : "NOT IN THE LEVEL");
		}
		game_engine_play_h2_announcement(sound);
	}
}

/* (h2_territories.c) the number of Halo 2's multiplayer events read for
the level (0: none - not a Halo 2 level, or no multiplayer globals) */
long h2_hud_mp_event_count(void)
{
	h2_hud_mp_load();
	return h2_hud_mp.count;
}

/* (h2_territories.c) a Halo 2 multiplayer event told as above, its
"#territory_name" the name given (the territories' events) */
void h2_hud_mp_event_named(int kind, int event, long cause_player_index, long effect_player_index, wchar_t const *name)
{
	long i;

	for (i = 0; name && name[i] && i < (long)NUMBEROF(h2_hud_mp_extra_name) - 1; i++)
		h2_hud_mp_extra_name[i] = name[i];
	h2_hud_mp_extra_name[i] = 0;
	h2_hud_mp_event(kind, event, cause_player_index, effect_player_index);
	h2_hud_mp_extra_name[0] = 0;
}

/* (game_engine.c) a Halo 2 level's kill told as Halo 2 tells it: the kill
event of its damage - a melee's, a vehicle's (a splatter), the sniper
rifle's or beam rifle's, a fall's, no one's -, a suicide's or a teammate's;
then one flavor event, the chain's (a double kill and up, within the
statistics' window), else the streak's (5, 10, 15, 20, then every 5), else
the dead player's broken spree (5 and up): unknown_157450.cpp. The dead
unit's last damage category tells the kill's kind (h2_weapons.c keeps Halo
2's category in each damage effect, the sniper rifle's and beam rifle's
damage the sniper's). */
void h2_hud_player_killed(long killing_player_index, long killing_object_index, long dead_player_index, int friendly_fire,
	short multiple_kills, short kills_in_a_row, short victim_kills_in_a_row)
{
	struct player_datum *dead = dead_player_index != NONE ? player_try_and_get(dead_player_index) : NULL;
	struct unit_datum *dead_unit = dead && dead->unit_index != NONE ? unit_try_and_get(dead->unit_index) : NULL;
	short marks = dead_unit ? dead_unit->unit.last_damage_category : 0;
	/* (units.c: the category in the low byte, bit 8 a stuck grenade's
	detonation, bit 9 a melee from behind) */
	short category = (short)(marks & 0xFF);
	int event = H2_HUD_GENERAL_KILL, flavor = NONE;

	(void)killing_object_index;
	if (!dead)
		return;
	if (killing_player_index == NONE)
	{
		event = category == H2_HUD_DAMAGE_FALLING ? H2_HUD_GENERAL_KILL_FALLING : H2_HUD_GENERAL_KILLED_BY_UNKNOWN;
		h2_hud_mp_event(H2_HUD_EVENTS_GENERAL, event, NONE, dead_player_index);
		return;
	}
	if (killing_player_index == dead_player_index)
	{
		h2_hud_mp_event(H2_HUD_EVENTS_GENERAL, H2_HUD_GENERAL_SUICIDE, dead_player_index, dead_player_index);
		return;
	}
	if (friendly_fire)
	{
		h2_hud_mp_event(H2_HUD_EVENTS_GENERAL, H2_HUD_GENERAL_KILL_TEAMMATE, killing_player_index, dead_player_index);
		return;
	}
	switch (category)
	{
		case H2_HUD_DAMAGE_MELEE: event = (marks & 0x200) ? H2_HUD_GENERAL_KILL_SNEAK_MELEE : H2_HUD_GENERAL_KILL_MELEE; break;
		case H2_HUD_DAMAGE_VEHICLE: event = H2_HUD_GENERAL_KILL_COLLISION; break;
		case H2_HUD_DAMAGE_SNIPER: event = H2_HUD_GENERAL_KILL_SNIPER; break;
		default: break;
	}
	if (marks & 0x100)
		event = H2_HUD_GENERAL_KILL_STICKY;
	h2_hud_mp_event(H2_HUD_EVENTS_GENERAL, event, killing_player_index, dead_player_index);
	if (multiple_kills >= 7)
		flavor = H2_HUD_FLAVOR_KILLIMANJARO;
	else if (multiple_kills == 6)
		flavor = H2_HUD_FLAVOR_KILLTROCITY;
	else if (multiple_kills == 5)
		flavor = H2_HUD_FLAVOR_KILL_FRENZY;
	else if (multiple_kills == 4)
		flavor = H2_HUD_FLAVOR_KILLTACULAR;
	else if (multiple_kills == 3)
		flavor = H2_HUD_FLAVOR_TRIPLE_KILL;
	else if (multiple_kills == 2)
		flavor = H2_HUD_FLAVOR_DOUBLE_KILL;
	else if (kills_in_a_row == 5)
		flavor = H2_HUD_FLAVOR_KILLING_SPREE;
	else if (kills_in_a_row == 10)
		flavor = H2_HUD_FLAVOR_RUNNING_RIOT;
	else if (kills_in_a_row == 15)
		flavor = H2_HUD_FLAVOR_15_IN_A_ROW;
	else if (kills_in_a_row == 20)
		flavor = H2_HUD_FLAVOR_20_IN_A_ROW;
	else if (kills_in_a_row > 20 && kills_in_a_row % 5 == 0)
		flavor = H2_HUD_FLAVOR_25_IN_A_ROW;
	else if (victim_kills_in_a_row >= 5)
		flavor = H2_HUD_FLAVOR_BROKE_SPREE;
	if (getenv("HALO_H2_EVENT_LOG"))
		platform_log("h2 hud: kill by %08lx of %08lx: category %d -> event %d, chain %d streak %d victim's %d -> flavor %d",
			(unsigned long)killing_player_index, (unsigned long)dead_player_index, (int)category, event, (int)multiple_kills,
			(int)kills_in_a_row, (int)victim_kills_in_a_row, flavor);
	if (flavor != NONE)
		h2_hud_mp_event(H2_HUD_EVENTS_FLAVOR, flavor, killing_player_index, dead_player_index);
}

/* the words' entry of a weapon definition (NONE: not Halo 2's) */
static long h2_hud_weapon_entry(long definition_index)
{
	static char const prefix[] = "h2\\weapons\\";
	char const *name = definition_index != NONE ? tag_get_name(definition_index) : NULL;
	long k;

	if (!name || strncmp(name, prefix, sizeof(prefix) - 1))
		return NONE;
	for (k = 0; k < H2_HUD_WEAPON_WORD_COUNT; k++)
	{
		if (!strcmp(name + sizeof(prefix) - 1, h2_hud_weapon_words[k].tag))
			return k;
	}
	return NONE;
}

/* (hud.c, hud_messaging.c) a Halo 2 weapon's name in its prompts ("Battle
Rifle"; NULL: not one of Halo 2's) */
wchar_t const *h2_hud_weapon_name(long definition_index)
{
	long k = h2_hud_weapon_entry(definition_index);

	if (k == NONE)
		return NULL;
	h2_hud_localization_read();
	return h2_hud_weapon_words[k].name;
}

/* (wide strings joined: no wide printf's conversions to trust) */
static void h2_hud_wide_append(wchar_t *out, long out_count, wchar_t const *text)
{
	long length = 0;

	while (length < out_count - 1 && out[length])
		length++;
	while (length < out_count - 1 && text && *text)
		out[length++] = *text++;
	out[length] = 0;
}

static void h2_hud_ascii_append(wchar_t *out, long out_count, char const *text)
{
	wchar_t wide[24];
	long i;

	for (i = 0; i < (long)NUMBEROF(wide) - 1 && text[i]; i++)
		wide[i] = (wchar_t)(unsigned char)text[i];
	wide[i] = 0;
	h2_hud_wide_append(out, out_count, wide);
}

/* (hud_messaging.c) a Halo 2 weapon's pickup line: "Picked up a Battle
Rifle", or with ammunition "Picked up 12 rounds for Battle Rifle"; FALSE if
it is not one of Halo 2's */
int h2_hud_weapon_pickup_text(long definition_index, long count, int ammunition, wchar_t *out, long out_count)
{
	long k = h2_hud_weapon_entry(definition_index);
	wchar_t const *pack;

	if (k == NONE || out_count < 2)
		return FALSE;
	/* (the map's own lines first: picked up, the singular and plural
	quantity messages - "Picked up \ue422 rounds for Battle Rifle") */
	if ((pack = h2_hud_weapon_message(definition_index, !ammunition ? H2_HUD_PROMPT_PICKED_UP :
		count == 1 ? H2_HUD_PROMPT_AMMO_SINGULAR : H2_HUD_PROMPT_AMMO_PLURAL)) != NULL)
	{
		h2_hud_prompt_plain(pack, count, out, out_count);
		if (getenv("HALO_H2_EVENT_LOG"))
		{
			char text[128];
			long i;

			for (i = 0; i < 127 && out[i]; i++)
				text[i] = out[i] < 0x80 ? (char)out[i] : '?';
			text[i] = 0;
			platform_log("h2 hud: pickup line from the map's pack: \"%s\"", text);
		}
		return TRUE;
	}
	h2_hud_localization_read();
	out[0] = 0;
	if (!ammunition)
		h2_hud_wide_append(out, out_count, h2_hud_weapon_words[k].picked_up);
	else
	{
		int plural = count != 1;
		char number[16];

		snprintf(number, sizeof(number), "%ld", count);
		if (h2_hud_weapon_words[k].ammo_after[plural][0])
		{
			h2_hud_wide_append(out, out_count, h2_hud_weapon_words[k].ammo_before[plural]);
			h2_hud_ascii_append(out, out_count, number);
			h2_hud_wide_append(out, out_count, h2_hud_weapon_words[k].ammo_after[plural]);
		}
		else
		{
			h2_hud_ascii_append(out, out_count, "Picked up ");
			h2_hud_ascii_append(out, out_count, number);
			h2_hud_ascii_append(out, out_count, plural ? " rounds for " : " round for ");
			h2_hud_wide_append(out, out_count, h2_hud_weapon_words[k].name);
		}
	}
	return TRUE;
}

/* (hud.c, hud_messaging.c) one of a Halo 2 weapon's prompts as the player's
own map has it (the weap tag's message string ids, the globals' language
pack's texts: "\ue47c \ue45a to swap for \ue112" - MCC's font draws
U+E47C as the hold verb, U+E448..U+E46A as the buttons, U+E1xx as the
weapon's picture, U+E422 a count): which H2_HUD_PROMPT_...; NULL where the
weapon is not Halo 2's or the map has no such line */
wchar_t const *h2_hud_weapon_message(long definition_index, int which)
{
	long k = h2_hud_weapon_entry(definition_index), w;

	if (k == NONE || which < 0 || which >= H2_HUD_MP_WEAPON_MESSAGES)
		return NULL;
	h2_hud_mp_load();
	for (w = 0; w < h2_hud_mp.weapon_count; w++)
	{
		if (!strcmp(h2_hud_mp.weapons[w].name, h2_hud_weapon_words[k].tag))
			return h2_hud_mp.weapons[w].text[which][0] ? h2_hud_mp.weapons[w].text[which] : NULL;
	}
	return NULL;
}

/* (hud_messaging.c) a weapon object's definition (NONE: no such weapon) */
long h2_hud_weapon_object_definition(long weapon_index)
{
	struct weapon_datum *weapon = weapon_index != NONE ? weapon_try_and_get(weapon_index) : NULL;

	return weapon ? weapon->definition_index : NONE;
}

/* a prompt's text with a count put in for U+E422 and its other glyphs
left out (the "Picked up ..." lines) */
static void h2_hud_prompt_plain(wchar_t const *text, long count, wchar_t *out, long out_count)
{
	long length = 0;

	for (; *text && length < out_count - 1; text++)
	{
		if (*text == 0xE422)
		{
			char number[16];
			long i;

			snprintf(number, sizeof(number), "%ld", count);
			for (i = 0; number[i] && length < out_count - 1; i++)
				out[length++] = (wchar_t)number[i];
		}
		else if (*text >= 0xE000 && *text <= 0xF8FF)
			continue;
		else
			out[length++] = *text;
	}
	out[length] = 0;
}

#pragma pop_macro("fopen")

/* a Halo 2 weapon's icon: its HUD's panel (the single weapon's, else the
right hand's) - its foreground image, the outline and the weapon's
picture; NULL if it has none */
static struct h2_hud_widget const *h2_hud_weapon_panel(long definition_index)
{
	long h, w;

	h2_hud_load();
	if (!h2_hud.loaded || definition_index == NONE)
		return NULL;
	for (h = 0; h < h2_hud.hud_count; h++)
	{
		struct h2_hud_loaded const *hud = &h2_hud.huds[h];

		if (hud->owner_tag != definition_index)
			continue;
		for (w = hud->first; w < hud->first + hud->count; w++)
		{
			struct h2_hud_widget const *widget = &h2_hud.widgets[w];
			char const *name = widget->file.name;

			if (!strncmp(name, "weapon_background", 17) && !strstr(name, "left") && !strstr(name, "out") &&
				widget->bitmap_fg_tag != NONE && widget->file.canvas_width && widget->file.canvas_height)
			{
				return widget;
			}
		}
	}
	return NULL;
}

/* the part of a panel's foreground that is the weapon's picture (of the
panel's own size: the count's box, its left 0.4, and the outline's edges
out - the battle rifle's panel; HALO_H2_ICON_PART="u0 v0 u1 v1" another, a
test's) */
static void h2_hud_weapon_icon_part(real part[4])
{
	char const *setting = getenv("HALO_H2_ICON_PART");

	part[0] = 0.42f, part[1] = 0.06f, part[2] = 0.975f, part[3] = 0.94f;
	if (setting)
		sscanf(setting, "%f %f %f %f", &part[0], &part[1], &part[2], &part[3]);
}

int h2_hud_weapon_has_icon(long definition_index)
{
	return h2_hud_weapon_panel(definition_index) != NULL;
}

/* (hud_messaging.c) a Halo 2 weapon's icon in its prompts (Halo 2's own
prompts show a glyph of its font, the same picture) drawn alpha blended, its
bottom left at x, bottom, height pixels high: its right edge (x if none is
drawn) */
real h2_hud_weapon_icon_draw(long definition_index, real x, real bottom, real height, pixel32 color)
{
	struct h2_hud_widget const *widget = h2_hud_weapon_panel(definition_index);
	struct h2_hud_file_widget const *f;
	struct bitmap_data *bitmap;
	real part[4], u0, v0, u1, v1, wide;

	if (!widget || !(bitmap = h2_hud_bitmap(widget->bitmap_fg_tag)))
		return x;
	f = &widget->file;
	h2_hud_weapon_icon_part(part);
	/* (a taller panel - the plasma rifle's, the sword's, 32 high: its
	heat or battery bar under the panel's 19 - its picture in its top) */
	if (f->height > 24 && !getenv("HALO_H2_ICON_PART"))
	{
		part[1] *= 19.0f / (real)f->height;
		part[3] *= 19.0f / (real)f->height;
	}
	u0 = part[0] * (real)f->width / (real)f->canvas_width;
	u1 = part[2] * (real)f->width / (real)f->canvas_width;
	v0 = part[1] * (real)f->height / (real)f->canvas_height;
	v1 = part[3] * (real)f->height / (real)f->canvas_height;
	if (part[3] <= part[1] || part[2] <= part[0])
		return x;
	wide = height * ((part[2] - part[0]) * (real)f->width) / ((part[3] - part[1]) * (real)f->height);
	h2_hud_quad_blend(bitmap, x, bottom - height, h2_hud_floor(x + wide + 0.5f), bottom, u0, v0, u1, v1, color, 0);
	return h2_hud_floor(x + wide + 0.5f);
}


/* the HUDs of the unit (unit TRUE) or of the weapon: a weapon's state its
own, the primary weapon's if it is the unit's right weapon, the
secondary's if its left */
/* (the holstered weapon's HUD being drawn: weapon state "backpack") */
static boolean h2_hud_backpack;

/* whether a HUD is the player's unit's: the Arbiter's (the file's "unit:elite")
while the player is an Elite and the level has it, else the Chief's ("") */
static boolean h2_hud_unit_hud(struct h2_hud_loaded const *hud)
{
	static long checked_count = -1;
	static boolean have_elite;
	long h;

	if (checked_count != h2_hud.hud_count)
	{
		checked_count = h2_hud.hud_count;
		have_elite = FALSE;
		for (h = 0; h < h2_hud.hud_count; h++)
			have_elite = have_elite || !strcmp(h2_hud.huds[h].owner, H2_HUD_OWNER_ELITE);
	}
	if (h2_hud_elite && have_elite)
		return !strcmp(hud->owner, H2_HUD_OWNER_ELITE);
	return !hud->owner[0];
}

static void h2_hud_draw_huds(short local_player_index, struct h2_hud_state *state, long weapon_index, boolean unit, char const *vehicle_owner)
{
	long weapon_definition_index = NONE;
	long h, w;

	if (!unit)
	{
		struct weapon_datum *weapon = weapon_index != NONE ? weapon_try_and_get(weapon_index) : NULL;
		struct weapon_definition *definition;

		if (!weapon && !vehicle_owner)
			return;
		state->weapon_hud = TRUE;
		if (!weapon)
			goto hud_list;
		definition = weapon_definition_get(weapon->definition_index);
		weapon_definition_index = weapon->definition_index;
		state->weapon = (unsigned short)(h2_hud_backpack ? 1 << 2 : weapon->object.parent_object_index != NONE &&
			dual_wield_left_weapon(weapon->object.parent_object_index) == weapon_index ? 1 << 1 : 1 << 0);
		state->clip = weapon->weapon.magazines[0].rounds_loaded;
		state->total = weapon->weapon.magazines[0].rounds_total;
		state->clip_maximum = 0;
		if (definition->weapon.magazines.count > 0)
			state->clip_maximum = TAG_BLOCK_GET_ELEMENT(&definition->weapon.magazines, 0, struct weapon_magazine_definition)->rounds_loaded_maximum;
		state->heat = weapon->weapon.heat;
		state->battery = 1.0f - weapon->weapon.age;
		if (TEST_FLAG(weapon->weapon.flags, 0))
			state->weapon |= 1 << 6;
		/* (a heat or battery weapon - the plasma rifle - has no rounds to run out of) */
		if (!state->clip && !state->total && definition->weapon.magazines.count > 0 && definition->weapon.heat_loss_per_second <= 0.0f)
			state->weapon |= 1 << 7;
		/* the rocket launcher's lock (h2_projectiles.c) */
		{
			boolean lockable, locking, locked;
			long target_index;
			struct object_datum *target;

			state->weapon &= ~(0x100 | 0x200 | 0x400);
			state->lock_on_screen = FALSE;
			if (h2_lock_on_hud(weapon_index, &lockable, &locking, &locked, &state->lock_fraction, &target_index))
			{
				state->weapon |= (lockable ? 0x100 : 0) | (locking ? 0x200 : 0) | (locked ? 0x400 : 0);
				if (target_index != NONE && (target = object_try_and_get_and_verify_type(target_index, _object_mask_all)) != NULL)
				{
					real_point2d screen;

					if (render_camera_world_to_screen(&render.camera, &render.frustum, &target->object.bounding_sphere_center, &screen))
					{
						state->lock_on_screen = TRUE;
						state->lock_point[0] = screen.x - (real)render.camera.viewport_bounds.x0;
						state->lock_point[1] = screen.y - (real)render.camera.viewport_bounds.y0;
					}
				}
				if (getenv("HALO_H2_LOCK_LOG") && (lockable || locking || locked) && (game_time_get() % 10) == 0)
					platform_log("h2 hud: lock %s%s%s %.2f, target %08lx %s (%.0f, %.0f)", lockable ? "lockable" : "", locking ? "locking" : "",
						locked ? "locked" : "", state->lock_fraction, (unsigned long)target_index, state->lock_on_screen ? "on screen" : "off screen",
						state->lock_point[0], state->lock_point[1]);
			}
		}
	}
hud_list:
	for (h = 0; h < h2_hud.hud_count; h++)
	{
		struct h2_hud_loaded const *hud = &h2_hud.huds[h];

		if (vehicle_owner)
		{
			/* (a vehicle's gun: its HUD by the vehicle's kind) */
			if (unit || strcmp(hud->owner, vehicle_owner))
				continue;
		}
		else if (unit ? !h2_hud_unit_hud(hud) : (!hud->owner[0] || hud->owner_tag == NONE || hud->owner_tag != weapon_definition_index ||
			!strcmp(hud->owner, H2_HUD_OWNER_ELITE)))
			continue;
		if (hud->owner[0] && strcmp(hud->owner, H2_HUD_OWNER_ELITE))
		{
			state->weapon &= ~((1 << 4) | (1 << 5));
			if (hud->low_clip > 0 && state->clip < hud->low_clip)
				state->weapon |= 1 << 4;
			if (hud->low_total > 0 && state->total < hud->low_total)
				state->weapon |= 1 << 5;
		}
		for (w = hud->first; w < hud->first + hud->count; w++)
		{
			struct h2_hud_widget const *widget = &h2_hud.widgets[w];

			if (!h2_hud_shown(&widget->file, state))
			{
				/* (tests: HALO_H2_HUD_LOG=2 the widgets not shown too, their draw flags) */
				if (getenv("HALO_H2_HUD_LOG") && !strcmp(getenv("HALO_H2_HUD_LOG"), "2") && (game_time_get() % 30) == 0)
				{
					unsigned char const *d = widget->file.draw_flags;

					platform_log("h2 hud: %s not shown (do %02x%02x %02x%02x %02x%02x %02x%02x, not %02x%02x %02x%02x %02x%02x %02x%02x; unit %04x extra %04x weapon %04x engine %04x)",
						widget->file.name, d[1], d[0], d[3], d[2], d[5], d[4], d[7], d[6], d[9], d[8], d[11], d[10], d[13], d[12], d[15], d[14],
						state->unit, state->extra, state->weapon, state->engine);
				}
				continue;
			}
			/* (the holstered weapon's HUD: its backpack widgets alone - its
			icon beside the weapon panel) */
			if (h2_hud_backpack && !((widget->file.draw_flags[4] | (widget->file.draw_flags[5] << 8)) & (1 << 2)))
				continue;
			if (getenv("HALO_H2_HUD_LOG") && (game_time_get() % 10) == 0)
				platform_log("h2 hud: %s drawn (unit %04x weapon %04x; type %d shader %d inputs %02x %02x %02x %02x special %ld)", widget->file.name,
					state->unit, state->weapon, widget->file.type, widget->file.shader, widget->file.inputs[0], widget->file.inputs[1],
					widget->file.inputs[2], widget->file.inputs[3], (long)widget->file.special);
			if (widget->file.type == H2_HUD_WIDGET_TEXT)
				h2_hud_draw_text(local_player_index, widget, state);
			else if (widget->file.type == H2_HUD_WIDGET_EMBLEM)
				h2_hud_draw_emblem(local_player_index, &widget->file, state);
			else
			{
				h2_hud_draw_bitmap(local_player_index, widget, state);
				/* this engine's blips on Halo 2's motion sensor (its middle;
				a campaign level's enemies and allies too) */
				if (h2_hud_unit_hud(hud) && !strcmp(widget->file.name, "motion_tracker_background"))
				{
					point2d point;
					real scale;

					h2_hud_point(local_player_index, &widget->file, &point, &scale);
					if (widget->file.unused[0] && widget->file.unused[0] < 4)
						scale *= (real)(1 << widget->file.unused[0]);
					point.x = (short)(point.x + 0.5f * widget->file.width * scale);
					point.y = (short)(point.y + 0.5f * widget->file.height * scale);
					h2_hud_motion_ping(&point, 0.5f * widget->file.width * scale);
					motion_sensor_draw_screen(local_player_index, local_player_count() > 1, &point);
				}
			}
		}
	}
}

short h2_hud_waypoint_count(void)
{
	h2_hud_load();
	return h2_hud.loaded ? h2_hud.waypoint_count : 0;
}

short h2_hud_waypoint_index(char const *name)
{
	short i;

	h2_hud_load();
	for (i = 0; name && h2_hud.loaded && i < h2_hud.waypoint_count; i++)
	{
		if (!strcasecmp(h2_hud.waypoints[i].name, name))
			return i;
	}
	return NONE;
}

/* (game_engine_render_nav_points) the next nav points' colour (0xRRGGBB:
a Territories territory's, its holder's; NONE the waypoint's own) */
static long h2_hud_nav_tint = NONE;

void h2_hud_nav_point_tint(long rgb)
{
	h2_hud_nav_tint = rgb;
}

long h2_hud_nav_point_tint_get(void)
{
	return h2_hud_nav_tint;
}

/* (h2_hud_ally_nav_points) the ally whose emblem the next nav point is */
static long h2_hud_nav_emblem_player = NONE;

long h2_hud_nav_emblem_player_get(void)
{
	return h2_hud_nav_emblem_player;
}

int h2_hud_nav_point_draw(short local_player_index, short nav_index, short screen_type, real x, real y, real theta,
	real distance)
{
	struct h2_hud_file_waypoint const *waypoint;
	struct bitmap_data *bitmap;
	struct dynamic_screen_vertex vertices[4];
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	real height = (real)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);
	real scale, half_wide, half_high, c, s, opacity;
	unsigned long r, g, b, a;
	long tag;
	short k, image = screen_type;

	(void)local_player_index;
	(void)distance;
	h2_hud_load();
	if (!h2_hud.loaded || !h2_hud.waypoint_count)
		return FALSE;
	/* (an ally: Halo 2's HUD globals' "ally" waypoint, waypoint_emblem over
	emblem_dummy: its emblem, on screen or hidden) */
	if (h2_hud_nav_emblem_player != NONE)
	{
		struct h2_emblem emblem;
		real half = 0.5f * H2_HUD_ALLY_EMBLEM_SIZE * h2_hud_view_scale(height);

		h2_emblem_player(h2_hud_nav_emblem_player, &emblem);
		if ((bitmap = h2_emblem_bitmap(&emblem, H2_EMBLEM_WHOLE)) != NULL)
			h2_hud_quad(bitmap, x - half, y - half, x + half, y + half, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFUL);
		return TRUE;
	}
	/* (a navpoint not of the file's: its first, "default") */
	if (nav_index < 0 || nav_index >= h2_hud.waypoint_count)
		nav_index = 0;
	waypoint = &h2_hud.waypoints[nav_index];
	if (image < 0 || image >= H2_HUD_WAYPOINT_IMAGES)
		image = 0;
	/* (occluded with no image of its own: the on screen one) */
	if (h2_hud.waypoint_tags[nav_index][image] == NONE)
		image = 0;
	tag = h2_hud.waypoint_tags[nav_index][image];
	if (tag == NONE || !waypoint->canvas_width[image] || !waypoint->canvas_height[image] ||
		!(bitmap = h2_hud_bitmap(tag)))
	{
		/* (drawn as nothing: Halo 2's HUD has it, this engine's arrow is not
		drawn over it) */
		return TRUE;
	}
	/* (Halo 2's HUD size: its 640x480 layout over the view's height, as
	the widgets; an image halved shift times drawn as many times twice) */
	scale = h2_hud_view_scale(height) * (real)(1 << waypoint->shift[image]);
	half_wide = 0.5f * (real)waypoint->width[image] * scale;
	half_high = 0.5f * (real)waypoint->height[image] * scale;
	c = (real)cos(theta);
	s = (real)sin(theta);
	opacity = waypoint->opacity > 0.0f && waypoint->opacity <= 1.0f ? waypoint->opacity : 1.0f;
	r = (waypoint->color >> 16) & 255, g = (waypoint->color >> 8) & 255, b = waypoint->color & 255;
	if (h2_hud_nav_tint != NONE)
		r = ((unsigned long)h2_hud_nav_tint >> 16) & 255, g = ((unsigned long)h2_hud_nav_tint >> 8) & 255, b = (unsigned long)h2_hud_nav_tint & 255;
	a = (unsigned long)(255.0f * opacity + 0.5f);
	for (k = 0; k < 4; k++)
	{
		boolean right = ((k + 1) & 2) != 0, bottom = k > 1;
		real dx = right ? half_wide : -half_wide, dy = bottom ? half_high : -half_high;

		vertices[k].position.x = x + dx * c - dy * s;
		vertices[k].position.y = y + dx * s + dy * c;
		vertices[k].texture_coordinates.x = right ? (real)waypoint->width[image] / (real)waypoint->canvas_width[image] : 0.0f;
		vertices[k].texture_coordinates.y = bottom ? (real)waypoint->height[image] / (real)waypoint->canvas_height[image] : 0.0f;
		vertices[k].color = (a << 24) | (r << 16) | (g << 8) | b;
	}
	csmemset(&parameters, 0, sizeof(parameters));
	parameters.map_texture_scale[0].i = parameters.map_texture_scale[0].j = 1.0f;
	parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
	parameters.framebuffer_blend_function = 0;
	parameters.map[0] = bitmap;
	rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
	if (getenv("HALO_H2_HUD_LOG") && (game_time_get() % 30) == 0)
		platform_log("h2 hud: nav point '%s' %s at %.0f %.0f turned %.2f, %.1f away", waypoint->name,
			image == 0 ? "on screen" : image == 1 ? "off screen" : "occluded", x, y, theta, distance);
	return TRUE;
}

/* (hud_render_nav_points) a team game's living allies' emblems over their
heads (Halo 2's HUD globals: "ally", shown on screen and occluded, never off
it); FALSE-free: nothing on another level */
void h2_hud_ally_nav_points(short local_player_index)
{
	long player_index = local_player_get_player_index(local_player_index), unit_index;
	struct data_iterator iterator;
	struct player_datum *player, *own;
	real_point3d head;

	h2_hud_load();
	if (!h2_hud.loaded || !game_engine_running() || !game_engine_has_teams() || player_index == NONE ||
		!(own = player_try_and_get(player_index)))
	{
		return;
	}
	unit_index = *(volatile long *)&own->unit_index;
	if (unit_index == NONE || !object_try_and_get_and_verify_type(unit_index, _object_mask_unit))
		return;
	unit_get_head_position(unit_index, &head);
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long ally_unit = *(volatile long *)&player->unit_index;
		real_point3d position;

		if (iterator.datum_index == player_index || player->team_index != own->team_index || ally_unit == NONE ||
			!object_try_and_get_and_verify_type(ally_unit, _object_mask_unit))
		{
			continue;
		}
		unit_get_head_position(ally_unit, &position);
		position.z += 0.2f;
		{
			static int said;

			if (!said++)
				platform_log("h2 hud: ally emblems drawn");
		}
		h2_hud_nav_emblem_player = iterator.datum_index;
		custom_render_nav_point(local_player_index, &position, 0,
			hud_get_nav_point_render_type(local_player_index, &head, &position, NONE));
		h2_hud_nav_emblem_player = NONE;
	}
}

int h2_hud_draw(
	short local_player_index,
	long unit_index)
{
	struct unit_datum *unit;
	struct h2_hud_state state;
	long weapon_index = NONE, left_weapon_index;

	h2_hud_load();
	if (!h2_hud.loaded || unit_index == NONE || !(unit = unit_try_and_get(unit_index)))
		return FALSE;
	/* the player's state */
	csmemset(&state, 0, sizeof(state));
	h2_hud_tint_for(unit_index);
	state.unit = (1 << 0) | (1 << 4) | (1 << 11) | (1 << 12);
	/* (Halo 2's unit flag 13, "dervish": the Arbiter's - his camouflage meter) */
	if (h2_hud_elite)
		state.unit |= 1 << 13;
	state.unit |= unit->unit.current_zoom_level == NONE ? 1 << 6 : unit->unit.current_zoom_level == 0 ? 1 << 7 : 1 << 8;
	state.frag_count = unit->unit.grenade_counts[0];
	state.plasma_count = unit->unit.grenade_counts[1];
	state.unit |= unit->unit.current_grenade_index == 0 ? 1 << 2 : unit->unit.current_grenade_index == 1 ? 1 << 3 : 1 << 1;
	state.shield = unit->object.shield_vitality;
	if (unit->object.recent_shield_damage > 0.0f || unit->object.recent_body_damage > 0.0f)
		state.shield_flash = 1;
	else if (TEST_FLAG(unit->object.damage_flags, _object_shield_charging_bit) ||
		(unit->object.shield_stun_ticks == 0 && unit->object.shield_vitality < 1.0f))
		state.shield_flash = 2;
	state.body = unit->object.body_vitality;
	state.camo = 1.0f - PIN(unit->unit.active_camouflage, 0.0f, 1.0f);
	/* (the Arbiter's camouflage meter as it drains and recharges, units.c) */
	{
		real h2_arbiter_camo_energy(long player_index);
		real energy = unit->unit.player_index != NONE ? h2_arbiter_camo_energy(unit->unit.player_index) : -1.0f;

		if (energy >= 0.0f)
			state.camo = energy;
	}
	/* (tests: HALO_H2_VITALITY_LOG, the player's shield and body each 2 s) */
	if (getenv("HALO_H2_VITALITY_LOG") && (game_time_get() % 60) == 0)
		platform_log("h2 hud: tick %ld shield %.3f body %.3f (stun %d)", (long)game_time_get(), unit->object.shield_vitality,
			unit->object.body_vitality, (int)unit->object.shield_stun_ticks);
	state.target = player_control_get_target_object_index(local_player_index) != NONE &&
		player_control_get_autoaim_level(local_player_index) > 0.0f;
	/* (Halo 2's engine flags: 0 the campaign alone, 1 the campaign in
	co-op, 2 a free for all, 3 a team game) */
	state.engine = game_engine_running() ? (game_engine_has_teams() ? 1 << 3 : 1 << 2) : game_is_cooperative() ? 1 << 1 : 1 << 0;
	/* (6 a timed game - the scoreboard's time left -, 7 an untimed one:
	Halo 2's display->timer, the engine options' time limit) */
	state.time_left = game_engine_running() ? game_engine_time_left_seconds() : NONE;
	state.engine |= state.time_left != NONE ? 1 << 6 : 1 << 7;
	/* (the scoreboard's: the player's or its team's score, the best other
	player's or team's) */
	if (game_engine_running() && game_engine)
	{
		long player_index = unit->unit.player_index;
		struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
		boolean teams = game_engine_has_teams();
		struct data_iterator iterator;
		struct player_datum *other;

		state.score_limit = global_variant.universal_variant.score_to_win;
		state.other_score = 0;
		state.player_index = player_index;
		state.rival_player_index = NONE;
		state.leading = TRUE;
		h2_hud_mp_load();   /* (the team colours) */
		if (player)
		{
			state.score = teams ? game_engine_get_team_score(player->team_index) :
				game_engine->get_player_score(player_index, _get_score_individual);
			data_iterator_new(&iterator, player_data);
			while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			{
				long other_score;

				if (iterator.datum_index == player_index || (teams && other->team_index == player->team_index))
					continue;
				other_score = teams ? game_engine_get_team_score(other->team_index) :
					game_engine->get_player_score(iterator.datum_index, _get_score_individual);
				if (!state.other_score_valid || other_score > state.other_score)
				{
					state.other_score = other_score;
					state.rival_player_index = iterator.datum_index;
				}
				state.other_score_valid = TRUE;
			}
		}
		/* the rows' colours: a free for all's the players' own (the armour's
		primary, unknown_07f720.cpp function_7f790's colours[0]), a team
		game's the multiplayer globals' team colours (function_7f720) */
		{
			extern union real_rgb_color *game_engine_player_get_change_color(union real_rgb_color *change_color, long player_index);
			real_rgb_color rgb;
			long k;

			for (k = 0; k < 2; k++)
			{
				long index = k == 0 ? player_index : state.rival_player_index;
				real *out = k == 0 ? state.self_rgb : state.rival_rgb;
				struct player_datum *p = index != NONE ? player_try_and_get(index) : NULL;

				out[0] = out[1] = out[2] = 0.6f;
				if (!p)
					continue;
				if (teams && h2_hud_mp_team_rgb(p->team_index, out))
					continue;
				game_engine_player_get_change_color(&rgb, index);
				out[0] = rgb.red, out[1] = rgb.green, out[2] = rgb.blue;
			}
		}
		/* (King's and Territories' scores are ticks and their score to win
		minutes: both in seconds, as Halo 2 counts them) */
		if (global_variant.game_engine_index == game_engine_king || global_variant.game_engine_index == game_engine_oddball)
		{
			state.score /= TICKS_PER_SECOND;
			state.other_score /= TICKS_PER_SECOND;
			state.score_limit *= 60;
			state.time_scores = TRUE;
		}
		/* (leading while no one has more: function_159460's; a tie keeps
		this player's row on top) */
		state.leading = state.score >= state.other_score;
		state.engine |= state.leading ? 1 << 4 : 1 << 5;
		state.engine |= state.other_score_valid ? 1 << 8 : 1 << 9;
		/* a Territories game's bars and the player's charge (Halo 2's
		s_player_score_display, unknown_157450.cpp function_15f3a0: each
		territory's bar in its holder's - or, neutral, its contender's - team
		colour, function_7f720's; a free for all's one default colour,
		g_468714's, not read: the HUD's own mark colour here; a player taking
		a territory has a charge, and the "player is arming bomb" flag's meter
		shows it - there is no Assault here to arm a bomb in) */
		{
			long holders[8], contenders[8], k;
			real fractions[8], charge = 0.0f;

			state.zone_count = h2_territories_hud(player_index, holders, contenders, fractions, 8, &charge);
			for (k = 0; k < state.zone_count; k++)
			{
				long owner = holders[k] != NONE ? holders[k] : contenders[k];
				struct player_datum *p = owner != NONE ? player_try_and_get(owner) : NULL;

				state.zone_fraction[k] = fractions[k];
				state.zone_rgb[k][0] = state.zone_rgb[k][1] = state.zone_rgb[k][2] = 0.0f;
				if (!p)
					continue;
				if (!teams || !h2_hud_mp_team_rgb(p->team_index, state.zone_rgb[k]))
					memcpy(state.zone_rgb[k], h2_hud_tint[1], sizeof(state.zone_rgb[k]));
			}
			if (charge > 0.0f)
			{
				state.charge = TRUE;
				state.charge_fraction = charge;
				state.engine |= 1 << 10;
			}
		}
	}
	if (unit->unit.current_weapon_index != NONE)
		weapon_index = unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
	/* (dual wielding, units/dual_wield.h: the right weapon's HUD as the
	primary weapon's, the left's as the secondary's) */
	left_weapon_index = dual_wield_left_weapon(unit_index);
	if (left_weapon_index != NONE)
		state.unit = (unsigned short)((state.unit & ~(1 << 4)) | (1 << 5));
	/* the unit's HUD, then its weapons' */
	state.weapon_hud = FALSE;
	h2_hud_draw_huds(local_player_index, &state, NONE, TRUE, NULL);
	h2_hud_draw_huds(local_player_index, &state, weapon_index, FALSE, NULL);
	if (left_weapon_index != NONE)
		h2_hud_draw_huds(local_player_index, &state, left_weapon_index, FALSE, NULL);
	/* the holstered weapon's icon (its HUD's backpack widget: MCC's small
	icon beside the weapon panel) - while dual wielding too, the weapon on
	the back (its "draw if backpack weapon" alone: MCC's frames, the SMG's
	icon beside a pair of needlers') */
	{
		short slot;

		for (slot = 0; slot < (short)NUMBEROF(unit->unit.weapon_object_indices); slot++)
		{
			long holstered = unit->unit.weapon_object_indices[slot];

			if (holstered != NONE && holstered != weapon_index && holstered != left_weapon_index)
			{
				h2_hud_backpack = TRUE;
				h2_hud_draw_huds(local_player_index, &state, holstered, FALSE, NULL);
				h2_hud_backpack = FALSE;
				break;
			}
		}
	}
	/* the gun of the vehicle this unit aims (a Ghost's driver, a Warthog's
	gunner, a turret's): Halo 2's vehicle HUD, its reticle and meters */
	{
		long aiming_index = player_control_get_aiming_unit_index(local_player_index);
		struct unit_datum *aiming = aiming_index != NONE && aiming_index != unit_index ? unit_try_and_get(aiming_index) : NULL;

		if (aiming)
		{
			char const *name = tag_get_name(aiming->definition_index);
			char const *owner = !name ? NULL : strstr(name, "ghost") ? "vehicle:ghost" : strstr(name, "gauss") ? "vehicle:gauss" :
				strstr(name, "warthog") ? "vehicle:chaingun" : strstr(name, "banshee") ? "vehicle:banshee" :
				strstr(name, "scorpion") ? "vehicle:scorpion" : strstr(name, "wraith") ? "vehicle:wraith" :
				strstr(name, "c_turret") ? "vehicle:c_turret" : strstr(name, "creep") ? "vehicle:creep" :
				strstr(name, "turret") ? "vehicle:turret" : NULL;
			long vehicle_weapon = aiming->unit.current_weapon_index != NONE ? aiming->unit.weapon_object_indices[aiming->unit.current_weapon_index] : NONE;

			if (getenv("HALO_H2_HUD_LOG") && (game_time_get() % 60) == 0)
				platform_log("h2 hud: aiming %s -> %s (weapon %ld)", name ? name : "?", owner ? owner : "none", vehicle_weapon);
			if (owner)
			{
				/* (the vehicle's own weapon's state where it has one) */
				if (vehicle_weapon == NONE)
					state.weapon = 1 << 0;
				h2_hud_draw_huds(local_player_index, &state, vehicle_weapon, FALSE, owner);
			}
		}
	}
	return TRUE;
}


/* (game_engine.c, a Halo 2 level's) the score held open as Halo 2 lays it
out: "Winning with N of M" (or the leader) over a list - a header bar,
Name and Score, a row each, the player's own brighter - in the HUD's
colour, at Halo 2's place (the owner's MCC frames: its left 0.48 of the
view's height left of the middle, 0.8 of it wide); TRUE if drawn */
int h2_hud_draw_scores(long player_index, real alpha)
{
	struct { long player; long score; long place; } rows[16];
	long ranked[16], ranked_places[16];
	boolean ranked_tied[16];
	long count = 0, ranked_count, i, k, white, limit;
	/* (the columns, of the view's height: the place, the emblem, the name,
	the score - the owner's MCC frame: a place column 0.05 wide, the emblem
	beside the name, the score's column 0.11 wide after a rule) */
	real place_width, emblem_size, score_width;
	long game_engine_in_game_ranking(long *player_indices, long *places, boolean *tied, long maximum);
	struct data_iterator iterator;
	struct player_datum *player;
	rectangle2d window = render.camera.window_bounds;
	real height = (real)(window.y1 - window.y0), x0, x1, y, row;
	real cx = (real)(window.x0 - render.camera.viewport_bounds.x0) + 0.5f * (real)(window.x1 - window.x0);
	real top = (real)(window.y0 - render.camera.viewport_bounds.y0);
	struct bitmap_data *bitmap;
	real_argb_color color;
	char title[96];
	wchar_t wide[96];
	rectangle2d bounds;
	boolean teams = game_engine_has_teams();

	h2_hud_load();
	draw_string_set_tab_stops(NULL, 0);
	if (!h2_hud.loaded || !game_engine || (white = h2_hud_tag("h2\\hud\\white")) == NONE ||
		!(bitmap = h2_hud_bitmap(white)))
	{
		return FALSE;
	}
	h2_hud_tint_for(player_index != NONE && player_try_and_get(player_index) ? player_get(player_index)->unit_index : NONE);
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && count < (long)NUMBEROF(rows))
	{
		if (player->quit_out_of_game)
			continue;
		rows[count].player = iterator.datum_index;
		rows[count].score = teams ? game_engine_get_team_score(player->team_index) :
			game_engine->get_player_score(iterator.datum_index, _get_score_individual);
		rows[count].place = NONE;
		count++;
	}
	/* (each row's place: the game's own in-game ranking, a tie sharing it) */
	ranked_count = game_engine_in_game_ranking(ranked, ranked_places, ranked_tied, 16);
	for (i = 0; i < count; i++)
		for (k = 0; k < ranked_count; k++)
			if (ranked[k] == rows[i].player)
				rows[i].place = ranked_places[k];
	/* (highest first) */
	for (i = 1; i < count; i++)
	{
		for (k = i; k > 0 && rows[k].score > rows[k - 1].score; k--)
		{
			long p = rows[k].player, sc = rows[k].score, pl = rows[k].place;

			rows[k].player = rows[k - 1].player, rows[k].score = rows[k - 1].score, rows[k].place = rows[k - 1].place;
			rows[k - 1].player = p, rows[k - 1].score = sc, rows[k - 1].place = pl;
		}
	}
	x0 = cx - 0.48f * height;
	x1 = cx + 0.32f * height;
	row = 0.036f * height;
	place_width = 0.05f * height;
	emblem_size = row - 4.0f;
	score_width = 0.11f * height;
	y = top + 0.05f * height;
	limit = global_variant.universal_variant.score_to_win;
	if (count > 0)
	{
		struct player_datum *leader = player_get(rows[0].player);
		boolean self = rows[0].player == player_index;

		if (self)
			snprintf(title, sizeof(title), "Winning with %ld of %ld", rows[0].score, limit);
		else
		{
			long n;

			for (n = 0; n < 31 && leader->name[n]; n++)
				title[n] = leader->name[n] < 0x80 ? (char)leader->name[n] : '?';
			snprintf(title + n, sizeof(title) - n, " leads with %ld of %ld", rows[0].score, limit);
		}
		for (i = 0; title[i] && i < (long)NUMBEROF(wide) - 1; i++)
			wide[i] = (wchar_t)title[i];
		wide[i] = 0;
		color.alpha = alpha;
		color.red = h2_hud_tint[1][0], color.green = h2_hud_tint[1][1], color.blue = h2_hud_tint[1][2];
		bounds.x0 = (short)x0, bounds.x1 = (short)x1, bounds.y0 = (short)(y - 2.0f), bounds.y1 = (short)(y + row + 24.0f);
		draw_string_set_draw_mode(hud_get_font_index(), NONE, H2_HUD_JUSTIFY_LEFT, 0, &color);
		rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, wide);
	}
	y += row;
	/* the header: a bar, the place's column, Name, a rule, Score */
	h2_hud_quad(bitmap, x0, y, x1, y + row, 0.0f, 0.0f, 0.0f, 0.0f, ((unsigned long)(alpha * 0xD0) << 24) | 0x080808UL);
	h2_hud_quad(bitmap, x0 + place_width - 1.0f, y, x0 + place_width + 1.0f, y + row, 0.0f, 0.0f, 0.0f, 0.0f,
		((unsigned long)(alpha * 0xA0) << 24) | 0x303030UL);
	h2_hud_quad(bitmap, x1 - score_width - 1.0f, y, x1 - score_width + 1.0f, y + row, 0.0f, 0.0f, 0.0f, 0.0f,
		((unsigned long)(alpha * 0xA0) << 24) | 0x303030UL);
	color.alpha = alpha;
	color.red = color.green = color.blue = 0.85f;
	bounds.x0 = (short)(x0 + place_width + 0.01f * height), bounds.x1 = (short)(x1 - score_width - 0.01f * height), bounds.y0 = (short)(y - 2.0f), bounds.y1 = (short)(y + row + 24.0f);
	draw_string_set_draw_mode(hud_get_font_index(), NONE, H2_HUD_JUSTIFY_LEFT, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, L"Name");
	bounds.x0 = (short)(x1 - score_width), bounds.x1 = (short)x1;
	draw_string_set_draw_mode(hud_get_font_index(), NONE, H2_HUD_JUSTIFY_CENTER, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, L"Score");
	y += row;
	for (i = 0; i < count && i < 8; i++)
	{
		struct player_datum *p = player_get(rows[i].player);
		boolean self = rows[i].player == player_index;
		wchar_t score[16];

		h2_hud_quad(bitmap, x0, y, x1, y + row - 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
			((unsigned long)(alpha * (self ? 0xA0 : 0x80)) << 24) | (self ? 0x404040UL : 0x282828UL));
		color.alpha = alpha;
		if (teams)
		{
			color.red = p->team_index == 0 ? 0.9f : 0.45f;
			color.green = 0.45f;
			color.blue = p->team_index == 0 ? 0.45f : 0.95f;
		}
		else
			color.red = color.green = color.blue = self ? 1.0f : 0.8f;
		/* the place (the ranking's: a team game's is its team's) */
		bounds.x0 = (short)x0, bounds.x1 = (short)(x0 + place_width), bounds.y0 = (short)(y - 2.0f), bounds.y1 = (short)(y + row + 24.0f);
		if (rows[i].place != NONE)
		{
			usnprintf(score, NUMBEROF(score), L"%ld", rows[i].place + 1);
			draw_string_set_draw_mode(hud_get_font_index(), NONE, H2_HUD_JUSTIFY_CENTER, 0, &color);
			rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, score);
		}
		/* the emblem beside the name (h2_emblem.c: the player's, a joiner's
		as the host told it) */
		if (h2_emblem_ready())
		{
			struct h2_emblem emblem;
			struct bitmap_data *picture;

			if (h2_emblem_player(rows[i].player, &emblem) && (picture = h2_emblem_bitmap(&emblem, H2_EMBLEM_WHOLE)) != NULL)
			{
				real ex = x0 + place_width + 0.01f * height, ey = y + 1.0f;

				h2_hud_quad(picture, ex, ey, ex + emblem_size, ey + emblem_size, 0.0f, 0.0f, 1.0f, 1.0f, ((unsigned long)(alpha * 0xFF) << 24) | 0xFFFFFFUL);
			}
		}
		bounds.x0 = (short)(x0 + place_width + 0.02f * height + emblem_size), bounds.x1 = (short)(x1 - score_width - 0.01f * height);
		draw_string_set_draw_mode(hud_get_font_index(), NONE, H2_HUD_JUSTIFY_LEFT, 0, &color);
		rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, p->name);
		usnprintf(score, NUMBEROF(score), L"%ld", rows[i].score);
		bounds.x0 = (short)(x1 - score_width), bounds.x1 = (short)x1;
		draw_string_set_draw_mode(hud_get_font_index(), NONE, H2_HUD_JUSTIFY_CENTER, 0, &color);
		rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, score);
		y += row;
	}
	return TRUE;
}

/* (h2_medals.c, h2_menu.c, carnage_report.c) a string of Halo 2's postgame
string list by its string id's name, from the level's HUD file (the
player's own main menu map's words); NULL when the level has none */
char const *h2_hud_word(char const *id)
{
	long k;

	h2_hud_load();
	if (!id || !h2_hud.loaded)
		return NULL;
	for (k = 0; k < h2_hud.word_count; k++)
	{
		if (!strcmp(h2_hud.words[k].id, id))
			return h2_hud.words[k].text;
	}
	return NULL;
}
