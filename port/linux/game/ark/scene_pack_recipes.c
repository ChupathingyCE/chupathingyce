/*
SCENE_PACK_RECIPES.C

(scene_pack_recipes.h)
*/

#include "scene_pack_recipes.h"

#include <string.h>

int scene_pack_recipes_is_look(
	char const *name)
{
	return !strncmp(name, "h3_", 3);
}

static struct scene_pack_recipe_entry *entry_of(
	struct scene_pack_recipe_entry *entries,
	int count,
	char const *name)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (entries[index].text && !strcmp(entries[index].name, name))
			return &entries[index];
	}
	return NULL;
}

/* a pool's free entry; of the looks' the one named longest ago */
static struct scene_pack_recipe_entry *entry_new(
	struct scene_pack_recipe_entry *entries,
	int count,
	int recycled)
{
	struct scene_pack_recipe_entry *oldest = &entries[0];
	int index;

	for (index = 0; index < count; index++)
	{
		if (!entries[index].text)
			return &entries[index];
		if (entries[index].named < oldest->named)
			oldest = &entries[index];
	}
	return recycled ? oldest : NULL;
}

int scene_pack_recipes_put(
	struct scene_pack_recipes *recipes,
	char const *name,
	char *text,
	unsigned long size,
	char **replaced)
{
	int look = scene_pack_recipes_is_look(name);
	struct scene_pack_recipe_entry *entries = look ? recipes->looks : recipes->made;
	int count = look ? MAXIMUM_LOOK_RECIPES : MAXIMUM_MADE_RECIPES;
	struct scene_pack_recipe_entry *entry = entry_of(entries, count, name);

	if (!entry)
		entry = entry_new(entries, count, look);
	if (!entry)
		return 0;
	*replaced = entry->text;
	strncpy(entry->name, name, sizeof(entry->name) - 1);
	entry->name[sizeof(entry->name) - 1] = 0;
	entry->text = text;
	entry->size = size;
	entry->named = ++recipes->clock;
	return 1;
}

char const *scene_pack_recipes_find(
	struct scene_pack_recipes const *recipes,
	char const *name,
	unsigned long *size)
{
	int look = scene_pack_recipes_is_look(name);
	struct scene_pack_recipe_entry const *entry = entry_of(
		(struct scene_pack_recipe_entry *)(look ? recipes->looks : recipes->made),
		look ? MAXIMUM_LOOK_RECIPES : MAXIMUM_MADE_RECIPES, name);

	if (!entry)
		return NULL;
	if (size)
		*size = entry->size;
	return entry->text;
}
