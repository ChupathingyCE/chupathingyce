/*
FORGE_FILES_TEST.C

Tests of Forge's layout files (port/linux/game/forge_files.c) and their
folder (port/linux/src/forge_store.c): the writer and the lowest version
that holds a layout, a round trip of every record kind, the validator
refusing each kind of damaged or hostile file with its own result, the
map's own objects a layout takes away (version 11), a
deterministic fuzz of the validator (truncations and flipped bytes, half
of them with the checksum made right again so the checks past it are
reached), and the store's saves, lists, loads and names. Built and run by
tools/test_forge_files.py with a scratch folder as its argument (with
AddressSanitizer and UndefinedBehaviorSanitizer when the compiler has
them); exits nonzero on a failure. Everything it writes is under that
folder.

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../game/forge_files.h"
#include "../src/port_folders.h"
#include "../src/forge_store.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;
static char scratch[512];

#define CHECK(condition) check((condition) != 0, #condition, __LINE__)

static void check(int passed, char const *what, int line)
{
	checks++;
	if (!passed)
	{
		failures++;
		printf("FAIL (line %d): %s\n", line, what);
	}
}

/* (forge_store.c's, the game's own in the build) */
const char *platform_folder(enum port_folder folder)
{
	static char path[600];

	snprintf(path, sizeof(path), "%s/%s", scratch, port_folder_name(folder));
	return path;
}

void platform_log(const char *format, ...)
{
	(void)format;
}

#define GROUP(a, b, c, d) (((unsigned int)(a) << 24) | ((unsigned int)(b) << 16) | ((unsigned int)(c) << 8) | (unsigned int)(d))

/* (the header's fields' offsets) */
#define AT_VERSION 4
#define AT_HEADER_SIZE 6
#define AT_FLAGS 8
#define AT_MAP 12
#define AT_PACKS 48
#define AT_PATHS 50
#define AT_RECORDS 52
#define AT_META 54
#define AT_PAYLOAD 56
#define AT_CHECKSUM 60
/* (a record's) */
#define RECORD_PATH 0
#define RECORD_KIND 2
#define RECORD_TEAM 3
#define RECORD_POSITION 4
#define RECORD_RESPAWN 28
#define RECORD_COUNT 30
#define RECORD_FLAGS 34
#define RECORD_AI_TEAM 35
#define RECORD_RESERVED 36

static unsigned char good[FORGE_FILE_MAXIMUM_SIZE + 16];
static int good_size;
static unsigned char work[FORGE_FILE_MAXIMUM_SIZE + 16];
static struct forge_file_layout layout_a;
static struct forge_file_layout layout_b;
static struct forge_file_layout layout_c;

static unsigned int random_state = 0x2545F491u;

static unsigned int next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static void put16(unsigned char *bytes, int at, unsigned int value)
{
	bytes[at] = (unsigned char)value;
	bytes[at + 1] = (unsigned char)(value >> 8);
}

static void put32(unsigned char *bytes, int at, unsigned int value)
{
	put16(bytes, at, value & 0xFFFF);
	put16(bytes, at + 2, value >> 16);
}

/* the payload's size and checksum made right again */
static void reseal(unsigned char *bytes, int size)
{
	put32(bytes, AT_PAYLOAD, (unsigned int)(size - FORGE_FILE_HEADER_SIZE));
	put32(bytes, AT_CHECKSUM, forge_file_crc32(0, bytes + FORGE_FILE_HEADER_SIZE, size - FORGE_FILE_HEADER_SIZE));
}

static void add_path(struct forge_file_layout *layout, unsigned int group, char const *path)
{
	struct forge_file_path *entry = &layout->paths[layout->path_count++];

	entry->group_tag = group;
	snprintf(entry->path, sizeof(entry->path), "%s", path);
}

static struct forge_file_record *add_record(struct forge_file_layout *layout, int path, float x, float y)
{
	struct forge_file_record *record = &layout->records[layout->record_count++];

	memset(record, 0, sizeof(*record));
	record->path_index = (unsigned short)path;
	record->kind = _forge_record_object;
	record->team = -1;
	record->position[0] = x;
	record->position[1] = y;
	record->position[2] = 1.f;
	record->angles[0] = 0.5f;
	record->respawn_seconds = 30;
	record->count = 1;
	record->flags = FORGE_RECORD_PLACE_AT_START;
	return record;
}

/* a marker's record: no path, its yaw alone */
static struct forge_file_record *add_marker(struct forge_file_layout *layout, int kind, int team, int variant)
{
	struct forge_file_record *record = &layout->records[layout->record_count++];

	memset(record, 0, sizeof(*record));
	record->path_index = FORGE_FILE_NO_PATH;
	record->kind = (unsigned char)kind;
	record->team = (signed char)team;
	record->variant = (unsigned char)variant;
	record->position[0] = 90.f + (float)layout->record_count;
	record->position[1] = -150.f;
	record->position[2] = 0.5f;
	record->angles[0] = -1.f;
	layout->flags |= FORGE_FILE_FLAG_NETGAME_SPAWNS;
	return record;
}

/* (version 14) a volume: a box of half width w, half length l and height h
(quarter world units), or a sphere of radius w (l and h 0) */
static struct forge_file_record *add_volume(struct forge_file_layout *layout, int type, int shape, int w, int l, int h,
	int amount)
{
	struct forge_file_record *record = add_marker(layout, _forge_record_volume, -1, type);

	record->params[FORGE_FILE_VOLUME_PARAM_SHAPE] = (unsigned char)shape;
	record->params[FORGE_FILE_VOLUME_PARAM_SIZE] = (unsigned char)w;
	record->params[FORGE_FILE_VOLUME_PARAM_SIZE + 1] = (unsigned char)l;
	record->params[FORGE_FILE_VOLUME_PARAM_SIZE + 2] = (unsigned char)h;
	record->params[FORGE_FILE_VOLUME_PARAM_AMOUNT] = (unsigned char)amount;
	return record;
}

/* a placement of the map's taken away (or kept, of a category taken away
whole) */
static void add_canvas(struct forge_file_layout *layout, int category, int kept, int index)
{
	struct forge_file_canvas_entry *entry = &layout->canvas.entries[layout->canvas.entry_count++];

	layout->flags |= FORGE_FILE_FLAG_CANVAS;
	entry->category = (unsigned char)category;
	entry->kept = (unsigned char)kept;
	entry->index = (unsigned short)index;
}

/* (version 15) the sky and fog: a sky's path (path_index, or NONE for the
map's) and a fog (density 0 for none) */
static struct forge_file_record *add_environment(struct forge_file_layout *layout, int path_index, int red, int density,
	int start, int opaque)
{
	struct forge_file_record *record = &layout->records[layout->record_count++];

	memset(record, 0, sizeof(*record));
	record->kind = _forge_record_environment;
	record->path_index = (unsigned short)(path_index >= 0 ? path_index : FORGE_FILE_NO_PATH);
	if (path_index >= 0)
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_SETS] |= FORGE_FILE_ENVIRONMENT_SKY;
	if (density)
	{
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_SETS] |= FORGE_FILE_ENVIRONMENT_FOG;
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_COLOR] = (unsigned char)red;
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_COLOR + 1] = 90;
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_COLOR + 2] = 200;
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_DENSITY] = (unsigned char)density;
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_START] = (unsigned char)start;
		record->params[FORGE_FILE_ENVIRONMENT_PARAM_OPAQUE] = (unsigned char)opaque;
	}
	layout->flags |= FORGE_FILE_FLAG_NETGAME_SPAWNS;
	return record;
}

/* (version 16) an effect (repeat, seconds) or a decal (size, tenths) of
path */
static struct forge_file_record *add_effect(struct forge_file_layout *layout, int kind, int path, int value)
{
	struct forge_file_record *record = add_marker(layout, kind, 0, 0);

	record->path_index = (unsigned short)path;
	record->params[0] = (unsigned char)value;
	return record;
}

/* Blood Gulch: a Warthog, two boulders, a rocket launcher, an overshield
(version 1) */
static void sample_layout(struct forge_file_layout *layout)
{
	memset(layout, 0, sizeof(*layout));
	snprintf(layout->map_name, sizeof(layout->map_name), "bloodgulch");
	layout->map_hash = 0x1234ABCD;
	add_path(layout, GROUP('v', 'e', 'h', 'i'), "vehicles\\warthog\\mp_warthog");
	add_path(layout, GROUP('s', 'c', 'e', 'n'), "scenery\\rocks\\boulder\\boulder");
	add_path(layout, GROUP('w', 'e', 'a', 'p'), "weapons\\rocket launcher\\rocket launcher");
	add_path(layout, GROUP('e', 'q', 'i', 'p'), "powerups\\over shield");
	add_record(layout, 0, 98.f, -158.f);
	add_record(layout, 1, 100.f, -156.f);
	add_record(layout, 1, 102.f, -156.f)->angles[0] = 3.14159f;
	add_record(layout, 2, 99.f, -161.f);
	add_record(layout, 3, 101.f, -163.f);
	snprintf(layout->title, sizeof(layout->title), "Gulch Test");
	snprintf(layout->author, sizeof(layout->author), "Tester");
	snprintf(layout->description, sizeof(layout->description), "A Warthog, boulders and a rocket launcher.");
	snprintf(layout->date, sizeof(layout->date), "2026-01-02 12:00");
}

/* every record kind and setting versions 1 to 7 and 11 to 16 hold */
static void full_layout(struct forge_file_layout *layout)
{
	struct forge_file_record *record;

	sample_layout(layout);
	add_path(layout, GROUP('a', 'c', 't', 'v'), "characters\\elite\\elite major");
	add_path(layout, GROUP('m', 'a', 'c', 'h'), "levels\\test\\door");
	layout->packs[0].recipe[0] = 1;
	layout->packs[0].recipe[1] = 2;
	snprintf(layout->packs[0].name, sizeof(layout->packs[0].name), "covenant_vehicles");
	layout->pack_count = 1;
	/* (a character: its AI team, its brain off, its respawn) */
	record = add_record(layout, 4, 95.f, -150.f);
	record->ai_team = FORGE_AI_TEAM_FLOOD;
	record->flags |= FORGE_RECORD_AI_OFF | FORGE_RECORD_AI_RESPAWN;
	record->respawn_seconds = 60;
	/* (physics) */
	add_record(layout, 1, 104.f, -156.f)->flags |= FORGE_RECORD_PHYSICS_FIXED;
	add_record(layout, 5, 106.f, -156.f)->flags |= FORGE_RECORD_PHYSICS_PHASED;
	/* (player spawns: an initial one of red for slayer, a respawn of any) */
	record = add_marker(layout, _forge_record_player_spawn, 0, 0);
	record->flags = FORGE_RECORD_SPAWN_INITIAL;
	record->game_types = FORGE_FILE_GAME_TYPE_SLAYER | FORGE_FILE_GAME_TYPE_INFECTION;
	add_marker(layout, _forge_record_player_spawn, -1, 0);
	/* (a teleporter's ends) */
	add_marker(layout, _forge_record_teleporter_in, 3, 0);
	add_marker(layout, _forge_record_teleporter_out, 3, 0);
	/* (game type objects) */
	add_marker(layout, _forge_record_flag, 0, FORGE_FILE_FLAG_TYPE_CTF);
	add_marker(layout, _forge_record_flag, 1, FORGE_FILE_FLAG_TYPE_CTF);
	add_marker(layout, _forge_record_flag, 2, FORGE_FILE_FLAG_TYPE_HILL);
	add_marker(layout, _forge_record_flag, 0, FORGE_FILE_FLAG_TYPE_ODDBALL);
	add_marker(layout, _forge_record_flag, 31, FORGE_FILE_FLAG_TYPE_RACE);
	/* (the map's own objects: two Warthogs, a boulder, an item spawn) */
	add_canvas(layout, _forge_canvas_scenery, 0, 7);
	add_canvas(layout, _forge_canvas_vehicles, 0, 0);
	add_canvas(layout, _forge_canvas_vehicles, 0, 1);
	add_canvas(layout, _forge_canvas_items, 0, FORGE_FILE_CANVAS_MAXIMUM_INDEX);
	/* (volumes: a kill box, a damage sphere for red players, a gravity box) */
	add_volume(layout, FORGE_FILE_VOLUME_KILL, FORGE_FILE_VOLUME_BOX, 40, 40, 8, 0);
	record = add_volume(layout, FORGE_FILE_VOLUME_DAMAGE, FORGE_FILE_VOLUME_SPHERE, 20, 0, 0, 25);
	record->team = 0;
	record->params[FORGE_FILE_VOLUME_PARAM_TARGET] = FORGE_FILE_VOLUME_PLAYERS;
	add_volume(layout, FORGE_FILE_VOLUME_GRAVITY, FORGE_FILE_VOLUME_BOX, 255, 1, 255, 0);
	/* (the sky and fog: a sky of the map's, a fog) */
	add_path(layout, FORGE_FILE_SKY_GROUP, "sky\\sky_bloodgulch\\sky_bloodgulch");
	add_environment(layout, layout->path_count - 1, 200, 60, 10, 100);
	/* (an effect, looping; one again every 5 s; a decal) */
	add_path(layout, FORGE_FILE_EFFECT_GROUP, "effects\\burning\\fire");
	add_path(layout, FORGE_FILE_DECAL_GROUP, "decals\\scorch\\scorch");
	add_effect(layout, _forge_record_effect, layout->path_count - 2, 0);
	add_effect(layout, _forge_record_effect, layout->path_count - 2, 5);
	add_effect(layout, _forge_record_decal, layout->path_count - 1, 10);
}

static int encode(struct forge_file_layout const *layout, unsigned char *bytes)
{
	int result = -1;
	int size = forge_file_encode(layout, bytes, FORGE_FILE_MAXIMUM_SIZE, &result);

	if (size < 0)
		printf("  (encode refused: %s)\n", forge_file_result_name(result));
	return size;
}

static int decode(unsigned char const *bytes, int size, char const *map, struct forge_file_layout *layout)
{
	char why[160];

	return forge_file_decode(bytes, size, map, layout, why, sizeof(why));
}

static int layouts_equal(struct forge_file_layout const *a, struct forge_file_layout const *b)
{
	int index;

	if (forge_file_layout_version(a) != forge_file_layout_version(b) || a->flags != b->flags ||
		strcmp(a->map_name, b->map_name) || a->map_hash != b->map_hash || a->pack_count != b->pack_count ||
		a->path_count != b->path_count || a->record_count != b->record_count || strcmp(a->title, b->title) ||
		strcmp(a->author, b->author) || strcmp(a->description, b->description) || strcmp(a->date, b->date))
	{
		return 0;
	}
	for (index = 0; index < a->pack_count; index++)
	{
		if (memcmp(a->packs[index].recipe, b->packs[index].recipe, sizeof(a->packs[index].recipe)) ||
			strcmp(a->packs[index].name, b->packs[index].name))
		{
			return 0;
		}
	}
	for (index = 0; index < a->path_count; index++)
	{
		if (a->paths[index].group_tag != b->paths[index].group_tag || strcmp(a->paths[index].path, b->paths[index].path))
			return 0;
	}
	for (index = 0; index < a->record_count; index++)
	{
		struct forge_file_record const *x = &a->records[index];
		struct forge_file_record const *y = &b->records[index];

		if (x->path_index != y->path_index || x->kind != y->kind || x->team != y->team ||
			memcmp(x->position, y->position, sizeof(x->position)) || memcmp(x->angles, y->angles, sizeof(x->angles)) ||
			x->respawn_seconds != y->respawn_seconds || x->count != y->count || x->game_types != y->game_types ||
			x->variant != y->variant || x->flags != y->flags || x->ai_team != y->ai_team ||
			x->ai_behavior != y->ai_behavior || x->ai_route != y->ai_route || x->ai_options != y->ai_options ||
			memcmp(x->params, y->params, sizeof(x->params)))
		{
			return 0;
		}
	}
	if (a->canvas.categories != b->canvas.categories || a->canvas.entry_count != b->canvas.entry_count)
		return 0;
	for (index = 0; index < a->canvas.entry_count; index++)
	{
		if (a->canvas.entries[index].category != b->canvas.entries[index].category ||
			a->canvas.entries[index].kept != b->canvas.entries[index].kept ||
			a->canvas.entries[index].index != b->canvas.entries[index].index)
		{
			return 0;
		}
	}
	return 1;
}

/* where record index starts in a file of layout */
static int record_at(struct forge_file_layout const *layout, int index)
{
	int at = FORGE_FILE_HEADER_SIZE + layout->pack_count * FORGE_FILE_PACK_SIZE;
	int path;

	for (path = 0; path < layout->path_count; path++)
		at += 5 + (int)strlen(layout->paths[path].path);
	return at + index * FORGE_FILE_RECORD_SIZE;
}

/* the good file (the sample's) copied, changed by a caller, resealed or
not, and decoded: its result */
static int damaged(int size, int sealed)
{
	if (sealed)
		reseal(work, size);
	return decode(work, size, "bloodgulch", &layout_c);
}

static void fresh(void)
{
	memcpy(work, good, (size_t)good_size);
}

/* ---------- the writer and the versions */

static void test_versions(void)
{
	struct forge_file_record *record;
	int size;

	sample_layout(&layout_a);
	CHECK(forge_file_layout_version(&layout_a) == 1);
	layout_a.records[0].path_index = 0;
	sample_layout(&layout_a);
	add_path(&layout_a, GROUP('a', 'c', 't', 'v'), "characters\\marine\\marine");
	record = add_record(&layout_a, 4, 90.f, -150.f);
	record->ai_team = FORGE_AI_TEAM_PLAYER;
	CHECK(forge_file_layout_version(&layout_a) == 2);
	record->flags |= FORGE_RECORD_AI_RESPAWN;
	CHECK(forge_file_layout_version(&layout_a) == 3);
	add_marker(&layout_a, _forge_record_player_spawn, 1, 0);
	CHECK(forge_file_layout_version(&layout_a) == 4);
	add_marker(&layout_a, _forge_record_teleporter_in, 0, 0);
	add_marker(&layout_a, _forge_record_teleporter_out, 0, 0);
	CHECK(forge_file_layout_version(&layout_a) == 5);
	add_marker(&layout_a, _forge_record_flag, 0, FORGE_FILE_FLAG_TYPE_CTF);
	CHECK(forge_file_layout_version(&layout_a) == 6);
	add_record(&layout_a, 1, 80.f, -150.f)->flags |= FORGE_RECORD_PHYSICS_FIXED;
	CHECK(forge_file_layout_version(&layout_a) == 7);
	size = encode(&layout_a, work);
	CHECK(size > 0 && work[AT_VERSION] == 7);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok && layout_b.version == 7);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 3);
	CHECK(forge_file_layout_version(&layout_a) == 11);
	size = encode(&layout_a, work);
	CHECK(size > 0 && work[AT_VERSION] == 11);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok && layout_b.version == 11);
	/* (the map's own objects alone: version 11 too) */
	sample_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_weapons, 0, 2);
	CHECK(forge_file_layout_version(&layout_a) == 11);

	/* (more than 256 records: version 13, read back; 256 stay version 1) */
	sample_layout(&layout_a);
	while (layout_a.record_count < FORGE_FILE_EARLIER_MAXIMUM_RECORDS)
		add_record(&layout_a, 1, 50.f + (float)(layout_a.record_count % 40), -120.f - (float)(layout_a.record_count / 40));
	CHECK(forge_file_layout_version(&layout_a) == 1);
	add_record(&layout_a, 1, 40.f, -100.f);
	CHECK(forge_file_layout_version(&layout_a) == FORGE_FILE_MORE_RECORDS_VERSION);
	size = encode(&layout_a, work);
	CHECK(size > 0 && work[AT_VERSION] == FORGE_FILE_MORE_RECORDS_VERSION);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok &&
		layout_b.version == FORGE_FILE_MORE_RECORDS_VERSION && layout_b.record_count == FORGE_FILE_EARLIER_MAXIMUM_RECORDS + 1);
	/* (the same records under an earlier version: too many for it, as a
	build from before version 13 counts them) */
	put16(work, AT_VERSION, 7);
	put32(work, AT_CHECKSUM, forge_file_crc32(0, work + FORGE_FILE_HEADER_SIZE, (size_t)(size - FORGE_FILE_HEADER_SIZE)));
	CHECK(decode(work, size, "bloodgulch", &layout_c) == _forge_file_too_many);
	layout_a.version = 7;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_too_many);
	/* (the most version 13 holds, and one more) */
	layout_a.version = 0;
	while (layout_a.record_count < FORGE_FILE_MAXIMUM_RECORDS)
		add_record(&layout_a, 1, 50.f + (float)(layout_a.record_count % 40), -120.f - (float)(layout_a.record_count / 40));
	size = encode(&layout_a, work);
	CHECK(size > 0 && decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok &&
		layout_b.record_count == FORGE_FILE_MAXIMUM_RECORDS);
	put16(work, AT_RECORDS, FORGE_FILE_MAXIMUM_RECORDS + 1);
	CHECK(decode(work, size, "bloodgulch", &layout_c) == _forge_file_too_many);
	/* (version 13 holds the map's own objects block too) */
	sample_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_scenery, 0, 3);
	while (layout_a.record_count <= FORGE_FILE_EARLIER_MAXIMUM_RECORDS)
		add_record(&layout_a, 1, 50.f + (float)(layout_a.record_count % 40), -120.f - (float)(layout_a.record_count / 40));
	CHECK(forge_file_layout_version(&layout_a) == FORGE_FILE_MORE_RECORDS_VERSION);
	size = encode(&layout_a, work);
	CHECK(size > 0 && decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok &&
		layout_b.version == FORGE_FILE_MORE_RECORDS_VERSION && layouts_equal(&layout_a, &layout_b));
	/* (version 13 with 256 records or fewer is not one a writer made) */
	sample_layout(&layout_a);
	layout_a.version = FORGE_FILE_MORE_RECORDS_VERSION;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);
	layout_a.version = 9;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);

	/* (a volume: version 14, the newest; a volume alone, on a layout of
	plain objects, 14 still) */
	sample_layout(&layout_a);
	add_volume(&layout_a, FORGE_FILE_VOLUME_KILL, FORGE_FILE_VOLUME_BOX, 4, 4, 4, 0);
	CHECK(forge_file_layout_version(&layout_a) == FORGE_FILE_VOLUMES_VERSION);
	size = encode(&layout_a, work);
	CHECK(size > 0 && work[AT_VERSION] == FORGE_FILE_VOLUMES_VERSION);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok && layout_b.version == FORGE_FILE_VOLUMES_VERSION);
	/* (a volume alone) */
	sample_layout(&layout_a);
	add_volume(&layout_a, FORGE_FILE_VOLUME_GRAVITY, FORGE_FILE_VOLUME_SPHERE, 12, 0, 0, 50);
	CHECK(forge_file_layout_version(&layout_a) == FORGE_FILE_VOLUMES_VERSION);
	/* (a sky and fog: version 15, the newest, read back) */
	add_environment(&layout_a, -1, 10, 40, 0, 50);
	CHECK(forge_file_layout_version(&layout_a) == FORGE_FILE_ENVIRONMENT_VERSION);
	size = encode(&layout_a, work);
	CHECK(size > 0 && work[AT_VERSION] == FORGE_FILE_ENVIRONMENT_VERSION);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok && layout_b.version == FORGE_FILE_ENVIRONMENT_VERSION);
	/* (a decal: version 16, the newest, read back) */
	add_path(&layout_a, FORGE_FILE_DECAL_GROUP, "decals\\scorch\\scorch");
	add_effect(&layout_a, _forge_record_decal, layout_a.path_count - 1, 10);
	CHECK(forge_file_layout_version(&layout_a) == FORGE_FILE_EFFECTS_VERSION);
	size = encode(&layout_a, work);
	CHECK(size > 0 && work[AT_VERSION] == FORGE_FILE_EFFECTS_VERSION);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok && layout_b.version == FORGE_FILE_EFFECTS_VERSION);

	/* (a version that holds nothing of its own is not one a writer made) */
	sample_layout(&layout_a);
	layout_a.version = 4;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);
	layout_a.version = 8;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);
	layout_a.version = 11;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);
	layout_a.version = 12;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);
	layout_a.version = 13;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);
	layout_a.version = FORGE_FILE_VOLUMES_VERSION;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);
	layout_a.version = FORGE_FILE_ENVIRONMENT_VERSION;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);
	layout_a.version = FORGE_FILE_EFFECTS_VERSION;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);
	layout_a.version = 0;
	/* (the writer refuses what the reader would) */
	layout_a.title[0] = 0;
	CHECK(forge_file_encode(&layout_a, work, FORGE_FILE_MAXIMUM_SIZE, &size) < 0 && size == _forge_file_bad_meta);
	sample_layout(&layout_a);
	CHECK(forge_file_encode(&layout_a, work, 100, &size) < 0 && size == _forge_file_no_room);
	/* (a character's AI on a non-character, physics on a character) */
	sample_layout(&layout_a);
	layout_a.records[0].ai_team = FORGE_AI_TEAM_FLOOD;
	CHECK(forge_file_encode(&layout_a, work, FORGE_FILE_MAXIMUM_SIZE, &size) < 0 && size == _forge_file_bad_record);
	sample_layout(&layout_a);
	layout_a.records[0].flags |= FORGE_RECORD_PHYSICS_MASK;
	CHECK(forge_file_encode(&layout_a, work, FORGE_FILE_MAXIMUM_SIZE, &size) < 0 && size == _forge_file_bad_record);
}

static void test_round_trip(void)
{
	int size;
	int again;
	static unsigned char second[FORGE_FILE_MAXIMUM_SIZE];

	full_layout(&layout_a);
	CHECK(forge_file_layout_version(&layout_a) == FORGE_FILE_EFFECTS_VERSION);
	size = encode(&layout_a, work);
	CHECK(size > FORGE_FILE_HEADER_SIZE);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok);
	CHECK(layouts_equal(&layout_a, &layout_b));
	CHECK(decode(work, size, NULL, &layout_b) == _forge_file_ok);
	/* (written again: the same bytes) */
	again = encode(&layout_b, second);
	CHECK(again == size && !memcmp(work, second, (size_t)size));

	/* (the good file the damage tests start from) */
	sample_layout(&layout_a);
	good_size = encode(&layout_a, good);
	CHECK(good_size > 0);
	CHECK(decode(good, good_size, "bloodgulch", &layout_b) == _forge_file_ok && layouts_equal(&layout_a, &layout_b));
	CHECK(layout_b.version == 1);
}

/* ---------- version 12: AI movement */

/* Blood Gulch with a route of three points (one a landing), a patrol area,
an Elite that patrols it and takes vehicles, a Warthog driven on it */
static void movement_layout(struct forge_file_layout *layout)
{
	struct forge_file_record *record;

	sample_layout(layout);
	add_path(layout, GROUP('a', 'c', 't', 'v'), "characters\\elite\\elite major");
	record = add_record(layout, 4, 95.f, -150.f);
	record->ai_behavior = FORGE_FILE_AI_PATROL;
	record->ai_route = 2;
	record->ai_options = FORGE_FILE_AI_OPTION_VEHICLES;
	layout->records[0].ai_route = 2;
	add_marker(layout, _forge_record_ai_route_point, 1, FORGE_FILE_ROUTE_STOP_NONE);
	add_marker(layout, _forge_record_ai_route_point, 1, FORGE_FILE_ROUTE_STOP_WAIT_5);
	add_marker(layout, _forge_record_ai_route_point, 1, FORGE_FILE_ROUTE_STOP_LAND);
	add_marker(layout, _forge_record_ai_area, 1, FORGE_FILE_AI_AREA_SIZES - 1);
}

/* a movement layout with one record changed, written: the writer's result */
static int movement_refused(int record_index, int field, int value)
{
	int result = _forge_file_ok;
	struct forge_file_record *record;

	movement_layout(&layout_a);
	record = &layout_a.records[record_index];
	switch (field)
	{
	case 0: record->ai_behavior = (unsigned char)value; break;
	case 1: record->ai_route = (unsigned char)value; break;
	case 2: record->ai_options = (unsigned char)value; break;
	case 3: record->team = (signed char)value; break;
	case 4: record->variant = (unsigned char)value; break;
	case 5: record->path_index = (unsigned short)value; break;
	case 6: record->kind = (unsigned char)value; break;
	case 7: record->angles[1] = (float)value; break;
	}
	if (forge_file_encode(&layout_a, work, FORGE_FILE_MAXIMUM_SIZE, &result) >= 0)
		return _forge_file_ok;
	return result;
}

static void test_ai_movement(void)
{
	int size;
	int again_size;
	int index;
	static unsigned char second[FORGE_FILE_MAXIMUM_SIZE];

	/* (written as version 12, read back the same, the same bytes again) */
	movement_layout(&layout_a);
	CHECK(forge_file_layout_version(&layout_a) == 12);
	size = encode(&layout_a, work);
	CHECK(size > 0 && work[AT_VERSION] == 12);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok && layout_b.version == 12);
	CHECK(layouts_equal(&layout_a, &layout_b));
	CHECK(layout_b.records[5].ai_behavior == FORGE_FILE_AI_PATROL && layout_b.records[5].ai_route == 2 &&
		layout_b.records[5].ai_options == FORGE_FILE_AI_OPTION_VEHICLES && layout_b.records[0].ai_route == 2);
	CHECK(encode(&layout_b, second) == size && !memcmp(work, second, (size_t)size));
	/* (the fields in their bytes: behavior, route, options after the AI team) */
	CHECK(work[record_at(&layout_a, 5) + RECORD_RESERVED] == FORGE_FILE_AI_PATROL &&
		work[record_at(&layout_a, 5) + RECORD_RESERVED + 1] == 2 &&
		work[record_at(&layout_a, 5) + RECORD_RESERVED + 2] == FORGE_FILE_AI_OPTION_VEHICLES);
	/* (a version 12 file with nothing of version 12 is not one a writer made;
	versions 8 to 11 and 13 are not this build's) */
	sample_layout(&layout_a);
	layout_a.version = 12;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);
	for (index = 8; index <= 11; index++)
	{
		layout_a.version = (unsigned short)index;
		CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_version);
	}
	movement_layout(&layout_a);
	size = encode(&layout_a, work);
	put16(work, AT_VERSION, FORGE_FILE_VERSION + 1);
	reseal(work, size);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_newer_version);
	/* (version 13 with 256 records or fewer: not one a writer made) */
	put16(work, AT_VERSION, FORGE_FILE_MORE_RECORDS_VERSION);
	reseal(work, size);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_bad_version);
	put16(work, AT_VERSION, 9);
	reseal(work, size);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_newer_version);
	/* (a version 12 field in an older file: the bytes were reserved) */
	put16(work, AT_VERSION, 7);
	reseal(work, size);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_bad_record);
	/* (out of range, or on what does not take it) */
	CHECK(movement_refused(5, 0, NUMBER_OF_FORGE_FILE_AI_BEHAVIORS) == _forge_file_bad_record);
	CHECK(movement_refused(5, 1, FORGE_FILE_MAXIMUM_ROUTES + 1) == _forge_file_bad_record);
	CHECK(movement_refused(5, 2, 0x80) == _forge_file_bad_record);
	CHECK(movement_refused(1, 1, 1) == _forge_file_bad_record);
	CHECK(movement_refused(0, 0, FORGE_FILE_AI_PURSUE) == _forge_file_bad_record);
	CHECK(movement_refused(0, 2, FORGE_FILE_AI_OPTION_VEHICLES) == _forge_file_bad_record);
	CHECK(movement_refused(0, 1, FORGE_FILE_MAXIMUM_ROUTES) == _forge_file_ok);
	CHECK(movement_refused(6, 3, FORGE_FILE_MAXIMUM_ROUTES) == _forge_file_bad_record);
	CHECK(movement_refused(6, 3, -1) == _forge_file_bad_record);
	CHECK(movement_refused(6, 4, NUMBER_OF_FORGE_FILE_ROUTE_STOPS) == _forge_file_bad_record);
	CHECK(movement_refused(9, 4, FORGE_FILE_AI_AREA_SIZES) == _forge_file_bad_record);
	CHECK(movement_refused(6, 5, 0) == _forge_file_bad_record);
	CHECK(movement_refused(6, 1, 1) == _forge_file_bad_record);
	CHECK(movement_refused(6, 7, 1) == _forge_file_bad_record);
	/* (the Vita build's kinds 10 to 18, and 21: refused) */
	for (index = 10; index <= 21; index++)
	{
		if (index == _forge_record_ai_route_point || index == _forge_record_ai_area)
			continue;
		CHECK(movement_refused(6, 6, index) == _forge_file_bad_record);
	}
	/* (no more route points or areas than the most) */
	movement_layout(&layout_a);
	for (index = 3; index <= FORGE_FILE_MAXIMUM_ROUTE_POINTS; index++)
		add_marker(&layout_a, _forge_record_ai_route_point, index % FORGE_FILE_MAXIMUM_ROUTES, 0);
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_too_many);
	layout_a.record_count--;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_ok);

	/* (a version 11 file: the map's own objects alone, written as 11 and
	read back; version 12 holds the block too, with AI movement) */
	sample_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_scenery, 0, 7);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 1);
	CHECK(forge_file_layout_version(&layout_a) == 11);
	size = encode(&layout_a, work);
	CHECK(size > 0 && work[AT_VERSION] == 11);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok && layout_b.version == 11 &&
		layouts_equal(&layout_a, &layout_b) && layout_b.canvas.entry_count == 2);
	movement_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_scenery, 0, 7);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 1);
	CHECK(forge_file_layout_version(&layout_a) == 12);
	size = encode(&layout_a, work);
	CHECK(size > 0 && work[AT_VERSION] == 12 && (work[AT_FLAGS] & FORGE_FILE_FLAG_CANVAS));
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok && layout_b.version == 12 &&
		layouts_equal(&layout_a, &layout_b) && layout_b.canvas.entry_count == 2);
	again_size = encode(&layout_b, second);
	CHECK(size > 0 && again_size == size && !memcmp(work, second, (size_t)size));
	/* (version 13: more than 256 records, with AI movement and the block) */
	movement_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_scenery, 0, 7);
	while (layout_a.record_count <= FORGE_FILE_EARLIER_MAXIMUM_RECORDS)
		add_record(&layout_a, 1, 50.f + (float)(layout_a.record_count % 40), -120.f - (float)(layout_a.record_count / 40));
	CHECK(forge_file_layout_version(&layout_a) == FORGE_FILE_MORE_RECORDS_VERSION);
	size = encode(&layout_a, work);
	CHECK(size > 0 && work[AT_VERSION] == FORGE_FILE_MORE_RECORDS_VERSION);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok &&
		layout_b.version == FORGE_FILE_MORE_RECORDS_VERSION && layouts_equal(&layout_a, &layout_b));
	/* (the same as version 12: too many records for it) */
	put16(work, AT_VERSION, 12);
	reseal(work, size);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_too_many);
	/* (the block's flag on a version 12 file without the block: refused) */
	movement_layout(&layout_a);
	layout_a.flags |= FORGE_FILE_FLAG_CANVAS;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_record);
	movement_layout(&layout_a);
	for (index = 1; index <= FORGE_FILE_MAXIMUM_AI_AREAS; index++)
		add_marker(&layout_a, _forge_record_ai_area, 0, 0);
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_too_many);
	/* (the netgame flag: a route point is no plain object) */
	movement_layout(&layout_a);
	layout_a.flags = 0;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_flags);
}

/* ---------- damaged and hostile files */

static void test_refused(void)
{
	int record0;
	int paths;
	int meta;

	sample_layout(&layout_a);
	record0 = record_at(&layout_a, 0);
	paths = FORGE_FILE_HEADER_SIZE;

	/* sizes and the header */
	CHECK(decode(good, 0, "bloodgulch", &layout_c) == _forge_file_too_small);
	CHECK(decode(NULL, 0, "bloodgulch", &layout_c) == _forge_file_too_small);
	CHECK(decode(good, FORGE_FILE_HEADER_SIZE - 1, "bloodgulch", &layout_c) == _forge_file_too_small);
	memset(work, 0, sizeof(work));
	memcpy(work, good, (size_t)good_size);
	CHECK(decode(work, FORGE_FILE_MAXIMUM_SIZE + 1, "bloodgulch", &layout_c) == _forge_file_too_large);
	fresh();
	work[0] = 'X';
	CHECK(damaged(good_size, 1) == _forge_file_bad_magic);
	fresh();
	put16(work, AT_VERSION, 0);
	CHECK(damaged(good_size, 1) == _forge_file_bad_version);
	fresh();
	put16(work, AT_VERSION, 8);
	CHECK(damaged(good_size, 1) == _forge_file_newer_version);
	fresh();
	put16(work, AT_VERSION, 10);
	CHECK(damaged(good_size, 1) == _forge_file_newer_version);
	fresh();
	put16(work, AT_VERSION, FORGE_FILE_VERSION + 1);
	CHECK(damaged(good_size, 1) == _forge_file_newer_version);
	/* (versions 11 to 16 with nothing of their own) */
	fresh();
	put16(work, AT_VERSION, 11);
	CHECK(damaged(good_size, 1) == _forge_file_bad_version);
	fresh();
	put16(work, AT_VERSION, 12);
	CHECK(damaged(good_size, 1) == _forge_file_bad_version);
	fresh();
	put16(work, AT_VERSION, 13);
	CHECK(damaged(good_size, 1) == _forge_file_bad_version);
	fresh();
	put16(work, AT_VERSION, FORGE_FILE_VOLUMES_VERSION);
	CHECK(damaged(good_size, 1) == _forge_file_bad_version);
	fresh();
	put16(work, AT_VERSION, FORGE_FILE_ENVIRONMENT_VERSION);
	CHECK(damaged(good_size, 1) == _forge_file_bad_version);
	fresh();
	put16(work, AT_VERSION, FORGE_FILE_EFFECTS_VERSION);
	CHECK(damaged(good_size, 1) == _forge_file_bad_version);
	fresh();
	put16(work, AT_VERSION, 0xFFFF);
	CHECK(damaged(good_size, 1) == _forge_file_newer_version);
	fresh();
	put16(work, AT_HEADER_SIZE, 63);
	CHECK(damaged(good_size, 1) == _forge_file_bad_header_size);
	fresh();
	put32(work, AT_FLAGS, 1u << 1);
	CHECK(damaged(good_size, 1) == _forge_file_bad_flags);
	fresh();
	put32(work, AT_FLAGS, 1u << 3);
	CHECK(damaged(good_size, 1) == _forge_file_bad_flags);
	/* (the netgame flag without a marker) */
	fresh();
	put32(work, AT_FLAGS, FORGE_FILE_FLAG_NETGAME_SPAWNS);
	CHECK(damaged(good_size, 1) == _forge_file_bad_flags);
	fresh();
	work[AT_MAP] = 'B';
	CHECK(damaged(good_size, 1) == _forge_file_bad_map_name);
	fresh();
	work[AT_MAP + 20] = 'x';
	CHECK(damaged(good_size, 1) == _forge_file_bad_map_name);
	fresh();
	CHECK(decode(work, good_size, "sidewinder", &layout_c) == _forge_file_wrong_map);

	/* counts */
	fresh();
	put16(work, AT_RECORDS, FORGE_FILE_MAXIMUM_RECORDS + 1);
	CHECK(damaged(good_size, 1) == _forge_file_too_many);
	fresh();
	put16(work, AT_PATHS, FORGE_FILE_MAXIMUM_PATHS + 1);
	CHECK(damaged(good_size, 1) == _forge_file_too_many);
	fresh();
	put16(work, AT_PACKS, FORGE_FILE_MAXIMUM_PACKS + 1);
	CHECK(damaged(good_size, 1) == _forge_file_too_many);
	fresh();
	put16(work, AT_META, FORGE_FILE_MAXIMUM_META + 1);
	CHECK(damaged(good_size, 1) == _forge_file_too_many);
	/* (more records than the file has bytes for) */
	fresh();
	put16(work, AT_RECORDS, 200);
	CHECK(damaged(good_size, 1) == _forge_file_bad_payload_size);

	/* the payload's size and checksum */
	fresh();
	CHECK(damaged(good_size - 1, 0) == _forge_file_bad_payload_size);
	/* (cut short and sealed again: the meta's size is not the bytes left) */
	fresh();
	CHECK(damaged(good_size - 1, 1) == _forge_file_bad_payload_size);
	fresh();
	put32(work, AT_PAYLOAD, (unsigned int)(good_size - FORGE_FILE_HEADER_SIZE + 4));
	CHECK(damaged(good_size, 0) == _forge_file_bad_payload_size);
	fresh();
	work[record0 + RECORD_POSITION] ^= 1;
	CHECK(damaged(good_size, 0) == _forge_file_bad_checksum);

	/* paths */
	fresh();
	work[paths + 5] = 'V';
	CHECK(damaged(good_size, 1) == _forge_file_bad_path);
	fresh();
	work[paths + 5] = '.';
	work[paths + 6] = '.';
	CHECK(damaged(good_size, 1) == _forge_file_bad_path);
	fresh();
	work[paths + 5] = '\\';
	CHECK(damaged(good_size, 1) == _forge_file_bad_path);
	fresh();
	work[paths + 7] = 0;
	CHECK(damaged(good_size, 1) == _forge_file_bad_path);
	fresh();
	work[paths + 4] = 0;
	CHECK(damaged(good_size, 1) == _forge_file_bad_path);
	fresh();
	work[paths + 4] = 255;
	CHECK(damaged(good_size, 1) == _forge_file_bad_path);
	fresh();
	put32(work, paths, GROUP('b', 'i', 't', 'm'));
	CHECK(damaged(good_size, 1) == _forge_file_bad_group);
	/* (the same path twice) */
	sample_layout(&layout_a);
	snprintf(layout_a.paths[1].path, sizeof(layout_a.paths[1].path), "%s", layout_a.paths[0].path);
	layout_a.paths[1].group_tag = layout_a.paths[0].group_tag;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_duplicate_path);
	/* (a pack's bad name) */
	full_layout(&layout_a);
	layout_a.packs[0].name[0] = 'C';
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_pack);

	/* records */
	fresh();
	put16(work, record0 + RECORD_PATH, 99);
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	put16(work, record0 + RECORD_PATH, 2);
	work[record0 + RECORD_KIND] = _forge_record_vehicle_spawn;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	put32(work, record0 + RECORD_POSITION, 0x7FC00000u);
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	put32(work, record0 + RECORD_POSITION + 4, 0x7F800000u);
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	put32(work, record0 + RECORD_POSITION + 8, 0x45BB8000u);
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	put32(work, record0 + 16, 0x40800000u);
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record0 + RECORD_KIND] = NUMBER_OF_FORGE_RECORD_KINDS;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record0 + RECORD_KIND] = 200;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record0 + RECORD_KIND] = _forge_record_hill;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record0 + RECORD_FLAGS] = 0x80;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record0 + RECORD_TEAM] = 8;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	put16(work, record0 + RECORD_RESPAWN, FORGE_FILE_MAXIMUM_RESPAWN_SECONDS + 1);
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record0 + RECORD_COUNT] = 2;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record0 + RECORD_RESERVED + 11] = 1;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	/* (a version 1 file with a later version's settings: an initial spawn's
	flag, an AI team, physics) */
	fresh();
	work[record0 + RECORD_FLAGS] |= FORGE_RECORD_SPAWN_INITIAL;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record0 + RECORD_AI_TEAM] = FORGE_AI_TEAM_HUMAN;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record0 + RECORD_FLAGS] |= FORGE_RECORD_PHYSICS_FIXED;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	/* (a teleporter in a version 4 file) */
	sample_layout(&layout_a);
	add_marker(&layout_a, _forge_record_player_spawn, 0, 0);
	add_marker(&layout_a, _forge_record_player_spawn, 1, 0);
	{
		int size = encode(&layout_a, work);
		int at = record_at(&layout_a, layout_a.record_count - 1);

		CHECK(size > 0 && work[AT_VERSION] == 4);
		work[at + RECORD_KIND] = _forge_record_teleporter_in;
		CHECK(damaged(size, 1) == _forge_file_bad_record);
	}

	/* the layout's limits, each reached in a written file and passed by one
	record changed in it */
	{
		int index;
		int size;
		int at;

		/* (teleporters: 32, then a 33rd) */
		sample_layout(&layout_a);
		for (index = 0; index < FORGE_FILE_MAXIMUM_TELEPORTERS; index++)
			add_marker(&layout_a, index & 1 ? _forge_record_teleporter_out : _forge_record_teleporter_in, index / 2, 0);
		size = encode(&layout_a, work);
		CHECK(size > 0 && decode(work, size, "bloodgulch", &layout_c) == _forge_file_ok);
		at = record_at(&layout_a, 0);
		put16(work, at + RECORD_PATH, FORGE_FILE_NO_PATH);
		work[at + RECORD_KIND] = _forge_record_teleporter_out;
		work[at + RECORD_TEAM] = 0;
		put16(work, at + RECORD_RESPAWN, 0);
		work[at + RECORD_COUNT] = 0;
		work[at + RECORD_FLAGS] = 0;
		CHECK(damaged(size, 1) == _forge_file_too_many);
		/* (a sender that leads nowhere) */
		sample_layout(&layout_a);
		add_marker(&layout_a, _forge_record_teleporter_in, 4, 0);
		add_marker(&layout_a, _forge_record_teleporter_out, 4, 0);
		size = encode(&layout_a, work);
		CHECK(size > 0);
		work[record_at(&layout_a, layout_a.record_count - 1) + RECORD_TEAM] = 5;
		CHECK(damaged(size, 1) == _forge_file_bad_record);
		/* (a channel past the last) */
		work[record_at(&layout_a, layout_a.record_count - 1) + RECORD_TEAM] = FORGE_FILE_MAXIMUM_TELEPORTER_CHANNELS;
		CHECK(damaged(size, 1) == _forge_file_bad_record);
		/* (game type objects: 64, then a 65th) */
		sample_layout(&layout_a);
		for (index = 0; index < FORGE_FILE_MAXIMUM_GAME_OBJECTS; index++)
			add_marker(&layout_a, _forge_record_flag, index % FORGE_FILE_MAXIMUM_RACE_CHECKPOINTS, FORGE_FILE_FLAG_TYPE_RACE);
		size = encode(&layout_a, work);
		CHECK(size > 0 && decode(work, size, "bloodgulch", &layout_c) == _forge_file_ok);
		at = record_at(&layout_a, 0);
		put16(work, at + RECORD_PATH, FORGE_FILE_NO_PATH);
		work[at + RECORD_KIND] = _forge_record_flag;
		work[at + RECORD_TEAM] = 0;
		work[at + 33] = FORGE_FILE_FLAG_TYPE_RACE;
		put16(work, at + RECORD_RESPAWN, 0);
		work[at + RECORD_COUNT] = 0;
		work[at + RECORD_FLAGS] = 0;
		put32(work, at + 16, 0);
		CHECK(damaged(size, 1) == _forge_file_too_many);
		/* (a hill's points: 12, then a 13th) */
		sample_layout(&layout_a);
		for (index = 0; index < FORGE_FILE_MAXIMUM_HILL_POINTS; index++)
			add_marker(&layout_a, _forge_record_flag, 3, FORGE_FILE_FLAG_TYPE_HILL);
		size = encode(&layout_a, work);
		CHECK(size > 0 && decode(work, size, "bloodgulch", &layout_c) == _forge_file_ok);
		at = record_at(&layout_a, 0);
		put16(work, at + RECORD_PATH, FORGE_FILE_NO_PATH);
		work[at + RECORD_KIND] = _forge_record_flag;
		work[at + RECORD_TEAM] = 3;
		work[at + 33] = FORGE_FILE_FLAG_TYPE_HILL;
		put16(work, at + RECORD_RESPAWN, 0);
		work[at + RECORD_COUNT] = 0;
		work[at + RECORD_FLAGS] = 0;
		put32(work, at + 16, 0);
		CHECK(damaged(size, 1) == _forge_file_too_many);
		/* (an index past its type's, a type not in the list, a pitch) */
		work[at + RECORD_TEAM] = FORGE_FILE_MAXIMUM_HILLS;
		CHECK(damaged(size, 1) == _forge_file_bad_record);
		work[at + RECORD_TEAM] = 0;
		work[at + 33] = 1;
		CHECK(damaged(size, 1) == _forge_file_bad_record);
		work[at + 33] = FORGE_FILE_FLAG_TYPE_HILL;
		put32(work, at + 20, 0x3F800000u);
		CHECK(damaged(size, 1) == _forge_file_bad_record);
	}

	/* the meta (the sample's texts, from where they start) */
	sample_layout(&layout_a);
	meta = good_size - (int)(strlen(layout_a.title) + strlen(layout_a.author) + strlen(layout_a.description) +
		strlen(layout_a.date) + 4);
	fresh();
	work[good_size - 1] = 'x';
	CHECK(damaged(good_size, 1) == _forge_file_bad_meta);
	fresh();
	work[meta + 3] = 0xFF;
	CHECK(damaged(good_size, 1) == _forge_file_bad_meta);
	fresh();
	/* (a control character in the author) */
	work[meta + (int)strlen(layout_a.title) + 1 + 2] = 0x07;
	CHECK(damaged(good_size, 1) == _forge_file_bad_meta);
	fresh();
	/* (the date not a date) */
	work[good_size - 5] = 'x';
	CHECK(damaged(good_size, 1) == _forge_file_bad_meta);
	/* (an overlong UTF-8 form, a surrogate, an empty title) */
	sample_layout(&layout_a);
	snprintf(layout_a.title, sizeof(layout_a.title), "A\xC0\xAF");
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_meta);
	snprintf(layout_a.title, sizeof(layout_a.title), "A\xED\xA0\x80");
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_meta);
	snprintf(layout_a.title, sizeof(layout_a.title), "Caf\xC3\xA9");
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_ok);
	layout_a.title[0] = 0;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_meta);

	/* (a refused file leaves nothing in the layout) */
	fresh();
	work[0] = 'X';
	full_layout(&layout_c);
	CHECK(damaged(good_size, 1) == _forge_file_bad_magic && layout_c.record_count == 0 && !layout_c.title[0]);
}

/* ---------- the map's own objects (version 11) */

static void test_canvas(void)
{
	static unsigned char file[FORGE_FILE_MAXIMUM_SIZE];
	int size;
	int block;
	int index;

	/* (a round trip, and the most entries) */
	sample_layout(&layout_a);
	for (index = 0; index < FORGE_FILE_MAXIMUM_CANVAS_ENTRIES; index++)
		add_canvas(&layout_a, index / 200, 0, index % 200);
	size = encode(&layout_a, file);
	CHECK(size > 0 && decode(file, size, "bloodgulch", &layout_b) == _forge_file_ok && layouts_equal(&layout_a, &layout_b));
	CHECK(layout_b.canvas.entry_count == FORGE_FILE_MAXIMUM_CANVAS_ENTRIES);
	layout_a.canvas.entry_count = FORGE_FILE_MAXIMUM_CANVAS_ENTRIES + 1;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_record);
	/* (a category taken away whole, with one kept) */
	sample_layout(&layout_a);
	layout_a.canvas.categories = 1 << _forge_canvas_scenery;
	add_canvas(&layout_a, _forge_canvas_scenery, 1, 4);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 0);
	size = encode(&layout_a, file);
	CHECK(size > 0 && decode(file, size, "bloodgulch", &layout_b) == _forge_file_ok && layouts_equal(&layout_a, &layout_b));
	/* (a whole category alone, no entry) */
	sample_layout(&layout_a);
	layout_a.flags |= FORGE_FILE_FLAG_CANVAS;
	layout_a.canvas.categories = 1 << _forge_canvas_items;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_ok);

	/* (what the validator refuses) */
	sample_layout(&layout_a);
	layout_a.flags |= FORGE_FILE_FLAG_CANVAS;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_record);
	sample_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 2);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 2);
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_record);
	sample_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 2);
	add_canvas(&layout_a, _forge_canvas_scenery, 0, 9);
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_record);
	sample_layout(&layout_a);
	add_canvas(&layout_a, NUMBER_OF_FORGE_CANVAS_CATEGORIES, 0, 0);
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_record);
	sample_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, FORGE_FILE_CANVAS_MAXIMUM_INDEX + 1);
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_record);
	sample_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_vehicles, 1, 0);
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_record);
	sample_layout(&layout_a);
	layout_a.canvas.categories = 1 << NUMBER_OF_FORGE_CANVAS_CATEGORIES;
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 0);
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_record);
	/* (entries without the flag, the flag in an older version) */
	sample_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 0);
	layout_a.flags &= ~(unsigned int)FORGE_FILE_FLAG_CANVAS;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_record);
	sample_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 0);
	layout_a.version = 7;
	CHECK(forge_file_layout_check(&layout_a, NULL) == _forge_file_bad_flags);

	/* (damaged files: the block's header, its count past the bytes) */
	sample_layout(&layout_a);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 0);
	add_canvas(&layout_a, _forge_canvas_vehicles, 0, 1);
	size = encode(&layout_a, file);
	CHECK(size > 0);
	block = record_at(&layout_a, layout_a.record_count);
	memcpy(work, file, (size_t)size);
	work[block] = 2;
	CHECK(damaged(size, 1) == _forge_file_bad_record);
	memcpy(work, file, (size_t)size);
	work[block + 1] = 1;
	CHECK(damaged(size, 1) == _forge_file_bad_record);
	memcpy(work, file, (size_t)size);
	work[block + 6] = 1;
	CHECK(damaged(size, 1) == _forge_file_bad_record);
	memcpy(work, file, (size_t)size);
	put16(work, block + 4, FORGE_FILE_MAXIMUM_CANVAS_ENTRIES + 1);
	CHECK(damaged(size, 1) == _forge_file_bad_record);
	memcpy(work, file, (size_t)size);
	put16(work, block + 4, 0x7FFF);
	CHECK(damaged(size, 1) == _forge_file_bad_record);
	memcpy(work, file, (size_t)size);
	put16(work, block + 4, 900);
	CHECK(damaged(size, 1) == _forge_file_bad_payload_size);
	/* (the entries swapped: not ascending) */
	memcpy(work, file, (size_t)size);
	put16(work, block + FORGE_FILE_CANVAS_HEADER_SIZE + 2, 1);
	put16(work, block + FORGE_FILE_CANVAS_HEADER_SIZE + FORGE_FILE_CANVAS_ENTRY_SIZE + 2, 0);
	CHECK(damaged(size, 1) == _forge_file_bad_record);
	/* (cut short inside the block) */
	memcpy(work, file, (size_t)size);
	put32(work, AT_PAYLOAD, (unsigned int)(block + 3 - FORGE_FILE_HEADER_SIZE));
	CHECK(damaged(block + 3, 1) == _forge_file_bad_payload_size);
	/* (the flag set on a file with no block: its meta read as one) */
	sample_layout(&layout_a);
	size = encode(&layout_a, file);
	memcpy(work, file, (size_t)size);
	put32(work, AT_FLAGS, FORGE_FILE_FLAG_CANVAS);
	put16(work, AT_VERSION, 11);
	CHECK(damaged(size, 1) != _forge_file_ok);
}

/* ---------- volumes (version 14) */

/* a layout of one volume (as add_volume), changed by change, checked */
static int volume_result(int type, int shape, int w, int l, int h, int amount, int change)
{
	struct forge_file_record *record;

	sample_layout(&layout_a);
	record = add_volume(&layout_a, type, shape, w, l, h, amount);
	switch (change)
	{
	case 1: record->team = 2; break;
	case 2: record->angles[1] = 0.25f; break;
	case 3: record->path_index = 0; break;
	case 4: record->respawn_seconds = 30; break;
	case 5: record->params[FORGE_FILE_VOLUME_PARAM_TARGET] = 2; break;
	case 6: record->params[FORGE_FILE_VOLUME_PARAM_TARGET + 1] = 1; break;
	case 7: record->params[FORGE_FILE_RECORD_PARAMS_SIZE - 1] = 1; break;
	case 8: layout_a.version = 7; break;
	case 9: layout_a.flags &= ~FORGE_FILE_FLAG_NETGAME_SPAWNS; break;
	case 10: record->flags = FORGE_RECORD_PLACE_AT_START; break;
	case 11: record->game_types = FORGE_FILE_GAME_TYPE_SLAYER; break;
	case 12: record->position[2] = 6000.f; break;
	/* (a team's volume for every unit) */
	case 13: record->team = 0; break;
	}
	return forge_file_layout_check(&layout_a, "bloodgulch");
}

static void test_volumes(void)
{
	struct forge_file_record *record;
	int size;
	int index;
	int record_index;

	/* (the good ones: each type, each shape, the ranges' ends) */
	CHECK(volume_result(FORGE_FILE_VOLUME_KILL, FORGE_FILE_VOLUME_BOX, 1, 1, 1, 0, 0) == _forge_file_ok);
	CHECK(volume_result(FORGE_FILE_VOLUME_KILL, FORGE_FILE_VOLUME_SPHERE, 255, 0, 0, 0, 0) == _forge_file_ok);
	CHECK(volume_result(FORGE_FILE_VOLUME_DAMAGE, FORGE_FILE_VOLUME_BOX, 8, 8, 8, 1, 0) == _forge_file_ok);
	CHECK(volume_result(FORGE_FILE_VOLUME_DAMAGE, FORGE_FILE_VOLUME_BOX, 8, 8, 8, 100, 0) == _forge_file_ok);
	CHECK(volume_result(FORGE_FILE_VOLUME_GRAVITY, FORGE_FILE_VOLUME_BOX, 8, 8, 8, 0, 0) == _forge_file_ok);
	CHECK(volume_result(FORGE_FILE_VOLUME_GRAVITY, FORGE_FILE_VOLUME_BOX, 8, 8, 8, 200, 0) == _forge_file_ok);
	/* (out of range: the type, the shape, a size, the amount for its type) */
	CHECK(volume_result(NUMBER_OF_FORGE_FILE_VOLUME_TYPES, FORGE_FILE_VOLUME_BOX, 8, 8, 8, 0, 0) == _forge_file_bad_record);
	CHECK(volume_result(FORGE_FILE_VOLUME_KILL, NUMBER_OF_FORGE_FILE_VOLUME_SHAPES, 8, 8, 8, 0, 0) == _forge_file_bad_record);
	CHECK(volume_result(FORGE_FILE_VOLUME_KILL, FORGE_FILE_VOLUME_BOX, 8, 0, 8, 0, 0) == _forge_file_bad_record);
	CHECK(volume_result(FORGE_FILE_VOLUME_KILL, FORGE_FILE_VOLUME_SPHERE, 8, 8, 0, 0, 0) == _forge_file_bad_record);
	CHECK(volume_result(FORGE_FILE_VOLUME_KILL, FORGE_FILE_VOLUME_SPHERE, 0, 0, 0, 0, 0) == _forge_file_bad_record);
	CHECK(volume_result(FORGE_FILE_VOLUME_KILL, FORGE_FILE_VOLUME_BOX, 8, 8, 8, 5, 0) == _forge_file_bad_record);
	CHECK(volume_result(FORGE_FILE_VOLUME_DAMAGE, FORGE_FILE_VOLUME_BOX, 8, 8, 8, 0, 0) == _forge_file_bad_record);
	CHECK(volume_result(FORGE_FILE_VOLUME_DAMAGE, FORGE_FILE_VOLUME_BOX, 8, 8, 8, 101, 0) == _forge_file_bad_record);
	CHECK(volume_result(FORGE_FILE_VOLUME_GRAVITY, FORGE_FILE_VOLUME_BOX, 8, 8, 8, 201, 0) == _forge_file_bad_record);
	/* (the rest of a record: each change refused) */
	for (index = 1; index <= 13; index++)
	{
		int result = volume_result(FORGE_FILE_VOLUME_KILL, FORGE_FILE_VOLUME_BOX, 8, 8, 8, 0, index);

		CHECK(result != _forge_file_ok);
		if (result == _forge_file_ok)
			printf("  (volume change %d passed)\n", index);
	}
	/* (another kind's own bytes: zeros) */
	sample_layout(&layout_a);
	layout_a.records[0].params[0] = 1;
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") == _forge_file_bad_record);
	/* (the most volumes, and one more) */
	sample_layout(&layout_a);
	for (index = 0; index < FORGE_FILE_MAXIMUM_VOLUMES; index++)
		add_volume(&layout_a, FORGE_FILE_VOLUME_KILL, FORGE_FILE_VOLUME_BOX, 4, 4, 4, 0);
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") == _forge_file_ok);
	add_volume(&layout_a, FORGE_FILE_VOLUME_KILL, FORGE_FILE_VOLUME_BOX, 4, 4, 4, 0);
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") == _forge_file_too_many);
	/* (in a file: a byte of a volume's changed is refused, resealed; a
	plain object's own bytes too) */
	sample_layout(&layout_a);
	record = add_volume(&layout_a, FORGE_FILE_VOLUME_DAMAGE, FORGE_FILE_VOLUME_BOX, 8, 8, 8, 10);
	(void)record;
	size = encode(&layout_a, work);
	CHECK(size > 0);
	record_index = record_at(&layout_a, layout_a.record_count - 1);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok && layouts_equal(&layout_a, &layout_b));
	memcpy(good, work, (size_t)size);
	good_size = size;
	fresh();
	work[record_index + 36] = 1;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record_index + 39 + FORGE_FILE_VOLUME_PARAM_AMOUNT] = 0;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record_index + 47] = 1;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record_at(&layout_a, 0) + 39] = 1;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	CHECK(damaged(good_size, 1) == _forge_file_ok);
}

/* ---------- the sky and fog (version 15) */

/* a layout with a sky's path and one sky and fog (as add_environment),
changed by change, checked */
static int environment_result(int sky, int density, int start, int opaque, int change)
{
	struct forge_file_record *record;

	sample_layout(&layout_a);
	add_path(&layout_a, FORGE_FILE_SKY_GROUP, "sky\\sky_bloodgulch\\sky_bloodgulch");
	record = add_environment(&layout_a, sky ? layout_a.path_count - 1 : -1, 120, density, start, opaque);
	switch (change)
	{
	case 1: record->team = 1; break;
	case 2: record->position[0] = 1.f; break;
	case 3: record->angles[0] = 0.5f; break;
	case 4: record->respawn_seconds = 30; break;
	case 5: record->params[FORGE_FILE_ENVIRONMENT_PARAM_SETS] |= 4; break;
	case 6: record->params[FORGE_FILE_ENVIRONMENT_PARAM_OPAQUE + 1] = 1; break;
	case 7: record->params[FORGE_FILE_RECORD_PARAMS_SIZE - 1] = 1; break;
	case 8: layout_a.version = 7; break;
	case 9: layout_a.flags &= ~FORGE_FILE_FLAG_NETGAME_SPAWNS; break;
	/* (the sky bit without a path, a path without the bit, a path of
	another group) */
	case 10: record->path_index = FORGE_FILE_NO_PATH; break;
	case 11: record->params[FORGE_FILE_ENVIRONMENT_PARAM_SETS] &= (unsigned char)~FORGE_FILE_ENVIRONMENT_SKY; break;
	case 12: record->path_index = 0; break;
	/* (fog bytes without the fog bit) */
	case 13: record->params[FORGE_FILE_ENVIRONMENT_PARAM_SETS] &= (unsigned char)~FORGE_FILE_ENVIRONMENT_FOG; break;
	/* (two of them) */
	case 14: layout_a.records[layout_a.record_count++] = *record; break;
	case 15: record->variant = 1; break;
	}
	return forge_file_layout_check(&layout_a, "bloodgulch");
}

static void test_environment(void)
{
	int index;
	int size;
	int at;

	/* (the good ones: a sky alone, a fog alone, both, the ranges' ends) */
	CHECK(environment_result(1, 0, 0, 0, 0) == _forge_file_ok);
	CHECK(environment_result(0, 1, 0, 1, 0) == _forge_file_ok);
	CHECK(environment_result(1, 100, 254, 255, 0) == _forge_file_ok);
	/* (out of range: nothing set, density, start past whole) */
	CHECK(environment_result(0, 0, 0, 0, 0) == _forge_file_bad_record);
	CHECK(environment_result(0, 101, 0, 10, 0) == _forge_file_bad_record);
	CHECK(environment_result(0, 50, 20, 10, 0) == _forge_file_bad_record);
	CHECK(environment_result(0, 50, 20, 0, 0) == _forge_file_bad_record);
	CHECK(environment_result(0, 50, 255, 255, 0) == _forge_file_bad_record);
	for (index = 1; index <= 15; index++)
	{
		int result = environment_result(1, 50, 10, 60, index);

		CHECK(result != _forge_file_ok);
		if (result == _forge_file_ok)
			printf("  (sky and fog change %d passed)\n", index);
	}
	/* (a sky's path in a layout of an older version) */
	sample_layout(&layout_a);
	add_path(&layout_a, FORGE_FILE_SKY_GROUP, "sky\\sky_bloodgulch\\sky_bloodgulch");
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") == _forge_file_bad_group);
	/* (a sky's path for an object) */
	layout_a.records[0].path_index = (unsigned short)(layout_a.path_count - 1);
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") != _forge_file_ok);
	/* (another kind's own bytes: zeros) */
	sample_layout(&layout_a);
	layout_a.records[0].params[0] = 1;
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") == _forge_file_bad_record);
	/* (in a file: round trip; a byte changed, resealed, refused) */
	sample_layout(&layout_a);
	add_path(&layout_a, FORGE_FILE_SKY_GROUP, "sky\\sky_bloodgulch\\sky_bloodgulch");
	add_environment(&layout_a, layout_a.path_count - 1, 30, 50, 10, 60);
	size = encode(&layout_a, work);
	CHECK(size > 0);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok && layouts_equal(&layout_a, &layout_b));
	memcpy(good, work, (size_t)size);
	good_size = size;
	at = record_at(&layout_a, layout_a.record_count - 1);
	fresh();
	work[at + 36] = 1;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[at + 39 + FORGE_FILE_ENVIRONMENT_PARAM_DENSITY] = 0;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[at + 47] = 1;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record_at(&layout_a, 0) + 40] = 1;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	CHECK(damaged(good_size, 1) == _forge_file_ok);
}

/* ---------- effects and decals (version 16) */

/* a layout with an effect's and a decal's path and one record of kind
(an effect's or a decal's) of value, changed by change, checked */
static int effect_result(int kind, int value, int change)
{
	struct forge_file_record *record;
	int effect_path;

	sample_layout(&layout_a);
	add_path(&layout_a, FORGE_FILE_EFFECT_GROUP, "effects\\burning\\fire");
	effect_path = layout_a.path_count - 1;
	add_path(&layout_a, FORGE_FILE_DECAL_GROUP, "decals\\scorch\\scorch");
	record = add_effect(&layout_a, kind, kind == _forge_record_decal ? effect_path + 1 : effect_path, value);
	switch (change)
	{
	case 1: record->team = 1; break;
	case 2: record->angles[1] = 0.5f; break;
	case 3: record->respawn_seconds = 30; break;
	case 4: record->params[1] = 1; break;
	case 5: record->params[FORGE_FILE_RECORD_PARAMS_SIZE - 1] = 1; break;
	case 6: layout_a.version = 7; break;
	case 7: layout_a.flags &= ~FORGE_FILE_FLAG_NETGAME_SPAWNS; break;
	/* (no path, an object's path, the other group's path) */
	case 8: record->path_index = FORGE_FILE_NO_PATH; break;
	case 9: record->path_index = 0; break;
	case 10: record->path_index = (unsigned short)(kind == _forge_record_decal ? effect_path : effect_path + 1); break;
	case 11: record->variant = 1; break;
	case 12: record->flags = FORGE_RECORD_PLACE_AT_START; break;
	}
	return forge_file_layout_check(&layout_a, "bloodgulch");
}

static void test_effects(void)
{
	int index;
	int size;
	int at;

	CHECK(effect_result(_forge_record_effect, 0, 0) == _forge_file_ok);
	CHECK(effect_result(_forge_record_effect, 60, 0) == _forge_file_ok);
	CHECK(effect_result(_forge_record_decal, 1, 0) == _forge_file_ok);
	CHECK(effect_result(_forge_record_decal, 40, 0) == _forge_file_ok);
	CHECK(effect_result(_forge_record_effect, 61, 0) == _forge_file_bad_record);
	CHECK(effect_result(_forge_record_decal, 0, 0) == _forge_file_bad_record);
	CHECK(effect_result(_forge_record_decal, 41, 0) == _forge_file_bad_record);
	for (index = 1; index <= 12; index++)
	{
		int result = effect_result(index % 2 ? _forge_record_effect : _forge_record_decal, 5, index);

		CHECK(result != _forge_file_ok);
		if (result == _forge_file_ok)
			printf("  (effect change %d passed)\n", index);
	}
	/* (an effect's or decal's path in a layout of an older version, or for an
	object) */
	sample_layout(&layout_a);
	add_path(&layout_a, FORGE_FILE_EFFECT_GROUP, "effects\\burning\\fire");
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") == _forge_file_bad_group);
	sample_layout(&layout_a);
	add_path(&layout_a, FORGE_FILE_DECAL_GROUP, "decals\\scorch\\scorch");
	add_effect(&layout_a, _forge_record_decal, layout_a.path_count - 1, 10);
	layout_a.records[0].path_index = (unsigned short)(layout_a.path_count - 1);
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") == _forge_file_bad_record);
	/* (the most, and one more) */
	sample_layout(&layout_a);
	add_path(&layout_a, FORGE_FILE_EFFECT_GROUP, "effects\\burning\\fire");
	add_path(&layout_a, FORGE_FILE_DECAL_GROUP, "decals\\scorch\\scorch");
	for (index = 0; index < FORGE_FILE_MAXIMUM_EFFECTS; index++)
		add_effect(&layout_a, _forge_record_effect, layout_a.path_count - 2, 2);
	for (index = 0; index < FORGE_FILE_MAXIMUM_DECALS; index++)
		add_effect(&layout_a, _forge_record_decal, layout_a.path_count - 1, 10);
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") == _forge_file_ok);
	add_effect(&layout_a, _forge_record_effect, layout_a.path_count - 2, 2);
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") == _forge_file_too_many);
	layout_a.record_count--;
	add_effect(&layout_a, _forge_record_decal, layout_a.path_count - 1, 10);
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") == _forge_file_too_many);
	/* (another kind's own bytes: zeros) */
	sample_layout(&layout_a);
	layout_a.records[0].params[0] = 1;
	CHECK(forge_file_layout_check(&layout_a, "bloodgulch") == _forge_file_bad_record);
	/* (in a file: round trip; bytes changed, resealed, refused) */
	sample_layout(&layout_a);
	add_path(&layout_a, FORGE_FILE_EFFECT_GROUP, "effects\\burning\\fire");
	add_effect(&layout_a, _forge_record_effect, layout_a.path_count - 1, 3);
	size = encode(&layout_a, work);
	CHECK(size > 0);
	CHECK(decode(work, size, "bloodgulch", &layout_b) == _forge_file_ok && layouts_equal(&layout_a, &layout_b));
	memcpy(good, work, (size_t)size);
	good_size = size;
	at = record_at(&layout_a, layout_a.record_count - 1);
	fresh();
	work[at + 36] = 1;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[at + 39] = 99;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[at + 47] = 1;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	work[record_at(&layout_a, 0) + 41] = 1;
	CHECK(damaged(good_size, 1) == _forge_file_bad_record);
	fresh();
	CHECK(damaged(good_size, 1) == _forge_file_ok);
}

/* ---------- the fuzz */

static void test_fuzz(int iterations)
{
	int iteration;
	int accepted = 0;
	static unsigned char again[FORGE_FILE_MAXIMUM_SIZE];

	full_layout(&layout_a);
	good_size = encode(&layout_a, good);
	CHECK(good_size > 0);
	for (iteration = 0; iteration < iterations; iteration++)
	{
		int size = good_size;
		int flips = 1 + (int)(next_random() % 8);
		int flip;
		int result;

		memcpy(work, good, (size_t)good_size);
		if (next_random() % 4 == 0)
			size = (int)(next_random() % (unsigned int)(good_size + 1));
		for (flip = 0; flip < flips && size > 0; flip++)
		{
			int at = (int)(next_random() % (unsigned int)size);

			work[at] = next_random() % 3 ? (unsigned char)(work[at] ^ (1u << (next_random() % 8))) : (unsigned char)next_random();
		}
		if (size >= FORGE_FILE_HEADER_SIZE && next_random() % 2)
			reseal(work, size);
		result = decode(work, size, "bloodgulch", &layout_b);
		CHECK(result >= 0 && result < NUMBER_OF_FORGE_FILE_RESULTS);
		if (result == _forge_file_ok)
		{
			int written;

			accepted++;
			/* (what passed is a layout that passes again, written and read) */
			CHECK(forge_file_layout_check(&layout_b, "bloodgulch") == _forge_file_ok);
			written = encode(&layout_b, again);
			CHECK(written > 0 && decode(again, written, "bloodgulch", &layout_c) == _forge_file_ok &&
				layouts_equal(&layout_b, &layout_c));
		}
		else
		{
			CHECK(layout_b.record_count == 0 && layout_b.path_count == 0 && !layout_b.title[0]);
		}
	}
	printf("fuzz: %d cases, %d accepted\n", iterations, accepted);
}

/* ---------- the store */

static void test_store(void)
{
	struct forge_store_entry entries[8];
	char why[FORGE_STORE_WHY_SIZE];
	char saved[FORGE_FILE_NAME_SIZE];
	char second[FORGE_FILE_NAME_SIZE];
	char path[1024];
	FILE *file;
	int count;

	sample_layout(&layout_a);
	CHECK(forge_store_save(&layout_a, NULL, saved, sizeof(saved), why, sizeof(why)));
	CHECK(!strcmp(saved, "gulch_test.hmv"));
	CHECK(forge_store_exists("bloodgulch", saved));
	CHECK(forge_store_load("bloodgulch", saved, &layout_b, why, sizeof(why)) && layouts_equal(&layout_a, &layout_b));
	/* (the same title again: a second file, numbered) */
	CHECK(forge_store_save(&layout_a, NULL, second, sizeof(second), why, sizeof(why)));
	CHECK(!strcmp(second, "gulch_test_2.hmv"));
	/* (saved over: the old one kept as the last good copy) */
	full_layout(&layout_a);
	CHECK(forge_store_save(&layout_a, saved, NULL, 0, why, sizeof(why)));
	CHECK(forge_store_load("bloodgulch", saved, &layout_b, why, sizeof(why)) && layout_b.record_count == layout_a.record_count);
	count = forge_store_list("bloodgulch", entries, 8);
	CHECK(count == 2 && !entries[0].damaged && entries[0].objects + entries[1].objects == 5 + layout_a.record_count);
	CHECK(forge_store_list(NULL, entries, 8) == 2);
	/* (a damaged file: refused; its last good copy read in its place) */
	snprintf(path, sizeof(path), "%s/forge/bloodgulch/%s", scratch, saved);
	file = fopen(path, "wb");
	CHECK(file != NULL);
	if (file)
	{
		fputs("HFRG not a layout", file);
		fclose(file);
	}
	CHECK(forge_store_load("bloodgulch", saved, &layout_b, why, sizeof(why)) && layout_b.record_count == 5);
	/* (one with no good copy: refused cleanly, listed as damaged) */
	snprintf(path, sizeof(path), "%s/forge/bloodgulch/junk.hmv", scratch);
	file = fopen(path, "wb");
	if (file)
	{
		fwrite(good, 1, 40, file);
		fclose(file);
	}
	CHECK(!forge_store_load("bloodgulch", "junk.hmv", &layout_b, why, sizeof(why)) && why[0] && layout_b.record_count == 0);
	count = forge_store_list("bloodgulch", entries, 8);
	CHECK(count == 3 && entries[2].damaged);
	/* (the wrong map's folder: refused) */
	CHECK(!forge_store_load("sidewinder", saved, &layout_b, why, sizeof(why)));
	/* (names this port does not make are never opened) */
	CHECK(!forge_store_load("bloodgulch", "../gulch_test.hmv", &layout_b, why, sizeof(why)));
	CHECK(!forge_store_load("../forge", saved, &layout_b, why, sizeof(why)));
	CHECK(!forge_store_load("bloodgulch", "gulch_test.txt", &layout_b, why, sizeof(why)));
	CHECK(!forge_store_load("BloodGulch", saved, &layout_b, why, sizeof(why)));
	/* (a layout the writer refuses: nothing written) */
	sample_layout(&layout_a);
	layout_a.records[0].kind = 99;
	CHECK(!forge_store_save(&layout_a, "refused.hmv", NULL, 0, why, sizeof(why)) &&
		!forge_store_exists("bloodgulch", "refused.hmv"));
	CHECK(forge_store_delete("bloodgulch", second) && !forge_store_exists("bloodgulch", second));
	CHECK(!forge_store_delete("bloodgulch", second));
	/* (names) */
	CHECK(forge_file_name_valid("a.hmv") && !forge_file_name_valid(".hmv") && !forge_file_name_valid("a/b.hmv") &&
		!forge_file_name_valid("A.hmv") && !forge_file_name_valid("abcdefghijklmnopqrstuvwxyz123.hmv"));
	forge_file_name_from_title("  My Map!! ", 3, saved, sizeof(saved));
	CHECK(!strcmp(saved, "my_map_3.hmv"));
	forge_file_name_from_title("***", 1, saved, sizeof(saved));
	CHECK(!strcmp(saved, "variant.hmv"));
	/* (names Windows keeps for a device, in any case, with or without an
	extension: never a layout's or a map folder's) */
	{
		static char const *const reserved[] = { "con", "PRN", "Aux", "nul", "com1", "COM9", "lpt1", "Lpt9",
			"con.hmv", "nul.txt", "COM1.hmv", "lpt9.tar.gz", "aux." };
		static char const *const allowed[] = { "cons", "nul_", "com0", "com10", "lpt", "comx", "console", "xcon",
			"lpt0.hmv", "conn.hmv", "", ".con" };
		int index;

		for (index = 0; index < (int)(sizeof(reserved) / sizeof(reserved[0])); index++)
			CHECK(forge_file_name_reserved(reserved[index]));
		for (index = 0; index < (int)(sizeof(allowed) / sizeof(allowed[0])); index++)
			CHECK(!forge_file_name_reserved(allowed[index]));
		CHECK(!forge_file_name_reserved(NULL));
	}
	CHECK(!forge_file_name_valid("con.hmv") && !forge_file_name_valid("nul.hmv") && !forge_file_name_valid("com1.hmv") &&
		!forge_file_name_valid("lpt9.hmv") && forge_file_name_valid("con_1.hmv") && forge_file_name_valid("com10.hmv"));
	CHECK(!forge_file_map_name_valid("aux") && !forge_file_map_name_valid("com3") && forge_file_map_name_valid("auxiliary"));
	forge_file_name_from_title("CON", 1, saved, sizeof(saved));
	CHECK(!strcmp(saved, "con_1.hmv") && forge_file_name_valid(saved));
	forge_file_name_from_title("  Com1 ", 2, saved, sizeof(saved));
	CHECK(!strcmp(saved, "com1_2.hmv"));
	forge_file_name_from_title("Console", 1, saved, sizeof(saved));
	CHECK(!strcmp(saved, "console.hmv"));
	/* (a layout titled after a device saves under a numbered name; one
	given a device's name is refused, nothing written) */
	sample_layout(&layout_a);
	snprintf(layout_a.title, sizeof(layout_a.title), "NUL");
	CHECK(forge_store_save(&layout_a, NULL, saved, sizeof(saved), why, sizeof(why)) && !strcmp(saved, "nul_1.hmv") &&
		forge_store_exists("bloodgulch", "nul_1.hmv"));
	CHECK(forge_store_delete("bloodgulch", "nul_1.hmv"));
	CHECK(!forge_store_save(&layout_a, "nul.hmv", NULL, 0, why, sizeof(why)) && !forge_store_exists("bloodgulch", "nul.hmv"));
	CHECK(!forge_store_load("bloodgulch", "con.hmv", &layout_b, why, sizeof(why)));
	snprintf(layout_a.map_name, sizeof(layout_a.map_name), "aux");
	CHECK(!forge_store_save(&layout_a, NULL, saved, sizeof(saved), why, sizeof(why)));
	forge_store_date_now(saved, sizeof(saved));
	CHECK(strlen(saved) == 16 && saved[4] == '-' && saved[13] == ':');
}

int main(int argc, char **argv)
{
	int iterations = 5000;

	if (argc < 2)
	{
		printf("usage: forge_files_test <scratch folder> [fuzz iterations]\n");
		return 2;
	}
	snprintf(scratch, sizeof(scratch), "%s", argv[1]);
	if (argc > 2)
		iterations = atoi(argv[2]);
	test_versions();
	test_round_trip();
	test_ai_movement();
	test_refused();
	test_canvas();
	test_volumes();
	test_environment();
	test_effects();
	test_store();
	test_fuzz(iterations);
	printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
