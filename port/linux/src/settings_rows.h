/*
SETTINGS_ROWS.H

The settings rows the setup screen's Settings (launcher.c) and the Guide's
SETTINGS (port/linux/game/guide.c) share: each a config.toml key, its label,
its values and how a step changes it, so that the same setting reads and
changes the same way wherever it shows (settings_rows.c). The rows are drawn
with ui_style_setting (ui_overlay_style.c).

Plain types only: the game's units and the platform's both call it.
*/

#ifndef __SETTINGS_ROWS_H
#define __SETTINGS_ROWS_H

/* where a row shows: the setup screen's tabs, and the Guide's SETTINGS */
enum
{
	SETTINGS_GROUP_DISPLAY,
	SETTINGS_GROUP_AUDIO,
	SETTINGS_GROUP_NETWORK,
	SETTINGS_GROUP_UPDATES,
	SETTINGS_GROUP_GUIDE,
	/* (the Guide's MODERN LIGHTING page,
	which its SETTINGS row of that name opens) */
	SETTINGS_GROUP_LIGHTING,

	NUMBER_OF_SETTINGS_GROUPS
};

struct settings_row;

/* a group's row at index (in this build: rows whose key it has not are
left out); NULL past the last */
const struct settings_row *settings_row_at(int group, int index);
const char *settings_row_label(const struct settings_row *row);
const char *settings_row_key(const struct settings_row *row);
/* whether a change takes a restart to show (the setup screen's NEEDS A
RESTART) */
int settings_row_restart(const struct settings_row *row);
/* whether a change takes a restart to show once the game is running (the
Guide's NEEDS A RESTART): settings_row_restart's, and the settings the
game reads as it starts (clips), which the setup screen sets in time */
int settings_row_restart_in_game(const struct settings_row *row);
/* its value, as the row shows it ("ON", "BORDERLESS", "1920x1080") */
void settings_row_value(const struct settings_row *row, char *text, int size);
/* the next value (step 1) or the one before (-1), written to config.toml;
what shows at once (the window's mode and size) is applied */
void settings_row_change(const struct settings_row *row, int step);
/* a line to show under the rows while this one is chosen (why it can't be
on here: RAY TRACING: NO RAY TRACING ON THIS GPU), or NULL */
const char *settings_row_note(const struct settings_row *row);
/* back to the default (the Performance row: every setting it sets) */
void settings_row_reset(const struct settings_row *row);

/* any setting by its key, as the setup screen's ALL lists them: its value
in words, and a step of it (a boolean turned, a word the next, a number up
or down; text that takes any value is not changed: 0) */
void settings_key_value(const char *key, char *text, int size);
/* whether the row shows its value between arrows (0: a row that opens
something, STEAM ACCOUNT and WORKSHOP FOLDER) */
/* whether a row can't be changed now (its value says UNAVAILABLE, and
settings_row_note why): shown without its arrows */
int settings_row_unavailable(const struct settings_row *row);
int settings_row_arrows(const struct settings_row *row);
/* what a row that opens a page does when changed (the Guide's MODERN
LIGHTING: its page, SETTINGS_GROUP_LIGHTING's rows) */
void settings_rows_set_page_action(void (*action)(void));
/* whether the row opens a page (its value shows what is set there) */
int settings_row_is_page(const struct settings_row *row);
/* what WORKSHOP FOLDER does when changed (the Guide: the folder picker) */
void settings_rows_set_folder_action(void (*action)(void));
/* what the STEAM row does when changed (the Guide: its Workshop's link row); unset, the row is hidden */
void settings_rows_set_steam_link_action(void (*action)(void));
/* whether the Guide has WORKSHOP (MCC on this computer): WORKSHOP MAPS shows
in its SETTINGS only then (guide_workshop.c, each frame) */
void settings_rows_set_workshop_offered(int offered);
int settings_key_change(const char *key, int step);

/* ---------- the Guide's SETTINGS pages (guide.c), which the setup
screen's Settings follows too (launcher.c: a tab for each but ABOUT) */

enum
{
	SETTINGS_PAGE_DISPLAY,
	SETTINGS_PAGE_SOUND,
	SETTINGS_PAGE_GAME,
	SETTINGS_PAGE_GUIDE,
	SETTINGS_PAGE_ABOUT,
	NUMBER_OF_SETTINGS_PAGES,

	/* the page a row no page lists goes on, at its end */
	SETTINGS_PAGE_OTHER = SETTINGS_PAGE_GAME,
};

/* the pages' names, in order ("DISPLAY", ...) */
const char *const *settings_page_names(void);
/* a page's lines, NULL ended: "#" and a caption that heads a block, "="
and a line of information (=version, =platform), else a row by its key (a
setting's config.toml key, or one of the Guide's own: "@devices", "@link",
"@logs0", "@logs1", "@setup", "@about") */
const char *const *settings_page_lines(int page);
/* the page that lists key, or -1 for none */
int settings_key_page(const char *key);
/* the row of a key in any group, as this build shows it (settings_row_at's
rule), or NULL */
const struct settings_row *settings_row_for_key(const char *key);

#endif
