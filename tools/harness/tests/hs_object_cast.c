#include "harness.h"

typedef unsigned short word;
#include "config.inc"
enum { _error_silent = 0, _hs_type_object = 1, _hs_function_inspect = 10, _hs_function_object_to_unit };
struct object_datum { struct { short type; } object; long definition_index; };
struct hs_syntax_node { long data, next_node_index; };
struct hs_stack_frame { long expression_index; };
struct hs_thread_datum { struct hs_stack_frame *stack; };
static struct hs_syntax_node node;
static struct hs_stack_frame frame;
static struct hs_thread_datum thread = { &frame };
static struct data_array *object_header_data;
static long stack_slot, returned;
static int returns, errors;
static const char *hs_type_names[8];
static struct hs_thread_datum *hs_thread_get(long index) { return &thread; }
static struct hs_syntax_node *hs_syntax_get(long index) { return &node; }
static void *hs_stack_allocate(long thread_index, int size) { return &stack_slot; }
static void hs_evaluate(long thread_index, long expression_index, long *result) { }
static void hs_return(long thread_index, long value) { returned = value; returns++; }
static const char *tag_get_name(long index) { return "scenery\\rock"; }
static void error(int priority, const char *format, ...) { errors++; }
#define object_header_try_and_get(index) ((struct object_header_datum *)datum_try_and_get(object_header_data, (index)))
#include "under_test.inc"
/* the retail object_get: halts on an index that is unused or changed */
static struct object_datum *object_get(long index)
{
	struct object_header_datum *header = object_header_try_and_get(index);

	CHECK(header, "halt: object index 0x%lx is unused or changed", (unsigned long)index);
	return header->datum;
}

static struct object_datum objects[4];

static long spawn(short type)
{
	long index = datum_new(object_header_data);
	struct object_header_datum *header = datum_get(object_header_data, index);

	header->type = (byte)type;
	header->datum = &objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(index)];
	header->datum->object.type = type;
	return index;
}

static long cast_to_unit(long object_index)
{
	stack_slot = object_index;
	returns = 0;
	returned = 12345;
	hs_evaluate_object_cast_up(_hs_function_object_to_unit, 0, FALSE);
	CHECK(returns == 1, "returned %d times", returns);
	return returned;
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	object_header_data = game_state_data_new("object", 4, sizeof(struct object_header_datum));
	CASE("live-unit")
	{
		long biped = spawn(_object_type_biped);

		CHECK(cast_to_unit(biped) == biped, "a live biped did not cast to a unit");
		CHECK(!errors, "error logged");
	}
	CASE("wrong-type")
	{
		long rock = spawn(_object_type_scenery);

		CHECK(cast_to_unit(rock) == NONE, "scenery cast to a unit");
		CHECK(errors == 1, "no error logged");
	}
	CASE("none")
	{
		CHECK(cast_to_unit(NONE) == NONE, "none did not cast to none");
	}
	CASE("deleted")
	{
		/* a script variable that kept the object after it was deleted (#1829) */
		long biped = spawn(_object_type_biped);

		datum_delete(object_header_data, biped);
		CHECK(cast_to_unit(biped) == NONE, "a deleted object did not cast to none");
		CHECK(!errors, "error logged");
	}
	CASE("reused")
	{
		/* the slot taken by a new object since: the old index is still gone */
		long biped = spawn(_object_type_biped), other;

		datum_delete(object_header_data, biped);
		other = spawn(_object_type_biped);
		CHECK(DATUM_INDEX_TO_ABSOLUTE_INDEX(other) == DATUM_INDEX_TO_ABSOLUTE_INDEX(biped), "slot not reused");
		CHECK(cast_to_unit(biped) == NONE, "the old index cast to the new object");
		CHECK(cast_to_unit(other) == other, "the new object did not cast");
	}
	return 0;
}
