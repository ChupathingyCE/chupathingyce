/* Native MCC linker tests with generated syntax, no game assets. */
#include "cseries.h"
#include "math/real_math.h"
#include "errors.h"
#include "hs/hs.h"
#include "hs/hs_scenario_definitions.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "scenario/scenario_definitions.h"
#include "mcc_runtime.h"
#include "mcc_scripts.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#undef malloc
#undef free
#undef memset
#undef memcpy
#undef strcmp
#undef strcpy

static boolean loaded;
static struct data_array list_data;
struct data_array *object_list_header_data = &list_data;
static long arguments[2], returned;
static struct object_datum mock_object;
static int deleted_lists;
word const hs_object_type_masks[6] = {0xFFFF, 3, 2, 4, 0x380, 0x40};
struct scenario *global_scenario_get(void) { return NULL; }
boolean ai_index_from_string(struct scenario *scenario, char const *name, long *result) {
    (void)scenario;
    if (strcmp(name, "falcon/pilot")) return FALSE;
    *result = (long)0x80000018; return TRUE;
}
long object_list_from_ai_reference(long reference) { return reference == NONE ? NONE : 0; }
void object_list_delete(long list) { (void)list; ++deleted_lists; }

void *csmemcpy(void *to, void const *from, unsigned long bytes) { return memcpy(to, from, bytes); }
long csstrcasecmp(char const *a, char const *b) { return _stricmp(a, b); }
void error(short priority, char const *format, ...) { (void)priority; (void)format; }
boolean mcc_cache_tags_loaded(void) { return loaded; }
int mcc_parameters_prepare(struct mcc_runtime *runtime, struct scenario *scenario, struct data_array *syntax) {
    (void)runtime; (void)scenario; (void)syntax; return 1;
}
boolean hs_macro_function_parse(short index, long expression) { (void)index; (void)expression; return TRUE; }
long *hs_macro_function_evaluate(short function, long thread, boolean initialize) {
    (void)function; (void)thread; return initialize ? arguments : NULL;
}
void hs_return(long thread, long value) { (void)thread; returned = value; }
short hs_find_function_by_name(char const *name) {
    if (!strcmp(name, "sleep")) return 321;
    if (!strcmp(name, "player_effect_set_max_rumble")) return 322;
    return NONE;
}
short hs_find_global_by_name(char const *name) { return !strcmp(name, "known_global") ? (short)0x8009 : NONE; }
void *object_try_and_get_and_verify_type(long index, unsigned long mask) {
    (void)mask; return index >= 0 && index <= 2 ? &mock_object : NULL;
}
real_point3d *object_get_origin(long index, real_point3d *point) {
    point->x = index == 0 ? 1.f : index == 1 ? 8.f : 10.f;
    point->y = point->z = 0; return point;
}
long object_list_get_first(long list, long *reference) { (void)list; *reference = 0; return 0; }
long object_list_get_next(long list, long *reference) { (void)list; return ++*reference == 1 ? 1 : NONE; }
void *datum_try_and_get(struct data_array *data, long handle) {
    unsigned index = (unsigned)handle & 0xFFFFu;
    struct hs_syntax_node *node;
    if (data == &list_data) return handle == 0 ? arguments : NULL;
    if (handle == NONE || index >= (unsigned)data->count) return NULL;
    node = (struct hs_syntax_node *)data->data + index;
    return node->datum_header == (short)((unsigned)handle >> 16) ? node : NULL;
}
void *mcc_runtime_pointer(struct mcc_runtime *runtime, uint32_t address, uint32_t bytes) {
    uint32_t offset = address - runtime->report.tag_base;
    return address >= runtime->report.tag_base && offset <= runtime->used && bytes <= runtime->used - offset ?
        runtime->tags + offset : NULL;
}

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "check failed at line %d\n", __LINE__); return 1; } } while (0)
int main(int argc, char **argv) {
    struct mcc_runtime runtime = {0};
    struct scenario *scenario;
    struct data_array *syntax;
    struct hs_syntax_node *nodes;
    struct hs_function_definition *extension;
    char *strings;
    uint32_t pointer;
    int expected = 1;
    CHECK(argc == 2);
    runtime.used = 0x100000;
    runtime.tags = malloc(runtime.used);
    CHECK(runtime.tags);
    memset(runtime.tags, 0, runtime.used);
    runtime.tag_index = runtime.tags + 40;
    runtime.report.tag_base = (uint32_t)(uintptr_t)runtime.tags;
    scenario = (struct scenario *)(runtime.tags + 0x100);
    syntax = (struct data_array *)(runtime.tags + 0x1000);
    nodes = (struct hs_syntax_node *)(syntax + 1);
    strings = (char *)(runtime.tags + 0xF0000);
    pointer = (uint32_t)(uintptr_t)scenario;
    memcpy(runtime.tags + 60, &pointer, 4);
    scenario->hs_syntax_data.address = syntax;
    scenario->hs_syntax_data.size = sizeof(*syntax) + 32767 * sizeof(*nodes);
    scenario->hs_string_constants.address = strings;
    scenario->hs_string_constants.size = 128;
    syntax->size = sizeof(*nodes);
    syntax->maximum_count = 32767;
    syntax->count = 4;
    syntax->data = nodes;
    nodes[0].datum_header = nodes[1].datum_header = nodes[2].datum_header = nodes[3].datum_header = 1;
    nodes[0].flags = 8;
    nodes[0].data = 0x10001;
    nodes[0].function_index = 777;
    nodes[1].type = _hs_function_name;
    nodes[1].flags = 1;
    nodes[1].next_node_index = 0x10002;
    nodes[2].type = _hs_type_object_list;
    nodes[2].flags = 1;
    nodes[2].next_node_index = 0x10003;
    nodes[3].type = _hs_type_object;
    nodes[3].flags = 1;
    nodes[3].next_node_index = NONE;
    strcpy(strings, "sleep");
    if (!strcmp(argv[1], "unknown")) { strcpy(strings, "not_implemented"); expected = 0; }
    if (!strcmp(argv[1], "bad_child")) { nodes[0].data = 0x10008; expected = 0; }
    if (!strcmp(argv[1], "child_salt")) { nodes[0].data = 0x20001; expected = 0; }
    if (!strcmp(argv[1], "short_data")) { scenario->hs_syntax_data.size = sizeof(*syntax) + 4 * sizeof(*nodes); expected = 0; }
    if (!strcmp(argv[1], "capacity")) { syntax->count = 19002; expected = 0; }
    if (!strcmp(argv[1], "unterminated")) { memset(strings, 'x', 128); expected = 0; }
    if (!strcmp(argv[1], "alias")) strcpy(strings, "player_effect_set_max_vibrate");
    if (!strcmp(argv[1], "extension")) strcpy(strings, "objects_distance_to_object");
    if (!strcmp(argv[1], "global")) {
        nodes[0].flags = 5; nodes[0].data = 0x8001; strcpy(strings, "known_global");
    }
    CHECK(mcc_scripts_prepare(&runtime) == expected);
    if (!expected) { free(runtime.tags); return 0; }
    CHECK(syntax->maximum_count == 19001);
    CHECK(syntax->data == nodes);
    CHECK(!mcc_script_function(MCC_HS_DISTANCE_TO_OBJECT));
    CHECK(mcc_script_find("objects_distance_to_object") == NONE);
    loaded = TRUE;
    {
        struct hs_syntax_node ai = {0};
        CHECK(mcc_script_ai_cast(_hs_type_ai, _hs_type_unit));
        ai.flags = 9; ai.constant_type = _hs_type_ai; ai.type = _hs_type_unit;
        CHECK(mcc_script_parse_ai(&ai, "falcon/pilot") == 1 && ai.data == (long)0x80000018);
        CHECK(mcc_script_parse_ai(&ai, "missing/pilot") == 0);
        ai.flags |= 4;
        CHECK(mcc_script_parse_ai(&ai, "falcon/pilot") == -1);
        CHECK(mcc_script_cast_ai(_hs_type_unit, 24) == 0 && deleted_lists == 1);
        CHECK(mcc_script_cast_ai(_hs_type_vehicle, 24) == NONE && deleted_lists == 2);
        CHECK(mcc_script_cast_ai(_hs_type_object_list, 24) == 0 && deleted_lists == 2);
        CHECK(mcc_script_cast_ai(_hs_type_unit, NONE) == NONE);
        loaded = FALSE;
        CHECK(!mcc_script_ai_cast(_hs_type_ai, _hs_type_unit));
        loaded = TRUE;
    }
    CHECK(mcc_script_find("player_effect_set_max_vibrate") == 322);
    if (!strcmp(argv[1], "global")) CHECK((unsigned short)nodes[0].short_value == 0x8009);
    else if (!strcmp(argv[1], "alias")) CHECK(nodes[0].function_index == 322);
    else if (!strcmp(argv[1], "extension")) {
        union { long bits; float value; } result;
        CHECK(nodes[0].function_index == MCC_HS_DISTANCE_TO_OBJECT);
        CHECK(mcc_script_call_valid(&nodes[0], syntax));
        nodes[3].type = _hs_type_real;
        CHECK(!mcc_script_call_valid(&nodes[0], syntax));
        extension = mcc_script_function(MCC_HS_DISTANCE_TO_OBJECT);
        CHECK(extension && extension->parameter_types[1] == _hs_type_object);
        arguments[0] = 0; arguments[1] = 2;
        extension->evaluate(MCC_HS_DISTANCE_TO_OBJECT, 0, TRUE);
        result.bits = returned;
        CHECK(result.value == 2.f);
        arguments[0] = NONE;
        extension->evaluate(MCC_HS_DISTANCE_TO_OBJECT, 0, TRUE);
        result.bits = returned;
        CHECK(result.value == -1.f);
    } else CHECK(nodes[0].function_index == 321 && nodes[1].function_index == 321);
    free(runtime.tags);
    return 0;
}
