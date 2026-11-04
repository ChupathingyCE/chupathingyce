/* game_messages_fuzz.c: a client's game message, as a host reads it, through
the game's packet table (source/networking/network_messages.c) and decoder
(source/memory/data_*.c), the units as the game builds them
(tools/test_host_fuzz.py, "game_sources").

The input is a message's packet after its 2-byte header, its type in its last
byte, as network_game_server_handle_client_message and
network_game_server_handle_datagram take it: a type a host reads is decoded
with the class its handler gives, into a structure of exactly its size in
an allocation of its own (AddressSanitizer reports a byte written past it).
What decodes must encode again, as the host does with what it keeps
(players, machines), to no more than the network's largest message.
Every `long` of the game is 32 bits: int here. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char byte;

enum
{
	MESSAGE_TYPE_COUNT = 35,
	/* (network_server_message_handler.c's) */
	CLASS_CLIENT_SEARCH = 0,
	CLASS_CLIENT_PREGAME = 3,
	CLASS_CLIENT_INGAME = 5,
	CLASS_CLIENT_POSTGAME = 7,
	/* (a message's length is 12 bits, its header's 2 bytes among them) */
	MAXIMUM_PACKET_SIZE = 0xFFF - 2,
};

void initialize_network_game_packets(void);
void *create_network_game_message(int message_type, const void *message_struct, short message_struct_size);
byte decode_network_game_message(void *message_struct, const void *encoded_message, short *encoded_message_size,
	short *packet_type, short *packet_version, int expected_packet_class);

/* (messages_sizes.c: each structure's size, compiled with network_messages.c) */
extern const int game_messages_fuzz_sizes[MESSAGE_TYPE_COUNT];

/* the class each type a host reads is decoded as (by its handler); -1 for a
type a host does not read */
static const signed char host_classes[MESSAGE_TYPE_COUNT] =
{
	[0] = CLASS_CLIENT_SEARCH, /* broadcast game search (a datagram) */
	[1] = CLASS_CLIENT_SEARCH, /* ping (a datagram) */
	[2 ... 11] = -1,
	[12 ... 19] = CLASS_CLIENT_PREGAME, /* join, players, settings, start, exit, precached */
	[20 ... 23] = -1,
	[24 ... 27] = CLASS_CLIENT_INGAME, /* loaded, game update (a datagram), players */
	[28 ... 31] = -1,
	[32 ... 34] = CLASS_CLIENT_POSTGAME, /* remove player, switch to pregame, exit */
};

/* ---------- what the units need of the rest of the game */

char temporary[256];

void *create_message(int type, const void *data, unsigned int data_size, void *buffer, unsigned short buffer_size)
{
	(void)type;
	(void)data;
	if (data_size > buffer_size)
		return NULL;
	return buffer;
}

void display_assert(char *information, char *file, int line, byte fatal)
{
	fprintf(stderr, "assert %s:%d %s%s\n", file, line, information ? information : "", fatal ? " (fatal)" : "");
	/* (an assertion a message reaches stops a host that checks them) */
	abort();
}

void release_assert_failed(const char *information, const char *file, int line, byte fatal)
{
	display_assert((char *)information, (char *)file, line, fatal);
}

void system_exit(int code)
{
	(void)code;
	abort();
}

void error(int priority, const char *format, ...)
{
	(void)priority;
	(void)format;
}

unsigned int system_milliseconds(void) { return 1; }
int halo_linux_vsnprintf(char *buffer, size_t count, const char *format, va_list arguments) { return vsnprintf(buffer, count, format, arguments); }
char *csprintf(char *buffer, char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(buffer, sizeof(temporary), format, arguments);
	va_end(arguments);
	return buffer;
}
void *csmemcpy(void *destination, const void *source, unsigned int size) { return memmove(destination, source, size); }
void *csmemset(void *buffer, int c, unsigned int size) { return memset(buffer, c, size); }
char *csstrcpy(char *destination, const char *source) { return strcpy(destination, source); }
char *csstrncpy(char *s1, const char *s2, unsigned int size) { return strncpy(s1, s2, size); }

/* ---------- the target */

int LLVMFuzzerTestOneInput(const byte *data, size_t size)
{
	static int initialized;
	byte *encoded;
	byte *decoded;
	short encoded_size;
	short packet_type;
	short packet_version = 1;
	int type;

	if (!initialized)
	{
		initialize_network_game_packets();
		initialized = 1;
	}
	/* (the handlers decode no empty packet: the host drops a message of its
	header alone) */
	if (size < 1 || size > MAXIMUM_PACKET_SIZE)
		return 0;
	type = data[size - 1];
	if (type >= MESSAGE_TYPE_COUNT || host_classes[type] < 0)
		return 0;

	/* (the packet alone in its allocation: a read past it is reported) */
	encoded = malloc(size);
	memcpy(encoded, data, size);
	decoded = malloc(game_messages_fuzz_sizes[type] ? game_messages_fuzz_sizes[type] : 1);
	memset(decoded, 0xA5, game_messages_fuzz_sizes[type]);
	encoded_size = (short)size;
	packet_type = (short)type;
	if (decode_network_game_message(decoded, encoded, &encoded_size, &packet_type, &packet_version,
		host_classes[type]))
	{
		if (packet_type != type)
		{
			fprintf(stderr, "decoded type %d from a packet of type %d\n", packet_type, type);
			abort();
		}
		/* (as the host sends on what it keeps: it must encode) */
		if (!create_network_game_message(type, decoded, (short)game_messages_fuzz_sizes[type]))
		{
			fprintf(stderr, "a decoded message of type %d does not encode\n", type);
			abort();
		}
	}
	free(decoded);
	free(encoded);
	return 0;
}
