/*
FONT_GROUP.C

symbols in this file:
0018C840 0070:
	_font_get_character_by_ascii_code (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "font_group.h"
/* port: the verified mark's glyph */
#include "halo_delta_mark.h"

/* ---------- constants */

enum
{
	/* port: the verified mark's sizes kept at once (the fonts' capitals'
	heights), and its largest bitmap's side */
	MAXIMUM_DELTA_MARKS = 6,
	MAXIMUM_DELTA_MARK_SIZE = 48,
};

/* ---------- macros */

/* ---------- structures */

/* port: a font character, as rasterizer_text.c's and draw_string.c's */
struct font_character
{
	word character;
	short character_width;
	short bitmap_width;
	short bitmap_height;
	short bitmap_origin_x;
	short bitmap_origin_y;
	short hardware_character_index;
	word pad;
	long pixels_offset;
};

/* port: the verified mark drawn for fonts that have no Delta (the maps'
fonts have no Greek): a character the size of a font's capitals, its pixels
its own (font_character_pixels) */
struct delta_mark
{
	short cap_height;
	short origin_y;
	struct font_character character;
	byte pixels[MAXIMUM_DELTA_MARK_SIZE * MAXIMUM_DELTA_MARK_SIZE];
};

/* ---------- prototypes */

/* ---------- globals */

static struct delta_mark delta_marks[MAXIMUM_DELTA_MARKS];
static short delta_mark_next = 0;

/* ---------- public code */

static struct font_character *delta_mark_character(struct font_header *font);

struct font_character *font_get_character_by_ascii_code(
	struct font_header *font,
	word character_code)
{
	struct font_character_table *character_table;
	struct font_character *character = NULL;

	/* port: no glyph for a character past the font's tables, a table that
	isn't a whole 256 entries, or an entry past the font's characters (the
	map's font, and any string's characters, e.g. a player's name) */
	if (!VALID_INDEX(character_code >> 8, font->character_tables.count))
		return NULL;

	character_table = TAG_BLOCK_GET_ELEMENT(
		&font->character_tables,
		character_code >> 8,
		struct font_character_table);

	if (character_table->character_indices.count == 256)
	{
		short *character_index = TAG_BLOCK_GET_ELEMENT(&character_table->character_indices, character_code & 0xFF, short);

		if (VALID_INDEX(*character_index, font->characters.count))
		{
			character = tag_block_get_element_with_size(
				&font->characters,
				*character_index,
				FONT_CHARACTER_SIZE);
		}
	}
	/* port: the verified mark, where the font has no Delta */
	if (!character && character_code == HALO_DELTA_MARK)
		character = delta_mark_character(font);

	return character;
}

/* port: a character's pixels: the font's, or the verified mark's own */
byte *font_character_pixels(
	struct font_header *font,
	struct font_character *character)
{
	short mark_index;

	for (mark_index = 0; mark_index < MAXIMUM_DELTA_MARKS; mark_index++)
	{
		if (character == &delta_marks[mark_index].character)
			return delta_marks[mark_index].pixels;
	}

	return (byte *)xbox_pointer(font->pixels.address) + character->pixels_offset;
}

/* ---------- private code */

/* port: the verified mark for a font: as tall as its capital H's ink, which
it stands level with, with a quarter of that after it before the name; NULL
when the font has no H */
static struct font_character *delta_mark_character(
	struct font_header *font)
{
	struct font_character *capital = font_get_character_by_ascii_code(font, 'H');
	byte const *pixels;
	struct delta_mark *mark;
	short top = NONE;
	short bottom = NONE;
	short row;
	short cap_height;
	short origin_y;
	short width;
	short mark_index;

	if (!capital)
		return NULL;
	pixels = font_character_pixels(font, capital);
	for (row = 0; row < capital->bitmap_height; row++)
	{
		short column;

		for (column = 0; column < capital->bitmap_width; column++)
		{
			if (pixels[row * capital->bitmap_width + column])
			{
				if (top == NONE)
					top = row;
				bottom = row;
				break;
			}
		}
	}
	if (top == NONE)
		return NULL;
	cap_height = (short)MIN(bottom - top + 1, MAXIMUM_DELTA_MARK_SIZE * 8 / 9);
	origin_y = (short)(capital->bitmap_origin_y - top);
	for (mark_index = 0; mark_index < MAXIMUM_DELTA_MARKS; mark_index++)
	{
		mark = &delta_marks[mark_index];
		if (mark->cap_height == cap_height && mark->origin_y == origin_y)
			return &mark->character;
	}
	/* (a new size, in the oldest's place; the text cache draws it again) */
	mark = &delta_marks[delta_mark_next];
	delta_mark_next = (short)((delta_mark_next + 1) % MAXIMUM_DELTA_MARKS);
	width = (short)halo_delta_mark_width(cap_height);
	csmemset(mark, 0, sizeof(*mark));
	mark->cap_height = cap_height;
	mark->origin_y = origin_y;
	mark->character.character = HALO_DELTA_MARK;
	mark->character.bitmap_width = width;
	mark->character.bitmap_height = cap_height;
	mark->character.bitmap_origin_x = 0;
	mark->character.bitmap_origin_y = origin_y;
	mark->character.character_width = (short)(width + halo_delta_mark_gap(cap_height));
	mark->character.hardware_character_index = NONE;
	halo_delta_mark_draw(mark->pixels, width, cap_height, width);

	return &mark->character;
}
