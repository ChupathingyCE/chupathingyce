/*
HALT_FACTS.C

port: the halt screen's facts (halt_facts.h), on plain types with no
allocation.
*/

#include "halt_facts.h"

#include <stdio.h>
#include <string.h>

/* ---------- private code */

static int starts_with(char const *text, int length, char const *prefix)
{
	int prefix_length = (int)strlen(prefix);

	return length >= prefix_length && !memcmp(text, prefix, (size_t)prefix_length);
}

static int contains(char const *text, int length, char const *part)
{
	int part_length = (int)strlen(part), index;

	for (index = 0; index + part_length <= length; index++)
	{
		if (!memcmp(text + index, part, (size_t)part_length))
			return 1;
	}
	return 0;
}

/* whether a line is an error or a warning, by its words */
static int line_important(char const *text, int length)
{
	static char const *const words[] =
	{
		"error", "warning", "fail", "couldn't", "can't", "cannot", "exception", "assert", "invalid", "missing",
		"not found", "refused",
	};
	char lower[HALT_FACTS_LINE_SIZE];
	int index, size = length < (int)sizeof(lower) - 1 ? length : (int)sizeof(lower) - 1;

	for (index = 0; index < size; index++)
		lower[index] = (char)(text[index] >= 'A' && text[index] <= 'Z' ? text[index] + 32 : text[index]);
	/* (a count of none is no failure: "0 failed") */
	if (contains(lower, size, " 0 failed") && !contains(lower, size, "error"))
		return 0;
	for (index = 0; index < (int)(sizeof(words) / sizeof(words[0])); index++)
	{
		if (contains(lower, size, words[index]))
			return 1;
	}
	return 0;
}

/* a debug.txt line's "11.17.26 14:02:33  " left off */
static int date_length(char const *text, int length)
{
	static char const pattern[] = "00.00.00 00:00:00  ";
	int index;

	if (length < (int)sizeof(pattern) - 1)
		return 0;
	for (index = 0; index < (int)sizeof(pattern) - 1; index++)
	{
		char c = text[index];

		if (pattern[index] == '0' ? (c < '0' || c > '9') : c != pattern[index])
			return 0;
	}
	return (int)sizeof(pattern) - 1;
}

/* ---------- public code */

enum
{
	/* the lines of the log's end that are looked at, at most */
	HALT_FACTS_SCANNED_LINES = 512,
	/* a stack walk's lines, at most (stack_walk_windows.c's 64 frames) */
	HALT_FACTS_WALK_LINES = 70,
};

void halt_facts_log_lines(struct halt_facts *facts, char const *text, int length)
{
	int starts[HALT_FACTS_SCANNED_LINES], lengths[HALT_FACTS_SCANNED_LINES];
	int repeats[HALT_FACTS_SCANNED_LINES];
	char chosen[HALT_FACTS_SCANNED_LINES];
	int count = 0, position = 0, index, halt = -1, walk = -1, kept = 0, first, collapsed;

	facts->line_count = 0;
	if (!text || length <= 0)
		return;
	/* the lines (the newest HALT_FACTS_SCANNED_LINES), their dates left
	off, the empty ones left out */
	while (position < length)
	{
		int start = position, line_length, skip;

		while (position < length && text[position] != '\n')
			position++;
		line_length = position - start;
		position++;
		while (line_length > 0 && text[start + line_length - 1] == '\r')
			line_length--;
		skip = date_length(text + start, line_length);
		/* (and the port's own lines' "chupathingyce: ") */
		if (starts_with(text + start + skip, line_length - skip, "chupathingyce: "))
			skip += 15;
		if (line_length - skip <= 0)
			continue;
		/* (debug.txt's own header, written atop each run's lines) */
		if (starts_with(text + start + skip, line_length - skip, "reference ") ||
			starts_with(text + start + skip, line_length - skip, "halobeta xbox"))
		{
			continue;
		}
		if (count == HALT_FACTS_SCANNED_LINES)
		{
			memmove(starts, starts + 1, sizeof(starts) - sizeof(starts[0]));
			memmove(lengths, lengths + 1, sizeof(lengths) - sizeof(lengths[0]));
			count--;
		}
		starts[count] = start + skip;
		lengths[count] = line_length - skip;
		count++;
	}
	/* the halt's own line (the newest EXCEPTION halt or assert), and the
	stack walk just above it ("Printing stuff for Mat's edification", then
	its frames) */
	for (index = count - 1; index >= 0 && halt < 0; index--)
	{
		if (starts_with(text + starts[index], lengths[index], "EXCEPTION halt") ||
			starts_with(text + starts[index], lengths[index], "EXCEPTION assert"))
		{
			halt = index;
		}
	}
	for (index = (halt < 0 ? count : halt) - 1; index >= 0 && index >= (halt < 0 ? count : halt) - HALT_FACTS_WALK_LINES;
		index--)
	{
		if (contains(text + starts[index], lengths[index], "Printing stuff for Mat's edification"))
		{
			walk = index;
			break;
		}
	}
	/* the lines before them, a line repeated made one ("(x4)") */
	first = walk >= 0 ? walk : halt >= 0 ? halt : count;
	collapsed = 0;
	for (index = 0; index < first; index++)
	{
		if (collapsed > 0 && lengths[collapsed - 1] == lengths[index] &&
			!memcmp(text + starts[collapsed - 1], text + starts[index], (size_t)lengths[index]))
		{
			repeats[collapsed - 1]++;
			continue;
		}
		starts[collapsed] = starts[index];
		lengths[collapsed] = lengths[index];
		repeats[collapsed] = 1;
		collapsed++;
	}
	/* the newest errors and warnings first (half the lines at most), then
	the newest of the rest, shown oldest first */
	memset(chosen, 0, sizeof(chosen));
	for (index = collapsed - 1; index >= 0 && kept < HALT_FACTS_LOG_LINES / 2; index--)
	{
		if (line_important(text + starts[index], lengths[index]))
		{
			chosen[index] = 2;
			kept++;
		}
	}
	for (index = collapsed - 1; index >= 0 && kept < HALT_FACTS_LOG_LINES; index--)
	{
		if (!chosen[index])
		{
			chosen[index] = 1;
			kept++;
		}
	}
	kept = 0;
	for (index = 0; index < collapsed; index++)
	{
		int size = lengths[index], at;
		char *out;

		if (!chosen[index])
			continue;
		out = facts->lines[kept];
		if (size > HALT_FACTS_LINE_SIZE - 16)
			size = HALT_FACTS_LINE_SIZE - 16;
		memcpy(out, text + starts[index], (size_t)size);
		out[size] = 0;
		/* (tabs and other controls as spaces: the fonts draw neither) */
		for (at = 0; at < size; at++)
		{
			if ((unsigned char)out[at] < ' ')
				out[at] = ' ';
		}
		if (repeats[index] > 1)
			snprintf(out + size, (size_t)(HALT_FACTS_LINE_SIZE - size), " (x%d)", repeats[index]);
		facts->important[kept] = (char)(chosen[index] == 2 || line_important(out, size));
		kept++;
	}
	facts->line_count = kept;
}

void halt_facts_place(char *out, int size, char const *file, long line)
{
	char const *from;
	char const *cursor;
	int length = 0;

	if (size <= 0)
		return;
	out[0] = 0;
	if (!file || !file[0])
		return;
	/* (after the last "source" or "port" folder, either case and slash;
	else the name alone) */
	from = NULL;
	for (cursor = file; *cursor; cursor++)
	{
		if ((cursor == file || cursor[-1] == '\\' || cursor[-1] == '/') &&
			(!strncmp(cursor, "SOURCE", 6) || !strncmp(cursor, "source", 6)) && (cursor[6] == '\\' || cursor[6] == '/'))
		{
			from = cursor + 7;
		}
		else if ((cursor == file || cursor[-1] == '\\' || cursor[-1] == '/') && !strncmp(cursor, "port", 4) &&
			(cursor[4] == '\\' || cursor[4] == '/'))
		{
			from = cursor;
		}
	}
	if (!from)
	{
		from = file;
		for (cursor = file; *cursor; cursor++)
		{
			if (*cursor == '\\' || *cursor == '/')
				from = cursor + 1;
		}
	}
	for (cursor = from; *cursor && length < size - 1; cursor++)
		out[length++] = *cursor == '\\' ? '/' : *cursor;
	out[length] = 0;
	if (line > 0 && length < size - 1)
		snprintf(out + length, (size_t)(size - length), ":%ld", line);
}

void halt_facts_map(char *out, int size, char const *loaded_name)
{
	int length = 0;

	if (size <= 0)
		return;
	while (loaded_name && loaded_name[length] && loaded_name[length] != '@' && length < size - 1)
	{
		out[length] = loaded_name[length];
		length++;
	}
	out[length] = 0;
}
