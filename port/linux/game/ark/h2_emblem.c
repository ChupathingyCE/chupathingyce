/*
H2_EMBLEM.C

Halo 2's player emblems: see h2_emblem.h. Halo 2's rules, from its maps and
the decompilation (halo2-decomp, GPL-3.0-or-later):

- the maps' channels: red the a layer's value, green its alpha, blue the b
  layer's value, alpha the b layer's alpha (MCC's textures.dat holds them
  B G R A, 256 a side);
- emblem_overlay_simple (the armour): colour = (1 - Aa t) Vb Cq + t Va Ct,
  alpha = (1 - Aa t) Ab + Aa t, t the toggle (emblem_toggle_foreground: 1
  unless flags bit 0 is set, function_bc970, src/unknown_0bb760.cpp), Ct and
  Cq the object's tertiary and quaternary change colours; the maps' scale
  1, MCC's mc_emblem's and elite_emblem's constants (neither turned nor moved);
- hud\waypoint_emblem (a whole emblem): the background's colour Va Cp + Vb Cs,
  its alpha Aa + Ab, its map at uv x 1.2 - 0.1 with a border of nothing (the
  shader's constants), under the foreground as above;
- an armour's colours: function_7f790, src/unknown_07f720.cpp: in a team game
  the primary is the team's colour and the secondary the player's primary
  (the emblem's own two are the same in every game).
*/

#include "cseries.h"
#include "cseries_windows.h"
#include "h2_emblem.h"
#include "emblem_net.h"
#include "h2_cache.h"
#include "h2_level.h"
#include "player_cosmetics.h"
#include "player_models.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmaps_internal.h"
#include "bitmaps/bitmap_group.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/player_ui.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "saved games/player_profile.h"
#include "scenario/scenario.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"
#include "lang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HALO_LINUX
#include "render_epoch.h" /* HALO_ENV_ONCE */
#endif

#undef malloc
#undef free
#undef realloc

#define H2_ALIGN __attribute__((force_align_arg_pointer))

void platform_log(char const *format, ...);
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
unsigned long system_milliseconds(void);
int bots_player_is_bot(long player_index);
boolean player_profile_emblem_get(long player_profile_index, byte *values);
boolean player_profile_emblem_set(long player_profile_index, byte const *values);

/* ---------- constants */

#define H2_EMBLEM_FILE H2_LEVEL_FOLDER "\\emblems.h2emblem"
/* (the profile's extension's flags byte: "Use model for all games" off,
h2_emblem_models_separate; never in an emblem's flags) */
#define H2_EMBLEM_PROFILE_SEPARATE_MODELS 0x80

enum
{
	/* (this engine's bitmap tags', menu_tags.c's) */
	H2_EMBLEM_BITMAP_TYPE_2D = 0,
	H2_EMBLEM_BITMAP_FORMAT_A8R8G8B8 = 11,
	H2_EMBLEM_BITMAP_POWER_OF_TWO_BIT = 0,

	H2_EMBLEM_FILE_SIGNATURE = 0x6D653268,   /* 'h2em' */
	H2_EMBLEM_FILE_VERSION = 1,
	/* (the two languages' names kept: English, and Spanish, the one language
	this project translates; the files' fifth) */
	H2_EMBLEM_LANGUAGES = 2,
	H2_EMBLEM_NAMES = H2_EMBLEM_FOREGROUNDS + H2_EMBLEM_BACKGROUNDS + H2_EMBLEM_COLORS,

	/* (0: the file's whole size, the source's) */
	H2_EMBLEM_MAXIMUM_SIDE = 256,
#if defined(HALO_VITA) || defined(HALO_GXM_NULL)
	H2_EMBLEM_SIDE = 128,
	H2_EMBLEM_WHOLE_SIDE = 64,
	H2_EMBLEM_TEXTURES = 24,
	H2_EMBLEM_MADE_A_FRAME = 1,
#else
	H2_EMBLEM_SIDE = 256,
	H2_EMBLEM_WHOLE_SIDE = 128,
	H2_EMBLEM_TEXTURES = 48,
	H2_EMBLEM_MADE_A_FRAME = 4,
#endif
	/* the objects remembered (a body keeps its player's emblem, as Halo 2
	copies the appearance into the object, function_14bfc0) */
	H2_EMBLEM_OBJECTS = 128,
	/* the menu widgets drawn as emblems */
	H2_EMBLEM_WIDGETS = 64,
};

/* where the emblems' file is read: Halo 2's own folder, else the old t:\\h2 */
static char const *h2_emblem_read_path(void)
{
	static char path[256];

	h2_level_folder_file("emblems.h2emblem", path, sizeof(path));
	return path;
}

/* ---------- structures */

struct h2_emblem_file_header
{
	unsigned long signature, version, side, foregrounds, backgrounds, source_size;
	unsigned long colors[H2_EMBLEM_COLORS];
	unsigned long teams[H2_EMBLEM_TEAMS];
	char names[H2_EMBLEM_LANGUAGES][H2_EMBLEM_NAMES][H2_EMBLEM_NAME_SIZE];
};

/* what a texture is: its pixels depend on these alone */
struct h2_emblem_look
{
	unsigned long rgb[H2_EMBLEM_CHANGE_COLORS];
	byte foreground, background, off, kind;
};

struct h2_emblem_texture
{
	struct h2_emblem_look look;
	struct bitmap_data bitmap;
	unsigned long used_frame;
	boolean made;
};

static struct
{
	boolean tried, loaded;
	struct h2_emblem_file_header header;
	FILE *file;
	struct h2_emblem_texture textures[H2_EMBLEM_TEXTURES];
	struct
	{
		long object_index;
		struct h2_emblem_look look;
		boolean known;
	} objects[H2_EMBLEM_OBJECTS];
	long next_object;
	struct
	{
		void const *widget;
		char const *name;
		struct h2_emblem emblem;
	} widgets[H2_EMBLEM_WIDGETS];
	long next_widget;
	unsigned long frame;
	long made_this_frame;
	long stand_in_tag;
	char level[64];
} h2_emblem;

/* (the settings panel's choice, from its thread: h2_emblem_update takes it) */
static volatile int h2_emblem_panel_row = NONE, h2_emblem_panel_index;

/* ---------- the file */

static void h2_emblem_box_half(unsigned char const *in, unsigned long side, unsigned char *out)
{
	unsigned long x, y;
	int k;

	for (y = 0; y < side / 2; y++)
	{
		for (x = 0; x < side / 2; x++)
		{
			unsigned char const *t = in + (2 * y * side + 2 * x) * 4, *b = t + side * 4;

			for (k = 0; k < 4; k++)
				out[(y * (side / 2) + x) * 4 + k] = (unsigned char)((t[k] + t[4 + k] + b[k] + b[4 + k] + 2) / 4);
		}
	}
}

/* (scene_pack.c) a build's read or write done: the memory card's turns and
the watchdog's heartbeat (scene_packs.c's worker_io) */
extern void (*scene_pack_io_hook)(unsigned long bytes, boolean source, boolean write);

/* size bytes written to file as a pack's are (scene_pack.c pack_write): in
pieces of 32 KB, each one told to scene_pack_io_hook - the card served in
turns, not held for a whole image's write. FALSE if a write failed */
static boolean emblem_write(FILE *file, void const *data, unsigned long size)
{
	unsigned char const *bytes = data;
	unsigned long done = 0;

	while (done < size)
	{
		unsigned long chunk = size - done > 32 * 1024 ? 32 * 1024 : size - done;

		if (fwrite(bytes + done, 1, chunk, file) != chunk)
			return FALSE;
		done += chunk;
		if (scene_pack_io_hook)
			scene_pack_io_hook(chunk, FALSE, TRUE);
	}
	return TRUE;
}

/* image of a bitmap tag's images, B G R A at side a side, into out */
H2_ALIGN static boolean h2_emblem_image_write(struct h2_cache *cache, unsigned long images, long index, FILE *file)
{
	static unsigned char level[H2_EMBLEM_SIDE * H2_EMBLEM_SIDE * 4 * 4];
	unsigned char const *image = h2_cache_at(cache, images + 0xA8 * (unsigned long)index, 0xA8);
	unsigned long got = 0, side;
	unsigned char *pixels;
	boolean good = FALSE;

	if (!image)
		return FALSE;
	side = *(unsigned short const *)(image + 4);
	/* (32-bit, 256 a side: every one of MCC's) */
	if (side != H2_EMBLEM_MAXIMUM_SIDE || *(unsigned short const *)(image + 6) != side ||
		(*(unsigned short const *)(image + 0xC) != 10 && *(unsigned short const *)(image + 0xC) != 11) ||
		!(pixels = h2_cache_texture_read(cache, *(unsigned long const *)(image + 0x38), *(unsigned long const *)(image + 0x50), &got)))
	{
		return FALSE;
	}
	if (got >= side * side * 4)
	{
		unsigned char const *from = pixels;

		while (side > H2_EMBLEM_SIDE)
		{
			h2_emblem_box_half(from, side, level);
			from = level;
			side /= 2;
		}
		good = emblem_write(file, from, side * side * 4);
	}
	free(pixels);
	return good;
}

static void h2_emblem_names_read(struct h2_cache *menu, char const *group_name, long first_name, long count)
{
	long tag = menu ? h2_cache_tag_find(menu, 'unic', group_name) : NONE;
	struct h2_cache_tag list;
	int language;

	if (tag == NONE || !h2_cache_tag_get(menu, tag, &list))
		return;
	for (language = 0; language < H2_EMBLEM_LANGUAGES; language++)
	{
		/* (the files' languages: English, ..., Spanish the fifth; each one's
		first string and count at 0x10: h2_menu_make.c) */
		long file_language = language ? 4 : 0, k, found = 0, strings;
		unsigned char const *at = h2_cache_at(menu, list.address + 0x10 + 4 * (unsigned long)file_language, 4);
		unsigned long packed = at ? *(unsigned long const *)at : 0;

		strings = (long)(packed >> 16) & 0xFFFF;
		for (k = 0; k < strings && found < count; k++)
		{
			char words[H2_EMBLEM_NAME_SIZE * 2], id[32], *text = words;
			unsigned long string_id = 0;
			long number;

			if (!h2_cache_localized_string(menu, file_language, (long)(packed & 0xFFFF) + k, &string_id, words, sizeof(words)) ||
				!h2_cache_string(menu, string_id, id, sizeof(id)))
			{
				continue;
			}
			/* (the items' ids are their numbers) */
			if (id[0] < '0' || id[0] > '9')
				continue;
			number = atol(id);
			if (number < 0 || number >= count)
				continue;
			/* (the colour picker's: its swatch's glyph and spaces first) */
			if ((unsigned char)text[0] == 0xEE)
				text += 3;
			while (*text == ' ')
				text++;
			snprintf(h2_emblem.header.names[language][first_name + number], H2_EMBLEM_NAME_SIZE, "%s", text);
			found++;
		}
	}
}

H2_ALIGN void h2_emblem_pack_make(char const *maps_directory)
{
	static char const *const bitmaps[2] = { "ui\\global_bitmaps\\emblems\\foreground", "ui\\global_bitmaps\\emblems\\background" };
	struct h2_cache *cache, *menu;
	struct h2_cache_tag tag;
	char why[128];
	char source[512];
	FILE *file;
	unsigned long source_size = 0;
	long i;
	int b;
	boolean good = TRUE;

	if (!maps_directory || !maps_directory[0])
		return;
	snprintf(source, sizeof(source), "%sshared.map", maps_directory);
	if ((file = fopen(source, "rb")) != NULL)
	{
		fseek(file, 0, SEEK_END);
		source_size = (unsigned long)ftell(file);
		fclose(file);
	}
	if (!source_size)
		return;
	/* (made already, from that file) */
	if ((file = fopen(h2_emblem_read_path(), "rb")) != NULL)
	{
		struct h2_emblem_file_header header;
		boolean current = fread(&header, sizeof(header), 1, file) == 1 && header.signature == H2_EMBLEM_FILE_SIGNATURE &&
			header.version == H2_EMBLEM_FILE_VERSION && header.side == H2_EMBLEM_SIDE && header.source_size == source_size;

		fclose(file);
		if (current)
			return;
	}
	if (h2_emblem.file)
	{
		fclose(h2_emblem.file);
		h2_emblem.file = NULL;
	}
	h2_emblem.loaded = FALSE;
	if (!(cache = h2_cache_open(maps_directory, "shared", why, sizeof(why))) || !h2_cache_is_mcc(cache))
	{
		platform_log("h2 emblems: NOT MADE (made again as the next Halo 2 level is resolved): shared.map cannot be read (%s)",
			cache ? "not MCC's" : why);
		if (cache)
			h2_cache_close(cache);
		return;
	}
	memset(&h2_emblem.header, 0, sizeof(h2_emblem.header));
	h2_emblem.header.signature = H2_EMBLEM_FILE_SIGNATURE;
	h2_emblem.header.version = H2_EMBLEM_FILE_VERSION;
	h2_emblem.header.side = H2_EMBLEM_SIDE;
	h2_emblem.header.foregrounds = H2_EMBLEM_FOREGROUNDS;
	h2_emblem.header.backgrounds = H2_EMBLEM_BACKGROUNDS;
	h2_emblem.header.source_size = source_size;
	/* the globals' profile colours (0x160: 12 bytes each, MCC's maps and
	Halo 2's kit field "profile colors") */
	{
		long globals = h2_cache_tag_find(cache, 'matg', "globals\\globals"), count = 0;
		unsigned long colors = 0;

		if (globals != NONE && h2_cache_tag_get(cache, globals, &tag) && h2_cache_block(cache, tag.address + 0x160, 12, &count, &colors) &&
			count >= H2_EMBLEM_COLORS)
		{
			for (i = 0; i < H2_EMBLEM_COLORS; i++)
			{
				float const *rgb = h2_cache_at(cache, colors + 12 * (unsigned long)i, 12);
				unsigned long packed = 0;
				int k;

				for (k = 0; k < 3 && rgb; k++)
				{
					float value = rgb[k] <= 0.0f ? 0.0f : rgb[k] >= 1.0f ? 1.0f : rgb[k];

					packed = (packed << 8) | (unsigned long)(value * 255.0f + 0.5f);
				}
				h2_emblem.header.colors[i] = packed;
			}
		}
		else
		{
			platform_log("h2 emblems: no profile colours in the globals (%ld)", count);
			good = FALSE;
		}
	}
	/* the multiplayer globals' team colours (universal 0: team colors, the
	decompilation's s_team_colors at 0x10, 12 bytes each) */
	{
		long globals = h2_cache_tag_find(cache, 'mulg', "multiplayer\\multiplayer_globals"), count = 0;
		unsigned long universal = 0, colors = 0;

		if (globals != NONE && h2_cache_tag_get(cache, globals, &tag) && h2_cache_block(cache, tag.address, 0x100, &count, &universal) &&
			count > 0 && h2_cache_block(cache, universal + 0x10, 12, &count, &colors))
		{
			for (i = 0; i < H2_EMBLEM_TEAMS && i < count; i++)
			{
				float const *rgb = h2_cache_at(cache, colors + 12 * (unsigned long)i, 12);
				unsigned long packed = 0;
				int k;

				for (k = 0; k < 3 && rgb; k++)
					packed = (packed << 8) | (unsigned long)((rgb[k] <= 0.0f ? 0.0f : rgb[k] >= 1.0f ? 1.0f : rgb[k]) * 255.0f + 0.5f);
				h2_emblem.header.teams[i] = packed;
			}
		}
	}
	/* the names (mainmenu.map's lists, if it is here) */
	menu = h2_cache_open(maps_directory, "mainmenu", why, sizeof(why));
	if (menu)
	{
		h2_emblem_names_read(menu, "ui\\screens\\game_shell\\settings_screen\\player_profile\\choose_emblem", 0, H2_EMBLEM_FOREGROUNDS);
		h2_emblem_names_read(menu, "ui\\screens\\game_shell\\settings_screen\\player_profile\\choose_emblem_background",
			H2_EMBLEM_FOREGROUNDS, H2_EMBLEM_BACKGROUNDS);
		h2_emblem_names_read(menu, "ui\\screens\\game_shell\\settings_screen\\player_profile\\color_picker",
			H2_EMBLEM_FOREGROUNDS + H2_EMBLEM_BACKGROUNDS, H2_EMBLEM_COLORS);
		h2_cache_close(menu);
	}
	/* the images, the foregrounds then the backgrounds */
	h2_level_folder_create();
	file = good ? fopen(H2_EMBLEM_FILE, "wb") : NULL;
	/* (its header last: a file cut short is no file) */
	{
		static struct h2_emblem_file_header empty;

		if (!file || fwrite(&empty, sizeof(empty), 1, file) != 1)
			good = FALSE;
	}
	for (b = 0; b < 2 && good; b++)
	{
		long wanted = b ? H2_EMBLEM_BACKGROUNDS : H2_EMBLEM_FOREGROUNDS, count = 0, bitmap = h2_cache_tag_find(cache, 'bitm', bitmaps[b]);
		unsigned long images = 0;

		if (bitmap == NONE || !h2_cache_tag_get(cache, bitmap, &tag) || !h2_cache_block(cache, tag.address + 0x44, 0xA8, &count, &images) ||
			count != wanted)
		{
			platform_log("h2 emblems: %s not found (%ld images)", bitmaps[b], count);
			good = FALSE;
			break;
		}
		for (i = 0; i < wanted && good; i++)
			good = h2_emblem_image_write(cache, images, i, file);
		if (!good)
			platform_log("h2 emblems: %s image %ld could not be read", bitmaps[b], i - 1);
	}
	if (file)
	{
		if (good)
			good = fseek(file, 0, SEEK_SET) == 0 && fwrite(&h2_emblem.header, sizeof(h2_emblem.header), 1, file) == 1;
		fclose(file);
	}
	h2_cache_close(cache);
	h2_emblem.tried = FALSE;
	platform_log("h2 emblems: %s %d foregrounds, %d backgrounds, %d colours, %d a side", good ? "made" :
		"NOT MADE (the file left out, made again as the next Halo 2 level is resolved):",
		H2_EMBLEM_FOREGROUNDS, H2_EMBLEM_BACKGROUNDS, H2_EMBLEM_COLORS, H2_EMBLEM_SIDE);
	/* (a file cut short is no file: the next try begins afresh) */
	if (!good)
		remove(H2_EMBLEM_FILE);
}

/* the file opened (once, and again after it is made) */
static boolean h2_emblem_load(void)
{
	if (h2_emblem.loaded)
		return TRUE;
	if (h2_emblem.tried)
		return FALSE;
	h2_emblem.tried = TRUE;
	if (!(h2_emblem.file = fopen(h2_emblem_read_path(), "rb")))
		return FALSE;
	if (fread(&h2_emblem.header, sizeof(h2_emblem.header), 1, h2_emblem.file) != 1 ||
		h2_emblem.header.signature != H2_EMBLEM_FILE_SIGNATURE || h2_emblem.header.version != H2_EMBLEM_FILE_VERSION ||
		h2_emblem.header.side != H2_EMBLEM_SIDE || h2_emblem.header.foregrounds != H2_EMBLEM_FOREGROUNDS ||
		h2_emblem.header.backgrounds != H2_EMBLEM_BACKGROUNDS)
	{
		fclose(h2_emblem.file);
		h2_emblem.file = NULL;
		return FALSE;
	}
	h2_emblem.loaded = TRUE;
	return TRUE;
}

boolean h2_emblem_ready(void)
{
	return h2_emblem_load();
}

boolean h2_emblem_available(void)
{
	static boolean made_tried;

	if (h2_emblem_load())
		return TRUE;
	if (made_tried)
		return FALSE;
	made_tried = TRUE;
	/* (a machine with MCC's Halo 2 whose file is not made yet) */
	{
		char const *cache_files_map_directory(void);
		char const *maps = h2_maps_directory();

		if (maps && maps[0] && h2_level_is_halo2_map(maps, "shared"))
			h2_emblem_pack_make(maps);
	}
	return h2_emblem_load();
}

/* an image's pixels (B G R A, the file's side), into pixels */
static boolean h2_emblem_image_read(boolean background, long index, unsigned char *pixels)
{
	unsigned long bytes = H2_EMBLEM_SIDE * H2_EMBLEM_SIDE * 4;
	unsigned long at = sizeof(struct h2_emblem_file_header) + bytes * ((background ? H2_EMBLEM_FOREGROUNDS : 0) + (unsigned long)index);

	return h2_emblem.file && fseek(h2_emblem.file, (long)at, SEEK_SET) == 0 && fread(pixels, bytes, 1, h2_emblem.file) == 1;
}

/* ---------- the passes */

/* a level's pixels as the look's pass makes them: fg (B G R A) and, for a
whole emblem, bg, side a side, into out (B G R A) */
static void h2_emblem_pass(struct h2_emblem_look const *look, unsigned char const *fg, unsigned char const *bg, long side,
	unsigned char *out)
{
	float t = look->off ? 0.0f : 1.0f;
	float cp[3], cs[3], ct[3], cq[3];
	long x, y;
	int k;

	for (k = 0; k < 3; k++)
	{
		/* (0xRRGGBB; these arrays B G R, as the texels) */
		cp[k] = (float)((look->rgb[0] >> (8 * k)) & 255) / 255.0f;
		cs[k] = (float)((look->rgb[1] >> (8 * k)) & 255) / 255.0f;
		ct[k] = (float)((look->rgb[2] >> (8 * k)) & 255) / 255.0f;
		cq[k] = (float)((look->rgb[3] >> (8 * k)) & 255) / 255.0f;
	}
	for (y = 0; y < side; y++)
	{
		for (x = 0; x < side; x++)
		{
			unsigned char const *f = fg + (y * side + x) * 4;
			float va = f[2] / 255.0f, aa = f[1] / 255.0f, vb = f[0] / 255.0f, ab = f[3] / 255.0f;
			float knock = 1.0f - aa * t;
			float alpha = knock * ab + aa * t;
			float color[3];
			unsigned char *o = out + (y * side + x) * 4;

			for (k = 0; k < 3; k++)
				color[k] = knock * vb * cq[k] + t * va * ct[k];
			if (bg)
			{
				/* (its map at uv x 1.2 - 0.1, nearest texel's pair blended along
				both ways; outside it the border: nothing) */
				float u = ((x + 0.5f) / (float)side) * 1.2f - 0.1f, v = ((y + 0.5f) / (float)side) * 1.2f - 0.1f;
				float back[3] = { 0.0f, 0.0f, 0.0f }, back_alpha = 0.0f;

				if (u >= 0.0f && u < 1.0f && v >= 0.0f && v < 1.0f)
				{
					float px = u * (float)side - 0.5f, py = v * (float)side - 0.5f;
					long x0 = px < 0.0f ? 0 : (long)px, y0 = py < 0.0f ? 0 : (long)py;
					long x1 = x0 + 1 < side ? x0 + 1 : x0, y1 = y0 + 1 < side ? y0 + 1 : y0;
					float fx = px - (float)x0, fy = py - (float)y0, sample[4];
					int c;

					if (fx < 0.0f)
						fx = 0.0f;
					if (fy < 0.0f)
						fy = 0.0f;
					for (c = 0; c < 4; c++)
					{
						float top = bg[(y0 * side + x0) * 4 + c] * (1.0f - fx) + bg[(y0 * side + x1) * 4 + c] * fx;
						float bottom = bg[(y1 * side + x0) * 4 + c] * (1.0f - fx) + bg[(y1 * side + x1) * 4 + c] * fx;

						sample[c] = (top * (1.0f - fy) + bottom * fy) / 255.0f;
					}
					for (c = 0; c < 3; c++)
						back[c] = sample[2] * cp[c] + sample[0] * cs[c];
					back_alpha = sample[1] + sample[3];
					if (back_alpha > 1.0f)
						back_alpha = 1.0f;
				}
				for (k = 0; k < 3; k++)
					color[k] += (1.0f - alpha) * back[k];
				alpha += (1.0f - alpha) * back_alpha;
			}
			for (k = 0; k < 3; k++)
				o[k] = (unsigned char)((color[k] >= 1.0f ? 1.0f : color[k]) * 255.0f + 0.5f);
			o[3] = (unsigned char)((alpha >= 1.0f ? 1.0f : alpha) * 255.0f + 0.5f);
		}
	}
}

/* made a texture: each level the pass over that level of its maps, as the
shader over mipped maps */
H2_ALIGN static boolean h2_emblem_texture_make(struct h2_emblem_texture *texture)
{
	struct h2_emblem_look const *look = &texture->look;
	static unsigned char fg[H2_EMBLEM_SIDE * H2_EMBLEM_SIDE * 4], bg[H2_EMBLEM_SIDE * H2_EMBLEM_SIDE * 4];
	static unsigned char half[H2_EMBLEM_SIDE * H2_EMBLEM_SIDE * 4];
	long side = look->kind == H2_EMBLEM_WHOLE ? H2_EMBLEM_WHOLE_SIDE : H2_EMBLEM_SIDE, level_side, total = 0, levels = 0, at = 0;
	long file_side = H2_EMBLEM_SIDE;
	unsigned char *pixels;
	struct bitmap_data *bitmap = &texture->bitmap;

	if (!h2_emblem_image_read(FALSE, look->foreground, fg) ||
		(look->kind == H2_EMBLEM_WHOLE && !h2_emblem_image_read(TRUE, look->background, bg)))
	{
		return FALSE;
	}
	while (file_side > side)
	{
		h2_emblem_box_half(fg, (unsigned long)file_side, half);
		memcpy(fg, half, (size_t)((file_side / 2) * (file_side / 2) * 4));
		if (look->kind == H2_EMBLEM_WHOLE)
		{
			h2_emblem_box_half(bg, (unsigned long)file_side, half);
			memcpy(bg, half, (size_t)((file_side / 2) * (file_side / 2) * 4));
		}
		file_side /= 2;
	}
	/* (mips down to 4 a side) */
	for (level_side = side; level_side >= 4; level_side /= 2)
	{
		total += level_side * level_side * 4;
		levels++;
	}
	if (!(pixels = malloc((size_t)total)))
		return FALSE;
	for (level_side = side; level_side >= 4; level_side /= 2)
	{
		h2_emblem_pass(look, fg, look->kind == H2_EMBLEM_WHOLE ? bg : NULL, level_side, pixels + at);
		at += level_side * level_side * 4;
		h2_emblem_box_half(fg, (unsigned long)level_side, half);
		memcpy(fg, half, (size_t)((level_side / 2) * (level_side / 2) * 4));
		if (look->kind == H2_EMBLEM_WHOLE)
		{
			h2_emblem_box_half(bg, (unsigned long)level_side, half);
			memcpy(bg, half, (size_t)((level_side / 2) * (level_side / 2) * 4));
		}
	}
	memset(bitmap, 0, sizeof(*bitmap));
	bitmap->signature = BITMAP_GROUP_TAG;
	bitmap->width = (short)side;
	bitmap->height = (short)side;
	bitmap->depth = 1;
	bitmap->type = H2_EMBLEM_BITMAP_TYPE_2D;
	bitmap->format = H2_EMBLEM_BITMAP_FORMAT_A8R8G8B8;
	bitmap->flags = FLAG(H2_EMBLEM_BITMAP_POWER_OF_TWO_BIT);
	bitmap->mipmap_count = (short)(levels - 1);
	bitmap->tag_index = NONE;
	bitmap->cache_block_index = NONE;
	bitmap->pixels_size = total;
	bitmap->base_address = pixels;
	if (!rasterizer_bitmap_new(bitmap) || !bitmap->hardware_format)
	{
		free(pixels);
		memset(bitmap, 0, sizeof(*bitmap));
		return FALSE;
	}
	rasterizer_bitmap_changed(bitmap);
	bitmap->base_address = NULL;
	free(pixels);
	return TRUE;
}

static void h2_emblem_look_of(struct h2_emblem const *emblem, short kind, struct h2_emblem_look *look)
{
	struct h2_emblem clean = *emblem;
	int k;

	memset(look, 0, sizeof(*look));
	if (!h2_emblem_valid(&clean))
		h2_emblem_default(&clean);
	for (k = 0; k < H2_EMBLEM_CHANGE_COLORS; k++)
		look->rgb[k] = h2_emblem.header.colors[clean.colors[k]];
	look->foreground = clean.foreground;
	look->background = clean.background;
	look->off = (clean.flags & H2_EMBLEM_FLAG_FOREGROUND_A_OFF) != 0;
	look->kind = (byte)kind;
	/* (the armour's pass has no background, nor its two colours) */
	if (kind == H2_EMBLEM_ARMOUR)
	{
		look->background = 0;
		look->rgb[0] = look->rgb[1] = 0;
	}
}

/* (h2_emblem_deferred_count) */
static unsigned long h2_emblem_deferred;

static struct bitmap_data *h2_emblem_texture_get(struct h2_emblem_look const *look)
{
	struct h2_emblem_texture *free_slot = NULL;
	long i;

	if (!h2_emblem_load())
		return NULL;
	for (i = 0; i < H2_EMBLEM_TEXTURES; i++)
	{
		struct h2_emblem_texture *texture = &h2_emblem.textures[i];

		if (texture->made && !memcmp(&texture->look, look, sizeof(*look)))
		{
			texture->used_frame = h2_emblem.frame;
			return &texture->bitmap;
		}
		if (!texture->made)
		{
			if (!free_slot || free_slot->made)
				free_slot = texture;
		}
		else if (texture->used_frame + 1 < h2_emblem.frame && (!free_slot || (free_slot->made && texture->used_frame < free_slot->used_frame)))
		{
			free_slot = texture;
		}
	}
	if (!free_slot || h2_emblem.made_this_frame >= H2_EMBLEM_MADE_A_FRAME)
	{
		h2_emblem_deferred++;
		return NULL;
	}
	if (free_slot->made)
	{
		rasterizer_bitmap_delete(&free_slot->bitmap);
		free_slot->made = FALSE;
	}
	h2_emblem.made_this_frame++;
	memset(&free_slot->look, 0, sizeof(free_slot->look));
	free_slot->look = *look;
	if (!h2_emblem_texture_make(free_slot))
		return NULL;
	free_slot->made = TRUE;
	free_slot->used_frame = h2_emblem.frame;
	return &free_slot->bitmap;
}

struct bitmap_data *h2_emblem_bitmap(struct h2_emblem const *emblem, short kind)
{
	struct h2_emblem_look look;

	if (!h2_emblem_load())
		return NULL;
	h2_emblem_look_of(emblem, kind, &look);
	return h2_emblem_texture_get(&look);
}

/* ---------- players */

short h2_emblem_nearest_color(unsigned long wanted)
{
	long best = 0x7FFFFFFF;
	short k, found = 0;

	for (k = 0; k < H2_EMBLEM_COLORS && h2_emblem_load(); k++)
	{
		unsigned long rgb = h2_emblem.header.colors[k];
		long dr = (long)((rgb >> 16) & 255) - (long)((wanted >> 16) & 255);
		long dg = (long)((rgb >> 8) & 255) - (long)((wanted >> 8) & 255);
		long db = (long)(rgb & 255) - (long)(wanted & 255);
		long distance = dr * dr + dg * dg + db * db;

		if (distance < best)
		{
			best = distance;
			found = k;
		}
	}
	return found;
}

void h2_emblem_profile_get(long profile_index, struct h2_emblem *emblem)
{
	byte values[8];

	h2_emblem_default(emblem);
	/* (a profile that has chosen none: the settings panel's species) */
	if (HALO_ENV_ONCE("HALO_H2_PLAYER_SPECIES") && !strcmp(HALO_ENV_ONCE("HALO_H2_PLAYER_SPECIES"), "elite"))
		emblem->model = 1;
	if (profile_index == NONE)
		return;
	if (player_profile_emblem_get(profile_index, values))
	{
		struct h2_emblem saved;

		memcpy(saved.colors, values, H2_EMBLEM_CHANGE_COLORS);
		saved.foreground = values[4];
		saved.background = values[5];
		saved.flags = (uint8_t)(values[6] & ~H2_EMBLEM_PROFILE_SEPARATE_MODELS);
		saved.model = values[7];
		if (h2_emblem_valid(&saved))
			*emblem = saved;
	}
	/* (one armour colour: the profile's, whichever game; Halo 2 wears the
	nearest of its eighteen) */
	if (h2_emblem_load())
	{
		struct player_profile profile;

		if (player_profile_get(profile_index, &profile))
		{
			short color = player_cosmetics_profile_color(profile_index, profile.primary_color_index);

			if (color != NONE)
				emblem->colors[0] = (uint8_t)h2_emblem_nearest_color(player_cosmetics_color_rgb(color));
		}
	}
}

boolean h2_emblem_models_separate(long profile_index)
{
	byte values[8];

	return profile_index != NONE && player_profile_emblem_get(profile_index, values) &&
		(values[6] & H2_EMBLEM_PROFILE_SEPARATE_MODELS) != 0;
}

boolean h2_emblem_profile_save(long profile_index, struct h2_emblem const *emblem)
{
	return h2_emblem_profile_save_separate(profile_index, emblem, h2_emblem_models_separate(profile_index));
}

boolean h2_emblem_profile_save_separate(long profile_index, struct h2_emblem const *emblem, boolean separate)
{
	byte values[8];

	if (profile_index == NONE || !h2_emblem_valid(emblem))
		return FALSE;
	memcpy(values, emblem->colors, H2_EMBLEM_CHANGE_COLORS);
	values[4] = emblem->foreground;
	values[5] = emblem->background;
	values[6] = (byte)((emblem->flags & H2_EMBLEM_FLAGS_KEPT) | (separate ? H2_EMBLEM_PROFILE_SEPARATE_MODELS : 0));
	values[7] = emblem->model;
	return player_profile_emblem_set(profile_index, values);
}

/* a profile's emblem as this machine shows it */
void h2_emblem_profile_shown(long profile_index, struct h2_emblem *emblem)
{
	/* (tests: HALO_H2_EMBLEM=foreground,background,flags,c0,c1,c2,c3) */
	char const *test = HALO_ENV_ONCE("HALO_H2_EMBLEM");
	int v[7];

	h2_emblem_profile_get(profile_index, emblem);
	if (test && sscanf(test, "%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) == 7)
	{
		struct h2_emblem wanted;
		int k;

		wanted.foreground = (uint8_t)v[0];
		wanted.background = (uint8_t)v[1];
		wanted.flags = (uint8_t)v[2];
		wanted.model = emblem->model;
		for (k = 0; k < H2_EMBLEM_CHANGE_COLORS; k++)
			wanted.colors[k] = (uint8_t)v[3 + k];
		if (h2_emblem_valid(&wanted))
			*emblem = wanted;
	}
}

void h2_emblem_local(short local_player_index, struct h2_emblem *emblem)
{
	/* (the first player's as Halo CE's model finds it: no profile active yet,
	one the card has) */
	h2_emblem_profile_shown(local_player_index == 0 ? player_models_first_profile() :
		player_ui_get_active_player_profile_index(local_player_index), emblem);
}

static unsigned long h2_emblem_name_seed(wchar_t const *name, long length)
{
	unsigned long hash = 2166136261UL;
	long k;

	for (k = 0; k < length && name[k]; k++)
		hash = ((hash ^ (unsigned long)(name[k] & 0xFFFF)) * 16777619UL) & 0xFFFFFFFFUL;
	return hash;
}

boolean h2_emblem_network_player(struct network_player const *player, struct h2_emblem *emblem)
{
	struct h2_emblem const *known = emblem_net_player(player->machine_index, player->controller_index);

	if (known)
		*emblem = *known;
	return known != NULL;
}

boolean h2_emblem_player(long player_index, struct h2_emblem *emblem)
{
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;

	h2_emblem_default(emblem);
	if (!player)
		return FALSE;
	if (bots_player_is_bot(player_index))
	{
		h2_emblem_from_seed(h2_emblem_name_seed(player->name, NUMBEROF(player->name)), emblem);
		return TRUE;
	}
	if (player->local_player_index != NONE)
	{
		h2_emblem_local(player->local_player_index, emblem);
		return TRUE;
	}
	return h2_emblem_network_player(&player->network_player_data, emblem);
}

/* ---------- objects */

static void h2_emblem_object_bind(long object_index, struct h2_emblem_look const *look)
{
	long i, slot = NONE;

	for (i = 0; i < H2_EMBLEM_OBJECTS; i++)
	{
		if (h2_emblem.objects[i].known && h2_emblem.objects[i].object_index == object_index)
		{
			slot = i;
			break;
		}
	}
	if (slot == NONE)
	{
		slot = h2_emblem.next_object;
		h2_emblem.next_object = (h2_emblem.next_object + 1) % H2_EMBLEM_OBJECTS;
	}
	h2_emblem.objects[slot].known = TRUE;
	h2_emblem.objects[slot].object_index = object_index;
	h2_emblem.objects[slot].look = *look;
}

struct bitmap_data *h2_emblem_object_bitmap(long object_index)
{
	long i;

	for (i = 0; i < H2_EMBLEM_OBJECTS; i++)
	{
		if (h2_emblem.objects[i].known && h2_emblem.objects[i].object_index == object_index)
			return h2_emblem_texture_get(&h2_emblem.objects[i].look);
	}
	return NULL;
}

void h2_emblem_widget_set(void const *widget, char const *name, struct h2_emblem const *emblem)
{
	long i, slot = NONE;

	for (i = 0; i < H2_EMBLEM_WIDGETS; i++)
	{
		if (h2_emblem.widgets[i].widget == widget)
		{
			slot = i;
			break;
		}
	}
	if (!emblem)
	{
		if (slot != NONE)
			h2_emblem.widgets[slot].widget = NULL;
		return;
	}
	if (slot == NONE)
	{
		slot = h2_emblem.next_widget;
		h2_emblem.next_widget = (h2_emblem.next_widget + 1) % H2_EMBLEM_WIDGETS;
	}
	h2_emblem.widgets[slot].widget = widget;
	h2_emblem.widgets[slot].name = name;
	h2_emblem.widgets[slot].emblem = *emblem;
}

struct bitmap_data *h2_emblem_widget_bitmap(void const *widget, char const *name)
{
	long i;

	for (i = 0; i < H2_EMBLEM_WIDGETS; i++)
	{
		if (h2_emblem.widgets[i].widget == widget)
		{
			if (h2_emblem.widgets[i].name != name)
				return NULL;
			return h2_emblem_bitmap(&h2_emblem.widgets[i].emblem, H2_EMBLEM_WHOLE);
		}
	}
	return NULL;
}

boolean h2_emblem_stand_in(long bitmap_tag)
{
	return bitmap_tag != NONE && bitmap_tag == h2_emblem.stand_in_tag;
}

void h2_emblem_map_unloaded(void)
{
	long i;

	for (i = 0; i < H2_EMBLEM_TEXTURES; i++)
	{
		if (h2_emblem.textures[i].made)
			rasterizer_bitmap_delete(&h2_emblem.textures[i].bitmap);
		h2_emblem.textures[i].made = FALSE;
	}
	memset(h2_emblem.objects, 0, sizeof(h2_emblem.objects));
	h2_emblem.next_object = 0;
	memset(h2_emblem.widgets, 0, sizeof(h2_emblem.widgets));
	h2_emblem.next_widget = 0;
	h2_emblem.stand_in_tag = NONE;
	h2_emblem.level[0] = 0;
}

/* ---------- the armour's colours */

static void h2_emblem_rgb_to_real(unsigned long rgb, real_rgb_color *color)
{
	color->red = (real)((rgb >> 16) & 255) / 255.0f;
	color->green = (real)((rgb >> 8) & 255) / 255.0f;
	color->blue = (real)(rgb & 255) / 255.0f;
}

/* the colour a player's armour wears as its primary: the swatch the player
chose in the one colour list - the profile's (this machine's player), what
its machine told the host (another player of this build: its extended
colour), else the game's record of its colour (Halo CE's eighteen) - exactly,
not the nearest of Halo 2's eighteen, which has no black, no silver and no
sky blue: BLACK came out sage green. FALSE: the player has none known (a
random colour kept, a bot): the appearance's own colour */
static boolean h2_emblem_player_swatch(long player_index, struct player_datum *player, unsigned long *rgb)
{
	short color = NONE;

	if (bots_player_is_bot(player_index))
		return FALSE;
	if (player->local_player_index != NONE)
	{
		long profile_index = player->local_player_index == 0 ? player_models_first_profile() :
			player_ui_get_active_player_profile_index(player->local_player_index);
		struct player_profile profile;

		if (profile_index != NONE && player_profile_get(profile_index, &profile))
			color = player_cosmetics_profile_color(profile_index, profile.primary_color_index);
		/* (RANDOM: the colour the host gave - the one every other machine draws this player in - not
		the appearance's own, which only this machine would show) */
		if (color == NONE)
			color = player->network_player_data.primary_color_index;
	}
	else
	{
		color = player_cosmetics_icon_color(player->network_player_data.icon_index);
		if (color == NONE)
			color = player->network_player_data.primary_color_index;
	}
	if (color < 0 || color >= PLAYER_COSMETICS_COLORS)
		return FALSE;
	*rgb = player_cosmetics_color_rgb(color);
	return TRUE;
}

/* a player's unit its Halo 2 colours (function_7f790: in a team game the
primary the team's and the secondary the player's primary), over what the
biped's variant chose: change colours C and D (h2_biped.c). emblem NULL:
no emblems' pack (its colours and teams' table not made - a card without
textures.dat, a make that failed): the player's own swatch still, the
team's this engine's, the secondary the biped's - never the appearance's
colour for one the player chose (insider.5: violet in CUSTOMIZATION, the
appearance's green on Turf) */
/* (h2_emblem_local_players_ready) the unit each local player's colours
were last given to */
static long h2_emblem_coloured_units[MAXIMUM_LOCAL_PLAYERS] = { NONE, NONE, NONE, NONE };

static void h2_emblem_armour_colours(long player_index, struct player_datum *player, struct h2_emblem const *emblem)
{
	struct object_datum *object = object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit);
	real_rgb_color primary, secondary, own;
	struct h2_emblem clean;
	unsigned long swatch;

	if (!object)
		return;
	if (player->local_player_index >= 0 && player->local_player_index < MAXIMUM_LOCAL_PLAYERS)
		h2_emblem_coloured_units[player->local_player_index] = player->unit_index;
	if (!emblem)
	{
		boolean has_swatch = h2_emblem_player_swatch(player_index, player, &swatch);

		if (has_swatch)
			h2_emblem_rgb_to_real(swatch, &own);
		if (game_engine_has_teams() && player->team_index >= 0)
		{
			game_engine_player_get_change_color(&primary, player_index);
			object->object.base_change_colors[2] = object->object.outgoing_change_colors[2] = primary;
			if (has_swatch)
				object->object.base_change_colors[3] = object->object.outgoing_change_colors[3] = own;
		}
		else if (has_swatch)
			object->object.base_change_colors[2] = object->object.outgoing_change_colors[2] = own;
		{
			static unsigned long said[8];
			long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

			if (absolute >= 0 && absolute < 256 && !(said[absolute >> 5] & (1UL << (absolute & 31))))
			{
				said[absolute >> 5] |= 1UL << (absolute & 31);
				platform_log("h2 emblems: no emblems' pack: player %ld's armour %s (#%06lX)", (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index),
					has_swatch ? "its own colour" : "the appearance's (no colour chosen)", has_swatch ? swatch : 0UL);
			}
		}
		return;
	}
	clean = *emblem;
	if (!h2_emblem_valid(&clean))
		return;
	/* (the player's own primary: its swatch, as chosen; else the appearance's) */
	if (h2_emblem_player_swatch(player_index, player, &swatch))
		h2_emblem_rgb_to_real(swatch, &own);
	else
		h2_emblem_rgb_to_real(h2_emblem.header.colors[clean.colors[0]], &own);
	if (game_engine_has_teams() && player->team_index >= 0 && player->team_index < H2_EMBLEM_TEAMS)
	{
		h2_emblem_rgb_to_real(h2_emblem.header.teams[player->team_index], &primary);
		secondary = own;
	}
	else
	{
		primary = own;
		h2_emblem_rgb_to_real(h2_emblem.header.colors[clean.colors[1]], &secondary);
	}
	object->object.base_change_colors[2] = object->object.outgoing_change_colors[2] = primary;
	object->object.base_change_colors[3] = object->object.outgoing_change_colors[3] = secondary;
}

/* ---------- the frame */

void h2_emblem_update(void)
{
	char const *scenario = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;

	h2_emblem.frame++;
	h2_emblem.made_this_frame = 0;
	h2_emblem_load();
	if (h2_emblem_panel_row != NONE)
	{
		int row = h2_emblem_panel_row, index = h2_emblem_panel_index;
		long profile = player_models_first_profile();
		struct h2_emblem emblem;

		h2_emblem_panel_row = NONE;
		h2_emblem_profile_get(profile, &emblem);
		if (row >= H2_EMBLEM_ROW_PRIMARY && row <= H2_EMBLEM_ROW_EMBLEM_SECONDARY)
			emblem.colors[row - H2_EMBLEM_ROW_PRIMARY] = (uint8_t)index;
		else if (row == H2_EMBLEM_ROW_FOREGROUND)
			emblem.foreground = (uint8_t)index;
		else if (row == H2_EMBLEM_ROW_BACKGROUND)
			emblem.background = (uint8_t)index;
		else if (row == H2_EMBLEM_ROW_MODEL)
			emblem.model = (uint8_t)index;
		else if (row == H2_EMBLEM_ROW_TOGGLE)
			emblem.flags = (uint8_t)((emblem.flags & ~H2_EMBLEM_FLAG_FOREGROUND_A_OFF) | (index ? H2_EMBLEM_FLAG_FOREGROUND_A_OFF : 0));
		{
			boolean separate = row == H2_EMBLEM_ROW_MODEL_FOR_ALL ? index != 0 : h2_emblem_models_separate(profile);

			/* (the check mark on: the Elite is Halo 2's alone, so Halo CE's
			model goes back to the Spartan) */
			if ((row == H2_EMBLEM_ROW_MODEL || row == H2_EMBLEM_ROW_MODEL_FOR_ALL) && !separate && emblem.model)
				player_models_profile_choose(profile, NONE);
			h2_emblem_profile_save_separate(profile, &emblem, separate);
		}
	}
	if (!scenario || strncmp(scenario, "h2\\", 3))
		return;
	if (strcmp(h2_emblem.level, scenario))
	{
		snprintf(h2_emblem.level, sizeof(h2_emblem.level), "%s", scenario);
		h2_emblem.stand_in_tag = tag_loaded('bitm', H2_EMBLEM_STAND_IN);
	}
	/* (Halo 2: emblems in multiplayer alone; the armour's colours the
	player's whether or not the emblems' pack is there: h2_emblem_armour_colours) */
	if (!game_engine_running())
		return;
	if (h2_emblem.stand_in_tag == NONE || !h2_emblem_load())
	{
		struct data_iterator iterator;
		struct player_datum *player;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->unit_index != NONE)
				h2_emblem_armour_colours(iterator.datum_index, player, NULL);
		}
		return;
	}
	{
		struct data_iterator iterator;
		struct player_datum *player;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->unit_index != NONE)
			{
				struct h2_emblem emblem;
				struct h2_emblem_look look;

				h2_emblem_player(iterator.datum_index, &emblem);
				h2_emblem_look_of(&emblem, H2_EMBLEM_ARMOUR, &look);
				h2_emblem_object_bind(player->unit_index, &look);
				h2_emblem_armour_colours(iterator.datum_index, player, &emblem);
			}
		}
	}
}

/* (h2_loading.c's reveal) every local player's unit there - and in a
multiplayer game its colours given this frame or before (h2_emblem_update,
before the frame is drawn): 1; a local player without a unit yet: 0; no
local player: -1 */
int h2_emblem_local_players_ready(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	int locals = 0;

	if (!player_data)
		return -1;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->local_player_index == NONE)
			continue;
		locals++;
		if (player->unit_index == NONE)
			return 0;
		if (game_engine_running() && player->local_player_index >= 0 && player->local_player_index < MAXIMUM_LOCAL_PLAYERS &&
			h2_emblem_coloured_units[player->local_player_index] != player->unit_index)
		{
			return 0;
		}
	}
	return locals ? 1 : -1;
}

/* (h2_loading.c's reveal) the emblems asked for and not made yet, the
frame's quota of makes spent (H2_EMBLEM_MADE_A_FRAME): a running total */
unsigned long h2_emblem_deferred_count(void)
{
	return h2_emblem_deferred;
}

/* ---------- the settings panel */

void h2_emblem_ce_model_chosen(long profile_index)
{
	struct h2_emblem emblem;

	h2_emblem_profile_get(profile_index, &emblem);
	if (emblem.model && !h2_emblem_models_separate(profile_index))
	{
		emblem.model = 0;
		h2_emblem_profile_save(profile_index, &emblem);
	}
}

int h2_emblem_panel_offered(void)
{
	return h2_emblem.loaded;
}

int h2_emblem_panel_count(int row)
{
	return row == H2_EMBLEM_ROW_FOREGROUND ? H2_EMBLEM_FOREGROUNDS : row == H2_EMBLEM_ROW_BACKGROUND ? H2_EMBLEM_BACKGROUNDS :
		row == H2_EMBLEM_ROW_TOGGLE || row == H2_EMBLEM_ROW_MODEL_FOR_ALL ? 2 : row == H2_EMBLEM_ROW_MODEL ? H2_EMBLEM_MODELS : H2_EMBLEM_COLORS;
}

char const *h2_emblem_panel_name(int row, int index)
{
	if (row == H2_EMBLEM_ROW_TOGGLE || row == H2_EMBLEM_ROW_MODEL_FOR_ALL)
		return index ? "Off" : "On";
	if (row == H2_EMBLEM_ROW_MODEL)
		return index ? "Elite" : "Spartan";
	return row == H2_EMBLEM_ROW_FOREGROUND ? h2_emblem_foreground_name((short)index) :
		row == H2_EMBLEM_ROW_BACKGROUND ? h2_emblem_background_name((short)index) : h2_emblem_color_name((short)index);
}

int h2_emblem_panel_chosen(int row)
{
	struct h2_emblem emblem;

	if (row == H2_EMBLEM_ROW_MODEL_FOR_ALL)
		return h2_emblem_models_separate(player_models_first_profile());
	h2_emblem_profile_get(player_models_first_profile(), &emblem);
	if (row >= H2_EMBLEM_ROW_PRIMARY && row <= H2_EMBLEM_ROW_EMBLEM_SECONDARY)
		return emblem.colors[row - H2_EMBLEM_ROW_PRIMARY];
	if (row == H2_EMBLEM_ROW_MODEL)
		return emblem.model;
	return row == H2_EMBLEM_ROW_FOREGROUND ? emblem.foreground : row == H2_EMBLEM_ROW_BACKGROUND ? emblem.background :
		(emblem.flags & H2_EMBLEM_FLAG_FOREGROUND_A_OFF) != 0;
}

void h2_emblem_panel_choose(int row, int index)
{
	if (row >= 0 && row < H2_EMBLEM_ROWS && index >= 0 && index < h2_emblem_panel_count(row))
	{
		h2_emblem_panel_index = index;
		h2_emblem_panel_row = row;
	}
}

/* ---------- names */

static char const *h2_emblem_name(long index)
{
	char const *code = lang_current();
	char const *name;

	if (!h2_emblem_load())
		return "";
	name = h2_emblem.header.names[code && !strcmp(code, "es") ? 1 : 0][index];
	if (!name[0])
		name = h2_emblem.header.names[0][index];
	if (!name[0])
	{
		/* (the file was made without mainmenu.map's lists - the names are read from it once, when the
		file is made: a number, "COLOR 5", rather than a blank row. Two threads read these: the
		menus' and the panel's - each its own rotating strings) */
		static char numbered[2][8][H2_EMBLEM_NAME_SIZE];
		static unsigned long next;
		char *text;
		unsigned long slot = __atomic_fetch_add(&next, 1, __ATOMIC_RELAXED) % 16;

		text = numbered[slot / 8][slot % 8];
		if (index < H2_EMBLEM_FOREGROUNDS)
			snprintf(text, H2_EMBLEM_NAME_SIZE, "%s %ld", T("ICON"), index + 1);
		else if (index < H2_EMBLEM_FOREGROUNDS + H2_EMBLEM_BACKGROUNDS)
			snprintf(text, H2_EMBLEM_NAME_SIZE, "%s %ld", T("BACKGROUND"), index - H2_EMBLEM_FOREGROUNDS + 1);
		else
			snprintf(text, H2_EMBLEM_NAME_SIZE, "%s %ld", T("COLOR"), index - H2_EMBLEM_FOREGROUNDS - H2_EMBLEM_BACKGROUNDS + 1);
		return text;
	}
	return name;
}

char const *h2_emblem_foreground_name(short index)
{
	return index >= 0 && index < H2_EMBLEM_FOREGROUNDS ? h2_emblem_name(index) : "";
}

char const *h2_emblem_background_name(short index)
{
	return index >= 0 && index < H2_EMBLEM_BACKGROUNDS ? h2_emblem_name(H2_EMBLEM_FOREGROUNDS + index) : "";
}

char const *h2_emblem_color_name(short index)
{
	return index >= 0 && index < H2_EMBLEM_COLORS ? h2_emblem_name(H2_EMBLEM_FOREGROUNDS + H2_EMBLEM_BACKGROUNDS + index) : "";
}

unsigned long h2_emblem_team_rgb(short team)
{
	return team >= 0 && team < H2_EMBLEM_TEAMS && h2_emblem_load() ? h2_emblem.header.teams[team] : 0xFFFFFF;
}

unsigned long h2_emblem_color_rgb(short index)
{
	return index >= 0 && index < H2_EMBLEM_COLORS && h2_emblem_load() ? h2_emblem.header.colors[index] : 0;
}
