/*
MAP_SHARE_WIRE.C

What map sharing's two sides write and read (map_share.h). Every number on
the wire is little-endian; the bodies are written byte by byte here, so a
big-endian build's are the same and the transport turns only the message
header word, as it does for every message.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "bungie_net/common/message_header.h"
#include "map_share.h"

#include <string.h>

/* ---------- public code */

void map_share_put16(
	unsigned char *at,
	unsigned int value)
{
	at[0] = (unsigned char)value;
	at[1] = (unsigned char)(value >> 8);
}

void map_share_put32(
	unsigned char *at,
	unsigned long value)
{
	at[0] = (unsigned char)value;
	at[1] = (unsigned char)(value >> 8);
	at[2] = (unsigned char)(value >> 16);
	at[3] = (unsigned char)(value >> 24);
}

unsigned int map_share_get16(
	unsigned char const *at)
{
	return (unsigned int)at[0] | ((unsigned int)at[1] << 8);
}

unsigned long map_share_get32(
	unsigned char const *at)
{
	return (unsigned long)at[0] | ((unsigned long)at[1] << 8) | ((unsigned long)at[2] << 16) |
		((unsigned long)at[3] << 24);
}

short map_share_message_build(
	unsigned char *message,
	unsigned char type,
	unsigned char const *body,
	short body_size,
	long game_time)
{
	short size = (short)(MAP_SHARE_DISTRIBUTED_HEADER_SIZE + body_size);
	word header = 0;

	/* (the message header word in this machine's order: the write turns it;
	the distributed kind's class, 2) */
	build_message_header(&header, (word)size, 2, 0);
	csmemcpy(message, &header, sizeof(header));
	message[2] = type;
	message[3] = 1;
	map_share_put32(message + 4, (unsigned long)game_time);
	if (body_size > 0)
		csmemcpy(message + MAP_SHARE_DISTRIBUTED_HEADER_SIZE, body, body_size);
	return size;
}

boolean map_share_message_parse(
	void const *message,
	long size,
	unsigned char *type,
	unsigned char const **body,
	long *body_size)
{
	unsigned char const *bytes = (unsigned char const *)message;

	if (!message || size < MAP_SHARE_DISTRIBUTED_HEADER_SIZE)
		return FALSE;
	if (bytes[2] < MAP_SHARE_MESSAGE_REQUEST || bytes[2] > MAP_SHARE_MESSAGE_PROGRESS)
		return FALSE;
	*type = bytes[2];
	*body = bytes + MAP_SHARE_DISTRIBUTED_HEADER_SIZE;
	*body_size = size - MAP_SHARE_DISTRIBUTED_HEADER_SIZE;
	return TRUE;
}

boolean map_share_level_parse(
	unsigned char const *field,
	char *level)
{
	long length = 0;

	while (length < MAP_SHARE_LEVEL_SIZE && field[length])
		length++;
	if (length < 1 || length >= MAP_SHARE_LEVEL_SIZE)
		return FALSE;
	csmemcpy(level, field, length);
	level[length] = 0;
	return TRUE;
}

unsigned long map_share_hash_version(
	unsigned char const *hash)
{
	unsigned long version = ((unsigned long)hash[0] << 24) | ((unsigned long)hash[1] << 16) |
		((unsigned long)hash[2] << 8) | (unsigned long)hash[3];

	return version ? version : 1;
}

boolean map_share_level_file(
	char const *level,
	char *file)
{
	static char const prefix[] = "custom_maps\\";
	static char const *const devices[] = { "con", "prn", "aux", "nul", "com", "lpt" };
	char base[8];
	size_t length;
	size_t index;

	if (!level || strncmp(level, prefix, sizeof(prefix) - 1))
		return FALSE;
	level += sizeof(prefix) - 1;
	length = strlen(level);
	if (length < 1 || length > 51)
		return FALSE;
	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)level[index];

		/* letters, digits, _ . and - alone: the name reaches file paths and
		the map bot's copy, never a shell, but nothing else is needed */
		if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '_' || character == '.' || character == '-'))
		{
			return FALSE;
		}
	}
	if (level[0] == '.' || level[0] == ' ' || level[length - 1] == '.' || level[length - 1] == ' ')
		return FALSE;
	/* (a device's name before the first dot: CON, NUL, COM1, ...) */
	for (index = 0; index < sizeof(base) - 1 && level[index] && level[index] != '.'; index++)
		base[index] = (char)((level[index] >= 'A' && level[index] <= 'Z') ? level[index] + 32 : level[index]);
	base[index] = 0;
	for (index = 0; index < NUMBEROF(devices); index++)
	{
		size_t device_length = strlen(devices[index]);

		if (!strncmp(base, devices[index], device_length) &&
			(base[device_length] == 0 ||
				(device_length == 3 && (index >= 4) && base[3] >= '0' && base[3] <= '9' && base[4] == 0)))
		{
			return FALSE;
		}
	}
	csmemcpy(file, level, length + 1);
	return TRUE;
}

#endif /* HALO_GAME_BROWSER */
