/*
BITMAP_BYTE_ORDER.H

A map's pixels as a big-endian console's CPU reads them (the Xbox 360, the
Wii U). A bitmap's pixel data is the Xbox's, little-endian, and is never
swapped: the GPU reads it as it is (port/linux/src/xbox_textures.c). The
game also reads some pixels itself: the light a particle or an object takes
from the lightmap under it, and the ground's color (objects/object_lights.c
through bitmaps/bitmaps.c's bitmap_2d_get_pixel). Those reads go through
here, so that a 16-bit or 32-bit texel, a DXT block's colors and indices,
and the color a DXT block decodes to come out as the Xbox has them.
Read byte by byte: the same on any host (port/console/tests/
bitmap_byte_order_test.c).
*/

#ifndef __BITMAP_BYTE_ORDER_H
#define __BITMAP_BYTE_ORDER_H

/* a 16-bit or 32-bit texel of a map's pixels, at address */
unsigned short bitmap_byte_order_read16(void const *address);
unsigned long bitmap_byte_order_read32(void const *address);

/* a DXT block's colors and indices (8 bytes: two 16-bit colors and 32 bits
of indices, bitmaps/s3tc/s3tc.h's s3tc_block_rgb) copied with their words
in the CPU's order */
void bitmap_byte_order_s3tc_rgb(void const *block, void *copy);

/* a DXT3 block (16 bytes: four 16-bit rows of alpha, then the colors'
block, s3tc_block_alpha4) copied with every word in the CPU's order */
void bitmap_byte_order_s3tc_alpha4(void const *block, void *copy);

/* a DXT5 block (16 bytes: the alpha bytes, then the colors' block,
s3tc_block_alpha3) copied with the colors' words in the CPU's order */
void bitmap_byte_order_s3tc_alpha3(void const *block, void *copy);

/* the color s3tc.c decodes (its bytes blue, green, red, alpha, a pixel32 as
the Xbox keeps it in memory) as a pixel32, 0xAARRGGBB */
unsigned long bitmap_byte_order_s3tc_color(void const *color);

#endif
