#include "harness.h"
#include <string.h>

/* (the fields the code under test reads) */
struct bitmap_data { short width, height, format; unsigned short flags; };
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define BITMAP_GROUP_TAG 0x6269746d
enum { MAXIMUM_HIRES_BITMAPS = 128 };
#include "sheets.inc"

/* a map's HUD bitmaps: each sheet a tag with one bitmap, its pixels'
checksum the stock one's or not, or no tag at all */
#define BACKGROUNDS "ui\\hud\\bitmaps\\combined\\hud_unit_backgrounds"
#define METERS "ui\\hud\\bitmaps\\combined\\hud_unit_meters"
#define NUMBERS "ui\\hud\\bitmaps\\combined\\hud_counter_numbers"
static struct hud_hires_embedded hud_hires_embedded[4];
static struct { char const *tag; int present; unsigned int crc; struct bitmap_data bitmap; } map[4];
static long hud_hires_asset_count(void) { return 4; }
static long tag_loaded(long group, char const *name)
{
    long index;
    (void)group;
    for (index = 0; index < 4; index++)
        if (map[index].present && !strcmp(map[index].tag, name)) return index;
    return NONE;
}
static struct bitmap_data *bitmap_group_try_and_get_bitmap(long group, short index)
{
    return index == 0 ? &map[group].bitmap : NULL;
}
static int mcc_cache_bitmap_source_crc(struct bitmap_data const *bitmap, unsigned long *crc)
{
    long index;
    for (index = 0; index < 4; index++)
        if (bitmap == &map[index].bitmap) { *crc = map[index].crc; return 1; }
    return 0;
}
static unsigned char const *mcc_cache_bitmap_pixels(struct bitmap_data const *bitmap, unsigned long *bytes, void *x)
{
    (void)bitmap; (void)x; *bytes = 0; return NULL;
}
static int mcc_texel(struct bitmap_data const *bitmap, unsigned char const *pixels, unsigned long bytes, long x, long y,
    unsigned char *texel)
{
    (void)bitmap; (void)pixels; (void)bytes; (void)x; (void)y; (void)texel; return 0;
}
static void platform_log(char const *format, ...) { (void)format; }
static void mcc_texture_bridge_hud_refresh(void) { }
static long loaded[MAXIMUM_HIRES_BITMAPS], loaded_count = NONE;
static void hud_hires_map_loaded(const long *assets, long count)
{
    memcpy(loaded, assets, (size_t)count * sizeof(*assets));
    loaded_count = count;
}
static struct { struct bitmap_data const *bitmap; long asset; } mcc_bitmaps[MAXIMUM_HIRES_BITMAPS];
static long mcc_bitmap_count;
#include "under_test.inc"

static void sheets(void)
{
    char const *tags[4] = { BACKGROUNDS, METERS, NUMBERS, BACKGROUNDS };
    long index;
    memset(hud_hires_embedded, 0, sizeof(hud_hires_embedded));
    for (index = 0; index < 4; index++) {
        hud_hires_embedded[index].tag = tags[index];
        hud_hires_embedded[index].bitmap = index == 3 ? 1 : 0;
        hud_hires_embedded[index].width = hud_hires_embedded[index].height = 512;
        hud_hires_embedded[index].crc = 0x1000u + (unsigned int)index;
        hud_hires_embedded[index].mcc = 1;
        if (index < 3) {
            map[index].tag = tags[index];
            map[index].present = 1;
            map[index].crc = 0x1000u + (unsigned int)index;
            map[index].bitmap.width = map[index].bitmap.height = 128;
        }
    }
    /* (the backgrounds' second bitmap is in no map here: bitmap 1 missing) */
}

static int has(long asset)
{
    long index;
    for (index = 0; index < loaded_count; index++)
        if (loaded[index] == asset) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2, "a case");
    sheets();
    if (!strcmp(argv[1], "stock")) {
        hud_hires_mcc_tags_loaded();
        CHECK(loaded_count == 3 && has(0) && has(1) && has(2), "every stock sheet is ours");
    } else if (!strcmp(argv[1], "own_background")) {
        map[0].crc ^= 1;
        hud_hires_mcc_tags_loaded();
        CHECK(!has(0) && !has(1), "a background of the map's own keeps the meters its own too");
        CHECK(has(2) && loaded_count == 1, "the other sheets stay ours");
        CHECK(mcc_bitmap_count == 1 && mcc_bitmaps[0].asset == 2, "and only they stand for the map's bitmaps");
    } else if (!strcmp(argv[1], "own_meter")) {
        map[1].crc ^= 1;
        hud_hires_mcc_tags_loaded();
        CHECK(!has(0) && !has(1) && has(2), "a meter of the map's own keeps the backgrounds its own too");
    } else if (!strcmp(argv[1], "no_meters")) {
        map[1].present = 0;
        hud_hires_mcc_tags_loaded();
        CHECK(has(0) && has(2) && loaded_count == 2, "a sheet the map has not got is not one of its own");
    } else if (!strcmp(argv[1], "own_numbers")) {
        map[2].crc ^= 1;
        hud_hires_mcc_tags_loaded();
        CHECK(has(0) && has(1) && !has(2), "another sheet of the map's own leaves the unit sheets ours");
    } else
        CHECK(0, "a known case");
    return 0;
}
