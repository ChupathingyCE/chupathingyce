/*
COMMAND_LINE.H

The dedicated server's command lines as text (server_commands.c): split
into words, and their numbers, durations, names and output. Plain C, with
no game types, so the same file is built into the game and into the tests
(server/tests/control_test.c).

A command line is printable ASCII, at most COMMAND_LINE_MAXIMUM_LENGTH
characters: words apart by spaces or tabs; a word in double quotes may hold
spaces ("Master Chief"), and \" and \\ inside the quotes stand for " and \.
A line beginning with # is a comment, and an empty one nothing.
*/

#ifndef COMMAND_LINE_H
#define COMMAND_LINE_H

enum
{
	COMMAND_LINE_MAXIMUM_LENGTH = 255,
	COMMAND_LINE_MAXIMUM_WORDS = 8,
	/* a word, with its terminator */
	COMMAND_LINE_WORD_SIZE = 64,
	/* the longest ban: ten years, in seconds */
	COMMAND_LINE_MAXIMUM_DURATION = 10 * 366 * 24 * 60 * 60,
};

struct command_line
{
	int count;
	char words[COMMAND_LINE_MAXIMUM_WORDS][COMMAND_LINE_WORD_SIZE];
};

/* the words of text (count 0 for an empty line or a comment); 0 if it is not
one (with why in error, error_size bytes) */
int command_line_parse(const char *text, struct command_line *line, char *error, int error_size);

/* a whole decimal number from minimum to maximum: 1 and the number in value,
else 0 */
int command_line_integer(const char *text, long minimum, long maximum, long *value);

/* a duration: a number of minutes ("30"), or numbers each with a unit, s, m,
h, d or w ("90s", "2h", "1d12h"), at most COMMAND_LINE_MAXIMUM_DURATION
seconds; "forever" (or "permanent") is 0. 1 and the seconds in seconds,
else 0 */
int command_line_duration(const char *text, long *seconds);

/* a duration (seconds) as text: "2d 3h", "45m", "forever" for 0 */
void command_line_duration_text(long seconds, char *text, int size);

/* how a player's name (as ASCII) matches what was typed, in either case: 2
the whole name, 1 its beginning, 0 not */
int command_line_name_match(const char *name, const char *text);

/* whether text is a map's name as a command may give it: a file's name
(letters, digits, _ - .), with @ce or @md after it for a Halo PC or HaloMD
map, at most 63 characters, no path */
int command_line_map_name_valid(const char *text);

/* whether text is a name the server may go by: 1 to 15 printable ASCII
characters, not all spaces */
int command_line_server_name_valid(const char *text);

/* a command's output, written into a buffer of the caller's: what does not
fit is cut, and truncated set */
struct command_output
{
	char *text;
	int size;
	int length;
	int truncated;
};

void command_output_begin(struct command_output *output, char *buffer, int size);
void command_output_printf(struct command_output *output, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
	__attribute__((format(printf, 2, 3)))
#endif
	;
/* text as a JSON string, in quotes: ", \ and control characters escaped,
and anything not ASCII written as \u00XX of its byte (the server's text is
ASCII) */
void command_output_json_string(struct command_output *output, const char *text);

#endif
