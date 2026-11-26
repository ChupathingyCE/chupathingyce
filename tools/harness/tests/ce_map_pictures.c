/*
CE_MAP_PICTURES.C (test)

Where the menus' map list finds Halo PC's map pictures in a bitmaps.map
(port/linux/game/ui_map_list.c: ce_bitmaps_header_valid,
ce_bitmaps_resource, ce_picture_pixels): Custom Edition's keeps them in one
resource at the offsets its ui.map gives, Halo PC retail's and MCC's each in
its own, elsewhere; any other file, or a table that points outside its file,
gives none. test_ce_map_pictures.py takes the code (under_test.inc).
*/

#include "harness.h"

#include <strings.h>

#define _stricmp strcasecmp

#include "under_test.inc"

#define PIXELS "ui\\shell\\bitmaps\\mp_map_grafix__pixels"
#define PICTURE_SIZE 16384UL

/* a table of up to 8 resources, their paths one after another */
struct table_maker
{
	struct ce_bitmaps_table table;
	unsigned long resources[8 * 3];
	char paths[512];
};

static void table_begin(struct table_maker *maker, unsigned long file_size)
{
	memset(maker, 0, sizeof(*maker));
	maker->table.resources = maker->resources;
	maker->table.paths = maker->paths;
	maker->table.file_size = file_size;
}

static void table_add(struct table_maker *maker, char const *path, unsigned long size, unsigned long offset)
{
	unsigned long *resource = maker->resources + maker->table.count++ * 3;

	resource[0] = maker->table.paths_size;
	resource[1] = size;
	resource[2] = offset;
	strcpy(maker->paths + maker->table.paths_size, path);
	maker->table.paths_size += (unsigned long)strlen(path) + 1;
}

/* a deterministic sequence for the crafted tables */
static unsigned long next_random(unsigned long *state)
{
	*state = *state * 1103515245UL + 12345UL;
	return (*state >> 8) ^ (*state << 13);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct table_maker maker;
	unsigned long found;

	/* Custom Edition's: the pictures in <tag>__pixels, at their own offsets */
	CASE("custom_edition")
	{
		table_begin(&maker, 0x1000000);
		table_add(&maker, "ui\\shell\\bitmaps\\other__pixels", 0x100, 0x10);
		table_add(&maker, PIXELS, 21 * PICTURE_SIZE, 0xd62628);
		found = 0;
		CHECK(ce_picture_pixels(&maker.table, 19, 0xd62628 + 19 * PICTURE_SIZE, PICTURE_SIZE, &found),
			"the unknown level's picture was not found");
		CHECK(found == 0xd62628 + 19 * PICTURE_SIZE, "found at 0x%lx", found);
		CHECK(ce_picture_pixels(&maker.table, 0, 0xd62628, PICTURE_SIZE, &found) && found == 0xd62628,
			"the first picture was not found where it is");
		CHECK(!ce_picture_pixels(&maker.table, 20, 0xd62628 + 20 * PICTURE_SIZE + 1, PICTURE_SIZE, &found),
			"a picture running past its resource was taken");
		CHECK(!ce_picture_pixels(&maker.table, 0, 0xd62628 - 1, PICTURE_SIZE, &found),
			"a picture starting before its resource was taken");
		return 0;
	}
	/* Halo PC retail's and MCC's: each picture its own resource, <tag>_<index>,
	found there whatever offset Custom Edition's ui.map gives */
	CASE("halo_pc")
	{
		table_begin(&maker, 0x3000000);
		table_add(&maker, "ui\\shell\\bitmaps\\mp_map_grafix_0", PICTURE_SIZE, 0x29e5c70);
		table_add(&maker, "UI\\Shell\\Bitmaps\\MP_Map_Grafix_19", PICTURE_SIZE, 0x2a31c70);
		table_add(&maker, "ui\\shell\\bitmaps\\mp_map_grafix_1", PICTURE_SIZE / 2, 0x29e9c70);
		found = 0;
		CHECK(ce_picture_pixels(&maker.table, 19, 0xdae628, PICTURE_SIZE, &found) && found == 0x2a31c70,
			"the unknown level's picture was not found by its path (0x%lx)", found);
		CHECK(ce_picture_pixels(&maker.table, 0, 0xd62628, PICTURE_SIZE, &found) && found == 0x29e5c70,
			"the first picture was not found by its path (0x%lx)", found);
		CHECK(!ce_picture_pixels(&maker.table, 1, 0xd66628, PICTURE_SIZE, &found),
			"a picture of another size was taken");
		CHECK(!ce_picture_pixels(&maker.table, 2, 0xd6a628, PICTURE_SIZE, &found),
			"a picture the file has not was taken");
		CHECK(!ce_picture_pixels(&maker.table, 1, 0x29e9c70, PICTURE_SIZE, &found),
			"a picture was taken at an offset without its resource");
		return 0;
	}
	/* a resource's path and bytes must lie in the file; offsets that wrap
	are no picture */
	CASE("bounds")
	{
		table_begin(&maker, 0x100000);
		table_add(&maker, PIXELS, 0x10000, 0xf8000);
		CHECK(ce_bitmaps_resource(&maker.table, PIXELS) == NONE, "a resource running past the file was found");
		CHECK(!ce_picture_pixels(&maker.table, 0, 0xf8000, PICTURE_SIZE, &found), "pixels past the file were taken");
		table_begin(&maker, 0x100000);
		table_add(&maker, PIXELS, 0x10000, 0xf0000);
		CHECK(ce_bitmaps_resource(&maker.table, PIXELS) == 0, "the file's last resource was not found");
		CHECK(!ce_picture_pixels(&maker.table, 0, 0xFFFFFFF0UL, 0x20, &found), "an offset that wraps was taken");
		CHECK(!ce_picture_pixels(&maker.table, 0, 0xf0010, 0xFFFFFFF8UL, &found), "a size that wraps was taken");
		CHECK(ce_picture_pixels(&maker.table, 0, 0xf0000 + 0x10000 - PICTURE_SIZE, PICTURE_SIZE, &found),
			"the resource's last picture was refused");
		/* (a path's offset past the paths) */
		maker.resources[0] = maker.table.paths_size;
		CHECK(ce_bitmaps_resource(&maker.table, PIXELS) == NONE, "a path outside the paths was read");
		return 0;
	}
	/* a bitmaps.map's header: Custom Edition's numbers, and what isn't one */
	CASE("headers")
	{
		unsigned long custom_edition[4] = { 1, 0x75b90b3, 0x75ce06d, 1706 };
		unsigned long halo_pc[4] = { 1, 0xb0e95f3, 0xb0fec2a, 1818 };
		unsigned long header[4];

		CHECK(ce_bitmaps_header_valid(custom_edition, 123547749), "Custom Edition's bitmaps.map was refused");
		CHECK(ce_bitmaps_header_valid(halo_pc, 185614690), "MCC's bitmaps.map was refused");
		memcpy(header, custom_edition, sizeof(header));
		header[0] = 2;
		CHECK(!ce_bitmaps_header_valid(header, 123547749), "a sounds.map was taken");
		memcpy(header, custom_edition, sizeof(header));
		header[3] = 0;
		CHECK(!ce_bitmaps_header_valid(header, 123547749), "a map without resources was taken");
		header[3] = 0x10001;
		CHECK(!ce_bitmaps_header_valid(header, 123547749), "too many resources were taken");
		memcpy(header, custom_edition, sizeof(header));
		CHECK(!ce_bitmaps_header_valid(header, 0x75ce06d + 1706 * 12 - 1), "a table past the file's end was taken");
		CHECK(ce_bitmaps_header_valid(header, 0x75ce06d + 1706 * 12), "a table ending the file was refused");
		header[1] = header[2] + 1;
		CHECK(!ce_bitmaps_header_valid(header, 123547749), "paths after the table were taken");
		header[1] = 8;
		CHECK(!ce_bitmaps_header_valid(header, 123547749), "paths inside the header were taken");
		memcpy(header, custom_edition, sizeof(header));
		header[1] = header[2] - 0x400001;
		CHECK(!ce_bitmaps_header_valid(header, 123547749), "more than 4 MB of paths were taken");
		memcpy(header, custom_edition, sizeof(header));
		header[1] = 0xFFFFFF00UL;
		header[2] = 0xFFFFFFF0UL;
		CHECK(!ce_bitmaps_header_valid(header, 123547749), "a table past the file was taken");
		return 0;
	}
	/* crafted tables: whatever a found picture is, its bytes lie in the file */
	CASE("crafted")
	{
		unsigned long state = 1, round;

		for (round = 0; round < 200000; round++)
		{
			unsigned long header[4], index, size, offset;
			long picture = (long)(next_random(&state) % 24);

			for (index = 0; index < 4; index++)
				header[index] = next_random(&state) % 8 == 0 ? next_random(&state) : next_random(&state) % 0x2000;
			header[0] = next_random(&state) % 4 ? 1 : header[0];
			if (ce_bitmaps_header_valid(header, 0x4000))
			{
				CHECK(header[2] <= 0x4000 && header[3] * 12 <= 0x4000 - header[2] && header[1] <= header[2],
					"a header whose table isn't in the file was taken: %lx %lx %lx", header[1], header[2], header[3]);
			}
			table_begin(&maker, next_random(&state) % 0x40000000UL);
			for (index = 0; index < 1 + next_random(&state) % 7; index++)
			{
				char path[64];

				if (next_random(&state) % 2)
					snprintf(path, sizeof(path), "%s", PIXELS);
				else
					snprintf(path, sizeof(path), "ui\\shell\\bitmaps\\mp_map_grafix_%ld", (long)(next_random(&state) % 24));
				table_add(&maker, path, next_random(&state) % 3 ? PICTURE_SIZE : next_random(&state),
					next_random(&state) % 3 ? next_random(&state) % 0x40000000UL : next_random(&state));
				if (next_random(&state) % 16 == 0)
					maker.resources[index * 3] = next_random(&state);
			}
			size = next_random(&state) % 2 ? PICTURE_SIZE : next_random(&state);
			offset = next_random(&state);
			if (ce_picture_pixels(&maker.table, picture, offset, size, &found))
			{
				CHECK(found <= maker.table.file_size && size <= maker.table.file_size - found,
					"a picture outside its file was found: 0x%lx bytes at 0x%lx of 0x%lx", size, found,
					maker.table.file_size);
			}
		}
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
