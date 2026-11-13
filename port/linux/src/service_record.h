/*
SERVICE_RECORD.H

A Halo profile's own service record, kept on this machine: its totals over
the multiplayer games it finished (games, wins, kills, deaths, assists,
time played, best spree), its medals (medal_rules.h) and its maps and
weapons, from which its favorites. The game's statistics recorder
(port/linux/game/game_stats.c) adds each game as it ends; DELTA's SERVICE
RECORD shows it (port/linux/game/guide_record.c), beside the site's when
the copy is linked to a Delta profile.

Kept as text, one value a line, in the profile's folder
(SERVICE_RECORD_FILE_NAME), so it reads the same on any machine and byte
order. Nothing here is sent anywhere.

The service record itself is ours (the site's since September, and
the Delta Guide's). Only the local totals and the top medals here follow the career
record of mOTA-lEV's OpenCE-Faber (firefight_stats.c: a per-profile records file in the
profile's folder, a game's local players' kills, deaths and assists added
up as it ends, the profile's most earned medals), reworked for our medals
and our site's rules (a win as the site counts one).

The platform layer's C, nothing of the game's: the unit tests
(port/linux/tests/medal_rules_test.c) build it as it is.
*/

#ifndef SERVICE_RECORD_H
#define SERVICE_RECORD_H

#include "medal_rules.h"

#define SERVICE_RECORD_FILE_NAME "service_record.txt"

enum
{
	/* the maps and weapons kept (past these, a new one is counted in the
	totals only) */
	SERVICE_RECORD_MAPS = 32,
	SERVICE_RECORD_WEAPONS = 32,
	SERVICE_RECORD_NAME_LENGTH = 48,
	/* the largest file read */
	SERVICE_RECORD_MAXIMUM_FILE = 16384,
	/* a total's largest value */
	SERVICE_RECORD_MAXIMUM_COUNT = 2000000000,
};

struct service_record_count
{
	char name[SERVICE_RECORD_NAME_LENGTH];
	long count;
};

struct service_record
{
	long games;
	long wins;
	long kills;
	long deaths;
	long assists;
	long betrayals;
	long suicides;
	long seconds;
	long best_spree;
	long medals[NUMBER_OF_MEDALS];
	int map_count;
	struct service_record_count maps[SERVICE_RECORD_MAPS];
	int weapon_count;
	struct service_record_count weapons[SERVICE_RECORD_WEAPONS];
};

/* a weapon's kills in one game: its damage's tag path
("weapons\sniper rifle\sniper bullet") */
struct service_record_weapon
{
	char const *path;
	int kills;
};

/* one finished game, as one player played it */
struct service_record_game
{
	/* the map's name or path ("bloodgulch", "levels\test\bloodgulch\...") */
	char const *map;
	int won;
	int kills;
	int deaths;
	int assists;
	int betrayals;
	int suicides;
	int seconds;
	int best_spree;
	int medals[NUMBER_OF_MEDALS];
	int weapon_count;
	struct service_record_weapon const *weapons;
};

void service_record_clear(struct service_record *record);
/* a game added to the record */
void service_record_add_game(struct service_record *record, struct service_record_game const *game);

/* the record as text (SERVICE_RECORD_FILE_NAME's), into text (size bytes);
its length, -1 when it does not fit */
int service_record_format(struct service_record const *record, char *text, int size);
/* a record read from text (unknown lines skipped, as a later build's);
nonzero when it was one */
int service_record_parse(struct service_record *record, char const *text);
/* read from and written to a file (written whole through a temporary file
beside it); nonzero on success. A missing file reads as an empty record */
int service_record_load(char const *path, struct service_record *record);
int service_record_save(char const *path, struct service_record const *record);

/* the medals earned in all */
long service_record_medal_total(struct service_record const *record);
/* the medals earned most, most first (ties: medal_rules.h's order), at most
maximum; their count */
int service_record_top_medals(struct service_record const *record, int *medals, long *counts, int maximum);
/* the map played most and the weapon with the most kills ("" for none) */
char const *service_record_favorite_map(struct service_record const *record);
char const *service_record_favorite_weapon(struct service_record const *record);

/* a map's name as the menus have it ("Blood Gulch"; else as it is) */
void service_record_map_title(char const *map, char *text, int size);
/* a weapon's family from its damage's tag path ("sniper rifle" from
"weapons\sniper rifle\sniper bullet", "warthog" from "vehicles\warthog\..."),
and as a title ("Sniper Rifle") */
void service_record_weapon_family(char const *path, char *text, int size);
void service_record_title(char const *name, char *text, int size);

#endif
