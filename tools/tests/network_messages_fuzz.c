/*
NETWORK_MESSAGES_FUZZ.C

libFuzzer's entry for the game's network messages as a host or a client
decodes them (tools/test_host_fuzz.py builds it against the game's packet
table and decoder, source/networking/network_messages.c and
source/memory/data_*.c, as tools/test_network_messages.py does): the input
is an encoded message (its version byte, fields and type byte, which the
handlers read first), decoded in every packet class into a buffer of exactly
its type's structure size. The join request's 234 reserved bytes after its
fields, as OpenCE's network version 40 sends it, are among the inputs the
fuzzer makes: a host's decoder leaves them.
Every `long` of the game is 32 bits: int here.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char byte;

enum
{
	MESSAGE_TYPE_COUNT = 35,
	CLIENT_JOIN_GAME_REQUEST = 12,
	PACKET_CLASS_CLIENT_PREGAME = 3,
	PACKET_CLASSES = 8,
};

void initialize_network_game_packets(void);
byte decode_network_game_message(void *message_struct, const void *encoded_message, short *encoded_message_size,
	short *packet_type, short *packet_version, int expected_packet_class);

extern const int game_messages_fuzz_sizes[MESSAGE_TYPE_COUNT];

/* ---------- what the five files need of the rest of the game */

char temporary[256];
static byte captured[0x4000];
static unsigned int captured_size;

void *create_message(int type, const void *data, unsigned int data_size, void *buffer, unsigned short buffer_size)
{
	(void)type;
	if (data_size > sizeof(captured) || data_size > buffer_size)
		return NULL;
	memcpy(captured, data, data_size);
	captured_size = data_size;
	return buffer;
}

void display_assert(char *information, char *file, int line, byte fatal)
{
	(void)information;
	(void)file;
	(void)line;
	(void)fatal;
}

/* (a release build's assertion: a failure here too) */
void release_assert_failed(const char *information, const char *file, int line, byte fatal)
{
	display_assert((char *)information, (char *)file, line, fatal);
	exit(1);
}

void system_exit(int code)
{
	exit(code ? code : 1);
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

/* ---------- the fuzzer */

static void decode(int type, int packet_class, const unsigned char *data, size_t size)
{
	unsigned char *encoded, *decoded;
	short encoded_size = (short)size, packet_type = (short)type, packet_version = 1;

	encoded = malloc(size);
	decoded = malloc((size_t)game_messages_fuzz_sizes[type]);
	if (encoded && decoded)
	{
		memcpy(encoded, data, size);
		memset(decoded, 0, (size_t)game_messages_fuzz_sizes[type]);
		decode_network_game_message(decoded, encoded, &encoded_size, &packet_type, &packet_version, packet_class);
	}
	free(encoded);
	free(decoded);
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	static int started;
	int type, packet_class;

	if (!started)
	{
		initialize_network_game_packets();
		started = 1;
	}
	if (size < 2 || size > 0x3000)
		return 0;
	/* (every handler tells the message by its last byte first, and decodes it into
	the structure of that type: the harness does the same, in every class) */
	type = data[size - 1];
	if (type >= MESSAGE_TYPE_COUNT)
		return 0;
	for (packet_class = 0; packet_class < PACKET_CLASSES; packet_class++)
		decode(type, packet_class, data, size);
	return 0;
}
