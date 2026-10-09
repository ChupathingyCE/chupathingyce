/*
COMMAND_LINE.C

The dedicated server's command lines as text (command_line.h). Everything
here is checked as a stranger's: a line comes from the server's console,
its startup commands or its control API (server/docs/admin.md).
*/

#include "command_line.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- private code */

static int is_space(
	char character)
{
	return character == ' ' || character == '\t';
}

static char lower(
	char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static void set_error(
	char *error,
	int error_size,
	const char *text)
{
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "%s", text);
}

/* ---------- public code */

int command_line_parse(
	const char *text,
	struct command_line *line,
	char *error,
	int error_size)
{
	int length;
	int index;

	line->count = 0;
	set_error(error, error_size, "");
	if (!text)
		return 1;
	/* (printable ASCII only, and no longer than a line may be) */
	for (length = 0; text[length]; length++)
	{
		unsigned char character = (unsigned char)text[length];

		if (length >= COMMAND_LINE_MAXIMUM_LENGTH)
		{
			set_error(error, error_size, "the line is too long");
			return 0;
		}
		if ((character < 0x20 || character > 0x7e) && character != '\t')
		{
			set_error(error, error_size, "the line has a character that is not printable ASCII");
			return 0;
		}
	}
	index = 0;
	while (is_space(text[index]))
		index++;
	if (text[index] == '#')
		return 1;
	while (text[index])
	{
		char *word;
		int word_length = 0;

		if (line->count >= COMMAND_LINE_MAXIMUM_WORDS)
		{
			set_error(error, error_size, "too many words");
			line->count = 0;
			return 0;
		}
		word = line->words[line->count];
		if (text[index] == '"')
		{
			index++;
			for (;;)
			{
				char character = text[index];

				if (!character)
				{
					set_error(error, error_size, "a quote is not closed");
					line->count = 0;
					return 0;
				}
				index++;
				if (character == '"')
					break;
				if (character == '\\' && (text[index] == '"' || text[index] == '\\'))
					character = text[index++];
				if (word_length >= COMMAND_LINE_WORD_SIZE - 1)
				{
					set_error(error, error_size, "a word is too long");
					line->count = 0;
					return 0;
				}
				word[word_length++] = character;
			}
			/* (a closing quote ends the word: "a"b is not one) */
			if (text[index] && !is_space(text[index]))
			{
				set_error(error, error_size, "a quote is followed by more of its word");
				line->count = 0;
				return 0;
			}
		}
		else
		{
			while (text[index] && !is_space(text[index]))
			{
				if (text[index] == '"')
				{
					set_error(error, error_size, "a quote in the middle of a word");
					line->count = 0;
					return 0;
				}
				if (word_length >= COMMAND_LINE_WORD_SIZE - 1)
				{
					set_error(error, error_size, "a word is too long");
					line->count = 0;
					return 0;
				}
				word[word_length++] = text[index++];
			}
		}
		word[word_length] = 0;
		line->count++;
		while (is_space(text[index]))
			index++;
	}
	return 1;
}

int command_line_integer(
	const char *text,
	long minimum,
	long maximum,
	long *value)
{
	long result = 0;
	int negative = 0;
	int digits = 0;

	if (!text)
		return 0;
	if (*text == '-')
	{
		negative = 1;
		text++;
	}
	for (; *text; text++)
	{
		if (*text < '0' || *text > '9' || ++digits > 9)
			return 0;
		result = result * 10 + (*text - '0');
	}
	if (!digits)
		return 0;
	if (negative)
		result = -result;
	if (result < minimum || result > maximum)
		return 0;
	*value = result;
	return 1;
}

int command_line_duration(
	const char *text,
	long *seconds)
{
	long total = 0;
	int parts = 0;

	if (!text || !text[0])
		return 0;
	if (!strcmp(text, "forever") || !strcmp(text, "permanent"))
	{
		*seconds = 0;
		return 1;
	}
	while (*text)
	{
		long number = 0;
		long unit;
		int digits = 0;

		while (*text >= '0' && *text <= '9')
		{
			/* (no part longer than the longest ban in seconds) */
			if (++digits > 9)
				return 0;
			number = number * 10 + (*text++ - '0');
		}
		if (!digits)
			return 0;
		switch (lower(*text))
		{
		case 's': unit = 1; text++; break;
		case 'm': unit = 60; text++; break;
		case 'h': unit = 60 * 60; text++; break;
		case 'd': unit = 24 * 60 * 60; text++; break;
		case 'w': unit = 7 * 24 * 60 * 60; text++; break;
		case 0:
			/* (a bare number is minutes, and only alone) */
			if (parts)
				return 0;
			unit = 60;
			break;
		default:
			return 0;
		}
		if (number > COMMAND_LINE_MAXIMUM_DURATION / unit)
			return 0;
		total += number * unit;
		if (total > COMMAND_LINE_MAXIMUM_DURATION)
			return 0;
		parts++;
	}
	/* (a ban of no time at all is no ban) */
	if (total <= 0)
		return 0;
	*seconds = total;
	return 1;
}

void command_line_duration_text(
	long seconds,
	char *text,
	int size)
{
	static const struct
	{
		long seconds;
		char unit;
	} units[] = { { 7 * 24 * 60 * 60, 'w' }, { 24 * 60 * 60, 'd' }, { 60 * 60, 'h' }, { 60, 'm' }, { 1, 's' } };
	int length = 0;
	int shown = 0;
	int index;

	if (size <= 0)
		return;
	text[0] = 0;
	if (seconds <= 0)
	{
		snprintf(text, (size_t)size, "forever");
		return;
	}
	/* (the two largest units: 2d 3h, not 2d 3h 4m 5s) */
	for (index = 0; index < (int)(sizeof(units) / sizeof(units[0])) && shown < 2; index++)
	{
		long count = seconds / units[index].seconds;

		if (!count)
		{
			if (shown)
				shown++;
			continue;
		}
		seconds -= count * units[index].seconds;
		if (length < size)
			length += snprintf(text + length, (size_t)(size - length), "%s%ld%c", shown ? " " : "", count, units[index].unit);
		shown++;
	}
}

int command_line_name_match(
	const char *name,
	const char *text)
{
	if (!name || !text || !text[0])
		return 0;
	for (; *text; name++, text++)
	{
		if (!*name || lower(*name) != lower(*text))
			return 0;
	}
	return *name ? 1 : 2;
}

int command_line_map_name_valid(
	const char *text)
{
	int length;
	int base_length;

	if (!text)
		return 0;
	length = (int)strlen(text);
	if (length == 0 || length > 63)
		return 0;
	base_length = length;
	if (length > 3 && (!strcmp(text + length - 3, "@ce") || !strcmp(text + length - 3, "@md") ||
		!strcmp(text + length - 3, "@pc")))
		base_length = length - 3;
	if (!base_length)
		return 0;
	for (length = 0; length < base_length; length++)
	{
		char character = text[length];

		if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.'))
		{
			return 0;
		}
	}
	/* (no "." or ".." for a name) */
	if (text[0] == '.')
		return 0;
	return 1;
}

int command_line_server_name_valid(
	const char *text)
{
	int length;
	int printing = 0;

	if (!text)
		return 0;
	for (length = 0; text[length]; length++)
	{
		if (length >= 15 || text[length] < 0x20 || text[length] > 0x7e)
			return 0;
		if (text[length] != ' ')
			printing = 1;
	}
	return printing;
}

void command_output_begin(
	struct command_output *output,
	char *buffer,
	int size)
{
	output->text = buffer;
	output->size = size;
	output->length = 0;
	output->truncated = 0;
	if (size > 0)
		buffer[0] = 0;
}

void command_output_printf(
	struct command_output *output,
	const char *format,
	...)
{
	va_list arguments;
	int room = output->size - output->length;
	int written;

	if (room <= 1)
	{
		output->truncated = 1;
		return;
	}
	va_start(arguments, format);
	written = vsnprintf(output->text + output->length, (size_t)room, format, arguments);
	va_end(arguments);
	if (written < 0)
	{
		output->text[output->length] = 0;
		return;
	}
	if (written >= room)
	{
		output->length = output->size - 1;
		output->truncated = 1;
	}
	else
	{
		output->length += written;
	}
}

void command_output_json_string(
	struct command_output *output,
	const char *text)
{
	static const char hex[] = "0123456789abcdef";
	char piece[8];

	command_output_printf(output, "\"");
	for (; text && *text; text++)
	{
		unsigned char character = (unsigned char)*text;

		if (character == '"' || character == '\\')
		{
			piece[0] = '\\';
			piece[1] = (char)character;
			piece[2] = 0;
		}
		else if (character < 0x20 || character >= 0x7f)
		{
			piece[0] = '\\';
			piece[1] = 'u';
			piece[2] = '0';
			piece[3] = '0';
			piece[4] = hex[character >> 4];
			piece[5] = hex[character & 15];
			piece[6] = 0;
		}
		else
		{
			piece[0] = (char)character;
			piece[1] = 0;
		}
		command_output_printf(output, "%s", piece);
	}
	command_output_printf(output, "\"");
}
