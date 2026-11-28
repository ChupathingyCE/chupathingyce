/*
FORGE_FILES.C

Forge's layout files (forge_files.h: the format and the validator's checks,
in order). Nothing here trusts a byte of the file: every count is bounded
before it is used, every offset checked against the bytes there, every
float's bits looked at before it is a float, every text checked as UTF-8,
and the layout handed back only when every check passed (a file is taken
whole or not at all).

SPDX-FileCopyrightText: 2026 BirchWoodGod
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "forge_files.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

#define FORGE_FILE_PI 3.14159265358979f
/* (angles written as float within +-pi, read back with float's rounding) */
#define FORGE_FILE_ANGLE_LIMIT (FORGE_FILE_PI + 0.0001f)

static unsigned char const forge_file_magic[4] = { 'H', 'F', 'R', 'G' };

/* the groups Forge places */
static unsigned int const forge_file_groups[] =
{
	0x76656869, /* vehi */
	0x77656170, /* weap */
	0x65716970, /* eqip */
	0x7363656E, /* scen */
	0x62697064, /* bipd */
	0x6D616368, /* mach */
	0x6374726C, /* ctrl */
	0x6C696669, /* lifi */
	0x61637476, /* actv */
};

#define FORGE_GROUP_VEHICLE 0x76656869
#define FORGE_GROUP_WEAPON 0x77656170
#define FORGE_GROUP_EQUIPMENT 0x65716970
#define FORGE_GROUP_ACTOR_VARIANT 0x61637476

static char const *const forge_file_result_names[NUMBER_OF_FORGE_FILE_RESULTS] =
{
	"ok", "shorter than its header", "larger than 64 KB", "not a Forge layout (magic)", "a bad version",
	"made by a newer build (this build reads layout versions 1 to 7 and 11 to 16)", "another header size", "unknown flags", "a bad map name", "of another map", "a count past its limit",
	"its size is not its counts'", "its checksum is wrong", "a bad pack", "a bad tag path", "a group Forge does not place",
	"a tag path twice", "a bad record", "bad title, author, description or date", "no room to write it",
};

/* ---------- reading and writing little-endian */

struct forge_file_reader
{
	unsigned char const *bytes;
	int size;
	int offset;
	int failed;
};

static unsigned int read_bytes(
	struct forge_file_reader *reader,
	int count)
{
	unsigned int value = 0;
	int index;

	if (reader->failed || count <= 0 || count > 4 || reader->offset < 0 || reader->size - reader->offset < count)
	{
		reader->failed = 1;
		return 0;
	}
	for (index = 0; index < count; index++)
		value |= (unsigned int)reader->bytes[reader->offset + index] << (8 * index);
	reader->offset += count;
	return value;
}

static unsigned int read_u32(struct forge_file_reader *reader) { return read_bytes(reader, 4); }
static unsigned int read_u16(struct forge_file_reader *reader) { return read_bytes(reader, 2); }
static unsigned int read_u8(struct forge_file_reader *reader) { return read_bytes(reader, 1); }

/* a float's bits, NONZERO in *finite when it is a finite number */
static float read_f32(
	struct forge_file_reader *reader,
	int *finite)
{
	unsigned int bits = read_u32(reader);
	float value;

	*finite = (bits & 0x7F800000u) != 0x7F800000u;
	if (!*finite)
		return 0.f;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

struct forge_file_writer
{
	unsigned char *bytes;
	int capacity;
	int offset;
	int failed;
};

static void write_bytes(
	struct forge_file_writer *writer,
	unsigned int value,
	int count)
{
	int index;

	if (writer->failed || writer->capacity - writer->offset < count)
	{
		writer->failed = 1;
		return;
	}
	for (index = 0; index < count; index++)
		writer->bytes[writer->offset + index] = (unsigned char)(value >> (8 * index));
	writer->offset += count;
}

static void write_f32(
	struct forge_file_writer *writer,
	float value)
{
	unsigned int bits;

	memcpy(&bits, &value, sizeof(bits));
	write_bytes(writer, bits, 4);
}

static void write_text(
	struct forge_file_writer *writer,
	char const *text,
	int size)
{
	int index;

	for (index = 0; index < size; index++)
		write_bytes(writer, (unsigned char)text[index], 1);
}

/* ---------- checks */

int forge_file_group_allowed(
	unsigned int group_tag)
{
	int index;

	for (index = 0; index < (int)(sizeof(forge_file_groups) / sizeof(forge_file_groups[0])); index++)
	{
		if (forge_file_groups[index] == group_tag)
			return 1;
	}
	return 0;
}

int forge_file_path_group_allowed(
	unsigned int group_tag)
{
	return forge_file_group_allowed(group_tag) || group_tag == FORGE_FILE_SKY_GROUP || group_tag == FORGE_FILE_EFFECT_GROUP ||
		group_tag == FORGE_FILE_DECAL_GROUP;
}

int forge_file_path_valid(
	char const *path,
	int length)
{
	int index;

	if (!path || length < 1 || length > FORGE_FILE_PATH_MAXIMUM_LENGTH)
		return 0;
	if (path[0] == '\\' || path[length - 1] == '\\')
		return 0;
	for (index = 0; index < length; index++)
	{
		char character = path[index];

		if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
			character == ' ' || character == '\\' || character == '-' || character == '.'))
		{
			return 0;
		}
		/* (no "..", no empty part) */
		if (index + 1 < length && ((character == '.' && path[index + 1] == '.') ||
			(character == '\\' && path[index + 1] == '\\')))
		{
			return 0;
		}
	}
	return 1;
}

int forge_file_map_name_valid(
	char const *name)
{
	int length;

	if (!name)
		return 0;
	for (length = 0; length < FORGE_FILE_MAP_NAME_SIZE && name[length]; length++)
	{
		char character = name[length];

		if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_'))
			return 0;
	}
	return length >= 1 && length < FORGE_FILE_MAP_NAME_SIZE && !forge_file_name_reserved(name);
}

int forge_file_text_valid(
	char const *text,
	int maximum)
{
	unsigned char const *bytes = (unsigned char const *)text;
	int index = 0;

	if (!text)
		return 0;
	while (bytes[index])
	{
		unsigned int code;
		int following;
		int extra;

		if (index >= maximum)
			return 0;
		code = bytes[index];
		if (code < 0x20 || code == 0x7F)
			return 0;
		if (code < 0x80)
		{
			index++;
			continue;
		}
		if ((code & 0xE0) == 0xC0)
		{
			following = 1;
			code &= 0x1F;
		}
		else if ((code & 0xF0) == 0xE0)
		{
			following = 2;
			code &= 0x0F;
		}
		else if ((code & 0xF8) == 0xF0)
		{
			following = 3;
			code &= 0x07;
		}
		else
		{
			return 0;
		}
		if (index + following >= maximum)
			return 0;
		for (extra = 1; extra <= following; extra++)
		{
			if ((bytes[index + extra] & 0xC0) != 0x80)
				return 0;
			code = (code << 6) | (bytes[index + extra] & 0x3F);
		}
		/* (no overlong form, no surrogate, nothing past U+10FFFF, no C1
		control) */
		if ((following == 1 && code < 0x80) || (following == 2 && code < 0x800) || (following == 3 && code < 0x10000) ||
			(code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF || (code >= 0x80 && code < 0xA0))
		{
			return 0;
		}
		index += following + 1;
	}
	return index < maximum;
}

int forge_file_name_reserved(
	char const *name)
{
	static char const *const devices[] = { "con", "prn", "aux", "nul" };
	char lower[5];
	int length;
	int index;

	if (!name)
		return 0;
	/* (the part before the first '.', in lower case: Windows opens the
	device whatever the extension) */
	for (length = 0; name[length] && name[length] != '.'; length++)
	{
		if (length >= 4)
			return 0;
		lower[length] = name[length] >= 'A' && name[length] <= 'Z' ? (char)(name[length] - 'A' + 'a') : name[length];
	}
	lower[length] = 0;
	for (index = 0; index < (int)(sizeof(devices) / sizeof(devices[0])); index++)
	{
		if (!strcmp(lower, devices[index]))
			return 1;
	}
	return length == 4 && (!strncmp(lower, "com", 3) || !strncmp(lower, "lpt", 3)) && lower[3] >= '1' && lower[3] <= '9';
}

int forge_file_name_valid(
	char const *file_name)
{
	int length;
	int stem;

	if (!file_name)
		return 0;
	for (length = 0; length < FORGE_FILE_NAME_SIZE && file_name[length]; length++)
		;
	if (length >= FORGE_FILE_NAME_SIZE)
		return 0;
	stem = length - (int)strlen(FORGE_FILE_EXTENSION);
	if (stem < 1 || stem > FORGE_FILE_STEM_MAXIMUM_LENGTH || strcmp(file_name + stem, FORGE_FILE_EXTENSION))
		return 0;
	for (length = 0; length < stem; length++)
	{
		char character = file_name[length];

		if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
			character == '-'))
		{
			return 0;
		}
	}
	return !forge_file_name_reserved(file_name);
}

void forge_file_name_from_title(
	char const *title,
	int number,
	char *file_name,
	int size)
{
	char stem[FORGE_FILE_STEM_MAXIMUM_LENGTH + 1];
	int length = 0;
	int index;

	for (index = 0; title && title[index] && length < 22; index++)
	{
		char character = title[index];

		if (character >= 'A' && character <= 'Z')
			character = (char)(character - 'A' + 'a');
		if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')))
			character = '_';
		/* (one '_' for a run of others, none at the start) */
		if (character == '_' && (!length || stem[length - 1] == '_'))
			continue;
		stem[length++] = character;
	}
	while (length > 0 && stem[length - 1] == '_')
		length--;
	stem[length] = 0;
	if (!length)
		snprintf(stem, sizeof(stem), "variant");
	/* (a stem Windows keeps for a device is numbered from 1: "con_1.hmv") */
	if (number < 2 && forge_file_name_reserved(stem))
		number = 1;
	else if (number < 2)
		number = 0;
	if (number > 0)
		snprintf(file_name, (size_t)size, "%s_%d%s", stem, number > 9999 ? 9999 : number, FORGE_FILE_EXTENSION);
	else
		snprintf(file_name, (size_t)size, "%s%s", stem, FORGE_FILE_EXTENSION);
}

float forge_file_angle_reduce(
	float angle)
{
	int turns = 0;

	if (!(angle == angle) || angle > 1000000.f || angle < -1000000.f)
		return 0.f;
	while (angle > FORGE_FILE_PI && turns++ < 1000)
		angle -= 2.f * FORGE_FILE_PI;
	while (angle < -FORGE_FILE_PI && turns++ < 2000)
		angle += 2.f * FORGE_FILE_PI;
	if (angle > FORGE_FILE_PI)
		angle = FORGE_FILE_PI;
	if (angle < -FORGE_FILE_PI)
		angle = -FORGE_FILE_PI;
	return angle;
}

char const *forge_file_result_name(
	int result)
{
	return result >= 0 && result < NUMBER_OF_FORGE_FILE_RESULTS ? forge_file_result_names[result] : "?";
}

unsigned int forge_file_crc32(
	unsigned int crc,
	unsigned char const *bytes,
	int size)
{
	static unsigned int table[256];
	static int table_made;
	int index;

	if (!table_made)
	{
		unsigned int entry;
		int bit;

		for (index = 0; index < 256; index++)
		{
			entry = (unsigned int)index;
			for (bit = 0; bit < 8; bit++)
				entry = (entry & 1) ? 0xEDB88320u ^ (entry >> 1) : entry >> 1;
			table[index] = entry;
		}
		table_made = 1;
	}
	crc = ~crc;
	for (index = 0; index < size; index++)
		crc = table[(crc ^ bytes[index]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

/* a pack's name: 1 to 24 of [a-z0-9_ -] */
static int forge_file_pack_name_valid(
	char const *name)
{
	int length;

	for (length = 0; length <= FORGE_FILE_PACK_NAME_SIZE && name[length]; length++)
	{
		char character = name[length];

		if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
			character == ' ' || character == '-'))
		{
			return 0;
		}
	}
	return length >= 1 && length <= FORGE_FILE_PACK_NAME_SIZE;
}

/* "YYYY-MM-DD HH:MM", or empty */
static int forge_file_date_valid(
	char const *date)
{
	static char const pattern[] = "dddd-dd-dd dd:dd";
	int index;

	if (!date[0])
		return 1;
	for (index = 0; pattern[index]; index++)
	{
		if (pattern[index] == 'd' ? !(date[index] >= '0' && date[index] <= '9') : date[index] != pattern[index])
			return 0;
	}
	return date[index] == 0;
}

/* the groups a record's kind takes: 0 none (no object), 1 any Forge
places, else the group */
static int forge_file_kind_takes(
	int kind,
	unsigned int group_tag)
{
	switch (kind)
	{
	case _forge_record_object:
		return forge_file_group_allowed(group_tag);
	case _forge_record_squad:
		return group_tag == FORGE_GROUP_ACTOR_VARIANT;
	case _forge_record_weapon_spawn:
		return group_tag == FORGE_GROUP_WEAPON || group_tag == FORGE_GROUP_EQUIPMENT;
	case _forge_record_vehicle_spawn:
		return group_tag == FORGE_GROUP_VEHICLE;
	}
	return 0;
}

static int forge_file_kind_has_object(
	int kind)
{
	return kind == _forge_record_object || kind == _forge_record_squad || kind == _forge_record_weapon_spawn ||
		kind == _forge_record_vehicle_spawn;
}

static int forge_file_finite(
	float value)
{
	unsigned int bits;

	memcpy(&bits, &value, sizeof(bits));
	return (bits & 0x7F800000u) != 0x7F800000u;
}

/* a record's place and angles: every float finite, the place within the
world, the angles within +-pi */
static int forge_file_record_floats_check(
	struct forge_file_record const *record)
{
	int index;

	for (index = 0; index < 3; index++)
	{
		if (!forge_file_finite(record->position[index]) || !forge_file_finite(record->angles[index]) ||
			!(record->position[index] > -FORGE_FILE_MAXIMUM_COORDINATE &&
				record->position[index] < FORGE_FILE_MAXIMUM_COORDINATE) ||
			!(record->angles[index] >= -FORGE_FILE_ANGLE_LIMIT && record->angles[index] <= FORGE_FILE_ANGLE_LIMIT))
		{
			return 0;
		}
	}
	return 1;
}

static int forge_file_kind_teleporter(
	int kind)
{
	return kind == _forge_record_teleporter_in || kind == _forge_record_teleporter_out;
}

/* whether a record's own bytes are all zero */
static int forge_file_params_zero(
	struct forge_file_record const *record)
{
	int index;

	for (index = 0; index < FORGE_FILE_RECORD_PARAMS_SIZE; index++)
	{
		if (record->params[index])
			return 0;
	}
	return 1;
}

/* (version 16) an effect's or a decal's record: its path one of its group,
its yaw alone, nothing else of an object's; its own bytes its repeat (an
effect's, 0 to 60) or size (a decal's, 1 to 40), then zeros */
static int forge_file_effect_check(
	struct forge_file_layout const *layout,
	int version,
	struct forge_file_record const *record)
{
	int decal = record->kind == _forge_record_decal;
	int value = record->params[0];
	int index;

	if (version < FORGE_FILE_EFFECTS_VERSION || record->path_index >= layout->path_count ||
		layout->paths[record->path_index].group_tag != (decal ? FORGE_FILE_DECAL_GROUP : FORGE_FILE_EFFECT_GROUP) ||
		record->team || record->respawn_seconds || record->count || record->game_types || record->variant ||
		record->flags || record->ai_team || record->ai_behavior || record->ai_route || record->ai_options)
	{
		return 0;
	}
	if (decal ? value < 1 || value > FORGE_FILE_MAXIMUM_DECAL_SIZE : value > FORGE_FILE_MAXIMUM_EFFECT_REPEAT)
		return 0;
	for (index = 1; index < FORGE_FILE_RECORD_PARAMS_SIZE; index++)
	{
		if (record->params[index])
			return 0;
	}
	return forge_file_record_floats_check(record) && record->angles[1] == 0.f && record->angles[2] == 0.f;
}

/* (version 15) the sky and fog's record: its path a sky's when it sets the
sky, else none; nothing of an object's; its own bytes what it sets (one at
least), the fog's in range and starting before it is whole when it sets
the fog, else zeros; then zeros */
static int forge_file_environment_check(
	struct forge_file_layout const *layout,
	int version,
	struct forge_file_record const *record)
{
	unsigned char const *params = record->params;
	int sets = params[FORGE_FILE_ENVIRONMENT_PARAM_SETS];
	int index;

	if (version < FORGE_FILE_ENVIRONMENT_VERSION || record->team || record->respawn_seconds || record->count || record->game_types ||
		record->variant || record->flags || record->ai_team || record->ai_behavior ||
		record->ai_route || record->ai_options)
	{
		return 0;
	}
	for (index = 0; index < 3; index++)
	{
		if (record->position[index] != 0.f || record->angles[index] != 0.f)
			return 0;
	}
	if (!sets || (sets & ~FORGE_FILE_ENVIRONMENT_KNOWN))
		return 0;
	if (sets & FORGE_FILE_ENVIRONMENT_SKY)
	{
		if (record->path_index >= layout->path_count || layout->paths[record->path_index].group_tag != FORGE_FILE_SKY_GROUP)
			return 0;
	}
	else if (record->path_index != FORGE_FILE_NO_PATH)
	{
		return 0;
	}
	if (sets & FORGE_FILE_ENVIRONMENT_FOG)
	{
		int density = params[FORGE_FILE_ENVIRONMENT_PARAM_DENSITY];
		int start = params[FORGE_FILE_ENVIRONMENT_PARAM_START];
		int opaque = params[FORGE_FILE_ENVIRONMENT_PARAM_OPAQUE];

		if (density < 1 || density > FORGE_FILE_MAXIMUM_FOG_DENSITY || start > FORGE_FILE_MAXIMUM_FOG_START || opaque < 1 ||
			(float)start >= (float)opaque * FORGE_FILE_FOG_OPAQUE_UNIT)
		{
			return 0;
		}
	}
	else
	{
		for (index = FORGE_FILE_ENVIRONMENT_PARAM_COLOR; index <= FORGE_FILE_ENVIRONMENT_PARAM_OPAQUE; index++)
		{
			if (params[index])
				return 0;
		}
	}
	for (index = FORGE_FILE_ENVIRONMENT_PARAM_OPAQUE + 1; index < FORGE_FILE_RECORD_PARAMS_SIZE; index++)
	{
		if (params[index])
			return 0;
	}
	return 1;
}

/* (version 14) a volume's record: no path, its team -1 to 1, a type, its
yaw alone, nothing else of an object's; its own bytes a shape, a size in
range (a sphere's radius alone), an amount its type takes, a target, then
zeros */
static int forge_file_volume_check(
	int version,
	struct forge_file_record const *record)
{
	unsigned char const *params = record->params;
	int shape = params[FORGE_FILE_VOLUME_PARAM_SHAPE];
	int amount = params[FORGE_FILE_VOLUME_PARAM_AMOUNT];
	int axis;

	if (version < FORGE_FILE_VOLUMES_VERSION || record->path_index != FORGE_FILE_NO_PATH || record->team < -1 || record->team > 1 ||
		record->variant >= NUMBER_OF_FORGE_FILE_VOLUME_TYPES || record->respawn_seconds || record->count ||
		record->game_types || record->flags || record->ai_team || record->ai_behavior || record->ai_route ||
		record->ai_options)
	{
		return 0;
	}
	if (shape >= NUMBER_OF_FORGE_FILE_VOLUME_SHAPES || params[FORGE_FILE_VOLUME_PARAM_TARGET] >= NUMBER_OF_FORGE_FILE_VOLUME_TARGETS)
		return 0;
	/* (a team's: its players only) */
	if (record->team != -1 && params[FORGE_FILE_VOLUME_PARAM_TARGET] != FORGE_FILE_VOLUME_PLAYERS)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		int size = params[FORGE_FILE_VOLUME_PARAM_SIZE + axis];

		if (shape == FORGE_FILE_VOLUME_SPHERE && axis > 0 ? size != 0 : size == 0)
			return 0;
	}
	for (axis = FORGE_FILE_VOLUME_PARAM_TARGET + 1; axis < FORGE_FILE_RECORD_PARAMS_SIZE; axis++)
	{
		if (params[axis])
			return 0;
	}
	if (record->variant == FORGE_FILE_VOLUME_KILL ? amount != 0 :
		record->variant == FORGE_FILE_VOLUME_DAMAGE ? amount < 1 || amount > FORGE_FILE_MAXIMUM_VOLUME_DAMAGE :
		amount > FORGE_FILE_MAXIMUM_VOLUME_GRAVITY)
	{
		return 0;
	}
	return forge_file_record_floats_check(record) && record->angles[1] == 0.f && record->angles[2] == 0.f;
}

static int forge_file_kind_ai_marker(
	int kind)
{
	return kind == _forge_record_ai_route_point || kind == _forge_record_ai_area;
}

static int forge_file_record_check(
	struct forge_file_layout const *layout,
	int version,
	struct forge_file_record const *record)
{
	int character;
	int spawn;

	/* (version 16's effects and decals: their own checks) */
	if (record->kind == _forge_record_effect || record->kind == _forge_record_decal)
		return forge_file_effect_check(layout, version, record);
	/* (version 15's sky and fog: its own checks) */
	if (record->kind == _forge_record_environment)
		return forge_file_environment_check(layout, version, record);
	/* (version 14's volume: its own checks) */
	if (record->kind == _forge_record_volume)
		return forge_file_volume_check(version, record);
	if (record->kind >= NUMBER_OF_FORGE_RECORD_KINDS && !forge_file_kind_ai_marker(record->kind))
		return 0;
	/* (version 12's route point and patrol area: no path, their route in
	the team byte, a point's stop or an area's size in the variant, the yaw
	alone, nothing else; none before version 12) */
	if (forge_file_kind_ai_marker(record->kind))
	{
		if (version < 12 || record->path_index != FORGE_FILE_NO_PATH || record->team < 0 ||
			record->team >= FORGE_FILE_MAXIMUM_ROUTES || record->respawn_seconds || record->count || record->game_types ||
			record->flags || record->ai_team || record->ai_behavior || record->ai_route || record->ai_options ||
			record->variant >= (record->kind == _forge_record_ai_route_point ? NUMBER_OF_FORGE_FILE_ROUTE_STOPS :
				FORGE_FILE_AI_AREA_SIZES))
		{
			return 0;
		}
		if (!forge_file_record_floats_check(record) || record->angles[1] != 0.f || record->angles[2] != 0.f ||
			!forge_file_params_zero(record))
		{
			return 0;
		}
		return 1;
	}
	/* (version 12's AI movement fields: an object's only, checked below
	once its group is known; none before version 12) */
	if (version < 12 && (record->ai_behavior || record->ai_route || record->ai_options))
		return 0;
	/* (no other kind has bytes of its own) */
	if (!forge_file_params_zero(record))
		return 0;
	/* (the kinds no build has written: a hill, a kill zone - refused in any
	version, as unknown kinds are) */
	if (record->kind == _forge_record_hill || record->kind == _forge_record_kill_zone)
		return 0;
	/* (version 6's game type object: no path, a netgame flag type of the
	list, its index below that type's limit, its yaw alone, nothing else;
	none before version 6) */
	if (record->kind == _forge_record_flag)
	{
		if (version < 6 || record->path_index != FORGE_FILE_NO_PATH || record->team < 0 ||
			record->team >= forge_file_flag_type_indices(record->variant) || record->respawn_seconds || record->count ||
			record->game_types || record->flags || record->ai_team || record->ai_behavior || record->ai_route ||
			record->ai_options)
		{
			return 0;
		}
		if (!forge_file_record_floats_check(record) || record->angles[1] != 0.f || record->angles[2] != 0.f)
			return 0;
		return 1;
	}
	/* (version 5's teleporter: no path, its channel in the team byte, its
	yaw alone, nothing else of an object's; none before version 5) */
	if (forge_file_kind_teleporter(record->kind))
	{
		if (version < 5 || record->path_index != FORGE_FILE_NO_PATH || record->team < 0 ||
			record->team >= FORGE_FILE_MAXIMUM_TELEPORTER_CHANNELS || record->respawn_seconds || record->count ||
			record->game_types || record->variant || record->flags || record->ai_team || record->ai_behavior ||
			record->ai_route || record->ai_options)
		{
			return 0;
		}
		if (!forge_file_record_floats_check(record) || record->angles[1] != 0.f || record->angles[2] != 0.f)
			return 0;
		return 1;
	}
	if (forge_file_kind_has_object(record->kind))
	{
		if (record->path_index >= layout->path_count ||
			!forge_file_kind_takes(record->kind, layout->paths[record->path_index].group_tag))
		{
			return 0;
		}
	}
	else if (record->path_index != FORGE_FILE_NO_PATH)
	{
		return 0;
	}
	if (record->team < -1 || record->team > FORGE_FILE_MAXIMUM_TEAM)
		return 0;
	if (record->respawn_seconds > FORGE_FILE_MAXIMUM_RESPAWN_SECONDS || record->count > FORGE_FILE_MAXIMUM_COUNT ||
		(record->kind == _forge_record_object && record->count > 1))
	{
		return 0;
	}
	/* (version 4's player spawn: its team any, red or blue, its game types
	infection's too, an initial spawn's flag alone, nothing else of an
	object's) */
	spawn = version >= 4 && record->kind == _forge_record_player_spawn;
	if (spawn && (record->team > FORGE_FILE_MAXIMUM_SPAWN_TEAM || record->respawn_seconds || record->count ||
		record->variant || record->ai_team || (record->flags & ~FORGE_RECORD_SPAWN_INITIAL) ||
		(record->game_types & ~FORGE_FILE_SPAWN_GAME_TYPES_MASK)))
	{
		return 0;
	}
	if ((!spawn && (record->game_types & ~FORGE_FILE_GAME_TYPES_MASK)) || record->variant > FORGE_FILE_MAXIMUM_VARIANT ||
		(record->flags & ~(version >= 7 ? FORGE_RECORD_FLAGS_KNOWN : version >= 4 ? FORGE_RECORD_FLAGS_VERSION_4 :
			version == 3 ? FORGE_RECORD_FLAGS_VERSION_3 :
			version == 2 ? FORGE_RECORD_FLAGS_VERSION_2 : FORGE_RECORD_FLAGS_VERSION_1)) ||
		(!spawn && (record->flags & FORGE_RECORD_SPAWN_INITIAL)))
	{
		return 0;
	}
	/* (version 2's AI settings: a character's only, its team one of the
	known; none in version 1) */
	character = forge_file_kind_has_object(record->kind) &&
		layout->paths[record->path_index].group_tag == FORGE_GROUP_ACTOR_VARIANT;
	if (version < 2 ? record->ai_team != 0 :
		record->ai_team >= NUMBER_OF_FORGE_AI_TEAMS || (!character && (record->ai_team || (record->flags & FORGE_RECORD_AI_OFF))))
	{
		return 0;
	}
	/* (version 12's AI movement: a character's behavior, route and options,
	a vehicle's route alone; nothing on anything else) */
	if (record->ai_behavior || record->ai_route || record->ai_options)
	{
		int vehicle = forge_file_kind_has_object(record->kind) && record->kind == _forge_record_object &&
			layout->paths[record->path_index].group_tag == FORGE_GROUP_VEHICLE;

		if (record->ai_behavior >= NUMBER_OF_FORGE_FILE_AI_BEHAVIORS || record->ai_route > FORGE_FILE_MAXIMUM_ROUTES ||
			(record->ai_options & ~FORGE_FILE_AI_OPTIONS_KNOWN) || (!character && !vehicle) ||
			(vehicle && (record->ai_behavior || record->ai_options)))
		{
			return 0;
		}
	}
	/* (version 3's respawn: a character's only, after a second or more) */
	if ((record->flags & FORGE_RECORD_AI_RESPAWN) && (!character || record->respawn_seconds == 0))
		return 0;
	/* (version 7's physics: an object's that is no character's, fixed or
	phased, never both) */
	if ((record->flags & FORGE_RECORD_PHYSICS_MASK) && (record->kind != _forge_record_object || character ||
		(record->flags & FORGE_RECORD_PHYSICS_MASK) == FORGE_RECORD_PHYSICS_MASK))
	{
		return 0;
	}
	return forge_file_record_floats_check(record);
}

int forge_file_flag_type_indices(
	int flag_type)
{
	switch (flag_type)
	{
	case FORGE_FILE_FLAG_TYPE_CTF: return FORGE_FILE_MAXIMUM_CTF_TEAMS;
	case FORGE_FILE_FLAG_TYPE_ODDBALL: return FORGE_FILE_MAXIMUM_ODDBALLS;
	case FORGE_FILE_FLAG_TYPE_RACE: return FORGE_FILE_MAXIMUM_RACE_CHECKPOINTS;
	case FORGE_FILE_FLAG_TYPE_HILL: return FORGE_FILE_MAXIMUM_HILLS;
	}
	return 0;
}

/* the meta's texts each within its room, terminated (the struct's own
arrays: a layout to be written may hold anything) */
static int forge_file_meta_terminated(
	struct forge_file_layout const *layout)
{
	return memchr(layout->title, 0, sizeof(layout->title)) && memchr(layout->author, 0, sizeof(layout->author)) &&
		memchr(layout->description, 0, sizeof(layout->description)) && memchr(layout->date, 0, sizeof(layout->date)) &&
		memchr(layout->map_name, 0, sizeof(layout->map_name));
}

int forge_file_layout_version(
	struct forge_file_layout const *layout)
{
	int index;

	int version = FORGE_FILE_FIRST_VERSION;

	if (layout->version)
		return layout->version;
	if (layout->flags & FORGE_FILE_FLAG_CANVAS)
		version = 11;
	/* (the highest any record needs) */
	for (index = 0; index < layout->record_count && index < FORGE_FILE_MAXIMUM_RECORDS; index++)
	{
		struct forge_file_record const *record = &layout->records[index];
		int needs = FORGE_FILE_FIRST_VERSION;

		if (record->kind == _forge_record_effect || record->kind == _forge_record_decal)
			needs = FORGE_FILE_EFFECTS_VERSION;
		else if (record->kind == _forge_record_environment)
			needs = FORGE_FILE_ENVIRONMENT_VERSION;
		else if (record->kind == _forge_record_volume)
			needs = FORGE_FILE_VOLUMES_VERSION;
		else if (forge_file_kind_ai_marker(record->kind) || record->ai_behavior || record->ai_route || record->ai_options)
			needs = 12;
		else if (record->flags & FORGE_RECORD_PHYSICS_MASK)
			needs = 7;
		else if (record->kind == _forge_record_flag)
			needs = 6;
		else if (forge_file_kind_teleporter(record->kind))
			needs = 5;
		else if (record->kind == _forge_record_player_spawn)
			needs = 4;
		else if (record->flags & FORGE_RECORD_AI_RESPAWN)
			needs = 3;
		else if (record->ai_team || (record->flags & FORGE_RECORD_AI_OFF))
			needs = 2;
		if (needs > version)
			version = needs;
	}
	/* (version 13: more records than the versions before it hold) */
	if (layout->record_count > FORGE_FILE_EARLIER_MAXIMUM_RECORDS && version < FORGE_FILE_MORE_RECORDS_VERSION)
		version = FORGE_FILE_MORE_RECORDS_VERSION;
	return version;
}

int forge_file_layout_check(
	struct forge_file_layout const *layout,
	char const *expected_map)
{
	int index;
	int other;
	int netgame = 0;
	int respawning = 0;
	int player_spawns = 0;
	int teleporters = 0;
	int game_objects = 0;
	int physics = 0;
	int ai_movement = 0;
	int route_points = 0;
	int ai_areas = 0;
	int volumes = 0;
	int environments = 0;
	int effects = 0;
	int decals = 0;
	unsigned char hill_points[FORGE_FILE_MAXIMUM_HILLS];
	unsigned int sender_channels = 0;
	unsigned int receiver_channels = 0;
	int version;

	if (!layout || !forge_file_meta_terminated(layout))
		return _forge_file_bad_meta;
	if (layout->version && (layout->version < FORGE_FILE_FIRST_VERSION || layout->version > FORGE_FILE_VERSION ||
		(layout->version >= FORGE_FILE_FIRST_SKIPPED_VERSION && layout->version <= FORGE_FILE_LAST_SKIPPED_VERSION)))
	{
		return _forge_file_bad_version;
	}
	if (layout->flags & ~FORGE_FILE_FLAGS_KNOWN)
		return _forge_file_bad_flags;
	if (!forge_file_map_name_valid(layout->map_name))
		return _forge_file_bad_map_name;
	if (expected_map && strcmp(layout->map_name, expected_map))
		return _forge_file_wrong_map;
	if (layout->pack_count < 0 || layout->pack_count > FORGE_FILE_MAXIMUM_PACKS || layout->path_count < 0 ||
		layout->path_count > FORGE_FILE_MAXIMUM_PATHS || layout->record_count < 0 ||
		layout->record_count > FORGE_FILE_MAXIMUM_RECORDS ||
		(layout->version && layout->version < FORGE_FILE_MORE_RECORDS_VERSION &&
		layout->record_count > FORGE_FILE_EARLIER_MAXIMUM_RECORDS))
	{
		return _forge_file_too_many;
	}
	for (index = 0; index < layout->pack_count; index++)
	{
		if (!memchr(layout->packs[index].name, 0, sizeof(layout->packs[index].name)) ||
			!forge_file_pack_name_valid(layout->packs[index].name))
		{
			return _forge_file_bad_pack;
		}
	}
	for (index = 0; index < layout->path_count; index++)
	{
		struct forge_file_path const *path = &layout->paths[index];
		char const *end = memchr(path->path, 0, sizeof(path->path));

		if (!end || !forge_file_path_valid(path->path, (int)(end - path->path)))
			return _forge_file_bad_path;
		if (!forge_file_path_group_allowed(path->group_tag))
			return _forge_file_bad_group;
		for (other = 0; other < index; other++)
		{
			if (layout->paths[other].group_tag == path->group_tag && !strcmp(layout->paths[other].path, path->path))
				return _forge_file_duplicate_path;
		}
	}
	version = forge_file_layout_version(layout);
	/* (the map's own objects: version 11 on, taking something away
	or keeping something, its entries in range and ascending; none, zeros) */
	if (layout->flags & FORGE_FILE_FLAG_CANVAS)
	{
		struct forge_file_canvas const *canvas = &layout->canvas;

		if (version < 11)
			return _forge_file_bad_flags;
		if ((canvas->categories & ~FORGE_FILE_CANVAS_CATEGORIES_MASK) || canvas->entry_count < 0 ||
			canvas->entry_count > FORGE_FILE_MAXIMUM_CANVAS_ENTRIES || (!canvas->categories && !canvas->entry_count))
		{
			return _forge_file_bad_record;
		}
		for (index = 0; index < canvas->entry_count; index++)
		{
			struct forge_file_canvas_entry const *entry = &canvas->entries[index];

			if (entry->category >= NUMBER_OF_FORGE_CANVAS_CATEGORIES || entry->index > FORGE_FILE_CANVAS_MAXIMUM_INDEX ||
				entry->kept != ((canvas->categories >> entry->category) & 1))
			{
				return _forge_file_bad_record;
			}
			if (index > 0 && (entry[-1].category > entry->category ||
				(entry[-1].category == entry->category && entry[-1].index >= entry->index)))
			{
				return _forge_file_bad_record;
			}
		}
	}
	else if (layout->canvas.categories || layout->canvas.entry_count)
	{
		return _forge_file_bad_record;
	}
	/* (a sky's path: version 15's alone) */
	for (index = 0; index < layout->path_count; index++)
	{
		if (layout->paths[index].group_tag == FORGE_FILE_SKY_GROUP && version < FORGE_FILE_ENVIRONMENT_VERSION)
			return _forge_file_bad_group;
	}
	/* (an effect's or a decal's path: version 16's alone) */
	for (index = 0; index < layout->path_count; index++)
	{
		if ((layout->paths[index].group_tag == FORGE_FILE_EFFECT_GROUP ||
			layout->paths[index].group_tag == FORGE_FILE_DECAL_GROUP) && version < FORGE_FILE_EFFECTS_VERSION)
		{
			return _forge_file_bad_group;
		}
	}
	for (index = 0; index < layout->record_count; index++)
	{
		struct forge_file_record const *record = &layout->records[index];

		if (!forge_file_record_check(layout, version, record))
			return _forge_file_bad_record;
		if (layout->records[index].kind != _forge_record_object)
			netgame = 1;
		if (layout->records[index].flags & FORGE_RECORD_AI_RESPAWN)
			respawning = 1;
		if (layout->records[index].flags & FORGE_RECORD_PHYSICS_MASK)
			physics = 1;
		if (layout->records[index].kind == _forge_record_player_spawn)
			player_spawns = 1;
		/* (version 12's AI movement: the route points and patrol areas
		counted) */
		if (forge_file_kind_ai_marker(layout->records[index].kind) || layout->records[index].ai_behavior ||
			layout->records[index].ai_route || layout->records[index].ai_options)
		{
			ai_movement = 1;
		}
		if (layout->records[index].kind == _forge_record_ai_route_point)
			route_points++;
		if (layout->records[index].kind == _forge_record_ai_area)
			ai_areas++;
		if (layout->records[index].kind == _forge_record_volume)
			volumes++;
		if (layout->records[index].kind == _forge_record_environment)
			environments++;
		if (layout->records[index].kind == _forge_record_effect)
			effects++;
		if (layout->records[index].kind == _forge_record_decal)
			decals++;
		/* (version 6's game type objects: a hill's points counted, its
		number checked in range with the record) */
		if (layout->records[index].kind == _forge_record_flag)
		{
			if (game_objects == 0)
				memset(hill_points, 0, sizeof(hill_points));
			game_objects++;
			if (layout->records[index].variant == FORGE_FILE_FLAG_TYPE_HILL &&
				++hill_points[layout->records[index].team] > FORGE_FILE_MAXIMUM_HILL_POINTS)
			{
				return _forge_file_too_many;
			}
		}
		/* (version 5's teleporters: their channels, checked in range with
		the record) */
		if (forge_file_kind_teleporter(layout->records[index].kind))
		{
			teleporters++;
			if (layout->records[index].kind == _forge_record_teleporter_in)
				sender_channels |= 1u << layout->records[index].team;
			else
				receiver_channels |= 1u << layout->records[index].team;
		}
	}
	/* (each version is written only for what it adds - version 3 for a
	respawning character, 4 for a player spawn, 5 for a teleporter, 6 for a
	game type object, 7 for an object's physics, 11 for the map's own
	objects, 12 for AI movement, 13 for more than 256 records, 14 for a
	volume, 15 for a sky and fog, 16 for an effect or a decal: a file of
	one without it is not one a writer made) */
	if ((version == 3 && !respawning) || (version == 4 && !player_spawns) || (version == 5 && !teleporters) ||
		(version == 6 && !game_objects) || (version == 7 && !physics) ||
		(version == 11 && !(layout->flags & FORGE_FILE_FLAG_CANVAS)) || (version == 12 && !ai_movement) ||
		(version == FORGE_FILE_MORE_RECORDS_VERSION && layout->record_count <= FORGE_FILE_EARLIER_MAXIMUM_RECORDS) ||
		(version == FORGE_FILE_VOLUMES_VERSION && !volumes) ||
		(version == FORGE_FILE_ENVIRONMENT_VERSION && !environments) ||
		(version == FORGE_FILE_EFFECTS_VERSION && !effects && !decals))
	{
		return _forge_file_bad_version;
	}
	if (volumes > FORGE_FILE_MAXIMUM_VOLUMES)
		return _forge_file_too_many;
	if (environments > 1)
		return _forge_file_too_many;
	if (effects > FORGE_FILE_MAXIMUM_EFFECTS || decals > FORGE_FILE_MAXIMUM_DECALS)
		return _forge_file_too_many;
	if (route_points > FORGE_FILE_MAXIMUM_ROUTE_POINTS || ai_areas > FORGE_FILE_MAXIMUM_AI_AREAS)
		return _forge_file_too_many;
	if (game_objects > FORGE_FILE_MAXIMUM_GAME_OBJECTS)
		return _forge_file_too_many;
	/* (the teleporters within their most, each sender's channel with a
	receiver: a sender leads somewhere) */
	if (teleporters > FORGE_FILE_MAXIMUM_TELEPORTERS)
		return _forge_file_too_many;
	if (sender_channels & ~receiver_channels)
		return _forge_file_bad_record;
	/* (the flag says whether any record is a netgame's) */
	if (((layout->flags & FORGE_FILE_FLAG_NETGAME_SPAWNS) != 0) != netgame)
		return _forge_file_bad_flags;
	if (!forge_file_text_valid(layout->title, FORGE_FILE_TITLE_SIZE) || !layout->title[0] ||
		!forge_file_text_valid(layout->author, FORGE_FILE_AUTHOR_SIZE) ||
		!forge_file_text_valid(layout->description, FORGE_FILE_DESCRIPTION_SIZE) ||
		!forge_file_date_valid(layout->date))
	{
		return _forge_file_bad_meta;
	}
	return _forge_file_ok;
}

/* the payload's size, from the counts and the paths' lengths */
static int forge_file_meta_size(
	struct forge_file_layout const *layout)
{
	return (int)(strlen(layout->title) + strlen(layout->author) + strlen(layout->description) + strlen(layout->date)) + 4;
}

static int forge_file_refused(
	int result,
	char *why,
	int why_size,
	char const *detail)
{
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "%s%s%s", forge_file_result_name(result), detail ? ": " : "", detail ? detail : "");
	return result;
}

static int forge_file_decode_checked(
	unsigned char const *bytes,
	int size,
	char const *expected_map,
	struct forge_file_layout *layout,
	char *why,
	int why_size)
{
	struct forge_file_reader reader;
	unsigned int version, header_size, payload_size, checksum, meta_size;
	int pack_count, path_count, record_count;
	int index;
	int result;
	char detail[64];

	if (!bytes || size < FORGE_FILE_HEADER_SIZE)
		return forge_file_refused(_forge_file_too_small, why, why_size, NULL);
	if (size > FORGE_FILE_MAXIMUM_SIZE)
		return forge_file_refused(_forge_file_too_large, why, why_size, NULL);
	reader.bytes = bytes;
	reader.size = size;
	reader.offset = 0;
	reader.failed = 0;
	if (memcmp(bytes, forge_file_magic, sizeof(forge_file_magic)))
		return forge_file_refused(_forge_file_bad_magic, why, why_size, NULL);
	reader.offset = 4;
	version = read_u16(&reader);
	header_size = read_u16(&reader);
	if (version < FORGE_FILE_FIRST_VERSION)
		return forge_file_refused(_forge_file_bad_version, why, why_size, NULL);
	if (version > FORGE_FILE_VERSION ||
		(version >= FORGE_FILE_FIRST_SKIPPED_VERSION && version <= FORGE_FILE_LAST_SKIPPED_VERSION))
	{
		snprintf(detail, sizeof(detail), "version %u", version);
		return forge_file_refused(_forge_file_newer_version, why, why_size, detail);
	}
	layout->version = (unsigned short)version;
	if (header_size != FORGE_FILE_HEADER_SIZE)
		return forge_file_refused(_forge_file_bad_header_size, why, why_size, NULL);
	layout->flags = read_u32(&reader);
	if (layout->flags & ~FORGE_FILE_FLAGS_KNOWN)
		return forge_file_refused(_forge_file_bad_flags, why, why_size, NULL);
	/* (the map's name, zeros after it) */
	memcpy(layout->map_name, bytes + reader.offset, FORGE_FILE_MAP_NAME_SIZE);
	{
		char const *zero = memchr(layout->map_name, 0, FORGE_FILE_MAP_NAME_SIZE);

		index = zero ? (int)(zero - layout->map_name) : FORGE_FILE_MAP_NAME_SIZE;
	}
	for (; index < FORGE_FILE_MAP_NAME_SIZE; index++)
	{
		if (layout->map_name[index])
			return forge_file_refused(_forge_file_bad_map_name, why, why_size, NULL);
	}
	if (!forge_file_map_name_valid(layout->map_name))
		return forge_file_refused(_forge_file_bad_map_name, why, why_size, NULL);
	if (expected_map && strcmp(layout->map_name, expected_map))
		return forge_file_refused(_forge_file_wrong_map, why, why_size, layout->map_name);
	reader.offset += FORGE_FILE_MAP_NAME_SIZE;
	layout->map_hash = read_u32(&reader);
	pack_count = (int)read_u16(&reader);
	path_count = (int)read_u16(&reader);
	record_count = (int)read_u16(&reader);
	meta_size = read_u16(&reader);
	payload_size = read_u32(&reader);
	checksum = read_u32(&reader);
	if (reader.failed || reader.offset != FORGE_FILE_HEADER_SIZE)
		return forge_file_refused(_forge_file_too_small, why, why_size, NULL);
	if (pack_count > FORGE_FILE_MAXIMUM_PACKS || path_count > FORGE_FILE_MAXIMUM_PATHS ||
		record_count > FORGE_FILE_MAXIMUM_RECORDS || meta_size > FORGE_FILE_MAXIMUM_META ||
		(version < FORGE_FILE_MORE_RECORDS_VERSION && record_count > FORGE_FILE_EARLIER_MAXIMUM_RECORDS))
	{
		return forge_file_refused(_forge_file_too_many, why, why_size, NULL);
	}
	if (payload_size != (unsigned int)(size - FORGE_FILE_HEADER_SIZE))
		return forge_file_refused(_forge_file_bad_payload_size, why, why_size, NULL);
	if (forge_file_crc32(0, bytes + FORGE_FILE_HEADER_SIZE, size - FORGE_FILE_HEADER_SIZE) != checksum)
		return forge_file_refused(_forge_file_bad_checksum, why, why_size, NULL);
	layout->pack_count = (short)pack_count;
	layout->path_count = (short)path_count;
	layout->record_count = (short)record_count;

	/* the packs */
	for (index = 0; index < pack_count; index++)
	{
		struct forge_file_pack *pack = &layout->packs[index];
		int length;
		int rest;

		pack->recipe[0] = read_u32(&reader);
		pack->recipe[1] = read_u32(&reader);
		if (reader.failed || size - reader.offset < FORGE_FILE_PACK_NAME_SIZE)
			return forge_file_refused(_forge_file_bad_payload_size, why, why_size, "the packs");
		memcpy(pack->name, bytes + reader.offset, FORGE_FILE_PACK_NAME_SIZE);
		pack->name[FORGE_FILE_PACK_NAME_SIZE] = 0;
		reader.offset += FORGE_FILE_PACK_NAME_SIZE;
		length = (int)strlen(pack->name);
		for (rest = length; rest < FORGE_FILE_PACK_NAME_SIZE; rest++)
		{
			if (pack->name[rest])
				return forge_file_refused(_forge_file_bad_pack, why, why_size, NULL);
		}
		if (!forge_file_pack_name_valid(pack->name))
			return forge_file_refused(_forge_file_bad_pack, why, why_size, NULL);
	}
	/* the paths */
	for (index = 0; index < path_count; index++)
	{
		struct forge_file_path *path = &layout->paths[index];
		int length;

		path->group_tag = read_u32(&reader);
		length = (int)read_u8(&reader);
		if (reader.failed || size - reader.offset < length)
			return forge_file_refused(_forge_file_bad_payload_size, why, why_size, "the paths");
		if (length < 1 || length > FORGE_FILE_PATH_MAXIMUM_LENGTH)
			return forge_file_refused(_forge_file_bad_path, why, why_size, NULL);
		memcpy(path->path, bytes + reader.offset, (size_t)length);
		path->path[length] = 0;
		reader.offset += length;
		if (!forge_file_path_valid(path->path, length))
			return forge_file_refused(_forge_file_bad_path, why, why_size, NULL);
		if (!forge_file_path_group_allowed(path->group_tag))
			return forge_file_refused(_forge_file_bad_group, why, why_size, NULL);
	}
	/* the records */
	if (size - reader.offset < record_count * FORGE_FILE_RECORD_SIZE)
		return forge_file_refused(_forge_file_bad_payload_size, why, why_size, "the records");
	for (index = 0; index < record_count; index++)
	{
		struct forge_file_record *record = &layout->records[index];
		int axis;
		int finite = 1;
		int reserved;

		record->path_index = (unsigned short)read_u16(&reader);
		record->kind = (unsigned char)read_u8(&reader);
		record->team = (signed char)(unsigned char)read_u8(&reader);
		for (axis = 0; axis < 3; axis++)
		{
			int one;

			record->position[axis] = read_f32(&reader, &one);
			finite &= one;
		}
		for (axis = 0; axis < 3; axis++)
		{
			int one;

			record->angles[axis] = read_f32(&reader, &one);
			finite &= one;
		}
		record->respawn_seconds = (unsigned short)read_u16(&reader);
		record->count = (unsigned char)read_u8(&reader);
		record->game_types = (unsigned short)read_u16(&reader);
		record->variant = (unsigned char)read_u8(&reader);
		record->flags = (unsigned char)read_u8(&reader);
		/* (version 1's first reserved byte: checked with the record) */
		record->ai_team = (unsigned char)read_u8(&reader);
		/* (version 12's: checked with the record) */
		record->ai_behavior = (unsigned char)read_u8(&reader);
		record->ai_route = (unsigned char)read_u8(&reader);
		record->ai_options = (unsigned char)read_u8(&reader);
		/* (the kind's own bytes: checked with the record) */
		for (reserved = 0; reserved < FORGE_FILE_RECORD_PARAMS_SIZE; reserved++)
			record->params[reserved] = (unsigned char)read_u8(&reader);

		if (reader.failed)
			return forge_file_refused(_forge_file_bad_payload_size, why, why_size, "the records");
		if (!finite)
		{
			snprintf(detail, sizeof(detail), "record %d", index);
			return forge_file_refused(_forge_file_bad_record, why, why_size, detail);
		}
	}
	/* (version 11) the map's own objects: its header, then its entries
	(their ranges and order checked with the layout) */
	if (layout->flags & FORGE_FILE_FLAG_CANVAS)
	{
		unsigned int block_version, zero, zero2, count;

		if (size - reader.offset < FORGE_FILE_CANVAS_HEADER_SIZE)
			return forge_file_refused(_forge_file_bad_payload_size, why, why_size, "the map's objects");
		block_version = read_u8(&reader);
		zero = read_u8(&reader);
		layout->canvas.categories = (unsigned short)read_u16(&reader);
		count = read_u16(&reader);
		zero2 = read_u16(&reader);
		if (block_version != FORGE_FILE_CANVAS_VERSION || zero || zero2 || count > FORGE_FILE_MAXIMUM_CANVAS_ENTRIES)
			return forge_file_refused(_forge_file_bad_record, why, why_size, "the map's objects");
		if (size - reader.offset < (int)count * FORGE_FILE_CANVAS_ENTRY_SIZE)
			return forge_file_refused(_forge_file_bad_payload_size, why, why_size, "the map's objects");
		layout->canvas.entry_count = (short)count;
		for (index = 0; index < (int)count; index++)
		{
			struct forge_file_canvas_entry *entry = &layout->canvas.entries[index];

			entry->category = (unsigned char)read_u8(&reader);
			entry->kept = (unsigned char)read_u8(&reader);
			entry->index = (unsigned short)read_u16(&reader);
		}
		if (reader.failed)
			return forge_file_refused(_forge_file_bad_payload_size, why, why_size, "the map's objects");
	}
	/* the meta: four texts, each ended by a zero, filling it exactly */
	if ((unsigned int)(size - reader.offset) != meta_size || meta_size < 4)
		return forge_file_refused(_forge_file_bad_payload_size, why, why_size, "the meta");
	{
		char *texts[4];
		int rooms[4];
		int text_index;
		int at = reader.offset;
		int end = reader.offset + (int)meta_size;

		texts[0] = layout->title;
		rooms[0] = FORGE_FILE_TITLE_SIZE;
		texts[1] = layout->author;
		rooms[1] = FORGE_FILE_AUTHOR_SIZE;
		texts[2] = layout->description;
		rooms[2] = FORGE_FILE_DESCRIPTION_SIZE;
		texts[3] = layout->date;
		rooms[3] = FORGE_FILE_DATE_SIZE;
		for (text_index = 0; text_index < 4; text_index++)
		{
			unsigned char const *zero = memchr(bytes + at, 0, (size_t)(end - at));
			int length;

			if (!zero)
				return forge_file_refused(_forge_file_bad_meta, why, why_size, "a text not ended");
			length = (int)(zero - (bytes + at));
			if (length >= rooms[text_index])
				return forge_file_refused(_forge_file_bad_meta, why, why_size, "a text too long");
			memcpy(texts[text_index], bytes + at, (size_t)length);
			texts[text_index][length] = 0;
			at += length + 1;
		}
		if (at != end)
			return forge_file_refused(_forge_file_bad_meta, why, why_size, "bytes after the texts");
	}
	/* the layout's own checks (each path once, the records' ranges, the
	texts) */
	result = forge_file_layout_check(layout, expected_map);
	if (result != _forge_file_ok)
		return forge_file_refused(result, why, why_size, NULL);
	if (why && why_size > 0)
		why[0] = 0;
	return _forge_file_ok;
}

int forge_file_decode(
	unsigned char const *bytes,
	int size,
	char const *expected_map,
	struct forge_file_layout *layout,
	char *why,
	int why_size)
{
	int result;

	if (!layout)
		return forge_file_refused(_forge_file_too_small, why, why_size, NULL);
	memset(layout, 0, sizeof(*layout));
	result = forge_file_decode_checked(bytes, size, expected_map, layout, why, why_size);
	/* (a refused file leaves an empty layout: nothing of it is used) */
	if (result != _forge_file_ok)
		memset(layout, 0, sizeof(*layout));
	return result;
}

int forge_file_encode(
	struct forge_file_layout const *layout,
	unsigned char *bytes,
	int capacity,
	int *result)
{
	struct forge_file_writer writer;
	int check = forge_file_layout_check(layout, NULL);
	int payload_start;
	int index;
	int meta_size;

	if (result)
		*result = check;
	if (check != _forge_file_ok)
		return -1;
	meta_size = forge_file_meta_size(layout);
	writer.bytes = bytes;
	writer.capacity = capacity < FORGE_FILE_MAXIMUM_SIZE ? capacity : FORGE_FILE_MAXIMUM_SIZE;
	writer.offset = 0;
	writer.failed = 0;
	/* the header, its payload size and checksum written last */
	write_bytes(&writer, 'H' | ('F' << 8) | ('R' << 16) | ((unsigned int)'G' << 24), 4);
	write_bytes(&writer, (unsigned int)forge_file_layout_version(layout), 2);
	write_bytes(&writer, FORGE_FILE_HEADER_SIZE, 2);
	write_bytes(&writer, layout->flags, 4);
	for (index = 0; index < FORGE_FILE_MAP_NAME_SIZE; index++)
		write_bytes(&writer, index < (int)strlen(layout->map_name) ? (unsigned char)layout->map_name[index] : 0, 1);
	write_bytes(&writer, layout->map_hash, 4);
	write_bytes(&writer, (unsigned int)layout->pack_count, 2);
	write_bytes(&writer, (unsigned int)layout->path_count, 2);
	write_bytes(&writer, (unsigned int)layout->record_count, 2);
	write_bytes(&writer, (unsigned int)meta_size, 2);
	write_bytes(&writer, 0, 4);
	write_bytes(&writer, 0, 4);
	payload_start = writer.offset;
	for (index = 0; index < layout->pack_count; index++)
	{
		struct forge_file_pack const *pack = &layout->packs[index];
		int character;
		int length = (int)strlen(pack->name);

		write_bytes(&writer, pack->recipe[0], 4);
		write_bytes(&writer, pack->recipe[1], 4);
		for (character = 0; character < FORGE_FILE_PACK_NAME_SIZE; character++)
			write_bytes(&writer, character < length ? (unsigned char)pack->name[character] : 0, 1);
	}
	for (index = 0; index < layout->path_count; index++)
	{
		struct forge_file_path const *path = &layout->paths[index];
		int length = (int)strlen(path->path);

		write_bytes(&writer, path->group_tag, 4);
		write_bytes(&writer, (unsigned int)length, 1);
		write_text(&writer, path->path, length);
	}
	for (index = 0; index < layout->record_count; index++)
	{
		struct forge_file_record const *record = &layout->records[index];
		int axis;

		write_bytes(&writer, record->path_index, 2);
		write_bytes(&writer, record->kind, 1);
		write_bytes(&writer, (unsigned char)record->team, 1);
		for (axis = 0; axis < 3; axis++)
			write_f32(&writer, record->position[axis]);
		for (axis = 0; axis < 3; axis++)
			write_f32(&writer, record->angles[axis]);
		write_bytes(&writer, record->respawn_seconds, 2);
		write_bytes(&writer, record->count, 1);
		write_bytes(&writer, record->game_types, 2);
		write_bytes(&writer, record->variant, 1);
		write_bytes(&writer, record->flags, 1);
		write_bytes(&writer, record->ai_team, 1);
		write_bytes(&writer, record->ai_behavior, 1);
		write_bytes(&writer, record->ai_route, 1);
		write_bytes(&writer, record->ai_options, 1);
		for (axis = 0; axis < FORGE_FILE_RECORD_PARAMS_SIZE; axis++)
			write_bytes(&writer, record->params[axis], 1);
	}
	/* (version 11) the map's own objects */
	if (layout->flags & FORGE_FILE_FLAG_CANVAS)
	{
		write_bytes(&writer, FORGE_FILE_CANVAS_VERSION, 1);
		write_bytes(&writer, 0, 1);
		write_bytes(&writer, layout->canvas.categories, 2);
		write_bytes(&writer, (unsigned int)layout->canvas.entry_count, 2);
		write_bytes(&writer, 0, 2);
		for (index = 0; index < layout->canvas.entry_count; index++)
		{
			write_bytes(&writer, layout->canvas.entries[index].category, 1);
			write_bytes(&writer, layout->canvas.entries[index].kept, 1);
			write_bytes(&writer, layout->canvas.entries[index].index, 2);
		}
	}
	write_text(&writer, layout->title, (int)strlen(layout->title) + 1);
	write_text(&writer, layout->author, (int)strlen(layout->author) + 1);
	write_text(&writer, layout->description, (int)strlen(layout->description) + 1);
	write_text(&writer, layout->date, (int)strlen(layout->date) + 1);
	if (writer.failed)
	{
		if (result)
			*result = _forge_file_no_room;
		return -1;
	}
	{
		int end = writer.offset;
		unsigned int crc = forge_file_crc32(0, bytes + payload_start, end - payload_start);

		writer.offset = 56;
		write_bytes(&writer, (unsigned int)(end - payload_start), 4);
		write_bytes(&writer, crc, 4);
		return end;
	}
}
