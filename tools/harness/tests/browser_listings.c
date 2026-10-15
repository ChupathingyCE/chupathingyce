/*
BROWSER_LISTINGS.C (test)

The real merge of the game list's listings (port/linux/src/browser.c): a
host that lists one game once for each network version is one game in the
list, at its newest version, and its players count once.
test_browser_listings.py takes the header (browser.h) and the code
(under_test.inc).
*/

#include "harness.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "browser.h"
#include "under_test.inc"

/* (an int the case needs, and what it was) */
#define CHECK_INT(value, wanted) CHECK((value) == (wanted), "%d, not %d", (int)(value), (int)(wanted))

static struct browser_game games[8];

/* a listing: the name (ASCII), the map, players, and the version */
static void listing(struct browser_game *game, char const *name, char const *map, short players, unsigned short version)
{
	int index;

	memset(game, 0, sizeof(*game));
	for (index = 0; name[index] && index < BROWSER_NAME_LENGTH; index++)
		game->name[index] = (unsigned short)name[index];
	snprintf(game->map, sizeof(game->map), "%s", map);
	snprintf(game->invite, sizeof(game->invite), "%02u%062u", version, 0u);
	game->engine = 2;
	game->players = players;
	game->maximum_players = 128;
	game->open = 1;
	game->version = version;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	int count;

	/* one game listed for versions 27, 29 and 28: one listing, 29's */
	if (!strcmp(case_name, "one-game-many-versions"))
	{
		listing(&games[0], ". SWAT pistols", "ratrace", 31, 27);
		listing(&games[1], ". SWAT pistols", "ratrace", 31, 29);
		listing(&games[2], ". SWAT pistols", "ratrace", 31, 28);
		listing(&games[3], "FractumSlayer", "Sneak@ce", 4, 29);
		count = merge_listings(games, 4);
		CHECK_INT(count, 2);
		CHECK_INT(games[0].version, 29);
		CHECK(!strncmp(games[0].invite, "29", 2), "the invite kept is %.2s...", games[0].invite);
		CHECK_INT(games[1].version, 29);
		CHECK_INT(games[0].players + games[1].players, 35);
		return 0;
	}
	/* games that differ in anything but the version stay apart */
	if (!strcmp(case_name, "different-games-kept"))
	{
		listing(&games[0], "Server", "ratrace", 31, 28);
		listing(&games[1], "Server", "ratrace", 30, 29);
		listing(&games[2], "Server", "bloodgulch", 31, 27);
		listing(&games[3], "Server 2", "ratrace", 31, 26);
		listing(&games[4], "Server", "ratrace", 31, 25);
		games[4].maximum_players = 16;
		listing(&games[5], "Server", "ratrace", 31, 24);
		games[5].roster_count = 1;
		games[5].roster[0].name[0] = 'A';
		listing(&games[6], "Server", "ratrace", 31, 23);
		games[6].roster_count = 1;
		games[6].roster[0].name[0] = 'B';
		count = merge_listings(games, 7);
		CHECK_INT(count, 7);
		return 0;
	}
	/* two games of one version are two, however alike */
	if (!strcmp(case_name, "same-version-kept"))
	{
		listing(&games[0], "Halo", "bloodgulch", 0, 29);
		listing(&games[1], "Halo", "bloodgulch", 0, 29);
		count = merge_listings(games, 2);
		CHECK_INT(count, 2);
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
