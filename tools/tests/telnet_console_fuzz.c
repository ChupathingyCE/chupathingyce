/*
TELNET_CONSOLE_FUZZ.C

libFuzzer's entry for the debug telnet console (source/networking/
telnet_console.c), as tools/test_host_fuzz.py builds it: each input is the
bytes one client sends, read as the game reads them (at most 32 bytes a
frame), through stand-ins for the transport and the script compiler.

What it checks besides the sanitizers: a client whose first line is an HTTP
request's (a method, a space, printable ASCII: what a web page can make a
browser send to this machine's port) runs no script, however long that line
is and whatever follows it.
*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the game's types and numbers, as source/cseries/cseries.h and
source/bungie_net/network/transport.h have them) */
typedef unsigned char boolean;
typedef unsigned short word;
struct transport_address;
struct transport_endpoint
{
	int which;
};

enum
{
	TRANSPORT_WOULD_BLOCK = -4,
	TRANSPORT_NONE = 0,
};

void telnet_console_initialize(void);
void telnet_console_dispose(void);
void telnet_console_process(void);

static struct transport_endpoint listener = { 1 };
static struct transport_endpoint client = { 2 };

static struct
{
	const uint8_t *data;
	size_t size;
	size_t offset;
	int accepted;
	int client_open;
	int http;
} state;

/* ---------- the game's, stood in for */

int config_boolean(const char *name)
{
	(void)name;
	return 1;
}

long config_integer(const char *name)
{
	(void)name;
	return 2323;
}

void *csmemset(void *buffer, long c, unsigned long size)
{
	return memset(buffer, (int)c, size);
}

unsigned long csstrlen(const char *s1)
{
	return strlen(s1);
}

long csstrncmp(const char *s1, const char *s2, unsigned long size)
{
	return strncmp(s1, s2, size);
}

char *csstrncpy(char *s1, const char *s2, unsigned long size)
{
	return strncpy(s1, s2, size);
}

void error(short priority, const char *format, ...)
{
	(void)priority;
	(void)format;
}

const char *transport_error_to_string(short code)
{
	(void)code;
	return "error";
}

struct transport_endpoint *create_transport_endpoint(long type)
{
	(void)type;
	return &listener;
}

short bind_endpoint(struct transport_endpoint *endpoint, struct transport_address *address)
{
	(void)endpoint;
	(void)address;
	return TRANSPORT_NONE;
}

short listen_endpoint(struct transport_endpoint *endpoint)
{
	(void)endpoint;
	return TRANSPORT_NONE;
}

struct transport_endpoint *accept_endpoint(struct transport_endpoint *listening_endpoint)
{
	(void)listening_endpoint;
	if (state.accepted)
		return NULL;
	state.accepted = 1;
	state.client_open = 1;
	return &client;
}

short set_endpoint_blocking(struct transport_endpoint *endpoint, long blocking)
{
	(void)endpoint;
	(void)blocking;
	return TRANSPORT_NONE;
}

boolean endpoint_readable(struct transport_endpoint *endpoint, word timeout)
{
	(void)timeout;
	if (endpoint == &listener)
		return !state.accepted;
	return state.client_open && state.offset < state.size;
}

long read_endpoint(struct transport_endpoint *endpoint, void *buffer, long size)
{
	size_t count = state.size - state.offset;

	if (endpoint != &client || !state.client_open)
		abort();
	if (!count)
		return TRANSPORT_WOULD_BLOCK;
	if (count > (size_t)size)
		count = (size_t)size;
	memcpy(buffer, state.data + state.offset, count);
	state.offset += count;
	return (long)count;
}

long write_endpoint(struct transport_endpoint *endpoint, const void *buffer, long size)
{
	(void)buffer;
	if (endpoint == &client && !state.client_open)
		abort();
	return size;
}

void delete_transport_endpoint(struct transport_endpoint *endpoint)
{
	if (endpoint == &client)
	{
		if (!state.client_open)
			abort();
		state.client_open = 0;
	}
}

boolean hs_compile_and_evaluate(const char *expression)
{
	/* (a browser's request: no script of it may run) */
	if (state.http)
	{
		fprintf(stderr, "a script ran from an HTTP request: \"%s\"\n", expression);
		abort();
	}
	return 1;
}

/* ---------- private code */

/* whether the input's first line is an HTTP request's, as a browser sends one */
static int http_request(const uint8_t *data, size_t size)
{
	static const char *const methods[] = { "GET ", "POST ", "PUT ", "HEAD ", "DELETE ", "OPTIONS ", "PATCH " };
	size_t index;
	int method = 0;

	for (index = 0; index < sizeof(methods) / sizeof(methods[0]); index++)
	{
		size_t length = strlen(methods[index]);

		if (size >= length && !memcmp(data, methods[index], length))
			method = 1;
	}
	if (!method)
		return 0;
	for (index = 0; index < size && data[index] != '\r' && data[index] != '\n'; index++)
	{
		if (data[index] < 0x20 || data[index] > 0x7e)
			return 0;
	}
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	int frame;

	memset(&state, 0, sizeof(state));
	state.data = data;
	state.size = size;
	state.http = http_request(data, size);
	telnet_console_initialize();
	for (frame = 0; frame < 100000 && (!state.accepted || (state.client_open && state.offset < state.size)); frame++)
		telnet_console_process();
	telnet_console_dispose();
	return 0;
}
