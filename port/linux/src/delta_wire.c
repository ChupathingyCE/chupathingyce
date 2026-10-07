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
		if (elapsed > (delta_u32)burst * 1000u)
			elapsed = (delta_u32)burst * 1000u;
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
