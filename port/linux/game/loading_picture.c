/*
LOADING_PICTURE.C

The picture the loading screen shows. The Xbox's was loading.tga in the
disc's maps folder, which progress_bar_initialize copies to the utility
drive (z:) for the loading screen to read. The port shows its own instead,
Chupathingy's (port/assets/loading/chupathingy_loading.tga, made by
tools/make_loading_picture.py and embedded in the game by
tools/embed_assets.py), written beside that copy when the game starts.

With display.xbox_loading_picture on it shows the Xbox's: xbox_loading.tga
in the maps folder (the setup screen's disc import keeps the disc's
loading.tga under that name: port/linux/src/xiso.c, XisoExtractor.java),
else the disc's loading.tga, else Chupathingy's. A picture is used only if
progress_bar.c can read it (an uncompressed 24-bit TGA of 320 by 240).

A console's build leaves this out (HALO_CONSOLE_NO_LOADING_PICTURE,
halo_console_features.h): it shows the disc's picture, as the Xbox did.
*/

#ifndef HALO_CONSOLE_NO_LOADING_PICTURE

#include "cseries.h"
#include "cache/cache_files.h"

#include <stdio.h>

#include "loading_picture.h"

/* port/linux/src/port_config.c */
int config_boolean(const char *name);

/* the embedded picture (tools/embed_assets.py); of size 0 in a checkout
without it */
extern const unsigned int loading_picture_data[];
extern const unsigned long loading_picture_data_size;

/* ---------- constants */

#define LOADING_PICTURE_HEADER_SIZE 18

/* ---------- private code */

/* a file of the maps folder, on drive (d: the maps, z: the utility drive) */
static void loading_picture_file(
	char drive,
	const char *name,
	char *path,
	long size)
{
	const char *map_directory = cache_files_map_directory();

	snprintf(path, (size_t)size, "%c%s%s", drive, map_directory + 1, name);
}

/* whether the file is a picture progress_bar.c reads */
static boolean loading_picture_usable(
	const char *path)
{
	unsigned char header[LOADING_PICTURE_HEADER_SIZE];
	boolean usable = FALSE;
	FILE *file = fopen(path, "rb");

	if (file)
	{
		if (fread(header, sizeof(header), 1, file) == 1)
		{
			usable = header[2] == 2 && header[16] == 24 &&
				(header[12] | header[13] << 8) == 320 && (header[14] | header[15] << 8) == 240;
		}
		fclose(file);
	}

	return usable;
}

/* ---------- public code */

void loading_picture_initialize(
	void)
{
	char path[256];
	FILE *file;

	if (!loading_picture_data_size)
		return;
	loading_picture_file('z', "chupathingy_loading.tga", path, sizeof(path));
	file = fopen(path, "wb");
	if (file)
	{
		fwrite(loading_picture_data, loading_picture_data_size, 1, file);
		fclose(file);
	}

	return;
}

void loading_picture_path(
	char *path,
	long size)
{
	if (config_boolean("display.xbox_loading_picture"))
	{
		loading_picture_file('d', "xbox_loading.tga", path, size);
		if (loading_picture_usable(path))
			return;
		/* (the disc's loading.tga, copied by progress_bar_initialize) */
		loading_picture_file('z', "loading.tga", path, size);
		if (loading_picture_usable(path))
			return;
	}
	loading_picture_file('z', "chupathingy_loading.tga", path, size);
	if (loading_picture_usable(path))
		return;
	/* (a checkout without Chupathingy's picture: the disc's, as before) */
	loading_picture_file('z', "loading.tga", path, size);

	return;
}

#endif
