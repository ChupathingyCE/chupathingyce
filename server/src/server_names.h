/*
SERVER_NAMES.H

The dedicated server's blocked names (sv_name_filter, server/docs/settings.md):
the list file, blocked_names.txt in the data folder (server/blocked_names.txt
in the repository, the same list the ChupathingyCE site uses), and whether a
player's name is on it. Plain C, with no game types, so the same unit is
built into the game and into the tests (server/tests/control_test.c).

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

#endif
