/*
GAME_REPORT.H

A finished game's carnage report as JSON (game_report.c): the lines of
game_engine_report_lines and the statistics recorder's members
(port/linux/game/game_stats.c), in the form the site's POST /v1/report and
POST /v1/client_report take (list_server.py's clean_report). The desktop
builds' game list (browser.c) and a console's reports
(port/console/reports/console_reports.c) both build theirs here, so the
report is the same wherever it is sent from.

Plain C, no threads and no network: the host tests build it too.
*/

#ifndef __GAME_REPORT_H
#define __GAME_REPORT_H

#include "browser.h"

/* the most a line takes without its extra: its fixed members, a full name
and the host's tag; a line of the scoreboard's numbers alone takes less */
#define GAME_REPORT_LINE_SIZE 640
#define GAME_REPORT_MINIMAL_LINE_SIZE 320

/* a line's tag (the host's report: who may confirm it), as 64 hex digits
and a terminator into tag: 1, or 0 for none */
typedef int (*game_report_tag_function)(const struct browser_report_player *player, void *context, char *tag);

/* UTF-16 (the game's names, without surrogate pairs) to UTF-8, at most
size - 1 bytes and a terminator */
void game_report_utf8(const unsigned short *name, int length, char *text, int size);

/* a name as a JSON string (its quotes and escapes) at out, size bytes at
most: its length */
int game_report_json_name(char *out, int size, const unsigned short *name, int length);

/* the bytes a report of these lines takes at most, with prefix and extra */
unsigned long game_report_size(const char *prefix, const struct browser_report_player *players, int count,
	const char *extra);

/* a report as JSON, allocated (the caller frees it), or NULL (no lines, or
no memory): prefix's members first (NULL for none: "\"invite\": \"...\", "
with its comma), then the teams, the scores, the length and the lines (each
with tag's, when it is given), then extra's members */
char *game_report_json(const char *prefix, int teams, int red_score, int blue_score, int duration_seconds,
	const struct browser_report_player *players, int count, const char *extra, game_report_tag_function tag,
	void *tag_context);

#endif
