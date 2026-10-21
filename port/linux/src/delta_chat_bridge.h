/*
DELTA_CHAT_BRIDGE.H

The bridge between Delta chat (docs/delta.md, Chat) and OpenCE's in-game
text chat: OpenCE's three distributed messages (their wire format as their
CC0 version of 2026-10-18 has it), the text's conversion each way, and the
host's checks of a line an OpenCE machine says. Plain C with no platform
dependency and no allocation, as delta_chat.c, so the tests
(port/linux/tests/delta_test.c) build it as it is; the game's side is
port/linux/game/chat.c.

OpenCE's chat goes over the game's distributed messages: a client's line to
its host as chat_up, the host's lines to each machine that hears them as
chat_down (a player's, or the host's notice, with no player), and the host's
settings as chat_config. Each message is the distributed header and one
entry. Text is printable ASCII without "|", at most 63 characters.
*/

#ifndef HALO_DELTA_CHAT_BRIDGE_H
#define HALO_DELTA_CHAT_BRIDGE_H

#include "delta_wire.h"

enum
{
	/* the distributed message types */
	DELTA_CHAT_BRIDGE_UP = 87,
	DELTA_CHAT_BRIDGE_DOWN = 88,
	DELTA_CHAT_BRIDGE_CONFIG = 89,

	/* the entries' sizes: a line (the player's absolute index, flags, two
	bytes of 0, and 64 bytes of text, ended or not) and the settings (flags,
	three bytes of 0) */
	DELTA_CHAT_BRIDGE_LINE_SIZE = 68,
	DELTA_CHAT_BRIDGE_TEXT_SIZE = 64,
	DELTA_CHAT_BRIDGE_CONFIG_SIZE = 4,

	/* a line's flags: to the speaker's team, to those in the speaker's
	vehicle, the host's notice (its text alone, no player) */
	DELTA_CHAT_BRIDGE_TEAM = 1 << 0,
	DELTA_CHAT_BRIDGE_VEHICLE = 1 << 1,
	DELTA_CHAT_BRIDGE_NOTICE = 1 << 2,

	/* the settings' flags: its players may chat, and send links */
	DELTA_CHAT_BRIDGE_ALLOWED = 1 << 0,
	DELTA_CHAT_BRIDGE_LINKS = 1 << 1
};

struct delta_chat_bridge_line
{
	unsigned char player;
	unsigned char flags;
	/* printable ASCII, ended (at most DELTA_CHAT_BRIDGE_TEXT_SIZE - 1) */
	char text[DELTA_CHAT_BRIDGE_TEXT_SIZE];
};

/* the host: one OpenCE machine's budget, as Delta Peer keeps a session's
(DELTA_PEER_CHAT_SAY_RATE and _BURST, the cooldown past them, and the same
line again within DELTA_CHAT_REPEAT_TIME) */
struct delta_chat_bridge_budget
{
	struct delta_rate rate;
	int paused;
	delta_u32 paused_until;
	int has_last;
	delta_u32 last_fingerprint;
	delta_u32 last_time;
};

/* a line's entry written (DELTA_CHAT_BRIDGE_LINE_SIZE bytes) and read: the
text read is cleaned (delta_chat_bridge_clean); 0 if it is empty */
void delta_chat_bridge_write_line(unsigned char *entry, const struct delta_chat_bridge_line *line);
int delta_chat_bridge_read_line(const unsigned char *entry, int size, struct delta_chat_bridge_line *line);
/* the settings' entry (DELTA_CHAT_BRIDGE_CONFIG_SIZE bytes), and read: its
flags, or -1 when too short */
void delta_chat_bridge_write_config(unsigned char *entry, int flags);
int delta_chat_bridge_read_config(const unsigned char *entry, int size);

/* OpenCE's text (at most size bytes, ended or not) as it is shown: printable
ASCII, "|" as "?", anything else "?", the spaces it begins and ends with
dropped, at most DELTA_CHAT_BRIDGE_TEXT_SIZE - 1 characters; ended in out,
its length. ASCII is UTF-8 as it is, so out goes on as Delta's text */
int delta_chat_bridge_clean(const char *text, int size, char *out);
/* Delta's UTF-8 text (length bytes) for OpenCE: Latin-1's letters without
their accents, typographic quotes, dashes and spaces as ASCII's, anything
else "?", then cleaned (and cut to 63 characters); its length */
int delta_chat_bridge_to_ascii(const char *text, int length, char *out);

/* a line's flags to Delta's scope (a vehicle's line is a team's: Delta has
no narrower one) and back */
int delta_chat_bridge_scope(int flags);
int delta_chat_bridge_flags(int scope);

/* the host's settings for its OpenCE machines: its mode (enum
delta_chat_mode), and whether it lets them chat at all (network.chat_legacy,
a dedicated server's chat_legacy). Links are allowed: Delta chat has no rule
against them */
int delta_chat_bridge_host_config(int mode, int legacy);

/* the host's checks of an OpenCE machine's line before it goes on as
Delta's own (delta_chat_host_decide and the filter come after): its budget,
the cooldown (milliseconds; 0 none) once past it, and the same line again
too soon. An enum delta_chat_refusal (with too fast, the seconds left in
*paused_seconds when paused), or 0 with the line counted */
int delta_chat_bridge_take(struct delta_chat_bridge_budget *budget, delta_u32 now, delta_u32 cooldown,
	const char *text, int length, int *paused_seconds);
/* the line taken went on: the repeat check remembers it */
void delta_chat_bridge_said(struct delta_chat_bridge_budget *budget, delta_u32 now, const char *text, int length);

/* (a client) whether it may say OpenCE's lines: the host's settings known,
and their flags */
int delta_chat_bridge_client_allowed(int known, int flags);

#endif
