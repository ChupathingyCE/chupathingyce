/*
DELTA_CHAT.H

Text chat's rules that need no game (docs/delta.md, Chat): the blocked-words
filter, the host's decision on a line, and the duplicate check. Plain C with
no platform dependency and no allocation, so the tests
(port/linux/tests/delta_test.c), the dedicated server and the game build it
as it is.

The filter reads the blocked names list's format (server/blocked_names.txt:
"<kind> <word>" lines, the words in ROT13) and a player's own list (plain
words, one a line). A line is checked a word at a time: each word's key
(lower case, accents dropped, digits and signs that stand for letters read
as those letters, anything else dropped), and each run of words of one
letter joined ("n i c e"), so a blocked word inside two harmless neighbors
("...an ig...") is not matched across the space between them.
*/

#ifndef HALO_DELTA_CHAT_H
#define HALO_DELTA_CHAT_H

#include "delta_wire.h"

enum
{
	DELTA_CHAT_FILTER_ENTRIES = 512,
	DELTA_CHAT_FILTER_WORD_SIZE = 31,

	/* the same text from one machine within this long is too fast
	(milliseconds) */
	DELTA_CHAT_REPEAT_TIME = 10000
};

/* a list entry's kind (the blocked names list's) */
enum delta_chat_filter_kind
{
	/* blocked wherever it appears in a word */
	_delta_chat_filter_anywhere = 0,
	/* blocked when it is the whole word */
	_delta_chat_filter_whole,
	/* a harmless word holding a blocked one: taken out of a word first */
	_delta_chat_filter_allow
};

struct delta_chat_filter_entry
{
	unsigned char kind;
	unsigned char length;
	char word[DELTA_CHAT_FILTER_WORD_SIZE + 1];
};

struct delta_chat_filter
{
	int count;
	struct delta_chat_filter_entry entries[DELTA_CHAT_FILTER_ENTRIES];
};

/* the blocked names list's entries (server/blocked_names.txt, in ROT13:
delta_chat_words.c), for delta_chat_filter_add with rot13 */
extern const char delta_chat_blocked_words[];

/* the filter emptied */
void delta_chat_filter_clear(struct delta_chat_filter *filter);
/* a list's text (length bytes) added: "<kind> <word>" lines (anywhere,
whole, allow) and, where plain, a word alone (anywhere); '#' starts a
comment. rot13: the words are written in ROT13 (the blocked names list).
The entries added */
int delta_chat_filter_add(struct delta_chat_filter *filter, const char *text, int length, int rot13);
/* the words of a line (UTF-8, length bytes) the filter blocks: how many,
and with masked (may be NULL; masked_size bytes, at least length + 1) the
line with each such word's characters as '*' */
int delta_chat_filter_check(const struct delta_chat_filter *filter, const char *text, int length, char *masked,
	int masked_size);

/* the host's decision on a line before it is routed (enum
delta_chat_refusal; 0 to relay it): its mode (enum delta_chat_mode), the
line's scope, whether the game has teams (co-op: everyone is a teammate,
so it does), whether its player is muted, and whether the filter blocks it */
int delta_chat_host_decide(int mode, int scope, int teams, int muted, int blocked);
/* whether a listener hears a line: its scope, whether the game has teams,
and the speaker's and the listener's teams */
int delta_chat_hears(int scope, int teams, int speaker_team, int listener_team);

/* a line's fingerprint, for the repeat check (FNV-1a of its bytes, case
and spaces aside) */
delta_u32 delta_chat_fingerprint(const char *text, int length);

/* a line's text trimmed of spaces at both ends (in place): its length */
int delta_chat_trim(char *text, int length);

#endif
