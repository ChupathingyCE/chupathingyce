/*
MCC_PREVIEW_PNG_FUZZ.C

libFuzzer's entry for the MCC map previews' own PNG reader
(mcc_preview_png, mcc_preview_image.c), what a Workshop item's preview file
holds (tools/test_host_fuzz.py builds it, target mcc_preview_png). The bytes
come in an allocation of exactly their size; the loaders it is reached from
are stubbed out here.
*/

#include "../src/mcc_preview_image.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct mcc_menu_metadata;
struct steam_workshop_map_text;

int mcc_manifest_package_root(const char *level, char *root)
{
	(void)level;
	(void)root;
	return 0;
}

int mcc_menu_metadata_load(const char *root, int campaign, struct mcc_menu_metadata *metadata)
{
	(void)root;
	(void)campaign;
	(void)metadata;
	return 0;
}

const struct steam_workshop_map_text *mcc_menu_metadata_find(const struct mcc_menu_metadata *metadata, const char *name)
{
	(void)metadata;
	(void)name;
	return NULL;
}

int posix_mcc_preview_read(const char *root, const char *relative, unsigned char *buffer, unsigned capacity)
{
	(void)root;
	(void)relative;
	(void)buffer;
	(void)capacity;
	return -1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct mcc_preview_image image;
	unsigned char *copy;
	int w, h;

	if (size > MCC_PREVIEW_FILE_LIMIT)
		return 0;
	copy = (unsigned char *)malloc(size ? size : 1);
	if (!copy)
		return 0;
	memcpy(copy, data, size);
	if (mcc_preview_png(copy, (unsigned)size, &image))
	{
		volatile uint32_t sum = image.pixels[0] ^ image.pixels[image.width * image.height - 1];

		(void)sum;
		mcc_preview_fit(image.width, image.height, 320, 180, &w, &h);
		mcc_preview_image_free(&image);
	}
	free(copy);
	return 0;
}
