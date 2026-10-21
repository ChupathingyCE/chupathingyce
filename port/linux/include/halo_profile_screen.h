/*
HALO_PROFILE_SCREEN.H

Delta Profile in the game (port/linux/game/profile_screen.c): its screen of
code over the Xbox's and Gearbox's menus, the dialogs it shows over any
menus, and what each of its sections says, which the Chupathingy menus show
with their own widgets (menu_functions.c). Only with HALO_GAME_BROWSER and
without HALO_CONSOLE_NO_DELTA_PROFILE.
*/

#ifndef HALO_PROFILE_SCREEN_H
#define HALO_PROFILE_SCREEN_H

/* a line of a section, and how it shows; a tile's, a row's and a game's
columns are tab separated */
enum
{
	_profile_line_text,
	_profile_line_title,
	_profile_line_sub,
	_profile_line_badges,
	/* a record's number: its label, then its value */
	_profile_line_tile,
	_profile_line_head,
	/* a recent game: when, map, type, result, kills and deaths */
	_profile_line_game,
	/* a label, then its value */
	_profile_line_row,
	_profile_line_dim,
	_profile_line_warn,
};

struct profile_line
{
	short style;
	char text[192];
};

struct halo_ui_pointer;

/* whether the ways in show: display.delta_profile, and a game list */
boolean profile_screen_available(void);

/* the screen of code (the Xbox's menus from Online Games' Start, which it
goes back to; Gearbox's from Select Profile), and whether it or a dialog of
its is up, taking the input */
void profile_screen_open(boolean from_online_games);
boolean profile_screen_active(void);
boolean profile_screen_dialog_active(void);
boolean profile_screen_dialog_only(void);
void profile_screen_process(void);
void profile_screen_render(void);
void profile_screen_pointer(struct halo_ui_pointer const *pointer);

/* the sections the Chupathingy menus' screen treats as its own: CLAN
(its emblem drawn over their panel: profile_screen_card_emblem) and
PRIVACY (a settings page of theirs) */
#define PROFILE_SCREEN_SECTION_CLAN 3
#define PROFILE_SCREEN_SECTION_PRIVACY 4

/* the sections: their count, names (LINK PROFILE or UNLINK THIS COPY),
whether one can be gone to now, their lines, and the action of their A
(a dialog: TRUE when there was one) */
short profile_screen_section_count(void);
char const *profile_screen_section_name(short section);
boolean profile_screen_section_open(short section);
short profile_screen_panel_lines(short section, struct profile_line *lines, short maximum);
boolean profile_screen_section_action(short section);
/* the profile asked of the site again (Y) */
void profile_screen_refresh(void);

/* the clan's emblem drawn over the Chupathingy menus' panel, size square,
x and y its top left (640 by 480), while the panel asks for it each update;
a size of 0 hides it at once */
void profile_screen_card_emblem(float x, float y, float size);

/* the Chupathingy menus' Profiles: its DELTA PROFILE row's summary, and a
Halo profile's "Delta:" line (FALSE: hidden) */
short profile_screen_summary(struct profile_line *lines, short maximum);
boolean profile_screen_delta_line(char *text, int size);

/* the site's privacy settings as the Chupathingy menus' page reads and
writes them (site.hide_stats, site.hide_heatmaps, site.show_clan_tag):
FALSE while not known */
boolean profile_screen_site_setting(char const *field, char *text, int size);
boolean profile_screen_site_setting_write(char const *field, char const *value);

/* the name a player shows by on the scoreboard and in the lobby
(profile_names.c): their Delta profile's, when they share it in a game on
Delta and display.profile_names is on; FALSE: their game name, as before */
boolean profile_names_player(long player_index, wchar_t *name, long size);

#endif
