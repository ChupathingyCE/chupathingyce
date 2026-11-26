#include "harness.h"
#include <string.h>

/* (the tag fields the code under test reads, as the game's 32-bit build has them) */
struct tag_reference { long group_tag; long name; long name_length; long index; };
struct tag_block { long count; long address; long definition; };
#include "structures.inc"

/* our own MCC pause screen's widgets (mcc_pause_definition) and the map's
(tag_get), and whether an MCC map is loaded */
static struct mcc_pause_widget owned_objective, owned_other, map_widget;
static struct mcc_pause_input inputs[64];
static int mcc_map = 1;
#define OWNED_OBJECTIVE 0x10001
#define OWNED_OTHER 0x10002
#define MAP_WIDGET 0x20003
static struct mcc_pause_widget *mcc_pause_definition(long handle)
{
    return handle == OWNED_OBJECTIVE ? &owned_objective : handle == OWNED_OTHER ? &owned_other : NULL;
}
static void *tag_get(long group, long handle)
{
    CHECK(group == 0x44654c61, "a widget's definition");
    return handle == MAP_WIDGET ? &map_widget : NULL;
}
static int mcc_cache_tags_loaded(void) { return mcc_map; }
#include "under_test.inc"

int main(int argc, char **argv)
{
    CHECK(argc == 2, "a case");
    strcpy(owned_objective.name, "objective");
    strcpy(owned_other.name, "pause_actions");
    map_widget.type = 1;
    map_widget.inputs.count = 1;
    map_widget.inputs.address = (long)inputs;
    inputs[0].function = MCC_PAUSE_OBJECTIVE_INPUT;
    if (!strcmp(argv[1], "owned")) {
        CHECK(mcc_pause_text_wrap_needed(OWNED_OBJECTIVE), "our own pause screen's objective wraps");
        CHECK(!mcc_pause_text_wrap_needed(OWNED_OTHER), "its other widgets don't");
    } else if (!strcmp(argv[1], "map_objective")) {
        CHECK(mcc_pause_text_wrap_needed(MAP_WIDGET), "an MCC map's own objective text box wraps");
        inputs[0].function = 1; inputs[1].function = MCC_PAUSE_OBJECTIVE_INPUT; map_widget.inputs.count = 2;
        CHECK(mcc_pause_text_wrap_needed(MAP_WIDGET), "whichever of its inputs shows the objective");
    } else if (!strcmp(argv[1], "other_text")) {
        inputs[0].function = 17;
        CHECK(!mcc_pause_text_wrap_needed(MAP_WIDGET), "another text box keeps the Xbox's drawing");
        inputs[0].function = MCC_PAUSE_OBJECTIVE_INPUT; map_widget.type = 0;
        CHECK(!mcc_pause_text_wrap_needed(MAP_WIDGET), "a widget that is no text box isn't one");
        CHECK(!mcc_pause_text_wrap_needed(NONE), "nor is no widget");
    } else if (!strcmp(argv[1], "xbox_map")) {
        mcc_map = 0;
        CHECK(!mcc_pause_text_wrap_needed(MAP_WIDGET), "another map's objective keeps the Xbox's drawing");
    } else if (!strcmp(argv[1], "crafted")) {
        /* a map's inputs beyond the most read, or none at no address: not read */
        inputs[0].function = 1;
        inputs[40].function = MCC_PAUSE_OBJECTIVE_INPUT;
        map_widget.inputs.count = 41;
        CHECK(!mcc_pause_text_wrap_needed(MAP_WIDGET), "more inputs than any widget has are not read");
        map_widget.inputs.count = -1;
        CHECK(!mcc_pause_text_wrap_needed(MAP_WIDGET), "nor a negative count");
        map_widget.inputs.count = 1; map_widget.inputs.address = 0; inputs[0].function = MCC_PAUSE_OBJECTIVE_INPUT;
        CHECK(!mcc_pause_text_wrap_needed(MAP_WIDGET), "nor a block at no address");
    } else
        CHECK(0, "a known case");
    return 0;
}
