/*
CAMPAIGN_LEVELS.H

The campaign's ten levels by their maps' file names ("a10"), in the menus'
order: a level's place is its frame in the menus' level pictures
(ui\shell\bitmaps\sp_levels, as the level select menus draw them) and its
name's in their strings. For the co-op games Online Games lists
(browser_screen.c) and the site's pictures of them (map_art.c). Plain C, no
game dependency: port/linux/tests/campaign_levels_test.c tests it.
*/

#ifndef __CAMPAIGN_LEVELS_H
#define __CAMPAIGN_LEVELS_H

enum
{
	CAMPAIGN_LEVEL_COUNT = 10,
};

static char const *const campaign_level_files[CAMPAIGN_LEVEL_COUNT] =
{
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
};

/* a campaign level's place (0 for a10 to 9 for d40) from its map's file
name, in any case, or -1 for a map that is not one */
static int campaign_level_index(char const *file)
{
	int index;

	if (!file)
		return -1;
	for (index = 0; index < CAMPAIGN_LEVEL_COUNT; index++)
	{
		char const *a = campaign_level_files[index], *b = file;

		while (*a && *b && (*a | 0x20) == (*b | 0x20))
			a++, b++;
		if (!*a && !*b)
			return index;
	}
	return -1;
}

#endif
