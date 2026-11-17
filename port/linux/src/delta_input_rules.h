/*
DELTA_INPUT_RULES.H

How DELTA's ways in read the keyboard and the controller, kept apart from
SDL so their tests build anywhere (port/linux/tests/delta_input_rules_test.c,
tools/test_delta_input_rules.py):

- controls.delta names one key or a few, separated by commas, as the game's
  other controls do ("F3, Backslash"). On a Mac, F3 is Mission Control and the
  F keys are the Mac's own unless fn is held, so the Mac's default adds
  the backslash key, and prompts there show the first key that isn't an
  F key.
- Back held in the menus (delta.hold_back) opens DELTA. While Back is down
  the menus don't get it: a tap (let go before the hold's time) reaches
  them as a short Back press once it is let go, and a hold never does.
  xinput_sdl.c runs it on the first player's merged pad, so a controller's
  Back and the keyboard's F1 both go through it.
*/

#ifndef DELTA_INPUT_RULES_H
#define DELTA_INPUT_RULES_H

enum
{
	DELTA_KEYS_MOST = 4,
	DELTA_KEY_NAME_SIZE = 24,
	/* (ms) Back held this long in the menus opens DELTA */
	DELTA_BACK_HOLD = 600,
	/* (ms) a tap's Back press as the menus get it, once it is let go, and
	for at least this many reads of the pad (a slow frame still sees it) */
	DELTA_BACK_TAP = 120,
	DELTA_BACK_TAP_READS = 2,
};

/* controls.delta's keys' names, trimmed (at most "most"): their count */
int delta_keys_split(const char *list, char names[][DELTA_KEY_NAME_SIZE], int most);

/* the key a prompt shows for DELTA: the first; on a Mac (apple) the first
that isn't an F key, if there is one ("F3" when the list is empty) */
void delta_key_shown(const char *list, int apple, char *shown, int size);

/* Back held in the menus */
struct delta_back_hold
{
	/* Back is down, since when, and whether this press opened DELTA */
	int held;
	unsigned long since;
	int opened;
	/* a tap's press for the menus, since when, and the reads it was in */
	int tapping;
	unsigned long tap_since;
	int tap_reads;
};

enum
{
	/* the menus get Back this poll */
	DELTA_BACK_PASS = 1,
	/* DELTA opens now */
	DELTA_BACK_OPEN = 2,
};

/* each poll: Back down now, whether a hold may open DELTA (delta.hold_back
on, in the menus, DELTA not open), and a millisecond clock: DELTA_BACK_*
flags */
int delta_back_hold_step(struct delta_back_hold *hold, int back_down, int armed, unsigned long now);
/* Back taken by something else this read (the capture buttons' Back and a
stick): the press opens nothing and leaves no tap, until it is let go.
Called before delta_back_hold_step, each read it is taken */
void delta_back_hold_cancel(struct delta_back_hold *hold);

/* ---------- the hint on reaching the main menu (with delta.menu_item off):
"Press F3 for the Delta Guide" (its line: friends, party and settings), by the
device used last, the first
DELTA_HINT_TIMES times the main menu is reached (delta.hint_shown counts) */

enum
{
	DELTA_HINT_TIMES = 3,
};

/* whether it shows now: fewer than DELTA_HINT_TIMES shown, and no DELTA row
in the menus */
int delta_hint_due(int shown, int menu_item);
/* its title: with a pad used last, "Hold Back for the Delta Guide" (delta.hold_back
on), else "Press the Guide button" (delta.delta_button on), else the
keyboard's "Press F3 for the Delta Guide"; key is the
keyboard's key as its prompts show it (F3, or \ on a Mac) */
void delta_hint_text(char *text, int size, int pad, const char *key, int guide_button, int hold_back);
/* the same for a Mac (apple), where macOS often keeps the controller's Home
button: Hold Back, else the keyboard's key, never Guide */
void delta_hint_text_for(char *text, int size, int pad, const char *key, int guide_button, int hold_back, int apple);

#endif
