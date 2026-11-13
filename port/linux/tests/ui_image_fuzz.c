/*
UI_IMAGE_FUZZ.C

libFuzzer's entry for the overlay's picture decoder (posix_ui_image_decode,
posix_ui_font.c: stb_image, JPEG and PNG only, its size limited): what a
Workshop item's preview, a Steam picture or the site's map art may hold
(tools/test_host_fuzz.py builds it, target ui_image). The bytes come in an
allocation of exactly their size.
*/

#include "../src/ui_font.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* (the built-in fonts the build generates: none here, no font is drawn) */
const unsigned int *const ui_font_files[POSIX_UI_FONT_COUNT];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned char *copy, *pixels;
	int width, height;

	if (size > (1u << 22))
		return 0;
	copy = (unsigned char *)malloc(size ? size : 1);
	if (!copy)
		return 0;
	memcpy(copy, data, size);
	pixels = posix_ui_image_decode(copy, (int)size, &width, &height);
	if (pixels)
	{
		/* (every pixel there to be read) */
		volatile unsigned char sum = 0;
		size_t index, count = (size_t)width * (size_t)height * 4;

		for (index = 0; index < count; index += 4093)
			sum ^= pixels[index];
		sum ^= pixels[count - 1];
		posix_ui_image_free(pixels);
	}
	free(copy);
	return 0;
}
