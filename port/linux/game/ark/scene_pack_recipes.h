/*
SCENE_PACK_RECIPES.H

The recipes the game makes rather than reads from app0:packs (player_models.c's,
forge_blocks.c's, player_looks.c's), as data, with no game in it, so that it is
tested on its own (port/vita/tests/scene_pack_recipes_test.c). scene_packs.c
locks it.

Two pools: what a machine's own files make, at most MAXIMUM_MADE_RECIPES and kept
for the process, and looks' (a look's recipe, named h3_...), which other machines'
looks and the customization preview name without end: recycled, the one least
recently named first, so they never run out and never starve the first pool. A
recipe recycled is only text: a pack wanted from it has its own parsed copy, and
looks are named again before they are wanted.
*/

#ifndef __SCENE_PACK_RECIPES_H
#define __SCENE_PACK_RECIPES_H

enum
{
	SCENE_PACK_RECIPES_NAME_SIZE = 32,
	MAXIMUM_MADE_RECIPES = 32,
	MAXIMUM_LOOK_RECIPES = 16,
};

struct scene_pack_recipe_entry
{
	char name[SCENE_PACK_RECIPES_NAME_SIZE];
	char *text;
	unsigned long size;
	/* (a look's) when it was named last */
	unsigned long named;
};

struct scene_pack_recipes
{
	struct scene_pack_recipe_entry made[MAXIMUM_MADE_RECIPES];
	struct scene_pack_recipe_entry looks[MAXIMUM_LOOK_RECIPES];
	unsigned long clock;
};

/* whether name is a look's: its pool's */
int scene_pack_recipes_is_look(
	char const *name);

/* text (the caller's allocation, size bytes, taken) kept as name's, *replaced what
it takes the place of (NULL none: the caller's to free, by the same allocator): 0
if the pool is full, text not taken. name must fit */
int scene_pack_recipes_put(
	struct scene_pack_recipes *recipes,
	char const *name,
	char *text,
	unsigned long size,
	char **replaced);

/* name's text and size, NULL if it has none */
char const *scene_pack_recipes_find(
	struct scene_pack_recipes const *recipes,
	char const *name,
	unsigned long *size);

#endif
