#include "harness.h"
#include <string.h>

/* (the fields the code under test reads) */
struct bitmap_data { short width, height, format; unsigned short flags; };
#include "under_test.inc"

static struct hud_hires_embedded sheet(int mcc, unsigned int width, unsigned int height)
{
    struct hud_hires_embedded embedded;
    memset(&embedded, 0, sizeof(embedded));
    embedded.tag = "ui\\hud\\bitmaps\\combined\\hud_counter_numbers";
    embedded.bitmap = 0;
    embedded.width = width;
    embedded.height = height;
    embedded.crc = 0x86f3f8fcu;
    embedded.mcc = mcc;
    return embedded;
}

/* the Xbox's swizzled index of x, y (x and y bits interleaved, x first) */
static unsigned long swizzled(long x, long y, long width, long height)
{
    unsigned long index = 0, bit = 0;
    long sx = 0, sy = 0;
    while ((1L << sx) < width || (1L << sy) < height) {
        if ((1L << sx) < width) index |= (unsigned long)((x >> sx++) & 1) << bit++;
        if ((1L << sy) < height) index |= (unsigned long)((y >> sy++) & 1) << bit++;
    }
    return index;
}

int main(int argc, char **argv)
{
    char const *tag = "ui\\hud\\bitmaps\\combined\\hud_counter_numbers";
    if (argc > 1 && !strcmp(argv[1], "match")) {
        struct hud_hires_embedded same = sheet(1, 512, 256), larger = sheet(1, 1024, 512);
        struct hud_hires_embedded xbox = sheet(0, 512, 256), smaller = sheet(1, 256, 128), other = sheet(1, 1024, 256);
        /* the stock sheet, at its size or a whole multiple of it */
        CHECK(hud_hires_mcc_match(&same, tag, 0, 512, 256, 0x86f3f8fcu), "the stock sheet is found");
        CHECK(hud_hires_mcc_match(&larger, tag, 0, 512, 256, 0x86f3f8fcu), "a 2x texture stands for it");
        /* a map maker's own art under the stock name keeps its look */
        CHECK(!hud_hires_mcc_match(&same, tag, 0, 512, 256, 0x86f3f8fdu), "other pixels are not the stock sheet");
        /* and a sheet of another name, index or size is not it either */
        CHECK(!hud_hires_mcc_match(&same, "ui\\hud\\bitmaps\\combined\\my_numbers", 0, 512, 256, 0x86f3f8fcu),
            "another tag is not the stock sheet");
        CHECK(!hud_hires_mcc_match(&same, tag, 1, 512, 256, 0x86f3f8fcu), "another bitmap is not the stock sheet");
        CHECK(!hud_hires_mcc_match(&same, tag, 0, 1024, 512, 0x86f3f8fcu), "a larger bitmap is not the stock sheet");
        CHECK(!hud_hires_mcc_match(&smaller, tag, 0, 512, 256, 0x86f3f8fcu), "a texture never stands for more texels");
        CHECK(!hud_hires_mcc_match(&other, tag, 0, 512, 256, 0x86f3f8fcu), "nor one of another shape");
        CHECK(!hud_hires_mcc_match(&same, tag, 0, 0, 0, 0x86f3f8fcu), "an empty bitmap is nothing");
        /* the Xbox maps' textures are never an MCC map's */
        CHECK(!hud_hires_mcc_match(&xbox, tag, 0, 512, 256, 0x86f3f8fcu), "an Xbox sheet is not for MCC maps");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "texels")) {
        unsigned char argb[8 * 4 * 4], a8y8[8 * 4 * 2], rgba[4];
        struct bitmap_data bitmap = {8, 4, 11, 0};
        long x, y;
        for (y = 0; y < 4; y++)
            for (x = 0; x < 8; x++) {
                unsigned long at = swizzled(x, y, 8, 4);
                argb[at * 4] = (unsigned char)x; argb[at * 4 + 1] = (unsigned char)y;
                argb[at * 4 + 2] = 200; argb[at * 4 + 3] = (unsigned char)(x * 16 + y);
                a8y8[at * 2] = (unsigned char)(x * 10 + y); a8y8[at * 2 + 1] = 77;
            }
        for (y = 0; y < 4; y++)
            for (x = 0; x < 8; x++) {
                bitmap.format = 11;
                CHECK(mcc_texel(&bitmap, argb, sizeof(argb), x, y, rgba), "a8r8g8b8 is read");
                CHECK(rgba[0] == 200 && rgba[1] == y && rgba[2] == x && rgba[3] == x * 16 + y, "a8r8g8b8 texel");
                bitmap.format = 3;
                CHECK(mcc_texel(&bitmap, a8y8, sizeof(a8y8), x, y, rgba), "a8y8 is read");
                CHECK(rgba[0] == x * 10 + y && rgba[1] == rgba[0] && rgba[2] == rgba[0] && rgba[3] == 77,
                    "a8y8 texel");
            }
        bitmap.format = 11;
        CHECK(!mcc_texel(&bitmap, argb, sizeof(argb), 8, 0, rgba), "outside the bitmap");
        CHECK(!mcc_texel(&bitmap, argb, sizeof(argb) - 4, 7, 3, rgba), "past its pixels");
        bitmap.flags = 16;
        CHECK(!mcc_texel(&bitmap, argb, sizeof(argb), 0, 0, rgba), "linear sheets are not read");
        bitmap.flags = 0;
        bitmap.format = 14;
        CHECK(!mcc_texel(&bitmap, argb, sizeof(argb), 0, 0, rgba), "compressed sheets are not read");
        bitmap.format = 11;
        bitmap.width = 6;
        CHECK(!mcc_texel(&bitmap, argb, sizeof(argb), 0, 0, rgba), "nor sheets of other sizes than powers of two");
        return 0;
    }
    return 2;
}
