/*
BITMAP_BYTE_ORDER_TEST.C

A map's pixels read by the CPU as a big-endian console reads them
(port/console/game/bitmap_byte_order.c), the way bitmaps/bitmaps.c's
bitmap_2d_get_pixel does there: a 16-bit and a 32-bit texel, and one pixel
of a DXT1, a DXT3 and a DXT5 block decoded by bitmaps/s3tc/s3tc.c, each
given as the Xbox's little-endian bytes and checked against the pixel32
(0xAARRGGBB) the Xbox reads. The helpers read byte by byte, so the host's
own byte order does not matter. Built by tools/test_bitmap_byte_order.py.
*/

#include <stdio.h>
#include <string.h>

#include "bitmaps/s3tc/s3tc.h"
#include "bitmap_byte_order.h"

static int failures;

static void check(const char *what, unsigned long got, unsigned long expected)
{
	if (got != expected)
	{
		printf("FAIL %s: 0x%08lx, expected 0x%08lx\n", what, got, expected);
		failures++;
	}
	else
		printf("ok   %s: 0x%08lx\n", what, got);
}

/* bitmaps.c's r5g6b5 case, on a texel read through the helper */
static unsigned long r5g6b5(unsigned short pixel)
{
	return 0xFF000000UL |
		((unsigned long)((((pixel >> 11) & 0x1F) << 3) | (((pixel >> 11) & 0x1F) >> 2)) << 16) |
		((unsigned long)((((pixel >> 5) & 0x3F) << 2) | (((pixel >> 5) & 0x3F) >> 4)) << 8) |
		(unsigned long)(((pixel & 0x1F) << 3) | ((pixel & 0x1F) >> 2));
}

int main(void)
{
	/* an orange lightmap texel, r5g6b5 0xFD20 (31, 41, 0): 0xFFFFA600 */
	static const unsigned char lightmap_texel[2] = { 0x20, 0xFD };
	/* an x8r8g8b8 texel, 0xFF336699 */
	static const unsigned char texel32[4] = { 0x99, 0x66, 0x33, 0xFF };
	/* DXT1: colors orange (0xFD20) and blue (0x001F), pixel (1, 0) the
	second color, the rest the first */
	static const unsigned char dxt1[8] = { 0x20, 0xFD, 0x1F, 0x00, 0x04, 0x00, 0x00, 0x00 };
	/* DXT3: the first row's alpha 0x00F0 (pixel 1 opaque), then the DXT1
	block above */
	static const unsigned char dxt3[16] =
	{
		0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x20, 0xFD, 0x1F, 0x00, 0x04, 0x00, 0x00, 0x00,
	};
	/* DXT5: alphas 0x80 and 0x00, every pixel the first, then the DXT1
	block above */
	static const unsigned char dxt5[16] =
	{
		0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x20, 0xFD, 0x1F, 0x00, 0x04, 0x00, 0x00, 0x00,
	};
	union
	{
		struct s3tc_block_rgb rgb;
		struct s3tc_block_alpha4 alpha4;
		struct s3tc_block_alpha3 alpha3;
	} block;
	struct s3tc_color color;

	check("r5g6b5 lightmap texel", r5g6b5(bitmap_byte_order_read16(lightmap_texel)), 0xFFFFA600UL);
	check("x8r8g8b8 texel", bitmap_byte_order_read32(texel32), 0xFF336699UL);

	bitmap_byte_order_s3tc_rgb(dxt1, &block.rgb);
	check("dxt1 block color0", block.rgb.color0, 0xFD20);
	check("dxt1 block color1", block.rgb.color1, 0x001F);
	check("dxt1 block indices", block.rgb.bitmap, 0x00000004UL);
	DecodeBlockRGB__single_pixel(&block.rgb, &color, 0, 0);
	check("dxt1 pixel (0, 0)", bitmap_byte_order_s3tc_color(&color), 0xFFFFA600UL);
	DecodeBlockRGB__single_pixel(&block.rgb, &color, 1, 0);
	check("dxt1 pixel (1, 0)", bitmap_byte_order_s3tc_color(&color), 0xFF0000FFUL);

	bitmap_byte_order_s3tc_alpha4(dxt3, &block.alpha4);
	DecodeBlockAlpha4__single_pixel(&block.alpha4, &color, 0, 0);
	check("dxt3 pixel (0, 0)", bitmap_byte_order_s3tc_color(&color), 0x00FFA600UL);
	DecodeBlockAlpha4__single_pixel(&block.alpha4, &color, 1, 0);
	check("dxt3 pixel (1, 0)", bitmap_byte_order_s3tc_color(&color), 0xFF0000FFUL);

	bitmap_byte_order_s3tc_alpha3(dxt5, &block.alpha3);
	DecodeBlockAlpha3__single_pixel(&block.alpha3, &color, 0, 0);
	check("dxt5 pixel (0, 0)", bitmap_byte_order_s3tc_color(&color), 0x80FFA600UL);

	printf("%s\n", failures ? "bitmap byte order: FAILED" : "bitmap byte order: all good");
	return failures ? 1 : 0;
}
