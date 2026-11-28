/*
SERVER_NAMES.H

The blocked names: the dedicated server's (sv_name_filter, server/docs/
settings.md: the list file, blocked_names.txt in the data folder,
server/blocked_names.txt in the repository, the same list the ChupathingyCE
site uses) and the game lists' (browser.c: the site's /v1/blocked-names,
else the built-in copy), and whether a name is on one. Plain C, with no game
types, so the same unit is built into the game, the server and the tests
(server/tests/control_test.c, port/linux/tests/game_list_rules_test.c).

The list's words are written in ROT13 and decoded as it is read. A name is
compared as its key, exactly as the site compares it: lower case, accents
dropped, spaces . _ - dropped, digits at its end dropped, digits and signs
that stand for letters read as those letters, Cyrillic and Greek look-alikes
read as Latin letters, and then anything that is not a-z dropped.
*/

#ifndef SERVER_NAMES_H
#define SERVER_NAMES_H

#include <stddef.h>

#define SERVER_NAMES_FILE "blocked_names.txt"

enum
{
	/* a list's entries, at most */
	SERVER_NAMES_MAXIMUM_ENTRIES = 512,
	/* a word, with its end */
	SERVER_NAMES_WORD_SIZE = 48
};

struct server_names_entry
{
	int kind;
	int length;
	char word[SERVER_NAMES_WORD_SIZE];
	/* the word with runs of a letter squeezed to one */
	char squeezed[SERVER_NAMES_WORD_SIZE];
};

/* a list of its own (the game lists'), apart from the server's */
struct server_names_list
{
	int count;
	struct server_names_entry entries[SERVER_NAMES_MAXIMUM_ENTRIES];
};

/* a list's text (length bytes) read into list, in place of what it had: the
entries it has (lines it cannot read are left out) */
int server_names_list_parse(struct server_names_list *list, const char *text, size_t length);

/* whether a name (count characters, as Unicode code points) is on list: 1,
else 0 (and 0 for no list, an empty one, or a name whose key is empty) */
int server_names_list_blocked(const struct server_names_list *list, const unsigned int *characters, int count);

/* the same, for a name in ASCII or UTF-8 */
int server_names_list_blocked_text(const struct server_names_list *list, const char *text);

/* the dedicated server's list, below */

/* the list's text (length bytes) read, in place of the list there was: the
entries it has (lines it cannot read are left out) */
int server_names_parse(const char *text, size_t length);

/* the list emptied */
void server_names_clear(void);

/* the list's entries */
int server_names_count(void);

/* whether a name (count characters, as Unicode code points) is blocked: 1,
else 0 (and 0 for an empty list, or a name whose key is empty) */
int server_names_blocked(const unsigned int *characters, int count);

/* the same, for a name in ASCII or UTF-8 */
int server_names_blocked_text(const char *text);

/* bans.txt's name rules (a line's name=, server/docs/admin.md, Bans) */

enum
{
	/* a rule's key, with its end (a character is at most 4 of its bytes) */
	SERVER_NAMES_RULE_KEY_SIZE = 64 * 4 + 1
};

/* a name's key for a name rule: what player_name_clean leaves out (control
characters, ones that draw as nothing, "|") left out, color codes ("^" and
a digit) and every space dropped, lower case, accents dropped, full-width
forms and Cyrillic and Greek look-alikes read as their Latin letters,
and any other character kept as itself (its UTF-8), so names in other
scripts match only themselves. Unlike the blocked names' key, digits and signs stay as they are and
nothing else is dropped, so a rule matches the name it names and its
look-alikes, never a longer or shorter name. Into key (size bytes): its
length, 0 for an empty one (which matches nothing) */
int server_names_rule_key(const unsigned int *characters, int count, char *key, size_t size);

/* the same, for a name in ASCII or UTF-8 */
int server_names_rule_key_text(const char *text, char *key, size_t size);

/* whether a name (count characters, as Unicode code points) is the one a
rule (its text, ASCII or UTF-8) names: their keys the same, and not empty.
1, else 0 */
int server_names_rule_matches(const char *rule, const unsigned int *characters, int count);

#endif
