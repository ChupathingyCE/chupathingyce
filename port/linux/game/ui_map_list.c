/*
UI_MAP_LIST.C

The menus' list of multiplayer maps, filled by the port rather than fixed in
the game (ui_widget_event_handler_functions.c's multiplayer level list): the
Xbox's thirteen maps first, as they were, then the Custom Edition maps
(Halo PC's, played as <name>@ce), each named with [CE], then HaloMD's maps
(Halo PC retail's, played as <name>@md), each named with [MD]: each family
in its folders (halo_map_families.h), in the order of their names, then a
map pack's maps (Halo CE+ and the like, Xbox maps played as <name>@<pack>
from maps_<pack>: halo_map_packs.h), each named with its pack's tag ([CE+]),
in the order of their packs. The builds with the
port's map list (HALO_MAP_LIST) have it, the original Xbox's among them
(the pack's maps alone: it plays no Halo PC map).

A row past the Xbox's has no string or bitmap frame of its own in ui.map:
its text comes from here, by a string list index of
UI_MAP_LIST_STRING_BASE and up (ui_widget.c asks ui_map_list_text when a
text box has one), and its picture by a bitmap frame of
UI_MAP_LIST_PICTURE_BASE and up (ui_widget.c asks ui_map_list_picture). Its
name is two lines in the map list's narrow boxes (the name, then [CE] or
[MD]), and one in the lobby's.

Those are Halo PC's own, read from its ui.map in maps_ce as Halo PC shows
them: the names of its map list (ui\shell\main_menu\mp_map_list), their
descriptions (...\mp_map_select\map_data) and their pictures
(ui\shell\bitmaps\mp_map_grafix, its pixels in maps_ce\bitmaps.map), each
the map's by Halo PC's order of its maps (ce_maps); a map of another's
making is named by its file, with Halo PC's picture for an unknown level.
Without Halo PC's ui.map, the names are ce_maps' and an Xbox map's picture
stands in. A HaloMD map is named as HaloMD's mod list names it (by its
file's name: halomd_map_names.h, written by tools/halomd_map_names.py), or
by its file's name made readable, with Halo PC's picture for an unknown
level.

The server browser (browser_screen.c) names a listed game's Custom Edition
or HaloMD map by the same (ui_map_list_family_name), on every build: the
builds without such maps (HALO_CUSTOM_EDITION) still name one. On those with
them it has Halo PC's picture of the map (ui_map_list_family_picture) and
asks whether the map is in its family's folders (ui_map_list_family_present).
*/

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"

#include <stdio.h>
#include <string.h>

#include "halo_map_families.h"
#include "halo_join_reasons.h"
#include "halo_ui_map_list.h"
#include "halomd_map_names.h"
#include "mcc_maps.h"

/* Halo PC's multiplayer maps, in the order of its map list (its strings and
pictures), and an Xbox map of the same kind whose picture stands in
without Halo PC's ui.map */
static struct
{
	char const *file;
	wchar_t const *name;
	short xbox_picture_index;
} const ce_maps[] =
{
	{ "beavercreek", L"Battle Creek", 0 },
	{ "sidewinder", L"Sidewinder", 1 },
	{ "damnation", L"Damnation", 2 },
	{ "ratrace", L"Rat Race", 3 },
	{ "prisoner", L"Prisoner", 4 },
	{ "hangemhigh", L"Hang 'Em High", 5 },
	{ "chillout", L"Chill Out", 6 },
	{ "carousel", L"Derelict", 7 },
	{ "boardingaction", L"Boarding Action", 8 },
	{ "bloodgulch", L"Blood Gulch", 9 },
	{ "wizard", L"Wizard", 10 },
	{ "putput", L"Chiron TL34", 11 },
	{ "longest", L"Longest", 12 },
	{ "icefields", L"Ice Fields", 1 },
	{ "deathisland", L"Death Island", 1 },
	{ "dangercanyon", L"Danger Canyon", 9 },
	{ "infinity", L"Infinity", 9 },
	{ "timberland", L"Timberland", 9 },
	{ "gephyrophobia", L"Gephyrophobia", 2 },
};

/* a map file's place in ce_maps (by its name, any case), or NONE */
static long ce_map_index(
	char const *file)
{
	long index;

	for (index = 0; index < (long)NUMBEROF(ce_maps); index++)
	{
		char const *a = file;
		char const *b = ce_maps[index].file;

		while (*a && (*a | 0x20) == *b)
		{
			a++;
			b++;
		}
		if (!*a && !*b)
			return index;
	}
	return NONE;
}

/* a HaloMD map's name, by its file's name: as HaloMD's mod list names it,
or another version's of the same map (<map>_<version>), or NULL */
static wchar_t const *halomd_map_name(
	char const *file)
{
	size_t stem_length = strlen(file);
	long index;

	for (index = 0; index < (long)NUMBEROF(halomd_map_names); index++)
	{
		if (!_stricmp(file, halomd_map_names[index].file))
			return halomd_map_names[index].name;
	}
	/* (another version's: the name without its _<version>) */
	while (stem_length && file[stem_length - 1] >= '0' && file[stem_length - 1] <= '9')
		stem_length--;
	if (!stem_length || stem_length == strlen(file) || file[stem_length - 1] != '_')
		return NULL;
	for (index = 0; index < (long)NUMBEROF(halomd_map_names); index++)
	{
		char const *other = halomd_map_names[index].file;
		size_t other_length = strlen(other);

		if (other_length > stem_length && !_strnicmp(file, other, stem_length) &&
			strspn(other + stem_length, "0123456789") == other_length - stem_length)
		{
			return halomd_map_names[index].name;
		}
	}
	return NULL;
}

/* a map file's name made readable, as the game list's web pages make it:
its underscores, dots and dashes spaces, each word begun with a capital
(hugeass_v2: Hugeass V2) */
static void ce_map_tidy_name(
	char const *file,
	wchar_t *name,
	long size)
{
	long length = 0;
	boolean space = FALSE;

	for (; *file && length < size - 1; file++)
	{
		unsigned char character = (unsigned char)*file;

		if (character == '_' || character == '.' || character == '-' || character == ' ' || character == '\t')
		{
			space = length > 0;
			continue;
		}
		if (space)
		{
			if (length >= size - 2)
				break;
			name[length++] = ' ';
			space = FALSE;
		}
		if ((length == 0 || name[length - 1] == ' ') && character >= 'a' && character <= 'z')
			character = (unsigned char)(character - 'a' + 'A');
		name[length++] = (wchar_t)character;
	}
	name[length] = 0;
}

#ifdef HALO_MAP_LIST

#include "halo_map_packs.h"

#ifdef HALO_CUSTOM_EDITION
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "rasterizer/rasterizer_swizzle.h"

#include <xtl.h>
#endif
#include <stdlib.h>

/* ---------- constants */

enum
{
	XBOX_MAP_COUNT = 13,
	/* (the Xbox's thirteen, then the Custom Edition, HaloMD, Halo PC and
	map packs' maps found, up to this many rows in all; on the original
	Xbox, which has a pack's alone, its thirteen and as many again) */
#ifdef HALO_CONSOLE_MAP_LIST_ROWS
	MAXIMUM_MAP_LIST = HALO_CONSOLE_MAP_LIST_ROWS,
#else
	/* (a computer's: a large Custom Edition folder and the whole MCC
	catalog, MCC_MAPS_MAXIMUM, together; 256 left the MCC maps, listed
	last, out) */
	MAXIMUM_MAP_LIST = 2560,
#endif
	MAP_NAME_LENGTH = 64,
	DISPLAY_NAME_LENGTH = 48,
	DESCRIPTION_LENGTH = 160,
	/* text boxes' string list indices from here are this list's: the row
	times the kinds of string, plus its string's kind */
	UI_MAP_LIST_STRING_BASE = 0x4000,
	UI_MAP_LIST_STRINGS_PER_ROW = NUMBER_OF_UI_MAP_LIST_STRINGS,
	/* the characters of a line of the description pane's name (Halo PC's
	box over the picture shows about this many of the menus' font) */
	PANE_NAME_LINE = 13,
	PANE_NAME_LINES = 2,
	/* and of a line of the map lists' cards' and boxes' names */
	CARD_NAME_LINE = 15,
	/* bitmap frames from here are Halo PC's map pictures */
	UI_MAP_LIST_PICTURE_BASE = 0x4000,
	/* and from here the campaign's level pictures (ui.map's sp_levels, a
	frame for each level) */
	UI_MAP_LIST_CAMPAIGN_PICTURE_BASE = 0x6000,
};

/* (every row's strings have string list indices, shorts) */
typedef char ui_map_list_string_indices_fit[
	UI_MAP_LIST_STRING_BASE + (long)MAXIMUM_MAP_LIST * UI_MAP_LIST_STRINGS_PER_ROW <= 0x7FFF ? 1 : -1];

#ifdef HALO_CUSTOM_EDITION
enum
{
	/* Halo PC's ui.map: its cache file's version, the address its tags are
	read at, and its map list's entry for an unknown level */
	CE_CACHE_VERSION = 609,
	CE_TAGS_ADDRESS = 0x40440000,
	CE_UNKNOWN_LEVEL = 19,
	MAXIMUM_CE_STRINGS = 24,
	MAXIMUM_CE_PICTURES = 24,
	/* its bitmaps' pixels kept in bitmaps.map */
	CE_BITMAP_EXTERNAL_FLAG = 0x100,
	/* (the bitmaps' type of a 2D texture, as the game's) */
	CE_BITMAP_TYPE_2D = 0,
};
#endif

/* ---------- structures */

struct ui_map_entry
{
	char map_name[MAP_NAME_LENGTH];
	wchar_t display_name[DISPLAY_NAME_LENGTH];
	wchar_t lobby_name[DISPLAY_NAME_LENGTH];
	wchar_t pane_name[DISPLAY_NAME_LENGTH];
	wchar_t description[DESCRIPTION_LENGTH];
	/* its row among the Xbox's (their strings and bitmap frames), or NONE */
	short xbox_index;
	short picture_index;
	/* its game's type in the lobby, for a row that has one of its own (a
	campaign level's: co-op), else NULL */
	wchar_t const *game_type;
	/* where it comes from (_ui_map_list_source_*) */
	short source;
	/* when its file was written (0: not known), once asked for: the view's
	Recently added */
	boolean added_known;
	long long added;
};

/* ---------- globals */

/* (the entries in the order they are found; the rows are the view's order
of them: ui_map_list_view) */
static struct ui_map_entry ui_map_list[MAXIMUM_MAP_LIST];
static char *ui_map_list_names_array[MAXIMUM_MAP_LIST];
static long ui_map_list_count_value;
static short ui_map_list_view[MAXIMUM_MAP_LIST];
/* the entries that are multiplayer maps, and those to the campaign's
co-op levels' end, as found */
static long ui_map_list_found_multiplayer, ui_map_list_found_campaign_end;
/* the source the entries being added come from */
static short ui_map_list_adding_source;
/* the maps found that the list had no room for, this refresh, and the
first of them (logged: ui_map_list_refresh) */
static long ui_map_list_left_out;
static char ui_map_list_first_left_out[MAP_NAME_LENGTH];
/* the rows that are multiplayer maps (the campaign's levels come after
them, for hosting network co-op: ui_map_list_count_with_campaign) */
static long ui_map_list_multiplayer_count;
/* the rows up to the campaign's co-op levels' end: the MCC catalog's
campaign maps follow them, found by name only (New Game's SINGLEPLAYER rows
name them: menu_functions.c), never a co-op game's */
static long ui_map_list_campaign_end;

#ifdef HALO_CUSTOM_EDITION
/* ---------- prototypes */

char const *cache_files_map_directory(void);
long bitmap_format_to_d3d_format(short format, word flags);

/* what Halo PC's ui.map has of its map list, read once: its names,
descriptions and pictures (about 11 KB) allocated when it is read, so a
console with no Custom Edition maps keeps that memory; each count is 0
while they aren't */
struct ce_ui_lists
{
	wchar_t names[MAXIMUM_CE_STRINGS][DISPLAY_NAME_LENGTH];
	wchar_t descriptions[MAXIMUM_CE_STRINGS][DESCRIPTION_LENGTH];
	struct bitmap_data pictures[MAXIMUM_CE_PICTURES];
};

static struct
{
	boolean read;
	long name_count;
	long description_count;
	long picture_count;
	struct ce_ui_lists *lists;
} ce_ui;

/* Halo PC's ui.map's tags, while they are read (ce_ui_read) */
static byte *ce_tags;
static unsigned long ce_tags_size;
#endif

/* ---------- private code */

/* (the game's wide characters are 16 bits: not the C library's) */
static void wide_copy(
	wchar_t *destination,
	long size,
	wchar_t const *source)
{
	long index;

	for (index = 0; index < size - 1 && source[index]; index++)
		destination[index] = source[index];
	destination[index] = 0;
}

static void wide_append(
	wchar_t *destination,
	long size,
	wchar_t const *source)
{
	long length = 0;

	while (length < size - 1 && destination[length])
		length++;
	wide_copy(destination + length, size - length, source);
}

/* (a name's bytes as wide characters) */
static void wide_from_text(
	wchar_t *destination,
	long size,
	char const *source)
{
	long index;

	for (index = 0; index < size - 1 && source[index]; index++)
		destination[index] = (wchar_t)(unsigned char)source[index];
	destination[index] = 0;
}

#ifdef HALO_CUSTOM_EDITION
static boolean file_read(
	HANDLE file,
	unsigned long offset,
	void *buffer,
	unsigned long size)
{
	unsigned long bytes_read = 0;

	if (SetFilePointer(file, (long)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return FALSE;
	return ReadFile(file, buffer, size, &bytes_read, NULL) && bytes_read == size;
}

/* an address of Halo PC's ui.map's tags made a pointer to size bytes of
them, or NULL outside them */
static void *ce_tags_pointer(
	unsigned long address,
	unsigned long size)
{
	if (address < CE_TAGS_ADDRESS || address - CE_TAGS_ADDRESS > ce_tags_size ||
		size > ce_tags_size - (address - CE_TAGS_ADDRESS))
	{
		return NULL;
	}
	return ce_tags + (address - CE_TAGS_ADDRESS);
}

/* the data of the tag of a group and name (size bytes of it in the tags), or NULL */
static byte *ce_tag_find(
	unsigned long group_tag,
	char const *name,
	unsigned long size)
{
	unsigned long *header = ce_tags_pointer(CE_TAGS_ADDRESS, 0x10);
	unsigned long *instances;
	unsigned long index;

	if (!header)
		return NULL;
	/* (its instances' address, ..., their count: no more than its tags
hold, so that the size does not overflow) */
	if (header[3] > ce_tags_size / 0x20)
		return NULL;
	instances = ce_tags_pointer(header[0], header[3] * 0x20);
	if (!instances)
		return NULL;
	for (index = 0; index < header[3]; index++)
	{
		/* (an instance: its group, ..., its name, its data) */
		unsigned long *instance = instances + index * 8;
		char const *instance_name = ce_tags_pointer(instance[4], strlen(name) + 1);

		if (instance[0] == group_tag && instance_name && !memcmp(instance_name, name, strlen(name) + 1))
			return ce_tags_pointer(instance[5], size);
	}
	return NULL;
}

/* a unicode string list's strings, each up to length characters: how many */
static long ce_string_list_read(
	char const *name,
	wchar_t *strings,
	long length,
	long maximum_count)
{
	unsigned long *block = (unsigned long *)ce_tag_find('ustr', name, 0x0c);
	unsigned long *references;
	long count, index;

	if (!block)
		return 0;
	count = MIN((long)block[0], maximum_count);
	references = ce_tags_pointer(block[1], count * 0x14);
	if (!references)
		return 0;
	for (index = 0; index < count; index++)
	{
		/* (a tag data: its size, ..., its address) */
		unsigned long size = references[index * 5];
		unsigned short const *text = ce_tags_pointer(references[index * 5 + 3], size);
		wchar_t *string = strings + index * length;
		long character;

		for (character = 0; text && character < length - 1 && character < (long)(size / 2) && text[character];
			character++)
		{
			string[character] = text[character];
		}
		string[character] = 0;
	}
	return count;
}

/* the map list's pictures: each made a bitmap of its own, its pixels and its
Direct3D texture in contiguous memory, as the texture cache would make it */
static void ce_pictures_read(
	HANDLE ui_file)
{
	byte *group = ce_tag_find('bitm', "ui\\shell\\bitmaps\\mp_map_grafix", 0x6c);
	HANDLE bitmaps_file = INVALID_HANDLE_VALUE;
	unsigned long *block;
	byte *elements;
	long count, index;

	if (!group)
		return;
	/* (its bitmaps block) */
	block = (unsigned long *)(group + 0x60);
	count = MIN((long)block[0], MAXIMUM_CE_PICTURES);
	elements = ce_tags_pointer(block[1], count * 0x30);
	if (!elements)
		return;
	for (index = 0; index < count; index++)
	{
		byte *element = elements + index * 0x30;
		struct bitmap_data *bitmap = &ce_ui.lists->pictures[index];
		unsigned short flags = *(unsigned short *)(element + 0xe);
		unsigned long pixels_offset = *(unsigned long *)(element + 0x18);
		unsigned long pixels_size = *(unsigned long *)(element + 0x1c);
		unsigned long allocated_size;
		HANDLE file = ui_file;
		D3DBaseTexture *texture;
		void *pixels;

		memset(bitmap, 0, sizeof(*bitmap));
		bitmap->signature = *(unsigned long *)element;
		bitmap->width = *(short *)(element + 0x4);
		bitmap->height = *(short *)(element + 0x6);
		bitmap->depth = *(short *)(element + 0x8);
		bitmap->type = *(short *)(element + 0xa);
		bitmap->format = *(short *)(element + 0xc);
		/* (Halo PC's own flags dropped: not cached, its pixels here) */
		bitmap->flags = flags & 0x3f;
		bitmap->mipmap_count = *(short *)(element + 0x14);
		bitmap->pixels_size = pixels_size;
		bitmap->tag_index = NONE;
		bitmap->cache_block_index = NONE;
		/* (a 2D bitmap the game draws, of a format with a Direct3D one:
		bitmap_format_to_d3d_format asserts there is) */
		if (bitmap->type != CE_BITMAP_TYPE_2D || bitmap->width <= 0 || bitmap->height <= 0 ||
			pixels_size == 0 || pixels_size > 0x100000 || !bitmap_verify(bitmap, FALSE) ||
			bitmap->format == 4 || bitmap->format == 5 || bitmap->format == 7 || bitmap->format == 12 ||
			bitmap->format == 13 || (bitmap->flags & 0x10))
		{
			break;
		}
		/* (as much as the renderer reads of its size and format, as the
		texture cache gives it: the pixels the file has, the rest zero) */
		allocated_size = MAX(pixels_size, (unsigned long)rasterizer_xbox_bitmap_get_pixel_data_size(bitmap));
		if (allocated_size > 0x400000)
			break;
		if (flags & CE_BITMAP_EXTERNAL_FLAG)
		{
			if (bitmaps_file == INVALID_HANDLE_VALUE)
			{
				char path[512];

				map_family_resource("bitmaps", path, sizeof(path));
				bitmaps_file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				if (bitmaps_file == INVALID_HANDLE_VALUE)
					break;
			}
			file = bitmaps_file;
		}
		pixels = XPhysicalAlloc(allocated_size, -1, 0, PAGE_READWRITE);
		texture = XPhysicalAlloc(sizeof(D3DBaseTexture), -1, 0, PAGE_READWRITE);
		if (pixels)
			memset(pixels, 0, allocated_size);
		if (!pixels || !texture || !file_read(file, pixels_offset, pixels, pixels_size))
		{
			if (pixels)
				XPhysicalFree(pixels);
			if (texture)
				XPhysicalFree(texture);
			break;
		}
		memset(texture, 0, sizeof(*texture));
		/* (texture_cache_initialize_hardware_format's, laid out as Halo PC
		lays out its pixels) */
		texture->Common = D3DCOMMON_TYPE_TEXTURE | 1 | D3DCOMMON_PORT_PC_LAYOUT;
		texture->Format =
			(floor_log2(bitmap->height) << D3DFORMAT_VSIZE_SHIFT) |
			(floor_log2(bitmap->width) << D3DFORMAT_USIZE_SHIFT) |
			(bitmap_format_to_d3d_format(bitmap->format, bitmap->flags) << D3DFORMAT_FORMAT_SHIFT) |
			(2 << D3DFORMAT_DIMENSION_SHIFT) |
			((rasterizer_xbox_bitmap_get_max_mipmap_count(bitmap) + 1) << D3DFORMAT_MIPMAP_SHIFT) |
			D3DFORMAT_BORDERSOURCE_COLOR |
			D3DFORMAT_DMACHANNEL_A;
		IDirect3DBaseTexture8_Register(texture, pixels);
		bitmap->base_address = xbox_address(pixels);
		bitmap->hardware_format = xbox_address(texture);
		ce_ui.picture_count = index + 1;
	}
	if (bitmaps_file != INVALID_HANDLE_VALUE)
		CloseHandle(bitmaps_file);
}

/* what Halo PC's ui.map (maps_ce\ui.map) has of its map list, read the first
time it is asked for */
static void ce_ui_read(
	void)
{
	char path[512];
	unsigned long header[6];
	HANDLE file;

	if (ce_ui.read)
		return;
	ce_ui.read = TRUE;
	map_family_resource("ui", path, sizeof(path));
	file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return;
	/* ('head', its version, its size, ..., its tags' offset and size) */
	if (file_read(file, 0, header, sizeof(header)) && header[0] == 'head' && header[1] == CE_CACHE_VERSION &&
		header[5] >= 0x10 && header[5] <= 0x4000000)
	{
		ce_tags = malloc(header[5]);
		ce_tags_size = header[5];
		ce_ui.lists = ce_tags ? calloc(1, sizeof(*ce_ui.lists)) : NULL;
		if (ce_ui.lists && file_read(file, header[4], ce_tags, header[5]))
		{
			ce_ui.name_count = ce_string_list_read("ui\\shell\\main_menu\\mp_map_list", ce_ui.lists->names[0],
				DISPLAY_NAME_LENGTH, MAXIMUM_CE_STRINGS);
			ce_ui.description_count = ce_string_list_read(
				"ui\\shell\\main_menu\\multiplayer_type_select\\mp_map_select\\map_data", ce_ui.lists->descriptions[0],
				DESCRIPTION_LENGTH, MAXIMUM_CE_STRINGS);
			ce_pictures_read(file);
		}
		free(ce_tags);
		ce_tags = NULL;
		ce_tags_size = 0;
	}
	CloseHandle(file);
}
#endif

static void add_entry(
	char const *map_name,
	wchar_t const *display_name,
	wchar_t const *lobby_name,
	wchar_t const *description,
	short xbox_index,
	short picture_index)
{
	struct ui_map_entry *entry;

	if (ui_map_list_count_value >= MAXIMUM_MAP_LIST)
	{
		if (!ui_map_list_left_out++)
			snprintf(ui_map_list_first_left_out, sizeof(ui_map_list_first_left_out), "%s", map_name);
		return;
	}
	entry = &ui_map_list[ui_map_list_count_value];
	snprintf(entry->map_name, sizeof(entry->map_name), "%s", map_name);
	wide_copy(entry->display_name, DISPLAY_NAME_LENGTH, display_name);
	wide_copy(entry->lobby_name, DISPLAY_NAME_LENGTH, lobby_name);
	wide_copy(entry->pane_name, DISPLAY_NAME_LENGTH, lobby_name);
	wide_copy(entry->description, DESCRIPTION_LENGTH, description);
	entry->xbox_index = xbox_index;
	entry->picture_index = picture_index;
	entry->game_type = NULL;
	entry->source = ui_map_list_adding_source;
	entry->added_known = FALSE;
	entry->added = 0;
	ui_map_list_count_value++;
}

/* a map pack's map's row (<file>@<pack>, maps_<pack>: halo_map_packs.h):
named as the library of known Xbox mod maps or the pack's maplist.txt names
it, marked with its pack's tag ([CE+]), with the picture of the Xbox's map
of its file's name (the pack's own are in its ui.map, not read) or, in the
pack's own menus, that row itself (map_pack_menus: its ui.map has the
pack's strings and pictures there) */
static void add_pack_entry(
	char const *map_name,
	short xbox_index)
{
	char file[MAP_NAME_LENGTH], pack[MAP_PACK_ID_LENGTH + 1], tag[16];
	char name[DISPLAY_NAME_LENGTH], title[DISPLAY_NAME_LENGTH], text[DESCRIPTION_LENGTH];
	wchar_t wide[DISPLAY_NAME_LENGTH];
	wchar_t mark[16];
	wchar_t display_name[DISPLAY_NAME_LENGTH];
	wchar_t lobby_name[DISPLAY_NAME_LENGTH];
	wchar_t description[DESCRIPTION_LENGTH];
	short picture_index = xbox_index != NONE ? xbox_index : 9;
	short known;

	map_family_parse(map_name, file, sizeof(file));
	if (!map_family_pack_id(map_name, pack, sizeof(pack)))
		return;
	for (known = 0; xbox_index == NONE && known < 13; known++)
	{
		if (!_stricmp(file, ce_maps[known].file))
			picture_index = ce_maps[known].xbox_picture_index;
	}
	map_pack_names(pack, file, 0, name, DISPLAY_NAME_LENGTH - 8, title, sizeof(title));
	map_pack_tag(pack, tag, sizeof(tag));
	wide_from_text(mark, NUMBEROF(mark), tag);
	snprintf(text, sizeof(text), "%s\r\n\r\n%s.map in maps_%s", title, file, pack);
	/* (a pack the library doesn't know: "the maps_foo pack" begun with a capital) */
	if (text[0] >= 'a' && text[0] <= 'z')
		text[0] = (char)(text[0] - 'a' + 'A');
	wide_from_text(wide, NUMBEROF(wide), name);
	wide_from_text(description, NUMBEROF(description), text);
	wide_copy(lobby_name, DISPLAY_NAME_LENGTH, wide);
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, L" [");
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, mark);
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, L"]");
	wide_copy(display_name, DISPLAY_NAME_LENGTH, wide);
	wide_append(display_name, DISPLAY_NAME_LENGTH, L"\r\n[");
	wide_append(display_name, DISPLAY_NAME_LENGTH, mark);
	wide_append(display_name, DISPLAY_NAME_LENGTH, L"]");
	add_entry(map_name, display_name, lobby_name, description, xbox_index, picture_index);
}

#ifdef HALO_CUSTOM_EDITION
/* a Custom Edition or HaloMD map's details from its own file, where its
row has no description of Halo PC's: how many spawn points its scenario
has and whether it places vehicles (the cache file's header and tag index,
as Halo PC lays them out); FALSE if the file is not one */
enum
{
	MAP_FILE_HEADER_SIZE = 0x800,
	MAP_FILE_TAG_DATA_OFFSET = 0x10,
	MAP_FILE_TAG_BASE_ADDRESS = 0x40440000,
	MAP_FILE_TAG_SIZE = 0x20,
	MAP_FILE_TAG_DATA_ADDRESS = 0x14,
	/* (the scenario's blocks: their counts) */
	SCENARIO_VEHICLES_COUNT = 0x240,
	SCENARIO_PLAYER_STARTING_LOCATIONS_COUNT = 0x354,
	SCENARIO_READ_SIZE = 0x358,
};

static unsigned long little_long(unsigned char const *bytes)
{
	return bytes[0] | (bytes[1] << 8) | ((unsigned long)bytes[2] << 16) | ((unsigned long)bytes[3] << 24);
}

static boolean map_file_details(
	short family,
	char const *file,
	long *spawns,
	long *vehicles)
{
	char path[512];
	unsigned char header[MAP_FILE_HEADER_SIZE], index[16], tag[MAP_FILE_TAG_SIZE];
	unsigned char scenario[SCENARIO_READ_SIZE];
	unsigned long tag_data, tag_array, scenario_index, scenario_address;
	boolean read = FALSE;
	FILE *stream;

	if (!map_family_find(family, file, path, sizeof(path)) || (stream = fopen(path, "rb")) == NULL)
		return FALSE;
	if (fread(header, sizeof(header), 1, stream) == 1 && !memcmp(header, "daeh", 4))
	{
		tag_data = little_long(header + MAP_FILE_TAG_DATA_OFFSET);
		if (!fseek(stream, (long)tag_data, SEEK_SET) && fread(index, sizeof(index), 1, stream) == 1)
		{
			tag_array = little_long(index) - MAP_FILE_TAG_BASE_ADDRESS;
			scenario_index = little_long(index + 4) & 0xFFFF;
			if (scenario_index < little_long(index + 12) &&
				!fseek(stream, (long)(tag_data + tag_array + scenario_index * MAP_FILE_TAG_SIZE), SEEK_SET) &&
				fread(tag, sizeof(tag), 1, stream) == 1 && !memcmp(tag, "rncs", 4))
			{
				scenario_address = little_long(tag + MAP_FILE_TAG_DATA_ADDRESS) - MAP_FILE_TAG_BASE_ADDRESS;
				if (!fseek(stream, (long)(tag_data + scenario_address), SEEK_SET) &&
					fread(scenario, sizeof(scenario), 1, stream) == 1)
				{
					*spawns = (long)little_long(scenario + SCENARIO_PLAYER_STARTING_LOCATIONS_COUNT);
					*vehicles = (long)little_long(scenario + SCENARIO_VEHICLES_COUNT);
					read = *spawns >= 0 && *spawns < 4096 && *vehicles >= 0 && *vehicles < 4096;
				}
			}
		}
	}
	fclose(stream);
	return read;
}

/* a Custom Edition or HaloMD map's row: its file's name (without its
suffix or .map) */
static void add_pc_entry(
	short family,
	char const *file)
{
	char map_name[MAP_NAME_LENGTH];
	wchar_t name[DISPLAY_NAME_LENGTH];
	wchar_t display_name[DISPLAY_NAME_LENGTH];
	wchar_t lobby_name[DISPLAY_NAME_LENGTH];
	wchar_t details[DESCRIPTION_LENGTH];
	wchar_t const *description = L"A Halo Custom\r\nEdition map";
	wchar_t const *mark = L"CE";
	long ce_index = CE_UNKNOWN_LEVEL;
	short picture_index = 9;
	short known;

	name[0] = 0;
	if (family == _map_family_halomd)
	{
		wchar_t const *known_name = halomd_map_name(file);

		description = L"A HaloMD map";
		mark = L"MD";
		if (known_name)
			wide_copy(name, DISPLAY_NAME_LENGTH - 7, known_name);
	}
	else if (family == _map_family_halo_pc)
	{
		/* (Halo PC retail's maps are the stock ones Custom Edition has
		too, named and pictured as those) */
		description = L"A Halo PC map";
		mark = L"PC";
	}
	/* (HaloMD's copies of Halo PC's own maps are those too, by HaloMD's name
	where its mod list has one) */
	for (known = 0; known < (short)NUMBEROF(ce_maps); known++)
	{
		if (!_stricmp(file, ce_maps[known].file))
		{
			ce_index = known;
			picture_index = ce_maps[known].xbox_picture_index;
			if (!name[0])
			{
				wide_copy(name, DISPLAY_NAME_LENGTH, ce_maps[known].name);
				if (known < ce_ui.name_count && ce_ui.lists->names[known][0])
					wide_copy(name, DISPLAY_NAME_LENGTH, ce_ui.lists->names[known]);
			}
			if (known < ce_ui.description_count)
				description = ce_ui.lists->descriptions[known];
			break;
		}
	}
	if (ce_index == CE_UNKNOWN_LEVEL)
	{
		/* (a map of another's making: what its own file says, laid out as
		the Xbox's descriptions are: the tagline, the players, vehicles) */
		long spawns, vehicles;

		wide_copy(details, DESCRIPTION_LENGTH, description);
		if (map_file_details(family, file, &spawns, &vehicles))
		{
			char line[48];
			wchar_t wide[48];
			long character;

			if (spawns > 0)
			{
				snprintf(line, sizeof(line), "\r\n\r\n%ld spawn points", spawns);
				for (character = 0; line[character] && character < (long)NUMBEROF(wide) - 1; character++)
					wide[character] = (wchar_t)line[character];
				wide[character] = 0;
				wide_append(details, DESCRIPTION_LENGTH, wide);
			}
			if (vehicles > 0)
				wide_append(details, DESCRIPTION_LENGTH, L"\r\n\r\nSupports vehicles");
		}
		description = details;
	}
	if (!name[0])
	{
		/* (a map of another's making: its file's name) */
		long character;

		for (character = 0; file[character] && character < DISPLAY_NAME_LENGTH - 7; character++)
			name[character] = (wchar_t)(unsigned char)file[character];
		name[character] = 0;
	}
	if (ce_index < ce_ui.picture_count)
		picture_index = (short)(UI_MAP_LIST_PICTURE_BASE + ce_index);
	wide_copy(lobby_name, DISPLAY_NAME_LENGTH, name);
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, L" [");
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, mark);
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, L"]");
	wide_copy(display_name, DISPLAY_NAME_LENGTH, name);
	wide_append(display_name, DISPLAY_NAME_LENGTH, L"\r\n[");
	wide_append(display_name, DISPLAY_NAME_LENGTH, mark);
	wide_append(display_name, DISPLAY_NAME_LENGTH, L"]");
	snprintf(map_name, sizeof(map_name), "%s%s", file, map_family_suffix(family));
	add_entry(map_name, display_name, lobby_name, description, NONE, picture_index);
}
#endif

#ifdef HALO_CUSTOM_EDITION
/* an MCC map's row (Halo: The Master Chief Collection's, cache version 13):
listed so that a player sees why it is not played, its description the
sentence (MAP_REASON_MCC_UNSUPPORTED) and picking it for a game is refused
at load with the same one (cache_files_windows.c) */
static void add_mcc_entry(
	short family,
	char const *file)
{
	wchar_t name[DISPLAY_NAME_LENGTH];
	wchar_t display_name[DISPLAY_NAME_LENGTH];
	wchar_t lobby_name[DISPLAY_NAME_LENGTH];
	wchar_t description[DESCRIPTION_LENGTH];
	char map_name[MAP_NAME_LENGTH];
	char const *reason = MAP_REASON_MCC_UNSUPPORTED;
	long character;

	for (character = 0; file[character] && character < DISPLAY_NAME_LENGTH - 12; character++)
		name[character] = (wchar_t)(unsigned char)file[character];
	name[character] = 0;
	wide_copy(lobby_name, DISPLAY_NAME_LENGTH, name);
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, L" [MCC]");
	wide_copy(display_name, DISPLAY_NAME_LENGTH, name);
	wide_append(display_name, DISPLAY_NAME_LENGTH, L"\r\n[MCC]");
	for (character = 0; reason[character] && character < DESCRIPTION_LENGTH - 1; character++)
		description[character] = (wchar_t)(unsigned char)reason[character];
	description[character] = 0;
	snprintf(map_name, sizeof(map_name), "%s%s", file, map_family_suffix(family));
	add_entry(map_name, display_name, lobby_name, description, NONE, 9);
}
#endif

/* a name broken between words into at most line_count lines of
line_characters (the description pane's name: PANE_NAME_LINE and
PANE_NAME_LINES; a card's first line: CARD_NAME_LINE); a word longer than a
line keeps its own line, whole, and words past the last line are left for
"..." after it */
static void pane_name_wrap(
	wchar_t *pane_name,
	wchar_t const *name,
	long line_characters,
	long line_count)
{
	long out = 0, line = 0, at = 0, lines = 1;

	while (name[at] == ' ')
		at++;
	while (name[at] && out < DISPLAY_NAME_LENGTH - 3)
	{
		long word = 0, index;

		while (name[at + word] && name[at + word] != ' ')
			word++;
		if (line && line + 1 + word > line_characters)
		{
			if (lines == line_count)
			{
				for (index = 0; index < 3 && out < DISPLAY_NAME_LENGTH - 1; index++)
					pane_name[out++] = '.';
				break;
			}
			lines++;
			pane_name[out++] = '\r';
			pane_name[out++] = '\n';
			line = 0;
		}
		else if (line)
		{
			pane_name[out++] = ' ';
			line++;
		}
		for (index = 0; index < word && out < DISPLAY_NAME_LENGTH - 1; index++)
			pane_name[out++] = name[at + index];
		line += word;
		at += word;
		while (name[at] == ' ')
			at++;
	}
	pane_name[out] = 0;
}

/* an MCC map's row from the MCC catalog (mcc_maps.c: maps_mcc, its
packages and the subscribed Workshop items): named as the catalog names it,
marked [MCC] as a Custom Edition map is marked [CE], its map name the
catalog's level name (mcc_maps\<package>\<level>), with Halo PC's picture
for an unknown level. Played alone or in split screen; a network game
refuses it as it refuses any MCC map */
static void add_mcc_catalog_entry(
	short index)
{
	wchar_t name[DISPLAY_NAME_LENGTH];
	wchar_t display_name[DISPLAY_NAME_LENGTH];
	wchar_t lobby_name[DISPLAY_NAME_LENGTH];
	wchar_t description[DESCRIPTION_LENGTH];
	wchar_t const *own = mcc_maps_description(index);
	char const *level = mcc_maps_level_name(index);

	if (!level || strlen(level) >= MAP_NAME_LENGTH)
		return;
	wide_copy(name, DISPLAY_NAME_LENGTH - 7, mcc_maps_name(index));
	wide_copy(lobby_name, DISPLAY_NAME_LENGTH, name);
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, L" [MCC]");
	/* (the map lists' cards: the map's whole name, on two lines when it
	needs them, then [MCC] after it where it fits, as a Custom Edition
	map's [CE]; a name of two full lines keeps its words, not the mark) */
	pane_name_wrap(display_name, name, CARD_NAME_LINE, 2);
	{
		wchar_t const *last = display_name;
		wchar_t const *at;

		for (at = display_name; *at; at++)
		{
			if (*at == '\n')
				last = at + 1;
		}
		if (last == display_name)
			wide_append(display_name, DISPLAY_NAME_LENGTH, L"\r\n[MCC]");
		else if ((long)(at - last) + 6 <= CARD_NAME_LINE)
			wide_append(display_name, DISPLAY_NAME_LENGTH, L" [MCC]");
	}
	wide_copy(description, DESCRIPTION_LENGTH, own && own[0] ? own : L"A Halo MCC map");
	/* (its own picture, its package's or Steam's picture of its Workshop
	item, by its catalog index: ui_widget.c draws it, or Halo PC's picture
	for an unknown level when there is none, ui_map_list_unknown_picture) */
	add_entry(level, display_name, lobby_name, description, NONE, (short)(MCC_MAPS_FIRST_DISPLAY_INDEX + index));
	if (ui_map_list_count_value && !strcmp(ui_map_list[ui_map_list_count_value - 1].map_name, level))
		pane_name_wrap(ui_map_list[ui_map_list_count_value - 1].pane_name, name, PANE_NAME_LINE, PANE_NAME_LINES);
}

/* the files found of a family's maps, while the list is filled */
struct found_files
{
	char names[MAXIMUM_MAP_LIST][MAP_NAME_LENGTH];
	long count;
};

static void file_found(
	char const *file,
	void *context)
{
	struct found_files *found = context;

	if (found->count < MAXIMUM_MAP_LIST && strlen(file) < MAP_NAME_LENGTH - 3)
		snprintf(found->names[found->count++], MAP_NAME_LENGTH, "%s", file);
	else if (found->count >= MAXIMUM_MAP_LIST && !ui_map_list_left_out++)
		snprintf(ui_map_list_first_left_out, sizeof(ui_map_list_first_left_out), "%s", file);
}

/* a pack's map found: by its name, <file>@<pack> */
static void pack_map_found(
	char const *pack,
	char const *file,
	void *context)
{
	struct found_files *found = context;

	if (found->count < MAXIMUM_MAP_LIST)
	{
		if (map_pack_map_name(pack, file, found->names[found->count], MAP_NAME_LENGTH))
			found->count++;
	}
}

/* (packs' maps in the order of their packs, then of their files) */
static int pack_map_compare(
	void const *a,
	void const *b)
{
	char const *first = (char const *)a;
	char const *second = (char const *)b;
	int order = _stricmp(strrchr(first, '@'), strrchr(second, '@'));

	return order ? order : _stricmp(first, second);
}

/* the campaign's levels, after the maps, for a network co-op game hosted
from the Xbox's map list (the Chupathingy menus' Select Map, as the Gearbox
menus' map list offers them): named as the server browser names them, with
the level select menus' picture of each. Counted apart from the maps:
only that list shows them (ui_map_list_count_with_campaign) */
#ifdef HALO_GAME_BROWSER
char const *main_get_solo_level_name(short level_index);
long tag_loaded(long group_tag, char const *name);
struct bitmap_data *bitmap_group_get_bitmap_from_sequence(long bitmap_group_index, short sequence_index,
	short frame_index);

static wchar_t const *const campaign_level_names[] =
{
	L"The Pillar of Autumn", L"Halo", L"The Truth and Reconciliation", L"The Silent Cartographer",
	L"Assault on the Control Room", L"343 Guilty Spark", L"The Library", L"Two Betrayals", L"Keyes", L"The Maw",
};
#endif

static void add_campaign_entries(
	void)
{
#ifdef HALO_GAME_BROWSER
	short level;

	for (level = 0; level < (short)NUMBEROF(campaign_level_names); level++)
	{
		char const *map_name = main_get_solo_level_name(level);

		if (map_name)
		{
			add_entry(map_name, campaign_level_names[level], campaign_level_names[level],
				L"Campaign co-op\r\n\r\nPlay this level with\r\nfriends", NONE,
				(short)(UI_MAP_LIST_CAMPAIGN_PICTURE_BASE + level));
			ui_map_list[ui_map_list_count_value - 1].game_type = L"Co-op";
		}
	}
#endif
}

/* ---------- the view (ui_map_list_view_set) */

#define ROW(row) (&ui_map_list[ui_map_list_view[row]])

long tag_loaded(long group_tag, char const *name);
wchar_t *unicode_string_list_get_string(long tag_index, short string_index);

/* (an MCC package's ModInfo.json and CampaignInfo.json, read once a run
for each level: what a search is matched against besides the names) */
#ifdef HALO_XBOX_CONSOLE
enum { VIEW_PACKAGE_TEXT = 1, VIEW_PACKAGE_TEXTS = 1 };
#else
enum { VIEW_PACKAGE_TEXT = 2048, VIEW_PACKAGE_TEXTS = MIN(MAXIMUM_MAP_LIST, MCC_MAPS_MAXIMUM) };
#endif

static struct
{
	short source, sort;
	char search[48];
	unsigned long serial;
	boolean present[NUMBER_OF_UI_MAP_LIST_SOURCES];
	long package_text_count;
	struct
	{
		char level[MAP_NAME_LENGTH];
		char text[VIEW_PACKAGE_TEXT];
	} *package_texts;
} view;

static char const *const view_source_names[NUMBER_OF_UI_MAP_LIST_SOURCES] = { "All", "Xbox", "CE", "MD", "MCC" };
static char const *const view_source_keys[NUMBER_OF_UI_MAP_LIST_SOURCES] = { "all", "xbox", "ce", "md", "mcc" };
static char const *const view_sort_names[NUMBER_OF_UI_MAP_LIST_SORTS] = { "Default order", "A-Z", "Recently added" };
static char const *const view_sort_keys[NUMBER_OF_UI_MAP_LIST_SORTS] = { "default", "az", "recent" };

static char view_lower(char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

/* (text, ASCII letters in any case, has search) */
static boolean view_contains(char const *text, char const *search)
{
	long at, index;

	if (!search[0])
		return TRUE;
	for (at = 0; text[at]; at++)
	{
		for (index = 0; search[index] && text[at + index] &&
			view_lower(text[at + index]) == view_lower(search[index]); index++)
			;
		if (!search[index])
			return TRUE;
	}
	return FALSE;
}

static void view_narrow(char *text, long size, wchar_t const *wide)
{
	long index;

	for (index = 0; wide && wide[index] && index < size - 1; index++)
		text[index] = wide[index] < 0x80 ? (char)wide[index] : '?';
	text[index] = 0;
}

/* an entry's name as its rows show it, without its tag ([CE], [MCC]) */
static void view_entry_name(struct ui_map_entry const *entry, char *name, long size)
{
	char *tag;

	if (entry->xbox_index != NONE)
	{
		long tag_index = tag_loaded('ustr', "ui\\shell\\main_menu\\mp_map_list");
		wchar_t const *string = tag_index != NONE ? unicode_string_list_get_string(tag_index, entry->xbox_index) : NULL;

		view_narrow(name, size, string);
		if (!name[0])
			snprintf(name, (size_t)size, "%s", entry->map_name);
		return;
	}
	view_narrow(name, size, entry->lobby_name);
	tag = strrchr(name, '[');
	if (tag && tag > name && tag[-1] == ' ')
		tag[-1] = 0;
}

/* an MCC level's package's ModInfo.json and CampaignInfo.json (a Workshop
item's are in its folder, above its maps folder), as one text */
static char const *view_package_text(char const *level)
{
	static char const *const files[] = { "ModInfo.json", "CampaignInfo.json" };
	char package[MAP_NAME_LENGTH], path[256];
	char *text, *cut;
	long index, used = 0, file, length;

	if (VIEW_PACKAGE_TEXT < 2 || _strnicmp(level, MCC_MAPS_LEVEL_PREFIX, sizeof(MCC_MAPS_LEVEL_PREFIX) - 1))
		return "";
	for (index = 0; index < view.package_text_count; index++)
	{
		if (!strcmp(view.package_texts[index].level, level))
			return view.package_texts[index].text;
	}
	if (!view.package_texts)
		view.package_texts = calloc(VIEW_PACKAGE_TEXTS, sizeof(*view.package_texts));
	if (!view.package_texts || view.package_text_count >= VIEW_PACKAGE_TEXTS)
		return "";
	index = view.package_text_count++;
	snprintf(view.package_texts[index].level, MAP_NAME_LENGTH, "%s", level);
	text = view.package_texts[index].text;
	text[0] = 0;
	/* (its package: the folder before the level's name, none for a loose map) */
	snprintf(package, sizeof(package), "%s", level + sizeof(MCC_MAPS_LEVEL_PREFIX) - 1);
	cut = strrchr(package, '\\');
	if (!cut)
		return text;
	*cut = 0;
	for (file = 0; file < 2; file++)
	{
		FILE *stream;

		snprintf(path, sizeof(path), "%s%s\\%s", MCC_MAPS_DIRECTORY, package, files[file]);
		stream = fopen(path, "rb");
		if (!stream)
			continue;
		length = (long)fread(text + used, 1, (size_t)(VIEW_PACKAGE_TEXT - 1 - used), stream);
		fclose(stream);
		used += length;
		text[used] = 0;
	}
#if defined(HALO_MCC) && HALO_MCC
	/* (a Workshop item: its folder's, the maps folder's parent) */
	if (!used)
	{
		extern int steam_workshop_package_folder(const char *item_id, char *folder, unsigned long size);
		extern int posix_mcc_preview_read(const char *root, const char *relative, void *buffer,
			unsigned long capacity);
		char folder[512];

		if (steam_workshop_package_folder(package, folder, sizeof(folder)))
		{
			char *slash = strrchr(folder, '/');

			if (!slash)
				slash = strrchr(folder, '\\');
			if (slash)
				*slash = 0;
			/* (read in place, kept inside the item's folder: posix_mcc_preview.c) */
			for (file = 0; file < 2; file++)
			{
				length = posix_mcc_preview_read(folder, files[file], text + used,
					(unsigned long)(VIEW_PACKAGE_TEXT - 1 - used));
				used += length > 0 ? length : 0;
				text[used] = 0;
			}
		}
	}
#endif
	return text;
}

/* whether the lists offer MCC maps now: in every game, offline, split
screen and network (a network game on one is hosted at OpenCE's 43:
network_game_server_mcc_version); a build without MCC maps lists none */
boolean ui_map_list_mcc_offered(
	void)
{
	return TRUE;
}

static boolean view_matches(struct ui_map_entry const *entry)
{
	char name[DISPLAY_NAME_LENGTH * 2];

	if (entry->source == _ui_map_list_source_mcc && !ui_map_list_mcc_offered())
		return FALSE;
	/* (the Workshop item the Guide's PLAY handed over: shown whatever the
	view, mcc_maps_set_first_item) */
	if (entry->source == _ui_map_list_source_mcc && mcc_maps_level_first(entry->map_name))
		return TRUE;
	if (view.source != _ui_map_list_source_all && entry->source != view.source)
		return FALSE;
	if (!view.search[0])
		return TRUE;
	view_entry_name(entry, name, sizeof(name));
	if (view_contains(name, view.search) || view_contains(entry->map_name, view.search))
		return TRUE;
	return entry->source == _ui_map_list_source_mcc && view_contains(view_package_text(entry->map_name), view.search);
}

/* when an entry's file was written, as a number that grows with it (0: not known) */
static long long view_entry_added(struct ui_map_entry *entry)
{
#ifdef HALO_CUSTOM_EDITION
	char path[256], file[MAP_NAME_LENGTH];
	short family;

	if (entry->added_known)
		return entry->added;
	entry->added_known = TRUE;
	path[0] = 0;
	if (!_strnicmp(entry->map_name, MCC_MAPS_LEVEL_PREFIX, sizeof(MCC_MAPS_LEVEL_PREFIX) - 1))
		snprintf(path, sizeof(path), "%s%s.map", MCC_MAPS_DIRECTORY,
			entry->map_name + sizeof(MCC_MAPS_LEVEL_PREFIX) - 1);
	else if ((family = map_family_parse(entry->map_name, file, sizeof(file))) == _map_family_xbox ||
		family == _map_family_pack)
	{
		char const *base = strrchr(entry->map_name, '\\');

		if (!map_family_xbox_path(base ? base + 1 : entry->map_name, path, sizeof(path)))
			path[0] = 0;
	}
	else if (!map_family_find(family, file, path, sizeof(path)))
		path[0] = 0;
	if (path[0])
	{
		HANDLE handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

		if (handle != INVALID_HANDLE_VALUE)
		{
			FILETIME written;

			if (GetFileTime(handle, NULL, NULL, &written))
				entry->added = ((long long)written.dwHighDateTime << 32) | written.dwLowDateTime;
			CloseHandle(handle);
		}
	}
	return entry->added;
#else
	(void)entry;
	return 0;
#endif
}

static int view_compare(void const *a, void const *b)
{
	struct ui_map_entry *first = &ui_map_list[*(short const *)a], *second = &ui_map_list[*(short const *)b];
	int order = 0;

	if (view.sort == _ui_map_list_sort_name)
	{
		char first_name[DISPLAY_NAME_LENGTH * 2], second_name[DISPLAY_NAME_LENGTH * 2];

		view_entry_name(first, first_name, sizeof(first_name));
		view_entry_name(second, second_name, sizeof(second_name));
		order = _stricmp(first_name, second_name);
	}
	else if (view.sort == _ui_map_list_sort_recent)
	{
		long long first_added = view_entry_added(first), second_added = view_entry_added(second);

		order = first_added > second_added ? -1 : first_added < second_added;
	}
	/* (in the list's own order otherwise: a stable sort) */
	return order ? order : *(short const *)a - *(short const *)b;
}

/* the rows: the view's multiplayer maps, the campaign's co-op levels, the
multiplayer maps the view leaves out, then the MCC campaign maps */
static boolean view_mcc_offered = TRUE;

static void view_apply(void)
{
	long entry, rows = 0, shown, campaign_shown;
	short source;

	for (source = 0; source < NUMBER_OF_UI_MAP_LIST_SOURCES; source++)
		view.present[source] = FALSE;
	for (entry = 0; entry < ui_map_list_found_multiplayer; entry++)
	{
		view.present[_ui_map_list_source_all] = TRUE;
		view.present[ui_map_list[entry].source] = TRUE;
		if (view_matches(&ui_map_list[entry]))
			ui_map_list_view[rows++] = (short)entry;
	}
	shown = rows;
	if (view.sort != _ui_map_list_sort_default)
		qsort(ui_map_list_view, (size_t)shown, sizeof(ui_map_list_view[0]), view_compare);
	/* (and first, in their order) */
	{
		long first = 0, row;

		for (row = 0; row < shown; row++)
		{
			short kept = ui_map_list_view[row];

			if (ui_map_list[kept].source == _ui_map_list_source_mcc && mcc_maps_level_first(ui_map_list[kept].map_name))
			{
				memmove(&ui_map_list_view[first + 1], &ui_map_list_view[first], (size_t)(row - first) * sizeof(ui_map_list_view[0]));
				ui_map_list_view[first++] = kept;
			}
		}
	}
	/* (the campaign's rows, less MCC's while they aren't offered) */
	for (entry = ui_map_list_found_multiplayer; entry < ui_map_list_found_campaign_end; entry++)
	{
		if (ui_map_list[entry].source != _ui_map_list_source_mcc || ui_map_list_mcc_offered())
			ui_map_list_view[rows++] = (short)entry;
	}
	campaign_shown = rows - shown;
	for (entry = 0; entry < ui_map_list_found_multiplayer; entry++)
	{
		long other;

		for (other = 0; other < shown && ui_map_list_view[other] != entry; other++)
			;
		if (other == shown)
			ui_map_list_view[rows++] = (short)entry;
	}
	for (entry = ui_map_list_found_multiplayer; entry < ui_map_list_found_campaign_end; entry++)
	{
		if (ui_map_list[entry].source == _ui_map_list_source_mcc && !ui_map_list_mcc_offered())
			ui_map_list_view[rows++] = (short)entry;
	}
	for (entry = ui_map_list_found_campaign_end; entry < ui_map_list_count_value; entry++)
		ui_map_list_view[rows++] = (short)entry;
	for (entry = 0; entry < ui_map_list_count_value; entry++)
		ui_map_list_names_array[entry] = ROW(entry)->map_name;
	ui_map_list_multiplayer_count = shown;
	ui_map_list_campaign_end = shown + campaign_shown;
	view_mcc_offered = ui_map_list_mcc_offered();
	view.serial++;
}

/* the view again when hosting began or ended since it was made (MCC's rows) */
static void view_refresh(
	void)
{
	if (ui_map_list_count_value && view_mcc_offered != ui_map_list_mcc_offered())
		view_apply();
}

void ui_map_list_view_set(
	short source,
	short sort,
	char const *search)
{
	view.source = (short)PIN(source, 0, NUMBER_OF_UI_MAP_LIST_SOURCES - 1);
	view.sort = (short)PIN(sort, 0, NUMBER_OF_UI_MAP_LIST_SORTS - 1);
	snprintf(view.search, sizeof(view.search), "%s", search ? search : "");
	view_apply();
}

unsigned long ui_map_list_view_serial(
	void)
{
	return view.serial;
}

boolean ui_map_list_source_present(
	short source)
{
	return source >= 0 && source < NUMBER_OF_UI_MAP_LIST_SOURCES && view.present[source];
}

char const *ui_map_list_source_name(
	short source)
{
	return view_source_names[PIN(source, 0, NUMBER_OF_UI_MAP_LIST_SOURCES - 1)];
}

char const *ui_map_list_sort_name(
	short sort)
{
	return view_sort_names[PIN(sort, 0, NUMBER_OF_UI_MAP_LIST_SORTS - 1)];
}

char const *ui_map_list_source_key(
	short source)
{
	return view_source_keys[PIN(source, 0, NUMBER_OF_UI_MAP_LIST_SOURCES - 1)];
}

char const *ui_map_list_sort_key(
	short sort)
{
	return view_sort_keys[PIN(sort, 0, NUMBER_OF_UI_MAP_LIST_SORTS - 1)];
}

short ui_map_list_source_from_key(
	char const *key)
{
	short source;

	for (source = 0; key && source < NUMBER_OF_UI_MAP_LIST_SOURCES; source++)
	{
		if (!_stricmp(key, view_source_keys[source]))
			return source;
	}
	return _ui_map_list_source_all;
}

short ui_map_list_sort_from_key(
	char const *key)
{
	short sort;

	for (sort = 0; key && sort < NUMBER_OF_UI_MAP_LIST_SORTS; sort++)
	{
		if (!_stricmp(key, view_sort_keys[sort]))
			return sort;
	}
	return _ui_map_list_sort_default;
}

/* ---------- public code */

/* the list anew: the Xbox's maps (the game's thirteen names, in its order),
then the Custom Edition maps found, then HaloMD's, then Halo PC retail's */
void ui_map_list_refresh(
	char *const *xbox_maps)
{
	static struct found_files found;
	/* (the counts last logged, to log each change once) */
	static long logged_counts[NUMBER_OF_MAP_FAMILIES] = { -1, -1, -1, -1, -1 };
	static long logged_left_out;
	long counts[NUMBER_OF_MAP_FAMILIES] = { XBOX_MAP_COUNT, 0, 0, 0, 0 };
	/* (with a pack's own menus, its maps of the Xbox's names are those rows:
	map_pack_menus) */
	char const *pack_menus = map_pack_menus();
	boolean pack_row[XBOX_MAP_COUNT];
	short index, family;
	long file;

	ui_map_list_count_value = 0;
	ui_map_list_left_out = 0;
	ui_map_list_adding_source = _ui_map_list_source_xbox;
	for (index = 0; index < XBOX_MAP_COUNT; index++)
	{
		char const *name = xbox_maps[index];
		char const *cursor;

		for (cursor = name; *cursor; cursor++)
		{
			if (*cursor == '\\')
				name = cursor + 1;
		}
		pack_row[index] = pack_menus && map_pack_present(pack_menus, name);
		if (pack_row[index])
		{
			char map_name[MAP_NAME_LENGTH];

			pack_row[index] = map_pack_map_name(pack_menus, name, map_name, sizeof(map_name));
			if (pack_row[index])
				add_pack_entry(map_name, index);
		}
		if (!pack_row[index])
			add_entry(xbox_maps[index], L"", L"", L"", index, index);
	}
#ifdef HALO_CUSTOM_EDITION
	for (family = _map_family_custom_edition; family <= _map_family_halo_pc; family++)
	{
		extern int platform_ce_tag_cache_ready;

		/* (none of them would load without their tag cache, which the
		platform layer could not map: port/linux/src/xbox_memory.c) */
		if (!platform_ce_tag_cache_ready)
			break;
#ifdef HALO_CONSOLE_NO_HALOMD_MAPS
		/* (HaloMD's maps, which this console doesn't play: not listed) */
		if (family == _map_family_halomd)
			continue;
#endif
		found.count = 0;
		map_family_list(family, file_found, &found);
		if (found.count)
			ce_ui_read();
		ui_map_list_adding_source = family == _map_family_halomd ? _ui_map_list_source_md : _ui_map_list_source_ce;
		/* (in the order of their names, as the list shows them) */
		qsort(found.names, found.count, MAP_NAME_LENGTH, (int (*)(void const *, void const *))_stricmp);
		for (file = 0; file < found.count; file++)
			add_pc_entry(family, found.names[file]);
		counts[family] = found.count;
		/* (and its MCC maps, which are only said not to work) */
		found.count = 0;
		ui_map_list_adding_source = _ui_map_list_source_mcc;
		map_family_list_mcc(family, file_found, &found);
		qsort(found.names, found.count, MAP_NAME_LENGTH, (int (*)(void const *, void const *))_stricmp);
		for (file = 0; file < found.count; file++)
			add_mcc_entry(family, found.names[file]);
	}
#endif
	/* (the MCC catalog's multiplayer maps, after the Custom Edition ones:
	mcc_maps.c reads its folders anew when they change) */
	ui_map_list_adding_source = _ui_map_list_source_mcc;
	for (file = 0; file < mcc_maps_type_count(FALSE); file++)
	{
		short index = mcc_maps_type_index((short)file, FALSE);

		if (index != NONE)
			add_mcc_catalog_entry(index);
	}
	/* (a map pack's maps, Xbox maps read by the Xbox's loader: no tag
	cache of their own) */
	found.count = 0;
	ui_map_list_adding_source = _ui_map_list_source_xbox;
	map_pack_list(pack_map_found, &found);
	qsort(found.names, found.count, MAP_NAME_LENGTH, pack_map_compare);
	for (file = 0; file < found.count; file++)
	{
		/* (one of the pack's menus' own rows already) */
		for (index = 0; index < XBOX_MAP_COUNT &&
			!(pack_row[index] && !_stricmp(found.names[file], ui_map_list[index].map_name)); index++)
			;
		if (index == XBOX_MAP_COUNT)
			add_pack_entry(found.names[file], NONE);
	}
	counts[_map_family_pack] = found.count;
	(void)family;
	ui_map_list_found_multiplayer = ui_map_list_count_value;
	add_campaign_entries();
	ui_map_list_found_campaign_end = ui_map_list_count_value;
	ui_map_list_adding_source = _ui_map_list_source_mcc;
	for (file = 0; file < mcc_maps_type_count(TRUE); file++)
	{
		short index = mcc_maps_type_index((short)file, TRUE);

		if (index != NONE)
			add_mcc_catalog_entry(index);
	}
	view_apply();
	if (ui_map_list_left_out != logged_left_out)
	{
		logged_left_out = ui_map_list_left_out;
		if (ui_map_list_left_out)
			error(_error_silent, "the menus' map list is full (%d rows): %ld maps left out, from %s",
				(int)MAXIMUM_MAP_LIST, ui_map_list_left_out, ui_map_list_first_left_out);
	}
	if (memcmp(counts, logged_counts, sizeof(counts)))
	{
		memcpy(logged_counts, counts, sizeof(counts));
		error(_error_silent, "the menus' map list: %ld Custom Edition maps, %ld HaloMD maps, %ld Halo PC maps, "
			"%ld map pack maps%s", counts[_map_family_custom_edition], counts[_map_family_halomd],
			counts[_map_family_halo_pc], counts[_map_family_pack], pack_menus ? " (a pack's own menus)" : "");
	}
}

long ui_map_list_count(
	void)
{
	view_refresh();
	return ui_map_list_multiplayer_count;
}

/* the rows with the campaign's levels after the maps */
long ui_map_list_count_with_campaign(
	void)
{
	view_refresh();
	return ui_map_list_campaign_end;
}

/* whether a row is one of the campaign's levels */
boolean ui_map_list_row_is_campaign(
	long row)
{
	return row >= ui_map_list_multiplayer_count && row < ui_map_list_campaign_end;
}

long ui_map_list_count_all(
	void)
{
	return ui_map_list_count_value;
}

/* the rows' map names, for the list widget's items */
char **ui_map_list_names(
	void)
{
	view_refresh();
	return ui_map_list_names_array;
}

/* the row of a map's name (as the list or a game has it), or NONE */
long ui_map_list_find(
	char const *map_name)
{
	long row;

	if (!map_name)
		return NONE;
	for (row = 0; row < ui_map_list_count_value; row++)
	{
		if (!_stricmp(map_name, ROW(row)->map_name))
			return row;
	}
	return NONE;
}

/* a row's string list index for one of its strings (_ui_map_list_string_*):
the Xbox's own, or this list's */
short ui_map_list_string_index(
	long row,
	short kind)
{
	if (row < 0 || row >= ui_map_list_count_value)
		return 0;
	if (ROW(row)->xbox_index != NONE)
		return ROW(row)->xbox_index;
	return (short)(UI_MAP_LIST_STRING_BASE + row * UI_MAP_LIST_STRINGS_PER_ROW + kind);
}

/* a row's bitmap frame: an Xbox map's, or Halo PC's picture of it */
short ui_map_list_picture_index(
	long row)
{
	if (row < 0 || row >= ui_map_list_count_value)
		return 0;
	return ROW(row)->picture_index;
}

/* the bitmap of a frame of this list's (UI_MAP_LIST_PICTURE_BASE and up), or
NULL for any other */
struct bitmap_data *ui_map_list_picture(
	short frame_index)
{
#ifdef HALO_GAME_BROWSER
	if (frame_index >= UI_MAP_LIST_CAMPAIGN_PICTURE_BASE &&
		frame_index < UI_MAP_LIST_CAMPAIGN_PICTURE_BASE + (short)NUMBEROF(campaign_level_names))
	{
		long bitmap_index = tag_loaded('bitm', "ui\\shell\\bitmaps\\sp_levels");

		return bitmap_index == NONE ? NULL :
			bitmap_group_get_bitmap_from_sequence(bitmap_index, 0, frame_index - UI_MAP_LIST_CAMPAIGN_PICTURE_BASE);
	}
#endif
#ifdef HALO_CUSTOM_EDITION
	if (frame_index < UI_MAP_LIST_PICTURE_BASE || frame_index - UI_MAP_LIST_PICTURE_BASE >= ce_ui.picture_count)
		return NULL;
	return &ce_ui.lists->pictures[frame_index - UI_MAP_LIST_PICTURE_BASE];
#else
	(void)frame_index;
	return NULL;
#endif
}

short ui_map_list_mcc_picture(
	short frame_index)
{
	return frame_index >= MCC_MAPS_FIRST_DISPLAY_INDEX && frame_index < MCC_MAPS_FIRST_DISPLAY_INDEX + MCC_MAPS_MAXIMUM ?
		(short)(frame_index - MCC_MAPS_FIRST_DISPLAY_INDEX) : NONE;
}

short ui_map_list_unknown_picture(
	void)
{
	/* (Halo PC's picture for an unknown level; without Halo PC's ui.map, the Xbox's frame 9) */
#ifdef HALO_CUSTOM_EDITION
	ce_ui_read();
	if (CE_UNKNOWN_LEVEL < ce_ui.picture_count)
		return (short)(UI_MAP_LIST_PICTURE_BASE + CE_UNKNOWN_LEVEL);
#endif
	return 9;
}

/* the text of a string list index of this list's (UI_MAP_LIST_STRING_BASE
and up), or NULL for any other */
wchar_t const *ui_map_list_text(
	short string_list_index)
{
	long row;

	if (string_list_index < UI_MAP_LIST_STRING_BASE)
		return NULL;
	row = (string_list_index - UI_MAP_LIST_STRING_BASE) / UI_MAP_LIST_STRINGS_PER_ROW;
	if (row >= ui_map_list_count_value)
		return NULL;
	switch ((string_list_index - UI_MAP_LIST_STRING_BASE) % UI_MAP_LIST_STRINGS_PER_ROW)
	{
	case _ui_map_list_string_description:
		return ROW(row)->description;
	case _ui_map_list_string_lobby_name:
		return ROW(row)->lobby_name;
	case _ui_map_list_string_game_type:
		return ROW(row)->game_type;
	case _ui_map_list_string_pane_name:
		return ROW(row)->pane_name;
	default:
		return ROW(row)->display_name;
	}
}

/* Halo PC's picture of one of its own maps, by its file's name; NULL for
another map (Halo PC has only its unknown level's question mark for those),
and without Halo PC's ui.map */
struct bitmap_data *ui_map_list_family_picture(
	short family,
	char const *file)
{
#ifdef HALO_CUSTOM_EDITION
	long index = family == _map_family_custom_edition || family == _map_family_halo_pc ? ce_map_index(file) : NONE;

	if (index == NONE)
		return NULL;
	ce_ui_read();
	return index < ce_ui.picture_count ? &ce_ui.lists->pictures[index] : NULL;
#else
	(void)family;
	(void)file;
	return NULL;
#endif
}

/* whether a family's folders have a map of this file's name, of its
family's version (where the game loads one from: cache_files_windows.c) */
boolean ui_map_list_family_present(
	short family,
	char const *file)
{
	/* (a pack's map is asked for by its name: ui_map_list_map_present) */
	if (family == _map_family_pack)
		return FALSE;
#ifdef HALO_CUSTOM_EDITION
	{
		char path[512];

		return family != _map_family_xbox && map_family_find(family, file, path, sizeof(path));
	}
#else
	return FALSE;
#endif
}

#endif

/* a Custom Edition or HaloMD map's name, by its file's name: Halo PC's own
for its maps (as its ui.map has it, on the builds with Custom Edition maps),
HaloMD's mod list's for HaloMD's, else the file's name made readable */
void ui_map_list_family_name(
	short family,
	char const *file,
	wchar_t *name,
	long size)
{
	long index = family == _map_family_custom_edition || family == _map_family_halo_pc ? ce_map_index(file) : NONE;
	wchar_t const *known = family == _map_family_halomd ? halomd_map_name(file) : NULL;
	long length;

#ifdef HALO_MAP_LIST
#endif
	if (index == NONE && !known)
	{
		ce_map_tidy_name(file, name, size);
		return;
	}
	if (index == NONE)
	{
		for (length = 0; length < size - 1 && known[length]; length++)
			name[length] = known[length];
		name[length] = 0;
		return;
	}
	known = ce_maps[index].name;
#ifdef HALO_CUSTOM_EDITION
	ce_ui_read();
	if (index < ce_ui.name_count && ce_ui.lists->names[index][0])
		known = ce_ui.lists->names[index];
#endif
	for (length = 0; length < size - 1 && known[length]; length++)
		name[length] = known[length];
	name[length] = 0;
}

#ifdef HALO_MAP_LIST
/* a game's map's name, by the map's name (<file>, <file>@ce, ...): a pack's
map's (<file>@<pack>) as the library or its pack's maplist.txt names it, any
other as ui_map_list_family_name names it */
void ui_map_list_map_name(
	char const *map,
	wchar_t *name,
	long size)
{
	char file[MAP_NAME_LENGTH], pack[MAP_PACK_ID_LENGTH + 1];
	short family = map_family_parse_listed(map, file, sizeof(file));

	if (family == _map_family_pack && map_family_pack_id(map, pack, sizeof(pack)))
	{
		char text[96], title[8];

		map_pack_names(pack, file, 0, text, sizeof(text), title, sizeof(title));
		wide_from_text(name, size, text);
		return;
	}
	ui_map_list_family_name(family, file, name, size);
}

/* whether this machine has a game's map past the Xbox's, by its name: a
pack's in its maps_<pack> folder, another in its family's folders */
boolean ui_map_list_map_present(
	char const *map)
{
	char file[MAP_NAME_LENGTH], pack[MAP_PACK_ID_LENGTH + 1];
	short family = map_family_parse_listed(map, file, sizeof(file));

	if (family == _map_family_pack)
		return map_family_pack_id(map, pack, sizeof(pack)) && map_pack_present(pack, file);
	return ui_map_list_family_present(family, file);
}

/* a game's map's tag (map_family_tag; a pack's map its pack's: CE+; an MCC map MCC), and
what it stands for (map_family_title; a pack's map its pack's name), into
text */
void ui_map_list_map_tag(
	char const *map,
	char *tag,
	long tag_size,
	char *title,
	long title_size)
{
	char pack[MAP_PACK_ID_LENGTH + 1];
	short family = map_family_parse_listed(map, NULL, 0);

	/* (an MCC map, mcc_maps\<package>\<map>: MCC, as the site marks it) */
	if (map && !strncmp(map, "mcc_maps\\", 9))
	{
		snprintf(tag, (size_t)tag_size, "%s", MAP_FAMILY_TAG_MCC);
		snprintf(title, (size_t)title_size, "%s", MAP_FAMILY_TITLE_MCC);
		return;
	}
	if (family == _map_family_pack && map_family_pack_id(map, pack, sizeof(pack)))
	{
		map_pack_tag(pack, tag, tag_size);
		map_pack_title(pack, title, title_size);
		if (title[0] >= 'a' && title[0] <= 'z')
			title[0] = (char)(title[0] - 'a' + 'A');
		return;
	}
	snprintf(tag, (size_t)tag_size, "%s", map_family_tag(family));
	snprintf(title, (size_t)title_size, "%s", map_family_title(family));
}
#endif
