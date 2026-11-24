/*
DIRECT_ADDRESS.H

A game's address as a player types it to join it directly (Direct IP, the
console's connect: port/linux/game/direct_join.c): an IPv4 address, an IPv6
address (bare, or in brackets with a port after), or a host name, then an
optional :port. Parsed here with no sockets or game, so that the unit test
(port/linux/tests/direct_address_test.c) runs it as the game does.

Also how a typed address is shown once it is entered: a host name as it
is, anything else never again (a star for each of its characters, as a
password is), and a saved one by the name its player gave it, else "Saved
server <n>" (never_show_ips: the menus, the HUD, screenshots).

Plain C99, header only: every build that joins by address includes it.
*/

#ifndef DIRECT_ADDRESS_H
#define DIRECT_ADDRESS_H

#include <stdio.h>
#include <string.h>

enum
{
	/* a host name's longest (RFC 1035), with its terminator */
	DIRECT_ADDRESS_HOST_SIZE = 254,
	/* the longest text taken: a host name and a port */
	DIRECT_ADDRESS_TEXT_SIZE = DIRECT_ADDRESS_HOST_SIZE + 8,
};

enum direct_address_kind
{
	_direct_address_ipv4 = 1,
	_direct_address_ipv6,
	_direct_address_name,
};

enum direct_address_result
{
	_direct_address_ok = 0,
	/* nothing typed */
	_direct_address_empty,
	/* not an address, nor a host name */
	_direct_address_bad,
	/* a port that is not 1 to 65535 */
	_direct_address_bad_port,
};

struct direct_address
{
	enum direct_address_kind kind;
	/* IPv4: first byte highest (1.2.3.4 is 0x01020304) */
	unsigned long ipv4;
	/* IPv6: its 16 bytes, first byte first */
	unsigned char ipv6[16];
	/* a host name, lowercase */
	char host[DIRECT_ADDRESS_HOST_SIZE];
	/* 0: none typed (the game's port) */
	unsigned short port;
};

/* (one decimal number of 1 to 5 digits, no sign, up to maximum) */
static inline int direct_address_number(const char *text, size_t length, unsigned long maximum, unsigned long *value)
{
	size_t index;

	*value = 0;
	if (!length || length > 5)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (text[index] < '0' || text[index] > '9')
			return 0;
		*value = *value * 10 + (unsigned long)(text[index] - '0');
	}
	return *value <= maximum;
}

/* a dotted quad filling text's length: 1 with *address */
static inline int direct_address_ipv4(const char *text, size_t length, unsigned long *address)
{
	unsigned long value = 0;
	size_t start = 0;
	int part;

	for (part = 0; part < 4; part++)
	{
		size_t end = start;
		unsigned long number;

		while (end < length && text[end] != '.')
			end++;
		if (end - start > 3 || !direct_address_number(text + start, end - start, 255, &number))
			return 0;
		value = value << 8 | number;
		if (part < 3)
		{
			if (end >= length)
				return 0;
			start = end + 1;
		}
		else if (end != length)
			return 0;
	}
	*address = value & 0xFFFFFFFFUL;
	return 1;
}

/* an IPv6 address filling text's length (hex groups, one "::", and an IPv4
address as its last 32 bits allowed): 1 with bytes */
static inline int direct_address_ipv6(const char *text, size_t length, unsigned char *bytes)
{
	unsigned short groups[8];
	int count = 0;
	int gap = -1;
	size_t at = 0;
	int index;

	if (length < 2 || length > 45)
		return 0;
	if (text[0] == ':')
	{
		if (text[1] != ':')
			return 0;
		gap = 0;
		at = 2;
		if (at == length)
		{
			memset(bytes, 0, 16);
			return 1;
		}
	}
	while (at < length)
	{
		size_t end = at;
		unsigned long value = 0;

		while (end < length && text[end] != ':' && text[end] != '.')
			end++;
		/* (the last 32 bits as an IPv4 address) */
		if (end < length && text[end] == '.')
		{
			unsigned long ipv4;

			if (count > 6 || !direct_address_ipv4(text + at, length - at, &ipv4))
				return 0;
			groups[count++] = (unsigned short)(ipv4 >> 16);
			groups[count++] = (unsigned short)(ipv4 & 0xFFFF);
			at = length;
			break;
		}
		if (end == at || end - at > 4 || count >= 8)
			return 0;
		for (index = 0; index < (int)(end - at); index++)
		{
			char character = text[at + (size_t)index];
			int digit = character >= '0' && character <= '9' ? character - '0' :
				character >= 'a' && character <= 'f' ? character - 'a' + 10 :
				character >= 'A' && character <= 'F' ? character - 'A' + 10 : -1;

			if (digit < 0)
				return 0;
			value = value << 4 | (unsigned long)digit;
		}
		groups[count++] = (unsigned short)value;
		at = end;
		if (at < length)
		{
			/* (":" then a group, or "::" once) */
			at++;
			if (at < length && text[at] == ':')
			{
				if (gap >= 0)
					return 0;
				gap = count;
				at++;
			}
			else if (at == length)
				return 0;
		}
	}
	if (gap < 0 ? count != 8 : count > 7)
		return 0;
	memset(bytes, 0, 16);
	for (index = 0; index < count; index++)
	{
		int place = gap >= 0 && index >= gap ? index + (8 - count) : index;

		bytes[2 * place] = (unsigned char)(groups[index] >> 8);
		bytes[2 * place + 1] = (unsigned char)(groups[index] & 0xFF);
	}
	return 1;
}

/* a host name filling text's length: labels of letters, digits and '-'
(not first or last), 1 to 63 long, dots between them, a dot after the
last allowed; not all digits and dots (a bad IPv4 address is not a name) */
static inline int direct_address_name(const char *text, size_t length)
{
	size_t at = 0;
	int letters = 0;

	if (length && text[length - 1] == '.')
		length--;
	if (!length || length > DIRECT_ADDRESS_HOST_SIZE - 1)
		return 0;
	while (at < length)
	{
		size_t end = at;

		while (end < length && text[end] != '.')
		{
			char character = text[end];

			if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z'))
				letters = 1;
			else if (character == '-')
			{
				if (end == at || end + 1 == length || text[end + 1] == '.')
					return 0;
			}
			else if (character < '0' || character > '9')
				return 0;
			end++;
		}
		if (end == at || end - at > 63)
			return 0;
		at = end + 1;
	}
	return letters;
}

/* text as an address (spaces around it allowed): _direct_address_ok with
*address, else what was wrong */
static inline enum direct_address_result direct_address_parse(const char *text, struct direct_address *address)
{
	size_t start = 0, length, host_length, colons = 0, index;
	const char *host;
	const char *port_text = NULL;
	size_t port_length = 0;

	memset(address, 0, sizeof(*address));
	if (!text)
		return _direct_address_empty;
	length = strlen(text);
	while (start < length && (text[start] == ' ' || text[start] == '\t'))
		start++;
	while (length > start && (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r' ||
		text[length - 1] == '\n'))
	{
		length--;
	}
	if (start == length)
		return _direct_address_empty;
	if (length - start >= DIRECT_ADDRESS_TEXT_SIZE)
		return _direct_address_bad;
	host = text + start;
	host_length = length - start;
	for (index = 0; index < host_length; index++)
		colons += host[index] == ':';
	/* [IPv6] or [IPv6]:port */
	if (host[0] == '[')
	{
		const char *close = memchr(host, ']', host_length);

		if (!close)
			return _direct_address_bad;
		if ((size_t)(close - host) + 1 < host_length)
		{
			if (close[1] != ':')
				return _direct_address_bad;
			port_text = close + 2;
			port_length = host_length - (size_t)(close - host) - 2;
		}
		host_length = (size_t)(close - host) - 1;
		host++;
		if (!direct_address_ipv6(host, host_length, address->ipv6))
			return _direct_address_bad;
		address->kind = _direct_address_ipv6;
	}
	/* a bare IPv6 address (two colons or more: no port) */
	else if (colons >= 2)
	{
		if (!direct_address_ipv6(host, host_length, address->ipv6))
			return _direct_address_bad;
		address->kind = _direct_address_ipv6;
	}
	else
	{
		if (colons == 1)
		{
			const char *colon = memchr(host, ':', host_length);

			port_text = colon + 1;
			port_length = host_length - (size_t)(colon - host) - 1;
			host_length = (size_t)(colon - host);
		}
		if (direct_address_ipv4(host, host_length, &address->ipv4))
			address->kind = _direct_address_ipv4;
		else if (direct_address_name(host, host_length))
		{
			address->kind = _direct_address_name;
			if (host[host_length - 1] == '.')
				host_length--;
			for (index = 0; index < host_length; index++)
			{
				char character = host[index];

				address->host[index] = character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') :
					character;
			}
			address->host[host_length] = 0;
		}
		else
			return _direct_address_bad;
	}
	/* (an IPv4 address in IPv6's form, ::ffff:a.b.c.d: the IPv4 address) */
	if (address->kind == _direct_address_ipv6)
	{
		static const unsigned char mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };

		if (!memcmp(address->ipv6, mapped, sizeof(mapped)))
		{
			address->kind = _direct_address_ipv4;
			address->ipv4 = (unsigned long)address->ipv6[12] << 24 | (unsigned long)address->ipv6[13] << 16 |
				(unsigned long)address->ipv6[14] << 8 | (unsigned long)address->ipv6[15];
			memset(address->ipv6, 0, sizeof(address->ipv6));
		}
	}
	if (port_text)
	{
		unsigned long port;

		/* (a link, scheme:// or a path, is no address with a port) */
		if (memchr(port_text, '/', port_length))
			return _direct_address_bad;
		if (!direct_address_number(port_text, port_length, 65535, &port) || !port)
			return _direct_address_bad_port;
		address->port = (unsigned short)port;
	}
	return _direct_address_ok;
}

/* whether a typed text may be shown again once entered: only a host name
(its port may be a bad one). An address is never shown, nor anything that
is not a host name: a link ("http://203.0.113.45"), an address with a port
too many ("1.2.3.4:5:6"), text too long to be one; each may hold an
address */
static inline int direct_address_shown_as_typed(const char *text)
{
	struct direct_address address;
	enum direct_address_result result = direct_address_parse(text, &address);

	return (result == _direct_address_ok || result == _direct_address_bad_port) &&
		address.kind == _direct_address_name;
}

/* text as a field shows it once entered: a host name as it is (its port
too), anything else a star for each character (as a password's).
Writes shown (size bytes) and returns it */
static inline const char *direct_address_shown(const char *text, char *shown, size_t size)
{
	size_t index, length;

	if (!size)
		return shown;
	if (!text || !*text)
	{
		shown[0] = 0;
		return shown;
	}
	if (direct_address_shown_as_typed(text))
	{
		snprintf(shown, size, "%s", text);
		return shown;
	}
	length = strlen(text);
	for (index = 0; index < length && index + 1 < size; index++)
		shown[index] = text[index] == ' ' ? ' ' : '*';
	shown[index] = 0;
	return shown;
}

/* a saved address's name in a list: the name its player gave it, else a
host name as it is, else "Saved server <number>" (1 the first) */
static inline const char *direct_address_saved_name(const char *name, const char *text, int number, char *shown,
	size_t size)
{
	if (name && *name)
		snprintf(shown, size, "%s", name);
	else if (text && *text && direct_address_shown_as_typed(text))
		snprintf(shown, size, "%s", text);
	else
		snprintf(shown, size, "Saved server %d", number);
	return shown;
}

#endif
