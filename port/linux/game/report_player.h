/*
REPORT_PLAYER.H

Reporting a player to a dedicated server's moderators (report_player.c;
docs/delta.md, Reports): DELTA's PLAYERS HERE, and the scoreboard's way
there.
*/

#ifndef __REPORT_PLAYER_H
#define __REPORT_PLAYER_H

enum
{
	/* nothing being reported */
	_report_step_none,
	/* the reason picked */
	_report_step_reason,
	/* a clip picked (none, the last, one of clips/) */
	_report_step_clip,

	REPORT_PLAYER_MOST_CHOICES = 8,
	REPORT_PLAYER_CHOICE_SIZE = 48,
};

/* whether this machine's host takes reports, and a player of a machine
other than this one's can be reported */
boolean report_player_available(
	long machine_index);

/* a report of a player (the game's player list index, their machine and
name in UTF-8) begun: its reason is asked first */
void report_player_begin(
	long player_list_index,
	long machine_index,
	char const *name);

/* the step now, its title, and its choices (the count) */
short report_player_step(
	void);
char const *report_player_title(
	void);
short report_player_choices(
	char choices[][REPORT_PLAYER_CHOICE_SIZE],
	short most);

/* a choice made (its index), or back a step (out at the first) */
void report_player_choose(
	short index);
void report_player_back(
	void);

/* the last report's outcome, as a line ("Report sent"; empty: none), and
whether it is still news (shown for a few seconds) */
char const *report_player_message(
	void);

/* each frame (guide.c): the host's answer heard, the clip sent if it asked */
void report_player_update(
	void);

#endif
