/* client_messages_fuzz.c: what a host sends a client machine, through the
client's own handlers (source/networking/network_client_message_handler.c),
its client (network_client_manager.c) and game (network_game_manager.c), as
the game builds them (tools/test_host_fuzz.py, "game_sources"), past the
decode network_messages stops at.

The client is made for each input (client_setup.c, at the end of the
client's unit): searching, joining, in a lobby of the host's machine (0) and
its own (1) with a player each, in game or after it. The input:

	byte 0: the client's state (0 searching, 1 joining, 2 the lobby, 3 in
	        game, 4 after it)
	then messages, each:
	byte:   bit 0 from another address than the host's, bit 2 a datagram
	        rather than over the connection, bit 3 the distributed kind
	        (_message_type_data) rather than a packet
	2 bytes: its length after the header (little-endian, at most 4093)
	that many bytes: the packet as the handler reads it after the header
	        (its version byte first, its type last)

The header is made here, of the right size. What the transport, Delta, map
sharing, MCC and the distributed netcode do with a message is theirs (their
own targets); here they take it and do nothing. No map loads. Every `long`
of the game is 32 bits: int here; its wchar_t 16. */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned short game_wchar;

enum
{
	MAXIMUM_PACKET_SIZE = 0xFFF - 2,
	MESSAGE_TYPE_DATA = 2,
	MESSAGE_TYPE_PACKET = 3,
	NONE = -1,
	HOST_ADDRESS = 0x0A000009,
};

struct network_game_client;
/* (transport.h's, the game's long 32 bits) */
struct transport_address
{
	union
	{
		unsigned int long_words[4];
		byte bytes[16];
	} address;
	word address_length;
	word port;
	int address_type;
};

/* (client_setup.c) */
struct network_game_client *client_messages_fuzz_reset(int state);

/* (the handler) */
byte network_game_client_handle_message(struct network_game_client *client, word *message, short message_size,
	struct transport_address *source_address);
void initialize_network_game_packets(void);

static struct network_game_client *fuzz_client;
static byte fuzz_unreliable;

/* ---------- what the units need of the rest of the game: what they do
with it here */

char temporary[256];
/* (no players' datums: a zeroed array, not valid, its next identifier 0) */
static long long fuzz_player_array[512];
void *player_data = fuzz_player_array;
char D3D__RenderState[4096];

void *create_message(int type, const void *data, unsigned int data_size, void *buffer, unsigned short buffer_size)
{
	(void)type;
	(void)data;
	if (data_size > buffer_size)
		return NULL;
	return buffer;
}

/* (an assertion the wire reaches: a release build notes it and carries on,
release_assert_failed; so does this, the build's checked form's system_exit
after it let go, and the sanitizers say what comes of it) */
static int fuzz_asserted;

void display_assert(char *information, char *file, int line, byte fatal)
{
	(void)information;
	(void)file;
	(void)line;
	fuzz_asserted = fatal;
}

void release_assert_failed(const char *information, const char *file, int line, byte fatal)
{
	(void)information;
	(void)file;
	(void)line;
	(void)fatal;
}

void system_exit(int code)
{
	(void)code;
	if (fuzz_asserted)
	{
		fuzz_asserted = 0;
		return;
	}
	abort();
}

void halt_and_catch_fire(void)
{
	abort();
}

void error(int priority, const char *format, ...)
{
	(void)priority;
	(void)format;
}

void console_warning(const char *format, ...)
{
	(void)format;
}

void platform_log(const char *format, ...)
{
	(void)format;
}

static unsigned int fuzz_clock;
unsigned int system_milliseconds(void) { return fuzz_clock += 7; }
int game_time_get(void) { return 100; }
int halo_linux_vsnprintf(char *buffer, size_t count, const char *format, va_list arguments)
{
	return vsnprintf(buffer, count, format, arguments);
}
int halo_linux_snprintf(char *buffer, size_t count, const char *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vsnprintf(buffer, count, format, arguments);
	va_end(arguments);
	return result;
}
char *csprintf(char *buffer, char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(buffer, sizeof(temporary), format, arguments);
	va_end(arguments);
	return buffer;
}
void *csmemcpy(void *destination, const void *source, unsigned int size) { return memmove(destination, source, size); }
void *csmemmove(void *destination, const void *source, unsigned int size) { return memmove(destination, source, size); }
void *csmemset(void *buffer, int c, unsigned int size) { return memset(buffer, c, size); }
int csmemcmp(const void *a, const void *b, unsigned int size) { return memcmp(a, b, size); }
char *csstrcpy(char *destination, const char *source) { return strcpy(destination, source); }
char *csstrncpy(char *s1, const char *s2, unsigned int size) { return strncpy(s1, s2, size); }
char *csstrcat(char *s1, const char *s2) { return strcat(s1, s2); }
int csstrcmp(const char *a, const char *b) { return strcmp(a, b); }
unsigned int csstrlen(const char *s) { return (unsigned int)strlen(s); }

/* (the game's wide text: 16-bit) */
unsigned int ustrlen(const game_wchar *s)
{
	unsigned int length = 0;

	while (s[length])
		length++;
	return length;
}
game_wchar *ustrncpy(game_wchar *destination, const game_wchar *source, unsigned int count)
{
	unsigned int index;

	for (index = 0; index < count && source[index]; index++)
		destination[index] = source[index];
	for (; index < count; index++)
		destination[index] = 0;
	return destination;
}
int ustrcmp(const game_wchar *a, const game_wchar *b)
{
	while (*a && *a == *b)
	{
		a++;
		b++;
	}
	return (int)*a - (int)*b;
}
char *wide_to_ascii(const game_wchar *source, char *destination, int size)
{
	int index;

	/* (in place, as the handlers call it: each character read before the
	byte it lands on is written) */
	for (index = 0; index < size - 1 && source[index]; index++)
		destination[index] = source[index] < 128 ? (char)source[index] : '?';
	if (size > 0)
		destination[index] = 0;
	return destination;
}
byte ascii_to_wide(const char *source, game_wchar *destination, int size)
{
	int index;
	int count = size / 2;

	for (index = 0; index < count - 1 && source[index]; index++)
		destination[index] = (byte)source[index];
	if (count > 0)
		destination[index] = 0;
	return 1;
}

/* (players.c's, as it reads the name: ended within count, printable, not
blank) */
char player_name_character_ascii(game_wchar character)
{
	return character >= 32 && character < 127 ? (char)character : '?';
}
byte player_name_clean(game_wchar *name, int count)
{
	int index;
	byte any = 0;

	for (index = 0; index < count - 1 && name[index]; index++)
	{
		if (name[index] < 32)
			name[index] = '?';
		if (name[index] != ' ')
			any = 1;
	}
	for (; index < count; index++)
		name[index] = 0;
	return any;
}

char *transport_address_to_string(void const *address)
{
	(void)address;
	return "10.0.0.1:2302";
}
char *transport_error_to_string(int error)
{
	(void)error;
	return "error";
}
void transport_get_nonce(byte *nonce, int length) { memset(nonce, 1, (size_t)length); }

void map_family_wire_name(const char *name, char *out, int size)
{
	snprintf(out, (size_t)size, "%s", name);
}
void map_family_from_wire_name(const char *name, char *out, int size)
{
	snprintf(out, (size_t)size, "%s", name);
}
char const *tag_name_strip_path(char const *name)
{
	return name;
}

/* (the client: the harness's; no server of its own) */
struct network_game_client *global_network_game_client_get(void) { return fuzz_client; }
byte network_connection_last_read_was_unreliable(void) { return fuzz_unreliable; }

/* (a connection: written to nowhere, its address its machine's) */
byte network_connection_write(void *connection, void const *data, int size, void const *address, int reliable)
{
	(void)connection;
	(void)data;
	(void)size;
	(void)address;
	(void)reliable;
	return 1;
}
byte network_connection_active(void *connection) { return connection != NULL; }
void network_connection_get_address(void *connection, struct transport_address *address, struct transport_address *unreliable)
{
	if (unreliable)
		memset(unreliable, 0, sizeof(*unreliable));
	memset(address, 0, sizeof(*address));
	address->address_length = 4;
	address->address.long_words[0] = connection ? HOST_ADDRESS : 0;
	address->port = 2302;
}

/* (the lobby's budget, Delta's join mark, map sharing and the distributed
netcode: their own targets) */
void network_distributed_handle_message(int machine_index, word const *message, word size)
{
	(void)machine_index;
	(void)message;
	(void)size;
}
int delta_legacy_announce(void) { return 39; }

/* ---------- the target */

int LLVMFuzzerTestOneInput(const byte *data, size_t size)
{
	static int initialized;
	size_t offset = 1;

	if (!initialized)
	{
		initialize_network_game_packets();
		initialized = 1;
	}
	if (size < 1)
		return 0;
	fuzz_client = client_messages_fuzz_reset(data[0] % 5);
	while (offset + 3 <= size)
	{
		byte control = data[offset];
		size_t length = (size_t)data[offset + 1] | (size_t)data[offset + 2] << 8;
		struct transport_address source;
		word *message;

		offset += 3;
		if (length > size - offset || length > MAXIMUM_PACKET_SIZE)
			break;
		/* (the message alone in its allocation: a read past it is reported) */
		message = malloc(2 + length);
		message[0] = (word)((2 + length) << 4 | (control & 8 ? MESSAGE_TYPE_DATA : MESSAGE_TYPE_PACKET) << 2);
		memcpy(message + 1, data + offset, length);
		offset += length;
		memset(&source, 0, sizeof(source));
		source.address_length = 4;
		source.address.long_words[0] = control & 1 ? HOST_ADDRESS + 1 : HOST_ADDRESS;
		source.port = 2302;
		fuzz_unreliable = (control & 4) != 0;
		network_game_client_handle_message(fuzz_client, message, (short)(2 + length), &source);
		free(message);
	}
	return 0;
}

/* (and the few whose answer is read) */
game_wchar const *network_game_get_random_player_name(void)
{
	static game_wchar const name[] = { 'R', 'a', 'n', 'd', 'o', 'm', 0 };

	return name;
}
void *transport_get_xnaddr(void *xnaddr)
{
	return xnaddr;
}
char const *main_get_multiplayer_map_name(void)
{
	return "levels\\test\\bloodgulch\\bloodgulch";
}
void p2p_hardware_id_sanitize(char *destination, int size, const char *source)
{
	snprintf(destination, (size_t)size, "%s", source ? source : "");
}
/* (the host's map is here and plays with others; its MCC identity, none,
is taken; every advertisement has this client's nonce) */
byte cache_files_map_present(char const *name, unsigned int version) { (void)name; (void)version; return 1; }
byte cache_files_map_plays_multiplayer(char const *name, char *build) { (void)name; build[0] = 0; return 1; }
byte mcc_identity_accept(void *game) { (void)game; return 1; }
byte transport_is_nonce(void const *data, int length) { (void)data; (void)length; return 1; }
int _strnicmp(const char *a, const char *b, size_t count)
{
	for (; count; count--, a++, b++)
	{
		int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
		int y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;

		if (x != y || !x)
			return x - y;
	}
	return 0;
}
/* (a network game's client) */
short game_connection(void) { return 1; }
