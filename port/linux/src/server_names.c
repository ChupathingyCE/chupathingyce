/*
SERVER_NAMES.C

The blocked names (server_names.h): a list, and a name's key compared with
it, as the ChupathingyCE site compares it. The dedicated server's
(sv_name_filter) is the one list kept here; the game lists keep their own
(browser.c).

  whole     a name is blocked when its key, or its key with runs of the
            same letter squeezed to one, is the word
  anywhere  a name is blocked when the word is in its key, or in its
            squeezed key, once the allow words are taken out of it
  allow     a harmless word containing a blocked one: taken out of the key
            (and its squeezed form out of the squeezed key) before the
            anywhere words are looked for, the longest first
*/

#include "server_names.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	NAMES_MAXIMUM_ENTRIES = SERVER_NAMES_MAXIMUM_ENTRIES,
	NAMES_WORD_SIZE = SERVER_NAMES_WORD_SIZE,
	/* a name's characters looked at, at most (a player's name is 11) */
	NAMES_MAXIMUM_CHARACTERS = 64,
	/* a key, with its end (a character is at most 3 of the key's) */
	NAMES_KEY_SIZE = NAMES_MAXIMUM_CHARACTERS * 3 + 1,
};

enum
{
	_names_anywhere,
	_names_whole,
	_names_allow,
};

/* ---------- globals */

/* the dedicated server's list (server_names_parse), made on its first
reading: no room taken in a copy that never reads one */
static struct server_names_list *names_list;

/* Latin-1 and Latin Extended-A (U+00A0 to U+017F) in lower case, their
accents dropped (Unicode's compatibility decomposition, its combining marks
left out): "~" for a character that is no letter, digit or space then */
static const char *const names_latin[] =
{
	/* 00a0 */ " ", "~", "~", "~", "~", "~", "~", "~", " ", "~", "a", "~", "~", "~", "~", " ",
	/* 00b0 */ "~", "~", "2", "3", " ", "~", "~", "~", " ", "1", "o", "~", "1~4", "1~2", "3~4", "~",
	/* 00c0 */ "a", "a", "a", "a", "a", "a", "~", "c", "e", "e", "e", "e", "i", "i", "i", "i",
	/* 00d0 */ "~", "n", "o", "o", "o", "o", "o", "~", "~", "u", "u", "u", "u", "y", "~", "~",
	/* 00e0 */ "a", "a", "a", "a", "a", "a", "~", "c", "e", "e", "e", "e", "i", "i", "i", "i",
	/* 00f0 */ "~", "n", "o", "o", "o", "o", "o", "~", "~", "u", "u", "u", "u", "y", "~", "y",
	/* 0100 */ "a", "a", "a", "a", "a", "a", "c", "c", "c", "c", "c", "c", "c", "c", "d", "d",
	/* 0110 */ "~", "~", "e", "e", "e", "e", "e", "e", "e", "e", "e", "e", "g", "g", "g", "g",
	/* 0120 */ "g", "g", "g", "g", "h", "h", "~", "~", "i", "i", "i", "i", "i", "i", "i", "i",
	/* 0130 */ "i", "~", "ij", "ij", "j", "j", "k", "k", "~", "l", "l", "l", "l", "l", "l", "l~",
	/* 0140 */ "l~", "~", "~", "n", "n", "n", "n", "n", "n", "~n", "~", "~", "o", "o", "o", "o",
	/* 0150 */ "o", "o", "~", "~", "r", "r", "r", "r", "r", "r", "s", "s", "s", "s", "s", "s",
	/* 0160 */ "s", "s", "t", "t", "t", "t", "~", "~", "u", "u", "u", "u", "u", "u", "u", "u",
	/* 0170 */ "u", "u", "u", "u", "w", "w", "y", "y", "y", "z", "z", "z", "z", "z", "z", "s",
};

/* Greek and Cyrillic letters that look like Latin ones (in either case, and
with accents), as those letters */
static const struct
{
	unsigned short character;
	char letter;
} names_look_alikes[] =
{
	{ 0x0386, 'a' }, { 0x038a, 'i' }, { 0x038c, 'o' }, { 0x038e, 'u' }, { 0x0390, 'i' }, { 0x0391, 'a' },
	{ 0x0399, 'i' }, { 0x039a, 'k' }, { 0x039d, 'v' }, { 0x039f, 'o' }, { 0x03a1, 'p' }, { 0x03a4, 't' },
	{ 0x03a5, 'u' }, { 0x03aa, 'i' }, { 0x03ab, 'u' }, { 0x03ac, 'a' }, { 0x03af, 'i' }, { 0x03b0, 'u' },
	{ 0x03b1, 'a' }, { 0x03b9, 'i' }, { 0x03ba, 'k' }, { 0x03bd, 'v' }, { 0x03bf, 'o' }, { 0x03c1, 'p' },
	{ 0x03c4, 't' }, { 0x03c5, 'u' }, { 0x03ca, 'i' }, { 0x03cb, 'u' }, { 0x03cc, 'o' }, { 0x03cd, 'u' },
	{ 0x03f0, 'k' }, { 0x03f1, 'p' }, { 0x0400, 'e' }, { 0x0401, 'e' }, { 0x0405, 's' }, { 0x0406, 'i' },
	{ 0x0407, 'i' }, { 0x0408, 'j' }, { 0x040c, 'k' }, { 0x040e, 'y' }, { 0x0410, 'a' }, { 0x0412, 'b' },
	{ 0x0415, 'e' }, { 0x041a, 'k' }, { 0x041c, 'm' }, { 0x041d, 'h' }, { 0x041e, 'o' }, { 0x0420, 'p' },
	{ 0x0421, 'c' }, { 0x0422, 't' }, { 0x0423, 'y' }, { 0x0425, 'x' }, { 0x0430, 'a' }, { 0x0432, 'b' },
	{ 0x0435, 'e' }, { 0x043a, 'k' }, { 0x043c, 'm' }, { 0x043d, 'h' }, { 0x043e, 'o' }, { 0x0440, 'p' },
	{ 0x0441, 'c' }, { 0x0442, 't' }, { 0x0443, 'y' }, { 0x0445, 'x' }, { 0x0450, 'e' }, { 0x0451, 'e' },
	{ 0x0455, 's' }, { 0x0456, 'i' }, { 0x0457, 'i' }, { 0x0458, 'j' }, { 0x045c, 'k' }, { 0x045e, 'y' },
	{ 0x04d0, 'a' }, { 0x04d1, 'a' }, { 0x04d2, 'a' }, { 0x04d3, 'a' }, { 0x04d6, 'e' }, { 0x04d7, 'e' },
	{ 0x04e6, 'o' }, { 0x04e7, 'o' }, { 0x04ee, 'y' }, { 0x04ef, 'y' }, { 0x04f0, 'y' }, { 0x04f1, 'y' },
	{ 0x04f2, 'y' }, { 0x04f3, 'y' },
};

/* ---------- private code */

/* a run of the same letter squeezed to one ("aaa": "a"), from text into out
(size bytes) */
static void squeeze(const char *text, char *out, size_t size)
{
	size_t length = 0;

	if (!size)
		return;
	for (; *text && length + 1 < size; text++)
	{
		if (length && out[length - 1] == *text)
			continue;
		out[length++] = *text;
	}
	out[length] = 0;
}

/* a character as the key's first step has it (lower case, accents dropped):
appended to text */
static void fold_character(unsigned int character, char *text, size_t *length, size_t size)
{
	const char *folded = "~";
	char single[2] = { 0, 0 };
	size_t index;

	if (character < 0x80)
	{
		single[0] = (char)(character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character);
		folded = single;
	}
	else if (character >= 0xA0 && character < 0xA0 + sizeof(names_latin) / sizeof(names_latin[0]))
		folded = names_latin[character - 0xA0];
	else if (character >= 0x300 && character <= 0x36F)
		/* (a combining mark: an accent, dropped) */
		folded = "";
	else if (character >= 0xFF01 && character <= 0xFF5E)
	{
		/* (a full-width form: its ASCII character) */
		character -= 0xFEE0;
		single[0] = (char)(character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character);
		folded = single;
	}
	else
	{
		for (index = 0; index < sizeof(names_look_alikes) / sizeof(names_look_alikes[0]); index++)
		{
			if (names_look_alikes[index].character == character)
			{
				single[0] = names_look_alikes[index].letter;
				folded = single;
				break;
			}
		}
	}
	for (; *folded && *length + 1 < size; folded++)
		text[(*length)++] = *folded;
	text[*length] = 0;
}

/* a digit's or a sign's letter (0 o, 1 i, ...), or the character itself */
static char look_alike_letter(char character)
{
	switch (character)
	{
	case '0': return 'o';
	case '1': return 'i';
	case '3': return 'e';
	case '4': return 'a';
	case '5': return 's';
	case '7': return 't';
	case '8': return 'b';
	case '@': return 'a';
	case '$': return 's';
	case '!': return 'i';
	case '|': return 'i';
	default: return character;
	}
}

/* a name's key (a-z only, maybe empty) into key (NAMES_KEY_SIZE bytes) */
static void name_key(const unsigned int *characters, int count, char *key)
{
	char folded[NAMES_KEY_SIZE];
	size_t length = 0;
	size_t kept = 0;
	size_t index;
	int character;

	folded[0] = 0;
	if (count > NAMES_MAXIMUM_CHARACTERS)
		count = NAMES_MAXIMUM_CHARACTERS;
	/* lower case, accents dropped, look-alike letters read as Latin */
	for (character = 0; character < count; character++)
		fold_character(characters[character], folded, &length, sizeof(folded));
	/* spaces . _ - dropped */
	for (index = 0; index < length; index++)
	{
		char c = folded[index];

		if (c != ' ' && c != '.' && c != '_' && c != '-')
			folded[kept++] = c;
	}
	/* digits at the end dropped */
	while (kept && folded[kept - 1] >= '0' && folded[kept - 1] <= '9')
		kept--;
	/* digits and signs read as letters, and anything not a-z dropped */
	length = 0;
	for (index = 0; index < kept; index++)
	{
		char c = look_alike_letter(folded[index]);

		if (c >= 'a' && c <= 'z')
			key[length++] = c;
	}
	key[length] = 0;
}

/* every place word is in text replaced by a mark that is no letter */
static void take_out(char *text, const char *word)
{
	size_t word_length = strlen(word);
	char *found;

	if (!word_length)
		return;
	while ((found = strstr(text, word)) != NULL)
	{
		*found = '#';
		memmove(found + 1, found + word_length, strlen(found + word_length) + 1);
	}
}

static int compare_allow_length(const void *a, const void *b)
{
	const struct server_names_entry *first = (const struct server_names_entry *)a;
	const struct server_names_entry *second = (const struct server_names_entry *)b;

	if (first->kind != second->kind)
		return first->kind - second->kind;
	return second->length - first->length;
}

/* ---------- public code */

int server_names_list_parse(struct server_names_list *list, const char *text, size_t length)
{
	size_t position = 0;

	list->count = 0;
	while (text && position < length)
	{
		char line[128];
		char kind[16];
		char word[NAMES_WORD_SIZE];
		size_t line_length = 0;
		struct server_names_entry *entry;
		int index;
		int valid = 1;

		while (position < length && text[position] != '\n')
		{
			if (line_length + 1 < sizeof(line))
				line[line_length++] = text[position];
			position++;
		}
		position++;
		line[line_length] = 0;
		if (line_length && line[line_length - 1] == '\r')
			line[--line_length] = 0;
		if (!line[0] || line[0] == '#')
			continue;
		if (sscanf(line, "%15s %47s", kind, word) != 2 || list->count >= NAMES_MAXIMUM_ENTRIES)
			continue;
		entry = &list->entries[list->count];
		if (!strcmp(kind, "anywhere"))
			entry->kind = _names_anywhere;
		else if (!strcmp(kind, "whole"))
			entry->kind = _names_whole;
		else if (!strcmp(kind, "allow"))
			entry->kind = _names_allow;
		else
			continue;
		/* (ROT13, and a-z only) */
		for (index = 0; word[index]; index++)
		{
			char c = word[index];

			if (c >= 'A' && c <= 'Z')
				c = (char)(c + ('a' - 'A'));
			if (c < 'a' || c > 'z')
			{
				valid = 0;
				break;
			}
			entry->word[index] = (char)('a' + (c - 'a' + 13) % 26);
		}
		if (!valid || !index)
			continue;
		entry->word[index] = 0;
		entry->length = index;
		squeeze(entry->word, entry->squeezed, sizeof(entry->squeezed));
		list->count++;
	}
	/* (the allow words, longest first) */
	qsort(list->entries, (size_t)list->count, sizeof(list->entries[0]), compare_allow_length);
	return list->count;
}

int server_names_list_blocked(const struct server_names_list *list, const unsigned int *characters, int count)
{
	char key[NAMES_KEY_SIZE];
	char squeezed[NAMES_KEY_SIZE];
	int variant;
	int index;

	if (!list || !list->count || !characters || count <= 0)
		return 0;
	name_key(characters, count, key);
	if (!key[0])
		return 0;
	squeeze(key, squeezed, sizeof(squeezed));
	for (index = 0; index < list->count; index++)
	{
		const struct server_names_entry *entry = &list->entries[index];

		if (entry->kind == _names_whole && (!strcmp(key, entry->word) || !strcmp(squeezed, entry->word)))
			return 1;
	}
	for (variant = 0; variant < 2; variant++)
	{
		char text[NAMES_KEY_SIZE];

		memcpy(text, variant ? squeezed : key, sizeof(text));
		for (index = 0; index < list->count; index++)
		{
			if (list->entries[index].kind != _names_allow)
				continue;
			take_out(text, list->entries[index].word);
			take_out(text, list->entries[index].squeezed);
		}
		for (index = 0; index < list->count; index++)
		{
			if (list->entries[index].kind == _names_anywhere && strstr(text, list->entries[index].word))
				return 1;
		}
	}
	return 0;
}

/* text in ASCII or UTF-8 as code points (NAMES_MAXIMUM_CHARACTERS at most):
their count */
static int decode_text(const char *text, unsigned int *characters)
{
	const unsigned char *cursor = (const unsigned char *)text;
	int count = 0;

	/* (UTF-8; a byte that does not begin a character as itself) */
	while (cursor && *cursor && count < NAMES_MAXIMUM_CHARACTERS)
	{
		unsigned int character = *cursor++;
		int more = character >= 0xF0 ? 3 : character >= 0xE0 ? 2 : character >= 0xC0 ? 1 : 0;

		if (more)
			character &= 0x3F >> more;
		for (; more && (*cursor & 0xC0) == 0x80; more--)
			character = (character << 6) | (*cursor++ & 0x3F);
		characters[count++] = character;
	}
	return count;
}

int server_names_list_blocked_text(const struct server_names_list *list, const char *text)
{
	unsigned int characters[NAMES_MAXIMUM_CHARACTERS];
	int count = decode_text(text, characters);

	return server_names_list_blocked(list, characters, count);
}

/* whether a character is one player_name_clean (source/game/players.c)
leaves out of a name: a control character, one that draws as nothing, a
lone surrogate, a non-character or "|" */
static int rule_left_out(unsigned int character)
{
	return character < 0x20 || (character >= 0x7F && character <= 0x9F) || character == 0xAD ||
		character == 0x34F || character == 0x115F || character == 0x1160 || character == 0x17B4 ||
		character == 0x17B5 || (character >= 0x180B && character <= 0x180E) ||
		(character >= 0x200B && character <= 0x200F) || (character >= 0x2028 && character <= 0x202E) ||
		(character >= 0x2060 && character <= 0x206F) || character == 0x2800 || character == 0x3164 ||
		(character >= 0xD800 && character <= 0xDFFF) || (character >= 0xFE00 && character <= 0xFE0F) ||
		character == 0xFEFF || character == 0xFFA0 || (character >= 0xFFF0 && character <= 0xFFFF) ||
		character == '|';
}

/* whether a character is a space (Unicode's, as player_name_clean makes
them plain ones) */
static int rule_space(unsigned int character)
{
	return character == ' ' || character == 0xA0 || character == 0x1680 || (character >= 0x2000 && character <= 0x200A) ||
		character == 0x202F || character == 0x205F || character == 0x3000;
}

/* a code point as UTF-8, appended to text */
static void append_utf8(unsigned int character, char *text, size_t *length, size_t size)
{
	unsigned char bytes[4];
	size_t count, index;

	if (character > 0x10FFFF)
		character = 0xFFFD;
	if (character < 0x80)
	{
		bytes[0] = (unsigned char)character;
		count = 1;
	}
	else if (character < 0x800)
	{
		bytes[0] = (unsigned char)(0xC0 | character >> 6);
		bytes[1] = (unsigned char)(0x80 | (character & 0x3F));
		count = 2;
	}
	else if (character < 0x10000)
	{
		bytes[0] = (unsigned char)(0xE0 | character >> 12);
		bytes[1] = (unsigned char)(0x80 | ((character >> 6) & 0x3F));
		bytes[2] = (unsigned char)(0x80 | (character & 0x3F));
		count = 3;
	}
	else
	{
		bytes[0] = (unsigned char)(0xF0 | character >> 18);
		bytes[1] = (unsigned char)(0x80 | ((character >> 12) & 0x3F));
		bytes[2] = (unsigned char)(0x80 | ((character >> 6) & 0x3F));
		bytes[3] = (unsigned char)(0x80 | (character & 0x3F));
		count = 4;
	}
	/* (a character that does not fit whole is left out) */
	if (*length + count >= size)
		return;
	for (index = 0; index < count; index++)
		text[(*length)++] = (char)bytes[index];
	text[*length] = 0;
}

int server_names_rule_key(const unsigned int *characters, int count, char *key, size_t size)
{
	char folded[SERVER_NAMES_RULE_KEY_SIZE];
	size_t length = 0;
	int character;

	if (!key || !size)
		return 0;
	key[0] = 0;
	if (!characters || count <= 0)
		return 0;
	if (count > NAMES_MAXIMUM_CHARACTERS)
		count = NAMES_MAXIMUM_CHARACTERS;
	folded[0] = 0;
	for (character = 0; character < count; character++)
	{
		unsigned int one = characters[character];

		/* (a UTF-16 pair, as a wide name may hold one: its code point) */
		if (one >= 0xD800 && one <= 0xDBFF && character + 1 < count && characters[character + 1] >= 0xDC00 &&
			characters[character + 1] <= 0xDFFF)
		{
			one = 0x10000 + ((one - 0xD800) << 10) + (characters[character + 1] - 0xDC00);
			character++;
		}
		if (rule_left_out(one) || rule_space(one))
			continue;
		/* (a color code: "^" and a digit, both left out) */
		if (one == '^' && character + 1 < count && characters[character + 1] >= '0' && characters[character + 1] <= '9')
		{
			character++;
			continue;
		}
		/* (a character the fold has no letter for, "~": itself, so that
		names of other scripts differ as they do) */
		{
			size_t before = length;

			fold_character(one, folded, &length, sizeof(folded));
			if (one >= 0x80 && length == before + 1 && folded[before] == '~')
			{
				length = before;
				folded[length] = 0;
				append_utf8(one, folded, &length, sizeof(folded));
			}
		}
	}
	/* (a folded character may be a space: U+00A0's and its kind) */
	{
		size_t index, kept = 0;

		for (index = 0; index < length && kept + 1 < size; index++)
		{
			if (folded[index] != ' ')
				key[kept++] = folded[index];
		}
		key[kept] = 0;
		return (int)kept;
	}
}

int server_names_rule_key_text(const char *text, char *key, size_t size)
{
	unsigned int characters[NAMES_MAXIMUM_CHARACTERS];
	int count = decode_text(text, characters);

	return server_names_rule_key(characters, count, key, size);
}

int server_names_rule_matches(const char *rule, const unsigned int *characters, int count)
{
	char rule_key[SERVER_NAMES_RULE_KEY_SIZE];
	char name_key[SERVER_NAMES_RULE_KEY_SIZE];

	if (!server_names_rule_key_text(rule, rule_key, sizeof(rule_key)) ||
		!server_names_rule_key(characters, count, name_key, sizeof(name_key)))
	{
		return 0;
	}
	return !strcmp(rule_key, name_key);
}

void server_names_clear(void)
{
	if (names_list)
		names_list->count = 0;
}

int server_names_count(void)
{
	return names_list ? names_list->count : 0;
}

int server_names_parse(const char *text, size_t length)
{
	if (!names_list)
		names_list = calloc(1, sizeof(*names_list));
	return names_list ? server_names_list_parse(names_list, text, length) : 0;
}

int server_names_blocked(const unsigned int *characters, int count)
{
	return server_names_list_blocked(names_list, characters, count);
}

int server_names_blocked_text(const char *text)
{
	return server_names_list_blocked_text(names_list, text);
}
