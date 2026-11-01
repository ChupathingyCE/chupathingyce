/*
DELTA_CHAT_BRIDGE.C

The bridge between Delta chat and OpenCE's in-game text chat: the rules
that need no game (delta_chat_bridge.h).
*/

#include "delta_chat_bridge.h"
#include "delta_chat.h"
#include "delta_peer.h"

#include <string.h>

/* ---------- the entries */

void delta_chat_bridge_write_line(unsigned char *entry, const struct delta_chat_bridge_line *line)
{
	int length;

	memset(entry, 0, DELTA_CHAT_BRIDGE_LINE_SIZE);
	entry[0] = line->player;
	entry[1] = line->flags;
	for (length = 0; length < DELTA_CHAT_BRIDGE_TEXT_SIZE - 1 && line->text[length]; length++)
		entry[4 + length] = (unsigned char)line->text[length];
}

int delta_chat_bridge_read_line(const unsigned char *entry, int size, struct delta_chat_bridge_line *line)
{
	memset(line, 0, sizeof(*line));
	if (size < DELTA_CHAT_BRIDGE_LINE_SIZE)
		return 0;
	line->player = entry[0];
	line->flags = entry[1];
	return delta_chat_bridge_clean((const char *)entry + 4, DELTA_CHAT_BRIDGE_TEXT_SIZE, line->text) > 0;
}

void delta_chat_bridge_write_config(unsigned char *entry, int flags)
{
	memset(entry, 0, DELTA_CHAT_BRIDGE_CONFIG_SIZE);
	entry[0] = (unsigned char)flags;
}

int delta_chat_bridge_read_config(const unsigned char *entry, int size)
{
	return size < DELTA_CHAT_BRIDGE_CONFIG_SIZE ? -1 : entry[0];
}

/* ---------- the text */

int delta_chat_bridge_clean(const char *text, int size, char *out)
{
	int length = 0;
	int index;

	for (index = 0; index < size && text[index] && length < DELTA_CHAT_BRIDGE_TEXT_SIZE - 1; index++)
	{
		char character = text[index];

		if (character < ' ' || character > '~' || character == '|')
			character = '?';
		if (character == ' ' && !length)
			continue;
		out[length++] = character;
	}
	while (length > 0 && out[length - 1] == ' ')
		length--;
	out[length] = 0;
	return length;
}

/* a code point as ASCII: itself, a letter without its accent, a quote, dash
or space's plain one, or '?' */
static char ascii_character(delta_u32 code)
{
	/* (Latin-1's letters, U+00C0 to U+00FF, without their accents) */
	static const char latin1[] =
		"AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs"
		"aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";

	if (code >= ' ' && code <= '~')
		return (char)code;
	if (code >= 0xC0 && code <= 0xFF)
		return latin1[code - 0xC0];
	switch (code)
	{
	case 0xA0: case 0x2002: case 0x2003: case 0x2009: return ' ';
	case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
	case 0x201C: case 0x201D: case 0x201E: case 0x2033: return '"';
	case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return '-';
	case 0x2026: return '.';
	default: return '?';
	}
}

int delta_chat_bridge_to_ascii(const char *text, int length, char *out)
{
	const unsigned char *bytes = (const unsigned char *)text;
	char ascii[DELTA_WIRE_CHAT_TEXT_SIZE + 1];
	int count = 0;
	int index = 0;

	while (index < length && count < (int)sizeof(ascii) - 1)
	{
		delta_u32 code = bytes[index];
		int more = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : code >= 0xC0 ? 1 : 0;
		int step;

		if (more)
			code &= more == 1 ? 0x1F : more == 2 ? 0x0F : 0x07;
		for (step = 1; step <= more && index + step < length && (bytes[index + step] & 0xC0) == 0x80; step++)
			code = (code << 6) | (bytes[index + step] & 0x3F);
		/* (a sequence cut short, or a stray continuation byte: one '?') */
		if (step <= more || (bytes[index] & 0xC0) == 0x80)
			code = 0xFFFD;
		index += step;
		/* (an ellipsis is three dots) */
		if (code == 0x2026 && count < (int)sizeof(ascii) - 3)
		{
			ascii[count++] = '.';
			ascii[count++] = '.';
		}
		ascii[count++] = ascii_character(code);
	}
	ascii[count] = 0;
	return delta_chat_bridge_clean(ascii, count, out);
}

/* ---------- the rules */

int delta_chat_bridge_scope(int flags)
{
	return flags & (DELTA_CHAT_BRIDGE_TEAM | DELTA_CHAT_BRIDGE_VEHICLE) ? _delta_chat_scope_team :
		_delta_chat_scope_all;
}

int delta_chat_bridge_flags(int scope)
{
	return scope == _delta_chat_scope_team ? DELTA_CHAT_BRIDGE_TEAM : 0;
}

int delta_chat_bridge_host_config(int mode, int legacy)
{
	/* (Delta chat has no rule against links: they are allowed here too) */
	if (mode == _delta_chat_mode_off || !legacy)
		return 0;
	return DELTA_CHAT_BRIDGE_ALLOWED | DELTA_CHAT_BRIDGE_LINKS;
}

int delta_chat_bridge_take(struct delta_chat_bridge_budget *budget, delta_u32 now, delta_u32 cooldown,
	const char *text, int length, int *paused_seconds)
{
	*paused_seconds = 0;
	if (budget->paused)
	{
		delta_u32 left = budget->paused_until - now;

		if (left && left < 0x80000000u)
		{
			*paused_seconds = (int)((left + 999) / 1000);
			return _delta_chat_refused_too_fast;
		}
		budget->paused = 0;
	}
	if (!delta_rate_take(&budget->rate, now, DELTA_PEER_CHAT_SAY_RATE, DELTA_PEER_CHAT_SAY_BURST))
	{
		if (cooldown)
		{
			budget->paused = 1;
			budget->paused_until = now + cooldown;
			*paused_seconds = (int)((cooldown + 999) / 1000);
		}
		return _delta_chat_refused_too_fast;
	}
	if (budget->has_last && budget->last_fingerprint == delta_chat_fingerprint(text, length) &&
		now - budget->last_time < DELTA_CHAT_REPEAT_TIME)
	{
		return _delta_chat_refused_too_fast;
	}
	return _delta_chat_refused_none;
}

void delta_chat_bridge_said(struct delta_chat_bridge_budget *budget, delta_u32 now, const char *text, int length)
{
	budget->has_last = 1;
	budget->last_fingerprint = delta_chat_fingerprint(text, length);
	budget->last_time = now;
}

int delta_chat_bridge_client_allowed(int known, int flags)
{
	return known && (flags & DELTA_CHAT_BRIDGE_ALLOWED);
}
