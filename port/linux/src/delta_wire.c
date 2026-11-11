/*
DELTA_WIRE.C

Delta Peer's wire format (delta_wire.h). Plain C89, no allocation, no
platform dependency.
*/

#include "delta_wire.h"

/* ---------- little-endian fields */

static unsigned short read_u16(const unsigned char *data)
{
	return (unsigned short)(data[0] | (data[1] << 8));
}

static delta_u32 read_u32(const unsigned char *data)
{
	return (delta_u32)data[0] | ((delta_u32)data[1] << 8) | ((delta_u32)data[2] << 16) | ((delta_u32)data[3] << 24);
}

static void write_u16(unsigned char *data, unsigned int value)
{
	data[0] = (unsigned char)(value & 0xFF);
	data[1] = (unsigned char)((value >> 8) & 0xFF);
}

static void write_u32(unsigned char *data, delta_u32 value)
{
	data[0] = (unsigned char)(value & 0xFF);
	data[1] = (unsigned char)((value >> 8) & 0xFF);
	data[2] = (unsigned char)((value >> 16) & 0xFF);
	data[3] = (unsigned char)((value >> 24) & 0xFF);
}

static void clear(void *destination, int size)
{
	unsigned char *bytes = (unsigned char *)destination;
	int index;

	for (index = 0; index < size; index++)
		bytes[index] = 0;
}

static void copy(void *destination, const void *source, int size)
{
	unsigned char *to = (unsigned char *)destination;
	const unsigned char *from = (const unsigned char *)source;
	int index;

	for (index = 0; index < size; index++)
		to[index] = from[index];
}

/* length bytes of text into build (DELTA_WIRE_BUILD_SIZE + 1), printable
ASCII kept and the rest '?' */
static void read_text(const unsigned char *data, int length, char *build)
{
	int index;

	for (index = 0; index < length && index < DELTA_WIRE_BUILD_SIZE; index++)
		build[index] = data[index] >= 0x20 && data[index] <= 0x7E ? (char)data[index] : '?';
	build[index] = 0;
}

/* the length of build as sent: up to its end, at most DELTA_WIRE_BUILD_SIZE */
static int text_length(const char *build)
{
	int length = 0;

	while (length < DELTA_WIRE_BUILD_SIZE && build[length])
		length++;
	return length;
}

static void write_text(unsigned char *data, const char *build, int length)
{
	int index;

	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)build[index];

		data[index] = character >= 0x20 && character <= 0x7E ? character : (unsigned char)'?';
	}
}

/* ---------- the header */

int delta_wire_read_header(const unsigned char *data, int size, struct delta_wire_header *header)
{
	if (!data || size < DELTA_WIRE_HEADER_SIZE || size > DELTA_WIRE_MAXIMUM_DATAGRAM)
		return 0;
	if (data[0] != DELTA_WIRE_MAGIC_0 || data[1] != DELTA_WIRE_MAGIC_1)
		return 0;
	header->major = data[2];
	header->type = data[3];
	header->length = read_u16(data + 4);
	/* (bytes 6 and 7 are reserved: sent as zeros, never read) */
	header->session = read_u32(data + 8);
	return header->length == size - DELTA_WIRE_HEADER_SIZE;
}

void delta_wire_write_header(unsigned char *data, int major, int type, int length, delta_u32 session)
{
	data[0] = DELTA_WIRE_MAGIC_0;
	data[1] = DELTA_WIRE_MAGIC_1;
	data[2] = (unsigned char)major;
	data[3] = (unsigned char)type;
	write_u16(data + 4, (unsigned int)length);
	write_u16(data + 6, 0);
	write_u32(data + 8, session);
}

int delta_wire_write_empty(unsigned char *data, int major, int type, delta_u32 session)
{
	delta_wire_write_header(data, major, type, 0, session);
	return DELTA_WIRE_HEADER_SIZE;
}

/* ---------- the platform key */

void delta_wire_read_key(const unsigned char *data, struct delta_platform_key *key)
{
	key->platform = data[0];
	key->version = data[1];
	key->flags = data[2];
	key->host_players = data[3];
	key->join_players = data[4];
	key->join_players_opt_in = data[5];
	key->memory_class = data[6];
	key->reserved = 0;
	/* (an opt-in limit under the default one means the default) */
	if (key->join_players_opt_in < key->join_players)
		key->join_players_opt_in = key->join_players;
}

void delta_wire_write_key(unsigned char *data, const struct delta_platform_key *key)
{
	data[0] = key->platform;
	data[1] = key->version;
	data[2] = key->flags;
	data[3] = key->host_players;
	data[4] = key->join_players;
	data[5] = key->join_players_opt_in;
	data[6] = key->memory_class;
	data[7] = 0;
}

/* (one policy row a platform of the registry: none missed) */
#define DELTA_POLICY_COUNT(row_platform, host, join, join_opt_in, memory, coop) +1
typedef char delta_platform_policy_check[(0 DELTA_PLATFORM_POLICY(DELTA_POLICY_COUNT)) == NUMBER_OF_DELTA_PLATFORMS ?
	1 : -1];
#undef DELTA_POLICY_COUNT

void delta_platform_policy_default(int platform, struct delta_platform_key *key)
{
#define DELTA_POLICY_ROW(row_platform, host, join, join_opt_in, memory, coop) \
	{ row_platform, DELTA_PLATFORM_KEY_VERSION, 0, host, join, join_opt_in, memory, 0 },
	static const struct delta_platform_key rows[NUMBER_OF_DELTA_PLATFORMS] = {
		DELTA_PLATFORM_POLICY(DELTA_POLICY_ROW)
	};
#undef DELTA_POLICY_ROW

	*key = rows[platform >= 0 && platform < NUMBER_OF_DELTA_PLATFORMS ? platform : 0];
	key->platform = (unsigned char)(platform >= 0 && platform < 256 ? platform : 0);
}

int delta_platform_policy_coop(int platform)
{
#define DELTA_POLICY_COOP(row_platform, host, join, join_opt_in, memory, coop) coop,
	static const unsigned char rows[NUMBER_OF_DELTA_PLATFORMS] = {
		DELTA_PLATFORM_POLICY(DELTA_POLICY_COOP)
	};
#undef DELTA_POLICY_COOP

	return rows[platform >= 0 && platform < NUMBER_OF_DELTA_PLATFORMS ? platform : 0];
}

/* ---------- HELLO
	0  capabilities (4)
	4  legacy version (2)
	6  machine index (1)
	7  build length (1, at most 31)
	8  legacy table serial (4)
	12 platform key (8)
	20 build (the length's bytes) */

int delta_wire_read_hello(const unsigned char *payload, int size, struct delta_wire_hello *hello)
{
	int length;

	clear(hello, (int)sizeof(*hello));
	if (!payload || size < DELTA_WIRE_HELLO_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	length = payload[7];
	if (length > DELTA_WIRE_BUILD_SIZE || size < DELTA_WIRE_HELLO_SIZE + length)
		return 0;
	hello->capabilities = read_u32(payload);
	hello->legacy_version = read_u16(payload + 4);
	hello->machine_index = payload[6];
	hello->legacy_table_serial = read_u32(payload + 8);
	delta_wire_read_key(payload + 12, &hello->key);
	read_text(payload + DELTA_WIRE_HELLO_SIZE, length, hello->build);
	return 1;
}

int delta_wire_write_hello(unsigned char *data, delta_u32 session, const struct delta_wire_hello *hello)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length = text_length(hello->build);

	write_u32(payload, hello->capabilities);
	write_u16(payload + 4, hello->legacy_version);
	payload[6] = hello->machine_index;
	payload[7] = (unsigned char)length;
	write_u32(payload + 8, hello->legacy_table_serial);
	delta_wire_write_key(payload + 12, &hello->key);
	write_text(payload + DELTA_WIRE_HELLO_SIZE, hello->build, length);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_hello, DELTA_WIRE_HELLO_SIZE + length, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_HELLO_SIZE + length;
}

/* ---------- WELCOME
	0  the host's capabilities (4)
	4  agreed capabilities (4)
	8  legacy version (2)
	10 the host's machine index (1)
	11 build length (1, at most 31)
	12 legacy table serial (4)
	16 platform key (8)
	24 build */

int delta_wire_read_welcome(const unsigned char *payload, int size, struct delta_wire_welcome *welcome)
{
	int length;

	clear(welcome, (int)sizeof(*welcome));
	if (!payload || size < DELTA_WIRE_WELCOME_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	length = payload[11];
	if (length > DELTA_WIRE_BUILD_SIZE || size < DELTA_WIRE_WELCOME_SIZE + length)
		return 0;
	welcome->capabilities = read_u32(payload);
	welcome->agreed = read_u32(payload + 4);
	welcome->legacy_version = read_u16(payload + 8);
	welcome->host_machine_index = payload[10];
	welcome->legacy_table_serial = read_u32(payload + 12);
	delta_wire_read_key(payload + 16, &welcome->key);
	read_text(payload + DELTA_WIRE_WELCOME_SIZE, length, welcome->build);
	return 1;
}

int delta_wire_write_welcome(unsigned char *data, delta_u32 session, const struct delta_wire_welcome *welcome)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length = text_length(welcome->build);

	write_u32(payload, welcome->capabilities);
	write_u32(payload + 4, welcome->agreed);
	write_u16(payload + 8, welcome->legacy_version);
	payload[10] = welcome->host_machine_index;
	payload[11] = (unsigned char)length;
	write_u32(payload + 12, welcome->legacy_table_serial);
	delta_wire_write_key(payload + 16, &welcome->key);
	write_text(payload + DELTA_WIRE_WELCOME_SIZE, welcome->build, length);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_welcome, DELTA_WIRE_WELCOME_SIZE + length, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_WELCOME_SIZE + length;
}

/* ---------- ROSTER
	0  room capabilities (4)
	4  room's player limit (1)
	5  reserved (3)
	8  entry count (1, at most 32)
	9  reserved (3)
	12 entries, 36 bytes each:
	   0  machine index (1)
	   1  flags (1: DELTA_ROSTER_*)
	   2  reserved (2)
	   4  agreed capabilities (4)
	   8  platform key (8)
	   16 profile revision (4)
	   20 player ID (16)
	An entry's size is fixed for this major: a later one appends fields
	after the entries, not inside them. */

int delta_wire_read_roster(const unsigned char *payload, int size, struct delta_wire_roster *roster)
{
	int count;
	int index;

	clear(roster, (int)sizeof(*roster));
	if (!payload || size < DELTA_WIRE_ROSTER_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	count = payload[8];
	if (count > DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES ||
		size < DELTA_WIRE_ROSTER_SIZE + count * DELTA_WIRE_ROSTER_ENTRY_SIZE)
	{
		return 0;
	}
	roster->room_capabilities = read_u32(payload);
	roster->room_players = payload[4];
	roster->count = (unsigned char)count;
	for (index = 0; index < count; index++)
	{
		const unsigned char *entry = payload + DELTA_WIRE_ROSTER_SIZE + index * DELTA_WIRE_ROSTER_ENTRY_SIZE;
		struct delta_wire_roster_entry *out = &roster->entries[index];
		int byte;

		out->machine_index = entry[0];
		out->flags = entry[1];
		out->capabilities = read_u32(entry + 4);
		delta_wire_read_key(entry + 8, &out->key);
		out->profile_revision = read_u32(entry + 16);
		for (byte = 0; byte < DELTA_WIRE_PLAYER_ID_SIZE; byte++)
			out->player_id[byte] = entry[20 + byte];
	}
	return 1;
}

int delta_wire_write_roster(unsigned char *data, delta_u32 session, const struct delta_wire_roster *roster)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int count = roster->count;
	int index;

	if (count > DELTA_WIRE_MAXIMUM_ROSTER_ENTRIES)
		return 0;
	clear(payload, DELTA_WIRE_ROSTER_SIZE + count * DELTA_WIRE_ROSTER_ENTRY_SIZE);
	write_u32(payload, roster->room_capabilities);
	payload[4] = roster->room_players;
	payload[8] = (unsigned char)count;
	for (index = 0; index < count; index++)
	{
		unsigned char *entry = payload + DELTA_WIRE_ROSTER_SIZE + index * DELTA_WIRE_ROSTER_ENTRY_SIZE;
		const struct delta_wire_roster_entry *in = &roster->entries[index];
		int byte;

		entry[0] = in->machine_index;
		entry[1] = in->flags;
		write_u32(entry + 4, in->capabilities);
		delta_wire_write_key(entry + 8, &in->key);
		write_u32(entry + 16, in->profile_revision);
		for (byte = 0; byte < DELTA_WIRE_PLAYER_ID_SIZE; byte++)
			entry[20 + byte] = in->player_id[byte];
	}
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_roster,
		DELTA_WIRE_ROSTER_SIZE + count * DELTA_WIRE_ROSTER_ENTRY_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_ROSTER_SIZE + count * DELTA_WIRE_ROSTER_ENTRY_SIZE;
}

/* ---------- PROFILE
	0  profile revision (4)
	4  player ID (16) */

int delta_wire_read_profile(const unsigned char *payload, int size, struct delta_wire_profile *profile)
{
	int byte;

	clear(profile, (int)sizeof(*profile));
	if (!payload || size < DELTA_WIRE_PROFILE_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	profile->revision = read_u32(payload);
	for (byte = 0; byte < DELTA_WIRE_PLAYER_ID_SIZE; byte++)
		profile->player_id[byte] = payload[4 + byte];
	return 1;
}

int delta_wire_write_profile(unsigned char *data, delta_u32 session, const struct delta_wire_profile *profile)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int byte;

	write_u32(payload, profile->revision);
	for (byte = 0; byte < DELTA_WIRE_PLAYER_ID_SIZE; byte++)
		payload[4 + byte] = profile->player_id[byte];
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_profile, DELTA_WIRE_PROFILE_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_PROFILE_SIZE;
}

/* ---------- PARTY_TAG
	0  the party's tag (8; zeros: in no party) */

int delta_wire_read_party_tag(const unsigned char *payload, int size, unsigned char *tag)
{
	int byte;

	clear(tag, DELTA_WIRE_PARTY_TAG_SIZE);
	if (!payload || size < DELTA_WIRE_PARTY_TAG_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	for (byte = 0; byte < DELTA_WIRE_PARTY_TAG_SIZE; byte++)
		tag[byte] = payload[byte];
	return 1;
}

int delta_wire_write_party_tag(unsigned char *data, delta_u32 session, const unsigned char *tag)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int byte;

	for (byte = 0; byte < DELTA_WIRE_PARTY_TAG_SIZE; byte++)
		payload[byte] = tag ? tag[byte] : 0;
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_party_tag, DELTA_WIRE_PARTY_TAG_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_PARTY_TAG_SIZE;
}

/* ---------- TABLE
	0  serial (4)
	4  the signed table's whole size (4, at most DELTA_LEGACY_SIGNED_SIZE)
	8  offset (4, a multiple of 1024)
	12 length (2: 1024, or the rest of the table)
	14 reserved (2)
	16 the piece's bytes */

/* the length of the piece at offset of a table of total bytes (0: none) */
static int table_piece(delta_u32 total, delta_u32 offset)
{
	if (total == 0 || total > DELTA_LEGACY_SIGNED_SIZE || offset >= total || offset % DELTA_WIRE_TABLE_CHUNK)
		return 0;
	return total - offset < DELTA_WIRE_TABLE_CHUNK ? (int)(total - offset) : DELTA_WIRE_TABLE_CHUNK;
}

int delta_wire_read_table(const unsigned char *payload, int size, struct delta_wire_table *table)
{
	clear(table, (int)sizeof(*table));
	if (!payload || size < DELTA_WIRE_TABLE_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	table->serial = read_u32(payload);
	table->total = read_u32(payload + 4);
	table->offset = read_u32(payload + 8);
	table->length = read_u16(payload + 12);
	if (!table->serial || table->serial == DELTA_WIRE_TABLE_NONE || !table->length ||
		table->length != table_piece(table->total, table->offset) || size < DELTA_WIRE_TABLE_SIZE + table->length)
	{
		clear(table, (int)sizeof(*table));
		return 0;
	}
	table->data = payload + DELTA_WIRE_TABLE_SIZE;
	return 1;
}

int delta_wire_write_table(unsigned char *data, delta_u32 session, const struct delta_wire_table *table)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int index;

	if (!table->data || !table->serial || table->serial == DELTA_WIRE_TABLE_NONE || table->length <= 0 ||
		table->length != table_piece(table->total, table->offset))
	{
		return 0;
	}
	write_u32(payload, table->serial);
	write_u32(payload + 4, table->total);
	write_u32(payload + 8, table->offset);
	write_u16(payload + 12, (unsigned int)table->length);
	write_u16(payload + 14, 0);
	for (index = 0; index < table->length; index++)
		payload[DELTA_WIRE_TABLE_SIZE + index] = table->data[index];
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_table, DELTA_WIRE_TABLE_SIZE + table->length, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_TABLE_SIZE + table->length;
}

/* ---------- TABLE_HAVE
	0  serial (4: 0 the built-in table, DELTA_WIRE_TABLE_NONE takes none) */

int delta_wire_read_table_have(const unsigned char *payload, int size, delta_u32 *serial)
{
	*serial = 0;
	if (!payload || size < DELTA_WIRE_TABLE_HAVE_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	*serial = read_u32(payload);
	return 1;
}

int delta_wire_write_table_have(unsigned char *data, delta_u32 session, delta_u32 serial)
{
	write_u32(data + DELTA_WIRE_HEADER_SIZE, serial);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_table_have, DELTA_WIRE_TABLE_HAVE_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_TABLE_HAVE_SIZE;
}

/* ---------- MAP
	0  family (1)
	1  flags (1: DELTA_WIRE_MAP_HASHED)
	2  name length (1, at most 63)
	3  reserved (1)
	4  size, low word (4)
	8  size, high word (4)
	12 hash (32: BLAKE2b-256 of the file; zeros without the flag)
	44 name (its length) */

static int map_name_character(char character)
{
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
		(character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.' ||
		character == ' ' || character == '[' || character == ']' || character == '(' || character == ')' ||
		character == '+';
}

int delta_wire_map_name_valid(const char *name)
{
	int length = 0;

	if (!name)
		return 0;
	for (length = 0; name[length]; length++)
	{
		if (length >= DELTA_WIRE_MAP_NAME_SIZE || !map_name_character(name[length]))
			return 0;
		if (name[length] == '.' && name[length + 1] == '.')
			return 0;
	}
	return length > 0 && !(length == 1 && name[0] == '.');
}

/* whether a map is one MAP carries: a plain name, and a Halo PC map's
hash */
static int map_valid(const struct delta_wire_map *map)
{
	if (map->flags & ~DELTA_WIRE_MAP_HASHED)
		return 0;
	if (map->family == 0)
		return !(map->flags & DELTA_WIRE_MAP_HASHED) && (!map->name[0] || delta_wire_map_name_valid(map->name));
	return (map->flags & DELTA_WIRE_MAP_HASHED) && delta_wire_map_name_valid(map->name);
}

int delta_wire_read_map(const unsigned char *payload, int size, struct delta_wire_map *map)
{
	int length;
	int index;

	clear(map, (int)sizeof(*map));
	if (!payload || size < DELTA_WIRE_MAP_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	length = payload[2];
	if (length > DELTA_WIRE_MAP_NAME_SIZE || size < DELTA_WIRE_MAP_SIZE + length)
		return 0;
	map->family = payload[0];
	map->flags = payload[1];
	map->size_low = read_u32(payload + 4);
	map->size_high = read_u32(payload + 8);
	for (index = 0; index < DELTA_WIRE_MAP_HASH_SIZE; index++)
		map->hash[index] = payload[12 + index];
	for (index = 0; index < length; index++)
	{
		/* (a 0 or any other character a file's name may not have refuses
		it: never mended into another name) */
		if (!map_name_character((char)payload[DELTA_WIRE_MAP_SIZE + index]))
		{
			clear(map, (int)sizeof(*map));
			return 0;
		}
		map->name[index] = (char)payload[DELTA_WIRE_MAP_SIZE + index];
	}
	map->name[length] = 0;
	if (!map_valid(map))
	{
		clear(map, (int)sizeof(*map));
		return 0;
	}
	return 1;
}

int delta_wire_write_map(unsigned char *data, delta_u32 session, const struct delta_wire_map *map)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length = 0;
	int index;

	if (!map_valid(map))
		return 0;
	while (map->name[length])
		length++;
	payload[0] = map->family;
	payload[1] = map->flags;
	payload[2] = (unsigned char)length;
	payload[3] = 0;
	write_u32(payload + 4, map->size_low);
	write_u32(payload + 8, map->size_high);
	for (index = 0; index < DELTA_WIRE_MAP_HASH_SIZE; index++)
		payload[12 + index] = (map->flags & DELTA_WIRE_MAP_HASHED) ? map->hash[index] : 0;
	for (index = 0; index < length; index++)
		payload[DELTA_WIRE_MAP_SIZE + index] = (unsigned char)map->name[index];
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_map, DELTA_WIRE_MAP_SIZE + length, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_MAP_SIZE + length;
}

/* ---------- player traits */

/* each movement trait: 0 (unchanged), else in its range */
static int movement_trait_valid(unsigned short value, int minimum, int maximum)
{
	return value == 0 || (value >= minimum && value <= maximum);
}

static int player_movement_valid(const struct delta_wire_player_traits *traits)
{
	return movement_trait_valid(traits->jump, DELTA_WIRE_TRAIT_JUMP_MINIMUM, DELTA_WIRE_TRAIT_JUMP_MAXIMUM) &&
		movement_trait_valid(traits->gravity, DELTA_WIRE_TRAIT_GRAVITY_MINIMUM, DELTA_WIRE_TRAIT_GRAVITY_MAXIMUM) &&
		movement_trait_valid(traits->melee, DELTA_WIRE_TRAIT_MELEE_MINIMUM, DELTA_WIRE_TRAIT_MELEE_MAXIMUM);
}

static int player_traits_valid(const struct delta_wire_player_traits *traits)
{
	return traits->health >= DELTA_WIRE_TRAIT_HEALTH_MINIMUM && traits->health <= DELTA_WIRE_TRAIT_HEALTH_MAXIMUM &&
		traits->damage >= DELTA_WIRE_TRAIT_DAMAGE_MINIMUM && traits->damage <= DELTA_WIRE_TRAIT_DAMAGE_MAXIMUM &&
		traits->resistance >= DELTA_WIRE_TRAIT_RESISTANCE_MINIMUM &&
		traits->resistance <= DELTA_WIRE_TRAIT_RESISTANCE_MAXIMUM &&
		traits->speed >= DELTA_WIRE_TRAIT_SPEED_MINIMUM && traits->speed <= DELTA_WIRE_TRAIT_SPEED_MAXIMUM &&
		traits->shields < NUMBER_OF_DELTA_WIRE_SHIELDS && traits->highlight <= 1 &&
		player_movement_valid(traits);
}

int delta_wire_traits_valid(const struct delta_wire_traits *traits)
{
	if (traits->kind == DELTA_WIRE_TRAITS_NONE)
		return 1;
	return traits->kind == DELTA_WIRE_TRAITS_BOMB && player_traits_valid(&traits->everyone) &&
		player_traits_valid(&traits->carrier);
}

static void read_player_traits(const unsigned char *data, struct delta_wire_player_traits *traits)
{
	traits->health = read_u16(data);
	traits->damage = read_u16(data + 2);
	traits->resistance = read_u16(data + 4);
	traits->speed = read_u16(data + 6);
	traits->shields = data[8];
	traits->highlight = data[9];
}

static void read_player_movement(const unsigned char *data, struct delta_wire_player_traits *traits)
{
	traits->jump = read_u16(data);
	traits->gravity = read_u16(data + 2);
	traits->melee = read_u16(data + 4);
}

static void write_player_movement(unsigned char *data, const struct delta_wire_player_traits *traits)
{
	write_u16(data, traits->jump);
	write_u16(data + 2, traits->gravity);
	write_u16(data + 4, traits->melee);
	data[6] = 0;
	data[7] = 0;
}

static void write_player_traits(unsigned char *data, const struct delta_wire_player_traits *traits)
{
	write_u16(data, traits->health);
	write_u16(data + 2, traits->damage);
	write_u16(data + 4, traits->resistance);
	write_u16(data + 6, traits->speed);
	data[8] = traits->shields;
	data[9] = traits->highlight;
	data[10] = 0;
	data[11] = 0;
}

int delta_wire_read_traits(const unsigned char *payload, int size, struct delta_wire_traits *traits)
{
	clear(traits, (int)sizeof(*traits));
	if (!payload || size < DELTA_WIRE_TRAITS_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	traits->kind = payload[0];
	/* (none: the sets are not read; they are zeros) */
	if (traits->kind != DELTA_WIRE_TRAITS_NONE)
	{
		read_player_traits(payload + 4, &traits->everyone);
		read_player_traits(payload + 4 + DELTA_WIRE_PLAYER_TRAITS_SIZE, &traits->carrier);
		/* (the movement traits, if the host's build sends them; a later
		version's are read as far as this one knows them) */
		if (size >= DELTA_WIRE_TRAITS_MOVEMENT_SIZE &&
			payload[DELTA_WIRE_TRAITS_SIZE] >= DELTA_WIRE_TRAITS_MOVEMENT_VERSION)
		{
			read_player_movement(payload + DELTA_WIRE_TRAITS_SIZE + 4, &traits->everyone);
			read_player_movement(payload + DELTA_WIRE_TRAITS_SIZE + 4 + DELTA_WIRE_PLAYER_MOVEMENT_SIZE,
				&traits->carrier);
		}
	}
	if (!delta_wire_traits_valid(traits))
	{
		clear(traits, (int)sizeof(*traits));
		return 0;
	}
	return 1;
}

int delta_wire_write_traits(unsigned char *data, delta_u32 session, const struct delta_wire_traits *traits)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	struct delta_wire_player_traits none;

	if (!delta_wire_traits_valid(traits))
		return 0;
	clear(&none, (int)sizeof(none));
	payload[0] = traits->kind;
	payload[1] = 0;
	payload[2] = 0;
	payload[3] = 0;
	write_player_traits(payload + 4, traits->kind != DELTA_WIRE_TRAITS_NONE ? &traits->everyone : &none);
	write_player_traits(payload + 4 + DELTA_WIRE_PLAYER_TRAITS_SIZE,
		traits->kind != DELTA_WIRE_TRAITS_NONE ? &traits->carrier : &none);
	/* (the movement traits after: an earlier build reads past them) */
	payload[DELTA_WIRE_TRAITS_SIZE] = DELTA_WIRE_TRAITS_MOVEMENT_VERSION;
	payload[DELTA_WIRE_TRAITS_SIZE + 1] = 0;
	payload[DELTA_WIRE_TRAITS_SIZE + 2] = 0;
	payload[DELTA_WIRE_TRAITS_SIZE + 3] = 0;
	write_player_movement(payload + DELTA_WIRE_TRAITS_SIZE + 4,
		traits->kind != DELTA_WIRE_TRAITS_NONE ? &traits->everyone : &none);
	write_player_movement(payload + DELTA_WIRE_TRAITS_SIZE + 4 + DELTA_WIRE_PLAYER_MOVEMENT_SIZE,
		traits->kind != DELTA_WIRE_TRAITS_NONE ? &traits->carrier : &none);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_traits, DELTA_WIRE_TRAITS_MOVEMENT_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_TRAITS_MOVEMENT_SIZE;
}

/* ---------- moderation */

/* whether length bytes of data are printable ASCII */
static int printable(const unsigned char *data, int length)
{
	int index;

	for (index = 0; index < length; index++)
	{
		if (data[index] < 0x20 || data[index] > 0x7E)
			return 0;
	}
	return 1;
}

/* length bytes of text into out (maximum + 1), printable ASCII kept and
the rest '?' */
static void read_moderation_text(const unsigned char *data, int length, char *out, int maximum)
{
	int index;

	for (index = 0; index < length && index < maximum; index++)
		out[index] = data[index] >= 0x20 && data[index] <= 0x7E ? (char)data[index] : '?';
	out[index] = 0;
}

/* the length of text as sent: up to its end, at most maximum */
static int moderation_text_length(const char *text, int maximum)
{
	int length = 0;

	while (length < maximum && text[length])
		length++;
	return length;
}

static void copy_bytes(unsigned char *destination, const unsigned char *source, int size)
{
	int index;

	for (index = 0; index < size; index++)
		destination[index] = source[index];
}

/* MOD_CHALLENGE
	0  nonce (32)
	32 binding length (1, at most 64)
	33 reserved (3)
	36 binding (printable ASCII) */

int delta_wire_read_mod_challenge(const unsigned char *payload, int size, struct delta_wire_mod_challenge *challenge)
{
	int length;

	clear(challenge, (int)sizeof(*challenge));
	if (!payload || size < DELTA_WIRE_MOD_CHALLENGE_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	length = payload[32];
	if (length > DELTA_WIRE_MODERATION_BINDING_SIZE || size < DELTA_WIRE_MOD_CHALLENGE_SIZE + length ||
		!printable(payload + DELTA_WIRE_MOD_CHALLENGE_SIZE, length))
	{
		return 0;
	}
	copy_bytes(challenge->nonce, payload, DELTA_WIRE_MODERATION_NONCE_SIZE);
	challenge->binding_length = length;
	read_moderation_text(payload + DELTA_WIRE_MOD_CHALLENGE_SIZE, length, challenge->binding,
		DELTA_WIRE_MODERATION_BINDING_SIZE);
	return 1;
}

int delta_wire_write_mod_challenge(unsigned char *data, delta_u32 session,
	const struct delta_wire_mod_challenge *challenge)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length = challenge->binding_length;

	if (length < 0 || length > DELTA_WIRE_MODERATION_BINDING_SIZE ||
		!printable((const unsigned char *)challenge->binding, length))
	{
		return 0;
	}
	clear(payload, DELTA_WIRE_MOD_CHALLENGE_SIZE);
	copy_bytes(payload, challenge->nonce, DELTA_WIRE_MODERATION_NONCE_SIZE);
	payload[32] = (unsigned char)length;
	copy_bytes(payload + DELTA_WIRE_MOD_CHALLENGE_SIZE, (const unsigned char *)challenge->binding, length);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_mod_challenge, DELTA_WIRE_MOD_CHALLENGE_SIZE + length,
		session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_MOD_CHALLENGE_SIZE + length;
}

/* MOD_PROOF
	0  moderator key (32)
	32 signature (64) */

int delta_wire_read_mod_proof(const unsigned char *payload, int size, struct delta_wire_mod_proof *proof)
{
	clear(proof, (int)sizeof(*proof));
	if (!payload || size < DELTA_WIRE_MOD_PROOF_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	copy_bytes(proof->key, payload, DELTA_WIRE_MODERATION_KEY_SIZE);
	copy_bytes(proof->signature, payload + 32, DELTA_WIRE_MODERATION_SIGNATURE_SIZE);
	return 1;
}

int delta_wire_write_mod_proof(unsigned char *data, delta_u32 session, const struct delta_wire_mod_proof *proof)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	copy_bytes(payload, proof->key, DELTA_WIRE_MODERATION_KEY_SIZE);
	copy_bytes(payload + 32, proof->signature, DELTA_WIRE_MODERATION_SIGNATURE_SIZE);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_mod_proof, DELTA_WIRE_MOD_PROOF_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_MOD_PROOF_SIZE;
}

/* MOD_STATE
	0  role (1)
	1  reserved (3)
	4  permissions (4)
	8  the longest timed ban, minutes (4) */

int delta_wire_read_mod_state(const unsigned char *payload, int size, struct delta_wire_mod_state *state)
{
	clear(state, (int)sizeof(*state));
	if (!payload || size < DELTA_WIRE_MOD_STATE_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	state->role = payload[0];
	/* (flags a build does not know are dropped) */
	state->actions = (unsigned char)(payload[1] & DELTA_WIRE_MOD_STATE_KNOWN);
	state->permissions = read_u32(payload + 4);
	state->ban_minutes = read_u32(payload + 8);
	return 1;
}

int delta_wire_write_mod_state(unsigned char *data, delta_u32 session, const struct delta_wire_mod_state *state)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	clear(payload, DELTA_WIRE_MOD_STATE_SIZE);
	payload[0] = state->role;
	payload[1] = (unsigned char)(state->actions & DELTA_WIRE_MOD_STATE_KNOWN);
	write_u32(payload + 4, state->permissions);
	write_u32(payload + 8, state->ban_minutes);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_mod_state, DELTA_WIRE_MOD_STATE_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_MOD_STATE_SIZE;
}

/* MOD_ACTION
	0  sequence (4)
	4  action (1)
	5  target machine (1)
	6  minutes (2)
	8  reason length (1, at most 63)
	9  reserved (3)
	12 signature (64)
	76 reason (printable ASCII) */

int delta_wire_read_mod_action(const unsigned char *payload, int size, struct delta_wire_mod_action *action)
{
	int length;

	clear(action, (int)sizeof(*action));
	if (!payload || size < DELTA_WIRE_MOD_ACTION_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	length = payload[8];
	if (length > DELTA_WIRE_MODERATION_REASON_SIZE || size < DELTA_WIRE_MOD_ACTION_SIZE + length ||
		!printable(payload + DELTA_WIRE_MOD_ACTION_SIZE, length))
	{
		return 0;
	}
	action->sequence = read_u32(payload);
	action->action = payload[4];
	action->target = payload[5];
	action->minutes = read_u16(payload + 6);
	action->reason_length = length;
	copy_bytes(action->signature, payload + 12, DELTA_WIRE_MODERATION_SIGNATURE_SIZE);
	read_moderation_text(payload + DELTA_WIRE_MOD_ACTION_SIZE, length, action->reason,
		DELTA_WIRE_MODERATION_REASON_SIZE);
	return 1;
}

int delta_wire_write_mod_action(unsigned char *data, delta_u32 session, const struct delta_wire_mod_action *action)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length = action->reason_length;

	if (length < 0 || length > DELTA_WIRE_MODERATION_REASON_SIZE ||
		!printable((const unsigned char *)action->reason, length))
	{
		return 0;
	}
	clear(payload, DELTA_WIRE_MOD_ACTION_SIZE);
	write_u32(payload, action->sequence);
	payload[4] = action->action;
	payload[5] = action->target;
	write_u16(payload + 6, action->minutes);
	payload[8] = (unsigned char)length;
	copy_bytes(payload + 12, action->signature, DELTA_WIRE_MODERATION_SIGNATURE_SIZE);
	copy_bytes(payload + DELTA_WIRE_MOD_ACTION_SIZE, (const unsigned char *)action->reason, length);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_mod_action, DELTA_WIRE_MOD_ACTION_SIZE + length,
		session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_MOD_ACTION_SIZE + length;
}

/* MOD_RESULT
	0  sequence (4)
	4  ok (1)
	5  text length (1, at most 127)
	6  reserved (2)
	8  text */

int delta_wire_read_mod_result(const unsigned char *payload, int size, struct delta_wire_mod_result *result)
{
	int length;

	clear(result, (int)sizeof(*result));
	if (!payload || size < DELTA_WIRE_MOD_RESULT_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	length = payload[5];
	if (length > DELTA_WIRE_MODERATION_TEXT_SIZE || size < DELTA_WIRE_MOD_RESULT_SIZE + length)
		return 0;
	result->sequence = read_u32(payload);
	result->ok = (unsigned char)(payload[4] ? 1 : 0);
	read_moderation_text(payload + DELTA_WIRE_MOD_RESULT_SIZE, length, result->text, DELTA_WIRE_MODERATION_TEXT_SIZE);
	return 1;
}

int delta_wire_write_mod_result(unsigned char *data, delta_u32 session, const struct delta_wire_mod_result *result)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length = moderation_text_length(result->text, DELTA_WIRE_MODERATION_TEXT_SIZE);

	clear(payload, DELTA_WIRE_MOD_RESULT_SIZE);
	write_u32(payload, result->sequence);
	payload[4] = (unsigned char)(result->ok ? 1 : 0);
	payload[5] = (unsigned char)length;
	write_text(payload + DELTA_WIRE_MOD_RESULT_SIZE, result->text, length);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_mod_result, DELTA_WIRE_MOD_RESULT_SIZE + length, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_MOD_RESULT_SIZE + length;
}

/* MOD_NOTICE
	0  kind (1)
	1  text length (1, at most 127)
	2  reserved (2)
	4  text */

int delta_wire_read_mod_notice(const unsigned char *payload, int size, struct delta_wire_mod_notice *notice)
{
	int length;

	clear(notice, (int)sizeof(*notice));
	if (!payload || size < DELTA_WIRE_MOD_NOTICE_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	length = payload[1];
	if (length > DELTA_WIRE_MODERATION_TEXT_SIZE || size < DELTA_WIRE_MOD_NOTICE_SIZE + length)
		return 0;
	notice->kind = payload[0];
	read_moderation_text(payload + DELTA_WIRE_MOD_NOTICE_SIZE, length, notice->text, DELTA_WIRE_MODERATION_TEXT_SIZE);
	return 1;
}

int delta_wire_write_mod_notice(unsigned char *data, delta_u32 session, const struct delta_wire_mod_notice *notice)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length = moderation_text_length(notice->text, DELTA_WIRE_MODERATION_TEXT_SIZE);

	clear(payload, DELTA_WIRE_MOD_NOTICE_SIZE);
	payload[0] = notice->kind;
	payload[1] = (unsigned char)length;
	write_text(payload + DELTA_WIRE_MOD_NOTICE_SIZE, notice->text, length);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_mod_notice, DELTA_WIRE_MOD_NOTICE_SIZE + length, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_MOD_NOTICE_SIZE + length;
}

/* MOD_BIND
	0  request (4)
	4  account length (1, at most 31)
	5  server name length (1, at most 31)
	6  reserved (2)
	8  account, then server name */

int delta_wire_read_mod_bind(const unsigned char *payload, int size, struct delta_wire_mod_bind *bind)
{
	int account, server;

	clear(bind, (int)sizeof(*bind));
	if (!payload || size < DELTA_WIRE_MOD_BIND_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	account = payload[4];
	server = payload[5];
	if (account > DELTA_WIRE_MODERATION_NAME_SIZE || server > DELTA_WIRE_MODERATION_NAME_SIZE ||
		size < DELTA_WIRE_MOD_BIND_SIZE + account + server)
	{
		return 0;
	}
	bind->request = read_u32(payload);
	read_moderation_text(payload + DELTA_WIRE_MOD_BIND_SIZE, account, bind->account, DELTA_WIRE_MODERATION_NAME_SIZE);
	read_moderation_text(payload + DELTA_WIRE_MOD_BIND_SIZE + account, server, bind->server,
		DELTA_WIRE_MODERATION_NAME_SIZE);
	return 1;
}

int delta_wire_write_mod_bind(unsigned char *data, delta_u32 session, const struct delta_wire_mod_bind *bind)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int account = moderation_text_length(bind->account, DELTA_WIRE_MODERATION_NAME_SIZE);
	int server = moderation_text_length(bind->server, DELTA_WIRE_MODERATION_NAME_SIZE);

	clear(payload, DELTA_WIRE_MOD_BIND_SIZE);
	write_u32(payload, bind->request);
	payload[4] = (unsigned char)account;
	payload[5] = (unsigned char)server;
	write_text(payload + DELTA_WIRE_MOD_BIND_SIZE, bind->account, account);
	write_text(payload + DELTA_WIRE_MOD_BIND_SIZE + account, bind->server, server);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_mod_bind, DELTA_WIRE_MOD_BIND_SIZE + account + server,
		session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_MOD_BIND_SIZE + account + server;
}

/* MOD_BIND_ANSWER
	0  request (4)
	4  accepted (1)
	5  reserved (3)
	8  moderator key (32)
	40 signature (64) */

int delta_wire_read_mod_bind_answer(const unsigned char *payload, int size, struct delta_wire_mod_bind_answer *answer)
{
	clear(answer, (int)sizeof(*answer));
	if (!payload || size < DELTA_WIRE_MOD_BIND_ANSWER_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	answer->request = read_u32(payload);
	answer->accepted = (unsigned char)(payload[4] ? 1 : 0);
	copy_bytes(answer->key, payload + 8, DELTA_WIRE_MODERATION_KEY_SIZE);
	copy_bytes(answer->signature, payload + 40, DELTA_WIRE_MODERATION_SIGNATURE_SIZE);
	return 1;
}

int delta_wire_write_mod_bind_answer(unsigned char *data, delta_u32 session,
	const struct delta_wire_mod_bind_answer *answer)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	clear(payload, DELTA_WIRE_MOD_BIND_ANSWER_SIZE);
	write_u32(payload, answer->request);
	payload[4] = (unsigned char)(answer->accepted ? 1 : 0);
	copy_bytes(payload + 8, answer->key, DELTA_WIRE_MODERATION_KEY_SIZE);
	copy_bytes(payload + 40, answer->signature, DELTA_WIRE_MODERATION_SIGNATURE_SIZE);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_mod_bind_answer, DELTA_WIRE_MOD_BIND_ANSWER_SIZE,
		session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_MOD_BIND_ANSWER_SIZE;
}

/* the signed messages: a label, the nonce, then the fields */

static int moderation_label(unsigned char *message, const char *label, const unsigned char *nonce)
{
	int length = 0;

	while (label[length])
	{
		message[length] = (unsigned char)label[length];
		length++;
	}
	copy_bytes(message + length, nonce, DELTA_WIRE_MODERATION_NONCE_SIZE);
	return length + DELTA_WIRE_MODERATION_NONCE_SIZE;
}

int delta_wire_moderation_proof_message(unsigned char *message, const unsigned char *nonce, const char *binding,
	int binding_length)
{
	int length = moderation_label(message, "delta moderation proof v1\n", nonce);

	if (binding_length < 0 || binding_length > DELTA_WIRE_MODERATION_BINDING_SIZE)
		binding_length = 0;
	copy_bytes(message + length, (const unsigned char *)binding, binding_length);
	return length + binding_length;
}

int delta_wire_moderation_action_message(unsigned char *message, const unsigned char *nonce,
	const struct delta_wire_mod_action *action)
{
	int length = moderation_label(message, "delta moderation action v1\n", nonce);
	int reason = action->reason_length;

	if (reason < 0 || reason > DELTA_WIRE_MODERATION_REASON_SIZE)
		reason = 0;
	write_u32(message + length, action->sequence);
	message[length + 4] = action->action;
	message[length + 5] = action->target;
	write_u16(message + length + 6, action->minutes);
	copy_bytes(message + length + 8, (const unsigned char *)action->reason, reason);
	return length + 8 + reason;
}

int delta_wire_moderation_bind_message(unsigned char *message, const unsigned char *nonce, delta_u32 request,
	int accepted, const char *binding, int binding_length)
{
	int length = moderation_label(message, "delta moderation bind v1\n", nonce);

	if (binding_length < 0 || binding_length > DELTA_WIRE_MODERATION_BINDING_SIZE)
		binding_length = 0;
	write_u32(message + length, request);
	message[length + 4] = (unsigned char)(accepted ? 1 : 0);
	copy_bytes(message + length + 5, (const unsigned char *)binding, binding_length);
	return length + 5 + binding_length;
}

/* ---------- chat */

/* CHAT_CONFIG
	0  mode (1)
	1  flags (1)
	2  reserved (2)
	4  mute seconds left (4) */

int delta_wire_read_chat_config(const unsigned char *payload, int size, struct delta_wire_chat_config *config)
{
	clear(config, (int)sizeof(*config));
	if (!payload || size < DELTA_WIRE_CHAT_CONFIG_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	if (payload[0] > _delta_chat_mode_on)
		return 0;
	config->mode = payload[0];
	config->flags = payload[1];
	config->mute_seconds = read_u32(payload + 4);
	return 1;
}

int delta_wire_write_chat_config(unsigned char *data, delta_u32 session, const struct delta_wire_chat_config *config)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if (config->mode > _delta_chat_mode_on)
		return 0;
	clear(payload, DELTA_WIRE_CHAT_CONFIG_SIZE);
	payload[0] = config->mode;
	payload[1] = config->flags;
	write_u32(payload + 4, config->mute_seconds);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_chat_config, DELTA_WIRE_CHAT_CONFIG_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_CHAT_CONFIG_SIZE;
}

/* CHAT_SAY
	0  sequence (4)
	4  scope (1)
	5  reserved (1)
	6  text length (1, 1 to 120)
	7  reserved (1)
	8  text (UTF-8) */

int delta_wire_read_chat_say(const unsigned char *payload, int size, struct delta_wire_chat_say *say)
{
	int length;

	clear(say, (int)sizeof(*say));
	if (!payload || size < DELTA_WIRE_CHAT_SAY_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	length = payload[6];
	if (payload[4] >= NUMBER_OF_DELTA_CHAT_SCOPES || length < 1 || length > DELTA_WIRE_CHAT_SAY_TEXT_SIZE ||
		size < DELTA_WIRE_CHAT_SAY_SIZE + length)
	{
		return 0;
	}
	say->sequence = read_u32(payload);
	say->scope = payload[4];
	say->length = length;
	copy_bytes((unsigned char *)say->text, payload + DELTA_WIRE_CHAT_SAY_SIZE, length);
	say->text[length] = 0;
	return 1;
}

int delta_wire_write_chat_say(unsigned char *data, delta_u32 session, const struct delta_wire_chat_say *say)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if (say->scope >= NUMBER_OF_DELTA_CHAT_SCOPES || say->length < 1 || say->length > DELTA_WIRE_CHAT_SAY_TEXT_SIZE)
		return 0;
	clear(payload, DELTA_WIRE_CHAT_SAY_SIZE);
	write_u32(payload, say->sequence);
	payload[4] = say->scope;
	payload[6] = (unsigned char)say->length;
	copy_bytes(payload + DELTA_WIRE_CHAT_SAY_SIZE, (const unsigned char *)say->text, say->length);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_chat_say, DELTA_WIRE_CHAT_SAY_SIZE + say->length,
		session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_CHAT_SAY_SIZE + say->length;
}

/* CHAT_LINE
	0  line (4)
	4  kind (1)
	5  scope (1)
	6  speaker machine (1; 255 none)
	7  speaker player (1; 255 none)
	8  name length (1, at most 47)
	9  text length (1, 1 to 200)
	10 reserved (2)
	12 name, then text (UTF-8) */

int delta_wire_read_chat_line(const unsigned char *payload, int size, struct delta_wire_chat_line *line)
{
	int name, length;

	clear(line, (int)sizeof(*line));
	if (!payload || size < DELTA_WIRE_CHAT_LINE_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	name = payload[8];
	length = payload[9];
	if (payload[4] >= NUMBER_OF_DELTA_CHAT_KINDS || payload[5] >= NUMBER_OF_DELTA_CHAT_SCOPES ||
		name > DELTA_WIRE_CHAT_NAME_SIZE || length < 1 || length > DELTA_WIRE_CHAT_TEXT_SIZE ||
		size < DELTA_WIRE_CHAT_LINE_SIZE + name + length)
	{
		return 0;
	}
	line->line = read_u32(payload);
	line->kind = payload[4];
	line->scope = payload[5];
	line->speaker_machine = payload[6];
	line->speaker_player = payload[7];
	line->name_length = name;
	copy_bytes((unsigned char *)line->name, payload + DELTA_WIRE_CHAT_LINE_SIZE, name);
	line->name[name] = 0;
	line->length = length;
	copy_bytes((unsigned char *)line->text, payload + DELTA_WIRE_CHAT_LINE_SIZE + name, length);
	line->text[length] = 0;
	return 1;
}

int delta_wire_write_chat_line(unsigned char *data, delta_u32 session, const struct delta_wire_chat_line *line)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if (line->kind >= NUMBER_OF_DELTA_CHAT_KINDS || line->scope >= NUMBER_OF_DELTA_CHAT_SCOPES ||
		line->name_length < 0 || line->name_length > DELTA_WIRE_CHAT_NAME_SIZE || line->length < 1 ||
		line->length > DELTA_WIRE_CHAT_TEXT_SIZE)
	{
		return 0;
	}
	clear(payload, DELTA_WIRE_CHAT_LINE_SIZE);
	write_u32(payload, line->line);
	payload[4] = line->kind;
	payload[5] = line->scope;
	payload[6] = line->speaker_machine;
	payload[7] = line->speaker_player;
	payload[8] = (unsigned char)line->name_length;
	payload[9] = (unsigned char)line->length;
	copy_bytes(payload + DELTA_WIRE_CHAT_LINE_SIZE, (const unsigned char *)line->name, line->name_length);
	copy_bytes(payload + DELTA_WIRE_CHAT_LINE_SIZE + line->name_length, (const unsigned char *)line->text,
		line->length);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_chat_line,
		DELTA_WIRE_CHAT_LINE_SIZE + line->name_length + line->length, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_CHAT_LINE_SIZE + line->name_length + line->length;
}

/* CHAT_REFUSED
	0  sequence (4)
	4  reason (1)
	5  reserved (3) */

int delta_wire_read_chat_refused(const unsigned char *payload, int size, struct delta_wire_chat_refused *refused)
{
	clear(refused, (int)sizeof(*refused));
	if (!payload || size < DELTA_WIRE_CHAT_REFUSED_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	refused->sequence = read_u32(payload);
	refused->reason = payload[4];
	refused->paused_seconds = read_u16(payload + 6);
	return 1;
}

int delta_wire_write_chat_refused(unsigned char *data, delta_u32 session,
	const struct delta_wire_chat_refused *refused)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	clear(payload, DELTA_WIRE_CHAT_REFUSED_SIZE);
	write_u32(payload, refused->sequence);
	payload[4] = refused->reason;
	write_u16(payload + 6, refused->paused_seconds);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_chat_refused, DELTA_WIRE_CHAT_REFUSED_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_CHAT_REFUSED_SIZE;
}

static int team_valid(unsigned char team, int other)
{
	return team == 0 || team == 1 || (other && team == DELTA_WIRE_TEAM_OTHER);
}

int delta_wire_read_team_request(const unsigned char *payload, int size, struct delta_wire_team_request *request)
{
	clear(request, (int)sizeof(*request));
	if (!payload || size < DELTA_WIRE_TEAM_REQUEST_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	request->sequence = read_u32(payload);
	request->player = payload[4];
	request->team = payload[5];
	return team_valid(request->team, 1);
}

int delta_wire_write_team_request(unsigned char *data, delta_u32 session,
	const struct delta_wire_team_request *request)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if (!team_valid(request->team, 1))
		return 0;
	clear(payload, DELTA_WIRE_TEAM_REQUEST_SIZE);
	write_u32(payload, request->sequence);
	payload[4] = request->player;
	payload[5] = request->team;
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_team_request, DELTA_WIRE_TEAM_REQUEST_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_TEAM_REQUEST_SIZE;
}

int delta_wire_read_team_result(const unsigned char *payload, int size, struct delta_wire_team_result *result)
{
	clear(result, (int)sizeof(*result));
	if (!payload || size < DELTA_WIRE_TEAM_RESULT_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	result->sequence = read_u32(payload);
	result->result = payload[4];
	result->team = payload[5];
	result->seconds = read_u16(payload + 6);
	return result->result <= _delta_team_refused_last && team_valid(result->team, 0);
}

int delta_wire_write_team_result(unsigned char *data, delta_u32 session, const struct delta_wire_team_result *result)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if (result->result > _delta_team_refused_last || !team_valid(result->team, 0))
		return 0;
	clear(payload, DELTA_WIRE_TEAM_RESULT_SIZE);
	write_u32(payload, result->sequence);
	payload[4] = result->result;
	payload[5] = result->team;
	write_u16(payload + 6, result->seconds);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_team_result, DELTA_WIRE_TEAM_RESULT_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_TEAM_RESULT_SIZE;
}

/* ---------- reports

REPORT
	0  sequence (4)
	4  target machine (1)
	5  target player (1)
	6  reason (1)
	7  flags (1)
	8  clip token (16; ignored by the host, which makes the clip's token:
	   zeros)
REPORT_RESULT
	0  sequence (4)
	4  result (1)
	5  flags (1)
	6  reserved (2)
	8  clip token (16, the host's: zeros without the send-clip flag; a host
	   before it sends the first 8 bytes alone) */

static int report_valid(const struct delta_wire_report *report)
{
	return report->reason >= _delta_report_cheating && report->reason < NUMBER_OF_DELTA_REPORT_REASONS &&
		!(report->flags & ~DELTA_WIRE_REPORT_CLIP) && report->target_machine != DELTA_WIRE_NO_MACHINE;
}

int delta_wire_read_report(const unsigned char *payload, int size, struct delta_wire_report *report)
{
	clear(report, (int)sizeof(*report));
	if (!payload || size < DELTA_WIRE_REPORT_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	report->sequence = read_u32(payload);
	report->target_machine = payload[4];
	report->target_player = payload[5];
	report->reason = payload[6];
	report->flags = payload[7];
	/* (its clip token is not read: the host makes the clip's token, so a
	client can't pick the one its clip goes to the site with) */
	return report_valid(report);
}

int delta_wire_write_report(unsigned char *data, delta_u32 session, const struct delta_wire_report *report)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if (!report_valid(report))
		return 0;
	clear(payload, DELTA_WIRE_REPORT_SIZE);
	write_u32(payload, report->sequence);
	payload[4] = report->target_machine;
	payload[5] = report->target_player;
	payload[6] = report->reason;
	payload[7] = report->flags;
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_report, DELTA_WIRE_REPORT_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_REPORT_SIZE;
}

int delta_wire_read_report_result(const unsigned char *payload, int size, struct delta_wire_report_result *result)
{
	clear(result, (int)sizeof(*result));
	if (!payload || size < DELTA_WIRE_REPORT_RESULT_SHORT_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	result->sequence = read_u32(payload);
	result->result = payload[4];
	/* (flags a build does not know are dropped) */
	result->flags = (unsigned char)(payload[5] & DELTA_WIRE_REPORT_SEND_CLIP);
	/* (the clip goes only with the host's token: a host before it, or one
	that sent zeros, gets none) */
	if (result->flags & DELTA_WIRE_REPORT_SEND_CLIP)
	{
		int index, any = 0;

		if (size >= DELTA_WIRE_REPORT_RESULT_SIZE)
			copy_bytes(result->clip_token, payload + 8, DELTA_WIRE_REPORT_CLIP_TOKEN_SIZE);
		for (index = 0; index < DELTA_WIRE_REPORT_CLIP_TOKEN_SIZE; index++)
			any |= result->clip_token[index];
		if (!any)
			result->flags = (unsigned char)(result->flags & ~DELTA_WIRE_REPORT_SEND_CLIP);
	}
	return result->result < NUMBER_OF_DELTA_REPORT_RESULTS;
}

int delta_wire_write_report_result(unsigned char *data, delta_u32 session,
	const struct delta_wire_report_result *result)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if (result->result >= NUMBER_OF_DELTA_REPORT_RESULTS)
		return 0;
	clear(payload, DELTA_WIRE_REPORT_RESULT_SIZE);
	write_u32(payload, result->sequence);
	payload[4] = result->result;
	payload[5] = (unsigned char)(result->flags & DELTA_WIRE_REPORT_SEND_CLIP);
	if (result->flags & DELTA_WIRE_REPORT_SEND_CLIP)
		copy_bytes(payload + 8, result->clip_token, DELTA_WIRE_REPORT_CLIP_TOKEN_SIZE);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_report_result, DELTA_WIRE_REPORT_RESULT_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_REPORT_RESULT_SIZE;
}

/* ---------- the host's rules */

static int host_rules_valid(const struct delta_wire_host_rules *rules)
{
	return rules->team_change <= DELTA_WIRE_HOST_TEAM_CHANGE_ALLOW &&
		rules->team_change_margin <= DELTA_WIRE_HOST_TEAM_CHANGE_MAXIMUM_MARGIN;
}

int delta_wire_read_host_rules(const unsigned char *payload, int size, struct delta_wire_host_rules *rules)
{
	clear(rules, (int)sizeof(*rules));
	if (!payload || size < DELTA_WIRE_HOST_RULES_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD || !payload[0])
		return 0;
	rules->flags = (unsigned char)(payload[1] & DELTA_WIRE_HOST_KNOWN_FLAGS);
	rules->team_change = payload[2];
	rules->team_change_margin = payload[3];
	if (!host_rules_valid(rules))
	{
		clear(rules, (int)sizeof(*rules));
		return 0;
	}
	return 1;
}

int delta_wire_write_host_rules(unsigned char *data, delta_u32 session, const struct delta_wire_host_rules *rules)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if (!host_rules_valid(rules) || (rules->flags & ~DELTA_WIRE_HOST_KNOWN_FLAGS))
		return 0;
	clear(payload, DELTA_WIRE_HOST_RULES_SIZE);
	payload[0] = DELTA_WIRE_HOST_RULES_VERSION;
	payload[1] = rules->flags;
	payload[2] = rules->team_change;
	payload[3] = rules->team_change_margin;
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_host_rules, DELTA_WIRE_HOST_RULES_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_HOST_RULES_SIZE;
}

/* ---------- voice */

int delta_wire_voice_packet_cap(int kbps)
{
	int cap;

	if (kbps < DELTA_WIRE_VOICE_MINIMUM_KBPS)
		kbps = DELTA_WIRE_VOICE_MINIMUM_KBPS;
	if (kbps > DELTA_WIRE_VOICE_MAXIMUM_KBPS)
		kbps = DELTA_WIRE_VOICE_MAXIMUM_KBPS;
	/* (a 20 ms frame's average share is kbps * 1000 / 8 / 50 bytes) */
	cap = kbps * 1000 / 8 / 50 * 2 + 16;
	return cap < DELTA_WIRE_VOICE_PACKET_SIZE ? cap : DELTA_WIRE_VOICE_PACKET_SIZE;
}

static int voice_channel_valid(int channel)
{
	return channel > _delta_voice_channel_none && channel < NUMBER_OF_DELTA_VOICE_CHANNELS;
}

int delta_wire_read_voice_up(const unsigned char *payload, int size, struct delta_wire_voice_up *up,
	const unsigned char **packet)
{
	clear(up, (int)sizeof(*up));
	*packet = 0;
	if (!payload || size < DELTA_WIRE_VOICE_UP_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	up->sequence = read_u16(payload);
	up->length = payload[2];
	up->channel = payload[3];
	up->hears = (unsigned char)(payload[4] & DELTA_VOICE_CHANNELS);
	if (up->length)
	{
		/* (a frame: said on a channel, its bytes exactly what came) */
		if (size != DELTA_WIRE_VOICE_UP_SIZE + up->length || !voice_channel_valid(up->channel))
			goto refused;
		*packet = payload + DELTA_WIRE_VOICE_UP_SIZE;
		return 1;
	}
	/* (no frame: the channels heard, and maybe a party's tag) */
	if (up->channel != _delta_voice_channel_none)
		goto refused;
	if (size == DELTA_WIRE_VOICE_UP_SIZE)
		return 1;
	if (size != DELTA_WIRE_VOICE_UP_SIZE + DELTA_WIRE_VOICE_PARTY_TAG_SIZE)
		goto refused;
	copy(up->party, payload + DELTA_WIRE_VOICE_UP_SIZE, DELTA_WIRE_VOICE_PARTY_TAG_SIZE);
	return 1;

refused:
	clear(up, (int)sizeof(*up));
	return 0;
}

int delta_wire_write_voice_up(unsigned char *data, delta_u32 session, const struct delta_wire_voice_up *up,
	const unsigned char *packet)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int size = DELTA_WIRE_VOICE_UP_SIZE;
	int index, party = 0;

	if (up->length ? !packet || !voice_channel_valid(up->channel) : up->channel != _delta_voice_channel_none)
		return 0;
	for (index = 0; index < DELTA_WIRE_VOICE_PARTY_TAG_SIZE; index++)
		party |= up->party[index];
	clear(payload, DELTA_WIRE_VOICE_UP_SIZE);
	write_u16(payload, up->sequence);
	payload[2] = up->length;
	payload[3] = up->channel;
	payload[4] = (unsigned char)(up->hears & DELTA_VOICE_CHANNELS);
	if (up->length)
	{
		copy(payload + size, packet, up->length);
		size += up->length;
	}
	else if (party)
	{
		copy(payload + size, up->party, DELTA_WIRE_VOICE_PARTY_TAG_SIZE);
		size += DELTA_WIRE_VOICE_PARTY_TAG_SIZE;
	}
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_voice_up, size, session);
	return DELTA_WIRE_HEADER_SIZE + size;
}

int delta_wire_read_voice_down(const unsigned char *payload, int size, struct delta_wire_voice_down *down,
	const unsigned char **packet)
{
	clear(down, (int)sizeof(*down));
	*packet = 0;
	if (!payload || size < DELTA_WIRE_VOICE_DOWN_SIZE + 1 || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	down->sequence = read_u16(payload);
	down->speaker_machine = payload[2];
	down->speaker_player = payload[3];
	down->route = payload[4];
	down->channel = payload[5];
	down->length = payload[6];
	if (!down->length || size != DELTA_WIRE_VOICE_DOWN_SIZE + down->length ||
		(down->route != _delta_voice_route_global && down->route != _delta_voice_route_proximity) ||
		!voice_channel_valid(down->channel))
	{
		clear(down, (int)sizeof(*down));
		return 0;
	}
	*packet = payload + DELTA_WIRE_VOICE_DOWN_SIZE;
	return 1;
}

int delta_wire_write_voice_down(unsigned char *data, delta_u32 session, const struct delta_wire_voice_down *down,
	const unsigned char *packet)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if (!down->length || !packet || !voice_channel_valid(down->channel) ||
		(down->route != _delta_voice_route_global && down->route != _delta_voice_route_proximity))
	{
		return 0;
	}
	clear(payload, DELTA_WIRE_VOICE_DOWN_SIZE);
	write_u16(payload, down->sequence);
	payload[2] = down->speaker_machine;
	payload[3] = down->speaker_player;
	payload[4] = down->route;
	payload[5] = down->channel;
	payload[6] = down->length;
	copy(payload + DELTA_WIRE_VOICE_DOWN_SIZE, packet, down->length);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_voice_down, DELTA_WIRE_VOICE_DOWN_SIZE + down->length,
		session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_VOICE_DOWN_SIZE + down->length;
}

static int voice_config_valid(const struct delta_wire_voice_config *config)
{
	return config->kbps >= DELTA_WIRE_VOICE_MINIMUM_KBPS && config->kbps <= DELTA_WIRE_VOICE_MAXIMUM_KBPS &&
		config->proximity >= DELTA_WIRE_VOICE_MINIMUM_PROXIMITY &&
		config->proximity <= DELTA_WIRE_VOICE_MAXIMUM_PROXIMITY;
}

int delta_wire_read_voice_config(const unsigned char *payload, int size, struct delta_wire_voice_config *config)
{
	clear(config, (int)sizeof(*config));
	if (!payload || size < DELTA_WIRE_VOICE_CONFIG_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD || !payload[0])
		return 0;
	config->channels = (unsigned char)(payload[1] & DELTA_VOICE_KNOWN_CONFIG);
	config->flags = (unsigned char)(payload[2] & DELTA_VOICE_CONFIG_MUTED);
	config->kbps = payload[3];
	config->proximity = read_u16(payload + 4);
	config->mute_seconds = read_u32(payload + 8);
	if (!voice_config_valid(config))
	{
		clear(config, (int)sizeof(*config));
		return 0;
	}
	return 1;
}

int delta_wire_write_voice_config(unsigned char *data, delta_u32 session, const struct delta_wire_voice_config *config)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

	if (!voice_config_valid(config) || (config->channels & ~DELTA_VOICE_KNOWN_CONFIG) ||
		(config->flags & ~DELTA_VOICE_CONFIG_MUTED))
	{
		return 0;
	}
	clear(payload, DELTA_WIRE_VOICE_CONFIG_SIZE);
	payload[0] = DELTA_WIRE_VOICE_CONFIG_VERSION;
	payload[1] = config->channels;
	payload[2] = config->flags;
	payload[3] = config->kbps;
	write_u16(payload + 4, config->proximity);
	write_u32(payload + 8, config->mute_seconds);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_voice_config, DELTA_WIRE_VOICE_CONFIG_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_VOICE_CONFIG_SIZE;
}

/* ---------- object sync */

int delta_wire_read_unit_vitals(const unsigned char *payload, int size, struct delta_wire_unit_vitals *vitals)
{
	int index;

	vitals->count = 0;
	vitals->flags = 0;
	if (!payload || size < DELTA_WIRE_UNIT_VITALS_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD ||
		payload[0] > DELTA_WIRE_MAXIMUM_UNIT_VITALS ||
		size < DELTA_WIRE_UNIT_VITALS_SIZE + payload[0] * DELTA_WIRE_UNIT_VITALS_ENTRY_SIZE)
	{
		return 0;
	}
	for (index = 0; index < payload[0]; index++)
	{
		const unsigned char *entry = payload + DELTA_WIRE_UNIT_VITALS_SIZE + index * DELTA_WIRE_UNIT_VITALS_ENTRY_SIZE;
		struct delta_wire_unit_vital *vital = &vitals->entries[index];

		vital->object = read_u32(entry);
		vital->flags = entry[4];
		vital->body = read_u16(entry + 6);
		vital->shield = read_u16(entry + 8);
		if (vital->flags & ~DELTA_WIRE_VITALS_FLAGS)
			return 0;
	}
	vitals->count = payload[0];
	vitals->flags = (unsigned char)(payload[1] & DELTA_WIRE_UNIT_VITALS_PASS);
	return 1;
}

int delta_wire_write_unit_vitals(unsigned char *data, delta_u32 session, const struct delta_wire_unit_vitals *vitals)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length;
	int index;

	if (vitals->count < 0 || vitals->count > DELTA_WIRE_MAXIMUM_UNIT_VITALS)
		return 0;
	length = DELTA_WIRE_UNIT_VITALS_SIZE + vitals->count * DELTA_WIRE_UNIT_VITALS_ENTRY_SIZE;
	clear(payload, length);
	payload[0] = (unsigned char)vitals->count;
	payload[1] = (unsigned char)(vitals->flags & DELTA_WIRE_UNIT_VITALS_PASS);
	for (index = 0; index < vitals->count; index++)
	{
		unsigned char *entry = payload + DELTA_WIRE_UNIT_VITALS_SIZE + index * DELTA_WIRE_UNIT_VITALS_ENTRY_SIZE;
		const struct delta_wire_unit_vital *vital = &vitals->entries[index];

		if (vital->flags & ~DELTA_WIRE_VITALS_FLAGS)
			return 0;
		write_u32(entry, vital->object);
		entry[4] = vital->flags;
		write_u16(entry + 6, vital->body);
		write_u16(entry + 8, vital->shield);
	}
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_unit_vitals, length, session);
	return DELTA_WIRE_HEADER_SIZE + length;
}

static int object_names_valid(const struct delta_wire_object_names *names)
{
	return names->total >= 0 && names->total <= DELTA_WIRE_MAXIMUM_OBJECT_NAMES && names->first >= 0 &&
		names->count >= 0 && names->count <= DELTA_WIRE_MAXIMUM_OBJECT_NAMES_ENTRIES &&
		names->first + names->count <= names->total;
}

int delta_wire_read_object_names(const unsigned char *payload, int size, struct delta_wire_object_names *names)
{
	int index;

	names->total = names->first = names->count = 0;
	if (!payload || size < DELTA_WIRE_OBJECT_NAMES_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	names->total = read_u16(payload);
	names->first = read_u16(payload + 2);
	names->count = payload[4];
	if (!object_names_valid(names) ||
		size < DELTA_WIRE_OBJECT_NAMES_SIZE + names->count * DELTA_WIRE_OBJECT_NAMES_ENTRY_SIZE)
	{
		names->total = names->first = names->count = 0;
		return 0;
	}
	for (index = 0; index < names->count; index++)
	{
		const unsigned char *entry = payload + DELTA_WIRE_OBJECT_NAMES_SIZE + index * DELTA_WIRE_OBJECT_NAMES_ENTRY_SIZE;
		struct delta_wire_object_name *name = &names->entries[index];

		name->object = read_u32(entry);
		name->name = read_u16(entry + 4);
		name->flags = (unsigned char)(entry[6] & DELTA_WIRE_NAME_FLAGS);
		name->vitals_flags = entry[7];
		name->body = read_u16(entry + 8);
		name->shield = read_u16(entry + 10);
		if (name->name >= DELTA_WIRE_MAXIMUM_OBJECT_NAMES || (name->vitals_flags & ~DELTA_WIRE_VITALS_FLAGS))
		{
			names->total = names->first = names->count = 0;
			return 0;
		}
	}
	return 1;
}

int delta_wire_write_object_names(unsigned char *data, delta_u32 session, const struct delta_wire_object_names *names)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length;
	int index;

	if (!object_names_valid(names))
		return 0;
	length = DELTA_WIRE_OBJECT_NAMES_SIZE + names->count * DELTA_WIRE_OBJECT_NAMES_ENTRY_SIZE;
	clear(payload, length);
	write_u16(payload, (unsigned int)names->total);
	write_u16(payload + 2, (unsigned int)names->first);
	payload[4] = (unsigned char)names->count;
	for (index = 0; index < names->count; index++)
	{
		unsigned char *entry = payload + DELTA_WIRE_OBJECT_NAMES_SIZE + index * DELTA_WIRE_OBJECT_NAMES_ENTRY_SIZE;
		const struct delta_wire_object_name *name = &names->entries[index];

		if (name->name >= DELTA_WIRE_MAXIMUM_OBJECT_NAMES || (name->flags & ~DELTA_WIRE_NAME_FLAGS) ||
			(name->vitals_flags & ~DELTA_WIRE_VITALS_FLAGS))
		{
			return 0;
		}
		write_u32(entry, name->object);
		write_u16(entry + 4, name->name);
		entry[6] = name->flags;
		entry[7] = name->vitals_flags;
		write_u16(entry + 8, name->body);
		write_u16(entry + 10, name->shield);
	}
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_object_names, length, session);
	return DELTA_WIRE_HEADER_SIZE + length;
}

int delta_wire_read_badge_ask(const unsigned char *payload, int size, unsigned char *nonce)
{
	clear(nonce, DELTA_WIRE_BADGE_NONCE_SIZE);
	if (!payload || size < DELTA_WIRE_BADGE_ASK_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	copy_bytes(nonce, payload, DELTA_WIRE_BADGE_NONCE_SIZE);
	return 1;
}

int delta_wire_write_badge_ask(unsigned char *data, delta_u32 session, const unsigned char *nonce)
{
	copy_bytes(data + DELTA_WIRE_HEADER_SIZE, nonce, DELTA_WIRE_BADGE_NONCE_SIZE);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_badge_ask, DELTA_WIRE_BADGE_ASK_SIZE, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_BADGE_ASK_SIZE;
}

int delta_wire_read_badge(const unsigned char *payload, int size, struct delta_wire_badge *badge)
{
	int length;

	clear(badge, (int)sizeof(*badge));
	if (!payload || size < DELTA_WIRE_BADGE_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD)
		return 0;
	length = read_u16(payload + 96);
	if (length > DELTA_WIRE_BADGE_CERTIFICATE_SIZE || size < DELTA_WIRE_BADGE_SIZE + length)
		return 0;
	copy_bytes(badge->nonce, payload, DELTA_WIRE_BADGE_NONCE_SIZE);
	copy_bytes(badge->signature, payload + 32, DELTA_WIRE_BADGE_SIGNATURE_SIZE);
	badge->proof = payload[98];
	badge->certificate_length = (unsigned short)length;
	copy_bytes(badge->certificate, payload + DELTA_WIRE_BADGE_SIZE, length);
	return 1;
}

int delta_wire_write_badge(unsigned char *data, delta_u32 session, const struct delta_wire_badge *badge)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length = badge->certificate_length;

	if (length > DELTA_WIRE_BADGE_CERTIFICATE_SIZE)
		return 0;
	clear(payload, DELTA_WIRE_BADGE_SIZE);
	copy_bytes(payload, badge->nonce, DELTA_WIRE_BADGE_NONCE_SIZE);
	copy_bytes(payload + 32, badge->signature, DELTA_WIRE_BADGE_SIGNATURE_SIZE);
	write_u16(payload + 96, (unsigned short)length);
	payload[98] = badge->proof;
	copy_bytes(payload + DELTA_WIRE_BADGE_SIZE, badge->certificate, length);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_badge, DELTA_WIRE_BADGE_SIZE + length, session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_BADGE_SIZE + length;
}

int delta_wire_chat_text_valid(const char *text, int length, int maximum_characters)
{
	const unsigned char *bytes = (const unsigned char *)text;
	int index = 0;
	int characters = 0;
	int shown = 0;

	if (!text || length < 1)
		return -1;
	while (index < length)
	{
		delta_u32 code = bytes[index];
		int more, step;

		if (code < 0x80)
			more = 0;
		else if (code >= 0xC2 && code <= 0xDF)
		{
			more = 1;
			code &= 0x1F;
		}
		else if (code >= 0xE0 && code <= 0xEF)
		{
			more = 2;
			code &= 0x0F;
		}
		else if (code >= 0xF0 && code <= 0xF4)
		{
			more = 3;
			code &= 0x07;
		}
		else
			return -1;
		if (index + more >= length && more)
			return -1;
		for (step = 1; step <= more; step++)
		{
			if ((bytes[index + step] & 0xC0) != 0x80)
				return -1;
			code = (code << 6) | (bytes[index + step] & 0x3F);
		}
		/* (overlong forms, surrogates, past the last code point) */
		if ((more == 2 && code < 0x800) || (more == 3 && code < 0x10000) || (code >= 0xD800 && code <= 0xDFFF) ||
			code > 0x10FFFF)
		{
			return -1;
		}
		if (code < 0x20 || code == 0x7F || (code >= 0x80 && code <= 0x9F) || (code >= 0x202A && code <= 0x202E) ||
			(code >= 0x2066 && code <= 0x2069) || (code >= 0x200B && code <= 0x200D) || code == 0x2060 ||
			code == 0xFEFF)
		{
			return -1;
		}
		if (code != ' ' && code != 0xA0 && code != 0x3000)
			shown = 1;
		characters++;
		index += more + 1;
	}
	if (!shown || (maximum_characters > 0 && characters > maximum_characters))
		return -1;
	return characters;
}


/* ---------- verified names */

/* ID_CHALLENGE
	0  version (1)
	1  binding length (1, at most 64)
	2  reserved (2)
	4  nonce (32)
	36 binding (printable ASCII) */

int delta_wire_read_id_challenge(const unsigned char *payload, int size, struct delta_wire_id_challenge *challenge)
{
	int length;

	clear(challenge, (int)sizeof(*challenge));
	if (!payload || size < DELTA_WIRE_ID_CHALLENGE_SIZE || size > DELTA_WIRE_MAXIMUM_PAYLOAD || !payload[0])
		return 0;
	length = payload[1];
	if (length > DELTA_WIRE_IDENTITY_BINDING_SIZE || size < DELTA_WIRE_ID_CHALLENGE_SIZE + length ||
		!printable(payload + DELTA_WIRE_ID_CHALLENGE_SIZE, length))
	{
		return 0;
	}
	copy_bytes(challenge->nonce, payload + 4, DELTA_WIRE_IDENTITY_NONCE_SIZE);
	challenge->binding_length = length;
	read_moderation_text(payload + DELTA_WIRE_ID_CHALLENGE_SIZE, length, challenge->binding,
		DELTA_WIRE_IDENTITY_BINDING_SIZE);
	return 1;
}

int delta_wire_write_id_challenge(unsigned char *data, delta_u32 session,
	const struct delta_wire_id_challenge *challenge)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length = challenge->binding_length;

	if (length < 0 || length > DELTA_WIRE_IDENTITY_BINDING_SIZE ||
		!printable((const unsigned char *)challenge->binding, length))
	{
		return 0;
	}
	clear(payload, DELTA_WIRE_ID_CHALLENGE_SIZE);
	payload[0] = DELTA_WIRE_IDENTITY_VERSION;
	payload[1] = (unsigned char)length;
	copy_bytes(payload + 4, challenge->nonce, DELTA_WIRE_IDENTITY_NONCE_SIZE);
	copy_bytes(payload + DELTA_WIRE_ID_CHALLENGE_SIZE, (const unsigned char *)challenge->binding, length);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_id_challenge, DELTA_WIRE_ID_CHALLENGE_SIZE + length,
		session);
	return DELTA_WIRE_HEADER_SIZE + DELTA_WIRE_ID_CHALLENGE_SIZE + length;
}

/* whether length bytes are a name's characters: a handle's in any capitals
(A-Z a-z 0-9 _ -) */
static int tag_characters(const unsigned char *data, int length)
{
	int index;

	for (index = 0; index < length; index++)
	{
		unsigned char c = data[index];

		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			return 0;
	}
	return 1;
}

/* a proof's fixed fields after the head (flags, time) and its body (the
signature, the issuer, the certificate, the name), from fields: flags at
0, time at 4, the body at body_offset; size bytes in all */
static int read_proof_fields(const unsigned char *fields, int body_offset, int size, struct delta_wire_id_proof *proof)
{
	const unsigned char *body = fields + body_offset;
	int certificate = proof->certificate_length, tag = proof->tag_length, index;

	if (certificate < DELTA_WIRE_IDENTITY_CERTIFICATE_LEAST || certificate > DELTA_WIRE_IDENTITY_CERTIFICATE_MOST ||
		tag > DELTA_WIRE_IDENTITY_TAG_SIZE || size < body_offset + 176 + certificate + tag ||
		!tag_characters(body + 176 + certificate, tag))
	{
		return 0;
	}
	/* (flags this build doesn't know are kept: they are signed) */
	proof->flags = fields[0];
	proof->time = read_u32(fields + 4);
	copy_bytes(proof->signature, body, DELTA_WIRE_IDENTITY_SIGNATURE_SIZE);
	copy_bytes(proof->issuer, body + 64, DELTA_WIRE_IDENTITY_ISSUER_SIZE);
	copy_bytes(proof->certificate, body + 176, certificate);
	for (index = 0; index < tag; index++)
		proof->tag[index] = (char)body[176 + certificate + index];
	proof->tag[tag] = 0;
	return 1;
}

static int proof_writable(const struct delta_wire_id_proof *proof)
{
	if (proof->local_player < 0 || proof->local_player >= DELTA_WIRE_IDENTITY_LOCAL_PLAYERS)
		return 0;
	if (!proof->certificate_length)
		return 1;
	return proof->certificate_length >= DELTA_WIRE_IDENTITY_CERTIFICATE_LEAST &&
		proof->certificate_length <= DELTA_WIRE_IDENTITY_CERTIFICATE_MOST && proof->tag_length >= 0 &&
		proof->tag_length <= DELTA_WIRE_IDENTITY_TAG_SIZE && proof->flags >= 0 && proof->flags <= 255 &&
		tag_characters((const unsigned char *)proof->tag, proof->tag_length);
}

static void write_proof_fields(unsigned char *fields, int body_offset, const struct delta_wire_id_proof *proof)
{
	unsigned char *body = fields + body_offset;

	fields[0] = (unsigned char)proof->flags;
	write_u32(fields + 4, proof->time);
	copy_bytes(body, proof->signature, DELTA_WIRE_IDENTITY_SIGNATURE_SIZE);
	copy_bytes(body + 64, proof->issuer, DELTA_WIRE_IDENTITY_ISSUER_SIZE);
	copy_bytes(body + 176, proof->certificate, proof->certificate_length);
	copy_bytes(body + 176 + proof->certificate_length, (const unsigned char *)proof->tag, proof->tag_length);
}

/* ID_PROOF
	0   version (1)
	1   local player (1, 0 to 3)
	2   certificate length (1: 0 withdrawn, and nothing follows; else 148 to
	    168)
	3   name length (1, at most 20)
	4   flags (1: 0x01 bound)
	5   reserved (3)
	8   time (4, Unix seconds)
	12  signature (64)
	76  issuer certificate (112)
	188 device certificate (L)
	    name (T: the handle in its capitals, A-Z a-z 0-9 _ -) */

int delta_wire_read_id_proof(const unsigned char *payload, int size, struct delta_wire_id_proof *proof)
{
	clear(proof, (int)sizeof(*proof));
	if (!payload || size < DELTA_WIRE_ID_PROOF_HEAD || size > DELTA_WIRE_MAXIMUM_PAYLOAD || !payload[0] ||
		payload[1] >= DELTA_WIRE_IDENTITY_LOCAL_PLAYERS)
	{
		return 0;
	}
	proof->local_player = payload[1];
	proof->certificate_length = payload[2];
	proof->tag_length = payload[3];
	if (!proof->certificate_length)
	{
		proof->tag_length = 0;
		return 1;
	}
	if (read_proof_fields(payload + 4, 8, size - 4, proof))
		return 1;
	clear(proof, (int)sizeof(*proof));
	return 0;
}

int delta_wire_write_id_proof(unsigned char *data, delta_u32 session, const struct delta_wire_id_proof *proof)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length;

	if (!proof_writable(proof))
		return 0;
	length = proof->certificate_length ? DELTA_WIRE_ID_PROOF_SIZE + proof->certificate_length + proof->tag_length :
		DELTA_WIRE_ID_PROOF_HEAD;
	clear(payload, length);
	payload[0] = DELTA_WIRE_IDENTITY_VERSION;
	payload[1] = (unsigned char)proof->local_player;
	payload[2] = (unsigned char)proof->certificate_length;
	payload[3] = (unsigned char)(proof->certificate_length ? proof->tag_length : 0);
	if (proof->certificate_length)
		write_proof_fields(payload + 4, 8, proof);
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_id_proof, length, session);
	return DELTA_WIRE_HEADER_SIZE + length;
}

/* ID_CARD
	0   version (1)
	1   machine index (1)
	2   local player (1, 0 to 3)
	3   certificate length (1: 0 withdrawn, and nothing follows; else 148 to
	    168)
	4   name length (1, at most 20)
	5   reserved (3)
	8   flags (1, the proof's)
	9   reserved (3)
	12  time (4, the proof's)
	16  nonce (32: the one the host challenged the machine's session with)
	48  signature (64)
	112 issuer certificate (112)
	224 device certificate (L)
	    name (T) */

int delta_wire_read_id_card(const unsigned char *payload, int size, struct delta_wire_id_card *card)
{
	clear(card, (int)sizeof(*card));
	if (!payload || size < DELTA_WIRE_ID_CARD_HEAD || size > DELTA_WIRE_MAXIMUM_PAYLOAD || !payload[0] ||
		payload[2] >= DELTA_WIRE_IDENTITY_LOCAL_PLAYERS)
	{
		return 0;
	}
	card->machine_index = payload[1];
	card->proof.local_player = payload[2];
	card->proof.certificate_length = payload[3];
	card->proof.tag_length = payload[4];
	if (!card->proof.certificate_length)
	{
		card->proof.tag_length = 0;
		return 1;
	}
	if (size >= DELTA_WIRE_ID_CARD_SIZE && read_proof_fields(payload + 8, 40, size - 8, &card->proof))
	{
		copy_bytes(card->nonce, payload + 16, DELTA_WIRE_IDENTITY_NONCE_SIZE);
		return 1;
	}
	clear(card, (int)sizeof(*card));
	return 0;
}

int delta_wire_write_id_card(unsigned char *data, delta_u32 session, const struct delta_wire_id_card *card)
{
	unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
	int length;

	if (card->machine_index < 0 || card->machine_index > 255 || !proof_writable(&card->proof))
		return 0;
	length = card->proof.certificate_length ?
		DELTA_WIRE_ID_CARD_SIZE + card->proof.certificate_length + card->proof.tag_length : DELTA_WIRE_ID_CARD_HEAD;
	clear(payload, length);
	payload[0] = DELTA_WIRE_IDENTITY_VERSION;
	payload[1] = (unsigned char)card->machine_index;
	payload[2] = (unsigned char)card->proof.local_player;
	payload[3] = (unsigned char)card->proof.certificate_length;
	payload[4] = (unsigned char)(card->proof.certificate_length ? card->proof.tag_length : 0);
	if (card->proof.certificate_length)
	{
		write_proof_fields(payload + 8, 40, &card->proof);
		copy_bytes(payload + 16, card->nonce, DELTA_WIRE_IDENTITY_NONCE_SIZE);
	}
	delta_wire_write_header(data, DELTA_MAJOR, _delta_message_id_card, length, session);
	return DELTA_WIRE_HEADER_SIZE + length;
}

/* ---------- rate limits */

int delta_rate_take(struct delta_rate *rate, delta_u32 now, int rate_per_second, int burst)
{
	delta_u32 limit = (delta_u32)burst * 1000u;

	if (!rate->started)
	{
		rate->started = 1;
		rate->tokens_milli = limit;
		rate->time = now;
	}
	else
	{
		delta_u32 elapsed = now - rate->time;

		/* (a long pause refills it whole; time running backward adds none) */
		if (elapsed > 0x7FFFFFFFu)
			elapsed = 0;
		if (elapsed > limit)
			elapsed = limit;
		rate->tokens_milli += elapsed * (delta_u32)rate_per_second;
		if (rate->tokens_milli > limit)
			rate->tokens_milli = limit;
		rate->time = now;
	}
	if (rate->tokens_milli < 1000u)
		return 0;
	rate->tokens_milli -= 1000u;
	return 1;
}
