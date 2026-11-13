/*
TITLE_TOOL_TEST.C

Runs the title tool (port/linux/src/title_tool.c) on a ui.map, for
tools/test_title_tool.py:

    title_tool_test <ui.map> <titles> <output folder>

titles: a line a title, tab-separated: its tag, bitmap index, CRC (decimal),
scale, text and its letters' lefts (comma-separated). Each title the tool
can make is written to <output folder>/<line number>.rgba: its width and
height (4 bytes each, little-endian), then its pixels, RGBA. The line
numbers made are printed; the exit code is 0 if the map was read, 1 if not.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../src/title_tool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the tool's table, which the game's is generated as: unused here) */
const struct title_tool_title title_tool_titles[] = { { 0 } };
const unsigned int title_tool_title_count = 0;

#define MAXIMUM_TITLES 64
#define MAXIMUM_LETTERS 64

static char lines[MAXIMUM_TITLES][1024];
static float lefts[MAXIMUM_TITLES][MAXIMUM_LETTERS];

static void write_u32(FILE *file, unsigned long value)
{
	unsigned char bytes[4] = { (unsigned char)value, (unsigned char)(value >> 8), (unsigned char)(value >> 16),
		(unsigned char)(value >> 24) };

	fwrite(bytes, 1, 4, file);
}

int main(int argc, char **argv)
{
	struct title_tool_title titles[MAXIMUM_TITLES];
	struct title_tool *tool;
	FILE *file;
	long count = 0, index;

	if (argc != 4)
	{
		fprintf(stderr, "usage: title_tool_test <ui.map> <titles> <output folder>\n");
		return 2;
	}
	file = fopen(argv[2], "r");
	if (!file)
		return 2;
	while (count < MAXIMUM_TITLES && fgets(lines[count], sizeof(lines[count]), file))
	{
		char *fields[6], *cursor = lines[count], *left;
		int field, letter = 0;

		lines[count][strcspn(lines[count], "\r\n")] = 0;
		for (field = 0; field < 6; field++)
		{
			fields[field] = cursor;
			cursor = strchr(cursor, '\t');
			if (!cursor && field < 5)
				break;
			if (cursor)
				*cursor++ = 0;
		}
		if (field < 6)
			continue;
		for (left = strtok(fields[5], ","); left && letter < MAXIMUM_LETTERS; left = strtok(NULL, ","))
			lefts[count][letter++] = strtof(left, NULL);
		titles[count].tag = fields[0];
		titles[count].bitmap = atoi(fields[1]);
		titles[count].crc = (unsigned int)strtoul(fields[2], NULL, 10);
		titles[count].scale = atoi(fields[3]);
		titles[count].text = fields[4];
		titles[count].lefts = lefts[count];
		count++;
	}
	fclose(file);
	tool = title_tool_new(argv[1], titles, count);
	if (!tool)
		return 1;
	for (index = 0; index < count; index++)
	{
		unsigned long width, height;
		unsigned char *pixels = title_tool_render(tool, index, &width, &height);
		char path[2048];
		FILE *out;

		if (!pixels)
			continue;
		snprintf(path, sizeof(path), "%s/%ld.rgba", argv[3], index);
		out = fopen(path, "wb");
		if (out)
		{
			write_u32(out, width);
			write_u32(out, height);
			fwrite(pixels, 4, width * height, out);
			fclose(out);
			printf("%ld\n", index);
		}
		free(pixels);
	}
	title_tool_free(tool);
	return 0;
}
