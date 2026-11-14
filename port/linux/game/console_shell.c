/*
CONSOLE_SHELL.C

The drop-down console's typing, as a shell's (console_shell.h).

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "console_shell.h"

#include <string.h>

#define MAXIMUM_WORDS 64

struct word
{
	char const *start;
	size_t length;
	/* quoted text: start and length are its inside, without the quotes */
	int quoted;
};

static int is_space(char character)
{
	return character == ' ' || character == '\t';
}

/* the line's words, at most maximum of them: bare, quoted, or a
parenthesized expression whole; *rest is where it stopped */
static int split(char const *line, struct word *words, int maximum, int *count, char const **rest)
{
	char const *at = line;

	*count = 0;
	*rest = line;
	while (*at)
	{
		struct word *word;

		while (is_space(*at))
			at++;
		*rest = at;
		if (!*at)
			break;
		if (*count >= maximum)
			return _console_shell_ok;
		word = &words[(*count)++];
		word->quoted = 0;
		if (*at == '"' || *at == '\'')
		{
			char quote = *at++;
			char const *end = strchr(at, quote);

			if (!end)
				return _console_shell_unterminated_quote;
			word->start = at;
			word->length = (size_t)(end - at);
			word->quoted = 1;
			at = end + 1;
		}
		else if (*at == '(')
		{
			int depth = 0;
			char in_quote = 0;

			word->start = at;
			do
			{
				if (!*at)
					return in_quote ? _console_shell_unterminated_quote : _console_shell_unmatched_parenthesis;
				if (in_quote)
				{
					if (*at == in_quote)
						in_quote = 0;
				}
				else if (*at == '"')
					in_quote = '"';
				else if (*at == '(')
					depth++;
				else if (*at == ')')
					depth--;
				at++;
			} while (depth > 0 || in_quote);
			word->length = (size_t)(at - word->start);
		}
		else if (*at == ')')
		{
			/* (a shell user quotes a ")" of their own) */
			return _console_shell_unmatched_parenthesis;
		}
		else
		{
			word->start = at;
			while (*at && !is_space(*at) && *at != '"' && *at != '\'' && *at != '(' && *at != ')')
				at++;
			word->length = (size_t)(at - word->start);
		}
	}
	*rest = at;
	return _console_shell_ok;
}

/* appends text (length characters), with room left for the end; 0 when it
does not fit */
static int append(char *expression, size_t size, size_t *used, char const *text, size_t length)
{
	if (*used + length + 1 > size)
		return 0;
	memcpy(expression + *used, text, length);
	*used += length;
	expression[*used] = 0;
	return 1;
}

/* a word's text, quoted text in double quotes (a double quote inside
single ones dropped: the scripts' text has no way to hold one) */
static int append_word(char *expression, size_t size, size_t *used, struct word const *word, int quote)
{
	size_t index;

	if (quote && !append(expression, size, used, "\"", 1))
		return 0;
	for (index = 0; index < word->length; index++)
	{
		if (word->quoted && word->start[index] == '"')
			continue;
		if (!append(expression, size, used, word->start + index, 1))
			return 0;
	}
	return !quote || append(expression, size, used, "\"", 1);
}

int console_shell_translate(char const *line, char *expression, size_t size,
	console_shell_text_tail_proc text_tail, void *context)
{
	struct word words[MAXIMUM_WORDS];
	char command[64];
	int count, result, index, tail = -1;
	size_t used = 0;
	char const *rest;

	if (!size)
		return _console_shell_too_long;
	expression[0] = 0;
	while (is_space(*line))
		line++;
	/* (the scripts' compiler runs no line with a ";": hs.c) */
	if (strchr(line, ';'))
		return _console_shell_semicolon;
	/* (the console's own form, whole) */
	if (*line == '(')
	{
		size_t length = strlen(line);

		while (length && is_space(line[length - 1]))
			length--;
		return append(expression, size, &used, line, length) ? _console_shell_ok : _console_shell_too_long;
	}
	/* the command, and how many values it takes when its last is text */
	result = split(line, words, 1, &count, &rest);
	if (result != _console_shell_ok)
		return result;
	if (!count)
		return _console_shell_ok;
	if (!words[0].quoted && words[0].length < sizeof(command) && text_tail)
	{
		memcpy(command, words[0].start, words[0].length);
		command[words[0].length] = 0;
		tail = text_tail(command, context);
	}
	/* the words: all of them, or with a text value last, the command and
	the values before it, the rest of the line being that text as typed */
	result = split(line, words, tail > 0 ? tail : MAXIMUM_WORDS, &count, &rest);
	if (result != _console_shell_ok)
		return result;
	if (tail <= 0 && *rest)
		return _console_shell_too_long;
	for (index = 0; index < count; index++)
	{
		if (index && !append(expression, size, &used, " ", 1))
			goto too_long;
		if (!append_word(expression, size, &used, &words[index], words[index].quoted))
			goto too_long;
	}
	if (tail > 0 && *rest)
	{
		size_t length = strlen(rest);
		size_t index_in_rest;

		while (length && is_space(rest[length - 1]))
			length--;
		/* (one quoted value, whole: its inside) */
		if (length >= 2 && (rest[0] == '"' || rest[0] == '\'') && rest[length - 1] == rest[0] &&
			!memchr(rest + 1, rest[0], length - 2))
		{
			rest++;
			length -= 2;
		}
		if (!append(expression, size, &used, " \"", 2))
			goto too_long;
		for (index_in_rest = 0; index_in_rest < length; index_in_rest++)
		{
			if (rest[index_in_rest] == '"')
				continue;
			if (!append(expression, size, &used, rest + index_in_rest, 1))
				goto too_long;
		}
		if (!append(expression, size, &used, "\"", 1))
			goto too_long;
	}
	return _console_shell_ok;

too_long:
	expression[0] = 0;
	return _console_shell_too_long;
}
