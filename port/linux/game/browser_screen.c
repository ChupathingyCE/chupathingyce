/*
BROWSER_SCREEN.C

The in-game server browser (configure.py
--game-browser): every game on the game list (port/linux/src/browser.c),
on a screen of its own over the menus, as the game's virtual keyboard is
(interface/virtual_keyboard.c): drawn and driven by code, not a widget of
the user interface's tags.

The Multiplayer menu's ONLINE GAMES item opens it (ui_widget.c). Up and
down pick a game, left and right turn the page, A joins it through its
invite, as a web page's Join or an invite link would, and B goes back. Once
the invite's host answers and advertises its game through the tunnel, the
game is joined and its lobby opened (wait_for_host).

A game on a Custom Edition map (Halo PC's, announced as <file>@ce) or a
HaloMD map (announced as <file>@md: halo_map_families.h) is named as the
menus' map list names it (ui_map_list.c: Halo PC's own names, HaloMD's mod
list's, or the file's made readable), marked HALO PC or HALOMD as the game
list's web pages mark it, and pictured by Halo PC's own picture of it (one
of Halo PC's maps), else the site's (map_art.c: a stock map without Halo
PC's ui.map), else a card with its name in its family's color. It is
joined only with the map in its family's folders (and on a build with Halo
PC map support, HALO_CUSTOM_EDITION): else its details say what is missing,
and A says so rather than join. A Custom Edition map can instead be
downloaded from the community map repositories (map_download.c, as
network.map_downloads says): A joins the game's invite, and once its host
advertises the game, the screen offers the map (Yes, No, Don't ask again),
downloads it with its progress shown (B cancels), checks it against the
host's version of it, and joins. A game left for want of such a map
(cache_files_map_present: a join through another screen, or the host moving
its game on to the map, as a dedicated server's playlist does) opens this
screen over the main menu with the same offer, and joins that host again,
in its next lobby or its game in progress (the game's protocol has the host
wait a minute at most for a machine to load a map, too short for most
downloads, so the game is left and joined again). If the map is not
downloaded after all, Online Games closes and the main menu says the map is
missing, as without the downloader.

Start opens Delta Profile (profile_screen.c; the web page with
display.delta_profile off); RB (only while
this copy is not linked to a profile: browser_profile_linked) opens Link
Profile over the list, for where no web browser opens: a code (and a QR
code) to type at the game list's /connect page on another device, then the
profile it was typed for, to confirm with A or refuse with B (browser.c).

The menus' pointer (the mouse, or the touchscreen's taps and drags:
halo_ui_pointer.h) works it as the PC menus' lists: a click on a game picks
it and a click on the picked game joins it, the wheel or a drag steps
through the games, a click on a button's prompt presses that button, and
the right button is B.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "cutscene/cinematics.h"
#include "halo_map_families.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/interface.h"
#include "rasterizer/rasterizer.h"
#include "text/draw_string.h"
#include "bitmaps/bitmap_group.h"
#include "interface/ui_widget.h"
#include "tag_files/tag_groups.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "saved games/saved_game_files.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "../src/browser.h"
#include "halo_port_limits.h"
#include "network_rules.h"
#include "../src/browser_ping.h"
#include "../src/map_art.h"
#include "../src/map_download.h"
#include "../src/ui_overlay.h"
#include "halo_ui_map_list.h"
#include "halo_ui_pointer.h"
#ifndef HALO_CONSOLE_NO_DELTA_PROFILE
#include "halo_profile_screen.h"

boolean pc_menus_chupathingy(void);
boolean pc_menu_delta_profile_open(void);
#endif

/* ---------- constants */

enum
{
	/* (event_manager.c's event types, which it keeps to itself) */
	BROWSER_EVENT_LEFT_STICK = 1,
	BROWSER_EVENT_BUTTON = 3,

	ROWS_PER_PAGE = 9,
	STATUS_DURATION = 6000,
	/* a picked game's host answers this soon, or it is given up on */
	CONNECT_TIMEOUT = 15000,
	/* the screen takes no A this soon after it opens */
	OPEN_SETTLE = 600,
	/* nor a button this soon after Link Profile's panel opens or closes (a
	press seen twice would close it, or the screen) */
	CONNECT_SETTLE = 400,
	/* a held direction: the first repeat waits this long, then it repeats
	this often; a gap longer than the last is a new press */
	STEP_FIRST_REPEAT_DELAY = 350,
	STEP_REPEAT_INTERVAL = 110,
	STEP_RELEASE_GAP = 150,
	/* whether the selected game's Halo PC map is in its family's folders,
	asked again this often */
	CE_MAP_CHECK_INTERVAL = 1000,
	/* the places the pointer presses a button, drawn each frame */
	MAXIMUM_TARGETS = 24,
};

/* whether a game's map can be played here (ce_map_state) */
enum
{
	/* an Xbox map */
	_ce_map_none,
	/* a Halo PC (Custom Edition or HaloMD) map, in its family's folders */
	_ce_map_present,
	/* a Halo PC map this machine lacks */
	_ce_map_missing,
	/* a Halo PC map, on a build without HALO_CUSTOM_EDITION */
	_ce_map_unsupported,
};

/* a Custom Edition map offered for the game being joined (map_download.c) */
enum
{
	_offer_none,
	/* asking whether to download it */
	_offer_asking,
	_offer_downloading,
	/* the download failed: why, until B */
	_offer_failed,
	/* one of Halo PC's own maps, never downloaded: where to copy it from,
	until B (map_archive_stock_name) */
	_offer_stock,
};

/* the game's engines, short (as players say them) to fit the column */
static char const *const engine_names[] =
{
	"", "CTF", "Slayer", "Oddball", "King", "Race", "Bomb",
};

/* the multiplayer maps' names in the menus */
static char const *const map_names[][2] =
{
	{ "beavercreek", "Battle Creek" }, { "bloodgulch", "Blood Gulch" }, { "boardingaction", "Boarding Action" },
	{ "carousel", "Derelict" }, { "chillout", "Chill Out" }, { "damnation", "Damnation" },
	{ "hangemhigh", "Hang 'Em High" }, { "longest", "Longest" }, { "prisoner", "Prisoner" },
	{ "putput", "Chiron TL-34" }, { "ratrace", "Rat Race" }, { "sidewinder", "Sidewinder" }, { "wizard", "Wizard" },
};

/* the list's orders (RT steps through them, LT and LB back; RB is Link
Profile's) */
enum
{
	SORT_PLAYERS,
	SORT_NAME,
	SORT_MAP,
	SORT_TYPE,
	/* (the shortest round trip first; games not measured last) */
	SORT_PING,

	NUMBER_OF_SORTS
};

/* ---------- globals */

static struct
{
	boolean active;
	short selected;
	short count;
	struct browser_game games[BROWSER_MAXIMUM_GAMES];
	char status[96];
	unsigned long status_time;
	short sort;
	/* a game picked: its invite, while its host's game is waited for */
	boolean connecting;
	char connecting_invite[BROWSER_INVITE_LENGTH + 1];
	char connecting_name[64];
	unsigned long connecting_time;
	/* when the screen opened (the menu's A that opened it picks nothing) */
	unsigned long opened_time;
	/* the last game's map asked of its family's folders, and the answer
	(ce_map_state) */
	char ce_map[BROWSER_MAP_LENGTH];
	short ce_map_answer;
	unsigned long ce_map_time;
	/* Link Profile's panel up, and what it shows (browser.c's, each frame);
	when it last opened, closed or asked for a code */
	boolean connect_open;
	unsigned long connect_changed_time;
	struct browser_connect connect;
	/* the game being joined: whether its host's advertised map was checked
	(wait_for_host) */
	boolean connecting_map_checked;
	/* a Custom Edition map the game being joined needs (_offer_*): its
	file's name, the host's version of it (its header's checksum, 0 if not
	known), "Don't ask again" ticked, and the download's progress */
	short offer;
	char offer_file[32];
	/* (the name the player knows it by: a hashed name's base, map_archive.h) */
	char offer_display[32];
	/* (a stock map's panel: heading and two lines, from map_family_stock_need) */
	char offer_stock_first[96];
	char offer_stock_second[160];
	unsigned long offer_version;
	boolean offer_dont_ask;
	struct map_download_progress offer_progress;
	/* when it was offered (the A that joined, still held, answers nothing) */
	unsigned long offer_time;
	/* a join left for want of that map (browser_screen_map_missing), to be
	joined again once it is downloaded: its invite (empty: a game of the
	local network) and its host's identifier (12 hex digits, as an invite
	starts) */
	boolean rejoin_pending;
	/* a game left for one of Halo PC's own maps (browser_screen_map_missing):
	its panel opened over the main menu, and Online Games closed with it */
	boolean stock_pending;
	boolean offer_closes_screen;
	char rejoin_invite[BROWSER_INVITE_LENGTH + 1];
	char rejoin_host[13];
	/* what the last frame drew for the pointer: the button each prompt
	presses, and the list's first game (none: the list not drawn) */
	short target_count;
	struct
	{
		float x0, y0, x1, y1;
		short button;
		/* a column's head: the order it puts the list in (NONE: a button) */
		short sort;
	} targets[MAXIMUM_TARGETS];
	short drawn_first;
} browser_screen;

/* ---------- private code */

static void set_status(
	char const *text)
{
	csstrncpy(browser_screen.status, text, sizeof(browser_screen.status) - 1);
	browser_screen.status[sizeof(browser_screen.status) - 1] = 0;
	browser_screen.status_time = system_milliseconds();
}

static void utf8_text(unsigned short const *name, long length, char *text, long size);

/* a game's map: its file's name (the path's last part, without a Halo PC
map's @ce or @md), and its family (halo_map_families.h) */
static short map_file(
	char const *path,
	char *file,
	long size)
{
	return map_family_parse_listed(path, file, size);
}

static short map_family(
	char const *path)
{
	return map_family_parse_listed(path, NULL, 0);
}

/* the campaign's levels in the menus' words, for the co-op games the list
shows (a level's scenario is its file's name) */
static char const *const campaign_names[][2] =
{
	{ "a10", "The Pillar of Autumn" }, { "a30", "Halo" }, { "a50", "The Truth and Reconciliation" },
	{ "b30", "The Silent Cartographer" }, { "b40", "Assault on the Control Room" }, { "c10", "343 Guilty Spark" },
	{ "c20", "The Library" }, { "c40", "Two Betrayals" }, { "d20", "Keyes" }, { "d40", "The Maw" },
};

/* a campaign level's name from its file's, or NULL for a map that is not one */
static char const *campaign_name(
	char const *file)
{
	long index;

	for (index = 0; index < NUMBEROF(campaign_names); index++)
	{
		if (!csstrcmp(file, campaign_names[index][0]))
			return campaign_names[index][1];
	}
	return NULL;
}

/* a game's map as players name it: an Xbox map's name in the menus, or a
Custom Edition map's as the menus' map list names it */
static char const *map_display_name(
	char const *path,
	char *text,
	long size)
{
	char file[BROWSER_MAP_LENGTH];
	long index;
	short family = map_file(path, file, sizeof(file));

	if (family != _map_family_xbox)
	{
		wchar_t name[48];
		unsigned short characters[48];
		long length;

		ui_map_list_map_name(path, name, NUMBEROF(name));
		for (length = 0; length < NUMBEROF(name) && name[length]; length++)
			characters[length] = (unsigned short)name[length];
		utf8_text(characters, length, text, size);
		/* (the file's name as it is, if nothing else names it) */
		if (!text[0])
			snprintf(text, (size_t)size, "%s", file);
		return text;
	}
	for (index = 0; index < NUMBEROF(map_names); index++)
	{
		if (!csstrcmp(file, map_names[index][0]))
			return map_names[index][1];
	}
	if (campaign_name(file))
		return campaign_name(file);
	/* (the path's last part, as it was) */
	snprintf(text, (size_t)size, "%s", file);
	return text;
}

/* a listed game's map as players name it: an Xbox map pack's by the name
its host announces ("Gothra": a player without the pack has no other), any
other as map_display_name names it */
static char const *game_map_display_name(
	struct browser_game const *game,
	char *text,
	long size)
{
	if (map_family(game->map) == _map_family_pack && game->map_title[0])
	{
		snprintf(text, (size_t)size, "%s", game->map_title);
		return text;
	}
	return map_display_name(game->map, text, size);
}

/* whether a game's map can be played here: an Xbox map, or a Custom Edition
or HaloMD map in its family's folders, missing, or on a build without them;
the folders asked again when fresh, or for another map, or now and then */
static short ce_map_state(
	struct browser_game const *game,
	boolean fresh)
{
	char file[BROWSER_MAP_LENGTH];
	short family = map_file(game->map, file, sizeof(file));

	if (family == _map_family_xbox)
		return _ce_map_none;
#ifdef HALO_CUSTOM_EDITION
	if (fresh || strcmp(browser_screen.ce_map, game->map) ||
		system_milliseconds() - browser_screen.ce_map_time > CE_MAP_CHECK_INTERVAL)
	{
		csstrncpy(browser_screen.ce_map, game->map, sizeof(browser_screen.ce_map) - 1);
		browser_screen.ce_map[sizeof(browser_screen.ce_map) - 1] = 0;
		browser_screen.ce_map_answer = ui_map_list_map_present(game->map) ? _ce_map_present : _ce_map_missing;
		browser_screen.ce_map_time = system_milliseconds();
	}
	return browser_screen.ce_map_answer;
#else
	(void)fresh;
	(void)family;
	return _ce_map_unsupported;
#endif
}

/* whether a game's missing map may be downloaded (a Custom Edition map,
as network.map_downloads says: map_download.c) */
static boolean ce_map_downloadable(
	struct browser_game const *game)
{
	char file[BROWSER_MAP_LENGTH];

	return map_file(game->map, file, sizeof(file)) == _map_family_custom_edition &&
		ce_map_state(game, FALSE) == _ce_map_missing && map_download_possible(file);
}

/* whether a game's missing map is one of Halo PC's own (map_archive.h):
never downloaded, the player told to copy it from their install */
static boolean ce_map_stock(
	struct browser_game const *game)
{
	char file[BROWSER_MAP_LENGTH];

	return map_file(game->map, file, sizeof(file)) == _map_family_custom_edition &&
		ce_map_state(game, FALSE) == _ce_map_missing && map_archive_stock_name(file);
}

/* (network_client_manager.c: the game whose host's identifier the invite
starts with, joined once it is advertised) */
long network_game_client_join_invite_host(char const *invite);
boolean create_global_network_game_client(void);
void game_connection_set(short connection);
/* (interface/: the network, as System Link's list starts it, and a game of
this machine's, as its Y makes one) */
boolean ui_online_games_start_network(void);
boolean ui_widget_online_games_create_game(void);

static void utf8_name(unsigned short const *name, char *text, long size);

/* the first player in the game to be joined or made, with the profile
System Link's Start would pick: the one last used, else the first saved.
(A profile's index is the saved game files' (saved_game_files.c), its valid
bit set: 0 is none, and player_profile_get made of it a profile named for
whichever saved file came first, a game type on a new install.) Its index
and profile, or NONE for none saved */
static long first_player_profile(
	struct player_profile *profile)
{
	long profile_index = player_ui_get_player1_last_used_profile_index();

	if (profile_index == NONE || !TEST_FLAG(profile_index, _saved_game_file_index_valid_bit))
	{
		long profile_indices[100];
		word profile_count = NUMBEROF(profile_indices);

		player_profiles_enumerate_available_to_local_player_index(0, &profile_count, profile_indices, FALSE);
		profile_index = profile_count ? profile_indices[0] : NONE;
	}
	if (profile_index != NONE && TEST_FLAG(profile_index, _saved_game_file_index_valid_bit) &&
		player_profile_get(profile_index, profile))
	{
		return profile_index;
	}
	return NONE;
}

/* the first player joined with it (none saved: the player keeps the profile
it has) */
static void join_first_player(
	void)
{
	struct player_profile profile;
	long profile_index;

	player_ui_local_player_joined_multiplayer_game(0);
	profile_index = first_player_profile(&profile);
	if (profile_index != NONE)
		player_ui_set_active_player_profile(0, profile_index, &profile);
}

/* ---------- a Custom Edition map the game being joined needs */

/* (p2p.c: the invite this machine joined last, its hex digits) */
int p2p_joined_invite(char *text, int size);
void platform_log(char const *format, ...);
int config_boolean(char const *name);
void display_error_text_deferred(wchar_t const *text, short local_player_index);

/* a game left for want of the map, which is not downloaded after all (the
player said no, cancelled it, or it failed: why, or NULL): Online Games
closed, and the main menu's error says the map is missing, as it does with
network.map_downloads "never" (cache_files_map_present's words) */
static void rejoin_given_up(
	char const *why)
{
	static wchar_t message[256];
	char text[256];
	long index;

	platform_log("map download: %s.map not downloaded (%s): the game is not joined again", browser_screen.offer_file,
		why ? why : "not wanted");
	browser_screen.offer = _offer_none;
	browser_screen.connecting = FALSE;
	browser_screen.rejoin_pending = FALSE;
	browser_screen.active = FALSE;
	map_download_reset();
	/* (the words the error has without the downloader: why the download
	failed is in the log, the dialog having no room for more) */
	snprintf(text, sizeof(text), "You don't have the %s map %.31s. Copy %.31s.map into %s.",
		map_family_badge(_map_family_custom_edition), browser_screen.offer_file, browser_screen.offer_file,
		map_family_folder(_map_family_custom_edition));
	for (index = 0; text[index] && index < NUMBEROF(message) - 1; index++)
		message[index] = (wchar_t)(unsigned char)text[index];
	message[index] = 0;
	display_error_text_deferred(message, NONE);
}

static void start_offered_download(
	void)
{
	if (map_download_start(browser_screen.offer_file, (unsigned int)browser_screen.offer_version))
	{
		browser_screen.offer = _offer_downloading;
		map_download_get(&browser_screen.offer_progress);
	}
	else if (browser_screen.rejoin_pending)
		rejoin_given_up("another download is running");
	else
	{
		browser_screen.offer = _offer_failed;
		csmemset(&browser_screen.offer_progress, 0, sizeof(browser_screen.offer_progress));
		snprintf(browser_screen.offer_progress.message, sizeof(browser_screen.offer_progress.message),
			"another download is running");
	}
}

/* one of Halo PC's own maps a game needs: its panel says to copy it from
the player's install (never downloaded) */
static void offer_stock(
	char const *file,
	unsigned long version)
{
	char blocker[160];

	csstrncpy(browser_screen.offer_file, file, sizeof(browser_screen.offer_file) - 1);
	browser_screen.offer_file[sizeof(browser_screen.offer_file) - 1] = 0;
	map_archive_display_name(browser_screen.offer_file, browser_screen.offer_display,
		(int)sizeof(browser_screen.offer_display));
	/* (the install's file names, lowercase) */
	{
		char *cursor;

		for (cursor = browser_screen.offer_display; *cursor; cursor++)
		{
			if (*cursor >= 'A' && *cursor <= 'Z')
				*cursor = (char)(*cursor + 32);
		}
	}
	map_family_stock_need(file, version, browser_screen.offer_stock_first, sizeof(browser_screen.offer_stock_first),
		browser_screen.offer_stock_second, sizeof(browser_screen.offer_stock_second), blocker, sizeof(blocker));
	browser_screen.offer = _offer_stock;
	browser_screen.offer_time = system_milliseconds();
	browser_screen.offer_closes_screen = FALSE;
	browser_screen.connecting = FALSE;
	platform_log("map download: %s is one of the game's own maps; not downloaded", file);
}

/* the map offered (asked about, or downloaded without asking, as
network.map_downloads says) */
static void offer_map(
	char const *file,
	unsigned long version)
{
	csstrncpy(browser_screen.offer_file, file, sizeof(browser_screen.offer_file) - 1);
	browser_screen.offer_file[sizeof(browser_screen.offer_file) - 1] = 0;
	map_archive_display_name(browser_screen.offer_file, browser_screen.offer_display,
		(int)sizeof(browser_screen.offer_display));
	browser_screen.offer_version = version;
	browser_screen.offer_dont_ask = FALSE;
	browser_screen.offer_time = system_milliseconds();
	map_download_reset();
	if (map_download_setting() == MAP_DOWNLOADS_ALWAYS)
		start_offered_download();
	else
		browser_screen.offer = _offer_asking;
}

/* the offer ended without the map: the join given up, with why (a game
left for want of it: the main menu's error, rejoin_given_up) */
static void offer_given_up(
	char const *why)
{
	char text[96];

	if (browser_screen.rejoin_pending)
	{
		rejoin_given_up(NULL);
		return;
	}
	browser_screen.offer = _offer_none;
	browser_screen.connecting = FALSE;
	browser_screen.rejoin_pending = FALSE;
	map_download_reset();
	if (why)
		set_status(why);
	else
	{
		snprintf(text, sizeof(text), "Needs maps_ce/%s.map to join", browser_screen.offer_display);
		set_status(text);
	}
}

/* the host of a join left for want of the map joined again: through its
invite (the tunnel to it), or on the local network, as a picked game is */
static void rejoin(
	void)
{
	browser_screen.rejoin_pending = FALSE;
	platform_log("map download: %s.map in place: joining the game again%s", browser_screen.offer_file,
		browser_screen.rejoin_invite[0] ? " (through its invite)" : " (on the local network)");
	join_first_player();
	if (browser_screen.rejoin_invite[0] && !browser_join(browser_screen.rejoin_invite))
	{
		set_status("Turn on Internet Play in Settings.");
		return;
	}
	browser_screen.connecting = TRUE;
	browser_screen.connecting_map_checked = FALSE;
	csstrncpy(browser_screen.connecting_invite, browser_screen.rejoin_host, sizeof(browser_screen.connecting_invite) - 1);
	browser_screen.connecting_invite[sizeof(browser_screen.connecting_invite) - 1] = 0;
	browser_screen.connecting_time = system_milliseconds();
}

/* the download followed, each frame: once the map is in place, the game is
joined */
static void follow_download(
	void)
{
	char text[96];

	map_download_get(&browser_screen.offer_progress);
	switch (browser_screen.offer_progress.state)
	{
	case MAP_DOWNLOAD_DONE:
		browser_screen.offer = _offer_none;
		map_download_reset();
		/* (the map's folder asked again: it is there now) */
		browser_screen.ce_map_time = system_milliseconds() - CE_MAP_CHECK_INTERVAL - 1;
		snprintf(text, sizeof(text), "Downloaded %s.map", browser_screen.offer_display);
		set_status(text);
		if (browser_screen.rejoin_pending)
			rejoin();
		else
			browser_screen.connecting_time = system_milliseconds();
		break;
	case MAP_DOWNLOAD_FAILED:
		/* (a game left for want of it: the main menu's error at once) */
		if (browser_screen.rejoin_pending)
			rejoin_given_up(browser_screen.offer_progress.message[0] ? browser_screen.offer_progress.message :
				"the repositories did not answer");
		else
			browser_screen.offer = _offer_failed;
		break;
	case MAP_DOWNLOAD_CANCELLED:
		offer_given_up("Download cancelled.");
		break;
	default: break;
	}
}

/* the offer's buttons: while asking, A downloads, B doesn't, Y ticks
"Don't ask again" (kept with the answer: network.map_downloads); while
downloading, B cancels; once failed, A or B closes */
static void offer_button(
	short button)
{
	switch (browser_screen.offer)
	{
	case _offer_asking:
		if (button == _gamepad_analog_button_a)
		{
			if (browser_screen.offer_dont_ask)
				map_download_set_setting(MAP_DOWNLOADS_ALWAYS);
			start_offered_download();
		}
		else if (button == _gamepad_analog_button_b)
		{
			if (browser_screen.offer_dont_ask)
				map_download_set_setting(MAP_DOWNLOADS_NEVER);
			offer_given_up(NULL);
		}
		else if (button == _gamepad_analog_button_y)
			browser_screen.offer_dont_ask = !browser_screen.offer_dont_ask;
		break;
	case _offer_downloading:
		if (button == _gamepad_analog_button_b)
			map_download_cancel();
		break;
	case _offer_failed:
		if (button == _gamepad_analog_button_a || button == _gamepad_analog_button_b)
			offer_given_up(NULL);
		break;
	case _offer_stock:
		if (button == _gamepad_analog_button_a || button == _gamepad_analog_button_b)
		{
			browser_screen.offer = _offer_none;
			set_status("");
			/* (a game left for it: back to the main menu) */
			if (browser_screen.offer_closes_screen)
				browser_screen.active = FALSE;
			browser_screen.offer_closes_screen = FALSE;
		}
		break;
	default: break;
	}
}

static char const *game_type_blocker(struct browser_game const *game);
static char const *version_blocker(struct browser_game const *game, char *text, long size);

/* a game picked: its invite joined (the tunnel to its host), then its game
joined once advertised through it (browser_screen_process) */
static void join_selected(
	void)
{
	struct browser_game const *game;

	if (browser_screen.selected < 0 || browser_screen.selected >= browser_screen.count)
		return;
	game = &browser_screen.games[browser_screen.selected];
	{
		char text[96];

		if (version_blocker(game, text, sizeof(text)))
		{
			set_status(text);
			return;
		}
	}
	if (!game->open)
	{
		set_status("That game is not accepting players.");
		return;
	}
	if (game_type_blocker(game))
	{
		set_status(game_type_blocker(game));
		return;
	}
	/* (a Custom Edition map's game, only with the map: a join without it
	would fail at its loading; the list's footer says what is missing. A
	map that may be downloaded is offered once the host advertises it:
	wait_for_host) */
	if (ce_map_state(game, TRUE) >= _ce_map_missing && !ce_map_downloadable(game))
	{
		/* (one of Halo PC's own maps: where to copy it from) */
		if (ce_map_stock(game))
		{
			char file[BROWSER_MAP_LENGTH];

			map_file(game->map, file, sizeof(file));
			offer_stock(file, 0);
		}
		return;
	}
	/* a network client searching, as System Link's (the advertisement comes
	to it: browser_screen_open started it) */
	if (!global_network_game_client_get())
	{
		if (!create_global_network_game_client())
		{
			set_status("Could not start the network.");
			return;
		}
		game_connection_set(_game_connection_network_client);
	}
	join_first_player();
	if (!browser_join(game->invite))
	{
		set_status("Turn on Internet Play in Settings.");
		return;
	}
	browser_screen.connecting = TRUE;
	browser_screen.connecting_map_checked = FALSE;
	browser_screen.rejoin_pending = FALSE;
	csstrncpy(browser_screen.connecting_invite, game->invite, sizeof(browser_screen.connecting_invite) - 1);
	browser_screen.connecting_invite[sizeof(browser_screen.connecting_invite) - 1] = 0;
	utf8_name(game->name, browser_screen.connecting_name, sizeof(browser_screen.connecting_name));
	browser_screen.connecting_time = system_milliseconds();
}

/* the picked game's host: its game joined once it is advertised, and its
lobby opened */
static void wait_for_host(
	void)
{
	long joined;

	/* (the map its host advertises, first: a Custom Edition map this
	machine lacks offered for download before the game is joined) */
	if (!browser_screen.connecting_map_checked)
	{
		struct network_invite_advertisement advertisement;
		long found = network_game_client_invite_host_advertisement(browser_screen.connecting_invite, &advertisement);

		if (found == 0)
		{
			if (system_milliseconds() - browser_screen.connecting_time > CONNECT_TIMEOUT)
			{
				browser_screen.connecting = FALSE;
				set_status("The host did not answer.");
			}
			return;
		}
		browser_screen.connecting_map_checked = TRUE;
#ifdef HALO_CUSTOM_EDITION
		if (found > 0)
		{
			char file[BROWSER_MAP_LENGTH];
			short family = map_family_parse(advertisement.map_name, file, sizeof(file));

			if (family == _map_family_custom_edition && !ui_map_list_family_present(family, file) &&
				map_archive_stock_name(file))
			{
				offer_stock(file, advertisement.map_version);
				return;
			}
			if (family == _map_family_custom_edition && !ui_map_list_family_present(family, file) &&
				map_download_possible(file))
			{
				offer_map(file, advertisement.map_version);
				return;
			}
		}
#endif
	}
	joined = network_game_client_join_invite_host(browser_screen.connecting_invite);
	if (joined > 0)
	{
		extern char const *pc_menus_screen(char const *name);

		browser_screen.connecting = FALSE;
		browser_screen.active = FALSE;
		ui_widgets_close_all();
		/* (the PC menus' lobby, when they are the menus) */
		ui_widget_load_by_name_or_tag(
			pc_menus_screen("ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen"),
			NONE, NULL, NONE, NONE, NONE, NONE);
	}
	else if (joined < 0)
	{
		browser_screen.connecting = FALSE;
		set_status("That game can't be joined from this version.");
	}
	else if (system_milliseconds() - browser_screen.connecting_time > CONNECT_TIMEOUT)
	{
		browser_screen.connecting = FALSE;
		set_status("The host did not answer.");
	}
}

static long compare_names(
	unsigned short const *a,
	unsigned short const *b)
{
	long index;

	for (index = 0; index < BROWSER_NAME_LENGTH; index++)
	{
		unsigned short x = a[index] >= 'a' && a[index] <= 'z' ? (unsigned short)(a[index] - 32) : a[index];
		unsigned short y = b[index] >= 'a' && b[index] <= 'z' ? (unsigned short)(b[index] - 32) : b[index];

		if (x != y || !x)
			return (long)x - (long)y;
	}
	return 0;
}

static long compare_games(
	struct browser_game const *a,
	struct browser_game const *b)
{
	long order;

	/* (closed games last, those of another version after them, whatever the order) */
	if (!browser_game_version_state(a) != !browser_game_version_state(b))
		return browser_game_version_state(a) ? 1 : -1;
	if (a->open != b->open)
		return a->open ? -1 : 1;
	switch (browser_screen.sort)
	{
	case SORT_NAME: order = compare_names(a->name, b->name); break;
	case SORT_MAP:
	{
		char a_name[64], b_name[64];

		order = strcmp(game_map_display_name(a, a_name, sizeof(a_name)), game_map_display_name(b, b_name, sizeof(b_name)));
		/* (an Xbox map before Halo PC's of the same name, Custom Edition's
		before HaloMD's) */
		if (!order)
			order = (long)map_family(a->map) - (long)map_family(b->map);
		break;
	}
	case SORT_TYPE: order = (long)a->engine * 2 + a->teams - ((long)b->engine * 2 + b->teams); break;
	case SORT_PING:
		/* (measured, then estimated, then none) */
		order = browser_ping_order(a->ping, a->ping_estimated, b->ping, b->ping_estimated);
		break;
	default: order = (long)b->players - (long)a->players; break;
	}
	return order ? order : compare_names(a->name, b->name);
}

/* the game list's games, in the screen's order; the selection stays on its
game */
static void fetch_games(
	void)
{
	char invite[BROWSER_INVITE_LENGTH + 1];
	short index, other;

	invite[0] = 0;
	if (browser_screen.selected >= 0 && browser_screen.selected < browser_screen.count)
		csstrncpy(invite, browser_screen.games[browser_screen.selected].invite, sizeof(invite) - 1);
	invite[sizeof(invite) - 1] = 0;
	browser_screen.count = (short)browser_get_games(browser_screen.games, BROWSER_MAXIMUM_GAMES);
	/* (co-op games are not listed here, whatever list they came from; an
	invite to one still joins) */
	for (index = other = 0; index < browser_screen.count; index++)
	{
		if (!browser_map_is_campaign(browser_screen.games[index].map))
			browser_screen.games[other++] = browser_screen.games[index];
	}
	browser_screen.count = other;
	/* (insertion: a few dozen games) */
	for (index = 1; index < browser_screen.count; index++)
	{
		struct browser_game game = browser_screen.games[index];

		for (other = index; other > 0 && compare_games(&browser_screen.games[other - 1], &game) > 0; other--)
			browser_screen.games[other] = browser_screen.games[other - 1];
		browser_screen.games[other] = game;
	}
	for (index = 0; invite[0] && index < browser_screen.count; index++)
	{
		if (!strcmp(browser_screen.games[index].invite, invite))
			browser_screen.selected = index;
	}
}

/* the game types this build plays: the Xbox's (0 is co-op), and Bomb
(game_engine_bomb.c) in a build with it; a game of another (a newer
build's) is listed, marked and not joined */
static char const *game_type_blocker(
	struct browser_game const *game)
{
#ifdef HALO_GAME_BROWSER
	long last = game_engine_bomb;
#else
	long last = game_engine_race;
#endif

	/* (a game of OpenCE's own game types, by the kind its listing says, as the
	game's own message says it on joining: network_client_message_handler.c) */
	{
		static char opence_text[96];
		char const *kind = network_rules_kind_name(game->kind, game->engine, 0);

		if (kind)
		{
			snprintf(opence_text, sizeof(opence_text), "This game uses an OpenCE game type (%s) Chupathingy doesn't support yet.",
				kind);
			return opence_text;
		}
	}
	if (game->engine >= 0 && game->engine <= last)
		return NULL;
	return game->engine == game_engine_bomb ? "Bomb and Grifball need a Chupathingy build with Bomb." :
		"A game type this build does not have: update Chupathingy to join.";
}

/* why a game of another network version is not joined, or NULL for one this
build can: it is listed and marked, not hidden */
static char const *version_blocker(
	struct browser_game const *game,
	char *text,
	long size)
{
	int state = browser_game_version_state(game);

	if (!state)
		return NULL;
	snprintf(text, (size_t)size, "This game runs %s version (network %d) and can't be joined.",
		state < 0 ? "an older" : "a newer", game->version);
	return text;
}

/* what this machine lacks to join a game on a Custom Edition map, or NULL
when nothing (ce_map_state) */
static char const *ce_map_blocker(
	struct browser_game const *game,
	char *text,
	long size)
{
	char file[BROWSER_MAP_LENGTH], display[BROWSER_MAP_LENGTH];
	short family = map_file(game->map, file, sizeof(file));

	switch (ce_map_state(game, FALSE))
	{
	case _ce_map_missing:
		/* (a hashed name by the file the player knows: map_archive.h) */
		map_archive_display_name(file, display, (int)sizeof(display));
		if (ce_map_stock(game))
		{
			char first[96], second[160];

			map_family_stock_need(file, 0, first, sizeof(first), second, sizeof(second), text, size);
		}
		else if (ce_map_downloadable(game))
		{
			snprintf(text, (size_t)size, "Needs %s/%s.map: join to download it", map_family_folder(family),
				display);
		}
		/* (a pack's map: its pack's folder, maps_<pack>) */
		else if (family == _map_family_pack)
		{
			char pack[MAP_PACK_ID_LENGTH + 1];

			map_family_pack_id(game->map, pack, sizeof(pack));
			snprintf(text, (size_t)size, "Needs maps_%s/%s.map to join", pack, file);
		}
		else
			snprintf(text, (size_t)size, "Needs %s/%s.map to join", map_family_folder(family), display);
		return text;
	case _ce_map_unsupported:
		return family == _map_family_halomd ? "HaloMD maps aren't supported in this build." :
			"Halo PC maps aren't supported in this build.";
	default:
		return NULL;
	}
}

/* whether this copy is linked to a profile (browser.c), and its name */
static boolean profile_linked(
	char *handle,
	long size)
{
	return browser_profile_linked(handle, (int)size) != 0;
}

/* RB: a Link Profile code, for the profile the screen's games are joined
with (its name goes to the page, to say who it links) */
static void open_link_profile(
	void)
{
	struct player_profile profile;
	unsigned short name[BROWSER_PLAYER_NAME_LENGTH];
	long index;

	csmemset(name, 0, sizeof(name));
	/* (the profile join_first_player takes: no saved file that is not one) */
	if (first_player_profile(&profile) != NONE)
	{
		for (index = 0; index < NUMBEROF(name) - 1 && index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH &&
			profile.player_name[index]; index++)
		{
			name[index] = (unsigned short)profile.player_name[index];
		}
	}
	browser_screen.connect_open = TRUE;
	browser_screen.connect_changed_time = system_milliseconds();
	browser_connect_start(name);
	browser_connect_get(&browser_screen.connect);
}

static void close_link_profile(
	void)
{
	browser_screen.connect_open = FALSE;
	browser_screen.connect_changed_time = system_milliseconds();
	browser_connect_stop();
}

/* the panel's buttons: A and B answer its question while it asks one, RB
asks for a new code once the last is done with, B closes it */
static void link_profile_button(
	short button)
{
	struct browser_connect const *connect = &browser_screen.connect;
	boolean asking = connect->state == BROWSER_CONNECT_CONFIRM;
	boolean done = connect->state == BROWSER_CONNECT_EXPIRED || connect->state == BROWSER_CONNECT_DECLINED ||
		connect->state == BROWSER_CONNECT_FAILED;

	switch (button)
	{
	case _gamepad_analog_button_a:
		if (asking && !connect->answered)
			browser_connect_answer(TRUE);
		break;
	case _gamepad_analog_button_b:
		if (asking)
		{
			if (!connect->answered)
				browser_connect_answer(FALSE);
		}
		else
			close_link_profile();
		break;
	case _gamepad_analog_button_black:
		if (done)
			open_link_profile();
		break;
	default: break;
	}
}

/* ---------- public code */

boolean browser_screen_active(
	void)
{
	return browser_screen.active;
}

/* the screen opened (the Multiplayer menu's ONLINE GAMES, interface/ui_widget.c) */
void browser_screen_open(
	void)
{
	browser_screen.active = TRUE;
	browser_screen.selected = 0;
	browser_screen.status[0] = 0;
	browser_screen.connecting = FALSE;
	if (browser_screen.connect_open)
		close_link_profile();
	browser_screen.opened_time = system_milliseconds();
	/* (the menu's A, still queued, is not a pick) */
	event_manager_flush();
	/* the network searching, as System Link's list starts it: a game left
	behind (a lobby backed out of) ended */
	if (!ui_online_games_start_network())
		set_status("Could not start the network.");
	fetch_games();
}

void browser_screen_map_missing(
	char const *file,
	unsigned long version)
{
#if defined(HALO_CUSTOM_EDITION) && !defined(HALO_SERVER)
	char invite[BROWSER_INVITE_LENGTH + 1];

	if (browser_screen.offer != _offer_none)
		return;
	/* (one of Halo PC's own maps: its panel says where to copy it from, in
	place of the main menu's error, whose dialog is too small for it) */
	if (map_archive_stock_name(file))
	{
		csstrncpy(browser_screen.offer_file, file, sizeof(browser_screen.offer_file) - 1);
		browser_screen.offer_file[sizeof(browser_screen.offer_file) - 1] = 0;
		browser_screen.offer_version = version;
		browser_screen.stock_pending = TRUE;
		return;
	}
	if (!map_download_possible(file))
		return;
	csstrncpy(browser_screen.offer_file, file, sizeof(browser_screen.offer_file) - 1);
	browser_screen.offer_file[sizeof(browser_screen.offer_file) - 1] = 0;
	map_archive_display_name(browser_screen.offer_file, browser_screen.offer_display,
		(int)sizeof(browser_screen.offer_display));
	browser_screen.offer_version = version;
	network_game_client_joined_host_identifier(browser_screen.rejoin_host, sizeof(browser_screen.rejoin_host));
	browser_screen.rejoin_invite[0] = 0;
	/* (the invite last joined, if it is this host's: its first 12 hex
	digits are the host's identifier, the first byte's two low bits set as
	an identifier's are: network_client_manager.c) */
	if (p2p_joined_invite(invite, sizeof(invite)) && strlen(invite) >= 12 &&
		!_strnicmp(invite + 2, browser_screen.rejoin_host + 2, 10))
	{
		char first[3];
		unsigned int value = 0;

		first[0] = invite[0];
		first[1] = invite[1];
		first[2] = 0;
		if (sscanf(first, "%2x", &value) == 1)
		{
			snprintf(first, sizeof(first), "%02x", (value & 0xFC) | 0x02);
			if (!_strnicmp(first, browser_screen.rejoin_host, 2))
				csstrncpy(browser_screen.rejoin_invite, invite, sizeof(browser_screen.rejoin_invite) - 1);
		}
	}
	browser_screen.rejoin_invite[sizeof(browser_screen.rejoin_invite) - 1] = 0;
	browser_screen.rejoin_pending = TRUE;
	error(_error_silent, "map download: the join left for %s.map; offered over the main menu, then the game joined again%s",
		file, browser_screen.rejoin_invite[0] ? " (through its invite)" : " (on the local network)");
#else
	(void)file;
	(void)version;
#endif
}

boolean browser_screen_open_map_offer(
	void)
{
	if (browser_screen.stock_pending)
	{
		char file[sizeof(browser_screen.offer_file)];

		browser_screen.stock_pending = FALSE;
		csstrncpy(file, browser_screen.offer_file, sizeof(file) - 1);
		file[sizeof(file) - 1] = 0;
		browser_screen_open();
		offer_stock(file, browser_screen.offer_version);
		browser_screen.offer_closes_screen = TRUE;
		return TRUE;
	}
	if (!browser_screen.rejoin_pending)
		return FALSE;
	browser_screen_open();
	csstrncpy(browser_screen.connecting_name, "the game", sizeof(browser_screen.connecting_name) - 1);
	offer_map(browser_screen.offer_file, browser_screen.offer_version);
	return TRUE;
}

/* the listed game an invite (its link, or the text around it) is for, if
the game list knows it: its name and its map as players name it, for the
clipboard's invite (invite_prompt.c) */
boolean browser_screen_invite_game(
	char const *link,
	char *name,
	long name_size,
	char *map,
	long map_size)
{
	static struct browser_game games[BROWSER_MAXIMUM_GAMES];
	char invite[BROWSER_INVITE_LENGTH + 1];
	char const *start = NULL;
	char const *search;
	long length = 0;
	short count, index;

	for (search = link; *search && !start; search++)
	{
		if (!_strnicmp(search, "halo://join/", 12))
			start = search + 12;
	}
	if (!start)
		return FALSE;
	while (length < BROWSER_INVITE_LENGTH && ((start[length] >= '0' && start[length] <= '9') ||
		((start[length] | 0x20) >= 'a' && (start[length] | 0x20) <= 'f')))
	{
		invite[length] = start[length];
		length++;
	}
	invite[length] = 0;
	if (length < BROWSER_INVITE_LENGTH)
		return FALSE;
	count = (short)browser_get_games(games, BROWSER_MAXIMUM_GAMES);
	for (index = 0; index < count; index++)
	{
		if (strlen(games[index].invite) == BROWSER_INVITE_LENGTH &&
			!_strnicmp(games[index].invite, invite, BROWSER_INVITE_LENGTH))
		{
			char text[64];

			utf8_name(games[index].name, name, name_size);
			snprintf(map, (size_t)map_size, "%s", game_map_display_name(&games[index], text, sizeof(text)));
			return TRUE;
		}
	}
	return FALSE;
}

/* Y: a game of this machine's, as System Link's Y makes one (the new game's
map chosen next; once it starts, the game list lists it) */
static void create_game(
	void)
{
	join_first_player();
	if (ui_widget_online_games_create_game())
		browser_screen.active = FALSE;
	else
		set_status("Could not create a game.");
}

/* the selection moved by move games, kept on the list */
static void step(
	short move)
{
	browser_screen.selected = (short)PIN(browser_screen.selected + move, 0,
		MAX(0, browser_screen.count - 1));
}

/* a step held down: the event manager sends the d-pad's and the stick's
direction every frame while it's held, so the list moved a row a frame.
The first press moves at once; held, it waits, then repeats at a steady
pace, as the menus do */
static void held_step(
	short move)
{
	static unsigned long last_event_time, last_step_time;
	static short last_move;
	static boolean repeating;
	unsigned long now = system_milliseconds();

	if (move != last_move || now - last_event_time > STEP_RELEASE_GAP)
	{
		step(move);
		last_step_time = now;
		repeating = FALSE;
	}
	else if (now - last_step_time >= (repeating ? STEP_REPEAT_INTERVAL : STEP_FIRST_REPEAT_DELAY))
	{
		step(move);
		last_step_time = now;
		repeating = TRUE;
	}
	last_move = move;
	last_event_time = now;
}

/* the public listings followed while the screen is up, for which games
have a password (browser_follow_listings); it closes in a press or a frame
of browser_screen_process, which then stops following them */
static void follow_listings(
	void)
{
	static boolean following;

	if (following != browser_screen.active)
	{
		following = browser_screen.active;
		browser_follow_listings(following);
	}
}

/* a button pressed: the controller's, or the pointer's on its prompt */
static void press(
	short button)
{
	if (system_milliseconds() - browser_screen.connect_changed_time < CONNECT_SETTLE)
		return;
	/* (Link Profile's panel takes the buttons while it is up, and the map
offer's while it is) */
	if (browser_screen.connect_open)
	{
		link_profile_button(button);
		return;
	}
	if (browser_screen.offer != _offer_none)
	{
		if (system_milliseconds() - browser_screen.offer_time >= CONNECT_SETTLE)
			offer_button(button);
		return;
	}
	switch (button)
	{
	case _gamepad_binary_button_dpad_up: held_step(-1); break;
	case _gamepad_binary_button_dpad_down: held_step(1); break;
	case _gamepad_binary_button_dpad_left: held_step(-ROWS_PER_PAGE); break;
	case _gamepad_binary_button_dpad_right: held_step(ROWS_PER_PAGE); break;
	case _gamepad_analog_button_a:
		if (!browser_screen.connecting && system_milliseconds() - browser_screen.opened_time > OPEN_SETTLE)
			join_selected();
		break;
	case _gamepad_binary_button_start:
#ifndef HALO_CONSOLE_NO_DELTA_PROFILE
		/* (Delta Profile's screen: the Chupathingy menus' own, else the
		screen of code; with display.delta_profile off, the web page) */
		if (profile_screen_available())
		{
			if (pc_menus_chupathingy() ? pc_menu_delta_profile_open() : (profile_screen_open(TRUE), TRUE))
				browser_screen.active = FALSE;
			break;
		}
#endif
		browser_open_profile();
		set_status("Opening your profile in the web browser");
		break;
	case _gamepad_analog_button_x:
		fetch_games();
		set_status("Refreshed");
		break;
	case _gamepad_analog_button_y:
		if (!browser_screen.connecting && system_milliseconds() - browser_screen.opened_time > OPEN_SETTLE)
			create_game();
		break;
	case _gamepad_binary_button_back:
		set_status("Filters are next");
		break;
	case _gamepad_analog_button_left_trigger:
	case _gamepad_analog_button_white:
		browser_screen.sort = (short)((browser_screen.sort + NUMBER_OF_SORTS - 1) % NUMBER_OF_SORTS);
		fetch_games();
		break;
	case _gamepad_analog_button_right_trigger:
		browser_screen.sort = (short)((browser_screen.sort + 1) % NUMBER_OF_SORTS);
		fetch_games();
		break;
	case _gamepad_analog_button_black:
		/* (not once this copy is linked: its prompt is not shown) */
		if (!browser_screen.connecting && !profile_linked(NULL, 0))
			open_link_profile();
		break;
	case _gamepad_analog_button_b:
		/* (B while a host is waited for: the wait given up) */
		if (browser_screen.connecting)
			browser_screen.connecting = FALSE;
		else
			browser_screen.active = FALSE;
		break;
	default: break;
	}
}

void browser_screen_process(
	void)
{
	struct event_record event;

	fetch_games();
	if (browser_screen.offer == _offer_downloading)
		follow_download();
	else if (browser_screen.connecting && browser_screen.offer == _offer_none)
		wait_for_host();
	if (browser_screen.connect_open)
		browser_connect_get(&browser_screen.connect);
	while (browser_screen.active && get_next_event(&event, NONE))
	{
		/* (a d-pad direction held steps at the menus' pace, held_step; every
		other button acts once a press, as the menus' buttons do: a held
		trigger went through the sorts a frame at a time) */
		if (event.type == BROWSER_EVENT_BUTTON &&
			(event.data.button.value == 1 ||
				(event.data.button.index >= _gamepad_binary_button_dpad_up &&
					event.data.button.index <= _gamepad_binary_button_dpad_right)))
			press(event.data.button.index);
		else if (event.type == BROWSER_EVENT_LEFT_STICK && !browser_screen.connect_open &&
			browser_screen.offer == _offer_none)
		{
			if (event.data.stick.y == SHORT_MAX)
				held_step(-1);
			else if (event.data.stick.y == SHORT_MIN)
				held_step(1);
			else if (event.data.stick.x == SHORT_MIN)
				held_step(-ROWS_PER_PAGE);
			else if (event.data.stick.x == SHORT_MAX)
				held_step(ROWS_PER_PAGE);
		}
	}
	if (browser_screen.selected >= browser_screen.count)
		browser_screen.selected = (short)MAX(0, browser_screen.count - 1);
	/* (the widgets behind take nothing while the browser is up) */
	event_manager_flush();
	follow_listings();
}

/* ---------- drawing: the Online Games screen (the overlay, ui_overlay.c) */

/* The screen is drawn as the Xbox's menus are: the title in the menus' title
face with its glow, the band under it over the menus' own background
(ui_widget.c leaves the screen behind undrawn), Xbox panels for the list and
the details, the menus' text faces, and the four prompts along the prompt
bar, the Xbox's buttons drawn by code (ui_overlay_button). */

/* the screen's colors (0xRRGGBBAA): the Xbox menus' blues */
enum
{
	COLOR_TITLE = 0x3D8BFFFF,
	COLOR_TITLE_GLOW = 0x3D8BFFFF,
	/* a row's text, and the selected row's */
	COLOR_ROW = 0x5AAEFFFF,
	COLOR_ROW_GLOW = 0x5AAEFFFF,
	COLOR_ROW_SELECTED = 0xFFFFFFFF,
	COLOR_SELECTION_FILL = 0x2A62C850,
	COLOR_SELECTION_EDGE = 0x6FB2FFFF,
	/* what can't be picked, greyed as the Xbox greys it; a closed game's
	players in a muted warm tint */
	COLOR_ROW_DIM = 0x56698AFF,
	COLOR_CLOSED = 0xB08A6EFF,
	COLOR_HEAD = 0x8FA6C8FF,
	COLOR_TEXT = 0xFFFFFFFF,
	COLOR_DIM = 0x8FA6C8FF,
	COLOR_LABEL = 0x7FB8FFFF,
	COLOR_RULE = 0x2F6DD0A0,
	/* the roster's players of each team, the Xbox's softened red and blue */
	COLOR_RED_TEAM = 0xFF8A80FF,
	COLOR_BLUE_TEAM = 0x8CC4FFFF,
	COLOR_PROMPT = 0x4AA3FFFF,
	/* a prompt that does nothing for the selected game */
	COLOR_PROMPT_OFF = 0x4A5E80FF,
	COLOR_BUTTON_OFF = 0xFFFFFF55,
	/* what keeps the selected game from being joined yet, and a message */
	COLOR_WARN = 0xF0A070FF,
	/* the list under a dialog */
	COLOR_SHADE = 0x02081AA0,
	/* the header's halo over the menus' background */
	COLOR_HALO = 0x000814FF,
};

/* the screen's places, in the 640x480 layout */
enum
{
	/* the band under the title, and the prompt bar */
	BAND_Y = 70, BAR_Y = 409, BAR_BOTTOM = 445,
	LIST_X = 32, LIST_Y = 79, LIST_WIDTH = 576, LIST_HEAD = 21, LIST_ROW = 19, LIST_FOOT = 20,
	DETAIL_Y = 297, DETAIL_HEIGHT = 106,
	/* the roster's places in the details */
	ROSTER_COLUMNS = 2, ROSTER_ROWS = 7,
	COLUMN_TAG = 59, COLUMN_NAME = 76, COLUMN_MAP = 268, COLUMN_TYPE = 400, COLUMN_PLAYERS = 558, COLUMN_PING = 600,
	/* the details' lines: the first's top, and each next's */
	DETAIL_TEXT_X = 250, DETAIL_FIRST_LINE = 23, DETAIL_LINE_STEP = 14,
	/* the pictures' frames, each the shape of its picture (the map's
	140x116; the game type's 140x114, its left 132, whose middle is the
	figure's), in the middle of the details' height */
	PICTURE_HEIGHT = 80, PICTURE_Y = DETAIL_Y + (DETAIL_HEIGHT - PICTURE_HEIGHT) / 2,
	MAP_PICTURE_X = 42, MAP_PICTURE_WIDTH = 97,
	TYPE_PICTURE_X = MAP_PICTURE_X + MAP_PICTURE_WIDTH + 8, TYPE_PICTURE_WIDTH = 93,
	ROSTER_X = 471,
	/* the prompts' right end; the most My Profile's prompt takes with the
	profile's name in it */
	PROMPTS_RIGHT = 608,
	PROFILE_PROMPT_ROOM = 200,
};

/* the list's text sizes: a row's, the column heads' and the footer's */
#define ROW_SIZE 12.0f
#define HEAD_SIZE 8.0f
#define FOOT_SIZE 8.0f
#define DETAIL_SIZE 9.5f
/* the map's family tag (map_family_tag): its text's size */
#define BADGE_SIZE 6.0f
/* the prompt bar's buttons and words, and the smaller prompts' */
#define PROMPT_BUTTON 18.0f
#define PROMPT_TEXT 13.0f
#define SMALL_BUTTON 14.0f
#define SMALL_TEXT 9.5f

/* the map pictures (the menus' mp_map_grafix, in their order:
ui_widget_game_data_input_functions.c) and the game types' (game_type_grafix) */
static char const *const map_picture_order[] =
{
	"beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
	"carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest",
};
static short const engine_picture[] = { 5, 0, 2, 3, 1, 4 };

static char const *const sort_names[NUMBER_OF_SORTS] = { "PLAYERS", "NAME", "MAP", "TYPE", "PING" };

static void utf8_name(
	unsigned short const *name,
	char *text,
	long size)
{
	utf8_text(name, BROWSER_NAME_LENGTH, text, size);
}

/* UTF-16 text, up to length characters, as UTF-8 */
static void utf8_text(
	unsigned short const *name,
	long length,
	char *text,
	long size)
{
	long used = 0, index;

	for (index = 0; index < length && name[index] && used < size - 4; index++)
	{
		unsigned int character = name[index];

		if (character < 0x80)
			text[used++] = (char)character;
		else if (character < 0x800)
		{
			text[used++] = (char)(0xC0 | (character >> 6));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			text[used++] = (char)(0xE0 | (character >> 12));
			text[used++] = (char)(0x80 | ((character >> 6) & 0x3F));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
	}
	text[used] = 0;
}

static char const *type_name(
	struct browser_game const *game,
	char *text,
	long size)
{
	char const *engine;
	static char const *const difficulties[] = { "Easy", "Normal", "Heroic", "Legendary" };
	char file[BROWSER_MAP_LENGTH];

	/* (a campaign level is co-op, whatever variant its host had chosen last) */
	if (map_file(game->map, file, sizeof(file)) == _map_family_xbox && campaign_name(file))
	{
		if (game->difficulty >= 1 && game->difficulty <= NUMBEROF(difficulties))
			snprintf(text, (size_t)size, "Co-op (%s)", difficulties[game->difficulty - 1]);
		else
			snprintf(text, (size_t)size, "Co-op");
		return text;
	}
	/* (OpenCE's game types and Hardcore, by name) */
	engine = network_rules_kind_name(game->kind, game->engine, 1);
	if (engine)
	{
		snprintf(text, (size_t)size, "%s", engine);
		return text;
	}
	engine = game->engine >= 0 && game->engine < NUMBEROF(engine_names) && engine_names[game->engine][0] ?
		engine_names[game->engine] : "New game type";

	/* (Capture the Flag is played in teams alone) */
	snprintf(text, (size_t)size, "%s%s", game->teams && game->engine != 1 ? "Team " : "", engine);
	return text;
}

/* ---------- a listed game's host, as the list says it (docs/delta.md, Delta
List: a ChupathingyCE host's own word; nothing of an older or OpenCE host) */

/* its platform (delta.h's registry), short for the row's tag and in full
for the details */
static char const *const platform_tags[] = { "", "WIN", "MAC", "LNX", "AND", "DECK", "XBOX", "360", "WIIU", "NX",
	"PSP", "VITA", "XBS" };
static char const *const platform_names[] = {
	"", "Windows", "Mac", "Linux", "Android", "Steam Deck", "Xbox", "Xbox 360", "Wii U", "Switch",
	"PSP", "Vita", "Xbox Series",
};

/* the row's tag before the name: the host's platform, SRV for a dedicated
server; "" when the list does not say */
static char const *host_tag(
	struct browser_game const *game)
{
	if (game->hosting == BROWSER_HOSTING_DEDICATED || game->hosting == BROWSER_HOSTING_OFFICIAL)
		return "SRV";
	if (game->has_platform && game->host_platform < NUMBEROF(platform_tags))
		return platform_tags[game->host_platform];
	return "";
}

/* the details' Host line ("Linux, Delta", "Official server (Linux), Delta",
"OpenCE (legacy, network 26)"); NULL when the list does not say */
static char const *host_text(
	struct browser_game const *game,
	char *text,
	long size)
{
	/* (a Delta host that does not say its platform: none named) */
	char const *platform = game->has_platform && game->host_platform > 0 &&
		game->host_platform < NUMBEROF(platform_names) ? platform_names[game->host_platform] : NULL;
	char const *protocol = game->protocol == BROWSER_PROTOCOL_DELTA ? "Delta" :
		game->protocol == BROWSER_PROTOCOL_OPENCE ? "OpenCE (legacy)" : NULL;
	char host[48], with_version[40];

	/* (its network version: an OpenCE host's always, a Delta host's when it
	is not this build's) */
	if (game->protocol == BROWSER_PROTOCOL_OPENCE && game->version)
	{
		snprintf(with_version, sizeof(with_version), "OpenCE (legacy, network %d)", game->version);
		protocol = with_version;
	}
	else if (game->protocol == BROWSER_PROTOCOL_DELTA && game->version && game->version != delta_legacy_announce())
	{
		snprintf(with_version, sizeof(with_version), "Delta, network %d", game->version);
		protocol = with_version;
	}

	if (game->hosting == BROWSER_HOSTING_DEDICATED || game->hosting == BROWSER_HOSTING_OFFICIAL)
	{
		snprintf(host, sizeof(host), "%s%s%s%s", game->hosting == BROWSER_HOSTING_OFFICIAL ? "Official server" :
			"Dedicated", platform ? " (" : "", platform ? platform : "", platform ? ")" : "");
	}
	else
		snprintf(host, sizeof(host), "%s", platform ? platform : "");
	if (!host[0] && !protocol)
		return NULL;
	snprintf(text, (size_t)size, "%s%s%s", host, host[0] && protocol ? ", " : "", protocol ? protocol : "");
	return text;
}

/* the game's machines by platform, as its Delta host counts them ("2 PC,
1 Xbox"; counts only); "" when the list does not say */
static char const *machines_text(
	struct browser_game const *game,
	char *text,
	long size)
{
	/* (the registry's numbers grouped as players say them; the last, those
	without Delta) */
	static struct { char const *name; unsigned char first, last; } const groups[] = {
		{ "PC", 1, 3 }, { "Steam Deck", 5, 5 }, { "Android", 4, 4 }, { "Xbox", 6, 6 }, { "Xbox 360", 7, 7 },
		{ "Wii U", 8, 8 }, { "Switch", 9, 9 }, { "PSP", 10, 10 }, { "Vita", 11, 11 }, { "Xbox Series", 12, 12 },
		{ "other", 0, 0 }, { "OpenCE", BROWSER_MACHINE_KINDS - 1, BROWSER_MACHINE_KINDS - 1 },
	};
	long used = 0, group;

	text[0] = 0;
	for (group = 0; group < (long)NUMBEROF(groups) && used < size - 24; group++)
	{
		int count = 0, kind;

		for (kind = groups[group].first; kind <= groups[group].last; kind++)
			count += game->machines[kind];
		if (count)
			used += snprintf(text + used, (size_t)(size - used), "%s%d %s", used ? ", " : "", count, groups[group].name);
	}
	return text;
}

static void draw_bitmap_picture(struct bitmap_data *bitmap, short art_width, short art_height, short x0, short y0,
	short x1, short y1);

/* a game's picture from the game's own bitmaps, in a cut-out of the overlay:
FALSE (nothing drawn) when the menus' ui.map has none */
static boolean draw_picture(
	char const *tag,
	short frame,
	short art_width,
	short art_height,
	short x0,
	short y0,
	short x1,
	short y1)
{
	long bitmap_index = tag_loaded('bitm', tag);
	struct bitmap_data *bitmap = bitmap_index != NONE ? bitmap_group_get_bitmap_from_sequence(bitmap_index, 0, frame) : NULL;

	if (!bitmap)
		return FALSE;
	draw_bitmap_picture(bitmap, art_width, art_height, x0, y0, x1, y1);
	return TRUE;
}

/* a picture of the overlay's own (the site's: map_art.c) in a frame, its
shape kept and its middle the frame's */
static void draw_art_picture(
	unsigned char const *pixels,
	int width,
	int height,
	int serial,
	float x,
	float y,
	float frame_width,
	float frame_height)
{
	float scale = MIN(frame_width / (float)width, frame_height / (float)height);
	float drawn_width = (float)width * scale, drawn_height = (float)height * scale;

	ui_overlay_rect(x, y, frame_width, frame_height, 0, 0x0A1A33FF);
	ui_overlay_picture(serial, pixels, width, height, x + (frame_width - drawn_width) * 0.5f,
		y + (frame_height - drawn_height) * 0.5f, drawn_width, drawn_height, 0xFFFFFFFF);
}

/* a card in a picture's frame, when there is no picture: the name in white
over a field of the color (0xRRGGBB), in one line or two, and under it the
subtitle (or none) in the color */
static void draw_card(
	char const *title,
	char const *subtitle,
	unsigned long rgb,
	float x,
	float y,
	float width,
	float height)
{
	unsigned int color = (unsigned int)(rgb << 8);
	float room = width - 10.0f;
	float size = 12.0f, sub_size = 7.5f, line_step, top;
	char lines[2][64];
	short line_count = 1, index;

	ui_overlay_rect(x, y, width, height, 0, 0x0A1A33FF);
	ui_overlay_gradient(x, y, width, height, 0, color | 0x78, color | 0x1C);
	snprintf(lines[0], sizeof(lines[0]), "%s", title);
	lines[1][0] = 0;
	/* (too wide: in two lines, broken at the space nearest its middle) */
	if (ui_overlay_text_width(UI_FONT_MENU, size, title) > room && strchr(title, ' '))
	{
		long length = (long)strlen(title), best = NONE, at;

		for (at = 0; at < length; at++)
		{
			if (title[at] == ' ' && (best == NONE || labs(at - length / 2) < labs(best - length / 2)))
				best = at;
		}
		snprintf(lines[0], sizeof(lines[0]), "%.*s", (int)best, title);
		snprintf(lines[1], sizeof(lines[1]), "%s", title + best + 1);
		line_count = 2;
	}
	for (index = 0; index < line_count; index++)
	{
		while (size > 7.0f && ui_overlay_text_width(UI_FONT_MENU, size, lines[index]) > room)
			size -= 0.5f;
	}
	line_step = size * 1.15f;
	top = y + (height - (line_step * line_count + (subtitle ? sub_size + 5.0f : 0.0f))) * 0.5f;
	for (index = 0; index < line_count; index++)
	{
		ui_overlay_text_glow(UI_FONT_MENU, size, x + width * 0.5f, top + index * line_step, UI_ALIGN_CENTER, 0xFFFFFFFF,
			color | 0x90, lines[index]);
	}
	if (subtitle)
	{
		ui_overlay_text(UI_FONT_MENU_SMALL, sub_size, x + width * 0.5f, top + line_count * line_step + 5.0f,
			UI_ALIGN_CENTER, color | 0xFF, subtitle);
	}
}

/* a picture (or none: the box alone), in a cut-out of the overlay */
static void draw_bitmap_picture(
	struct bitmap_data *bitmap,
	short art_width,
	short art_height,
	short x0,
	short y0,
	short x1,
	short y1)
{
	rectangle2d bounds;

	ui_overlay_cutout(x0, y0, (float)(x1 - x0), (float)(y1 - y0));
	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_quad(&bounds, 0xFF0A1A33);
	if (bitmap)
	{
		/* (the picture fills the bitmap's top left; the rest is margin. The
		box is the picture's shape, so that it is drawn whole, its middle in
		the box's: draw_bitmap_in_rect takes a part's size, not its place) */
		rectangle2d art;

		art.x0 = 0;
		art.y0 = 0;
		art.x1 = (short)MIN(art_width, bitmap->width);
		art.y1 = (short)MIN(art_height, bitmap->height);
		draw_bitmap_in_rect(bitmap, &bounds, &art, NULL, 0xFFFFFFFF, NULL, FALSE);
	}
}

/* the game's button for each of the overlay's glyphs (UI_BUTTON_A...) */
static short const glyph_buttons[NUMBER_OF_UI_BUTTONS] =
{
	_gamepad_analog_button_a, _gamepad_analog_button_b, _gamepad_analog_button_x, _gamepad_analog_button_y,
	_gamepad_binary_button_start, _gamepad_analog_button_left_trigger, _gamepad_analog_button_right_trigger,
	_gamepad_analog_button_white, _gamepad_analog_button_black, _gamepad_binary_button_dpad_left,
	_gamepad_binary_button_dpad_right, _gamepad_binary_button_back,
};

/* a place the pointer presses a button (a glyph's: UI_BUTTON_A...), for
the next frame (browser_screen_pointer) */
static void add_target(
	float x0,
	float y0,
	float x1,
	float y1,
	int button)
{
	if (browser_screen.target_count < MAXIMUM_TARGETS)
	{
		browser_screen.targets[browser_screen.target_count].x0 = x0;
		browser_screen.targets[browser_screen.target_count].y0 = y0;
		browser_screen.targets[browser_screen.target_count].x1 = x1;
		browser_screen.targets[browser_screen.target_count].y1 = y1;
		browser_screen.targets[browser_screen.target_count].button = glyph_buttons[button];
		browser_screen.targets[browser_screen.target_count].sort = NONE;
		browser_screen.target_count++;
	}
}

/* a column's head, which puts the list in its order */
static void add_sort_target(
	float x0,
	float y0,
	float x1,
	float y1,
	short sort)
{
	if (browser_screen.target_count < MAXIMUM_TARGETS)
	{
		browser_screen.targets[browser_screen.target_count].x0 = x0;
		browser_screen.targets[browser_screen.target_count].y0 = y0;
		browser_screen.targets[browser_screen.target_count].x1 = x1;
		browser_screen.targets[browser_screen.target_count].y1 = y1;
		browser_screen.targets[browser_screen.target_count].button = NONE;
		browser_screen.targets[browser_screen.target_count].sort = sort;
		browser_screen.target_count++;
	}
}

/* ---------- drawing: the prompts */

static float prompt_width_sized(
	int button,
	char const *words,
	float button_size,
	float text_size)
{
	return ui_overlay_button_width(button, button_size) + 3.0f + ui_overlay_text_width(UI_FONT_MENU, text_size, words);
}

/* a button's prompt (its glyph, then =WORD in the menus' face), x its left
and y its glyph's top; pressed by the pointer from top to bottom and a
little to either side. Its right end */
static float prompt_sized(
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
	add_target(left - 6.0f, top, x + 6.0f, bottom, button);
	return x;
}

/* a row of prompts (count of them) from x, PROMPT_GAP apart: their right
end. on: which do something now (NULL: all) */
#define PROMPT_GAP 14.0f

static float prompts_width(
	short count,
	int const *buttons,
	char const *const *words,
	float button_size,
	float text_size)
{
	float width = 0.0f;
	short index;

	for (index = 0; index < count; index++)
		width += prompt_width_sized(buttons[index], words[index], button_size, text_size) + (index ? PROMPT_GAP : 0.0f);
	return width;
}

static float prompts(
	short count,
	int const *buttons,
	char const *const *words,
	boolean const *on,
	float x,
	float y,
	float button_size,
	float text_size,
	float top,
	float bottom)
{
	short index;

	for (index = 0; index < count; index++)
	{
		x = prompt_sized(buttons[index], words[index], x, y, button_size, text_size, on ? on[index] : TRUE, top,
			bottom) + PROMPT_GAP;
	}
	return x - PROMPT_GAP;
}

/* a line of text with a button's glyph in it, centred on x */
static void text_with_button(
	int font,
	float size,
	float x,
	float y,
	unsigned int color,
	char const *before,
	int button,
	char const *after)
{
	float glyph = size * 1.45f;
	float width = ui_overlay_text_width(font, size, before) + 4 + ui_overlay_button_width(button, glyph) + 4 +
		ui_overlay_text_width(font, size, after);

	x -= width / 2;
	x += ui_overlay_text(font, size, x, y, UI_ALIGN_LEFT, color, before) + 4;
	x += ui_overlay_button(button, glyph, x, y - (glyph - size) * 0.5f, 0xFFFFFFFF) + 4;
	ui_overlay_text(font, size, x, y, UI_ALIGN_LEFT, color, after);
}

/* ---------- drawing: the list's pieces */

/* a game's map's tag (XBX, HCE, HMD, GBX: map_family_tag; a pack's map its
pack's, CE+: ui_map_list_map_tag), by the map's name (NULL: the family's) */
static char const *family_badge_tag(
	short family,
	char const *map)
{
	static char tag[16];
	char title[64];

	if (!map || family != _map_family_pack)
		return map_family_tag(family);
	ui_map_list_map_tag(map, tag, sizeof(tag), title, sizeof(title));
	return tag;
}

/* a map's family tag, size its text's height: its width */
static float family_badge_width(
	short family,
	char const *map,
	float size)
{
	return ui_overlay_text_width(UI_FONT_MENU, size, family_badge_tag(family, map)) + size * 1.1f;
}

/* the tag at x (its left), centred on text whose top is text_y and height
text_size: a thin pill in the family's color */
static void draw_family_badge(
	short family,
	char const *map,
	float size,
	float x,
	float text_y,
	float text_size,
	boolean dim)
{
	unsigned int color = (unsigned int)(map_family_color(family) << 8);
	float height = size * 1.7f;
	float y = text_y + text_size * 0.5f - height * 0.5f;
	float width = family_badge_width(family, map, size);

	ui_overlay_rect(x, y, width, height, height * 0.3f, color | (dim ? 0x0C : 0x1C));
	ui_overlay_outline(x, y, width, height, height * 0.3f, 0.75f, color | (dim ? 0x50 : 0xA8));
	ui_overlay_text(UI_FONT_MENU, size, x + width * 0.5f, y + (height - size) * 0.5f + 0.2f, UI_ALIGN_CENTER,
		color | (dim ? 0x80 : 0xFF), family_badge_tag(family, map));
}

/* a padlock, size high, at x (its left), y (its top): a game with a
password */
static float draw_lock(
	float x,
	float y,
	float size,
	unsigned int color)
{
	float width = size * 0.72f;

	ui_overlay_outline(x + width * 0.2f, y, width * 0.6f, size * 0.7f, width * 0.3f, size * 0.13f, color);
	ui_overlay_rect(x, y + size * 0.42f, width, size * 0.58f, size * 0.1f, color);
	return width;
}

/* the active order's mark beside its column's head: a small arrow down */
static void draw_sort_mark(
	float x,
	float y,
	unsigned int color)
{
	ui_overlay_rect(x, y, 5.0f, 1.0f, 0, color);
	ui_overlay_rect(x + 1.0f, y + 1.0f, 3.0f, 1.0f, 0, color);
	ui_overlay_rect(x + 2.0f, y + 2.0f, 1.0f, 1.0f, 0, color);
}

/* a column's head: white with the mark when the list is in its order; the
pointer puts the list in it (sort NONE: not now, under a dialog) */
static void column_head(
	float x,
	int align,
	char const *text,
	boolean active,
	short sort)
{
	float y = LIST_Y + 7.0f;
	float width = ui_overlay_text(UI_FONT_MENU_SMALL, HEAD_SIZE, x, y, align, active ? 0xFFFFFFFF : COLOR_HEAD, text);

	if (active)
	{
		draw_sort_mark(align == UI_ALIGN_RIGHT ? x + 3.0f : x + width + 3.0f, y + HEAD_SIZE * 0.4f, 0xFFFFFFFF);
	}
	if (sort != NONE)
	{
		/* (the mark's room on both sides, and the head's whole height) */
		float left = align == UI_ALIGN_RIGHT ? x - width : x;

		add_sort_target(left - 4.0f, LIST_Y, left + width + 12.0f, LIST_Y + LIST_HEAD, sort);
	}
}

/* ---------- drawing: the dialogs */

enum
{
	CONNECT_X = 100, CONNECT_Y = 84, CONNECT_WIDTH = 440, CONNECT_HEIGHT = 312,
	CONNECT_QR_X = 382, CONNECT_QR_Y = 150, CONNECT_QR_WIDTH = 138,
	COLOR_CONNECTED = 0x8CE68AFF,
	COLOR_QR_DARK = 0x081020FF,
};

/* a dialog's frame: a lit Xbox panel, its title in the menus' title face
(over the list, shaded: ui_overlay_layer) */
static void dialog_frame(
	float x,
	float y,
	float width,
	float height,
	char const *title)
{
	char capitals[64];
	long index;

	ui_overlay_panel(x, y, width, height, TRUE);
	if (!title)
		return;
	/* (in capitals, as the menus' titles) */
	for (index = 0; title[index] && index < NUMBEROF(capitals) - 1; index++)
		capitals[index] = title[index] >= 'a' && title[index] <= 'z' ? (char)(title[index] - 32) : title[index];
	capitals[index] = 0;
	ui_overlay_text_glow(UI_FONT_TITLE, 11.0f, x + 22, y + 22, UI_ALIGN_LEFT, COLOR_TITLE, COLOR_TITLE_GLOW, capitals);
}

/* the dialog's buttons (count of them), centred along its foot, B before A
as the Xbox's dialogs have them */
static void dialog_prompts(
	short count,
	int const *buttons,
	char const *const *words)
{
	float y = CONNECT_Y + CONNECT_HEIGHT - 34;
	float width = prompts_width(count, buttons, words, PROMPT_BUTTON - 2, PROMPT_TEXT - 1);

	prompts(count, buttons, words, NULL, 320 - width / 2, y, PROMPT_BUTTON - 2, PROMPT_TEXT - 1,
		CONNECT_Y + CONNECT_HEIGHT - 44, CONNECT_Y + CONNECT_HEIGHT);
}

static void dialog_prompt(
	int button,
	char const *words)
{
	dialog_prompts(1, &button, &words);
}

/* the page with the code as a QR code: dark modules on a light square with
its quiet zone, each row's dark runs one rectangle (overlapping a little,
so that no seam shows between them) */
static void draw_qr(
	struct browser_connect const *connect)
{
	short size = (short)connect->qr_size;
	float module = (float)CONNECT_QR_WIDTH / (size + 8);
	short x, y, run;

	ui_overlay_rect(CONNECT_QR_X, CONNECT_QR_Y, CONNECT_QR_WIDTH, CONNECT_QR_WIDTH, 4, 0xFFFFFFFF);
	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x = (short)(x + run))
		{
			for (run = 0; x + run < size && connect->qr[y * size + x + run] == connect->qr[y * size + x]; run++)
				;
			if (connect->qr[y * size + x])
			{
				ui_overlay_rect(CONNECT_QR_X + (x + 4) * module, CONNECT_QR_Y + (y + 4) * module,
					run * module + 0.3f, module + 0.3f, 0, COLOR_QR_DARK);
			}
		}
	}
	ui_overlay_text(UI_FONT_MENU_SMALL, 9.0f, CONNECT_QR_X + CONNECT_QR_WIDTH / 2, CONNECT_QR_Y + CONNECT_QR_WIDTH + 6,
		UI_ALIGN_CENTER, COLOR_DIM, "or scan this code");
}

static void draw_link_profile(
	void)
{
	struct browser_connect const *connect = &browser_screen.connect;
	char text[192];
	float center = CONNECT_X + CONNECT_WIDTH / 2;

	dialog_frame(CONNECT_X, CONNECT_Y, CONNECT_WIDTH, CONNECT_HEIGHT, "LINK PROFILE");

	switch (connect->state)
	{
	case BROWSER_CONNECT_WAITING:
	{
		float left = CONNECT_X + 22;
		float y = CONNECT_Y + 60;

		ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, left, y, UI_ALIGN_LEFT, COLOR_DIM, "On your phone or computer, go to");
		ui_overlay_text_glow(UI_FONT_MENU, 13.0f, left, y + 16, UI_ALIGN_LEFT, COLOR_LABEL, 0x3D8BFF60, connect->page);
		ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, left, y + 38, UI_ALIGN_LEFT, COLOR_DIM, "and enter:");
		ui_overlay_panel(left, y + 56, 238, 62, FALSE);
		ui_overlay_text_glow(UI_FONT_MENU, 34.0f, left + 119, y + 69, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFFFF,
			connect->code);
		snprintf(text, sizeof(text), "Good for %d:%02d", connect->seconds / 60, connect->seconds % 60);
		ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, left, y + 130, UI_ALIGN_LEFT, COLOR_DIM, text);
		ui_overlay_text(UI_FONT_MENU, 10.0f, left, y + 150, UI_ALIGN_LEFT, COLOR_TEXT, "Waiting\xE2\x80\xA6");
		if (connect->qr_size)
			draw_qr(connect);
		dialog_prompt(UI_BUTTON_B, "=CLOSE");
		break;
	}
	case BROWSER_CONNECT_CONFIRM:
		/* (the profile the code was typed for: the player says whether it
		is theirs) */
		if (connect->previous[0])
			snprintf(text, sizeof(text), "Move this game from %s to %s?", connect->previous, connect->handle);
		else
			snprintf(text, sizeof(text), "Connect this game to %s?", connect->handle);
		ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, CONNECT_Y + 96, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFF90, text);
		snprintf(text, sizeof(text), "Not %s? Cancel.", connect->handle);
		ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, center, CONNECT_Y + 130, UI_ALIGN_CENTER, COLOR_DIM, text);
		if (connect->answered)
		{
			ui_overlay_text(UI_FONT_MENU, 11.0f, center, CONNECT_Y + 196, UI_ALIGN_CENTER, COLOR_TEXT,
				"Sending your answer\xE2\x80\xA6");
		}
		else
		{
			static int const buttons[] = { UI_BUTTON_B, UI_BUTTON_A };
			static char const *const words[] = { "=CANCEL", "=CONNECT" };

			snprintf(text, sizeof(text), "%d:%02d to answer", connect->seconds / 60, connect->seconds % 60);
			ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, center, CONNECT_Y + 196, UI_ALIGN_CENTER, COLOR_LABEL, text);
			dialog_prompts(NUMBEROF(buttons), buttons, words);
		}
		break;
	case BROWSER_CONNECT_CONNECTED:
		snprintf(text, sizeof(text), "Connected as %s", connect->handle);
		ui_overlay_text_glow(UI_FONT_MENU, 17.0f, center, CONNECT_Y + 106, UI_ALIGN_CENTER, COLOR_CONNECTED, 0x6BE38A80,
			text);
		dialog_prompt(UI_BUTTON_B, "=DONE");
		break;
	case BROWSER_CONNECT_EXPIRED:
	case BROWSER_CONNECT_DECLINED:
	case BROWSER_CONNECT_FAILED:
	{
		static int const buttons[] = { UI_BUTTON_B, UI_BUTTON_RIGHT_SHOULDER };
		static char const *const words[] = { "=CLOSE", "=NEW CODE" };

		if (connect->state == BROWSER_CONNECT_EXPIRED)
			text_with_button(UI_FONT_MENU, 12.0f, center, CONNECT_Y + 116, COLOR_TEXT, "Code expired. Press",
				UI_BUTTON_RIGHT_SHOULDER, "for a new code.");
		else if (connect->state == BROWSER_CONNECT_DECLINED)
			text_with_button(UI_FONT_MENU, 12.0f, center, CONNECT_Y + 116, COLOR_TEXT, "Cancelled. Press",
				UI_BUTTON_RIGHT_SHOULDER, "for a new code.");
		else
		{
			ui_overlay_text(UI_FONT_MENU, 11.0f, center, CONNECT_Y + 100, UI_ALIGN_CENTER, COLOR_WARN, connect->message);
			text_with_button(UI_FONT_MENU, 12.0f, center, CONNECT_Y + 128, COLOR_TEXT, "Press", UI_BUTTON_RIGHT_SHOULDER,
				"to try again.");
		}
		dialog_prompts(NUMBEROF(buttons), buttons, words);
		break;
	}
	default:
		ui_overlay_text(UI_FONT_MENU, 11.0f, center, CONNECT_Y + 116, UI_ALIGN_CENTER, COLOR_TEXT,
			"Getting a code\xE2\x80\xA6");
		dialog_prompt(UI_BUTTON_B, "=CLOSE");
		break;
	}
}

static void draw_map_offer(
	void)
{
	struct map_download_progress const *progress = &browser_screen.offer_progress;
	float center = CONNECT_X + CONNECT_WIDTH / 2;
	char text[192];

	dialog_frame(CONNECT_X, CONNECT_Y, CONNECT_WIDTH, CONNECT_HEIGHT,
		browser_screen.offer == _offer_stock ? MAP_ARCHIVE_STOCK_TITLE : "MISSING MAP");
	draw_family_badge(_map_family_custom_edition, NULL, BADGE_SIZE + 1.5f,
		CONNECT_X + CONNECT_WIDTH - 22 - family_badge_width(_map_family_custom_edition, NULL, BADGE_SIZE + 1.5f), CONNECT_Y + 20,
		12.0f, FALSE);

	switch (browser_screen.offer)
	{
	case _offer_asking:
	{
		static int const buttons[] = { UI_BUTTON_B, UI_BUTTON_A };
		static char const *const words[] = { "=NO", "=YES" };
		float glyph = 15.0f, box = 10.0f;
		float width, x, y = CONNECT_Y + 196;

		snprintf(text, sizeof(text), "%s plays a map you don't have.", browser_screen.connecting_name);
		/* (the game being joined, or "the game": a sentence's start) */
		if (text[0] >= 'a' && text[0] <= 'z')
			text[0] = (char)(text[0] - 32);
		ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, center, CONNECT_Y + 62, UI_ALIGN_CENTER, COLOR_DIM, text);
		snprintf(text, sizeof(text), "Download %s.map from the", browser_screen.offer_display);
		ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, CONNECT_Y + 86, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFF90, text);
		ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, CONNECT_Y + 106, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFF90,
			"community map repository?");
		/* "Don't ask again", ticked or not (Y, or a click on it) */
		width = ui_overlay_button_width(UI_BUTTON_Y, glyph) + 6 + box + 6 +
			ui_overlay_text_width(UI_FONT_MENU, 11.0f, "DON'T ASK AGAIN");
		x = center - width / 2;
		x += ui_overlay_button(UI_BUTTON_Y, glyph, x, y, 0xFFFFFFFF) + 6;
		ui_overlay_outline(x, y + (glyph - box) / 2, box, box, 2, 1.0f, COLOR_PROMPT);
		if (browser_screen.offer_dont_ask)
			ui_overlay_rect(x + 2.5f, y + (glyph - box) / 2 + 2.5f, box - 5, box - 5, 1, COLOR_PROMPT);
		ui_overlay_text_glow(UI_FONT_MENU, 11.0f, x + box + 6, y + (glyph - 11.0f) / 2 + 0.5f, UI_ALIGN_LEFT,
			COLOR_PROMPT, 0x3D8BFF60, "DON'T ASK AGAIN");
		add_target(center - width / 2 - 6, y - 6, center + width / 2 + 6, y + glyph + 6, UI_BUTTON_Y);
		dialog_prompts(NUMBEROF(buttons), buttons, words);
		break;
	}
	case _offer_downloading:
	{
		float bar_x = CONNECT_X + 40, bar_width = CONNECT_WIDTH - 80;
		float done = 0.0f;

		snprintf(text, sizeof(text), "Downloading %s.map", browser_screen.offer_display);
		ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, CONNECT_Y + 88, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFF90, text);
		switch (progress->phase)
		{
		case MAP_DOWNLOAD_RECEIVING:
			if (progress->total)
			{
				snprintf(text, sizeof(text), "%.1f of %.1f MB", progress->received / 1048576.0,
					progress->total / 1048576.0);
				done = (float)progress->received / (float)progress->total;
			}
			else
				snprintf(text, sizeof(text), "%.1f MB", progress->received / 1048576.0);
			break;
		case MAP_DOWNLOAD_UNPACKING:
			snprintf(text, sizeof(text), "Unpacking\xE2\x80\xA6 %.1f MB", progress->received / 1048576.0);
			done = 1.0f;
			break;
		case MAP_DOWNLOAD_CHECKING:
			snprintf(text, sizeof(text), "Checking\xE2\x80\xA6");
			done = 1.0f;
			break;
		default:
			snprintf(text, sizeof(text), "Connecting to the map repository\xE2\x80\xA6");
			break;
		}
		ui_overlay_rect(bar_x, CONNECT_Y + 126, bar_width, 12, 6, 0x0A1E44FF);
		if (done > 0.0f)
		{
			ui_overlay_gradient(bar_x, CONNECT_Y + 126, bar_width * (done < 1.0f ? done : 1.0f), 12, 6, 0x8FC4FFFF,
				0x2A62C8FF);
		}
		ui_overlay_outline(bar_x, CONNECT_Y + 126, bar_width, 12, 6, 1.0f, 0x4F9DFFFF);
		ui_overlay_text(UI_FONT_MENU, 10.0f, center, CONNECT_Y + 150, UI_ALIGN_CENTER, COLOR_TEXT, text);
		dialog_prompt(UI_BUTTON_B, "=CANCEL");
		break;
	}
	case _offer_stock:
	{
		/* (map_family_stock_need's words, a line at a time) */
		char second[sizeof(browser_screen.offer_stock_second)];
		char *newline;

		snprintf(text, sizeof(text), "%s", browser_screen.offer_stock_first);
		ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, CONNECT_Y + 92, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFF90, text);
		snprintf(second, sizeof(second), "%s", browser_screen.offer_stock_second);
		newline = strchr(second, '\n');
		if (newline)
			*newline++ = 0;
		ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, center, CONNECT_Y + 142, UI_ALIGN_CENTER, COLOR_DIM, second);
		if (newline)
			ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, center, CONNECT_Y + 158, UI_ALIGN_CENTER, COLOR_DIM, newline);
		dialog_prompt(UI_BUTTON_B, "=CLOSE");
		break;
	}
	default:
		snprintf(text, sizeof(text), "Could not download %s.map", browser_screen.offer_display);
		ui_overlay_text_glow(UI_FONT_MENU, 15.0f, center, CONNECT_Y + 92, UI_ALIGN_CENTER, COLOR_WARN, 0xF0A07060, text);
		/* (why, as a sentence) */
		snprintf(text, sizeof(text), "%s.", progress->message[0] ? progress->message : "the repositories did not answer");
		if (text[0] >= 'a' && text[0] <= 'z')
			text[0] = (char)(text[0] - 32);
		ui_overlay_text(UI_FONT_MENU, 10.0f, center, CONNECT_Y + 126, UI_ALIGN_CENTER, COLOR_TEXT, text);
		snprintf(text, sizeof(text), "Or copy %s.map into maps_ce.", browser_screen.offer_display);
		ui_overlay_text(UI_FONT_MENU_SMALL, 9.5f, center, CONNECT_Y + 160, UI_ALIGN_CENTER, COLOR_DIM, text);
		dialog_prompt(UI_BUTTON_B, "=CLOSE");
		break;
	}
}

/* ---------- drawing: the screen */

/* the title, the counts and the header's two prompts */
static void draw_header(
	float left,
	float right)
{
	long players = 0, index;
	char text[160];
	float x, y;

	for (index = 0; index < browser_screen.count; index++)
		players += browser_screen.games[index].players;
	ui_overlay_band(left, right, BAND_Y, BAR_Y);
	/* (the size and place of the Xbox screens' titles, MULTIPLAYER's) */
	ui_overlay_text_glow(UI_FONT_TITLE, 13.5f, 64, 45.0f, UI_ALIGN_LEFT, COLOR_TITLE, COLOR_TITLE_GLOW, "ONLINE GAMES");

	/* servers and players, then the list they come from, at the right (a
	dark halo keeps them clear of a bright background) */
	x = PROMPTS_RIGHT;
	y = 11;
	snprintf(text, sizeof(text), "%ld", players);
	x -= ui_overlay_text_glow(UI_FONT_MENU, 9.5f, x, y, UI_ALIGN_RIGHT, COLOR_TEXT, COLOR_HALO, text) + 3;
	x -= ui_overlay_text_glow(UI_FONT_MENU, 9.5f, x, y, UI_ALIGN_RIGHT, COLOR_HEAD, COLOR_HALO, "PLAYERS") + 10;
	snprintf(text, sizeof(text), "%d", browser_screen.count);
	x -= ui_overlay_text_glow(UI_FONT_MENU, 9.5f, x, y, UI_ALIGN_RIGHT, COLOR_TEXT, COLOR_HALO, text) + 3;
	ui_overlay_text_glow(UI_FONT_MENU, 9.5f, x, y, UI_ALIGN_RIGHT, COLOR_HEAD, COLOR_HALO, "SERVERS");
	/* (the game list's address, in capitals as the rest) */
	browser_server_name(text, sizeof(text));
	for (index = 0; text[index]; index++)
	{
		if (text[index] >= 'a' && text[index] <= 'z')
			text[index] = (char)(text[index] - 32);
	}
	/* (no list: no line; the list itself says why it is empty) */
	if (text[0])
	{
		x = PROMPTS_RIGHT;
		y = 27;
		x -= ui_overlay_text_glow(UI_FONT_MENU, 9.5f, x, y, UI_ALIGN_RIGHT, COLOR_TEXT, COLOR_HALO, text) + 3;
		ui_overlay_text_glow(UI_FONT_MENU, 9.5f, x, y, UI_ALIGN_RIGHT, COLOR_HEAD, COLOR_HALO, "MASTER");
	}

	/* RB and Start, the prompts past the bar's four: Link Profile only while
	this copy is not linked to a profile, and My Profile with its name when
	known and there is room */
	if (browser_screen.offer == _offer_none && !browser_screen.connect_open)
	{
		int buttons[] = { UI_BUTTON_RIGHT_SHOULDER, UI_BUTTON_START };
		char const *words[] = { "=LINK PROFILE", "=MY PROFILE" };
		boolean on[] = { !browser_screen.connecting, TRUE };
		char handle[64], mine[96];
		boolean linked = profile_linked(handle, sizeof(handle));
		short first = linked ? 1 : 0;
		float width;

		if (linked && handle[0])
		{
			snprintf(mine, sizeof(mine), "=MY PROFILE (%s)", handle);
			words[1] = mine;
			if (prompts_width(1, &buttons[1], &words[1], SMALL_BUTTON, SMALL_TEXT) > PROFILE_PROMPT_ROOM)
				words[1] = "=MY PROFILE";
		}
		width = prompts_width((short)(NUMBEROF(buttons) - first), buttons + first, words + first, SMALL_BUTTON,
			SMALL_TEXT);
		prompts((short)(NUMBEROF(buttons) - first), buttons + first, words + first, on + first, PROMPTS_RIGHT - width,
			43, SMALL_BUTTON, SMALL_TEXT, 38, 60);
	}
}

/* a listed game's row (row: its place on the page) */
static void draw_row(
	struct browser_game const *game,
	short row,
	boolean picked)
{
	float y = (float)(LIST_Y + LIST_HEAD + row * LIST_ROW);
	float text_y = y + (LIST_ROW - ROW_SIZE) * 0.5f + 0.5f;
	char text[160], name[64];
	char const *map_name;
	int version_state = browser_game_version_state(game);
	unsigned int color = picked ? COLOR_ROW_SELECTED : game->open && !version_state ? COLOR_ROW : COLOR_ROW_DIM;
	unsigned int glow = picked ? 0x6FB2FFC0 : 0;
	short family = map_family(game->map);
	float badge_x = COLUMN_TYPE - 10 - family_badge_width(family, game->map, BADGE_SIZE);
	float size, width;

	if (picked)
	{
		/* the Xbox's selection: a lit rounded outline over a lighter fill */
		ui_overlay_rect(LIST_X + 5, y, LIST_WIDTH - 10, LIST_ROW, 7, COLOR_SELECTION_FILL);
		ui_overlay_outline(LIST_X + 4, y - 1, LIST_WIDTH - 8, LIST_ROW + 2, 8, 1.0f, 0x6FB2FF40);
		ui_overlay_outline(LIST_X + 5, y, LIST_WIDTH - 10, LIST_ROW, 7, 1.25f, COLOR_SELECTION_EDGE);
	}
	/* (the host's platform, small and dim in the room before the name) */
	if (host_tag(game)[0])
	{
		ui_overlay_text(UI_FONT_MENU, 6.5f, COLUMN_TAG, y + (LIST_ROW - 6.5f) * 0.5f + 0.5f, UI_ALIGN_CENTER,
			picked ? 0xC8D8F0FF : COLOR_HEAD, host_tag(game));
	}
	utf8_name(game->name, name, sizeof(name));
	width = ui_overlay_text_glow(UI_FONT_MENU, ROW_SIZE, COLUMN_NAME, text_y, UI_ALIGN_LEFT, color, glow, name);
	if (game->locked)
	{
		draw_lock(COLUMN_NAME + width + 4, text_y + 1, ROW_SIZE * 0.75f, color);
		width += ROW_SIZE;
	}
	/* (a game this build can't join, by its network version: marked) */
	if (version_state)
	{
		ui_overlay_text(UI_FONT_MENU, 6.5f, COLUMN_NAME + width + 5, y + (LIST_ROW - 6.5f) * 0.5f + 0.5f, UI_ALIGN_LEFT,
			COLOR_CLOSED, version_state < 0 ? "OLD VERSION" : "NEW VERSION");
	}
	/* the map: a name too long for the room left of its family's tag smaller,
	its middle where the others' is */
	map_name = game_map_display_name(game, text, sizeof(text));
	size = ROW_SIZE;
	while (size > 8.0f && ui_overlay_text_width(UI_FONT_MENU, size, map_name) > badge_x - 4 - COLUMN_MAP)
		size -= 0.5f;
	ui_overlay_text_glow(UI_FONT_MENU, size, COLUMN_MAP, text_y + (ROW_SIZE - size) / 2, UI_ALIGN_LEFT, color, glow,
		map_name);
	draw_family_badge(family, game->map, BADGE_SIZE, badge_x, text_y, ROW_SIZE, !game->open && !picked);
	ui_overlay_text_glow(UI_FONT_MENU, ROW_SIZE, COLUMN_TYPE, text_y, UI_ALIGN_LEFT, color, glow,
		type_name(game, text, sizeof(text)));
	snprintf(text, sizeof(text), "%d/%d", game->players, game->maximum_players);
	ui_overlay_text_glow(UI_FONT_MENU, ROW_SIZE, COLUMN_PLAYERS, text_y, UI_ALIGN_RIGHT,
		game->open || picked ? color : COLOR_CLOSED, glow, text);
	/* (the round trip: the tunnel's while it reaches the host, or a ping
	before joining, browser.c; "~" before an estimate, from the host's region;
	nothing where neither is known) */
	if (game->ping >= 0)
	{
		snprintf(text, sizeof(text), game->ping_estimated ? "~%d" : "%d", game->ping);
		ui_overlay_text_glow(UI_FONT_MENU, ROW_SIZE, COLUMN_PING, text_y, UI_ALIGN_RIGHT, color, glow, text);
	}
}

/* the list with no game in it: why, in plain words, and what to do */
static void draw_empty_list(
	void)
{
	float center = LIST_X + LIST_WIDTH / 2;
	float y = LIST_Y + LIST_HEAD + 60;
	char const *title, *line;

	switch (browser_list_status())
	{
	case BROWSER_LIST_NONE:
		title = "No game list.";
		line = "Set network.browser_url in config.toml.";
		break;
	case BROWSER_LIST_FAILED:
		title = "The game list isn't answering.";
		line = "Check your internet connection.";
		break;
	case BROWSER_LIST_WAITING:
		title = "Looking for games\xE2\x80\xA6";
		line = NULL;
		break;
	default:
		ui_overlay_text_glow(UI_FONT_MENU, 14.0f, center, y, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFFA0,
			"No one is hosting right now.");
		text_with_button(UI_FONT_MENU_SMALL, 10.0f, center, y + 24, COLOR_DIM, "Press", UI_BUTTON_Y,
			"to host a game.");
		return;
	}
	ui_overlay_text_glow(UI_FONT_MENU, 14.0f, center, y, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFFA0, title);
	if (line)
		ui_overlay_text(UI_FONT_MENU_SMALL, 10.0f, center, y + 24, UI_ALIGN_CENTER, COLOR_DIM, line);
}

/* the list's footer: the order (or a message, or what the selected game
lacks to be joined), and the page */
static void draw_list_footer(
	struct browser_game const *selected,
	short page_first,
	short page_count)
{
	float y = (float)(LIST_Y + LIST_HEAD + ROWS_PER_PAGE * LIST_ROW) + 3;
	float text_y = y + (LIST_FOOT - FOOT_SIZE) * 0.5f;
	char blocker_text[BROWSER_MAP_LENGTH + 64];
	char const *blocker = !selected ? NULL : version_blocker(selected, blocker_text, sizeof(blocker_text)) ?
		blocker_text : game_type_blocker(selected) ? game_type_blocker(selected) :
		ce_map_blocker(selected, blocker_text, sizeof(blocker_text));
	char text[96];
	float width;

	if (!browser_screen.connecting && browser_screen.status[0] &&
		system_milliseconds() - browser_screen.status_time < STATUS_DURATION)
	{
		ui_overlay_text(UI_FONT_MENU, FOOT_SIZE + 0.5f, LIST_X + 14, text_y - 0.25f, UI_ALIGN_LEFT, COLOR_WARN,
			browser_screen.status);
	}
	else if (blocker)
		ui_overlay_text(UI_FONT_MENU, FOOT_SIZE + 0.5f, LIST_X + 14, text_y - 0.25f, UI_ALIGN_LEFT, COLOR_WARN, blocker);
	else
	{
		/* (RT steps to the next order, and so does the pointer on it) */
		float x = LIST_X + 12;

		x += ui_overlay_button(UI_BUTTON_RIGHT_TRIGGER, SMALL_BUTTON, x, text_y - (SMALL_BUTTON - FOOT_SIZE) / 2,
			0xFFFFFFFF) + 6;
		snprintf(text, sizeof(text), "SORTED BY %s  \xC2\xB7  CLOSED GAMES LAST", sort_names[browser_screen.sort]);
		width = ui_overlay_text(UI_FONT_MENU_SMALL, FOOT_SIZE, x, text_y, UI_ALIGN_LEFT, COLOR_HEAD, text);
		add_target(LIST_X, y, x + width + 10, y + LIST_FOOT, UI_BUTTON_RIGHT_TRIGGER);
	}
	snprintf(text, sizeof(text), "\xE2\x80\xB9  PAGE %d OF %d  \xE2\x80\xBA", page_first / ROWS_PER_PAGE + 1, page_count);
	width = ui_overlay_text_glow(UI_FONT_MENU, FOOT_SIZE, LIST_X + LIST_WIDTH - 14, text_y, UI_ALIGN_RIGHT, COLOR_PROMPT,
		0x3D8BFF50, text);
	/* (the pointer turns the page back on its left half, on with its right) */
	add_target(LIST_X + LIST_WIDTH - 24 - width, y, LIST_X + LIST_WIDTH - 14 - width / 2, y + LIST_FOOT,
		UI_BUTTON_DPAD_LEFT);
	add_target(LIST_X + LIST_WIDTH - 14 - width / 2, y, LIST_X + LIST_WIDTH, y + LIST_FOOT, UI_BUTTON_DPAD_RIGHT);
}

/* the selected game's details: its pictures, its lines and who is in it */
static void draw_details(
	struct browser_game const *selected)
{
	short map_frame = NUMBEROF(map_picture_order);
	char file[BROWSER_MAP_LENGTH], map_name[64], text[160], name[64];
	char const *map_label;
	short family = map_family(selected->map);
	float y = DETAIL_Y + DETAIL_FIRST_LINE;
	float x, line_y;
	long index;

	/* the pictures, in framed insets: the menus' own, else the site's (a
stock map's, while it loads or without one: map_art.c), else a card with
the map's name over its family's color (a custom map's) */
	map_file(selected->map, file, sizeof(file));
	map_label = game_map_display_name(selected, map_name, sizeof(map_name));
	{
		boolean drawn = FALSE;
		char art_path[64];

		if (family == _map_family_xbox)
		{
			for (index = 0; index < NUMBEROF(map_picture_order); index++)
			{
				if (!strcmp(file, map_picture_order[index]))
					map_frame = (short)index;
			}
			/* (the menus' picture of an unknown map is a question mark: none) */
			if (map_frame < NUMBEROF(map_picture_order))
			{
				drawn = draw_picture("ui\\shell\\bitmaps\\mp_map_grafix", map_frame, 140, 116, MAP_PICTURE_X, PICTURE_Y,
					MAP_PICTURE_X + MAP_PICTURE_WIDTH, PICTURE_Y + PICTURE_HEIGHT);
			}
		}
#ifdef HALO_CUSTOM_EDITION
		else
		{
			/* (Halo PC's picture of one of its own maps) */
			struct bitmap_data *bitmap = ui_map_list_family_picture(family, file);

			if (bitmap)
			{
				draw_bitmap_picture(bitmap, 140, 116, MAP_PICTURE_X, PICTURE_Y, MAP_PICTURE_X + MAP_PICTURE_WIDTH,
					PICTURE_Y + PICTURE_HEIGHT);
				drawn = TRUE;
			}
		}
#endif
		/* (a map pack's map has its stock file's name, not its art: a card) */
		if (!drawn && family != _map_family_pack &&
			map_art_site_path(family == _map_family_xbox, file, art_path, (int)sizeof(art_path)))
		{
			unsigned char const *pixels;
			int width, height, serial;

			if (map_art_get(art_path, &pixels, &width, &height, &serial) == MAP_ART_READY)
			{
				draw_art_picture(pixels, width, height, serial, MAP_PICTURE_X, PICTURE_Y, MAP_PICTURE_WIDTH,
					PICTURE_HEIGHT);
				drawn = TRUE;
			}
		}
		if (!drawn)
		{
			draw_card(map_label, map_family_kind(family), map_family_color(family), MAP_PICTURE_X, PICTURE_Y,
				MAP_PICTURE_WIDTH, PICTURE_HEIGHT);
		}
	}
	ui_overlay_outline(MAP_PICTURE_X - 1, PICTURE_Y - 1, MAP_PICTURE_WIDTH + 2, PICTURE_HEIGHT + 2, 2, 1.0f, 0x4F9DFFC0);
	/* (the game type's: a card with its name for one the menus have no
	picture of) */
	if (selected->engine <= 0 || selected->engine >= NUMBEROF(engine_picture) ||
		!draw_picture("ui\\shell\\bitmaps\\game_type_grafix", engine_picture[selected->engine], 132, 114,
			TYPE_PICTURE_X, PICTURE_Y, TYPE_PICTURE_X + TYPE_PICTURE_WIDTH, PICTURE_Y + PICTURE_HEIGHT))
	{
		draw_card(type_name(selected, text, sizeof(text)), NULL, 0x3D8BFF, TYPE_PICTURE_X, PICTURE_Y,
			TYPE_PICTURE_WIDTH, PICTURE_HEIGHT);
	}
	ui_overlay_outline(TYPE_PICTURE_X - 1, PICTURE_Y - 1, TYPE_PICTURE_WIDTH + 2, PICTURE_HEIGHT + 2, 2, 1.0f,
		0x4F9DFFC0);

	utf8_name(selected->name, name, sizeof(name));
	x = DETAIL_TEXT_X + ui_overlay_text_glow(UI_FONT_MENU, 13.0f, DETAIL_TEXT_X, DETAIL_Y + 6, UI_ALIGN_LEFT, 0xFFFFFFFF,
		0x6FB2FFA0, name);
	if (selected->locked)
		draw_lock(x + 5, DETAIL_Y + 8, 10.0f, 0xFFFFFFFF);
#define DETAIL_LINE(label, value) \
		line_y = y; \
		x = DETAIL_TEXT_X + ui_overlay_text(UI_FONT_MENU_SMALL, DETAIL_SIZE, DETAIL_TEXT_X, y, UI_ALIGN_LEFT, COLOR_LABEL, \
			label) + 4; \
		x += ui_overlay_text(UI_FONT_MENU, DETAIL_SIZE, x, y, UI_ALIGN_LEFT, COLOR_TEXT, value); \
		y += DETAIL_LINE_STEP;
	snprintf(text, sizeof(text), "%s%s", browser_game_version_state(selected) ? (browser_game_version_state(selected) < 0 ? "Older version" : "Newer version") :
		selected->open ? "Accepting players" : "In progress",
		selected->locked ? ", password" : "");
	DETAIL_LINE("Status:", text);
	DETAIL_LINE("Map:", map_label);
	/* (its family, spelled out beside its tag) */
	draw_family_badge(family, selected->map, BADGE_SIZE, x + 6, line_y, DETAIL_SIZE, FALSE);
	{
		char tag[16], title[64];

		ui_map_list_map_tag(selected->map, tag, sizeof(tag), title, sizeof(title));
		/* (an Xbox map pack's as its host names it) */
		if (family == _map_family_pack && selected->map_pack[0])
			snprintf(title, sizeof(title), "%s", selected->map_pack);
		ui_overlay_text(UI_FONT_MENU_SMALL, DETAIL_SIZE - 1.0f, x + 10 + family_badge_width(family, selected->map, BADGE_SIZE),
			line_y + 0.5f, UI_ALIGN_LEFT, COLOR_DIM, title);
	}
	type_name(selected, text, sizeof(text));
	if (selected->score_limit)
		snprintf(text + strlen(text), sizeof(text) - strlen(text), " to %d", selected->score_limit);
	DETAIL_LINE("Rules:", text);
	/* (its machines by platform, as its Delta host counts them) */
	snprintf(text, sizeof(text), "%d of %d", selected->players, selected->maximum_players);
	{
		char machines[96];

		if (machines_text(selected, machines, sizeof(machines))[0])
			snprintf(text + strlen(text), sizeof(text) - strlen(text), " (%s)", machines);
	}
	DETAIL_LINE("Players:", text);
	if (host_text(selected, text, sizeof(text)))
	{
		DETAIL_LINE("Host:", text);
	}
#undef DETAIL_LINE
	(void)line_y;

	/* who is in it (the host's roster, when it sends one: two columns of
	seven, the last place saying how many more), behind a thin rule */
	ui_overlay_rect(ROSTER_X - 10, DETAIL_Y + 10, 1.0f, DETAIL_HEIGHT - 20, 0, COLOR_RULE);
	ui_overlay_text_glow(UI_FONT_MENU, 8.5f, ROSTER_X, DETAIL_Y + 9, UI_ALIGN_LEFT, COLOR_LABEL, 0x3D8BFF50, "IN GAME");
	if (!selected->players && !selected->roster_count)
		ui_overlay_text(UI_FONT_MENU_SMALL, 8.5f, ROSTER_X, DETAIL_Y + 24, UI_ALIGN_LEFT, COLOR_DIM, "No one yet");
	else if (!selected->roster_count)
	{
		ui_overlay_text(UI_FONT_MENU_SMALL, 8.5f, ROSTER_X, DETAIL_Y + 24, UI_ALIGN_LEFT, COLOR_DIM,
			"This host doesn't share names");
	}
	else
	{
		long kept = selected->roster_count < BROWSER_LISTED_ROSTER ? selected->roster_count : BROWSER_LISTED_ROSTER;
		long places = ROSTER_COLUMNS * ROSTER_ROWS;
		long shown = selected->roster_count > places ? places - 1 : kept;

		for (index = 0; index < shown; index++)
		{
			struct browser_roster_player const *player = &selected->roster[index];
			unsigned long color = !selected->teams || player->team < 0 ? COLOR_TEXT :
				player->team == 0 ? COLOR_RED_TEAM : COLOR_BLUE_TEAM;

			utf8_name(player->name, name, sizeof(name));
			ui_overlay_text(UI_FONT_MENU, 8.5f, ROSTER_X + (index / ROSTER_ROWS) * 66,
				DETAIL_Y + 24 + (index % ROSTER_ROWS) * 11, UI_ALIGN_LEFT, (unsigned int)color, name);
		}
		if (selected->roster_count > shown)
		{
			snprintf(text, sizeof(text), "+%d more", selected->roster_count - (int)shown);
			ui_overlay_text(UI_FONT_MENU_SMALL, 8.5f, ROSTER_X + (shown / ROSTER_ROWS) * 66,
				DETAIL_Y + 24 + (shown % ROSTER_ROWS) * 11, UI_ALIGN_LEFT, COLOR_DIM, text);
		}
	}
}

/* the prompt bar's four, right to left as the Xbox reads them: A joins, B
backs out, X refreshes, Y makes a game */
static void draw_bar_prompts(
	struct browser_game const *selected)
{
	static int const buttons[] = { UI_BUTTON_Y, UI_BUTTON_X, UI_BUTTON_B, UI_BUTTON_A };
	static char const *const words[] = { "=CREATE GAME", "=REFRESH", "=BACK", "=JOIN" };
	/* (A greyed for a game whose Halo PC map or game type can't be played
	here, and with no game to join) */
	boolean on[] = { TRUE, TRUE, TRUE, selected != NULL && !browser_game_version_state(selected) &&
		!(ce_map_state(selected, FALSE) >= _ce_map_missing && !ce_map_downloadable(selected) && !ce_map_stock(selected)) &&
		!game_type_blocker(selected) };
	short count = selected ? 4 : 3;
	float width = prompts_width(count, buttons, words, PROMPT_BUTTON, PROMPT_TEXT);

	prompts(count, buttons, words, on, PROMPTS_RIGHT - width, BAR_Y + (BAR_BOTTOM - BAR_Y - PROMPT_BUTTON) / 2,
		PROMPT_BUTTON, PROMPT_TEXT, BAR_Y, 480);
}

void browser_screen_render(
	void)
{
	short page_first, page_count, row;
	float margin = (float)((halo_screen_width() - 640) / 2 + 2);
	struct browser_game const *selected = browser_screen.count ? &browser_screen.games[browser_screen.selected] : NULL;
	boolean dialog = browser_screen.connect_open || browser_screen.offer != _offer_none;

	/* (what the pointer may press, as this frame draws it) */
	browser_screen.target_count = 0;
	browser_screen.drawn_first = NONE;
	if (!ui_overlay_available())
		return;

	/* the title and the band, over the menus' own background, its
	widescreen margins too */
	draw_header(-margin, 640 + margin);
	ui_overlay_prompt_bar(-margin, 640 + margin, BAR_Y, BAR_BOTTOM);

	/* the list */
	ui_overlay_panel(LIST_X, LIST_Y, LIST_WIDTH, LIST_HEAD + ROWS_PER_PAGE * LIST_ROW + 3 + LIST_FOOT, FALSE);
	column_head(COLUMN_NAME, UI_ALIGN_LEFT, "SERVER", browser_screen.sort == SORT_NAME, dialog ? NONE : SORT_NAME);
	column_head(COLUMN_MAP, UI_ALIGN_LEFT, "MAP", browser_screen.sort == SORT_MAP, dialog ? NONE : SORT_MAP);
	column_head(COLUMN_TYPE, UI_ALIGN_LEFT, "TYPE", browser_screen.sort == SORT_TYPE, dialog ? NONE : SORT_TYPE);
	column_head(COLUMN_PLAYERS - (browser_screen.sort == SORT_PLAYERS ? 9 : 0), UI_ALIGN_RIGHT, "PLAYERS",
		browser_screen.sort == SORT_PLAYERS, dialog ? NONE : SORT_PLAYERS);
	column_head(COLUMN_PING - (browser_screen.sort == SORT_PING ? 9 : 0), UI_ALIGN_RIGHT, "PING",
		browser_screen.sort == SORT_PING, dialog ? NONE : SORT_PING);
	page_first = (short)(browser_screen.selected - browser_screen.selected % ROWS_PER_PAGE);
	page_count = (short)MAX(1, (browser_screen.count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
	if (!dialog)
		browser_screen.drawn_first = page_first;
	if (!browser_screen.count)
		draw_empty_list();
	for (row = 0; row < ROWS_PER_PAGE && page_first + row < browser_screen.count; row++)
		draw_row(&browser_screen.games[page_first + row], row, page_first + row == browser_screen.selected && !dialog);
	if (!dialog)
		draw_list_footer(selected, page_first, page_count);

	/* the selected game (not under a dialog: its pictures would show through
	it) */
	if (!dialog)
	{
		ui_overlay_panel(LIST_X, DETAIL_Y, LIST_WIDTH, DETAIL_HEIGHT, FALSE);
		if (selected)
			draw_details(selected);
		draw_bar_prompts(selected);
	}
	else
	{
		/* a dialog over the list, shaded, its buttons alone (the bar empties) */
		ui_overlay_layer(1);
		ui_overlay_rect(-margin, BAND_Y + 1, 640 + 2 * margin, BAR_Y - BAND_Y - 2, 0, COLOR_SHADE);
		if (browser_screen.connect_open)
			draw_link_profile();
		else
			draw_map_offer();
		ui_overlay_layer(0);
		return;
	}

	if (browser_screen.connecting && browser_screen.offer == _offer_none)
	{
		char line[160];
		long dots = (long)((system_milliseconds() - browser_screen.connecting_time) / 400 % 4);
		float x;

		ui_overlay_layer(1);
		snprintf(line, sizeof(line), "Connecting to %s%.*s", browser_screen.connecting_name, (int)dots, "...");
		ui_overlay_panel(170, 196, 300, 70, TRUE);
		ui_overlay_text_glow(UI_FONT_MENU, 12.0f, 320, 208, UI_ALIGN_CENTER, 0xFFFFFFFF, 0x3D8BFF90, line);
		x = 320 - prompt_width_sized(UI_BUTTON_B, "=CANCEL", 15.0f, 11.0f) / 2;
		/* (the pointer cancels on the panel's lower half, its prompt's) */
		prompt_sized(UI_BUTTON_B, "=CANCEL", x, 234, 15.0f, 11.0f, TRUE, 230, 266);
		add_target(170, 230, 470, 266, UI_BUTTON_B);
		ui_overlay_layer(0);
	}
}

/* the menus' pointer, each frame while the screen is up (ui_widget.c), in
the menus' 640x480: a click presses the button whose prompt it is on, or
picks the game it is on, or joins that game if it is the one picked; the
wheel (a drag on the touchscreen) steps through the games; the right
button is B */
void browser_screen_pointer(
	struct halo_ui_pointer const *pointer)
{
	short index;

	if (!browser_screen.active)
		return;
	if (pointer->right_clicks)
		press(_gamepad_analog_button_b);
	/* (positive: away from the player, or a finger moving down: back) */
	if (pointer->wheel_steps && !browser_screen.connect_open && browser_screen.offer == _offer_none)
		step((short)-pointer->wheel_steps);
	if (!pointer->left_clicks || !browser_screen.active)
		return;
	/* (debug.touch_targets: each tap, as the menus' are logged) */
	if (config_boolean("debug.touch_targets"))
	{
		for (index = 0; index < browser_screen.target_count; index++)
		{
			if (pointer->click_x >= browser_screen.targets[index].x0 && pointer->click_x < browser_screen.targets[index].x1 &&
				pointer->click_y >= browser_screen.targets[index].y0 && pointer->click_y < browser_screen.targets[index].y1)
				break;
		}
		if (index < browser_screen.target_count)
		{
			platform_log("touch targets: Online Games tap at %d,%d hit %s %d [%.0f,%.0f,%.0f,%.0f]", pointer->click_x,
				pointer->click_y, browser_screen.targets[index].sort == NONE ? "button" : "column",
				browser_screen.targets[index].sort == NONE ? browser_screen.targets[index].button :
				browser_screen.targets[index].sort, browser_screen.targets[index].x0, browser_screen.targets[index].y0,
				browser_screen.targets[index].x1, browser_screen.targets[index].y1);
		}
		else
			platform_log("touch targets: Online Games tap at %d,%d hit no button", pointer->click_x, pointer->click_y);
	}
	for (index = 0; index < browser_screen.target_count; index++)
	{
		if (pointer->click_x >= browser_screen.targets[index].x0 && pointer->click_x < browser_screen.targets[index].x1 &&
			pointer->click_y >= browser_screen.targets[index].y0 && pointer->click_y < browser_screen.targets[index].y1)
		{
			if (browser_screen.targets[index].sort == NONE)
				press(browser_screen.targets[index].button);
			else if (browser_screen.sort != browser_screen.targets[index].sort)
			{
				browser_screen.sort = browser_screen.targets[index].sort;
				fetch_games();
			}
			return;
		}
	}
	/* (not through the panel of a game being joined) */
	if (browser_screen.drawn_first != NONE && !browser_screen.connect_open && !browser_screen.connecting &&
		browser_screen.offer == _offer_none &&
		pointer->click_x >= LIST_X && pointer->click_x < LIST_X + LIST_WIDTH &&
		pointer->click_y >= LIST_Y + LIST_HEAD && pointer->click_y < LIST_Y + LIST_HEAD + ROWS_PER_PAGE * LIST_ROW)
	{
		short game = (short)(browser_screen.drawn_first + (pointer->click_y - LIST_Y - LIST_HEAD) / LIST_ROW);

		if (game == browser_screen.selected)
			press(_gamepad_analog_button_a);
		else if (game < browser_screen.count)
			browser_screen.selected = game;
	}
}

#endif
