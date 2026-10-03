/*
UI_MAP_LIST.C

The menus' list of multiplayer maps, filled by the port rather than fixed in
the game (ui_widget_event_handler_functions.c's multiplayer level list): the
Xbox's thirteen maps first, as they were, then the Custom Edition maps in
maps\ce (Halo PC's, played as <name>@ce), each named with [CE].

A row past the Xbox's has no string or bitmap frame of its own in ui.map:
its text comes from here, by a string list index of
UI_MAP_LIST_STRING_BASE and up (ui_widget.c asks ui_map_list_text when a
text box has one), and its picture by a bitmap frame of
UI_MAP_LIST_PICTURE_BASE and up (ui_widget.c asks ui_map_list_picture). Its
name is two lines in the map list's narrow boxes (the name, then [CE]), and
one in the lobby's.

Those are Halo PC's own, read from its ui.map in maps\ce as Halo PC shows
them: the names of its map list (ui\shell\main_menu\mp_map_list), their
descriptions (...\mp_map_select\map_data) and their pictures
(ui\shell\bitmaps\mp_map_grafix, its pixels in maps\ce\bitmaps.map), each
the map's by Halo PC's order of its maps (ce_maps); a map of another's
making is named by its file, with Halo PC's picture for an unknown level.
Without Halo PC's ui.map, the names are ce_maps' and an Xbox map's picture
stands in.
*/

#ifdef HALO_64BIT

#include "cseries.h"
#include "cseries_windows.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "rasterizer/rasterizer_swizzle.h"

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "halo_ui_map_list.h"

/* ---------- constants */

enum
{
	XBOX_MAP_COUNT = 13,
	MAXIMUM_MAP_LIST = 64,
	MAP_NAME_LENGTH = 64,
	DISPLAY_NAME_LENGTH = 48,
	DESCRIPTION_LENGTH = 160,
	/* text boxes' string list indices from here are this list's: the row
	times four, plus its string's kind */
	UI_MAP_LIST_STRING_BASE = 0x4000,
	UI_MAP_LIST_STRINGS_PER_ROW = 4,
	/* bitmap frames from here are Halo PC's map pictures */
	UI_MAP_LIST_PICTURE_BASE = 0x4000,

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

/* ---------- structures */

struct ui_map_entry
{
	char map_name[MAP_NAME_LENGTH];
	wchar_t display_name[DISPLAY_NAME_LENGTH];
	wchar_t lobby_name[DISPLAY_NAME_LENGTH];
	wchar_t description[DESCRIPTION_LENGTH];
	/* its row among the Xbox's (their strings and bitmap frames), or NONE */
	short xbox_index;
	short picture_index;
};

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

/* ---------- prototypes */

char const *cache_files_map_directory(void);
long bitmap_format_to_d3d_format(short format, word flags);

/* ---------- globals */

static struct ui_map_entry ui_map_list[MAXIMUM_MAP_LIST];
static char *ui_map_list_names_array[MAXIMUM_MAP_LIST];
static long ui_map_list_count_value;

/* what Halo PC's ui.map has of its map list, read once */
static struct
{
	boolean read;
	long name_count;
	wchar_t names[MAXIMUM_CE_STRINGS][DISPLAY_NAME_LENGTH];
	long description_count;
	wchar_t descriptions[MAXIMUM_CE_STRINGS][DESCRIPTION_LENGTH];
	long picture_count;
	struct bitmap_data pictures[MAXIMUM_CE_PICTURES];
} ce_ui;

/* Halo PC's ui.map's tags, while they are read (ce_ui_read) */
static byte *ce_tags;
static unsigned long ce_tags_size;

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

/* whether the file is a Custom Edition multiplayer map: a cache file of
version 609 whose type is multiplayer */
static boolean ce_map_is_multiplayer(
	char const *path)
{
	unsigned long header[0x19];
	HANDLE file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	boolean result = FALSE;

	if (file == INVALID_HANDLE_VALUE)
		return FALSE;
	if (file_read(file, 0, header, sizeof(header)))
	{
		/* ('head', version, ..., the type a short at 0x60: 1 multiplayer) */
		result = header[0] == 'head' && header[1] == CE_CACHE_VERSION && (header[0x60 / 4] & 0xffff) == 1;
	}
	CloseHandle(file);
	return result;
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

/* the data of the tag of a group and name, or NULL */
static byte *ce_tag_find(
	unsigned long group_tag,
	char const *name)
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
			return ce_tags_pointer(instance[5], 0x6c);
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
	unsigned long *block = (unsigned long *)ce_tag_find('ustr', name);
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
	byte *group = ce_tag_find('bitm', "ui\\shell\\bitmaps\\mp_map_grafix");
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
		struct bitmap_data *bitmap = &ce_ui.pictures[index];
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

				snprintf(path, sizeof(path), "%sce\\bitmaps.map", cache_files_map_directory());
				bitmaps_file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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

/* what Halo PC's ui.map (maps\ce\ui.map) has of its map list, read the first
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
	snprintf(path, sizeof(path), "%sce\\ui.map", cache_files_map_directory());
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return;
	/* ('head', its version, its size, ..., its tags' offset and size) */
	if (file_read(file, 0, header, sizeof(header)) && header[0] == 'head' && header[1] == CE_CACHE_VERSION &&
		header[5] >= 0x10 && header[5] <= 0x4000000)
	{
		ce_tags = malloc(header[5]);
		ce_tags_size = header[5];
		if (ce_tags && file_read(file, header[4], ce_tags, header[5]))
		{
			ce_ui.name_count = ce_string_list_read("ui\\shell\\main_menu\\mp_map_list", ce_ui.names[0],
				DISPLAY_NAME_LENGTH, MAXIMUM_CE_STRINGS);
			ce_ui.description_count = ce_string_list_read(
				"ui\\shell\\main_menu\\multiplayer_type_select\\mp_map_select\\map_data", ce_ui.descriptions[0],
				DESCRIPTION_LENGTH, MAXIMUM_CE_STRINGS);
			ce_pictures_read(file);
		}
		free(ce_tags);
		ce_tags = NULL;
		ce_tags_size = 0;
	}
	CloseHandle(file);
}

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
		return;
	entry = &ui_map_list[ui_map_list_count_value];
	snprintf(entry->map_name, sizeof(entry->map_name), "%s", map_name);
	wide_copy(entry->display_name, DISPLAY_NAME_LENGTH, display_name);
	wide_copy(entry->lobby_name, DISPLAY_NAME_LENGTH, lobby_name);
	wide_copy(entry->description, DESCRIPTION_LENGTH, description);
	entry->xbox_index = xbox_index;
	entry->picture_index = picture_index;
	ui_map_list_names_array[ui_map_list_count_value] = entry->map_name;
	ui_map_list_count_value++;
}

/* a Custom Edition map's row: its file's name (without .map) */
static void add_ce_entry(
	char const *file)
{
	char map_name[MAP_NAME_LENGTH];
	wchar_t name[DISPLAY_NAME_LENGTH];
	wchar_t display_name[DISPLAY_NAME_LENGTH];
	wchar_t lobby_name[DISPLAY_NAME_LENGTH];
	wchar_t const *description = L"A Halo Custom\r\nEdition map";
	long ce_index = CE_UNKNOWN_LEVEL;
	short picture_index = 9;
	short known;

	name[0] = 0;
	for (known = 0; known < (short)NUMBEROF(ce_maps); known++)
	{
		if (!_stricmp(file, ce_maps[known].file))
		{
			ce_index = known;
			picture_index = ce_maps[known].xbox_picture_index;
			wide_copy(name, DISPLAY_NAME_LENGTH, ce_maps[known].name);
			if (known < ce_ui.name_count && ce_ui.names[known][0])
				wide_copy(name, DISPLAY_NAME_LENGTH, ce_ui.names[known]);
			if (known < ce_ui.description_count)
				description = ce_ui.descriptions[known];
			break;
		}
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
	wide_append(lobby_name, DISPLAY_NAME_LENGTH, L" [CE]");
	wide_copy(display_name, DISPLAY_NAME_LENGTH, name);
	wide_append(display_name, DISPLAY_NAME_LENGTH, L"\r\n[CE]");
	snprintf(map_name, sizeof(map_name), "%s@ce", file);
	add_entry(map_name, display_name, lobby_name, description, NONE, picture_index);
}

/* ---------- public code */

/* the list anew: the Xbox's maps (the game's thirteen names, in its order),
then the Custom Edition maps found */
void ui_map_list_refresh(
	char *const *xbox_maps)
{
	char pattern[256];
	WIN32_FIND_DATAA data;
	HANDLE find;
	short index;

	ui_map_list_count_value = 0;
	for (index = 0; index < XBOX_MAP_COUNT; index++)
		add_entry(xbox_maps[index], L"", L"", L"", index, index);
	snprintf(pattern, sizeof(pattern), "%sce\\*.map", cache_files_map_directory());
	find = FindFirstFileA(pattern, &data);
	if (find != INVALID_HANDLE_VALUE)
	{
		char files[MAXIMUM_MAP_LIST][MAP_NAME_LENGTH];
		long file_count = 0, file;

		do
		{
			char path[512];
			size_t length = strlen(data.cFileName);

			if (length < 5 || _stricmp(data.cFileName + length - 4, ".map") || length - 4 >= MAP_NAME_LENGTH - 3)
				continue;
			snprintf(path, sizeof(path), "%sce\\%s", cache_files_map_directory(), data.cFileName);
			if (!ce_map_is_multiplayer(path) || file_count >= MAXIMUM_MAP_LIST)
				continue;
			snprintf(files[file_count], MAP_NAME_LENGTH, "%.*s", (int)(length - 4), data.cFileName);
			file_count++;
		}
		while (FindNextFileA(find, &data));
		CloseHandle(find);
		if (file_count)
			ce_ui_read();
		/* (in the order of their names, as the list shows them) */
		qsort(files, file_count, MAP_NAME_LENGTH, (int (*)(void const *, void const *))_stricmp);
		for (file = 0; file < file_count; file++)
			add_ce_entry(files[file]);
	}
}

long ui_map_list_count(
	void)
{
	return ui_map_list_count_value;
}

/* the rows' map names, for the list widget's items */
char **ui_map_list_names(
	void)
{
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
		if (!_stricmp(map_name, ui_map_list[row].map_name))
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
	if (ui_map_list[row].xbox_index != NONE)
		return ui_map_list[row].xbox_index;
	return (short)(UI_MAP_LIST_STRING_BASE + row * UI_MAP_LIST_STRINGS_PER_ROW + kind);
}

/* a row's bitmap frame: an Xbox map's, or Halo PC's picture of it */
short ui_map_list_picture_index(
	long row)
{
	if (row < 0 || row >= ui_map_list_count_value)
		return 0;
	return ui_map_list[row].picture_index;
}

/* the bitmap of a frame of this list's (UI_MAP_LIST_PICTURE_BASE and up), or
NULL for any other */
struct bitmap_data *ui_map_list_picture(
	short frame_index)
{
	if (frame_index < UI_MAP_LIST_PICTURE_BASE || frame_index - UI_MAP_LIST_PICTURE_BASE >= ce_ui.picture_count)
		return NULL;
	return &ce_ui.pictures[frame_index - UI_MAP_LIST_PICTURE_BASE];
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
		return ui_map_list[row].description;
	case _ui_map_list_string_lobby_name:
		return ui_map_list[row].lobby_name;
	default:
		return ui_map_list[row].display_name;
	}
}

#endif
