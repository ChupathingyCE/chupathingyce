#include "harness.h"

/* the parent's placement, with a field after its corner that must come
through untouched */
struct hud_absolute_placement_definition { short corner, pad; };
#include "anchors.inc"
#define HALO_CUSTOM_EDITION 1
/* ce_hud.c's state */
static boolean ce_hud_child_anchors;
static long ce_hud_meter_bitmap_count, ce_hud_button_icon_group_count;
/* an MCC map's anchor (mcc_hud_child_placement), MCC's own: here it takes
the element's first short when an MCC map is loaded */
static int mcc;
static struct hud_absolute_placement_definition const *mcc_hud_child_placement(
    struct hud_absolute_placement_definition const *parent, void const *element,
    struct hud_absolute_placement_definition *storage)
{
    short child = *(short const *)element;

    if (!mcc || child < 1 || child > 9)
        return parent;
    *storage = *parent;
    storage->corner = (short)(child - 1);
    return storage;
}
#include "under_test.inc"

static void each_anchor(int custom_edition, int mcc_map)
{
    struct hud_absolute_placement_definition parent = {_hud_anchor_top_left, 123}, storage = {77, 77};
    struct hud_absolute_placement_definition const *result;
    short child;

    ce_hud_child_anchors = custom_edition;
    mcc = mcc_map;
    for (child = -2; child <= 12; child++) {
        short element = child;

        storage.corner = 77;
        result = weapon_hud_element_placement(&parent, &element, child, &storage);
        if (mcc_map && child >= 1 && child <= 9) {
            CHECK(result == &storage && result->corner == child - 1, "an MCC map's anchor is MCC's");
        } else if (custom_edition && child >= 1 && child <= 5) {
            CHECK(result == &storage, "a corner or the middle is the element's own");
            CHECK(result->corner == child - 1, "the corner it names");
        } else {
            CHECK(result == &parent, "from the parent, an edge's middle or out of range: the parent's");
        }
        CHECK(result->pad == 123, "the rest of the parent's placement kept");
        CHECK(parent.corner == _hud_anchor_top_left && parent.pad == 123, "the parent tag never written");
    }
    CHECK(weapon_hud_element_placement(&parent, &(short){(short)0x8005}, (short)0x8005, &storage) == &parent,
        "a value with high bits is not one");
}

int main(int argc, char **argv)
{
    CHECK(argc == 2, "a case");
    if (!strcmp(argv[1], "xbox"))
        each_anchor(0, 0);
    else if (!strcmp(argv[1], "custom_edition"))
        each_anchor(1, 0);
    else if (!strcmp(argv[1], "mcc"))
        each_anchor(0, 1);
    else if (!strcmp(argv[1], "unloaded")) {
        /* a Custom Edition map, then an Xbox map: nothing kept */
        ce_hud_child_anchors = 1;
        ce_hud_meter_bitmap_count = ce_hud_button_icon_group_count = 3;
        ce_hud_tags_unloaded();
        CHECK(!ce_hud_meter_bitmap_count && !ce_hud_button_icon_group_count, "the rest let go of as before");
        each_anchor(ce_hud_child_anchors, 0);
        CHECK(ce_hud_child_anchors == 0, "unloading turns them off");
    }
    else
        CHECK(0, "a known case");
    return 0;
}
