/*
LOG_ADDRESS.C

An IP address as the log may show it (log_address.h): a public one as a
tag of this run's, the rest as they are, all of them whole with
debug.log_addresses.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "p2p_internal.h"
#include "log_address.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

enum
{
	SALT_SIZE = 16,
	/* bytes of the hash in a tag (6 hexadecimal digits) */
	TAG_SIZE = 3,
};

static pthread_once_t salt_once = PTHREAD_ONCE_INIT;
static pthread_once_t whole_once = PTHREAD_ONCE_INIT;
static unsigned char salt[SALT_SIZE];

static void make_salt(void)
{
	posix_random_bytes(salt, sizeof(salt));
}

/* said once, before the first public address the log has whole: a log
written so is not one to post, and a server's operator should know why it
has them */
static void say_whole(void)
{
	platform_log("Log: debug.log_addresses is on (or HALO_LOG_ADDRESSES is set): other machines' internet "
		"addresses are written whole; do not post this log");
}

/* whether an address is no one's on the internet (log_address.h) */
static int local_address(const unsigned char *bytes, int length)
{
	if (length == 4)
	{
		return bytes[0] == 0 || bytes[0] == 10 || bytes[0] == 127 ||
			(bytes[0] == 172 && (bytes[1] & 0xF0) == 16) || (bytes[0] == 192 && bytes[1] == 168) ||
			(bytes[0] == 169 && bytes[1] == 254) || (bytes[0] == 100 && (bytes[1] & 0xC0) == 64);
	}
	else
	{
		static const unsigned char zero[15] = { 0 };

		/* ::1 and :: */
		if (!memcmp(bytes, zero, 15) && bytes[15] <= 1)
			return 1;
		/* fc00::/7 and fe80::/10 */
		return (bytes[0] & 0xFE) == 0xFC || (bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0x80);
	}
}

int log_address_local(const unsigned char *bytes, int length)
{
	return (length == 4 || length == 16) && local_address(bytes, length);
}

/* whether an address is this machine's own (loopback or unspecified): one
written as it is even where a local network's are tagged */
static int own_address(const unsigned char *bytes, int length)
{
	static const unsigned char zero[15] = { 0 };

	if (length == 4)
		return bytes[0] == 0 || bytes[0] == 127;
	return !memcmp(bytes, zero, 15) && bytes[15] <= 1;
}

/* an address as the log may show it; local_whole: a local network's (and
the other ranges local_address names) as it is, else as a tag too */
static const char *write_address(const unsigned char *bytes, int length, int port, char *text, int size,
	int local_whole)
{
	char address[48];
	int used;
	int whole;

	if (!text || size <= 0)
		return "";
	if (!bytes || (length != 4 && length != 16))
	{
		snprintf(text, (size_t)size, "unknown");
		return text;
	}
	whole = local_whole ? local_address(bytes, length) : own_address(bytes, length);
	if (whole || config_boolean("debug.log_addresses"))
	{
		if (!whole)
			pthread_once(&whole_once, say_whole);
		if (length == 4)
		{
			snprintf(address, sizeof(address), "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
		}
		else
		{
			int group;

			for (group = 0, used = 0; group < 8; group++)
			{
				used += snprintf(address + used, sizeof(address) - (size_t)used, "%s%x", group ? ":" : "",
					(unsigned int)(bytes[2 * group] << 8 | bytes[2 * group + 1]));
			}
		}
	}
	else
	{
		unsigned char data[SALT_SIZE + 1 + 16];
		unsigned char digest[P2P_SHA256_SIZE];
		char tag[2 * TAG_SIZE + 1];

		pthread_once(&salt_once, make_salt);
		memcpy(data, salt, SALT_SIZE);
		data[SALT_SIZE] = (unsigned char)length;
		memcpy(data + SALT_SIZE + 1, bytes, (size_t)length);
		p2p_sha256(data, SALT_SIZE + 1 + length, digest);
		p2p_hex(digest, TAG_SIZE, tag);
		snprintf(address, sizeof(address), "addr#%s", tag);
		/* (a tag has no colons: no brackets) */
		length = 4;
	}
	if (port < 0)
		snprintf(text, (size_t)size, "%s", address);
	else if (length == 16)
		snprintf(text, (size_t)size, "[%s]:%d", address, port);
	else
		snprintf(text, (size_t)size, "%s:%d", address, port);
	return text;
}

const char *log_address(const unsigned char *bytes, int length, int port, char *text, int size)
{
	return write_address(bytes, length, port, text, size, 1);
}

const char *log_address_ipv4(unsigned long network_address, unsigned short network_port, char *text, int size)
{
	/* (the 32 bits of a sockaddr_in's address, in its byte order) */
	unsigned int value = (unsigned int)network_address;
	unsigned char bytes[4];
	unsigned char port[2];

	memcpy(bytes, &value, 4);
	memcpy(port, &network_port, 2);
	return log_address(bytes, 4, network_port ? (port[0] << 8 | port[1]) : -1, text, size);
}

const char *log_address_ipv4_peer(unsigned long network_address, unsigned short network_port, char *text, int size)
{
	unsigned int value = (unsigned int)network_address;
	unsigned char bytes[4];
	unsigned char port[2];

	memcpy(bytes, &value, 4);
	memcpy(port, &network_port, 2);
	return write_address(bytes, 4, network_port ? (port[0] << 8 | port[1]) : -1, text, size, 0);
}

const char *log_address_name(const char *name, char *text, int size)
{
	unsigned char data[SALT_SIZE + 1 + 256];
	unsigned char digest[P2P_SHA256_SIZE];
	char tag[2 * TAG_SIZE + 1];
	size_t length;

	if (!text || size <= 0)
		return "";
	if (!name || !*name)
	{
		snprintf(text, (size_t)size, "unknown");
		return text;
	}
	length = strlen(name);
	if (!strcmp(name, "localhost") || config_boolean("debug.log_addresses"))
	{
		if (strcmp(name, "localhost"))
			pthread_once(&whole_once, say_whole);
		snprintf(text, (size_t)size, "%s", name);
		return text;
	}
	if (length > 256)
		length = 256;
	pthread_once(&salt_once, make_salt);
	memcpy(data, salt, SALT_SIZE);
	/* (a name's tag apart from an address's of the same bytes) */
	data[SALT_SIZE] = 0xFF;
	memcpy(data + SALT_SIZE + 1, name, length);
	p2p_sha256(data, SALT_SIZE + 1 + (int)length, digest);
	p2p_hex(digest, TAG_SIZE, tag);
	snprintf(text, (size_t)size, "name#%s", tag);
	return text;
}
