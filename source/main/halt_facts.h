/*
HALT_FACTS.H

port: what the halt screen (halt_and_catch_fire, main.c) tells the player:
what stopped the game and where (the assert's reason, file and line), the
map and the mode, the build, and the log's last lines before it. Gathered
once, as the game stops, into fixed buffers: no tag is looked up and
nothing is allocated, as the game's state may be broken. Every build shows
them; the desktop's overlay draws them in the menus' look with ways out
(port/linux/game/halt_screen.c), the others in the game's terminal font.

Plain C on plain types (halt_facts.c): the game's units, the port's and
the tests include it.
*/

#ifndef __HALT_FACTS_H
#define __HALT_FACTS_H

enum
{
	HALT_FACTS_LOG_LINES = 10,
	HALT_FACTS_LINE_SIZE = 256,
};

struct halt_facts
{
	/* the reason (an assert's condition, an error's message) */
	char reason[256];
	/* its file and line, as "cseries/errors.c:97" ("" for none) */
	char place[HALT_FACTS_LINE_SIZE];
	/* the map's name ("" before any) and its kind, in plain words ("Xbox
	map", "Custom Edition map", "MCC map", "Stubbs") */
	char map[64];
	char const *mode;
	char const *build;
	/* the log's last lines before the halt, oldest first, a line repeated
	once with "(x4)" after it; important: an error's or a warning's */
	char lines[HALT_FACTS_LOG_LINES][HALT_FACTS_LINE_SIZE];
	char important[HALT_FACTS_LOG_LINES];
	int line_count;
};

/* the log's last lines before the halt, from the end of its text (length
bytes): the stack walk and the halt's own EXCEPTION lines left out (the
walk stays in debug.txt; the reason is shown on its own), each line's date
and time (and the port's "chupathingyce: ") left off, a line repeated in a
row made one, and the newest errors and warnings kept first (up to half of
them) so that noise does not push them out */
void halt_facts_log_lines(struct halt_facts *facts, char const *text, int length);

/* a source file's place as the screen shows it: from its folder under the
source tree ("c:\\halo\\SOURCE\\cseries\\errors.c" and line 97 as
"cseries/errors.c:97"), else its file's name; "" for no file */
void halt_facts_place(char *out, int size, char const *file, long line);

/* a map's name as the screen shows it: the loaded map's file name without
its family ("bloodgulch@ce" as "bloodgulch") */
void halt_facts_map(char *out, int size, char const *loaded_name);

#endif
