/*
PROFILE_SCREEN.C

Delta Profile in the game (configure.py --game-browser; docs/delta.md,
Delta Profile): the player's own profile on the Chupathingy site, as this
copy's player key reaches it (browser.c's browser_profile_*). The game
shows it and links or unlinks this copy; it never makes, renames or deletes
a profile, and account matters (password, email, two-step sign-in, export,
deletion) stay on the site, reached with OPEN ON THE WEB. Linking is per
copy: every Halo profile on this copy counts for the one Delta profile.

The same sections in every menu set: OVERVIEW, LINKED COPIES, ROLES,
CLAN, PRIVACY, then LINK PROFILE (or UNLINK THIS COPY) and OPEN ON THE
WEB. CLAN shows the profile's clan (its emblem, tag, motto and members by
rank) or the clans that invited it; its A opens a dialog to show or hide
the tag, leave the clan, or join or decline an invite. What each shows is made here once (profile_screen_panel_lines):
- the Chupathingy menus show it with their own widgets
  (port/assets/menus/chupathingy/delta_profile.xml, filled by
  menu_functions.c), their Privacy a settings page of theirs
  (tools/port_settings.py);
- the Xbox's menus (Online Games' Start) and Gearbox's (Select Profile's
  DELTA) show it on a screen of code over the menus, drawn as
  Online Games is (browser_screen.c), Privacy's rows in it.
The dialogs with a QR code (Link Profile, Open on the Web, unlinking another
copy) and the question before unlinking this one are drawn here for all
three, over whichever screen asked for them.

Kept in memory only; debug.txt says whether the profile loaded, never what
it holds. No other player's name shows, but a server owner's handle on a
role, as the site shows its holder; no address anywhere.
*/

#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_DELTA_PROFILE)

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "cutscene/cinematics.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/interface.h"
#include "rasterizer/rasterizer.h"
#include "bitmaps/bitmap_group.h"
#include "interface/ui_widget.h"
#include "tag_files/tag_groups.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "saved games/saved_game_files.h"
#include "../src/browser.h"
#include "../src/delta_profile.h"
#include "../src/clan_emblem.h"
#include "../src/ui_overlay.h"
#include "halo_ui_pointer.h"
#include "halo_profile_screen.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int config_boolean(char const *name);
char const *config_string(char const *name);
int config_write(char const *name, char const *value);
void platform_log(char const *format, ...);
short pc_menus_set(void);
void browser_screen_open(void);
boolean ui_widget_port_open_screen(char const *name);

/* ---------- constants */

enum
{
	/* (event_manager.c's event types) */
	PROFILE_EVENT_LEFT_STICK = 1,
	PROFILE_EVENT_BUTTON = 3,

	/* the screen takes no A this soon after it opens, nor a button this soon
	after a dialog opens or closes */
	OPEN_SETTLE = 500,
	DIALOG_SETTLE = 400,
	/* a held direction (as Online Games) */
	STEP_FIRST_REPEAT_DELAY = 350,
	STEP_REPEAT_INTERVAL = 110,
	STEP_RELEASE_GAP = 150,
	/* "Getting your profile..." only after this long */
	LOADING_SHOWN_AFTER = 300,
	/* the profile asked for again this often while the site does not answer,
	and while another copy's unlinking is waited for (the site takes a key's
	requests 6 a minute) */
	RETRY_INTERVAL = 30000,
	COPIES_POLL_INTERVAL = 15000,
	/* a request for the emblem on the Chupathingy menus' panel is kept
	this long (the panel asks for it as it updates) */
	CARD_EMBLEM_KEPT = 120,
	/* the clan's members its panel shows (more: "+N more"), and the
	emblem's pixels (square) */
	CLAN_PANEL_MEMBERS = 9,
	EMBLEM_PIXELS = 128,
	MAXIMUM_TARGETS = 32,
	/* (ui_widget.c's feedback sound for a choice that does nothing) */
	SOUND_ERROR = 4,
	MAXIMUM_PANEL_LINES = 24,
};

/* the sections */
enum
{
	_section_overview,
	_section_copies,
	_section_roles,
	_section_clan,
	_section_privacy,
	/* LINK PROFILE, or UNLINK THIS COPY */
	_section_link,
	_section_web,

	NUMBER_OF_PROFILE_SECTIONS
};

/* the dialogs */
enum
{
	_dialog_none,
	_dialog_link,
	_dialog_unlink,
	_dialog_web,
	_dialog_copies,
	_dialog_clan,
};

/* the clan dialog: its rows, the question before leaving, and a request
sent (its answer shown) */
enum
{
	_clan_rows,
	_clan_asking,
	_clan_sent,
};

/* the clan dialog's rows with a clan */
enum
{
	_clan_row_tag,
	_clan_row_leave,

	NUMBER_OF_CLAN_ROWS
};

/* Privacy's rows (the overlay's; the Chupathingy menus' page
has the same: tools/port_settings.py) */
enum
{
	_privacy_game,
	_privacy_site,
};

struct privacy_row
{
	char const *label;
	short kind;
	/* the setting (config.toml's), or the site's field */
	char const *name;
	/* its values and how they show, the default first */
	char const *values[4];
	char const *shown[4];
	char const *help;
};

static struct privacy_row const privacy_rows[] =
{
	{ "SHARE MY PROFILE", _privacy_game, "network.share_profile", { "false", "true" }, { "OFF", "ON" },
		"Show your Delta name to Chupathingy players." },
	{ "RECORD GAMES I JOIN", _privacy_game, "network.report_joined_games", { "true", "false" }, { "ON", "OFF" },
		"Report the games you join." },
	{ "RECORD GAMES I HOST", _privacy_game, "network.report_hosted_games", { "true", "false" }, { "ON", "OFF" },
		"Report the games you host." },
	{ "DELTA STATS DETAIL", _privacy_game, "network.report_events", { "true", "false" }, { "ON", "OFF" },
		"Send kills, weapons and medals as host." },
	{ "HIDE MY RECORD", _privacy_site, "hide_stats", { "false", "true" }, { "NO", "YES" },
		"Hide your record from other players." },
	{ "HIDE MY HEATMAPS", _privacy_site, "hide_heatmaps", { "false", "true" }, { "NO", "YES" },
		"Hide your heatmaps from other players." },
	{ "SHOW CLAN TAG", _privacy_site, "show_clan_tag", { "true", "false" }, { "ON", "OFF" },
		"Show your clan tag to Chupathingy players." },
#ifndef HALO_CONSOLE_NO_DISCORD
	{ "DISCORD STATUS", _privacy_game, "discord.presence", { "full", "minimal", "off" }, { "FULL", "MINIMAL", "OFF" },
		"What Discord shows while you play." },
#endif
	{ "SHOW PROFILE NAMES", _privacy_game, "display.profile_names", { "true", "false" }, { "ON", "OFF" },
		"Show players by their Delta names." },
};

/* (halo_profile_screen.h names two of them for the Chupathingy menus) */
typedef char profile_screen_sections_named[(_section_clan == PROFILE_SCREEN_SECTION_CLAN &&
	_section_privacy == PROFILE_SCREEN_SECTION_PRIVACY) ? 1 : -1];

static char const *const section_names[NUMBER_OF_PROFILE_SECTIONS] =
{
	"OVERVIEW", "LINKED COPIES", "ROLES", "CLAN", "PRIVACY", "LINK PROFILE", "OPEN ON THE WEB",
};

/* the multiplayer maps' names in the menus (as Online Games names them) */
static char const *const map_names[][2] =
{
	{ "beavercreek", "Battle Creek" }, { "bloodgulch", "Blood Gulch" }, { "boardingaction", "Boarding Action" },
	{ "carousel", "Derelict" }, { "chillout", "Chill Out" }, { "damnation", "Damnation" },
	{ "hangemhigh", "Hang 'Em High" }, { "longest", "Longest" }, { "prisoner", "Prisoner" },
	{ "putput", "Chiron TL-34" }, { "ratrace", "Rat Race" }, { "sidewinder", "Sidewinder" }, { "wizard", "Wizard" },
};

/* ---------- globals */

static struct
{
	/* the screen of code up (the Xbox's and Gearbox's menus), and a dialog
	up (over it, or over the Chupathingy menus' screen) */
	boolean active;
	short dialog;
	/* Online Games opened it: back there when it closes */
	boolean from_online_games;
	unsigned long opened_time;
	unsigned long dialog_time;
	short section;
	/* (the screen of code's) in the section's rows, and which */
	boolean in_panel;
	short row;
	/* the last answer, and the status with it */
	struct browser_profile_status status;
	struct delta_profile profile;
	int profile_serial;
	unsigned long asked_time;
	unsigned long last_load_time;
	/* Link Profile's panel */
	struct browser_connect connect;
	/* a QR code of a page of the site, and its address */
	int page_qr_size;
	unsigned char page_qr[BROWSER_CONNECT_QR_SIZE * BROWSER_CONNECT_QR_SIZE];
	char page[160];
	/* the copy count when Unlink A Copy opened (fewer: one was unlinked) */
	short copies_before;
	/* what the pointer may press this frame */
	short target_count;
	struct
	{
		float x0, y0, x1, y1;
		short button;
		/* a section or a row, else NONE */
		short section;
		short row;
	} targets[MAXIMUM_TARGETS];
	/* the Chupathingy menus' Profiles: the summary their DELTA row
	shows asked for (profile_screen_profiles_row) */
	unsigned long summary_time;
	/* the clan dialog's row, and where it is (its rows, the question
	before leaving, a request sent) */
	short dialog_row;
	short clan_state;
	/* the emblem the Chupathingy menus' panel asked for (where, and when) */
	float card_x, card_y, card_size;
	unsigned long card_time;
	/* the emblem's pixels, and which emblem they are */
	unsigned char *emblem_pixels;
	int emblem_serial;
} profile_screen;

/* ---------- the profile */

boolean profile_screen_available(
	void)
{
	return config_boolean("display.delta_profile") && config_string("network.browser_url")[0];
}

/* the last answer read, and the profile asked for if wanted (a minute old,
or as asked) */
static void refresh(
	boolean force)
{
	unsigned long now = system_milliseconds();

	if (force || !profile_screen.asked_time)
	{
		browser_profile_load(force);
		profile_screen.asked_time = now;
		profile_screen.last_load_time = now;
	}
	/* (again while the site does not answer, and while another copy's
	unlinking is waited for) */
	else if ((profile_screen.status.state == BROWSER_PROFILE_FAILED && now - profile_screen.last_load_time > RETRY_INTERVAL) ||
		(profile_screen.dialog == _dialog_copies && now - profile_screen.last_load_time > COPIES_POLL_INTERVAL))
	{
		browser_profile_load(TRUE);
		profile_screen.last_load_time = now;
	}
	else
		browser_profile_load(FALSE);
	browser_profile_get(&profile_screen.status, NULL);
	if (profile_screen.status.serial != profile_screen.profile_serial)
	{
		browser_profile_get(&profile_screen.status, &profile_screen.profile);
		profile_screen.profile_serial = profile_screen.status.serial;
	}
}

static boolean linked(
	void)
{
	return profile_screen.status.has_profile && profile_screen.profile.linked;
}

/* a section the player can go to now (Linked Copies and Roles need a
linked profile) */
static boolean section_open(
	short section)
{
	if (section == _section_copies || section == _section_roles || section == _section_clan)
		return linked() && !profile_screen.profile.locked;
	return TRUE;
}

char const *profile_screen_section_name(
	short section)
{
	if (section == _section_link && linked())
		return "UNLINK THIS COPY";
	return section >= 0 && section < NUMBER_OF_PROFILE_SECTIONS ? section_names[section] : "";
}

boolean profile_screen_section_open(
	short section)
{
	return section_open(section);
}

short profile_screen_section_count(
	void)
{
	return NUMBER_OF_PROFILE_SECTIONS;
}

/* ---------- the words */

static char const *map_name(
	char const *map)
{
	short index;

	for (index = 0; index < NUMBEROF(map_names); index++)
	{
		if (!strcmp(map, map_names[index][0]))
			return map_names[index][1];
	}
	return map;
}

static char const *platform_name(
	char const *platform)
{
	static char const *const names[][2] =
	{
		{ "pc_linux", "Linux PC" }, { "pc_windows", "Windows PC" }, { "pc_macos", "Mac" },
		{ "android", "Android" }, { "steam_deck", "Steam Deck" }, { "xbox", "Xbox" }, { "xbox360", "Xbox 360" },
		{ "xbox_series", "Xbox Series" }, { "psp", "PSP" }, { "vita", "PS Vita" }, { "wiiu", "Wii U" },
		{ "pc_freebsd", "FreeBSD PC" }, { "xbox_uwp", "Xbox Series" }, { "switch", "Switch" }, { "ps3", "PS3" },
		{ "ps4", "PS4" }, { "ps5", "PS5" }, { "n64", "Nintendo 64" }, { "pc_legacy", "Legacy PC" },
	};
	short index;

	for (index = 0; index < NUMBEROF(names); index++)
	{
		if (!strcmp(platform, names[index][0]))
			return names[index][1];
	}
	return platform[0] ? platform : "Copy";
}

/* a count with thousands' commas */
static void number_text(
	long long value,
	char *text,
	int size)
{
	char digits[32];
	int length, index, used = 0;

	snprintf(digits, sizeof(digits), "%lld", value < 0 ? 0 : value);
	length = (int)strlen(digits);
	for (index = 0; index < length && used < size - 2; index++)
	{
		if (index && (length - index) % 3 == 0)
			text[used++] = ',';
		text[used++] = digits[index];
	}
	text[used] = 0;
}

/* when, as a person says it: "2h ago", "Yesterday", "Oct 12" */
static void when_text(
	long long when,
	char *text,
	int size)
{
	time_t now = time(NULL);
	time_t moment = (time_t)when;
	long long ago = (long long)now - when;
	struct tm *local;

	if (when <= 0)
	{
		snprintf(text, (size_t)size, "never");
		return;
	}
	if (ago < 3600)
		snprintf(text, (size_t)size, "%lldm ago", ago < 60 ? 1 : ago / 60);
	else if (ago < 86400)
		snprintf(text, (size_t)size, "%lldh ago", ago / 3600);
	else if (ago < 2 * 86400)
		snprintf(text, (size_t)size, "Yesterday");
	else if ((local = localtime(&moment)) != NULL)
		strftime(text, (size_t)size, "%b %d", local);
	else
		snprintf(text, (size_t)size, "long ago");
}

static void month_year_text(
	long long when,
	char *text,
	int size)
{
	time_t moment = (time_t)when;
	struct tm *local = when > 0 ? localtime(&moment) : NULL;

	if (local)
		strftime(text, (size_t)size, "%b %Y", local);
	else
		text[0] = 0;
}

static char const *role_name(
	char const *role)
{
	if (!strcmp(role, "owner"))
		return "Owner";
	if (!strcmp(role, "admin"))
		return "Admin";
	if (!strcmp(role, "moderator"))
		return "Moderator";
	return role;
}

/* ---------- the panel's lines (every menu set's) */

static short add_line(
	struct profile_line *lines,
	short count,
	short maximum,
	short style,
	char const *format,
	...)
{
	va_list arguments;

	if (count >= maximum)
		return count;
	lines[count].style = style;
	va_start(arguments, format);
	vsnprintf(lines[count].text, sizeof(lines[count].text), format, arguments);
	va_end(arguments);
	return (short)(count + 1);
}

static short privacy_value(struct privacy_row const *row);

/* a game type's short name ("Capture the Flag": "CTF") */
static char const *type_name(
	char const *type)
{
	if (!strcmp(type, "Capture the Flag"))
		return "CTF";
	if (!strcmp(type, "King of the Hill"))
		return "King";
	return type;
}

/* a result, short: "Lost 46 to 50" as "Lost 46-50" */
static void result_text(
	char const *result,
	char *text,
	int size)
{
	char const *to = strstr(result, " to ");

	if (to)
		snprintf(text, (size_t)size, "%.*s-%s", (int)(to - result), result, to + 4);
	else
		snprintf(text, (size_t)size, "%s", result);
}

/* a state that keeps the profile from showing: its lines, or 0 when the
profile shows */
static short state_lines(
	struct profile_line *lines,
	short maximum)
{
	struct browser_profile_status const *status = &profile_screen.status;
	short count = 0;

	if (!config_boolean("network.online"))
		return add_line(lines, count, maximum, _profile_line_text, "Online play is off");
	if (status->state == BROWSER_PROFILE_NO_KEY)
		return add_line(lines, count, maximum, _profile_line_text, "No player key");
	if (status->state == BROWSER_PROFILE_NO_LIST)
		return add_line(lines, count, maximum, _profile_line_text, "No game list");
	if (!status->has_profile)
	{
		if (status->state == BROWSER_PROFILE_FAILED)
			return add_line(lines, count, maximum, _profile_line_warn, "Site not answering");
		if (system_milliseconds() - profile_screen.opened_time > LOADING_SHOWN_AFTER || !profile_screen.active)
			return add_line(lines, count, maximum, _profile_line_dim, "Loading\xE2\x80\xA6");
		return add_line(lines, count, maximum, _profile_line_dim, " ");
	}
	if (profile_screen.profile.linked && profile_screen.profile.locked)
	{
		count = add_line(lines, count, maximum, _profile_line_title, "%s", profile_screen.profile.display);
		count = add_line(lines, count, maximum, _profile_line_sub, "@%s", profile_screen.profile.handle);
		return add_line(lines, count, maximum, _profile_line_warn, "Profile locked");
	}
	return 0;
}

/* the panel's last line: the site isn't answering, with the profile it
gave before */
static short stale_line(
	struct profile_line *lines,
	short count,
	short maximum)
{
	if (profile_screen.status.state != BROWSER_PROFILE_FAILED || !profile_screen.status.has_profile)
		return count;
	return add_line(lines, count, maximum, _profile_line_warn, "Offline \xC2\xB7 %lu min old",
		(profile_screen.status.age_seconds + 59) / 60);
}

static short not_linked_lines(
	struct profile_line *lines,
	short count,
	short maximum)
{
	count = add_line(lines, count, maximum, _profile_line_title, "Not linked");
	if (profile_screen.profile.games > 0)
		count = add_line(lines, count, maximum, _profile_line_row, "WAITING\t%d game%s", profile_screen.profile.games,
			profile_screen.profile.games == 1 ? "" : "s");
	return count;
}

/* the badges, as many as fit in width characters, then how many more */
static void badges_text(
	struct delta_profile const *profile,
	char *text,
	int size,
	int width)
{
	short index;

	text[0] = 0;
	for (index = 0; index < profile->badge_count; index++)
	{
		int more = profile->badge_count - index - 1;
		int length = (int)strlen(text) + (index ? 3 : 0) + (int)strlen(profile->badges[index]) + (more ? 4 : 0);

		if (index && length > width)
			break;
		if (index)
			strncat(text, " \xC2\xB7 ", (size_t)size - strlen(text) - 1);
		strncat(text, profile->badges[index], (size_t)size - strlen(text) - 1);
	}
	if (index < profile->badge_count)
		snprintf(text + strlen(text), (size_t)size - strlen(text), " +%d", profile->badge_count - index);
}

static short overview_lines(
	struct profile_line *lines,
	short count,
	short maximum)
{
	struct delta_profile const *profile = &profile_screen.profile;
	char since[32], badges[160], value[32];
	short index;

	month_year_text(profile->created, since, sizeof(since));
	count = add_line(lines, count, maximum, _profile_line_title, "%s", profile->display);
	count = add_line(lines, count, maximum, _profile_line_sub, "@%s%s%s%s", profile->handle, since[0] ? " \xC2\xB7 since " : "",
		since, profile->hidden ? " \xC2\xB7 hidden" : "");
	badges_text(profile, badges, sizeof(badges), 24);
	if (badges[0])
		count = add_line(lines, count, maximum, _profile_line_badges, "%s", badges);
	if (profile->has_record)
	{
		long long hours = (profile->record_seconds + 1800) / 3600;

		number_text(profile->record_games, value, sizeof(value));
		count = add_line(lines, count, maximum, _profile_line_tile, "GAMES\t%s", value);
		number_text(profile->record_wins, value, sizeof(value));
		count = add_line(lines, count, maximum, _profile_line_tile, "WINS\t%s", value);
		number_text(profile->record_kills, value, sizeof(value));
		count = add_line(lines, count, maximum, _profile_line_tile, "KILLS\t%s", value);
		count = add_line(lines, count, maximum, _profile_line_tile, "K/D\t%.2f",
			(double)profile->record_kills / (double)(profile->record_deaths ? profile->record_deaths : 1));
		number_text(profile->record_assists, value, sizeof(value));
		count = add_line(lines, count, maximum, _profile_line_tile, "ASSISTS\t%s", value);
		number_text(profile->record_medals, value, sizeof(value));
		count = add_line(lines, count, maximum, _profile_line_tile, "MEDALS\t%s", value);
		number_text(hours, value, sizeof(value));
		count = add_line(lines, count, maximum, _profile_line_tile, "HOURS\t%s", value);
		count = add_line(lines, count, maximum, _profile_line_tile, "BEST\t%s",
			profile->record_best_type[0] ? type_name(profile->record_best_type) : "-");
	}
	if (profile->recent_count)
		count = add_line(lines, count, maximum, _profile_line_head, "RECENT GAMES\tTYPE\tPLACE\tK-D");
	for (index = 0; index < profile->recent_count; index++)
	{
		struct delta_profile_game const *game = &profile->recent[index];
		char result[48];

		result_text(game->result, result, sizeof(result));
		count = add_line(lines, count, maximum, _profile_line_game, "%s\t%s\t%s\t%d-%d", map_name(game->map),
			type_name(game->type), result, game->kills, game->deaths);
	}
	return count;
}

static short copies_lines(
	struct profile_line *lines,
	short count,
	short maximum)
{
	struct delta_profile const *profile = &profile_screen.profile;
	short index;

	for (index = 0; index < profile->copy_count; index++)
	{
		struct delta_profile_copy const *copy = &profile->copies[index];
		char seen[32];

		when_text(copy->last_seen, seen, sizeof(seen));
		if (copy->this_copy)
			count = add_line(lines, count, maximum, _profile_line_row, "%s %s\tThis copy", platform_name(copy->platform), copy->label);
		else if (copy->last_seen <= 0)
			count = add_line(lines, count, maximum, _profile_line_row, "%s %s\tNever played", platform_name(copy->platform), copy->label);
		else
			count = add_line(lines, count, maximum, _profile_line_row, "%s %s\tPlayed %s", platform_name(copy->platform), copy->label,
				seen);
	}
	if (profile->more_copies)
		count = add_line(lines, count, maximum, _profile_line_row, "+%d more\t", profile->more_copies);
	return count;
}

static short roles_lines(
	struct profile_line *lines,
	short count,
	short maximum)
{
	struct delta_profile const *profile = &profile_screen.profile;
	short index;

	if (!profile->role_count)
		count = add_line(lines, count, maximum, _profile_line_text, "No roles");
	for (index = 0; index < profile->role_count; index++)
	{
		struct delta_profile_role const *role = &profile->roles[index];

		count = add_line(lines, count, maximum, _profile_line_row, "%s\t%s \xC2\xB7 %s", role->name, role_name(role->role),
			role->online ? "On" : "Off");
	}
	if (profile->more_roles)
		count = add_line(lines, count, maximum, _profile_line_row, "+%d more\t", profile->more_roles);
	return count;
}

static char const *rank_name(
	char const *rank)
{
	if (!strcmp(rank, "leader"))
		return "Leader";
	if (!strcmp(rank, "officer"))
		return "Officer";
	return "Member";
}

/* the clan: its name, tag and size, motto, then its members by rank (the
first CLAN_PANEL_MEMBERS, then how many more); or the clans that invited
the profile */
static short clan_lines(
	struct profile_line *lines,
	short count,
	short maximum)
{
	struct delta_profile const *profile = &profile_screen.profile;
	short index;

	if (profile->has_clan)
	{
		struct delta_profile_clan const *clan = &profile->clan;
		short shown = (short)MIN(clan->member_count, CLAN_PANEL_MEMBERS + 1);

		if (clan->count > shown)
			shown = (short)MIN(shown, CLAN_PANEL_MEMBERS);
		count = add_line(lines, count, maximum, _profile_line_title, "%s", clan->name);
		count = add_line(lines, count, maximum, _profile_line_sub, "[%s] \xC2\xB7 %d member%s", clan->tag, clan->count,
			clan->count == 1 ? "" : "s");
		if (clan->motto[0])
			count = add_line(lines, count, maximum, _profile_line_badges, "%s", clan->motto);
		count = add_line(lines, count, maximum, _profile_line_head, "MEMBERS\t\t\tRANK");
		for (index = 0; index < shown; index++)
		{
			struct delta_profile_clan_member const *member = &clan->members[index];

			count = add_line(lines, count, maximum, _profile_line_row, "%s\t%s%s", member->display, rank_name(member->rank),
				member->you ? " \xC2\xB7 You" : "");
		}
		if (clan->count > shown)
			count = add_line(lines, count, maximum, _profile_line_row, "+%d more\t", clan->count - shown);
		return count;
	}
	count = add_line(lines, count, maximum, _profile_line_title, "No clan");
	if (!profile->invite_count)
		return add_line(lines, count, maximum, _profile_line_dim, "No invites");
	count = add_line(lines, count, maximum, _profile_line_head, "INVITES\t\t\tMEMBERS");
	for (index = 0; index < profile->invite_count && index < CLAN_PANEL_MEMBERS + 1; index++)
		count = add_line(lines, count, maximum, _profile_line_row, "[%s] %s\t%d", profile->invites[index].tag,
			profile->invites[index].name, profile->invites[index].count);
	if (profile->invite_count > index)
		count = add_line(lines, count, maximum, _profile_line_row, "+%d more\t", profile->invite_count - index);
	return count;
}

static short link_lines(
	struct profile_line *lines,
	short count,
	short maximum)
{
	if (!linked())
		return add_line(lines, count, maximum, _profile_line_text, "Link this copy to a Delta profile.");
	return add_line(lines, count, maximum, _profile_line_text, "Unlink this copy from @%s.", profile_screen.profile.handle);
}

static short web_lines(
	struct profile_line *lines,
	short count,
	short maximum)
{
	char site[128];

	browser_server_name(site, sizeof(site));
	return add_line(lines, count, maximum, _profile_line_text, "%s/profile", site);
}

/* Privacy's rows and their values */
static short privacy_lines(
	struct profile_line *lines,
	short count,
	short maximum)
{
	short index;

	for (index = 0; index < NUMBEROF(privacy_rows); index++)
	{
		short value = privacy_value(&privacy_rows[index]);

		count = add_line(lines, count, maximum, _profile_line_row, "%s\t%s", privacy_rows[index].label,
			value == NONE ? "-" : privacy_rows[index].shown[value]);
	}
	return count;
}

short profile_screen_panel_lines(
	short section,
	struct profile_line *lines,
	short maximum)
{
	short count;

	refresh(FALSE);
	/* (Privacy's game rows, and Link and Open on the Web, work whatever the
	state; the rest need the profile) */
	if (section == _section_privacy)
		return privacy_lines(lines, 0, maximum);
	if (section == _section_web)
		return web_lines(lines, 0, maximum);
	count = state_lines(lines, maximum);
	if (count)
		return section == _section_link && profile_screen.status.has_profile && !linked() ? link_lines(lines, 0, maximum) : count;
	if (!section_open(section))
		return add_line(lines, 0, maximum, _profile_line_text, "Not linked");
	switch (section)
	{
	case _section_overview: count = linked() ? overview_lines(lines, 0, maximum) : not_linked_lines(lines, 0, maximum); break;
	case _section_copies: count = copies_lines(lines, 0, maximum); break;
	case _section_roles: count = roles_lines(lines, 0, maximum); break;
	case _section_clan: count = clan_lines(lines, 0, maximum); break;
	case _section_link: count = link_lines(lines, 0, maximum); break;
	default: count = 0; break;
	}
	return stale_line(lines, count, maximum);
}

/* the "Delta:" line of a Halo profile's panel; FALSE when it is hidden */
boolean profile_screen_delta_line(
	char *text,
	int size)
{
	char handle[64];

	if (!profile_screen_available())
		return FALSE;
	if (browser_profile_linked(handle, sizeof(handle)) && handle[0])
		snprintf(text, (size_t)size, "Delta: @%s", handle);
	else
		snprintf(text, (size_t)size, "Delta: not linked");
	return TRUE;
}

/* the Chupathingy menus' Profiles, its DELTA row chosen: its
summary (a title, then label and value rows), the profile asked for */
short profile_screen_summary(
	struct profile_line *lines,
	short maximum)
{
	struct delta_profile const *profile = &profile_screen.profile;
	char value[32];
	short count;

	refresh(FALSE);
	count = state_lines(lines, maximum);
	if (count)
		return count;
	if (!linked())
		return not_linked_lines(lines, count, maximum);
	count = add_line(lines, count, maximum, _profile_line_title, "%s", profile->display);
	count = add_line(lines, count, maximum, _profile_line_sub, "@%s", profile->handle);
	if (profile->has_record)
	{
		number_text(profile->record_games, value, sizeof(value));
		count = add_line(lines, count, maximum, _profile_line_row, "GAMES\t%s", value);
		number_text(profile->record_wins, value, sizeof(value));
		count = add_line(lines, count, maximum, _profile_line_row, "WINS\t%s", value);
		count = add_line(lines, count, maximum, _profile_line_row, "K/D\t%.2f",
			(double)profile->record_kills / (double)(profile->record_deaths ? profile->record_deaths : 1));
	}
	return add_line(lines, count, maximum, _profile_line_row, "COPIES\t%d", profile->copy_count + profile->more_copies);
}

/* ---------- the actions (every menu set's) */

static void open_dialog(
	short dialog)
{
	profile_screen.dialog = dialog;
	profile_screen.dialog_time = system_milliseconds();
	profile_screen.dialog_row = 0;
	profile_screen.clan_state = _clan_rows;
	browser_profile_action_seen();
	if (dialog == _dialog_web || dialog == _dialog_copies)
	{
		profile_screen.page_qr_size = browser_page_qr(dialog == _dialog_web ? "/profile" : "/profile#copies",
			profile_screen.page, sizeof(profile_screen.page), profile_screen.page_qr);
		profile_screen.copies_before = (short)profile_screen.profile.copy_count;
		profile_screen.last_load_time = system_milliseconds();
	}
	/* (the press that opened it is not the dialog's) */
	event_manager_flush();
}

static void close_dialog(
	void)
{
	if (profile_screen.dialog == _dialog_link)
	{
		browser_connect_stop();
		/* (a link made: the profile again, and its overview) */
		if (profile_screen.connect.state == BROWSER_CONNECT_CONNECTED)
		{
			refresh(TRUE);
			profile_screen.section = _section_overview;
			profile_screen.in_panel = FALSE;
		}
	}
	profile_screen.dialog = _dialog_none;
	profile_screen.dialog_time = system_milliseconds();
	browser_profile_action_seen();
}

/* Link Profile, for player 1's Halo profile (its name goes to the page, to
say who it links) */
static void open_link(
	void)
{
	struct player_profile profile;
	unsigned short name[BROWSER_PLAYER_NAME_LENGTH];
	long profile_index = player_ui_get_active_player_profile_index(0);
	long index;

	csmemset(name, 0, sizeof(name));
	if (profile_index == NONE)
		profile_index = player_ui_get_player1_last_used_profile_index();
	if (profile_index != NONE && TEST_FLAG(profile_index, _saved_game_file_index_valid_bit) &&
		player_profile_get(profile_index, &profile))
	{
		for (index = 0; index < NUMBEROF(name) - 1 && index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH &&
			profile.player_name[index]; index++)
		{
			name[index] = (unsigned short)profile.player_name[index];
		}
	}
	browser_connect_start(name);
	browser_connect_get(&profile_screen.connect);
	open_dialog(_dialog_link);
}

/* a section's A: Link Profile, the question before unlinking, Open on the
Web's dialog; TRUE when it did something */
boolean profile_screen_section_action(
	short section)
{
	refresh(FALSE);
	switch (section)
	{
	case _section_link:
		if (linked() && !profile_screen.profile.locked)
			open_dialog(_dialog_unlink);
		else if (profile_screen.status.has_profile && !linked())
			open_link();
		else if (profile_screen.status.state == BROWSER_PROFILE_FAILED || profile_screen.status.state == BROWSER_PROFILE_NO_KEY)
			open_link();
		else
			return FALSE;
		return TRUE;
	case _section_web:
		open_dialog(_dialog_web);
		return TRUE;
	case _section_copies:
		if (!linked())
			return FALSE;
		open_dialog(_dialog_copies);
		return TRUE;
	case _section_clan:
		if (!linked() || profile_screen.profile.locked || (!profile_screen.profile.has_clan && !profile_screen.profile.invite_count))
			return FALSE;
		open_dialog(_dialog_clan);
		return TRUE;
	default:
		return FALSE;
	}
}

void profile_screen_refresh(
	void)
{
	refresh(TRUE);
}

boolean profile_screen_dialog_active(
	void)
{
	return profile_screen.dialog != _dialog_none;
}

/* a dialog up over the menus' own screen (the Chupathingy menus'), which
are drawn under it */
boolean profile_screen_dialog_only(
	void)
{
	return profile_screen.dialog != _dialog_none && !profile_screen.active;
}

/* a site setting's value ("true", "false"), for the Chupathingy menus'
page (site.hide_stats, site.hide_heatmaps): FALSE while it is not known */
boolean profile_screen_site_setting(
	char const *field,
	char *text,
	int size)
{
	refresh(FALSE);
	if (!linked() || !profile_screen.profile.has_privacy || profile_screen.profile.locked)
		return FALSE;
	if (!strcmp(field, "hide_stats"))
		snprintf(text, (size_t)size, "%s", profile_screen.profile.hide_stats ? "true" : "false");
	else if (!strcmp(field, "hide_heatmaps"))
		snprintf(text, (size_t)size, "%s", profile_screen.profile.hide_heatmaps ? "true" : "false");
	else if (!strcmp(field, "show_presence"))
		snprintf(text, (size_t)size, "%s", profile_screen.profile.show_presence ? "true" : "false");
	else if (!strcmp(field, "show_clan_tag"))
		snprintf(text, (size_t)size, "%s", profile_screen.profile.show_clan_tag ? "true" : "false");
	else
		return FALSE;
	return TRUE;
}

boolean profile_screen_site_setting_write(
	char const *field,
	char const *value)
{
	if (!linked() || profile_screen.profile.locked)
		return FALSE;
	browser_profile_privacy(field, !strcmp(value, "true"));
	return TRUE;
}

/* ---------- the screen of code: opening, closing and its input */

boolean profile_screen_active(
	void)
{
	return profile_screen.active || profile_screen.dialog != _dialog_none;
}

void profile_screen_open(
	boolean from_online_games)
{
	profile_screen.active = TRUE;
	profile_screen.from_online_games = from_online_games;
	profile_screen.section = _section_overview;
	profile_screen.in_panel = FALSE;
	profile_screen.row = 0;
	profile_screen.dialog = _dialog_none;
	profile_screen.opened_time = system_milliseconds();
	refresh(FALSE);
	event_manager_flush();
}

static void close_screen(
	void)
{
	profile_screen.active = FALSE;
	if (profile_screen.dialog != _dialog_none)
		close_dialog();
	if (profile_screen.from_online_games)
		browser_screen_open();
}

/* the next open section from section, by step (1 or -1) */
static short next_section(
	short section,
	short step)
{
	short tries;

	for (tries = 0; tries < NUMBER_OF_PROFILE_SECTIONS; tries++)
	{
		section = (short)((section + step + NUMBER_OF_PROFILE_SECTIONS) % NUMBER_OF_PROFILE_SECTIONS);
		if (section_open(section))
			return section;
	}
	return section;
}

/* a privacy row's value's index (its values') */
static short privacy_value(
	struct privacy_row const *row)
{
	char text[64] = "";
	short index;

	if (row->kind == _privacy_site)
	{
		if (!profile_screen_site_setting(row->name, text, sizeof(text)))
			return NONE;
	}
	else
	{
		extern int config_text(char const *name, char *text, size_t size);

		config_text(row->name, text, sizeof(text));
	}
	for (index = 0; index < 4 && row->values[index]; index++)
	{
		if (!strcmp(text, row->values[index]))
			return index;
	}
	return 0;
}

static short privacy_value_count(
	struct privacy_row const *row)
{
	short count = 0;

	while (count < 4 && row->values[count])
		count++;
	return count;
}

static void privacy_change(
	short step)
{
	struct privacy_row const *row = &privacy_rows[profile_screen.row];
	short value = privacy_value(row);
	short count = privacy_value_count(row);

	if (value == NONE)
		return;
	value = (short)((value + step + count) % count);
	if (row->kind == _privacy_site)
		profile_screen_site_setting_write(row->name, row->values[value]);
	else if (!config_write(row->name, row->values[value]))
		platform_log("Delta Profile: could not write %s to config.toml", row->name);
}

/* Y on Privacy: the game's rows at their defaults (the site's
belong to the profile, and stay) */
static void privacy_defaults(
	void)
{
	short index;

	for (index = 0; index < NUMBEROF(privacy_rows); index++)
	{
		if (privacy_rows[index].kind == _privacy_game)
			config_write(privacy_rows[index].name, privacy_rows[index].values[0]);
	}
}

static short panel_row_count(
	void)
{
	if (profile_screen.section == _section_privacy)
		return NUMBEROF(privacy_rows);
	return 0;
}

/* the leader of a clan with other members can't leave it */
static boolean clan_leader_held(
	void)
{
	struct delta_profile_clan const *clan = &profile_screen.profile.clan;

	return !strcmp(clan->rank, "leader") && clan->count > 1;
}

static short clan_dialog_rows(
	void)
{
	if (profile_screen.profile.has_clan)
		return NUMBER_OF_CLAN_ROWS;
	return (short)profile_screen.profile.invite_count;
}

static void clan_dialog_button(
	short button)
{
	struct delta_profile const *profile = &profile_screen.profile;
	short rows = clan_dialog_rows();

	switch (profile_screen.clan_state)
	{
	case _clan_sent:
		/* (the answer read: done, or back to the rows) */
		if (profile_screen.status.action == BROWSER_PROFILE_ACTION_WORKING)
			break;
		if (button == _gamepad_analog_button_a || button == _gamepad_analog_button_b)
		{
			if (profile_screen.status.action == BROWSER_PROFILE_ACTION_DONE || !clan_dialog_rows())
				close_dialog();
			else
			{
				profile_screen.clan_state = _clan_rows;
				profile_screen.dialog_row = 0;
				browser_profile_action_seen();
			}
		}
		break;
	case _clan_asking:
		if (button == _gamepad_analog_button_a)
		{
			browser_profile_clan("leave", 0);
			profile_screen.clan_state = _clan_sent;
		}
		else if (button == _gamepad_analog_button_b)
			profile_screen.clan_state = _clan_rows;
		break;
	default:
		if (button == _gamepad_binary_button_dpad_up && rows)
			profile_screen.dialog_row = (short)MAX(0, profile_screen.dialog_row - 1);
		else if (button == _gamepad_binary_button_dpad_down && rows)
			profile_screen.dialog_row = (short)MIN(rows - 1, profile_screen.dialog_row + 1);
		else if (button == _gamepad_analog_button_b)
			close_dialog();
		else if (profile->has_clan && profile_screen.dialog_row == _clan_row_tag &&
			(button == _gamepad_analog_button_a || button == _gamepad_binary_button_dpad_left ||
			button == _gamepad_binary_button_dpad_right))
		{
			browser_profile_privacy("show_clan_tag", !profile->show_clan_tag);
		}
		else if (profile->has_clan && profile_screen.dialog_row == _clan_row_leave && button == _gamepad_analog_button_a)
		{
			if (clan_leader_held())
				ui_play_audio_feedback_sound(SOUND_ERROR);
			else
				profile_screen.clan_state = _clan_asking;
		}
		else if (!profile->has_clan && profile_screen.dialog_row < rows &&
			(button == _gamepad_analog_button_a || button == _gamepad_analog_button_x))
		{
			browser_profile_clan(button == _gamepad_analog_button_a ? "accept" : "decline",
				profile->invites[profile_screen.dialog_row].id);
			profile_screen.clan_state = _clan_sent;
		}
		break;
	}
}

static void dialog_button(
	short button)
{
	struct browser_connect const *connect = &profile_screen.connect;

	switch (profile_screen.dialog)
	{
	case _dialog_link:
	{
		boolean asking = connect->state == BROWSER_CONNECT_CONFIRM;
		boolean done = connect->state == BROWSER_CONNECT_EXPIRED || connect->state == BROWSER_CONNECT_DECLINED ||
			connect->state == BROWSER_CONNECT_FAILED;

		if (button == _gamepad_analog_button_a && asking && !connect->answered)
			browser_connect_answer(TRUE);
		else if (button == _gamepad_analog_button_b && asking)
		{
			if (!connect->answered)
				browser_connect_answer(FALSE);
		}
		else if (button == _gamepad_analog_button_b)
			close_dialog();
		else if (button == _gamepad_analog_button_black && done)
		{
			close_dialog();
			open_link();
		}
		break;
	}
	case _dialog_unlink:
		if (profile_screen.status.action == BROWSER_PROFILE_ACTION_WORKING)
			break;
		if (profile_screen.status.action != BROWSER_PROFILE_ACTION_NONE)
		{
			/* (the answer read: done, and a copy unlinked shows as not linked) */
			if (button == _gamepad_analog_button_a || button == _gamepad_analog_button_b)
				close_dialog();
		}
		else if (button == _gamepad_analog_button_a)
			browser_profile_unlink();
		else if (button == _gamepad_analog_button_b)
			close_dialog();
		break;
	case _dialog_web:
		if (button == _gamepad_analog_button_a)
		{
			/* (the page opened here, signed in: the sign-in code goes in its
			address, never on the screen) */
			browser_open_profile();
			close_dialog();
		}
		else if (button == _gamepad_analog_button_b)
			close_dialog();
		break;
	case _dialog_copies:
		if (button == _gamepad_analog_button_b)
			close_dialog();
		break;
	case _dialog_clan:
		clan_dialog_button(button);
		break;
	default:
		break;
	}
}

static void press(
	short button);

static void held_press(
	short button)
{
	static unsigned long last_event_time, last_press_time;
	static short last_button;
	static boolean repeating;
	unsigned long now = system_milliseconds();

	if (button != last_button || now - last_event_time > STEP_RELEASE_GAP)
	{
		press(button);
		last_press_time = now;
		repeating = FALSE;
	}
	else if (now - last_press_time >= (repeating ? STEP_REPEAT_INTERVAL : STEP_FIRST_REPEAT_DELAY))
	{
		press(button);
		last_press_time = now;
		repeating = TRUE;
	}
	last_button = button;
	last_event_time = now;
}

static void press(
	short button)
{
	unsigned long now = system_milliseconds();

	if (now - profile_screen.dialog_time < DIALOG_SETTLE)
		return;
	if (profile_screen.dialog != _dialog_none)
	{
		dialog_button(button);
		return;
	}
	if (!profile_screen.active)
		return;
	switch (button)
	{
	case _gamepad_binary_button_dpad_up:
		if (profile_screen.in_panel)
			profile_screen.row = (short)MAX(0, profile_screen.row - 1);
		else
			profile_screen.section = next_section(profile_screen.section, -1);
		break;
	case _gamepad_binary_button_dpad_down:
		if (profile_screen.in_panel)
			profile_screen.row = (short)MIN(panel_row_count() - 1, profile_screen.row + 1);
		else
			profile_screen.section = next_section(profile_screen.section, 1);
		break;
	case _gamepad_binary_button_dpad_left:
	case _gamepad_binary_button_dpad_right:
		if (profile_screen.in_panel && profile_screen.section == _section_privacy)
			privacy_change(button == _gamepad_binary_button_dpad_left ? -1 : 1);
		break;
	case _gamepad_analog_button_white:
	case _gamepad_analog_button_black:
		profile_screen.section = next_section(profile_screen.section, button == _gamepad_analog_button_white ? -1 : 1);
		profile_screen.in_panel = FALSE;
		break;
	case _gamepad_analog_button_a:
	case _gamepad_binary_button_start:
		if (now - profile_screen.opened_time < OPEN_SETTLE)
			break;
		if (profile_screen.in_panel && profile_screen.section == _section_privacy)
			privacy_change(1);
		else if (panel_row_count())
		{
			profile_screen.in_panel = TRUE;
			profile_screen.row = 0;
		}
		else
			profile_screen_section_action(profile_screen.section);
		break;
	case _gamepad_analog_button_x:
		if (profile_screen.section == _section_copies && linked())
			open_dialog(_dialog_copies);
		break;
	case _gamepad_analog_button_y:
		if (profile_screen.section == _section_privacy)
			privacy_defaults();
		else
			refresh(TRUE);
		break;
	case _gamepad_analog_button_b:
	case _gamepad_binary_button_back:
		if (profile_screen.in_panel)
			profile_screen.in_panel = FALSE;
		else
			close_screen();
		break;
	default:
		break;
	}
}

void profile_screen_process(
	void)
{
	struct event_record event;

	refresh(FALSE);
	if (profile_screen.dialog == _dialog_link)
	{
		browser_connect_get(&profile_screen.connect);
		/* (linked: the profile again, at once) */
		if (profile_screen.connect.state == BROWSER_CONNECT_CONNECTED && profile_screen.status.serial == profile_screen.profile_serial &&
			!profile_screen.status.loading && linked() == FALSE)
		{
			refresh(TRUE);
		}
	}
	/* (another copy unlinked on the web: the dialog closes by itself) */
	if (profile_screen.dialog == _dialog_copies && profile_screen.profile.copy_count < profile_screen.copies_before)
		close_dialog();
	if (!section_open(profile_screen.section))
	{
		profile_screen.section = _section_overview;
		profile_screen.in_panel = FALSE;
	}
	while (profile_screen_active() && get_next_event(&event, NONE))
	{
		if (event.type == PROFILE_EVENT_BUTTON)
		{
			short button = event.data.button.index;

			if (button >= _gamepad_binary_button_dpad_up && button <= _gamepad_binary_button_dpad_right)
				held_press(button);
			else if (event.data.button.value == 1)
				press(button);
		}
		else if (event.type == PROFILE_EVENT_LEFT_STICK && profile_screen.dialog == _dialog_none)
		{
			if (event.data.stick.y == SHORT_MAX)
				held_press(_gamepad_binary_button_dpad_up);
			else if (event.data.stick.y == SHORT_MIN)
				held_press(_gamepad_binary_button_dpad_down);
			else if (event.data.stick.x == SHORT_MIN)
				held_press(_gamepad_binary_button_dpad_left);
			else if (event.data.stick.x == SHORT_MAX)
				held_press(_gamepad_binary_button_dpad_right);
		}
	}
	/* (the menus behind take nothing while it is up) */
	event_manager_flush();
}

/* ---------- drawing (the overlay, ui_overlay.c), as Online Games is */

enum
{
	COLOR_TITLE = 0x3D8BFFFF,
	COLOR_TITLE_GLOW = 0x3D8BFFFF,
	COLOR_ROW = 0x5AAEFFFF,
	COLOR_ROW_GLOW = 0x5AAEFF60,
	COLOR_ROW_DIM = 0x56698AFF,
	COLOR_SELECTION_FILL = 0x2A62C850,
	COLOR_SELECTION_EDGE = 0x6FB2FFFF,
	COLOR_HEAD = 0x8FA6C8FF,
	COLOR_TEXT = 0xFFFFFFFF,
	COLOR_DIM = 0x8FA6C8FF,
	COLOR_LABEL = 0x7FB8FFFF,
	COLOR_RULE = 0x2F6DD0A0,
	COLOR_PROMPT = 0x4AA3FFFF,
	COLOR_PROMPT_OFF = 0x4A5E80FF,
	COLOR_BUTTON_OFF = 0xFFFFFF55,
	COLOR_WARN = 0xF0A070FF,
	COLOR_GOOD = 0x8CE68AFF,
	COLOR_BAD = 0xFF8A80FF,
	COLOR_SHADE = 0x02081AA0,
	COLOR_HALO = 0x000814FF,
	COLOR_QR_DARK = 0x081020FF,
	COLOR_TILE = 0x0A1A33C0,
};

enum
{
	BAND_Y = 70, BAR_Y = 409, BAR_BOTTOM = 445,
	PROMPTS_RIGHT = 608,
	LIST_X = 32, LIST_Y = 82, LIST_WIDTH = 196, SECTION_STEP = 26,
	PANEL_X = 238, PANEL_Y = 80, PANEL_WIDTH = 370, PANEL_HEIGHT = 322,
	SPARTAN_WIDTH = 84, SPARTAN_HEIGHT = 102,
	DIALOG_X = 100, DIALOG_Y = 84, DIALOG_WIDTH = 440, DIALOG_HEIGHT = 312,
	QR_X = 382, QR_Y = 150, QR_WIDTH = 138,
};

#define PROMPT_BUTTON 18.0f
#define PROMPT_TEXT 13.0f
#define SMALL_BUTTON 14.0f
#define SMALL_TEXT 9.5f
#define PROMPT_GAP 14.0f

static short const glyph_buttons[NUMBER_OF_UI_BUTTONS] =
{
	_gamepad_analog_button_a, _gamepad_analog_button_b, _gamepad_analog_button_x, _gamepad_analog_button_y,
	_gamepad_binary_button_start, _gamepad_analog_button_left_trigger, _gamepad_analog_button_right_trigger,
	_gamepad_analog_button_white, _gamepad_analog_button_black, _gamepad_binary_button_dpad_left,
	_gamepad_binary_button_dpad_right, _gamepad_binary_button_back,
};

static void add_target(
	float x0,
	float y0,
	float x1,
	float y1,
	short button,
	short section,
	short row)
{
	if (profile_screen.target_count < MAXIMUM_TARGETS)
	{
		profile_screen.targets[profile_screen.target_count].x0 = x0;
		profile_screen.targets[profile_screen.target_count].y0 = y0;
		profile_screen.targets[profile_screen.target_count].x1 = x1;
		profile_screen.targets[profile_screen.target_count].y1 = y1;
		profile_screen.targets[profile_screen.target_count].button = button;
		profile_screen.targets[profile_screen.target_count].section = section;
		profile_screen.targets[profile_screen.target_count].row = row;
		profile_screen.target_count++;
	}
}

static float prompt_width(
	int button,
	char const *words,
	float button_size,
	float text_size)
{
	return ui_overlay_button_width(button, button_size) + 3.0f + ui_overlay_text_width(UI_FONT_MENU, text_size, words);
}

/* a button's prompt, its glyph then =WORD; its right end */
static float prompt(
	int button,
	char const *words,
	float x,
	float y,
	float button_size,
	float text_size,
	boolean on,
	float top,
	float bottom)
{
	float left = x;

	x += ui_overlay_button(button, button_size, x, y, on ? 0xFFFFFFFF : COLOR_BUTTON_OFF) + 3.0f;
	x += ui_overlay_text_glow(UI_FONT_MENU, text_size, x, y + (button_size - text_size) * 0.5f + 0.5f, UI_ALIGN_LEFT,
		on ? COLOR_PROMPT : COLOR_PROMPT_OFF, on ? 0x3D8BFF80 : 0x00000000, words);
	if (on)
		add_target(left - 6.0f, top, x + 6.0f, bottom, glyph_buttons[button], NONE, NONE);
	return x;
}

/* prompts from the right end leftward, as the bar has them */
static void prompts_right(
	short count,
	int const *buttons,
	char const *const *words,
	float right,
	float y,
	float button_size,
	float text_size,
	float top,
	float bottom)
{
	float width = 0.0f, x;
	short index;

	for (index = 0; index < count; index++)
		width += prompt_width(buttons[index], words[index], button_size, text_size) + (index ? PROMPT_GAP : 0.0f);
	x = right - width;
	for (index = 0; index < count; index++)
		x = prompt(buttons[index], words[index], x, y, button_size, text_size, TRUE, top, bottom) + PROMPT_GAP;
}

static void prompts_centered(
	short count,
	int const *buttons,
	char const *const *words,
	float center,
	float y,
	float top,
	float bottom)
{
	float width = 0.0f;
	short index;

	for (index = 0; index < count; index++)
		width += prompt_width(buttons[index], words[index], PROMPT_BUTTON - 2, PROMPT_TEXT - 1) + (index ? PROMPT_GAP : 0.0f);
	prompts_right(count, buttons, words, center + width * 0.5f, y, PROMPT_BUTTON - 2, PROMPT_TEXT - 1, top, bottom);
}

/* text cut to width, ending "..." when cut; drawn */
static void fitted_text(
	int font,
	float size,
	float x,
	float y,
	int align,
	unsigned int color,
	float width,
	char const *text)
{
	char line[192];
	long length = (long)strlen(text);

	if (ui_overlay_text_width(font, size, text) <= width)
	{
		ui_overlay_text(font, size, x, y, align, color, text);
		return;
	}
	while (length > 0)
	{
		length--;
		while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80)
			length--;
		snprintf(line, sizeof(line), "%.*s...", (int)length, text);
		if (ui_overlay_text_width(font, size, line) <= width)
			break;
	}
	ui_overlay_text(font, size, x, y, align, color, line);
}

/* text broken into lines of width at most (at spaces); the lines drawn */
static float wrapped_text(
	int font,
	float size,
	float x,
	float y,
	float width,
	float step,
	unsigned int color,
	char const *text,
	short maximum_lines)
{
	char line[256];
	short lines = 0;

	while (*text && lines < maximum_lines)
	{
		long length = (long)strlen(text), fit = length, at;

		/* (the most that fits, broken at a space) */
		if (ui_overlay_text_width(font, size, text) > width)
		{
			fit = 0;
			for (at = 1; at <= length && at < (long)sizeof(line); at++)
			{
				if (text[at] != ' ' && text[at] != 0)
					continue;
				snprintf(line, sizeof(line), "%.*s", (int)at, text);
				if (ui_overlay_text_width(font, size, line) > width)
					break;
				fit = at;
			}
			if (!fit)
				fit = MIN(length, (long)sizeof(line) - 1);
		}
		snprintf(line, sizeof(line), "%.*s", (int)fit, text);
		ui_overlay_text(font, size, x, y, UI_ALIGN_LEFT, color, line);
		y += step;
		lines++;
		text += fit;
		while (*text == ' ')
			text++;
	}
	return y;
}

/* the first player's Spartan, in their armor's color, centered in its
frame (the menus' own picture of it: ui\shell\bitmaps\colors_sm) */
static void draw_spartan(
	float x,
	float y,
	float width,
	float height)
{
	long profile_index = player_ui_get_active_player_profile_index(0);
	long bitmap_index = tag_loaded('bitm', "ui\\shell\\bitmaps\\colors_sm");
	struct player_profile profile;
	struct bitmap_data *bitmap;
	short frame = 0;
	rectangle2d bounds, art;
	float drawn_width, drawn_height, scale;

	ui_overlay_panel(x, y, width, height, FALSE);
	if (profile_index == NONE)
		profile_index = player_ui_get_player1_last_used_profile_index();
	if (profile_index != NONE && TEST_FLAG(profile_index, _saved_game_file_index_valid_bit) &&
		player_profile_get(profile_index, &profile))
	{
		/* (a profile with no color yet: the first) */
		frame = (short)MAX(0, profile.primary_color_index);
	}
	bitmap = bitmap_index != NONE ? bitmap_group_get_bitmap_from_sequence(bitmap_index, 0, frame) : NULL;
	if (!bitmap)
		return;
	/* (the picture 112 by 136 of the bitmap's top left, its shape kept, its
	middle the frame's on both axes) */
	scale = MIN((width - 8.0f) / 112.0f, (height - 8.0f) / 136.0f);
	drawn_width = 112.0f * scale;
	drawn_height = 136.0f * scale;
	bounds.x0 = (short)(x + (width - drawn_width) * 0.5f);
	bounds.y0 = (short)(y + (height - drawn_height) * 0.5f);
	bounds.x1 = (short)(bounds.x0 + drawn_width);
	bounds.y1 = (short)(bounds.y0 + drawn_height);
	ui_overlay_cutout(bounds.x0, bounds.y0, (float)(bounds.x1 - bounds.x0), (float)(bounds.y1 - bounds.y0));
	draw_quad(&bounds, 0xFF0A1A33);
	art.x0 = 0;
	art.y0 = 0;
	art.x1 = (short)MIN(112, bitmap->width);
	art.y1 = (short)MIN(136, bitmap->height);
	draw_bitmap_in_rect(bitmap, &bounds, &art, NULL, 0xFFFFFFFF, NULL, FALSE);
}

/* the clan's emblem, size square, x and y its top left (its pixels made
again only when it changes) */
static void draw_emblem(
	float x,
	float y,
	float size)
{
	int const *fields = profile_screen.profile.clan.emblem;
	struct clan_emblem emblem;
	int serial;

	emblem.foreground = fields[0];
	emblem.background = fields[1];
	emblem.primary = fields[2];
	emblem.secondary = fields[3];
	emblem.background_color = fields[4];
	serial = clan_emblem_serial(&emblem);
	if (!profile_screen.emblem_pixels)
	{
		profile_screen.emblem_pixels = malloc(EMBLEM_PIXELS * EMBLEM_PIXELS * 4);
		if (!profile_screen.emblem_pixels)
			return;
		profile_screen.emblem_serial = 0;
	}
	if (serial != profile_screen.emblem_serial)
	{
		clan_emblem_render(&emblem, EMBLEM_PIXELS, profile_screen.emblem_pixels);
		profile_screen.emblem_serial = serial;
	}
	ui_overlay_picture(serial, profile_screen.emblem_pixels, EMBLEM_PIXELS, EMBLEM_PIXELS, x, y, size, size, 0xFFFFFFFF);
}

/* the Chupathingy menus' panel shows the clan: its emblem there, size
square (asked for as the panel updates; drawn while it asks); a size of 0:
not shown from now */
void profile_screen_card_emblem(
	float x,
	float y,
	float size)
{
	profile_screen.card_x = x;
	profile_screen.card_y = y;
	profile_screen.card_size = size;
	profile_screen.card_time = size > 0.0f ? system_milliseconds() : 0;
}

static void draw_header(
	float left,
	float right)
{
	char text[160], site[128];
	long index;
	float x;

	ui_overlay_band(left, right, BAND_Y, BAR_Y);
	ui_overlay_text_glow(UI_FONT_TITLE, 13.5f, 64, 45.0f, UI_ALIGN_LEFT, COLOR_TITLE, COLOR_TITLE_GLOW, "DELTA PROFILE");
	/* the profile, then the site, at the right */
	x = PROMPTS_RIGHT;
	if (linked())
		snprintf(text, sizeof(text), "@%s", profile_screen.profile.handle);
	else
		snprintf(text, sizeof(text), "%s", profile_screen.status.has_profile ? "NOT LINKED" : "-");
	for (index = 0; text[index]; index++)
		text[index] = (char)(text[index] >= 'a' && text[index] <= 'z' ? text[index] - 32 : text[index]);
	x -= ui_overlay_text_glow(UI_FONT_MENU, 9.5f, x, 11, UI_ALIGN_RIGHT, COLOR_TEXT, COLOR_HALO, text) + 3;
	ui_overlay_text_glow(UI_FONT_MENU, 9.5f, x, 11, UI_ALIGN_RIGHT, COLOR_HEAD, COLOR_HALO, "PROFILE");
	browser_server_name(site, sizeof(site));
	for (index = 0; site[index]; index++)
		site[index] = (char)(site[index] >= 'a' && site[index] <= 'z' ? site[index] - 32 : site[index]);
	x = PROMPTS_RIGHT;
	x -= ui_overlay_text_glow(UI_FONT_MENU, 9.5f, x, 27, UI_ALIGN_RIGHT, COLOR_TEXT, COLOR_HALO, site) + 3;
	ui_overlay_text_glow(UI_FONT_MENU, 9.5f, x, 27, UI_ALIGN_RIGHT, COLOR_HEAD, COLOR_HALO, "SITE");
	if (profile_screen.dialog == _dialog_none)
	{
		static int const buttons[] = { UI_BUTTON_LEFT_SHOULDER, UI_BUTTON_RIGHT_SHOULDER };
		static char const *const words[] = { "=PREVIOUS", "=NEXT" };

		prompts_right(NUMBEROF(buttons), buttons, words, PROMPTS_RIGHT, 43, SMALL_BUTTON, SMALL_TEXT, 38, 60);
	}
}

static void draw_sections(
	void)
{
	short section;
	float y = LIST_Y;

	for (section = 0; section < NUMBER_OF_PROFILE_SECTIONS; section++)
	{
		boolean picked = section == profile_screen.section;
		boolean open = section_open(section);
		unsigned int color = !open ? COLOR_ROW_DIM : picked ? COLOR_TEXT : COLOR_ROW;

		/* (a rule before the actions) */
		if (section == _section_link)
		{
			ui_overlay_rect(LIST_X + 10, y + 4, LIST_WIDTH - 20, 1.0f, 0, COLOR_RULE);
			y += 12;
		}
		if (picked)
		{
			ui_overlay_rect(LIST_X, y - 3, LIST_WIDTH, 22, 6, profile_screen.in_panel ? 0x2A62C828 : COLOR_SELECTION_FILL);
			ui_overlay_outline(LIST_X, y - 3, LIST_WIDTH, 22, 6, 1.2f, profile_screen.in_panel ? COLOR_RULE : COLOR_SELECTION_EDGE);
		}
		ui_overlay_text_glow(UI_FONT_MENU, 12.0f, LIST_X + 12, y, UI_ALIGN_LEFT, color, open ? COLOR_ROW_GLOW : 0,
			profile_screen_section_name(section));
		if (open)
			add_target(LIST_X, y - 3, LIST_X + LIST_WIDTH, y + 19, NONE, section, NONE);
		y += SECTION_STEP;
	}
}

/* a line's columns (tab separated) */
static short split_columns(
	char *text,
	char **columns,
	short maximum)
{
	short count = 0;

	columns[count++] = text;
	while (count < maximum && (text = strchr(text, '\t')) != NULL)
	{
		*text++ = 0;
		columns[count++] = text;
	}
	return count;
}

/* the section's lines in the panel */
static void draw_lines(
	struct profile_line *lines,
	short count,
	float x,
	float y,
	float width,
	float bottom)
{
	short index, tile = 0;
	float tile_width = (width - 3 * 6.0f) / 4.0f;

	for (index = 0; index < count && y < bottom; index++)
	{
		struct profile_line *line = &lines[index];
		char *columns[6];

		switch (line->style)
		{
		case _profile_line_title:
			ui_overlay_text_glow(UI_FONT_MENU, 16.0f, x, y, UI_ALIGN_LEFT, COLOR_TEXT, 0x3D8BFF90, line->text);
			y += 21;
			break;
		case _profile_line_sub:
			fitted_text(UI_FONT_MENU_SMALL, 9.5f, x, y, UI_ALIGN_LEFT, COLOR_LABEL, width, line->text);
			y += 14;
			break;
		case _profile_line_badges:
			fitted_text(UI_FONT_MENU_SMALL, 8.0f, x, y, UI_ALIGN_LEFT, COLOR_HEAD, width, line->text);
			y += 14;
			break;
		case _profile_line_tile:
		{
			float tile_x = x + (tile % 4) * (tile_width + 6.0f);

			split_columns(line->text, columns, 2);
			ui_overlay_rect(tile_x, y, tile_width, 34, 5, COLOR_TILE);
			ui_overlay_outline(tile_x, y, tile_width, 34, 5, 0.75f, COLOR_RULE);
			ui_overlay_text_glow(UI_FONT_MENU, 13.0f, tile_x + tile_width * 0.5f, y + 4, UI_ALIGN_CENTER, COLOR_TEXT,
				0x3D8BFF60, columns[1]);
			ui_overlay_text(UI_FONT_MENU_SMALL, 6.5f, tile_x + tile_width * 0.5f, y + 23, UI_ALIGN_CENTER, COLOR_LABEL,
				columns[0]);
			tile++;
			if (tile % 4 == 0 || index + 1 >= count || lines[index + 1].style != _profile_line_tile)
			{
				y += 39;
				tile = 0;
			}
			break;
		}
		case _profile_line_head:
		case _profile_line_game:
		{
			/* (map, type and place in their columns, kills-deaths at the
			right; the head names them) */
			boolean head = line->style == _profile_line_head;
			float type_x = x + width * 0.42f, place_x = x + width * 0.62f;
			short columns_count = split_columns(line->text, columns, 4);

			if (head)
				y += 3;
			fitted_text(UI_FONT_MENU_SMALL, 8.5f, x, y, UI_ALIGN_LEFT, head ? COLOR_LABEL : COLOR_TEXT, type_x - x - 6, columns[0]);
			if (columns_count > 1)
				fitted_text(UI_FONT_MENU_SMALL, 8.5f, type_x, y, UI_ALIGN_LEFT, COLOR_LABEL, place_x - type_x - 6, columns[1]);
			if (columns_count > 2)
			{
				fitted_text(UI_FONT_MENU_SMALL, 8.5f, place_x, y, UI_ALIGN_LEFT, head ? COLOR_LABEL :
					!strncmp(columns[2], "Won", 3) ? COLOR_GOOD : !strncmp(columns[2], "Lost", 4) ? COLOR_BAD : COLOR_TEXT,
					width * 0.24f, columns[2]);
			}
			if (columns_count > 3)
				ui_overlay_text(UI_FONT_MENU_SMALL, 8.5f, x + width, y, UI_ALIGN_RIGHT, head ? COLOR_LABEL : COLOR_TEXT, columns[3]);
			y += 14;
			break;
		}
		case _profile_line_row:
		{
			float value_width = 0;

			if (split_columns(line->text, columns, 2) > 1 && columns[1][0])
			{
				value_width = ui_overlay_text_width(UI_FONT_MENU_SMALL, 8.5f, columns[1]);
				if (value_width > width * 0.5f)
					value_width = width * 0.5f;
				fitted_text(UI_FONT_MENU_SMALL, 8.5f, x + width, y + 1.5f, UI_ALIGN_RIGHT, COLOR_LABEL, value_width, columns[1]);
				value_width += 10;
			}
			fitted_text(UI_FONT_MENU, 10.0f, x, y, UI_ALIGN_LEFT, COLOR_TEXT, width - value_width, columns[0]);
			y += 16;
			break;
		}
		case _profile_line_warn:
			y = wrapped_text(UI_FONT_MENU_SMALL, 9.0f, x, y, width, 12, COLOR_WARN, line->text, 3) + 3;
			break;
		case _profile_line_dim:
			y = wrapped_text(UI_FONT_MENU_SMALL, 8.5f, x, y, width, 11.5f, COLOR_DIM, line->text, 4) + 4;
			break;
		default:
			y = wrapped_text(UI_FONT_MENU, 10.5f, x, y, width, 14, COLOR_TEXT, line->text, 3) + 2;
			break;
		}
	}
}

/* a value's arrow (pointing left or right), centered on x, a 10-point
line's top at y: thin slices of a triangle (the menu font has no arrows) */
static void draw_arrow(
	float x,
	float y,
	boolean left,
	unsigned int color)
{
	short slice;

	for (slice = 0; slice < 5; slice++)
	{
		float height = 9.0f - slice * 1.8f;

		/* (the tallest slice the arrow's back, the shortest its point) */
		ui_overlay_rect(x + (left ? 1.5f - slice : slice - 2.5f), y + 3.0f + (9.0f - height) * 0.5f, 1.0f, height, 0, color);
	}
}

/* Privacy's rows: label, then the value between arrows */
static void draw_privacy(
	float x,
	float y,
	float width)
{
	short index;
	char const *group = NULL;

	for (index = 0; index < NUMBEROF(privacy_rows); index++)
	{
		struct privacy_row const *row = &privacy_rows[index];
		char const *row_group = row->kind == _privacy_site ? "ON THE SITE" :
			!strcmp(row->name, "discord.presence") || !strcmp(row->name, "display.profile_names") ? "ELSEWHERE" : "IN GAMES";
		short value = privacy_value(row);
		boolean picked = profile_screen.in_panel && index == profile_screen.row;
		boolean off = value == NONE;

		if (row_group != group)
		{
			group = row_group;
			ui_overlay_text(UI_FONT_MENU_SMALL, 7.5f, x, y + 2, UI_ALIGN_LEFT, COLOR_LABEL, group);
			y += 12;
		}
		if (picked)
		{
			ui_overlay_rect(x - 6, y - 3, width + 12, 20, 5, COLOR_SELECTION_FILL);
			ui_overlay_outline(x - 6, y - 3, width + 12, 20, 5, 1.2f, COLOR_SELECTION_EDGE);
		}
		ui_overlay_text(UI_FONT_MENU, 10.0f, x, y, UI_ALIGN_LEFT, off ? COLOR_ROW_DIM : picked ? COLOR_TEXT : COLOR_ROW, row->label);
		ui_overlay_text(UI_FONT_MENU, 10.0f, x + width - 50, y, UI_ALIGN_CENTER, off ? COLOR_ROW_DIM : COLOR_TEXT,
			off ? "-" : row->shown[value]);
		if (picked && !off)
		{
			draw_arrow(x + width - 92, y, TRUE, COLOR_PROMPT);
			draw_arrow(x + width - 8, y, FALSE, COLOR_PROMPT);
		}
		add_target(x - 6, y - 3, x + width + 6, y + 17, NONE, _section_privacy, index);
		y += 21;
	}
	/* the picked row's help, or the section's */
	y += 4;
	if (profile_screen.in_panel)
	{
		wrapped_text(UI_FONT_MENU_SMALL, 8.5f, x, y, width, 11.5f, COLOR_TEXT, privacy_rows[profile_screen.row].help, 3);
		if (privacy_rows[profile_screen.row].kind == _privacy_site && profile_screen.status.message[0])
			ui_overlay_text(UI_FONT_MENU_SMALL, 8.5f, x, y + 36, UI_ALIGN_LEFT,
				profile_screen.status.action == BROWSER_PROFILE_ACTION_FAILED ? COLOR_WARN : COLOR_GOOD, profile_screen.status.message);
	}
}

static void draw_panel(
	void)
{
	struct profile_line lines[MAXIMUM_PANEL_LINES];
	short count;
	float x = PANEL_X + 16, y = PANEL_Y + 14, width = PANEL_WIDTH - 32;

	ui_overlay_panel(PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT, profile_screen.in_panel);
	if (profile_screen.section == _section_privacy)
	{
		draw_privacy(x, y, width);
		return;
	}
	count = profile_screen_panel_lines(profile_screen.section, lines, MAXIMUM_PANEL_LINES);
	/* (the overview's Spartan at the left of its name and record) */
	if (profile_screen.section == _section_overview && linked() && !profile_screen.profile.locked)
	{
		draw_spartan(x, y, SPARTAN_WIDTH, SPARTAN_HEIGHT + 30);
		x += SPARTAN_WIDTH + 12;
		width -= SPARTAN_WIDTH + 12;
		/* (the record beside the picture, the recent games under both) */
		{
			short index;

			for (index = 0; index < count && lines[index].style != _profile_line_head; index++)
				;
			draw_lines(lines, index, x, y, width, PANEL_Y + PANEL_HEIGHT - 8);
			draw_lines(lines + index, (short)(count - index), PANEL_X + 16, y + SPARTAN_HEIGHT + 40, PANEL_WIDTH - 32,
				PANEL_Y + PANEL_HEIGHT - 8);
		}
		return;
	}
	/* (the clan's emblem at the left of its name, tag and motto; its members
under both) */
	if (profile_screen.section == _section_clan && linked() && profile_screen.profile.has_clan)
	{
		short index;

		ui_overlay_panel(x, y, 84, 84, FALSE);
		draw_emblem(x + 6, y + 6, 72);
		for (index = 0; index < count && lines[index].style != _profile_line_head; index++)
			;
		draw_lines(lines, index, x + 96, y + 6, width - 96, PANEL_Y + PANEL_HEIGHT - 8);
		draw_lines(lines + index, (short)(count - index), x, y + 92, width, PANEL_Y + PANEL_HEIGHT - 8);
		return;
	}
	draw_lines(lines, count, x, y, width, PANEL_Y + PANEL_HEIGHT - 8);
}

static void draw_bar(
	void)
{
	int buttons[4];
	char const *words[4];
	short count = 0;

	if (profile_screen.section == _section_privacy)
	{
		buttons[count] = UI_BUTTON_Y;
		words[count++] = "=DEFAULTS";
	}
	else if (profile_screen.section == _section_overview)
	{
		buttons[count] = UI_BUTTON_Y;
		words[count++] = "=REFRESH";
	}
	if (profile_screen.section == _section_copies && linked())
	{
		buttons[count] = UI_BUTTON_X;
		words[count++] = "=UNLINK A COPY";
	}
	buttons[count] = UI_BUTTON_B;
	words[count++] = "=BACK";
	if (profile_screen.in_panel || panel_row_count() || profile_screen.section >= _section_link ||
		(profile_screen.section == _section_clan && linked() &&
		(profile_screen.profile.has_clan || profile_screen.profile.invite_count)))
	{
		buttons[count] = UI_BUTTON_A;
		words[count++] = profile_screen.in_panel ? "=CHANGE" : "=SELECT";
	}
	prompts_right(count, buttons, words, PROMPTS_RIGHT, BAR_Y + (BAR_BOTTOM - BAR_Y - PROMPT_BUTTON) / 2, PROMPT_BUTTON,
		PROMPT_TEXT, BAR_Y, 480);
}

/* ---------- drawing: the dialogs (every menu set's) */

static void dialog_frame(
	char const *title)
{
	ui_overlay_panel(DIALOG_X, DIALOG_Y, DIALOG_WIDTH, DIALOG_HEIGHT, TRUE);
	ui_overlay_text_glow(UI_FONT_TITLE, 11.0f, DIALOG_X + 22, DIALOG_Y + 22, UI_ALIGN_LEFT, COLOR_TITLE, COLOR_TITLE_GLOW, title);
}

static void dialog_prompts(
	short count,
	int const *buttons,
	char const *const *words)
{
	prompts_centered(count, buttons, words, 320, DIALOG_Y + DIALOG_HEIGHT - 34, DIALOG_Y + DIALOG_HEIGHT - 44,
		DIALOG_Y + DIALOG_HEIGHT);
}

static void dialog_prompt(
	int button,
	char const *words)
{
	dialog_prompts(1, &button, &words);
}

static void draw_qr(
	unsigned char const *modules,
	short size)
{
	float module = (float)QR_WIDTH / (size + 8);
	short x, y, run;

	ui_overlay_rect(QR_X, QR_Y, QR_WIDTH, QR_WIDTH, 4, 0xFFFFFFFF);
	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x = (short)(x + run))
		{
			for (run = 0; x + run < size && modules[y * size + x + run] == modules[y * size + x]; run++)
				;
			if (modules[y * size + x])
			{
				ui_overlay_rect(QR_X + (x + 4) * module, QR_Y + (y + 4) * module, run * module + 0.3f, module + 0.3f, 0,
					COLOR_QR_DARK);
			}
		}
	}
}

static void draw_link_dialog(
	void)
{
	struct browser_connect const *connect = &profile_screen.connect;
	char text[192];
	float center = DIALOG_X + DIALOG_WIDTH / 2;

	dialog_frame("LINK PROFILE");
	switch (connect->state)
	{
	case BROWSER_CONNECT_WAITING:
	{
		float left = DIALOG_X + 22, y = DIALOG_Y + 60;

		ui_overlay_text(UI_FONT_MENU_SMALL, 11.5f, left, y, UI_ALIGN_LEFT, COLOR_DIM, "Go to");
		ui_overlay_text_glow(UI_FONT_MENU, 13.0f, left, y + 16, UI_ALIGN_LEFT, COLOR_LABEL, 0x3D8BFF60, connect->page);
		ui_overlay_text(UI_FONT_MENU_SMALL, 11.5f, left, y + 38, UI_ALIGN_LEFT, COLOR_DIM, "Enter");
		ui_overlay_panel(left, y + 56, 238, 62, FALSE);
		ui_overlay_text_glow(UI_FONT_MENU, 34.0f, left + 119, y + 69, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFFFF, connect->code);
		snprintf(text, sizeof(text), "Expires in %d:%02d", connect->seconds / 60, connect->seconds % 60);
		ui_overlay_text(UI_FONT_MENU_SMALL, 11.5f, left, y + 130, UI_ALIGN_LEFT, COLOR_DIM, text);
		if (connect->qr_size)
			draw_qr(connect->qr, (short)connect->qr_size);
		dialog_prompt(UI_BUTTON_B, "=CLOSE");
		break;
	}
	case BROWSER_CONNECT_CONFIRM:
		if (connect->previous[0])
			snprintf(text, sizeof(text), "Move this copy from @%s to @%s?", connect->previous, connect->handle);
		else
			snprintf(text, sizeof(text), "Link this copy to @%s?", connect->handle);
		ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, DIALOG_Y + 96, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFF90, text);
		if (connect->answered)
			ui_overlay_text(UI_FONT_MENU, 11.0f, center, DIALOG_Y + 196, UI_ALIGN_CENTER, COLOR_TEXT, "Sending your answer\xE2\x80\xA6");
		else
		{
			static int const buttons[] = { UI_BUTTON_B, UI_BUTTON_A };
			static char const *const words[] = { "=NO", "=YES" };

			dialog_prompts(NUMBEROF(buttons), buttons, words);
		}
		break;
	case BROWSER_CONNECT_CONNECTED:
		snprintf(text, sizeof(text), "Connected as @%s", connect->handle);
		ui_overlay_text_glow(UI_FONT_MENU, 17.0f, center, DIALOG_Y + 106, UI_ALIGN_CENTER, COLOR_GOOD, 0x6BE38A80, text);
		dialog_prompt(UI_BUTTON_B, "=DONE");
		break;
	case BROWSER_CONNECT_EXPIRED:
	case BROWSER_CONNECT_DECLINED:
	case BROWSER_CONNECT_FAILED:
	{
		static int const buttons[] = { UI_BUTTON_B, UI_BUTTON_RIGHT_SHOULDER };
		static char const *const words[] = { "=CLOSE", "=NEW CODE" };

		ui_overlay_text(UI_FONT_MENU, 11.0f, center, DIALOG_Y + 110, UI_ALIGN_CENTER,
			connect->state == BROWSER_CONNECT_FAILED ? COLOR_WARN : COLOR_TEXT,
			connect->state == BROWSER_CONNECT_EXPIRED ? "The code expired." :
			connect->state == BROWSER_CONNECT_DECLINED ? "Not linked." : connect->message);
		dialog_prompts(NUMBEROF(buttons), buttons, words);
		break;
	}
	default:
		ui_overlay_text(UI_FONT_MENU, 11.0f, center, DIALOG_Y + 116, UI_ALIGN_CENTER, COLOR_TEXT, "Getting a code\xE2\x80\xA6");
		dialog_prompt(UI_BUTTON_B, "=CLOSE");
		break;
	}
}

static void draw_unlink_dialog(
	void)
{
	struct browser_profile_status const *status = &profile_screen.status;
	char text[192];
	float center = DIALOG_X + DIALOG_WIDTH / 2, left = DIALOG_X + 30, width = DIALOG_WIDTH - 60;

	dialog_frame("UNLINK THIS COPY");
	switch (status->action)
	{
	case BROWSER_PROFILE_ACTION_WORKING:
		ui_overlay_text(UI_FONT_MENU, 12.0f, center, DIALOG_Y + 120, UI_ALIGN_CENTER, COLOR_TEXT, "Unlinking\xE2\x80\xA6");
		break;
	case BROWSER_PROFILE_ACTION_DONE:
		ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, DIALOG_Y + 100, UI_ALIGN_CENTER, COLOR_GOOD, 0x6BE38A80,
			"Unlinked");
		dialog_prompt(UI_BUTTON_A, "=OK");
		break;
	case BROWSER_PROFILE_ACTION_FIRST_COPY:
		snprintf(text, sizeof(text), "This copy made @%s and can't be unlinked.", profile_screen.profile.handle);
		wrapped_text(UI_FONT_MENU, 12.0f, left, DIALOG_Y + 96, width, 17, COLOR_TEXT, text, 4);
		dialog_prompt(UI_BUTTON_A, "=OK");
		break;
	case BROWSER_PROFILE_ACTION_FAILED:
		wrapped_text(UI_FONT_MENU, 12.0f, left, DIALOG_Y + 100, width, 17, COLOR_WARN, status->message, 4);
		dialog_prompt(UI_BUTTON_A, "=OK");
		break;
	default:
	{
		static int const buttons[] = { UI_BUTTON_B, UI_BUTTON_A };
		static char const *const words[] = { "=NO", "=YES" };

		snprintf(text, sizeof(text), "Unlink this copy from @%s?", profile_screen.profile.handle);
		ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, DIALOG_Y + 90, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFF90, text);
		dialog_prompts(NUMBEROF(buttons), buttons, words);
		break;
	}
	}
}

/* a page of the site with its QR code: Open on the Web, and another copy's
unlinking */
static void draw_page_dialog(
	void)
{
	boolean web = profile_screen.dialog == _dialog_web;
	float left = DIALOG_X + 22, y = DIALOG_Y + 60, width = QR_X - left - 16;

	dialog_frame(web ? "OPEN ON THE WEB" : "UNLINK A COPY");
	ui_overlay_text(UI_FONT_MENU_SMALL, 11.5f, left, y, UI_ALIGN_LEFT, COLOR_DIM, "Go to");
	ui_overlay_text_glow(UI_FONT_MENU, 12.0f, left, y + 16, UI_ALIGN_LEFT, COLOR_LABEL, 0x3D8BFF60, profile_screen.page);
	if (!web)
		wrapped_text(UI_FONT_MENU_SMALL, 11.5f, left, y + 44, width, 15, COLOR_TEXT, "Sign in, then unlink the copy.", 2);
	if (profile_screen.page_qr_size)
		draw_qr(profile_screen.page_qr, (short)profile_screen.page_qr_size);
	if (web)
	{
		static int const buttons[] = { UI_BUTTON_B, UI_BUTTON_A };
		static char const *const words[] = { "=CLOSE", "=OPEN HERE" };

		dialog_prompts(NUMBEROF(buttons), buttons, words);
	}
	else
		dialog_prompt(UI_BUTTON_B, "=CANCEL");
}

/* the clan dialog: the tag shown or not, and leaving; or the invites,
joined or declined; the question before leaving; a request's answer */
static void draw_clan_dialog(
	void)
{
	struct delta_profile const *profile = &profile_screen.profile;
	struct browser_profile_status const *status = &profile_screen.status;
	struct delta_profile_clan const *clan = &profile->clan;
	float center = DIALOG_X + DIALOG_WIDTH / 2, left = DIALOG_X + 30, width = DIALOG_WIDTH - 60, y = DIALOG_Y + 64;
	short rows = clan_dialog_rows(), index, first;
	char text[192];

	dialog_frame(profile->has_clan ? "CLAN" : "INVITES");
	if (profile_screen.clan_state == _clan_sent)
	{
		if (status->action == BROWSER_PROFILE_ACTION_WORKING || status->action == BROWSER_PROFILE_ACTION_NONE)
			ui_overlay_text(UI_FONT_MENU, 12.0f, center, DIALOG_Y + 120, UI_ALIGN_CENTER, COLOR_TEXT, "Sending\xE2\x80\xA6");
		else
		{
			if (status->action == BROWSER_PROFILE_ACTION_DONE)
				ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, DIALOG_Y + 100, UI_ALIGN_CENTER, COLOR_GOOD, 0x6BE38A80,
					status->message);
			else
				wrapped_text(UI_FONT_MENU, 12.0f, left, DIALOG_Y + 100, width, 17, COLOR_WARN, status->message, 4);
			dialog_prompt(UI_BUTTON_A, "=OK");
		}
		return;
	}
	if (profile_screen.clan_state == _clan_asking)
	{
		static int const buttons[] = { UI_BUTTON_B, UI_BUTTON_A };
		static char const *const words[] = { "=NO", "=YES" };

		snprintf(text, sizeof(text), "Leave [%s] %s?", clan->tag, clan->name);
		fitted_text(UI_FONT_MENU, 15.0f, center, DIALOG_Y + 90, UI_ALIGN_CENTER, 0xFFFFFFFF, width, text);
		dialog_prompts(NUMBEROF(buttons), buttons, words);
		return;
	}
	/* (the invites' columns named; the rows: at most 8 at once, the picked
	one among them) */
	if (!profile->has_clan)
	{
		ui_overlay_text(UI_FONT_MENU_SMALL, 7.5f, left, y - 12, UI_ALIGN_LEFT, COLOR_LABEL, "CLAN");
		ui_overlay_text(UI_FONT_MENU_SMALL, 7.5f, left + width - 30, y - 12, UI_ALIGN_CENTER, COLOR_LABEL, "MEMBERS");
	}
	first = (short)MAX(0, MIN(profile_screen.dialog_row - 3, rows - 8));
	for (index = first; index < rows && index < first + 8; index++)
	{
		boolean picked = index == profile_screen.dialog_row;
		boolean off = profile->has_clan && index == _clan_row_leave && clan_leader_held();
		char const *label, *value;
		char number[16];

		if (profile->has_clan)
		{
			label = index == _clan_row_tag ? "SHOW MY TAG" : clan->count == 1 ? "DISBAND CLAN" : "LEAVE CLAN";
			value = index == _clan_row_tag ? (!profile->has_privacy ? "-" : profile->show_clan_tag ? "ON" : "OFF") : "";
		}
		else
		{
			snprintf(text, sizeof(text), "[%s] %s", profile->invites[index].tag, profile->invites[index].name);
			snprintf(number, sizeof(number), "%d", profile->invites[index].count);
			label = text;
			value = number;
		}
		if (picked)
		{
			ui_overlay_rect(left - 6, y - 3, width + 12, 20, 5, COLOR_SELECTION_FILL);
			ui_overlay_outline(left - 6, y - 3, width + 12, 20, 5, 1.2f, COLOR_SELECTION_EDGE);
		}
		fitted_text(UI_FONT_MENU, 10.0f, left, y, UI_ALIGN_LEFT, off ? COLOR_ROW_DIM : picked ? COLOR_TEXT : COLOR_ROW,
			width - 70, label);
		if (value[0])
			ui_overlay_text(UI_FONT_MENU, 10.0f, left + width - 30, y, UI_ALIGN_CENTER, COLOR_TEXT, value);
		if (picked && profile->has_clan && index == _clan_row_tag)
		{
			draw_arrow(left + width - 62, y, TRUE, COLOR_PROMPT);
			draw_arrow(left + width + 2, y, FALSE, COLOR_PROMPT);
		}
		add_target(left - 6, y - 3, left + width + 6, y + 17, NONE, NONE, index);
		y += 22;
	}
	/* (one line under the rows: why the leader can't leave, or how the last
	change went) */
	if (profile->has_clan && profile_screen.dialog_row == _clan_row_leave && clan_leader_held())
		ui_overlay_text(UI_FONT_MENU_SMALL, 9.0f, left, y + 8, UI_ALIGN_LEFT, COLOR_DIM, "Make another member leader first");
	else if (status->message[0] && status->action != BROWSER_PROFILE_ACTION_NONE)
		ui_overlay_text(UI_FONT_MENU_SMALL, 9.0f, left, y + 8, UI_ALIGN_LEFT,
			status->action == BROWSER_PROFILE_ACTION_FAILED ? COLOR_WARN : COLOR_GOOD, status->message);
	if (profile->has_clan)
	{
		static int const buttons[] = { UI_BUTTON_B, UI_BUTTON_A };
		static char const *const words[] = { "=BACK", "=SELECT" };

		dialog_prompts(NUMBEROF(buttons), buttons, words);
	}
	else
	{
		static int const buttons[] = { UI_BUTTON_B, UI_BUTTON_X, UI_BUTTON_A };
		static char const *const words[] = { "=BACK", "=DECLINE", "=JOIN" };

		dialog_prompts(NUMBEROF(buttons), buttons, words);
	}
}

static void draw_dialog(
	float margin)
{
	ui_overlay_layer(1);
	ui_overlay_rect(-margin, BAND_Y + 1, 640 + 2 * margin, BAR_Y - BAND_Y - 2, 0, COLOR_SHADE);
	switch (profile_screen.dialog)
	{
	case _dialog_link: draw_link_dialog(); break;
	case _dialog_unlink: draw_unlink_dialog(); break;
	case _dialog_clan: draw_clan_dialog(); break;
	default: draw_page_dialog(); break;
	}
	ui_overlay_layer(0);
}

/* each frame (ui_widget.c's render_ui_widgets) */
void profile_screen_render(
	void)
{
	float margin = (float)((halo_screen_width() - 640) / 2 + 2);

	profile_screen.target_count = 0;
	/* (the Chupathingy menus' panel showing the clan: its emblem) */
	if (!profile_screen.active && ui_overlay_available() && profile_screen.card_time &&
		system_milliseconds() - profile_screen.card_time < CARD_EMBLEM_KEPT && linked() && profile_screen.profile.has_clan)
	{
		draw_emblem(profile_screen.card_x, profile_screen.card_y, profile_screen.card_size);
	}
	if (!ui_overlay_available() || !profile_screen_active())
		return;
	if (profile_screen.active)
	{
		draw_header(-margin, 640 + margin);
		ui_overlay_prompt_bar(-margin, 640 + margin, BAR_Y, BAR_BOTTOM);
		draw_sections();
		if (profile_screen.dialog == _dialog_none)
		{
			draw_panel();
			draw_bar();
		}
	}
	if (profile_screen.dialog != _dialog_none)
	{
		/* (over the menus' own screen: the dialog's targets alone) */
		profile_screen.target_count = 0;
		draw_dialog(margin);
	}
}

/* the menus' pointer while it is up (ui_widget.c): a click on a prompt
presses it, on a section shows it, on a row picks it (again: changes it);
the right button is B */
void profile_screen_pointer(
	struct halo_ui_pointer const *pointer)
{
	short index;

	if (!profile_screen_active())
		return;
	if (pointer->right_clicks)
		press(_gamepad_analog_button_b);
	if (!pointer->left_clicks)
		return;
	for (index = 0; index < profile_screen.target_count; index++)
	{
		if (pointer->click_x < profile_screen.targets[index].x0 || pointer->click_x >= profile_screen.targets[index].x1 ||
			pointer->click_y < profile_screen.targets[index].y0 || pointer->click_y >= profile_screen.targets[index].y1)
		{
			continue;
		}
		if (profile_screen.targets[index].button != NONE)
			press(profile_screen.targets[index].button);
		else if (profile_screen.dialog == _dialog_clan && profile_screen.targets[index].row != NONE)
		{
			/* (a row of the clan dialog: picked, again: chosen) */
			if (profile_screen.dialog_row == profile_screen.targets[index].row)
				press(_gamepad_analog_button_a);
			profile_screen.dialog_row = profile_screen.targets[index].row;
		}
		else if (profile_screen.targets[index].row != NONE)
		{
			if (profile_screen.in_panel && profile_screen.row == profile_screen.targets[index].row)
				privacy_change(1);
			profile_screen.in_panel = TRUE;
			profile_screen.row = profile_screen.targets[index].row;
		}
		else if (profile_screen.targets[index].section == profile_screen.section && !profile_screen.in_panel)
			press(_gamepad_analog_button_a);
		else
		{
			profile_screen.section = profile_screen.targets[index].section;
			profile_screen.in_panel = FALSE;
		}
		return;
	}
}

#endif
