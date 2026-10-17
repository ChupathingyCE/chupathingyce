/*
DELTA_CHAT.C

Text chat's rules that need no game (delta_chat.h).
*/

#include "delta_chat.h"

#include <string.h>

enum
{
	KEY_SIZE = 96,
	/* the words of a line checked (a line is at most 200 bytes) */
	MAXIMUM_WORDS = 128
};

/* ---------- keys */

/* one UTF-8 character at text (length bytes left): its code point and
size, or the byte alone for one that does not decode */
static delta_u32 next_character(const unsigned char *text, int length, int *size)
{
	delta_u32 code = text[0];
	int more = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : code >= 0xC0 ? 1 : 0;
	int index;

	if (!more || more >= length)
	{
		*size = 1;
		return code;
	}
	code &= more == 1 ? 0x1F : more == 2 ? 0x0F : 0x07;
	for (index = 1; index <= more; index++)
	{
		if ((text[index] & 0xC0) != 0x80)
		{
			*size = 1;
			return text[0];
		}
		code = (code << 6) | (text[index] & 0x3F);
	}
	*size = more + 1;
	return code;
}

/* a character as the key reads it: a-z, or 0 for none */
static char key_letter(delta_u32 code)
{
	/* (Latin-1's letters, U+00C0 to U+00FF, without their accents) */
	static const char latin1[] =
		"aaaaaaaceeeeiiii" "dnooooo\0ouuuuyts"
		"aaaaaaaceeeeiiii" "dnooooo\0ouuuuyty";
	static const struct
	{
		unsigned short code;
		char letter;
	} alike[] =
	{
		/* Cyrillic and Greek letters that look like Latin ones */
		{ 0x0430, 'a' }, { 0x0410, 'a' }, { 0x0432, 'b' }, { 0x0412, 'b' }, { 0x0435, 'e' }, { 0x0415, 'e' },
		{ 0x043A, 'k' }, { 0x041A, 'k' }, { 0x043C, 'm' }, { 0x041C, 'm' }, { 0x043D, 'h' }, { 0x041D, 'h' },
		{ 0x043E, 'o' }, { 0x041E, 'o' }, { 0x0440, 'p' }, { 0x0420, 'p' }, { 0x0441, 'c' }, { 0x0421, 'c' },
		{ 0x0442, 't' }, { 0x0422, 't' }, { 0x0443, 'y' }, { 0x0423, 'y' }, { 0x0445, 'x' }, { 0x0425, 'x' },
		{ 0x0456, 'i' }, { 0x0406, 'i' }, { 0x0458, 'j' }, { 0x0408, 'j' }, { 0x0455, 's' }, { 0x0405, 's' },
		{ 0x03B1, 'a' }, { 0x0391, 'a' }, { 0x03B2, 'b' }, { 0x0392, 'b' }, { 0x03B5, 'e' }, { 0x0395, 'e' },
		{ 0x03B9, 'i' }, { 0x0399, 'i' }, { 0x03BA, 'k' }, { 0x039A, 'k' }, { 0x03BD, 'v' }, { 0x039D, 'n' },
		{ 0x03BF, 'o' }, { 0x039F, 'o' }, { 0x03C1, 'p' }, { 0x03A1, 'p' }, { 0x03C4, 't' }, { 0x03A4, 't' },
		{ 0x03C5, 'u' }, { 0x03C7, 'x' }, { 0x03A7, 'x' }, { 0x0396, 'z' }, { 0x0397, 'h' }, { 0x039C, 'm' },
	};
	size_t index;

	if (code >= 'a' && code <= 'z')
		return (char)code;
	if (code >= 'A' && code <= 'Z')
		return (char)(code - 'A' + 'a');
	switch (code)
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
	default: break;
	}
	if (code >= 0xC0 && code <= 0xFF)
		return latin1[code - 0xC0];
	for (index = 0; index < sizeof(alike) / sizeof(alike[0]); index++)
	{
		if (alike[index].code == code)
			return alike[index].letter;
	}
	return 0;
}

/* length bytes of text appended to key (its length now, at most
KEY_SIZE - 1) as letters */
static int key_append(char *key, int key_length, const char *text, int length)
{
	int index = 0;

	while (index < length && key_length < KEY_SIZE - 1)
	{
		int size;
		char letter = key_letter(next_character((const unsigned char *)text + index, length - index, &size));

		if (letter)
			key[key_length++] = letter;
		index += size;
	}
	key[key_length] = 0;
	return key_length;
}

/* key with each run of one letter squeezed to one ("aaa" to "a") */
static void squeeze(const char *key, char *squeezed)
{
	int length = 0;

	for (; *key; key++)
	{
		if (!length || squeezed[length - 1] != *key)
			squeezed[length++] = *key;
	}
	squeezed[length] = 0;
}

/* every occurrence of word taken out of key, in place */
static void remove_word(char *key, const char *word, int word_length)
{
	char *found;

	if (word_length <= 0)
		return;
	while ((found = strstr(key, word)) != NULL)
		memmove(found, found + word_length, strlen(found + word_length) + 1);
}

/* whether a word's key is blocked */
static int key_blocked(const struct delta_chat_filter *filter, const char *key)
{
	char allowed[KEY_SIZE], squeezed[KEY_SIZE], allowed_squeezed[KEY_SIZE];
	int index;

	if (!key[0])
		return 0;
	strcpy(allowed, key);
	for (index = 0; index < filter->count; index++)
	{
		if (filter->entries[index].kind == _delta_chat_filter_allow)
			remove_word(allowed, filter->entries[index].word, filter->entries[index].length);
	}
	squeeze(key, squeezed);
	squeeze(allowed, allowed_squeezed);
	for (index = 0; index < filter->count; index++)
	{
		const struct delta_chat_filter_entry *entry = &filter->entries[index];

		if (entry->kind == _delta_chat_filter_whole)
		{
			if (!strcmp(key, entry->word) || !strcmp(squeezed, entry->word))
				return 1;
		}
		else if (entry->kind == _delta_chat_filter_anywhere)
		{
			if (strstr(allowed, entry->word) || strstr(allowed_squeezed, entry->word))
				return 1;
		}
	}
	return 0;
}

/* ---------- the list */

void delta_chat_filter_clear(struct delta_chat_filter *filter)
{
	filter->count = 0;
}

static int same_text(const char *text, int length, const char *word)
{
	return (int)strlen(word) == length && !memcmp(text, word, (size_t)length);
}

int delta_chat_filter_add(struct delta_chat_filter *filter, const char *text, int length, int rot13)
{
	int added = 0;
	int index = 0;

	while (index < length)
	{
		int end = index, word_start, word_end, kind = _delta_chat_filter_anywhere;
		const char *first;
		int first_length;
		char key[KEY_SIZE];
		int key_length, position;

		while (end < length && text[end] != '\n')
			end++;
		/* (the line, to its comment) */
		{
			int stop = index;

			while (stop < end && text[stop] != '#')
				stop++;
			word_start = index;
			word_end = stop;
		}
		index = end + 1;
		while (word_start < word_end && (text[word_start] == ' ' || text[word_start] == '\t' ||
			text[word_start] == '\r'))
		{
			word_start++;
		}
		while (word_end > word_start && (text[word_end - 1] == ' ' || text[word_end - 1] == '\t' ||
			text[word_end - 1] == '\r'))
		{
			word_end--;
		}
		if (word_start == word_end)
			continue;
		/* "<kind> <word>", or a word alone */
		first = text + word_start;
		for (first_length = 0; word_start + first_length < word_end && first[first_length] != ' ' &&
			first[first_length] != '\t'; first_length++)
		{
		}
		if (word_start + first_length < word_end && (same_text(first, first_length, "anywhere") ||
			same_text(first, first_length, "whole") || same_text(first, first_length, "allow")))
		{
			kind = first[0] == 'a' && first[1] == 'n' ? _delta_chat_filter_anywhere :
				first[0] == 'w' ? _delta_chat_filter_whole : _delta_chat_filter_allow;
			word_start += first_length;
			while (word_start < word_end && (text[word_start] == ' ' || text[word_start] == '\t'))
				word_start++;
		}
		key_length = key_append(key, 0, text + word_start, word_end - word_start);
		if (!key_length || key_length > DELTA_CHAT_FILTER_WORD_SIZE || filter->count >= DELTA_CHAT_FILTER_ENTRIES)
			continue;
		if (rot13)
		{
			for (position = 0; position < key_length; position++)
				key[position] = (char)('a' + (key[position] - 'a' + 13) % 26);
		}
		filter->entries[filter->count].kind = (unsigned char)kind;
		filter->entries[filter->count].length = (unsigned char)key_length;
		memcpy(filter->entries[filter->count].word, key, (size_t)key_length + 1);
		filter->count++;
		added++;
	}
	return added;
}

/* ---------- a line */

struct word
{
	int start;
	int end;
	int letters;
	int blocked;
};

int delta_chat_filter_check(const struct delta_chat_filter *filter, const char *text, int length, char *masked,
	int masked_size)
{
	struct word words[MAXIMUM_WORDS];
	int count = 0, blocked = 0;
	int index, out;

	if (!text || length <= 0)
	{
		if (masked && masked_size > 0)
			masked[0] = 0;
		return 0;
	}
	/* the words, and each one's key */
	index = 0;
	while (index < length && count < MAXIMUM_WORDS)
	{
		char key[KEY_SIZE];

		while (index < length && text[index] == ' ')
			index++;
		if (index >= length)
			break;
		words[count].start = index;
		while (index < length && text[index] != ' ')
			index++;
		words[count].end = index;
		words[count].letters = key_append(key, 0, text + words[count].start, words[count].end - words[count].start);
		words[count].blocked = filter->count > 0 && key_blocked(filter, key);
		count++;
	}
	/* runs of words of one letter, read together ("n i c e") */
	for (index = 0; index < count && filter->count > 0;)
	{
		int end = index;

		while (end < count && words[end].letters == 1)
			end++;
		if (end - index >= 2)
		{
			char key[KEY_SIZE];
			int key_length = 0, word;

			key[0] = 0;
			for (word = index; word < end; word++)
				key_length = key_append(key, key_length, text + words[word].start, words[word].end - words[word].start);
			if (key_blocked(filter, key))
			{
				for (word = index; word < end; word++)
					words[word].blocked = 1;
			}
		}
		index = end > index ? end : index + 1;
	}
	for (index = 0; index < count; index++)
		blocked += words[index].blocked;
	if (!masked || masked_size <= 0)
		return blocked;

	/* the line again, each blocked word's characters a '*' */
	out = 0;
	index = 0;
	{
		int word = 0;

		while (index < length && out < masked_size - 1)
		{
			while (word < count && words[word].end <= index)
				word++;
			if (word < count && index >= words[word].start && words[word].blocked)
			{
				int size;

				next_character((const unsigned char *)text + index, length - index, &size);
				masked[out++] = '*';
				index += size;
			}
			else
				masked[out++] = text[index++];
		}
	}
	masked[out] = 0;
	return blocked;
}

/* ---------- the host's rules */

int delta_chat_host_decide(int mode, int scope, int teams, int muted, int blocked)
{
	if (mode == _delta_chat_mode_off)
		return _delta_chat_refused_off;
	if (muted)
		return _delta_chat_refused_muted;
	if (scope == _delta_chat_scope_team ? !teams : mode == _delta_chat_mode_team)
		return _delta_chat_refused_scope;
	if (blocked)
		return _delta_chat_refused_blocked;
	return _delta_chat_refused_none;
}

int delta_chat_hears(int scope, int teams, int speaker_team, int listener_team)
{
	if (scope != _delta_chat_scope_team)
		return 1;
	return teams && speaker_team == listener_team;
}

delta_u32 delta_chat_fingerprint(const char *text, int length)
{
	delta_u32 hash = 2166136261u;
	int index;

	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)text[index];

		if (character == ' ')
			continue;
		if (character >= 'A' && character <= 'Z')
			character = (unsigned char)(character - 'A' + 'a');
		hash ^= character;
		hash *= 16777619u;
	}
	return hash;
}

int delta_chat_trim(char *text, int length)
{
	int start = 0;

	while (start < length && text[start] == ' ')
		start++;
	while (length > start && text[length - 1] == ' ')
		length--;
	if (start)
		memmove(text, text + start, (size_t)(length - start));
	text[length - start] = 0;
	return length - start;
}
