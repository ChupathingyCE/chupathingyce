/* mcc_script_call_valid (port/linux/game/mcc_scripts.c) against MCC function
 * definitions laid out as the game has them: the first parameter type in the
 * definition, the rest in its wrapper. Built for the host, so a 64-bit host
 * checks the padded layout the 32-bit harness can't. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define TRUE 1
#define FALSE 0
#define NONE (-1)
typedef unsigned char boolean;
typedef float real;
#define HS_UNION_SLOT(type, name) struct { type name; };
typedef void (*hs_evaluate_procedure)(short, long, boolean);
typedef boolean (*hs_function_parse_proc)(short, long);
struct data_array;
#include "types.inc"
enum { _hs_type_void = 4, _hs_type_boolean = 5, _hs_type_real = 6, _hs_type_script = 3,
    _hs_type_object_list = 14, _hs_type_cutscene_flag = 18, _hs_type_unit = 40,
    _hs_type_starting_profile = 26 };
#define MCC_HS_CAMPAIGN_FIRST 0x7010

static struct {
    struct hs_function_definition definition;
    short other_parameters[2];
} three = {
    { _hs_type_void, 0, "player_add_equipment", NULL, NULL, NULL, NULL, 3, { _hs_type_unit } },
    { _hs_type_starting_profile, _hs_type_boolean }
};

static struct hs_syntax_node nodes[8];
static struct hs_function_definition *mcc_script_function(short index)
{
    return index == 0x7003 ? &three.definition : NULL;
}
static void *datum_try_and_get(struct data_array *array, long handle)
{
    (void)array;
    return handle >= 0 && handle < 8 ? &nodes[handle] : NULL;
}
#include "under_test.inc"

static void chain(short const *types, int count)
{
    int i;
    memset(nodes, 0, sizeof(nodes));
    nodes[0].function_index = 0x7003; nodes[0].flags = 8; nodes[0].data = 1;
    nodes[1].next_node_index = count ? 2 : NONE;
    for (i = 0; i < count; ++i) {
        nodes[2 + i].type = types[i];
        nodes[2 + i].next_node_index = i + 1 < count ? 3 + i : NONE;
    }
}

int main(int argc, char **argv)
{
    static short const right[3] = { _hs_type_unit, _hs_type_starting_profile, _hs_type_boolean };
    static short const wrong[3] = { _hs_type_unit, _hs_type_boolean, _hs_type_boolean };
    char const *mode = argc > 1 ? argv[1] : "";
    if (!strcmp(mode, "right")) { chain(right, 3); CHECK(mcc_script_call_valid(&nodes[0], NULL)); }
    else if (!strcmp(mode, "wrong")) { chain(wrong, 3); CHECK(!mcc_script_call_valid(&nodes[0], NULL)); }
    else if (!strcmp(mode, "short")) { chain(right, 2); CHECK(!mcc_script_call_valid(&nodes[0], NULL)); }
    else return 2;
    return 0;
}
