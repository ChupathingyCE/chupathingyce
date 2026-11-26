/*
TAG_SCHEMA_GROUPS.C

The tag groups' schemas (tag_schema.h), from each tag_schema_*.c.
*/

/* ---------- headers */

#include "cseries.h"
#include "cache/physical_memory_map.h"
#include "tag_schema.h"

#include <string.h>

/* ---------- globals */

struct tag_schema_group const *const tag_schema_group_lists[] =
{
	tag_schema_object_groups,
	tag_schema_model_groups,
	tag_schema_collision_groups,
	tag_schema_render_groups,
	tag_schema_effect_groups,
	tag_schema_scenario_groups,
	NULL
};

/* (cache/physical_memory_map.h's tag cache) */
struct tag_schema_layout const tag_schema_xbox_layout =
{
	"Xbox",
	tag_schema_group_lists,
	NULL,
	TAG_CACHE_SIZE,
	0
};

/* Stubbs the Zombie's maps (port/linux/game/stubbs_mode.c), once Stubbs
mode has rewritten their tags in the Xbox layout: the Xbox layout, but for
the maxima Stubbs's tools went past where the game has no array of that
size: a model's shaders (his characters have up to 35; the 32 is Halo's
tools', and the game reads them by the block's count) */
enum
{
	STUBBS_MAXIMUM_SHADERS_PER_MODEL = 64,
};

static long tag_schema_stubbs_field_maximum(
	struct tag_schema_field const *field,
	long maximum)
{
	struct tag_schema_definition const *element = field->definition;

	if (field->type == _tag_schema_block && field->name && !strcmp(field->name, "shaders") &&
		element && element->name && !strcmp(element->name, "model_shader_reference"))
	{
		return STUBBS_MAXIMUM_SHADERS_PER_MODEL;
	}
	return maximum;
}

struct tag_schema_layout const tag_schema_stubbs_layout =
{
	"Stubbs",
	tag_schema_group_lists,
	NULL,
	TAG_CACHE_SIZE,
	0,
	tag_schema_stubbs_field_maximum
};
