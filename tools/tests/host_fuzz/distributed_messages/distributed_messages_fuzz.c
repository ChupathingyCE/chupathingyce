/* distributed_messages_fuzz.c: what a host sends a client in game, and a
client its host, the distributed netcode's messages (port/linux/NETCODE.md),
through the receiver's own handlers (port/linux/game/network_distributed.c's
network_distributed_handle_message, and network_objects.c,
network_damage.c, network_actors.c and network_coop.c under it), as the game
builds them (tools/test_host_fuzz.py, "game_sources").

The client's world is made for each input (world_setup.c, at the end of
network_objects.c): four players on a biped each (player 0 this machine's),
a vehicle, two weapons and a scenery object, every one the host's object
this machine has. No map is loaded: what reads a tag answers as with none
(server_stubs-style stubs, distributed_stubs.c). The input:

	byte 0: bits 0-6 the game time's low bits (the messages' ticks are near
	        it), bit 7 this machine the host (machine 0) and the messages its
	        clients' (machines 1 to 3, the players 1 to 3 theirs)
	then messages, each:
	byte:   bit 0 a datagram rather than over the sender's stream, bit 1 the
	        world's object handles put in (below), bits 2-3 the client
	        machine that sends it (1 to 3; 0 is 1)
	2 bytes: its length after the message header (little-endian, at most
	        4093): the distributed header's type, count and tick, then the
	        entries
	that many bytes

With bit 1, each 4 bytes 0x7F 0x7F 0x00 n in the entries are the world's
object n's handle (n modulo 8), and 0x7E 0x7E 0x00 n player n's: the
handles' identifiers are the data arrays' own. Every `long` of the game is
32 bits: int here. */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char byte;
typedef unsigned short word;

enum
{
	MAXIMUM_PACKET_SIZE = 0xFFF - 2,
	MESSAGE_TYPE_DATA = 2,
	NONE = -1,
	FUZZ_GAME_TIME = 3000,
};

/* (world_setup.c) */
void distributed_messages_fuzz_world_reset(void);
int distributed_messages_fuzz_object_handle(int which);
int distributed_messages_fuzz_player_handle(int which);
extern int distributed_messages_fuzz_local_player;

/* (the units) */
void network_distributed_handle_message(int machine_index, word const *message, word size);
void network_distributed_new_game(void);
void network_objects_new_game(void);
void network_distributed_handle_stream_message(int machine_index, word const *message, word size);

static int fuzz_time = FUZZ_GAME_TIME;
static short fuzz_connection = 1;
static byte fuzz_unreliable;
static int fuzz_asserted;

/* ---------- what the units need of the rest of the game: what they do
with it here */

char temporary[256];
unsigned int xbox_address_space_base;
char D3D__RenderState[4096];

/* (the game's globals: a scenario, HUD and cinematics of nothing, the world's
directions; no effects) */
static long long fuzz_zero[65536];
static float const fuzz_origin[3] = { 0.0f, 0.0f, 0.0f };
static float const fuzz_forward[3] = { 1.0f, 0.0f, 0.0f };
void *global_scenario = fuzz_zero;
float global_gravity = 0.00357f;
void const *global_origin3d = fuzz_origin;
void const *global_forward3d = fuzz_forward;
void const *global_zero_vector3d = fuzz_origin;
void *cinematic_globals = fuzz_zero;
void *hud_globals = fuzz_zero;
void *hud_scripted_globals = fuzz_zero;
void *effect_data;
short global_structure_bsp_index;
short global_current_collision_users[16];
short global_current_collision_user_depth;
void *system_malloc(unsigned int size) { return malloc(size); }

void display_assert(char *information, char *file, int line, byte fatal)
{
	/* (an assertion the wire reaches: a release build notes it and carries
	on, release_assert_failed; so does this, the checked form's system_exit
	after it let go, and the sanitizers say what comes of it) */
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

void xbox_address_out_of_range(void const *pointer)
{
	fprintf(stderr, "a pointer outside the Xbox address space: %p\n", pointer);
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

void network_event(const char *format, ...)
{
	(void)format;
}

unsigned int system_milliseconds(void)
{
	static unsigned int clock;

	return clock += 7;
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
int halo_linux_vsnprintf(char *buffer, size_t count, const char *format, va_list arguments)
{
	return vsnprintf(buffer, count, format, arguments);
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
int csstrcmp(const char *a, const char *b) { return strcmp(a, b); }
int csstrncmp(const char *a, const char *b, unsigned int size) { return strncmp(a, b, size); }
unsigned int csstrlen(const char *s) { return (unsigned int)strlen(s); }

/* (a client in a game in progress, at the host's time) */
short game_connection(void) { return fuzz_connection; }
byte game_in_progress(void) { return 1; }
byte game_engine_running(void) { return 1; }
int game_time_get(void) { return fuzz_time; }
byte network_connection_last_read_was_unreliable(void) { return fuzz_unreliable; }
byte network_game_distributed_client(void) { return fuzz_connection == 1; }
int local_player_get_player_index(short local_player_index)
{
	return local_player_index == 0 ? distributed_messages_fuzz_local_player : NONE;
}
short local_player_get_next(short local_player_index)
{
	return local_player_index < 0 ? 0 : NONE;
}
int player_index_from_unit_index(int unit_index)
{
	(void)unit_index;
	return NONE;
}

/* ---------- the target */

static void fuzz_put_handles(byte *entries, size_t size)
{
	size_t offset;

	for (offset = 0; offset + 4 <= size; offset++)
	{
		int handle;

		if (entries[offset + 2] != 0 || entries[offset] != entries[offset + 1])
			continue;
		if (entries[offset] == 0x7F)
			handle = distributed_messages_fuzz_object_handle(entries[offset + 3]);
		else if (entries[offset] == 0x7E)
			handle = distributed_messages_fuzz_player_handle(entries[offset + 3]);
		else
			continue;
		memcpy(entries + offset, &handle, 4);
		offset += 3;
	}
}

int LLVMFuzzerTestOneInput(const byte *data, size_t size)
{
	size_t offset = 1;

	if (size < 1)
		return 0;
	fuzz_time = FUZZ_GAME_TIME + (data[0] & 0x7F);
	fuzz_connection = data[0] & 0x80 ? 2 : 1;
	distributed_messages_fuzz_world_reset();
	network_objects_new_game();
	network_distributed_new_game();
	while (offset + 3 <= size)
	{
		byte control = data[offset];
		size_t length = (size_t)data[offset + 1] | (size_t)data[offset + 2] << 8;
		word *message;

		offset += 3;
		if (length > size - offset || length > MAXIMUM_PACKET_SIZE)
			break;
		/* (the message alone in its allocation: a read past it is reported) */
		message = malloc(2 + length);
		message[0] = (word)((2 + length) << 4 | MESSAGE_TYPE_DATA << 2);
		memcpy(message + 1, data + offset, length);
		offset += length;
		/* (the entries after the 6 bytes of type, count and tick) */
		if ((control & 2) && length > 6)
			fuzz_put_handles((byte *)(message + 1) + 6, length - 6);
		fuzz_unreliable = control & 1;
		if (fuzz_connection == 1)
			network_distributed_handle_message(NONE, message, (word)(2 + length));
		else if (control & 1)
			network_distributed_handle_message((control >> 2 & 3) ? control >> 2 & 3 : 1, message, (word)(2 + length));
		else
			network_distributed_handle_stream_message((control >> 2 & 3) ? control >> 2 & 3 : 1, message,
				(word)(2 + length));
		free(message);
	}
	return 0;
}
