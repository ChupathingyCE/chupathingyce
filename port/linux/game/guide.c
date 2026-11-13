/*
GUIDE.C

The Guide (guide.h): one panel over whatever is on screen, the same in the
Xbox's and the Chupathingy menus and over a game in progress.
Its look is the Chupathingy menus' (the Xbox's blues, the plasma under the
menus' items, glow on the selected row and the edges, the prompt bar), its
data social.c's.

The panel: the player's card (their name and what they're doing), then
the sections, switched with LB and RB (Q and E, or a click): FRIENDS,
PARTY, PLAYERS HERE, RECENT, SERVICE RECORD and SETTINGS.
- PLAYERS HERE lists the game's players: their Delta Tag when they share
  their profile (else their game name), their platform's logo when the game
  is on Delta, and a local Mute (the chat log's, chat.c). A player verified
  in this game (the identity capability's roster, delta_peer.h) also gets
  ADD FRIEND (Y); a name alone never does. No address is ever shown.
- SETTINGS has the Guide's own settings (settings_rows.c's rows, which the
  setup screen's Settings shares), then ABOUT: the About screen (about.c),
  which the menus' ABOUT rows open too (guide_about_open).
- FRIENDS is the site's friends (browser.c polls them, social.h): the
  invites, the friends online, the offline ones (a row that shows them),
  the friend requests. A on a row opens its options: a friend's JOIN (the
  site checks, then the game joins its halo://join link), INVITE TO MY GAME
  (in a game on Delta), REMOVE and BLOCK (each pressed twice); an invite's
  JOIN and DISMISS; a request's ACCEPT and DECLINE. Only Delta profiles,
  never an OpenCE player, never an address.
- PARTY is the site's party (browser.c, social.h): its members (LEADER, and
  each one's platform), the open slots with the party's code, then INVITE
  FRIENDS (FRIENDS' INVITE TO PARTY), COPY PARTY LINK (halo://party/CODE),
  BRING PARTY TO MY GAME (delta.bring_party), WHO CAN JOIN (the leader's)
  and LEAVE PARTY. The leader's A on a member: MAKE LEADER and KICK. Out of
  a party: the party invites, START A PARTY and JOIN WITH A CODE (the d-pad
  picks the letters, Y pastes a code or a link). A copy with no Delta
  profile can start one, and join by a code. With COPY PARTY LINK chosen,
  its link shows as a QR code too.
- Following the leader: when the leader joins or hosts a game (with BRING
  PARTY TO MY GAME), each member gets "Following Sarge to Blood Gulch" for
  a few seconds, A to go now and B to stay, then joins through the
  game's halo://join link; or one line why they can't. Each member's copy
  tells the game's host its party (Delta Peer's party_teams), so a host
  that keeps parties together puts them on one team.
- SERVICE RECORD is the first player's own record: this device's
  (service_record.c, kept as their games end) beside their Delta
  profile's, and MEDALS ON HUD (guide_record.c).
- RECENT is the Delta players from this player's last games: the site's
  (its confirmed report lines) and those verified in games this run, newest
  first. A on a row: ADD FRIEND (Y too), BLOCK (pressed twice) and VIEW
  PROFILE (the site's page, /u/HANDLE). Never an OpenCE player.
- CLAN is the profile's clan (browser.c: the profile, its roster asked
  for with the friends): its members by rank with where they are, and for
  the leader and officers the invites out, INVITE FRIENDS (FRIENDS, where
  a friend has INVITE TO CLAN) and A on a member of a lower rank, KICK
  (pressed twice). LEAVE CLAN last (pressed twice). With no clan: the
  clans' invites (JOIN CLAN, DECLINE). Making a clan stays on the site.
- Without a Delta profile, FRIENDS, CLAN and RECENT offer SIGN IN (Delta
  Profile's Link Profile).

It takes the first player's input while open (guide_input.h): the game
gets a controller at rest, and their player stands still
(guide_takes_player). Network games keep running; a game on this machine
alone pauses as it does under the pause menu. B closes it, back to what was
under it as it was.

It opens with controls.delta (F3; on a Mac the backslash key too, as macOS
keeps F3), the first player's Guide button (controls.delta_button, where
SDL reads it), Back held in the menus with delta.hold_back (off by default;
the menus never get a hold's Back), the main menu's and the Multiplayer
menu's DELTA (on PARTY: menu_tags.c, ui_widget.c), or the first player's
pause menu's DELTA (menu_tags.c, delta.pause_menu); the same key or button,
or B, closes it. FRIENDS' INVITE TO PARTY (START PARTY AND INVITE with no
party yet) says what came of it under the options, and marks the friend's
row INVITED, then IN PARTY.

Toasts show for a few seconds (in the menus in the bottom left corner,
clear of their items; in a game low in the middle): a new
invite or friend request, and a friend come online (delta.notify_online,
off by default, menus only). In a game only with delta.notify_in_game. The
Guide's key or button while one shows opens its item. The pause menu's
DELTA counts what waits ("DELTA 2": invites and requests).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "guide.h"

#ifdef HALO_GUIDE

#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "input/input.h"
#include "memory/data.h"
#include "interface/event_manager.h"
#include "interface/interface.h"
#include "interface/ui_widget.h"
#include "bitmaps/bitmap_group.h"
#include "cache/texture_cache.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "saved games/player_profile.h"
#include "interface/player_ui.h"
#include "main/main.h"
#include "bink/bink_playback.h"
#include "../src/about.h"
#include "../src/browser.h"
#include "../src/delta_peer.h"
#include "../src/clan_emblem.h"
#include "../src/delta_profile.h"
#include "../src/discord_activity.h"
#include "../src/guide_input.h"
#include "../src/social.h"
#include "../src/settings_rows.h"
#include "../src/ui_overlay.h"
#include "delta.h"
#ifdef HALO_CAPTURE
#include "../src/capture.h"
#endif
#include "halo_ui_pointer.h"
#include "halo_profile_screen.h"
#include "chat.h"
#include "voice_game.h"
#ifdef HALO_PORT_VOICE_AUDIO
#include "../src/voice.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "../src/delta_wire.h"
#include "../src/delta_chat_game.h"
#endif
#include "../src/delta_input_rules.h"
#include "guide_record.h"
#include "guide_workshop.h"
#include "guide_logs.h"
#include "report_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int config_boolean(char const *name);
long config_integer(char const *name);
int config_menus_retired_pending(void);
void config_menus_retired_seen(void);
char const *config_string(char const *name);
int config_write(char const *name, char const *value);
int config_write_boolean(char const *name, int value);
double config_real(char const *name);
void platform_log(char const *format, ...);
/* delta_peer_game.c's */
int delta_peer_game_on_delta(void);
int delta_peer_player_platform(int player_index);
int delta_peer_player_profile(int player_index, unsigned char *player_id);
/* discord_game.c's */
boolean discord_game_get(struct discord_activity_game *game);
/* virtual_keyboard.c's */
boolean virtual_keyboard_active(void);
/* p2p.c's: this copy's game's invite (hosted, else joined), and an invite
link joined */
int p2p_hosting_invite(char *text, int size);
int p2p_joined_invite(char *text, int size);
int p2p_join_invite(char const *text);
/* menu_tags.c's: the pause menu's DELTA text (NONE: none in this map) */
long pc_menu_pause_guide_label(void);
/* ui_widget.c's: the game paused as a menu pauses it */
void ui_widgets_port_pause_game(boolean pause);
/* the platform layer's clipboard (sdl_platform.c) */
int platform_clipboard_get(char *text, int size);
void platform_clipboard_set(char const *text);
/* delta_peer.c's and delta_peer_game.c's: a platform's registry name, and
this machine's party tag told to its game's host (party_teams) */
char const *delta_peer_platform_name(int platform);
void delta_peer_game_set_party_tag(unsigned char const *tag);
/* browser_screen.c's: a game joined by its link over the menus */
boolean browser_screen_join_link(char const *link, char const *name);
/* the platform layer's (sdl_platform.c) */
void platform_open_url(char const *url);
#ifdef DELTA_PEER_IDENTITY_CARDS
/* delta_peer_game.c's: a player verified in this game (the identity
capability, docs/delta.md "Verified names") */
int delta_peer_game_player_identity(int machine_index, int local_player, struct delta_peer_identity *identity);
#endif

/* ---------- constants */

/* the panel's place, in the menus' 640x480 */
enum
{
	PANEL_X = 48, PANEL_Y = 38, PANEL_WIDTH = 544, PANEL_HEIGHT = 398,
	PANEL_RIGHT = PANEL_X + PANEL_WIDTH,
	INSET = 16,
	CONTENT_LEFT = PANEL_X + INSET,
	CONTENT_RIGHT = PANEL_RIGHT - INSET,
	HEADER_Y = PANEL_Y + 12,
	RULE_Y = PANEL_Y + 54,
	TABS_Y = PANEL_Y + 62,
	TABS_HEIGHT = 22,
	BODY_Y = PANEL_Y + 96,
	PROMPTS_Y = PANEL_Y + PANEL_HEIGHT - 30,
	BODY_BOTTOM = PROMPTS_Y - 8,
	LIST_WIDTH = 262,
	DETAIL_LEFT = CONTENT_LEFT + LIST_WIDTH + 12,
	ROW_HEIGHT = 30,
	ROW_GAP = 4,
	SETTING_HEIGHT = 26,

	HEADING_HEIGHT = 16,
	OPTION_HEIGHT = 24,
	OPTION_GAP = 4,
	/* a refusal's line, shown this long */
	MESSAGE_TIME = 6000,
	/* (as Delta Profile's dialogs: DIALOG_SETTLE) */
	CONFIRM_SETTLE = 400,
	/* the browser thread told where the player is this often */
	WHERE_INTERVAL = 1000,
	/* the game's verified players looked at for RECENT this often */
	RECENT_NOTICE_INTERVAL = 5000,

	/* the toast's place */
	TOAST_WIDTH = 260, TOAST_HEIGHT = 34,
	/* (in a game, low in the middle; in the menus, in the bottom left
	corner, clear of their items, QUIT the lowest, and their prompts) */
	TOAST_GAME_X = 320 - TOAST_WIDTH / 2, TOAST_GAME_Y = 404,
	TOAST_MENU_X = 24, TOAST_MENU_Y = 442,
	TOAST_TIME = 5000,
	TOAST_FADE = 400,

	/* a d-pad held: again after this long, then this often */
	REPEAT_DELAY = 400,
	REPEAT_INTERVAL = 120,

	/* (the plasma's map from ui.map: looked for this many frames, then the
	overlay's own noise) */
	PLASMA_TRIES = 240,

	MAXIMUM_TARGETS = 48,

	/* CLAN: the profile (and its clan) looked at this often while it's
	open or an action is out; the emblem's pixels */
	CLAN_LOOK_INTERVAL = 250,
	CLAN_LOOK_CLOSED = 2000,
	EMBLEM_PIXELS = 64,
};

/* the colors: the theme's (ui_style_color: Halo Blue, Chupathingy Red or
Dashboard Green), and those the same in all */
#define COLOR_PANEL_EDGE ui_style_color(UI_COLOR_PANEL_EDGE)
#define COLOR_LABEL ui_style_color(UI_COLOR_LABEL_SOFT)
#define COLOR_ROW_EDGE ui_style_color(UI_COLOR_ROW_EDGE)
#define COLOR_PROMPT_GLOW ui_style_color(UI_COLOR_PROMPT_GLOW)
#define COLOR_DOT_GAME ui_style_color(UI_COLOR_DOT_GAME)
#define COLOR_DOT_OFFLINE ui_style_color(UI_COLOR_DOT_OFFLINE)
#define COLOR_DOT_ONLINE ui_style_color(UI_COLOR_GOOD)
#define COLOR_DIMMED ui_style_color(UI_COLOR_TEXT_OFF)
#define COLOR_PLAYER ui_style_color(UI_COLOR_PLAYER)
enum
{
	COLOR_VALUE = 0xFFFFFFFF,
	COLOR_DOT_AWAY = 0xE0A53AFF,
	COLOR_MUTED = 0xFF9F9FFF,
};

/* SETTINGS' rows after the settings, signed in: the devices (Delta
Profile's devices dialog) and Link a device (another device's code) */
enum
{
	_device_row_devices,
	_device_row_link,
	NUMBER_OF_DEVICE_ROWS
};

static char const *const device_row_labels[NUMBER_OF_DEVICE_ROWS] =
{
	"DEVICES", "LINK A DEVICE",
};

#ifdef HALO_PORT_VOICE_AUDIO
/* SETTINGS' voice rows (voice_game.c), after the shared settings: its way,
the channel talked on, the voices' volume and each channel's, whether
proximity voices take the game's 3D sound, and who's talking */
enum
{
	_setting_voice_chat,
	_setting_voice_channel,
	_setting_voice_volume,
	_setting_voice_party,
	_setting_voice_team,
	_setting_voice_proximity,
	_setting_voice_everyone,
	_setting_voice_positional,
	_setting_voice_speakers,
	NUMBER_OF_VOICE_SETTINGS
};

static char const *const voice_setting_labels[NUMBER_OF_VOICE_SETTINGS] =
{
	"VOICE CHAT", "VOICE CHANNEL", "VOICE VOLUME", "PARTY VOLUME", "TEAM VOLUME", "PROXIMITY VOLUME",
	"EVERYONE VOLUME", "3D VOICES", "WHO'S TALKING",
};

static char const *const voice_setting_keys[NUMBER_OF_VOICE_SETTINGS] =
{
	"audio.voice_chat", "audio.voice_channel", "audio.voice_volume", "audio.voice_volume_party",
	"audio.voice_volume_team", "audio.voice_volume_proximity", "audio.voice_volume_everyone",
	"audio.voice_positional", "display.voice_speakers",
};
#else
enum { NUMBER_OF_VOICE_SETTINGS = 0 };
#endif

/* what a click or tap lands on */
enum
{
	_target_tab,
	_target_row,
	_target_action,
	_target_setting,
	_target_sign_in,
	_target_close,
	_target_option,
	_target_prompt,
	/* PLAYERS HERE's buttons: MUTE, ADD FRIEND */
	_target_player_action,
	/* WORKSHOP's PREV and NEXT (index 0 and 1), and Steam's sign-in window's button */
	_target_page,
	_target_modal,
	/* SEND MY LOGS' window's buttons (index 0 the left one) */
	_target_logs,
};

/* RECENT's options on a player */
enum
{
	_recent_add,
	_recent_block,
	_recent_view,
	MAXIMUM_RECENT_OPTIONS = 3,
	/* the handles asked to be friends this run (ADD FRIEND) */
	MAXIMUM_ASKED = 32,
	/* PARTY's QR code: its side, in the menus' 640x480 */
	PARTY_QR_WIDTH = 92,
};

/* FRIENDS' items, top to bottom: the invites, the friends online, the
offline friends' row (open: they follow), the requests */
enum
{
	_item_heading,
	_item_invite,
	_item_friend,
	_item_offline,
	_item_request,
	/* a request this player sent (CANCEL), a player they block (UNBLOCK) */
	_item_outgoing,
	_item_blocked,
};

enum
{
	_heading_invites,
	_heading_online,
	_heading_requests,
	_heading_sent,
	_heading_blocked,
};

struct friend_item
{
	short kind;
	/* the invite's, friend's or request's index in the state; a heading's
	_heading_* */
	short index;
};

#define MAXIMUM_FRIEND_ITEMS (6 + SOCIAL_MAXIMUM_INVITES + SOCIAL_MAXIMUM_FRIENDS + 2 * SOCIAL_MAXIMUM_REQUESTS + \
	SOCIAL_MAXIMUM_BLOCKED)

/* what a FRIENDS item's options are */
enum
{
	_option_join,
	_option_invite,
	_option_remove,
	_option_block,
	_option_accept,
	_option_decline,
	_option_dismiss,
	_option_party_invite,
	/* a friend asked into the clan (a leader's or officer's) */
	_option_clan_invite,
	/* CLAN's: a clan's invite answered, a member kicked */
	_option_clan_join,
	_option_clan_decline,
	_option_kick,
	/* a request sent taken back, a player unblocked */
	_option_cancel,
	_option_unblock,
	MAXIMUM_OPTIONS = 6
};

/* CLAN's items, top to bottom: the members, the invites out (a leader's
or officer's), INVITE FRIENDS and LEAVE CLAN; with no clan, the clans'
invites */
enum
{
	_clan_heading,
	_clan_member,
	_clan_invited,
	_clan_invite,
	_clan_action,
};

enum
{
	_clan_heading_members,
	_clan_heading_invited,
	_clan_heading_invites,
};

enum
{
	_clan_action_invite,
	_clan_action_leave,
};

/* guide.armed for LEAVE CLAN's first press (past every _option_*) */
#define ARMED_LEAVE 100

#define MAXIMUM_CLAN_ITEMS (5 + 2 * DELTA_PROFILE_CLAN_MEMBERS + DELTA_PROFILE_CLAN_INVITES)


/* what a toast leads to */
enum
{
	_toast_none,
	_toast_invite,
	_toast_request,
	_toast_friend,
	/* a party invite (its party's ID), and a party's line (shown in a game
	too: following the leader, or why not) */
	_toast_party_invite,
	_toast_party_note,
	/* a clan's invite (its clan's ID): CLAN */
	_toast_clan_invite,
	/* the main menu's note: how to open DELTA (its key opens it) */
	_toast_hint,
	/* the main menu's note, once: the Gearbox menus, which the player's
	config.toml chose, were retired (config_menus_retired_pending) */
	_toast_menus_retired,
};

/* PARTY's items: members (or, out of a party, its invites), then its
buttons, VOICE first (in a party whose game carries party voice) */
enum
{
	_party_item_member,
	_party_item_invite,
	_party_item_voice,
	_party_item_action,
};

enum
{
	_party_action_invite_friends,
	_party_action_copy_link,
	_party_action_bring,
	_party_action_who,
	_party_action_new_code,
	_party_action_follow,
	_party_action_leave,
	_party_action_start,
	_party_action_join_code,
	/* the leader's on a member */
	_party_action_make_leader,
	_party_action_kick,
	MAXIMUM_PARTY_ITEMS = SOCIAL_PARTY_LARGEST + SOCIAL_MAXIMUM_PARTY_INVITES + 8
};

struct party_item
{
	short kind;
	short index;
};

struct guide_target
{
	float x0, y0, x1, y1;
	short kind;
	short index;
};

/* ---------- globals */

static struct
{
	boolean open;
	short section;
	/* the row chosen in each section */
	short row[NUMBER_OF_SOCIAL_SECTIONS];
	/* the first row shown of a long list, and of SETTINGS */
	short first_row;
	short settings_first;
	unsigned int held;
	unsigned int repeat_held;
	unsigned long repeat_time;
	unsigned long opened_time;
	/* it paused the game (a game on this machine alone) */
	boolean paused_game;

	/* PLAYERS HERE: the rows, and each row's player (their machine and game
	name, for the mute) */
	struct social_list players;
	struct social_list players_fresh;
	short machines[SOCIAL_MAXIMUM_ROWS];
	wchar_t names[SOCIAL_MAXIMUM_ROWS][12];
	/* each row's handle when they're verified in this game ("": not) */
	char handles[SOCIAL_MAXIMUM_ROWS][SOCIAL_HANDLE_SIZE];

	/* RECENT: the players verified in games this run, the rows shown
	(those and the site's), the chosen one's options (open, which, BLOCK
	waiting for its second press: NONE for none), the handles asked to be
	friends this run */
	struct social_recent_seen recent_seen;
	struct social_recent recent[SOCIAL_MAXIMUM_RECENT];
	short recent_count;
	boolean recent_options;
	short recent_option;
	short recent_armed;
	char asked[MAXIMUM_ASKED][SOCIAL_HANDLE_SIZE];
	short asked_next;

	/* PARTY's QR code of its link, and the code it was made for */
	char qr_code[SOCIAL_PARTY_CODE_SIZE];
	int qr_size;
	unsigned char qr[BROWSER_CONNECT_QR_SIZE * BROWSER_CONNECT_QR_SIZE];
	/* ... and their index in the game's player list (a report's) */
	short player_lists[SOCIAL_MAXIMUM_ROWS];

	/* the card */
	char name[SOCIAL_TAG_SIZE];
	char status[SOCIAL_STATUS_SIZE];
	unsigned int color;
	boolean linked;

	/* the toast, and when it came */
	char toast_title[96];
	char toast_line[96];
	unsigned long toast_time;
	boolean toast_shown;
	/* (a toast for something the player just did, as a screenshot: shown
	in a game too) */
	boolean toast_always;
	/* (a screenshot's or clip's own, capture.c's: left out of the next
	screenshot) */
	boolean toast_capture;
	boolean debug_toast_done;
	boolean debug_open_done;
	/* debug.delta_test: the next step, and when the last one ran */
	short debug_step;
	unsigned long debug_step_time;
	boolean debug_row_done;
	/* debug.party_test's next step (its steps one after another) */
	short debug_party_step;
	/* the main menu's note counted as shown (it is once drawn) */
	boolean toast_counted;
	/* (the retired Gearbox menus' note, put in line once a run) */
	boolean menus_retired_queued;
	/* the toast's item (an invite's ID, a handle) and its sender's initial */
	short toast_kind;
	char toast_key[SOCIAL_INVITE_ID_SIZE];
	char toast_initial[8];

	/* FRIENDS: the site's state's serial and status (browser.c), and
	whether one loaded this run */
	int friends_serial;
	int friends_status;
	boolean friends_loaded;
	unsigned long where_time;
	/* the items, the offline friends shown, the chosen item (by what it
	is, kept as the list changes) */
	struct friend_item items[MAXIMUM_FRIEND_ITEMS];
	short item_count;
	boolean offline_open;
	short chosen_kind;
	char chosen_key[SOCIAL_INVITE_ID_SIZE];
	/* its options panel: open (the d-pad moves in it), the option chosen,
	one waiting for its second press (REMOVE, BLOCK), NONE for none */
	boolean options;
	short option;
	short armed;
	/* the action out (browser_social_act), and a line about the last */
	boolean acting;
	int action_serial;
	char message[96];
	/* the message is good news (said plainly, not as a warning) */
	boolean message_ok;
	/* FRIENDS' INVITE TO PARTY on its way (the friend's handle), and
	whether a party is being started for it first */
	char party_invite_handle[SOCIAL_HANDLE_SIZE];
	boolean party_invite_starting;
	/* the invites this player sent (INVITED on a friend's row) */
	struct social_sent_list sent;
	/* news waiting for the toast before it to end */
	struct social_toast_queue toasts;
	/* the clans' invites known (their IDs), and whether they were read once */
	int clan_invites_known[DELTA_PROFILE_CLAN_INVITES];
	int clan_invites_known_count;
	boolean clan_invites_read;
	unsigned long message_time;
	/* the invites dismissed (by ID) */
	char dismissed[SOCIAL_MAXIMUM_INVITES][SOCIAL_INVITE_ID_SIZE];
	short dismissed_next;

	/* PARTY: the party as the site said (browser_party_get) and its
	serial; whether this copy was in one; the action out and a line about
	the last; LEAVE PARTY or KICK waiting for its second press (NONE: none);
	the leader's options on a member (its index); the code being picked */
	struct social_party party;
	int party_serial;
	boolean had_party;
	boolean party_acting;
	int party_result_seen;
	char party_message[96];
	unsigned long party_message_time;
	short party_armed;
	short party_member;
	/* that member's ID, to find them again as the party changes */
	char party_member_id[SOCIAL_PARTY_MEMBER_ID_SIZE];
	/* the item chosen, and its row, as last remembered: the row follows
	its item when the list changes under it (party_selection_keep) */
	boolean party_kept;
	short party_kept_row;
	struct social_list_key party_kept_key;
	/* the items as last drawn, row by row: a tap is aimed at what was drawn
	(party_tap_lands) */
	struct social_list_key party_drawn[MAXIMUM_PARTY_ITEMS];
	short party_drawn_count;
	/* (the two above set to NONE, once) */
	boolean party_ready;
	boolean code_entry;
	char code[SOCIAL_PARTY_CODE_SIZE];
	short code_cursor;
	/* the leader's game last told to the party (its invite's code) */
	char go_sent[160];
	/* following the leader: the offer up, since when, its link and line;
	the last go acted on. Or a party's link opened (follow_party: its code),
	asked about the same way with no countdown */
	boolean follow_active;
	unsigned long follow_time;
	char follow_link[SOCIAL_PARTY_LINK_SIZE];
	char follow_line[96];
	long long follow_seen;
	char follow_party[SOCIAL_PARTY_CODE_SIZE];

	/* CLAN: the profile's state (browser_profile_get) and serial, when it
	was last looked at, its items and the chosen one (by what it is), and
	an action out (browser_profile_clan*) */
	int clan_state;
	int clan_serial;
	boolean clan_loaded;
	unsigned long clan_time;
	struct friend_item clan_items[MAXIMUM_CLAN_ITEMS];
	short clan_count;
	short clan_chosen_kind;
	char clan_chosen_key[SOCIAL_TAG_SIZE];
	boolean clan_acting;
	/* the emblem's pixels, made again when it changes */
	unsigned char *emblem_pixels;
	int emblem_serial;

	int plasma_tries;

	/* SIGN IN here (Quick Connect, browser.c's Link Profile): its code
asked for, and what it came to */
	boolean signing_in;
	struct browser_connect connect;
	/* when its question first showed: A only after CONFIRM_SETTLE, so a
	press meant for something else doesn't answer it */
	unsigned long confirm_shown;

	/* the About screen in the panel (from SETTINGS' ABOUT: B goes back
	there; from the menus' ABOUT: B closes it) */
	boolean about;
	boolean about_from_settings;
	struct about_view about_view;

	struct guide_target targets[MAXIMUM_TARGETS];
	short target_count;
} guide;

/* the site's friends: as shown, and as just read (browser_social_get) */
static struct social_state friends_state;
static struct social_state friends_fresh;

static boolean party_act(char const *action, char const *value);
static void party_update(void);
static void party_where(void);
static boolean party_has_leader(void);
static void party_link_offer(void);
static void section_go(short section);
static void recent_build(void);
static void toast_from(short kind, char const *key, char const *title, char const *line, char const *name);

/* CLAN: the Delta profile as browser.c keeps it (its clan, its invites) */
static struct delta_profile clan_profile;

static boolean clan_can_invite(void);
static void clan_act_started(void);
static void debug_steps(void);

/* ---------- private code */

static void target_add(float x0, float y0, float x1, float y1, short kind, short index)
{
	if (guide.target_count >= MAXIMUM_TARGETS)
		return;
	guide.targets[guide.target_count].x0 = x0;
	guide.targets[guide.target_count].y0 = y0;
	guide.targets[guide.target_count].x1 = x1;
	guide.targets[guide.target_count].y1 = y1;
	guide.targets[guide.target_count].kind = kind;
	guide.targets[guide.target_count].index = index;
	guide.target_count++;
}

/* the game's UTF-16 as UTF-8 */
static void utf8_from_wide(char *text, int size, wchar_t const *wide, int length)
{
	int used = 0, index;

	for (index = 0; index < length && wide[index] && used < size - 1; index++)
	{
		unsigned int character = (unsigned short)wide[index];

		if (character < 0x80)
			text[used++] = (char)character;
		else if (character < 0x800)
		{
			if (used + 2 > size - 1)
				break;
			text[used++] = (char)(0xC0 | (character >> 6));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
		else
		{
			if (used + 3 > size - 1)
				break;
			text[used++] = (char)(0xE0 | (character >> 12));
			text[used++] = (char)(0x80 | ((character >> 6) & 0x3F));
			text[used++] = (char)(0x80 | (character & 0x3F));
		}
	}
	text[used] = 0;
}

static unsigned int fade(unsigned int color, float share)
{
	float alpha = (float)(color & 0xFF) * share;

	return (color & 0xFFFFFF00) | (unsigned int)(alpha < 0.0f ? 0.0f : alpha > 255.0f ? 255.0f : alpha);
}

/* a game alone on this machine: the Guide pauses it as the pause menu does */
static boolean pauses_game(void)
{
	return !main_menu_is_active() && game_in_progress() && game_connection() == _game_connection_local;
}

static boolean in_game(void)
{
	return !main_menu_is_active() && game_in_progress();
}

/* whether SIGN IN goes anywhere: Delta Profile's Link Profile */
static short sign_in_section(void)
{
	short section, count;

	if (!profile_screen_available())
		return NONE;
	count = profile_screen_section_count();
	for (section = 0; section < count; section++)
	{
		if (!strcmp(profile_screen_section_name(section), "LINK PROFILE"))
			return section;
	}
	return NONE;
}

/* debug.delta_sample (debug_sample_friends) */
#ifdef HALO_RELEASE
#define debug_sample() FALSE
#else
static boolean debug_sample(void)
{
	return config_boolean("debug.delta_sample");
}
#endif

/* ---------- the card */

static void card_update(void)
{
	struct discord_activity_game game;
	char handle[64];

	guide.linked = debug_sample() || browser_profile_linked(handle, sizeof(handle)) != 0;
	guide.name[0] = 0;
	/* (the profile's name as the site shows it, else its handle) */
	if (guide.linked && guide.friends_loaded && friends_state.handle[0])
		social_copy_text(guide.name, sizeof(guide.name), social_name(friends_state.display, friends_state.handle));
	else if (guide.linked && handle[0])
		social_copy_text(guide.name, sizeof(guide.name), handle);
	guide.color = COLOR_PLAYER;
	if (player_ui_get_active_player_profile_index(0) != NONE)
	{
		struct player_profile profile;
		real_rgb_color color;

		player_ui_get_active_player_profile(0, &profile);
		if (!guide.name[0])
			utf8_from_wide(guide.name, sizeof(guide.name), profile.player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH);
		color = player_profile_get_rgb_color(profile.primary_color_index);
		guide.color = ((unsigned int)(PIN(color.red, 0.0f, 1.0f) * 255.0f) << 24) |
			((unsigned int)(PIN(color.green, 0.0f, 1.0f) * 255.0f) << 16) |
			((unsigned int)(PIN(color.blue, 0.0f, 1.0f) * 255.0f) << 8) | 0xFF;
	}
	/* (no profile chosen, in a game: the first player's name there) */
	if (!guide.name[0] && in_game())
	{
		struct data_iterator iterator;
		struct player_datum *player;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->local_player_index == 0)
			{
				utf8_from_wide(guide.name, sizeof(guide.name), player->name, NUMBEROF(player->name));
				break;
			}
		}
	}
	social_own_status(discord_game_get(&game) ? &game : NULL, guide.status, sizeof(guide.status));
}

/* ---------- PLAYERS HERE: the game's players (none outside a network game) */

/* a player verified in this game (the identity capability's roster):
their handle and the tag their proof says ("" if none); FALSE for anyone
else (an OpenCE player, a name alone, or a build without it) */
static boolean player_verified(struct player_datum const *player, char *handle, char *display)
{
#ifdef DELTA_PEER_IDENTITY_CARDS
	struct delta_peer_identity identity;

	if (delta_peer_game_on_delta() && delta_peer_game_player_identity(player->network_player_data.machine_index,
		player->network_player_data.controller_index, &identity) && social_handle_valid(identity.handle))
	{
		social_copy_text(handle, SOCIAL_HANDLE_SIZE, identity.handle);
		social_copy_text(display, SOCIAL_TAG_SIZE, identity.tag);
		return TRUE;
	}
#else
	(void)player;
#endif
	handle[0] = 0;
	display[0] = 0;
	return FALSE;
}

/* whether this player is a friend of handle's, asked them this run, or is
them */
static boolean handle_is_friend(char const *handle)
{
	short index;

	for (index = 0; index < friends_state.friend_count; index++)
	{
		if (!strcmp(friends_state.friends[index].handle, handle))
			return TRUE;
	}
	return FALSE;
}

static boolean handle_asked(char const *handle)
{
	short index;

	for (index = 0; index < MAXIMUM_ASKED; index++)
	{
		if (guide.asked[index][0] && !strcmp(guide.asked[index], handle))
			return TRUE;
	}
	return FALSE;
}

static boolean handle_is_own(char const *handle)
{
	return friends_state.handle[0] && !strcmp(friends_state.handle, handle);
}

/* ADD FRIEND for a handle: signed in to Delta, not them, not a friend or
asked already */
static boolean can_add_friend(char const *handle)
{
	return handle[0] && guide.linked && guide.friends_loaded && guide.friends_status != BROWSER_SOCIAL_UNLINKED &&
		!handle_is_own(handle) && !handle_is_friend(handle) && !handle_asked(handle);
}

/* the game's verified players noted for RECENT (every few seconds in a
network game, the Guide open or not) */
static void recent_notice(void)
{
	static unsigned long last;
	unsigned long now = system_milliseconds();
	struct data_iterator iterator;
	struct player_datum *player;
	struct discord_activity_game game;
	char handle[SOCIAL_HANDLE_SIZE], display[SOCIAL_TAG_SIZE];
	char const *map = "";
	boolean noted = FALSE;

	if (last && now - last < RECENT_NOTICE_INTERVAL)
		return;
	last = now ? now : 1;
	if (!network_game_get_game() || main_menu_is_active() || !game_in_progress())
		return;
	csmemset(&game, 0, sizeof(game));
	if (discord_game_get(&game))
		map = game.map;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->local_player_index != NONE || player->quit_out_of_game || !player_verified(player, handle, display))
			continue;
		social_recent_note(&guide.recent_seen, handle, display, map, (long long)time(NULL));
		noted = TRUE;
	}
	if (noted)
		recent_build();
}

static void players_update(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	boolean delta = delta_peer_game_on_delta() != 0;
	boolean teams = game_engine_running() && game_engine_has_teams();
	short count = 0;

	social_list_clear(&guide.players_fresh);
	if (!network_game_get_game() || main_menu_is_active() || !game_in_progress())
	{
		social_list_commit(&guide.players, &guide.players_fresh);
		return;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && count < SOCIAL_MAXIMUM_ROWS)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct social_row row;
		wchar_t shown[48];
		char handle[SOCIAL_HANDLE_SIZE], display[SOCIAL_TAG_SIZE];
		boolean you = player->local_player_index != NONE;

		if (player->quit_out_of_game)
			continue;
		csmemset(&row, 0, sizeof(row));
		if (you || !player_verified(player, handle, display))
			handle[0] = 0;
		/* (a shared Delta profile's name, else the game name) */
		if (profile_names_player(absolute, shown, NUMBEROF(shown)))
		{
			utf8_from_wide(row.tag, sizeof(row.tag), shown, NUMBEROF(shown));
			row.verified = TRUE;
		}
		else
			utf8_from_wide(row.tag, sizeof(row.tag), player->name, NUMBEROF(player->name));
		row.platform = delta ? delta_peer_player_platform((int)absolute) : 0;
		if (row.platform < 0)
			row.platform = 0;
		row.state = SOCIAL_STATE_IN_GAME;
		row.key = (int)absolute;
		row.muted = !you && chat_local_muted(player->network_player_data.machine_index, player->name);
		row.actions = you ? 0 : SOCIAL_ACTION_MUTE;
		if (can_add_friend(handle))
			row.actions |= SOCIAL_ACTION_ADD_FRIEND;
		if (voice_game_machine_talking(player->network_player_data.machine_index) && !row.muted)
			snprintf(row.status, sizeof(row.status), "Talking");
		else if (you)
			snprintf(row.status, sizeof(row.status), "You");
		else if (row.muted)
			snprintf(row.status, sizeof(row.status), "Muted");
		else if (teams)
			snprintf(row.status, sizeof(row.status), "%s", player->team_index == 0 ? "Red Team" : "Blue Team");
		if (social_list_add(&guide.players_fresh, &row) >= 0)
		{
			guide.machines[guide.players_fresh.count - 1] = player->network_player_data.machine_index;
			social_copy_text(guide.handles[guide.players_fresh.count - 1], SOCIAL_HANDLE_SIZE, handle);
			guide.player_lists[guide.players_fresh.count - 1] = (short)absolute;
			csmemcpy(guide.names[guide.players_fresh.count - 1], player->name, sizeof(guide.names[0]));
			count++;
		}
	}
	social_list_commit(&guide.players, &guide.players_fresh);
	if (guide.row[SOCIAL_SECTION_PLAYERS_HERE] >= guide.players.count)
		guide.row[SOCIAL_SECTION_PLAYERS_HERE] = (short)MAX(0, guide.players.count - 1);
}

static void friend_act(short action, char const *handle);

/* ADD FRIEND on a row of PLAYERS HERE (a verified player only) */
static void player_add_friend(short row)
{
	if (row < 0 || row >= guide.players.count || !(guide.players.rows[row].actions & SOCIAL_ACTION_ADD_FRIEND))
		return;
	friend_act(BROWSER_SOCIAL_REQUEST, guide.handles[row]);
}

static void player_mute_toggle(short row)
{
	struct social_row const *player;

	if (row < 0 || row >= guide.players.count)
		return;
	player = &guide.players.rows[row];
	if (!(player->actions & SOCIAL_ACTION_MUTE))
		return;
	chat_local_mute(guide.machines[row], guide.names[row], !player->muted);
	players_update();
}

/* a player's options (A on their row): MUTE or UNMUTE, ADD FRIEND for a
verified player, and REPORT where the host takes reports (report_player.c);
their count, and their labels */
enum
{
	_player_option_mute,
	_player_option_add_friend,
	_player_option_report,
	MAXIMUM_PLAYER_OPTIONS = REPORT_PLAYER_MOST_CHOICES,
};

static short player_options(short row, short *kinds, char labels[][REPORT_PLAYER_CHOICE_SIZE])
{
	struct social_row const *player;
	short count = 0;

	if (row < 0 || row >= guide.players.count)
		return 0;
	player = &guide.players.rows[row];
	if (player->actions & SOCIAL_ACTION_MUTE)
	{
		kinds[count] = _player_option_mute;
		snprintf(labels[count++], REPORT_PLAYER_CHOICE_SIZE, "%s", player->muted ? "UNMUTE" : "MUTE");
	}
	if (player->actions & SOCIAL_ACTION_ADD_FRIEND)
	{
		kinds[count] = _player_option_add_friend;
		snprintf(labels[count++], REPORT_PLAYER_CHOICE_SIZE, "ADD FRIEND");
	}
	if ((player->actions & SOCIAL_ACTION_MUTE) && report_player_available(guide.machines[row]))
	{
		kinds[count] = _player_option_report;
		snprintf(labels[count++], REPORT_PLAYER_CHOICE_SIZE, "REPORT");
	}
	return count;
}

/* the choices the detail panel lists now: a report's step's, else the
player's options */
static short player_choices(short row, short *kinds, char labels[][REPORT_PLAYER_CHOICE_SIZE])
{
	if (report_player_step() != _report_step_none)
		return report_player_choices(labels, MAXIMUM_PLAYER_OPTIONS);
	return player_options(row, kinds, labels);
}

/* A on PLAYERS HERE: the choice made in the report, or the option run, or
the player's options shown */
static void players_activate(short row)
{
	short kinds[MAXIMUM_PLAYER_OPTIONS];
	char labels[MAXIMUM_PLAYER_OPTIONS][REPORT_PLAYER_CHOICE_SIZE];
	short count;

	if (report_player_step() != _report_step_none)
	{
		report_player_choose(guide.option);
		guide.option = 0;
		if (report_player_step() == _report_step_none)
			guide.options = FALSE;
		return;
	}
	count = player_options(row, kinds, labels);
	if (!guide.options)
	{
		if (count > 0)
		{
			guide.options = TRUE;
			guide.option = 0;
		}
		return;
	}
	if (guide.option < 0 || guide.option >= count)
		return;
	if (kinds[guide.option] == _player_option_mute)
	{
		guide.options = FALSE;
		player_mute_toggle(row);
		return;
	}
	if (kinds[guide.option] == _player_option_add_friend)
	{
		guide.options = FALSE;
		player_add_friend(row);
		return;
	}
	{
		char name[64];

		snprintf(name, sizeof(name), "%s", guide.players.rows[row].tag);
		report_player_begin(guide.player_lists[row], guide.machines[row], name);
		guide.option = 0;
	}
}

static void message_set(char const *text);

/* ---------- SETTINGS */

/* (settings_rows.h's: the rows the setup screen's Settings shares) */
static short shared_settings_count(void)
{
	short count = 0;

	while (settings_row_at(SETTINGS_GROUP_GUIDE, count))
		count++;
	return count;
}

/* the settings' rows: the shared ones, then voice's */
static short settings_count(void)
{
	return (short)(shared_settings_count() + NUMBER_OF_VOICE_SETTINGS);
}

#ifdef HALO_PORT_VOICE_AUDIO
/* a SETTINGS row's voice setting; NONE for one that isn't voice's */
static short voice_setting_of(short row)
{
	short voice = (short)(row - shared_settings_count());

	return voice >= 0 && voice < NUMBER_OF_VOICE_SETTINGS ? voice : NONE;
}

static char const *const voice_modes[] = { "off", "push_to_talk", "open_mic" };
static char const *const voice_mode_names[] = { "OFF", "PUSH TO TALK", "OPEN MIC" };
/* (the channels in the order the rows show them) */
static int const voice_channels[] =
{
	_delta_voice_channel_party, _delta_voice_channel_team, _delta_voice_channel_proximity,
	_delta_voice_channel_everyone
};

static boolean setting_is_volume(short setting)
{
	return setting >= _setting_voice_volume && setting <= _setting_voice_everyone;
}

/* a voice setting's value as it shows */
static void voice_setting_value(short setting, char *text, int size)
{
	if (setting == _setting_voice_chat)
		snprintf(text, (size_t)size, "%s", voice_mode_names[voice_mode()]);
	else if (setting == _setting_voice_channel)
		snprintf(text, (size_t)size, "%s", voice_game_channel_name(voice_game_channel()));
	else if (setting_is_volume(setting))
	{
		double volume = config_real(voice_setting_keys[setting]);

		if (volume <= 0.0)
			snprintf(text, (size_t)size, "OFF");
		else
			snprintf(text, (size_t)size, "%d%%", (int)(volume * 100.0 + 0.5));
	}
	else
		snprintf(text, (size_t)size, "%s", config_boolean(voice_setting_keys[setting]) ? "ON" : "OFF");
}

/* a voice setting stepped */
static void voice_setting_change(short setting, short direction)
{
	if (setting == _setting_voice_chat)
	{
		short mode = (short)((voice_mode() + (direction < 0 ? 2 : 1)) % 3);

		config_write(voice_setting_keys[setting], voice_modes[mode]);
	}
	else if (setting == _setting_voice_channel)
	{
		/* (every channel, a closed one shown dimmed with why) */
		short count = (short)NUMBEROF(voice_channels), index, at = 0;

		for (index = 0; index < count; index++)
		{
			if (voice_channels[index] == voice_game_channel())
				at = index;
		}
		voice_game_set_channel(voice_channels[(at + (direction < 0 ? count - 1 : 1)) % count]);
	}
	else if (setting_is_volume(setting))
	{
		char value[16];
		double volume = config_real(voice_setting_keys[setting]) + (direction < 0 ? -0.25 : 0.25);

		volume = volume < 0.0 ? 0.0 : volume > 2.0 ? 2.0 : volume;
		snprintf(value, sizeof(value), "%.2f", volume);
		config_write(voice_setting_keys[setting], value);
	}
	else
		config_write_boolean(voice_setting_keys[setting], !config_boolean(voice_setting_keys[setting]));
}
#endif

static void setting_change(short setting, short direction)
{
	struct settings_row const *row = settings_row_at(SETTINGS_GROUP_GUIDE, setting);

#ifdef HALO_PORT_VOICE_AUDIO
	if (voice_setting_of(setting) != NONE)
	{
		voice_setting_change(voice_setting_of(setting), direction);
		return;
	}
#endif
	if (row)
	{
		settings_row_change(row, direction < 0 ? -1 : 1);
		/* (the menus' DELTA rows are put in as the menus load) */
		if (!strcmp(settings_row_key(row), "delta.menu_item"))
			message_set("Takes effect next time the menus load");
	}
}

/* SETTINGS' rows: the settings, then the devices' rows when signed in,
then the logs' (guide_logs.c), then ABOUT */
static short settings_rows(void)
{
	return (short)(settings_count() + (guide.linked && profile_screen_available() ? NUMBER_OF_DEVICE_ROWS : 0) +
		guide_logs_rows() + 1);
}

/* ABOUT's row, the last */
static short about_row(void)
{
	return (short)(settings_rows() - 1);
}

/* SEND MY LOGS' row, the first of the logs' */
static short logs_row(void)
{
	return (short)(about_row() - guide_logs_rows());
}

/* a row after the settings: its label */
static char const *extra_row_label(short row)
{
	if (row == about_row())
		return "ABOUT";
	if (row >= logs_row())
		return guide_logs_row_label((short)(row - logs_row()));
	return device_row_labels[row - settings_count()];
}

/* a device row's A: Delta Profile's dialog, over what was under DELTA */
static void device_row_activate(short row)
{
	guide_close();
	if (!(row == _device_row_devices ? profile_screen_open_devices() : profile_screen_open_link_a_device()))
	{
		guide_open();
		message_set(row == _device_row_devices ? "Your devices aren't loaded yet." :
			"Sign in on this device first.");
	}
}

/* A on a row after the settings: a device row's dialog, or ABOUT's screen */
static void extra_row_activate(short row)
{
	if (row == about_row())
	{
		guide.about = TRUE;
		guide.about_from_settings = TRUE;
		about_reset(&guide.about_view);
	}
	else if (row >= logs_row())
	{
		char const *said = guide_logs_row_activate((short)(row - logs_row()));

		if (said)
			message_set(said);
	}
	else
		device_row_activate((short)(row - settings_count()));
}

/* ---------- SIGN IN: Quick Connect's code, shown here */

static void sign_in_start(void)
{
	browser_connect_start(NULL);
	browser_connect_get(&guide.connect);
	guide.signing_in = TRUE;
}

static void sign_in_stop(void)
{
	if (guide.signing_in)
		browser_connect_stop();
	guide.signing_in = FALSE;
}

/* the section shows SIGN IN (FRIENDS, PARTY, RECENT) */
static boolean signing_in_here(void)
{
	return guide.section != SOCIAL_SECTION_SETTINGS && guide.section != SOCIAL_SECTION_PLAYERS_HERE;
}

/* SIGN IN's A: a code, the player's yes, or a new code after one ran out */
static void sign_in_activate(void)
{
	struct browser_connect const *connect = &guide.connect;

	if (!guide.signing_in)
		sign_in_start();
	else if (connect->state == BROWSER_CONNECT_CONFIRM && !connect->answered)
	{
		if (system_milliseconds() - guide.confirm_shown >= CONFIRM_SETTLE)
			browser_connect_answer(TRUE);
	}
	else if (connect->state == BROWSER_CONNECT_EXPIRED || connect->state == BROWSER_CONNECT_DECLINED ||
		connect->state == BROWSER_CONNECT_FAILED)
	{
		sign_in_start();
	}
	else if (connect->state == BROWSER_CONNECT_CONNECTED)
	{
		sign_in_stop();
		browser_profile_load(TRUE);
	}
}

/* ---------- FRIENDS: the site's friends (browser.c polls them) */

static boolean invite_dismissed(char const *id)
{
	short index;

	for (index = 0; index < SOCIAL_MAXIMUM_INVITES; index++)
	{
		if (guide.dismissed[index][0] && !strcmp(guide.dismissed[index], id))
			return TRUE;
	}
	return FALSE;
}

static short invites_waiting(void)
{
	short index, count = 0;

	for (index = 0; index < friends_state.invite_count; index++)
		count += !invite_dismissed(friends_state.invites[index].id);
	return count;
}

/* signed in to Delta, as the site says */
static boolean friends_signed_in(void)
{
	return guide.linked && guide.friends_status != BROWSER_SOCIAL_UNLINKED;
}

/* what waits: invites not dismissed, and friend requests */
static short friends_waiting(void)
{
	if (!guide.friends_loaded || guide.friends_status == BROWSER_SOCIAL_UNLINKED ||
		guide.friends_status == BROWSER_SOCIAL_NONE)
	{
		return 0;
	}
	return (short)(invites_waiting() + friends_state.request_count + friends_state.party_invite_count);
}

/* an item's key: an invite's ID, a player's handle */
static char const *item_key(struct friend_item const *item)
{
	switch (item->kind)
	{
	case _item_invite: return friends_state.invites[item->index].id;
	case _item_friend: return friends_state.friends[item->index].handle;
	case _item_request: return friends_state.requests[item->index].handle;
	case _item_outgoing: return friends_state.outgoing[item->index].handle;
	case _item_blocked: return friends_state.blocked[item->index];
	default: return "";
	}
}

/* the handle an item's actions go to */
static char const *item_handle(struct friend_item const *item)
{
	return item->kind == _item_invite ? friends_state.invites[item->index].from : item_key(item);
}

static boolean item_selectable(short index)
{
	return index >= 0 && index < guide.item_count && guide.items[index].kind != _item_heading;
}

static void item_add(short kind, short index)
{
	if (guide.item_count >= MAXIMUM_FRIEND_ITEMS)
		return;
	guide.items[guide.item_count].kind = kind;
	guide.items[guide.item_count].index = index;
	guide.item_count++;
}

static void chosen_remember(void)
{
	short row = guide.row[SOCIAL_SECTION_FRIENDS];

	if (row < 0 || row >= guide.item_count)
		return;
	guide.chosen_kind = guide.items[row].kind;
	social_copy_text(guide.chosen_key, sizeof(guide.chosen_key), item_key(&guide.items[row]));
}

/* the items from the state, the chosen one kept where it still is */
static void items_build(void)
{
	short index, online = 0, offline = 0, found = NONE;
	short *row = &guide.row[SOCIAL_SECTION_FRIENDS];

	guide.item_count = 0;
	if (invites_waiting())
	{
		item_add(_item_heading, _heading_invites);
		for (index = 0; index < friends_state.invite_count; index++)
		{
			if (!invite_dismissed(friends_state.invites[index].id))
				item_add(_item_invite, index);
		}
	}
	for (index = 0; index < friends_state.friend_count; index++)
	{
		if (friends_state.friends[index].online)
			online++;
		else
			offline++;
	}
	if (online)
		item_add(_item_heading, _heading_online);
	for (index = 0; index < friends_state.friend_count; index++)
	{
		if (friends_state.friends[index].online)
			item_add(_item_friend, index);
	}
	if (offline)
	{
		item_add(_item_offline, offline);
		for (index = 0; guide.offline_open && index < friends_state.friend_count; index++)
		{
			if (!friends_state.friends[index].online)
				item_add(_item_friend, index);
		}
	}
	if (friends_state.request_count)
	{
		item_add(_item_heading, _heading_requests);
		for (index = 0; index < friends_state.request_count; index++)
			item_add(_item_request, index);
	}
	if (friends_state.outgoing_count)
	{
		item_add(_item_heading, _heading_sent);
		for (index = 0; index < friends_state.outgoing_count; index++)
			item_add(_item_outgoing, index);
	}
	if (friends_state.blocked_count)
	{
		item_add(_item_heading, _heading_blocked);
		for (index = 0; index < friends_state.blocked_count; index++)
			item_add(_item_blocked, index);
	}
	/* (the chosen item, where it is now; else the row where it was) */
	for (index = 0; index < guide.item_count && found == NONE; index++)
	{
		if (item_selectable(index) && guide.items[index].kind == guide.chosen_kind &&
			!strcmp(item_key(&guide.items[index]), guide.chosen_key))
			found = index;
	}
	if (found != NONE)
		*row = found;
	else
	{
		guide.options = FALSE;
		*row = (short)PIN(*row, 0, MAX(0, guide.item_count - 1));
		while (*row > 0 && !item_selectable(*row))
			(*row)--;
		/* (the first row a heading: the row under it) */
		while (*row < guide.item_count - 1 && !item_selectable(*row))
			(*row)++;
		chosen_remember();
	}
}

/* this copy's game can take a friend (in a game on Delta with an invite) */
static boolean can_invite(void)
{
	char invite[160];

	if (!in_game() || !network_game_get_game() || !delta_peer_game_on_delta())
		return FALSE;
	if (!p2p_hosting_invite(invite, sizeof(invite)) && !p2p_joined_invite(invite, sizeof(invite)))
		return FALSE;
	return social_presence_invite_valid(invite) != 0;
}

/* whether a handle's player is in this player's party */
static boolean party_has_handle(char const *handle)
{
	short index;

	for (index = 0; guide.party.has && index < guide.party.member_count; index++)
	{
		if (guide.party.members[index].handle[0] && !strcmp(guide.party.members[index].handle, handle))
			return TRUE;
	}
	return FALSE;
}

/* an item's options (_option_*): their count */
static short item_options(struct friend_item const *item, short *options)
{
	short count = 0;

	switch (item->kind)
	{
	case _item_friend:
		if (friends_state.friends[item->index].playing)
			options[count++] = _option_join;
		if (can_invite())
			options[count++] = _option_invite;
		/* (with no party yet, it starts one first: START PARTY AND INVITE) */
		if ((!guide.party.has || guide.party.member_count < guide.party.largest) &&
			!party_has_handle(friends_state.friends[item->index].handle))
		{
			options[count++] = _option_party_invite;
		}
		if (clan_can_invite())
			options[count++] = _option_clan_invite;
		options[count++] = _option_remove;
		options[count++] = _option_block;
		break;
	case _item_invite:
		options[count++] = _option_join;
		options[count++] = _option_dismiss;
		break;
	case _item_request:
		options[count++] = _option_accept;
		options[count++] = _option_decline;
		break;
	case _item_outgoing:
		options[count++] = _option_cancel;
		break;
	case _item_blocked:
		options[count++] = _option_unblock;
		break;
	}
	return count;
}

static boolean item_has_option(struct friend_item const *item, short option)
{
	short options[MAXIMUM_OPTIONS], count = item_options(item, options), index;

	for (index = 0; index < count; index++)
	{
		if (options[index] == option)
			return TRUE;
	}
	return FALSE;
}

static char const *option_label(short option, boolean armed)
{
	switch (option)
	{
	case _option_join: return "JOIN";
	case _option_invite: return "INVITE TO MY GAME";
	case _option_remove: return armed ? "CONFIRM REMOVE" : "REMOVE";
	case _option_block: return armed ? "CONFIRM BLOCK" : "BLOCK";
	case _option_accept: return "ACCEPT";
	case _option_decline: return "DECLINE";
	case _option_party_invite: return guide.party.has ? "INVITE TO PARTY" : "START PARTY AND INVITE";
	case _option_clan_invite: return "INVITE TO CLAN";
	case _option_clan_join: return "JOIN CLAN";
	case _option_clan_decline: return "DECLINE";
	case _option_kick: return armed ? "CONFIRM KICK" : "KICK";
	case _option_cancel: return "CANCEL REQUEST";
	case _option_unblock: return "UNBLOCK";
	default: return "DISMISS";
	}
}

static struct friend_item const *chosen_item(void)
{
	short row = guide.row[SOCIAL_SECTION_FRIENDS];

	return row >= 0 && row < guide.item_count ? &guide.items[row] : NULL;
}

static void message_set(char const *text)
{
	social_copy_text(guide.message, sizeof(guide.message), text);
	guide.message_time = system_milliseconds();
	guide.message_ok = FALSE;
}

/* good news: "Invite sent to Grif" */
static void message_ok_set(char const *text)
{
	message_set(text);
	guide.message_ok = TRUE;
}

/* the message's look: good news plainly, else a warning */
static short message_style(void)
{
	return guide.message_ok ? UI_TEXT_LINE : UI_TEXT_WARNING;
}

/* a friend's name as their row shows it (their handle when not a friend) */
static char const *friend_name(char const *handle)
{
	short index;

	for (index = 0; index < friends_state.friend_count; index++)
	{
		if (!strcmp(friends_state.friends[index].handle, handle))
			return social_name(friends_state.friends[index].display, friends_state.friends[index].handle);
	}
	return handle;
}

static boolean friend_online(char const *handle)
{
	short index;

	for (index = 0; index < friends_state.friend_count; index++)
	{
		if (!strcmp(friends_state.friends[index].handle, handle))
			return friends_state.friends[index].online;
	}
	return FALSE;
}

#define STILL_SENDING "Still sending the last one. Try again in a moment."

static void friend_act(short action, char const *handle)
{
	struct browser_social_result result;

	if (guide.acting)
	{
		message_set(STILL_SENDING);
		return;
	}
	if (!browser_social_act(action, handle))
	{
		message_set(STILL_SENDING);
		return;
	}
	browser_social_result(&result);
	guide.acting = TRUE;
	guide.action_serial = result.serial;
	guide.message[0] = 0;
}

static void invite_dismiss(char const *id)
{
	social_copy_text(guide.dismissed[guide.dismissed_next], SOCIAL_INVITE_ID_SIZE, id);
	guide.dismissed_next = (short)((guide.dismissed_next + 1) % SOCIAL_MAXIMUM_INVITES);
	if (guide.toast_kind == _toast_invite && !strcmp(guide.toast_key, id))
		guide.toast_shown = FALSE;
	social_toast_forget(&guide.toasts, _toast_invite, id);
	guide.options = FALSE;
	items_build();
}

static void party_invite_send(char const *handle);

/* an option of the chosen item */
static void option_run(short option)
{
	struct friend_item const *item = chosen_item();

	if (!item || !item_has_option(item, option))
		return;
	/* (REMOVE and BLOCK take a second press) */
	if ((option == _option_remove || option == _option_block) && guide.armed != option)
	{
		guide.armed = option;
		return;
	}
	guide.armed = NONE;
	switch (option)
	{
	case _option_join: friend_act(BROWSER_SOCIAL_JOIN, item_handle(item)); break;
	case _option_invite: friend_act(BROWSER_SOCIAL_INVITE, item_handle(item)); break;
	case _option_remove: friend_act(BROWSER_SOCIAL_REMOVE, item_handle(item)); break;
	case _option_block: friend_act(BROWSER_SOCIAL_BLOCK, item_handle(item)); break;
	case _option_accept: friend_act(BROWSER_SOCIAL_ACCEPT, item_handle(item)); break;
	case _option_decline: friend_act(BROWSER_SOCIAL_DECLINE, item_handle(item)); break;
	case _option_cancel: friend_act(BROWSER_SOCIAL_CANCEL, item_handle(item)); break;
	case _option_unblock: friend_act(BROWSER_SOCIAL_UNBLOCK, item_handle(item)); break;
	case _option_dismiss: invite_dismiss(item_key(item)); break;
	case _option_party_invite: party_invite_send(item_handle(item)); guide.options = FALSE; break;
	case _option_clan_invite:
		if (!guide.clan_acting && browser_profile_clan_member("invite", item_handle(item)))
			clan_act_started();
		break;
	}
}

/* A on FRIENDS: the chosen item's options, or the offline friends shown */
static void friends_activate(void)
{
	struct friend_item const *item = chosen_item();
	short options[MAXIMUM_OPTIONS], count;

	if (!item)
		return;
	if (guide.options)
	{
		count = item_options(item, options);
		if (guide.option >= 0 && guide.option < count)
			option_run(options[guide.option]);
		return;
	}
	if (item->kind == _item_offline)
	{
		guide.offline_open = !guide.offline_open;
		items_build();
		return;
	}
	if (item_options(item, options) > 0)
	{
		guide.options = TRUE;
		guide.option = 0;
		guide.armed = NONE;
	}
}

static void friends_move(short step)
{
	short *row = &guide.row[SOCIAL_SECTION_FRIENDS];
	short next = *row;
	struct friend_item const *item = chosen_item();

	if (guide.options && item)
	{
		short options[MAXIMUM_OPTIONS], count = item_options(item, options);

		guide.option = (short)PIN(guide.option + step, 0, MAX(0, count - 1));
		guide.armed = NONE;
		return;
	}
	do
		next = (short)(next + step);
	while (next > 0 && next < guide.item_count && !item_selectable(next));
	if (item_selectable(next))
	{
		*row = next;
		guide.armed = NONE;
		guide.message[0] = 0;
		chosen_remember();
	}
}

/* the friends' name a toast shows (none in streamer mode) */
static void toast_from(short kind, char const *key, char const *title, char const *line, char const *name)
{
	social_copy_text(guide.toast_title, sizeof(guide.toast_title), title);
	social_copy_text(guide.toast_line, sizeof(guide.toast_line), line);
	guide.toast_initial[0] = 0;
	if (name && name[0] && !(name[0] & 0x80))
	{
		guide.toast_initial[0] = (char)(name[0] >= 'a' && name[0] <= 'z' ? name[0] - 'a' + 'A' : name[0]);
		guide.toast_initial[1] = 0;
	}
	guide.toast_kind = kind;
	guide.toast_counted = FALSE;
	social_copy_text(guide.toast_key, sizeof(guide.toast_key), key);
	guide.toast_time = system_milliseconds();
	guide.toast_shown = TRUE;
	guide.toast_always = FALSE;
	guide.toast_capture = FALSE;
}

/* the first party invite in fresh that was not in the state shown (-1:
none) */
static short party_invite_news(void)
{
	short index, old;

	for (index = 0; index < friends_fresh.party_invite_count; index++)
	{
		boolean seen = FALSE;

		for (old = 0; old < friends_state.party_invite_count && !seen; old++)
			seen = !strcmp(friends_state.party_invites[old].party, friends_fresh.party_invites[index].party);
		if (!seen)
			return index;
	}
	return NONE;
}

/* the toasts for what is new: an invite, a party invite, a request (not
those already waiting at the first state), a friend online
(delta.notify_online, menus) */
static void friends_news(boolean first)
{
	struct social_news news;
	boolean streamer = config_boolean("chat.streamer_mode") != 0;
	char title[96], line[96];
	short party_invite = first ? NONE : party_invite_news();

	social_news(&friends_state, &friends_fresh, &news);
	/* (each piece of news queued: all of one poll's show, one after
	another, social_toast_push) */
	if (news.invite >= 0 && !invite_dismissed(friends_fresh.invites[news.invite].id))
	{
		struct social_invite const *invite = &friends_fresh.invites[news.invite];
		char const *name = social_name(invite->display, invite->from);

		if (streamer)
			snprintf(title, sizeof(title), "Game invite");
		else
			snprintf(title, sizeof(title), "%s invited you", name);
		if (social_game_line(&invite->game, line, sizeof(line)) < 0)
			snprintf(line, sizeof(line), "Game invite");
		social_toast_push(&guide.toasts, _toast_invite, invite->id, title, line, streamer ? "" : name);
		platform_log("DELTA: a new invite");
	}
	if (party_invite != NONE)
	{
		struct social_party_invite const *invite = &friends_fresh.party_invites[party_invite];
		char const *name = social_name(invite->display, invite->from);

		if (streamer)
			snprintf(title, sizeof(title), "Party invite");
		else
			snprintf(title, sizeof(title), "%s invited you", name);
		snprintf(line, sizeof(line), "Party of %d", invite->size);
		social_toast_push(&guide.toasts, _toast_party_invite, invite->party, title, line, streamer ? "" : name);
		platform_log("DELTA: a new party invite");
	}
	if (news.request >= 0 && !first)
	{
		struct social_request const *request = &friends_fresh.requests[news.request];
		char const *name = social_name(request->display, request->handle);

		social_toast_push(&guide.toasts, _toast_request, request->handle, streamer ? "Friend request" : name,
			streamer ? "" : "Friend request", streamer ? "" : name);
		platform_log("DELTA: a new friend request");
	}
	if (news.online >= 0 && !first && !in_game() && config_boolean("delta.notify_online"))
	{
		struct social_friend const *buddy = &friends_fresh.friends[news.online];
		char const *name = social_name(buddy->display, buddy->handle);

		social_friend_status(buddy, line, sizeof(line));
		social_toast_push(&guide.toasts, _toast_friend, buddy->handle, streamer ? "Friend online" : name, line,
			streamer ? "" : name);
	}
}

/* debug.delta_sample: signed in, with made-up friends (one in a game, one
online, one offline) and a party of two with its code, nothing asked of
the site, for screenshots of the signed-in look. Debug builds only */
static void debug_sample_friends(struct social_state *state)
{
	static char const *const handles[] = { "sarge", "grif", "simmons" };
	static char const *const names[] = { "Sarge", "Grif", "Simmons" };
	short index;

	csmemset(state, 0, sizeof(*state));
	social_copy_text(state->handle, sizeof(state->handle), "caboose");
	social_copy_text(state->display, sizeof(state->display), "Caboose");
	state->friend_count = 3;
	for (index = 0; index < 3; index++)
	{
		struct social_friend *buddy = &state->friends[index];

		social_copy_text(buddy->handle, sizeof(buddy->handle), handles[index]);
		social_copy_text(buddy->display, sizeof(buddy->display), names[index]);
		buddy->online = index < 2;
		buddy->playing = index == 0;
		buddy->joinable = index == 0;
		if (index == 0)
		{
			buddy->game.listed = TRUE;
			social_copy_text(buddy->game.name, sizeof(buddy->game.name), "Red Base");
			social_copy_text(buddy->game.map, sizeof(buddy->game.map), "bloodgulch");
			buddy->game.engine = 1;
			buddy->game.teams = TRUE;
			buddy->game.players = 6;
			buddy->game.maximum_players = 16;
		}
	}
}

static void debug_sample_party(struct social_party *party)
{
	csmemset(party, 0, sizeof(*party));
	party->has = TRUE;
	social_copy_text(party->id, sizeof(party->id), "sample");
	social_copy_text(party->code, sizeof(party->code), "RVB234");
	party->leader = TRUE;
	social_copy_text(party->who_can_join, sizeof(party->who_can_join), "friends");
	party->largest = 4;
	party->member_count = 2;
	social_copy_text(party->members[0].id, sizeof(party->members[0].id), "1");
	social_copy_text(party->members[0].name, sizeof(party->members[0].name), "Caboose");
	social_copy_text(party->members[0].handle, sizeof(party->members[0].handle), "caboose");
	social_copy_text(party->members[0].platform, sizeof(party->members[0].platform), "pc_macos");
	party->members[0].leader = TRUE;
	party->members[0].you = TRUE;
	social_copy_text(party->members[1].id, sizeof(party->members[1].id), "2");
	social_copy_text(party->members[1].name, sizeof(party->members[1].name), "Sarge");
	social_copy_text(party->members[1].handle, sizeof(party->members[1].handle), "sarge");
	social_copy_text(party->members[1].platform, sizeof(party->members[1].platform), "pc_windows");
}

/* each frame: where the player is, told to the browser thread; a new
state read, its toasts; an action's answer */
static void friends_update(void)
{
	unsigned long now = system_milliseconds();
	int serial = guide.friends_serial;
	struct browser_social_result result;

	if (!guide.where_time || now - guide.where_time >= WHERE_INTERVAL)
	{
		guide.where_time = now ? now : 1;
		browser_social_where(in_game(), guide.open);
		party_where();
		party_link_offer();
	}
	party_update();
	if (debug_sample())
	{
		guide.friends_status = BROWSER_SOCIAL_LOADED;
		if (!guide.friends_loaded)
		{
			debug_sample_friends(&friends_fresh);
			serial = guide.friends_serial + 1;
		}
	}
	else
		guide.friends_status = browser_social_get(&friends_fresh, &serial);
	if (serial != guide.friends_serial)
	{
		boolean first = !guide.friends_loaded;

		guide.friends_serial = serial;
		if (guide.friends_status == BROWSER_SOCIAL_LOADED)
		{
			friends_news(first);
			guide.friends_loaded = TRUE;
		}
		else if (guide.friends_status == BROWSER_SOCIAL_NONE || guide.friends_status == BROWSER_SOCIAL_UNLINKED)
			guide.friends_loaded = FALSE;
		friends_state = friends_fresh;
		items_build();
		recent_build();
	}
	if (!guide.acting)
		return;
	browser_social_result(&result);
	if (result.serial != guide.action_serial || result.state == BROWSER_SOCIAL_BUSY)
		return;
	guide.acting = FALSE;
	if (result.state == BROWSER_SOCIAL_REFUSED)
	{
		message_set(result.message);
		return;
	}
	if (result.action == BROWSER_SOCIAL_JOIN)
	{
		char link[192];

		if (browser_social_take_join(link, sizeof(link)))
		{
			/* (as any invite link: Internet play joins it) */
			guide_close();
			platform_log("DELTA: joining a friend's game");
			p2p_join_invite(link);
		}
		return;
	}
	if (result.action == BROWSER_SOCIAL_INVITE)
	{
		char line[128];

		/* (INVITED on their row while the site keeps it) */
		social_sent_add(&guide.sent, result.handle, SOCIAL_SENT_GAME, system_milliseconds());
		social_sent_line(line, sizeof(line), SOCIAL_SENT_GAME, friend_name(result.handle), friend_online(result.handle), "");
		message_ok_set(line);
		guide.options = FALSE;
	}
	else if (result.action == BROWSER_SOCIAL_REQUEST)
	{
		char line[128];

		/* (asked: no ADD FRIEND for them again this run) */
		social_copy_text(guide.asked[guide.asked_next], SOCIAL_HANDLE_SIZE, result.handle);
		guide.asked_next = (short)((guide.asked_next + 1) % MAXIMUM_ASKED);
		guide.recent_options = FALSE;
		social_sent_line(line, sizeof(line), SOCIAL_SENT_REQUEST, result.handle, 1, "");
		message_ok_set(line);
		recent_build();
	}
	else
	{
		if (result.action == BROWSER_SOCIAL_CANCEL || result.action == BROWSER_SOCIAL_UNBLOCK)
		{
			char line[128];

			snprintf(line, sizeof(line), result.action == BROWSER_SOCIAL_CANCEL ? "Request to %s taken back" :
				"%s unblocked", result.handle);
			message_ok_set(line);
		}
		if (result.action == BROWSER_SOCIAL_BLOCK)
		{
			social_recent_forget(&guide.recent_seen, result.handle);
			guide.recent_options = FALSE;
			recent_build();
		}
		guide.options = FALSE;
	}
}

/* the toast's item chosen on FRIENDS, its options open */
static boolean toast_go(void)
{
	short index;
	short kind = guide.toast_kind == _toast_invite ? _item_invite : guide.toast_kind == _toast_request ? _item_request :
		guide.toast_kind == _toast_friend ? _item_friend : NONE;

	if (guide.toast_kind == _toast_hint || guide.toast_kind == _toast_menus_retired)
	{
		guide.toast_shown = FALSE;
		return TRUE;
	}
	if (guide.toast_kind == _toast_clan_invite)
	{
		guide.section = SOCIAL_SECTION_CLAN;
		guide.first_row = 0;
		guide.toast_shown = FALSE;
		return TRUE;
	}
	if (guide.toast_kind == _toast_party_invite || guide.toast_kind == _toast_party_note)
	{
		guide.section = SOCIAL_SECTION_PARTY;
		guide.first_row = 0;
		guide.row[SOCIAL_SECTION_PARTY] = 0;
		for (index = 0; guide.toast_kind == _toast_party_invite && index < friends_state.party_invite_count; index++)
		{
			if (!strcmp(friends_state.party_invites[index].party, guide.toast_key))
				guide.row[SOCIAL_SECTION_PARTY] = index;
		}
		guide.toast_shown = FALSE;
		return TRUE;
	}
	if (kind == NONE)
		return FALSE;
	guide.section = SOCIAL_SECTION_FRIENDS;
	guide.first_row = 0;
	for (index = 0; index < guide.item_count; index++)
	{
		if (guide.items[index].kind == kind && !strcmp(item_key(&guide.items[index]), guide.toast_key))
		{
			guide.row[SOCIAL_SECTION_FRIENDS] = index;
			chosen_remember();
			guide.options = kind != _item_friend;
			guide.option = 0;
			guide.armed = NONE;
			break;
		}
	}
	guide.toast_shown = FALSE;
	return TRUE;
}

/* ---------- RECENT: the Delta players of this player's last games */

/* the rows from the site's list and this run's, the chosen one kept */
static void recent_build(void)
{
	short index, *row = &guide.row[SOCIAL_SECTION_RECENT];
	char chosen[SOCIAL_HANDLE_SIZE];

	chosen[0] = 0;
	if (*row >= 0 && *row < guide.recent_count)
		social_copy_text(chosen, sizeof(chosen), guide.recent[*row].handle);
	guide.recent_count = (short)social_recent_merge(&friends_state, &guide.recent_seen, friends_state.handle,
		guide.recent, SOCIAL_MAXIMUM_RECENT);
	for (index = 0; index < guide.recent_count; index++)
	{
		if (handle_asked(guide.recent[index].handle))
			guide.recent[index].requested = TRUE;
		if (handle_is_friend(guide.recent[index].handle))
			guide.recent[index].is_friend = TRUE;
		if (chosen[0] && !strcmp(guide.recent[index].handle, chosen))
			*row = index;
	}
	if (*row >= guide.recent_count)
	{
		*row = (short)MAX(0, guide.recent_count - 1);
		guide.recent_options = FALSE;
	}
}

static struct social_recent const *recent_chosen(void)
{
	short row = guide.row[SOCIAL_SECTION_RECENT];

	return row >= 0 && row < guide.recent_count ? &guide.recent[row] : NULL;
}

/* a recent player's options (_recent_*): their count */
static short recent_option_list(struct social_recent const *player, short *options)
{
	short count = 0;

	if (!player->is_friend && !player->requested && can_add_friend(player->handle))
		options[count++] = _recent_add;
	options[count++] = _recent_block;
	options[count++] = _recent_view;
	return count;
}

static char const *recent_option_label(short option, boolean armed)
{
	switch (option)
	{
	case _recent_add: return "ADD FRIEND";
	case _recent_block: return armed ? "CONFIRM BLOCK" : "BLOCK";
	default: return "VIEW PROFILE";
	}
}

/* the site's page of a profile (/u/HANDLE), in the system's browser */
static void profile_page_open(char const *handle)
{
	char const *base = config_string("network.browser_url");
	size_t length = strlen(base);
	char url[256];

	if (!social_handle_valid(handle) || !length)
		return;
	while (length && base[length - 1] == '/')
		length--;
	snprintf(url, sizeof(url), "%.*s/u/%s", (int)length, base, handle);
	platform_log("DELTA: opening a profile page");
	platform_open_url(url);
}

static void recent_option_run(short option)
{
	struct social_recent const *player = recent_chosen();
	short options[MAXIMUM_RECENT_OPTIONS], count, index;
	boolean has = FALSE;

	if (!player)
		return;
	count = recent_option_list(player, options);
	for (index = 0; index < count; index++)
		has = has || options[index] == option;
	if (!has)
		return;
	if (option == _recent_block && guide.recent_armed != option)
	{
		guide.recent_armed = option;
		return;
	}
	guide.recent_armed = NONE;
	switch (option)
	{
	case _recent_add: friend_act(BROWSER_SOCIAL_REQUEST, player->handle); break;
	case _recent_block: friend_act(BROWSER_SOCIAL_BLOCK, player->handle); break;
	case _recent_view: profile_page_open(player->handle); break;
	}
}

/* A on RECENT: the chosen player's options, or the one chosen in them */
static void recent_activate(void)
{
	struct social_recent const *player = recent_chosen();
	short options[MAXIMUM_RECENT_OPTIONS], count;

	if (!player)
		return;
	count = recent_option_list(player, options);
	if (guide.recent_options)
	{
		if (guide.recent_option >= 0 && guide.recent_option < count)
			recent_option_run(options[guide.recent_option]);
		return;
	}
	guide.recent_options = TRUE;
	guide.recent_option = 0;
	guide.recent_armed = NONE;
}

static void recent_move(short step)
{
	struct social_recent const *player = recent_chosen();
	short *row = &guide.row[SOCIAL_SECTION_RECENT];

	if (guide.recent_options && player)
	{
		short options[MAXIMUM_RECENT_OPTIONS], count = recent_option_list(player, options);

		guide.recent_option = (short)PIN(guide.recent_option + step, 0, MAX(0, count - 1));
		guide.recent_armed = NONE;
		return;
	}
	if (guide.recent_count > 0)
		*row = (short)PIN(*row + step, 0, guide.recent_count - 1);
	guide.recent_armed = NONE;
	guide.message[0] = 0;
}

/* Y on RECENT: ADD FRIEND, where it is offered */
static boolean recent_can_add(void)
{
	struct social_recent const *player = recent_chosen();
	short options[MAXIMUM_RECENT_OPTIONS];

	return guide.section == SOCIAL_SECTION_RECENT && friends_signed_in() && guide.friends_loaded &&
		!guide.recent_options && player && recent_option_list(player, options) > 0 && options[0] == _recent_add;
}

/* ---------- PARTY: the site's party (browser.c), following its leader */

/* a platform's number from its registry name (0: not known) */
static int platform_number(char const *name)
{
	int platform;

	for (platform = 1; name && name[0] && platform < NUMBER_OF_DELTA_PLATFORMS; platform++)
	{
		if (!strcmp(delta_peer_platform_name(platform), name))
			return platform;
	}
	return 0;
}

static void party_message_set(char const *text)
{
	social_copy_text(guide.party_message, sizeof(guide.party_message), text);
	guide.party_message_time = system_milliseconds();
}

/* an action on the party (browser_party_act), one at a time: whether it
was taken (FALSE: one is on its way still, said on PARTY) */
static boolean party_act(char const *action, char const *value)
{
	struct browser_social_result result;

	/* (a copy with no profile goes by its game name) */
	if (guide.party_acting || !browser_party_act(action, value, guide.name))
	{
		party_message_set(STILL_SENDING);
		return FALSE;
	}
	browser_party_result(&result);
	guide.party_acting = TRUE;
	guide.party_result_seen = result.serial;
	guide.party_message[0] = 0;
	guide.party_armed = NONE;
	return TRUE;
}

/* FRIENDS' INVITE TO PARTY: the invite, or with no party yet, a party
started and then the invite (party_invite_answer). What came of it is said
under the friend's options, and INVITED marks their row */
static void party_invite_send(char const *handle)
{
	if (guide.party_invite_handle[0] || guide.party_acting)
	{
		message_set(STILL_SENDING);
		return;
	}
	guide.party_invite_starting = !guide.party.has;
	social_copy_text(guide.party_invite_handle, sizeof(guide.party_invite_handle), handle);
	if (!party_act(guide.party_invite_starting ? "create" : "invite", guide.party_invite_starting ? "" : handle))
	{
		guide.party_invite_handle[0] = 0;
		message_set(STILL_SENDING);
		return;
	}
	guide.message[0] = 0;
}

/* the answer to FRIENDS' INVITE TO PARTY's request: a party started goes
on to the invite; an invite sent or refused is said */
static void party_invite_answer(struct browser_social_result const *result)
{
	char handle[SOCIAL_HANDLE_SIZE], line[128];

	social_copy_text(handle, sizeof(handle), guide.party_invite_handle);
	if (result->state == BROWSER_SOCIAL_REFUSED)
	{
		guide.party_invite_handle[0] = 0;
		social_sent_line(line, sizeof(line), SOCIAL_SENT_PARTY, friend_name(handle), 1,
			result->message[0] ? result->message : "Couldn't send the invite. Try again.");
		message_set(line);
		return;
	}
	if (guide.party_invite_starting)
	{
		guide.party_invite_starting = FALSE;
		if (!party_act("invite", handle))
		{
			guide.party_invite_handle[0] = 0;
			message_set(STILL_SENDING);
		}
		return;
	}
	guide.party_invite_handle[0] = 0;
	social_sent_add(&guide.sent, handle, SOCIAL_SENT_PARTY, system_milliseconds());
	social_sent_line(line, sizeof(line), SOCIAL_SENT_PARTY, friend_name(handle), friend_online(handle), "");
	message_ok_set(line);
}

/* the party's own buttons, in the order they show */
static short party_actions(short *actions)
{
	short count = 0;

	if (!guide.party.has)
	{
		actions[count++] = _party_action_start;
		actions[count++] = _party_action_join_code;
		return count;
	}
	if (guide.party_member != NONE)
	{
		actions[count++] = _party_action_make_leader;
		if (party_has_leader())
			actions[count++] = _party_action_kick;
		return count;
	}
	if (friends_signed_in() && guide.party.member_count < guide.party.largest)
		actions[count++] = _party_action_invite_friends;
	actions[count++] = _party_action_copy_link;
	/* (the leader's: a member's game is never the party's) */
	if (guide.party.leader)
		actions[count++] = _party_action_bring;
	actions[count++] = _party_action_who;
	if (guide.party.leader)
		actions[count++] = _party_action_new_code;
	else
		actions[count++] = _party_action_follow;
	actions[count++] = _party_action_leave;
	return count;
}

/* PARTY's VOICE button: in a party whose party channel can be talked on
now (voice on here, a host with Delta voice that allows it and hasn't muted
this player, a party tag that reads); not on a member. Why not, when it
can't, is SETTINGS' VOICE CHANNEL's to say */
static boolean party_voice_shown(void)
{
#ifdef HALO_PORT_VOICE_AUDIO
	return guide.party.has && guide.party_member == NONE &&
		voice_game_channel_state(_delta_voice_channel_party) == _voice_channel_open;
#else
	return FALSE;
#endif
}

/* PARTY's items: the members (or the party invites), then the buttons,
VOICE first */
static short party_items(struct party_item *items)
{
	short actions[8], count = 0, index, action_count = party_actions(actions);

	if (guide.party.has)
	{
		for (index = 0; index < guide.party.member_count; index++)
		{
			items[count].kind = _party_item_member;
			items[count++].index = index;
		}
		if (party_voice_shown())
		{
			items[count].kind = _party_item_voice;
			items[count++].index = 0;
		}
	}
	else
	{
		for (index = 0; index < friends_state.party_invite_count && guide.friends_loaded && friends_signed_in(); index++)
		{
			items[count].kind = _party_item_invite;
			items[count++].index = index;
		}
	}
	for (index = 0; index < action_count; index++)
	{
		items[count].kind = _party_item_action;
		items[count++].index = actions[index];
	}
	return count;
}

/* whether an item is in the button column (VOICE and the buttons) */
static boolean party_item_button(struct party_item const *item)
{
	return item->kind == _party_item_action || item->kind == _party_item_voice;
}

/* the first button's row */
static short party_first_action(struct party_item const *items, short count)
{
	short first = 0;

	while (first < count && !party_item_button(&items[first]))
		first++;
	return first;
}

/* an item as remembered: a member and an invite by their IDs, as their
numbers change when others come and go */
static void party_item_key(struct party_item const *item, struct social_list_key *key)
{
	csmemset(key, 0, sizeof(*key));
	key->kind = item->kind;
	key->index = item->index;
	if (item->kind == _party_item_member && item->index >= 0 && item->index < guide.party.member_count)
	{
		social_copy_text(key->id, sizeof(key->id), guide.party.members[item->index].id);
		key->index = 0;
	}
	else if (item->kind == _party_item_invite && item->index >= 0 && item->index < friends_state.party_invite_count)
	{
		social_copy_text(key->id, sizeof(key->id), friends_state.party_invites[item->index].party);
		key->index = 0;
	}
}

/* the items now, as remembered (party_item_key); their count */
static short party_keys(struct social_list_key *keys)
{
	struct party_item items[MAXIMUM_PARTY_ITEMS];
	short count = party_items(items), index;

	for (index = 0; index < count; index++)
		party_item_key(&items[index], &keys[index]);
	return count;
}

/* before a press, a click or a frame drawn: the row back on the item
remembered if the list moved it (a member joined or left, VOICE came or
went), and LEAVE or KICK disarmed if the item under the row is another now
(else the next A could hand over leadership with no second press). TRUE when
the item under the row is another than the one remembered: a press made this
frame was aimed at the old one, so handle drops what would act on it */
static boolean party_selection_keep(void)
{
	struct social_list_key keys[MAXIMUM_PARTY_ITEMS];
	short count = party_keys(keys);
	int changed;

	guide.row[SOCIAL_SECTION_PARTY] = (short)social_list_keep(guide.party_kept ? &guide.party_kept_key : NULL,
		guide.party_kept_row, keys, count, guide.row[SOCIAL_SECTION_PARTY], &changed);
	if (changed)
		guide.party_armed = NONE;
	guide.party_kept = count > 0;
	guide.party_kept_row = guide.row[SOCIAL_SECTION_PARTY];
	if (count > 0)
		guide.party_kept_key = keys[guide.party_kept_row];
	return changed != 0;
}

/* the list as drawn this frame, for the taps on it (party_tap_lands) */
static void party_drawn_remember(void)
{
	guide.party_drawn_count = party_keys(guide.party_drawn);
}

/* whether a tap on row (a target from the frame last drawn) still lands on
the item drawn there: not when the list changed under it since (a member
left, VOICE came or went), the tap then dropped as aimed at another item */
static boolean party_tap_lands(short row)
{
	struct social_list_key keys[MAXIMUM_PARTY_ITEMS];
	short count = party_keys(keys);

	return social_list_same_item(guide.party_drawn, guide.party_drawn_count, keys, count, row) != 0;
}

/* after a press or a click: the item chosen now remembered as it is (what
the press armed stays armed) */
static void party_selection_remember(void)
{
	struct party_item items[MAXIMUM_PARTY_ITEMS];
	short count = party_items(items), row = guide.row[SOCIAL_SECTION_PARTY];

	guide.party_kept = row >= 0 && row < count;
	if (!guide.party_kept)
		return;
	guide.party_kept_row = row;
	party_item_key(&items[row], &guide.party_kept_key);
}

static boolean party_chosen(struct party_item *item)
{
	struct party_item items[MAXIMUM_PARTY_ITEMS];
	short count = party_items(items), row = guide.row[SOCIAL_SECTION_PARTY];

	if (row < 0 || row >= count)
		return FALSE;
	*item = items[row];
	return TRUE;
}

/* whether anyone leads the party (a leader who went quiet hands it only to
an invited member; with none, the members choose) */
static boolean party_has_leader(void)
{
	return social_party_leader(&guide.party) != NULL;
}

static char const *party_action_label(short action)
{
	switch (action)
	{
	case _party_action_invite_friends: return "INVITE FRIENDS";
	case _party_action_copy_link: return "COPY PARTY LINK";
	case _party_action_bring: return "BRING PARTY TO MY GAME";
	case _party_action_who: return "WHO CAN JOIN";
	case _party_action_new_code: return "NEW PARTY CODE";
	case _party_action_follow: return "FOLLOW LEADER";
	case _party_action_leave: return guide.party_armed == _party_action_leave ? "CONFIRM LEAVE" : "LEAVE PARTY";
	case _party_action_start: return "START A PARTY";
	case _party_action_join_code: return "JOIN WITH A CODE";
	case _party_action_make_leader: return party_has_leader() ? "MAKE LEADER" : "CHOOSE AS LEADER";
	default: return guide.party_armed == _party_action_kick ? "CONFIRM KICK" : "KICK";
	}
}

/* the party's line on a member (LEADER, YOU); a member with no Delta
profile is marked unverified (their name is only the game name they send) */
static char const *member_note(struct social_party_member const *member)
{
	if (!member->handle[0])
		return member->leader ? "Leader \xC2\xB7 Unverified" : member->you ? "You \xC2\xB7 Unverified" : "Unverified";
	return member->leader ? "Leader" : member->you ? "You" : "";
}

/* the code picked so far (with JOIN WITH A CODE), as social.c reads it */
static void code_pasted(void)
{
	char text[256], code[SOCIAL_PARTY_CODE_SIZE];

	if (!platform_clipboard_get(text, sizeof(text)) || !social_party_code_clean(text, code))
	{
		party_message_set("No party code on the clipboard");
		return;
	}
	social_copy_text(guide.code, sizeof(guide.code), code);
	guide.code_cursor = SOCIAL_PARTY_CODE_LENGTH - 1;
}

static void code_step(short direction)
{
	static char const letters[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
	short count = (short)(sizeof(letters) - 1), at = 0;
	char *c = &guide.code[guide.code_cursor];
	char const *found = strchr(letters, *c);

	if (*c && found)
		at = (short)(found - letters);
	at = (short)((at + (direction < 0 ? count - 1 : 1)) % count);
	*c = letters[at];
}

static void code_entry_handle(unsigned int pressed)
{
	char code[SOCIAL_PARTY_CODE_SIZE];

	if (pressed & GUIDE_INPUT_B)
	{
		guide.code_entry = FALSE;
		return;
	}
	if (pressed & GUIDE_INPUT_UP)
		code_step(1);
	if (pressed & GUIDE_INPUT_DOWN)
		code_step(-1);
	if (pressed & GUIDE_INPUT_LEFT)
		guide.code_cursor = (short)MAX(0, guide.code_cursor - 1);
	if (pressed & GUIDE_INPUT_RIGHT)
		guide.code_cursor = (short)MIN(SOCIAL_PARTY_CODE_LENGTH - 1, guide.code_cursor + 1);
	if (pressed & GUIDE_INPUT_Y)
		code_pasted();
	if (pressed & GUIDE_INPUT_A)
	{
		if (social_party_code_clean(guide.code, code))
		{
			party_act("join", code);
			guide.code_entry = FALSE;
		}
		else
			party_message_set("That isn't a party code.");
	}
}

/* A on PARTY */
static void party_activate(void)
{
	struct party_item item;
	char link[SOCIAL_PARTY_LINK_SIZE];

	if (!party_chosen(&item) || guide.party_acting)
		return;
	if (item.kind == _party_item_member)
	{
		struct social_party_member const *member = &guide.party.members[item.index];

		/* (the leader's options on another member; with no leader, any
		member's choice of one, themselves too) */
		if ((guide.party.leader && !member->you) || !party_has_leader())
		{
			struct party_item items[MAXIMUM_PARTY_ITEMS];
			short count;

			guide.party_member = item.index;
			social_copy_text(guide.party_member_id, sizeof(guide.party_member_id), member->id);
			guide.party_armed = NONE;
			count = party_items(items);
			guide.row[SOCIAL_SECTION_PARTY] = party_first_action(items, count);
		}
		return;
	}
#ifdef HALO_PORT_VOICE_AUDIO
	if (item.kind == _party_item_voice)
	{
		/* (the channel talked on, stepped to the next one that can be used
		now; SETTINGS' VOICE CHANNEL steps through all of them) */
		voice_game_set_channel(voice_game_next_channel());
		return;
	}
#endif
	if (item.kind == _party_item_invite)
	{
		party_act("accept", friends_state.party_invites[item.index].party);
		return;
	}
	switch (item.index)
	{
	case _party_action_invite_friends:
		section_go(SOCIAL_SECTION_FRIENDS);
		break;
	case _party_action_copy_link:
		if (social_party_link(guide.party.code, link, sizeof(link)))
		{
			platform_clipboard_set(link);
			party_message_set("Link copied");
		}
		break;
	case _party_action_bring:
		config_write_boolean("delta.bring_party", !config_boolean("delta.bring_party"));
		guide.go_sent[0] = 0;
		break;
	case _party_action_who:
		if (guide.party.leader)
			party_act("rule", social_party_rule_next(guide.party.who_can_join, 1));
		break;
	case _party_action_new_code:
		if (guide.party.leader)
		{
			party_act("code", "");
			party_message_set("The old code no longer works");
		}
		break;
	case _party_action_follow:
		config_write("delta.follow_leader", social_follow_mode_next(config_string("delta.follow_leader")));
		break;
	case _party_action_leave:
		if (guide.party_armed != _party_action_leave)
			guide.party_armed = _party_action_leave;
		else
			party_act("leave", "");
		break;
	case _party_action_start:
		party_act("create", "");
		break;
	case _party_action_join_code:
		guide.code_entry = TRUE;
		guide.code_cursor = 0;
		memset(guide.code, 0, sizeof(guide.code));
		memset(guide.code, 'A', SOCIAL_PARTY_CODE_LENGTH);
		guide.party_message[0] = 0;
		break;
	case _party_action_make_leader:
	case _party_action_kick:
		if (guide.party_member < 0 || guide.party_member >= guide.party.member_count)
			break;
		if (item.index == _party_action_kick && guide.party_armed != _party_action_kick)
		{
			guide.party_armed = _party_action_kick;
			break;
		}
		party_act(item.index == _party_action_kick ? "kick" : "lead", guide.party.members[guide.party_member].id);
		guide.row[SOCIAL_SECTION_PARTY] = guide.party_member;
		guide.party_member = NONE;
		break;
	}
}

/* X on PARTY: a party invite declined */
static void party_decline(void)
{
	struct party_item item;

	if (party_chosen(&item) && item.kind == _party_item_invite)
		party_act("decline", friends_state.party_invites[item.index].party);
}

/* LEFT and RIGHT on PARTY: between the list and the buttons */
static void party_side(void)
{
	struct party_item items[MAXIMUM_PARTY_ITEMS], item;
	short count = party_items(items), first_action = party_first_action(items, count);

	if (!party_chosen(&item) || first_action == 0 || first_action >= count)
		return;
	guide.row[SOCIAL_SECTION_PARTY] = party_item_button(&item) ? 0 : first_action;
	guide.party_armed = NONE;
}

/* the toasts, and what is said, as the party changes */
static void party_news(struct social_party const *before)
{
	struct social_party_member const *leader = social_party_leader(&guide.party);
	char line[96];

	if (!before->has && guide.party.has && !guide.party.leader && leader)
	{
		snprintf(line, sizeof(line), "You're in %s's party", leader->name);
		toast_from(_toast_party_note, "", "Party", line, "");
	}
	else if (before->has && !guide.party.has && !guide.party_acting && !guide.open)
		toast_from(_toast_party_note, "", "Party", "You're not in a party now", "");
}

/* each frame: the party read, its tag told to the game's host, the
leader's game told to the party, a member's following */
static void party_update(void)
{
	struct browser_social_result result;
	struct social_party before = guide.party;
	int serial = guide.party_serial;
	unsigned char tag[8];
	boolean tagged;

	if (!debug_sample())
		browser_party_get(&guide.party, &serial);
	else if (!guide.party.has)
	{
		debug_sample_party(&guide.party);
		serial = guide.party_serial + 1;
	}
	if (serial != guide.party_serial)
	{
		guide.party_serial = serial;
		party_news(&before);
		/* (the member the leader's options are on, found again by ID: one
		who left takes their options with them) */
		if (guide.party_member != NONE)
		{
			short index, found = NONE;

			for (index = 0; index < guide.party.member_count; index++)
			{
				if (!strcmp(guide.party.members[index].id, guide.party_member_id))
					found = index;
			}
			guide.party_member = found;
		}
		if (guide.party_member != NONE && !guide.party.leader && party_has_leader())
			guide.party_member = NONE;
		if (!guide.party.has)
			guide.go_sent[0] = 0;
		/* (a party left or a new one: its invites out are no longer
		this player's to show) */
		if (!guide.party.has || (before.has && strcmp(before.id, guide.party.id)))
			social_sent_forget(&guide.sent, NULL, SOCIAL_SENT_PARTY);
		/* (a new party's go is new; an older one's isn't followed again) */
		if (!before.has || strcmp(before.id, guide.party.id))
			guide.follow_seen = guide.party.go.has ? guide.party.go.serial : 0;
	}
	/* (an action's answer: a refusal's line; one a link asked for, in a
	toast) */
	browser_party_result(&result);
	if (result.serial == guide.party_result_seen && guide.party_acting && result.state != BROWSER_SOCIAL_BUSY)
	{
		guide.party_acting = FALSE;
		if (result.state == BROWSER_SOCIAL_REFUSED)
			party_message_set(result.message);
		if (guide.party_invite_handle[0])
			party_invite_answer(&result);
	}
	else if (result.serial != guide.party_result_seen && result.state != BROWSER_SOCIAL_BUSY)
	{
		guide.party_result_seen = result.serial;
		if (result.state == BROWSER_SOCIAL_REFUSED)
			toast_from(_toast_party_note, "", "Party", result.message, "");
	}
	tagged = guide.party.has && social_party_tag_bytes(guide.party.tag, tag);
	delta_peer_game_set_party_tag(tagged ? tag : NULL);
	/* (its members in a game hear each other on the party channel) */
	voice_game_set_party(tagged ? tag : NULL);
}

/* about once a second: the leader's game told to the party (with BRING PARTY
TO MY GAME), a member's copy following it */
static void party_where(void)
{
	char invite[160];
	boolean in_network_game = network_game_get_game() != NULL;
	struct social_follow_inputs inputs;
	int decision;

	invite[0] = 0;
	if (in_network_game && !p2p_hosting_invite(invite, sizeof(invite)) && !p2p_joined_invite(invite, sizeof(invite)))
		invite[0] = 0;
	if (guide.party.has && guide.party.leader)
	{
		if (!in_network_game || !invite[0])
			guide.go_sent[0] = 0;
		else if (config_boolean("delta.bring_party") && social_presence_invite_valid(invite) &&
			strcmp(invite, guide.go_sent))
		{
			social_copy_text(guide.go_sent, sizeof(guide.go_sent), invite);
			browser_party_go(invite);
			platform_log("DELTA: the party is told where its leader went");
		}
		return;
	}
	if (guide.follow_active)
		return;
	memset(&inputs, 0, sizeof(inputs));
	inputs.seen_serial = guide.follow_seen;
	inputs.current_invite = invite;
	inputs.mode = social_follow_mode(config_string("delta.follow_leader"));
	inputs.hosting_others = global_network_game_server_get() && network_game_get_game() &&
		network_game_get_game()->machine_count > 1;
	decision = social_follow_decide(&guide.party, &inputs);
	if (decision == SOCIAL_FOLLOW_NONE)
		return;
	guide.follow_seen = guide.party.go.serial;
	if (decision == SOCIAL_FOLLOW_CANT)
	{
		char line[96];

		social_follow_reason(&guide.party, &inputs, line, sizeof(line));
		toast_from(_toast_party_note, "", "Party", line, "");
		platform_log("DELTA: can't follow the party's leader (%s)", guide.party.go.reason[0] ? guide.party.go.reason :
			"hosting");
		return;
	}
	if (decision != SOCIAL_FOLLOW_OFFER || virtual_keyboard_active())
		return;
	/* (the offer: over the Guide, which closes) */
	if (guide.open)
		guide_close();
	social_copy_text(guide.follow_link, sizeof(guide.follow_link), guide.party.go.join);
	social_follow_line(&guide.party, guide.follow_line, sizeof(guide.follow_line));
	guide.follow_party[0] = 0;
	guide.follow_active = TRUE;
	guide.follow_time = system_milliseconds();
	guide.held = 0xFFFFFFFF;
	platform_guide_capture(TRUE);
	event_manager_flush();
	platform_log("DELTA: following the party's leader in a few seconds");
}

static void follow_end(boolean go)
{
	guide.follow_active = FALSE;
	if (!guide.open)
		platform_guide_capture(FALSE);
	event_manager_flush();
	/* (a party's link: joined only on the player's word) */
	if (guide.follow_party[0])
	{
		if (go)
			party_act("join", guide.follow_party);
		platform_log(go ? "DELTA: joining a party by its link" : "DELTA: the party link was not joined");
		guide.follow_party[0] = 0;
		return;
	}
	if (go)
	{
		struct social_party_member const *leader = social_party_leader(&guide.party);
		char name[64];

		platform_log("DELTA: following the party's leader");
		snprintf(name, sizeof(name), "%s's game", leader ? leader->name : "the leader");
		/* (in the menus, Online Games joins it as a pasted link; in a game,
		the link alone: the host shows under System Link) */
		if (!browser_screen_join_link(guide.follow_link, name))
			p2p_join_invite(guide.follow_link);
	}
	else
		platform_log("DELTA: stayed");
}

/* whether the offer's countdown ending joins (else the member stays): in
a game, only with delta.follow_leader "always"; a party's link waits for
an answer */
static boolean follow_timeout_joins(void)
{
	return social_follow_timeout_joins(social_follow_mode(config_string("delta.follow_leader")), in_game()) ? TRUE :
		FALSE;
}

static void follow_handle(unsigned int pressed)
{
	if (pressed & GUIDE_INPUT_B)
		follow_end(FALSE);
	else if (pressed & GUIDE_INPUT_A)
		follow_end(TRUE);
	else if (!guide.follow_party[0] && system_milliseconds() - guide.follow_time >= SOCIAL_FOLLOW_TIME)
		follow_end(follow_timeout_joins());
}

/* a party's link opened (or on the command line): asked about first, never
joined unseen */
static void party_link_offer(void)
{
	char code[SOCIAL_PARTY_CODE_SIZE], shown[16];

	if (guide.follow_active || virtual_keyboard_active() || !browser_party_take_link(code, sizeof(code)))
		return;
	if (guide.open)
		guide_close();
	social_party_code_shown(code, shown, sizeof(shown));
	social_copy_text(guide.follow_party, sizeof(guide.follow_party), code);
	snprintf(guide.follow_line, sizeof(guide.follow_line), "Join the party %s?", shown);
	guide.follow_active = TRUE;
	guide.follow_time = system_milliseconds();
	guide.held = 0xFFFFFFFF;
	platform_guide_capture(TRUE);
	event_manager_flush();
	platform_log("DELTA: a party link, asking first");
}


/* ---------- CLAN: the profile's clan (browser.c keeps the profile; its
roster, with handles and where each member is, comes with the friends) */

static boolean clan_in(void)
{
	return guide.clan_loaded && clan_profile.linked && clan_profile.has_clan;
}

static boolean clan_can_invite(void)
{
	return clan_in() && social_clan_can_invite(clan_profile.clan.rank);
}

static void clan_act_started(void)
{
	guide.clan_acting = TRUE;
	guide.message[0] = 0;
}

static void clan_item_add(short kind, short index)
{
	if (guide.clan_count >= MAXIMUM_CLAN_ITEMS)
		return;
	guide.clan_items[guide.clan_count].kind = kind;
	guide.clan_items[guide.clan_count].index = index;
	guide.clan_count++;
}

/* an item's key, to keep it chosen as the list changes: a member's handle
(their name before the roster comes), an invite's clan tag, a handle out */
static char const *clan_item_key(struct friend_item const *item)
{
	switch (item->kind)
	{
	case _clan_member:
	{
		struct delta_profile_clan_member const *member = &clan_profile.clan.members[item->index];

		return member->handle[0] ? member->handle : member->display;
	}
	case _clan_invited: return clan_profile.clan_invited[item->index];
	case _clan_invite: return clan_profile.invites[item->index].tag;
	case _clan_action: return item->index == _clan_action_invite ? "invite" : "leave";
	default: return "";
	}
}

static boolean clan_item_selectable(short index)
{
	return index >= 0 && index < guide.clan_count && guide.clan_items[index].kind != _clan_heading;
}

static struct friend_item const *clan_chosen_item(void)
{
	short row = guide.row[SOCIAL_SECTION_CLAN];

	return row >= 0 && row < guide.clan_count ? &guide.clan_items[row] : NULL;
}

static void clan_chosen_remember(void)
{
	struct friend_item const *item = clan_chosen_item();

	if (!item)
		return;
	guide.clan_chosen_kind = item->kind;
	social_copy_text(guide.clan_chosen_key, sizeof(guide.clan_chosen_key), clan_item_key(item));
}

static void clan_items_build(void)
{
	short index, found = NONE;
	short *row = &guide.row[SOCIAL_SECTION_CLAN];

	guide.clan_count = 0;
	if (clan_in())
	{
		clan_item_add(_clan_heading, _clan_heading_members);
		for (index = 0; index < clan_profile.clan.member_count; index++)
			clan_item_add(_clan_member, index);
		if (social_clan_can_invite(clan_profile.clan.rank) && clan_profile.clan_invited_count > 0)
		{
			clan_item_add(_clan_heading, _clan_heading_invited);
			for (index = 0; index < clan_profile.clan_invited_count; index++)
				clan_item_add(_clan_invited, index);
		}
		if (social_clan_can_invite(clan_profile.clan.rank))
			clan_item_add(_clan_action, _clan_action_invite);
		clan_item_add(_clan_action, _clan_action_leave);
	}
	else if (guide.clan_loaded && clan_profile.linked && clan_profile.invite_count > 0)
	{
		clan_item_add(_clan_heading, _clan_heading_invites);
		for (index = 0; index < clan_profile.invite_count; index++)
			clan_item_add(_clan_invite, index);
	}
	for (index = 0; index < guide.clan_count && found == NONE; index++)
	{
		if (clan_item_selectable(index) && guide.clan_items[index].kind == guide.clan_chosen_kind &&
			!strcmp(clan_item_key(&guide.clan_items[index]), guide.clan_chosen_key))
		{
			found = index;
		}
	}
	if (found != NONE)
		*row = found;
	else
	{
		if (guide.section == SOCIAL_SECTION_CLAN)
			guide.options = FALSE;
		*row = (short)PIN(*row, 0, MAX(0, guide.clan_count - 1));
		while (*row > 0 && !clan_item_selectable(*row))
			(*row)--;
		while (*row < guide.clan_count - 1 && !clan_item_selectable(*row))
			(*row)++;
		clan_chosen_remember();
	}
}

/* an item's options (_option_*): their count */
static short clan_item_options(struct friend_item const *item, short *options)
{
	short count = 0;

	if (item->kind == _clan_member)
	{
		struct delta_profile_clan_member const *member = &clan_profile.clan.members[item->index];

		if (!member->you && member->handle[0] && social_clan_can_kick(clan_profile.clan.rank, member->rank))
			options[count++] = _option_kick;
	}
	else if (item->kind == _clan_invite)
	{
		options[count++] = _option_clan_join;
		options[count++] = _option_clan_decline;
	}
	return count;
}

static void clan_option_run(short option)
{
	struct friend_item const *item = clan_chosen_item();
	short options[MAXIMUM_OPTIONS], count, index;
	boolean found = FALSE;

	if (!item || guide.clan_acting)
		return;
	count = clan_item_options(item, options);
	for (index = 0; index < count; index++)
		found |= options[index] == option;
	if (!found)
		return;
	/* (KICK takes a second press) */
	if (option == _option_kick && guide.armed != option)
	{
		guide.armed = option;
		return;
	}
	guide.armed = NONE;
	if (option == _option_kick)
	{
		if (browser_profile_clan_member("kick", clan_profile.clan.members[item->index].handle))
			clan_act_started();
	}
	else
	{
		browser_profile_clan(option == _option_clan_join ? "accept" : "decline", clan_profile.invites[item->index].id);
		clan_act_started();
	}
}

/* A on CLAN: the chosen item's options, INVITE FRIENDS, LEAVE CLAN */
static void clan_activate(void)
{
	struct friend_item const *item = clan_chosen_item();
	short options[MAXIMUM_OPTIONS], count;

	if (!item)
		return;
	if (guide.options)
	{
		count = clan_item_options(item, options);
		if (guide.option >= 0 && guide.option < count)
			clan_option_run(options[guide.option]);
		return;
	}
	if (item->kind == _clan_action)
	{
		if (item->index == _clan_action_invite)
		{
			/* (FRIENDS, where a friend has INVITE TO CLAN) */
			guide.section = SOCIAL_SECTION_FRIENDS;
			guide.first_row = 0;
			guide.armed = NONE;
			return;
		}
		/* (LEAVE CLAN takes a second press) */
		if (guide.armed != ARMED_LEAVE)
		{
			guide.armed = ARMED_LEAVE;
			return;
		}
		guide.armed = NONE;
		if (!guide.clan_acting)
		{
			browser_profile_clan("leave", 0);
			clan_act_started();
		}
		return;
	}
	if (clan_item_options(item, options) > 0)
	{
		guide.options = TRUE;
		guide.option = 0;
		guide.armed = NONE;
	}
}

static void clan_move(short step)
{
	short *row = &guide.row[SOCIAL_SECTION_CLAN];
	short next = *row;
	struct friend_item const *item = clan_chosen_item();

	if (guide.options && item)
	{
		short options[MAXIMUM_OPTIONS], count = clan_item_options(item, options);

		guide.option = (short)PIN(guide.option + step, 0, MAX(0, count - 1));
		guide.armed = NONE;
		return;
	}
	do
		next = (short)(next + step);
	while (next > 0 && next < guide.clan_count && !clan_item_selectable(next));
	if (clan_item_selectable(next))
	{
		*row = next;
		guide.armed = NONE;
		guide.message[0] = 0;
		clan_chosen_remember();
	}
}

/* the profile looked at (often while the Guide is open or an action is
out, now and then while it's closed, for the pause row's count): a new one,
the items again; an action's answer, its line */
/* a clan's invite new since the last read (none the first time: those
waited before this run) gets a toast */
static void clan_invite_news(void)
{
	boolean streamer = config_boolean("chat.streamer_mode") != 0;
	int index, known;

	for (index = 0; guide.clan_invites_read && index < clan_profile.invite_count; index++)
	{
		struct delta_profile_invite const *invite = &clan_profile.invites[index];
		boolean seen = FALSE;
		char key[16], title[96];

		for (known = 0; known < guide.clan_invites_known_count && !seen; known++)
			seen = guide.clan_invites_known[known] == invite->id;
		if (seen)
			continue;
		snprintf(key, sizeof(key), "%d", invite->id);
		if (streamer || !invite->tag[0])
			snprintf(title, sizeof(title), "Clan invite");
		else
			snprintf(title, sizeof(title), "[%s] invited you", invite->tag);
		social_toast_push(&guide.toasts, _toast_clan_invite, key, title, streamer ? "Clan invite" : invite->name, "");
		platform_log("DELTA: a new clan invite");
	}
	guide.clan_invites_known_count = 0;
	for (index = 0; index < clan_profile.invite_count && index < DELTA_PROFILE_CLAN_INVITES; index++)
		guide.clan_invites_known[guide.clan_invites_known_count++] = clan_profile.invites[index].id;
	guide.clan_invites_read = TRUE;
}

/* the main menu reached (from anywhere else): with no DELTA row in the
menus (delta.menu_item off), a note says how to open DELTA, the device
used last's way (delta_hint_text), the first DELTA_HINT_TIMES times in the
player's history (delta.hint_shown). It waits its turn behind any other
toast */
#define HINT_SETTLE 1500

static void hint_update(boolean menu_open)
{
	/* (once a visit to the main menu, after it has been up a moment with no
	video over it: the intro's and the attract's are no new visit) */
	static boolean noted;
	static unsigned long since;
	unsigned long now = system_milliseconds();
	int shown;

	if (!menu_open || !main_menu_is_active())
	{
		noted = FALSE;
		since = 0;
		return;
	}
	/* (nor over the on-screen keyboard, the Workshop's search) */
	if (bink_playback_active() || guide.open || virtual_keyboard_active())
	{
		since = 0;
		return;
	}
	if (!since)
		since = now ? now : 1;
	if (noted || now - since < HINT_SETTLE)
		return;
	noted = TRUE;
	/* (the Gearbox menus retired: said first, once, in the same note;
	display.menus is written as "xbox" once it is drawn, render_toast) */
	if (config_menus_retired_pending() && !guide.menus_retired_queued)
	{
		guide.menus_retired_queued = TRUE;
		social_toast_push(&guide.toasts, _toast_menus_retired, "menus_retired", "Gearbox menus were retired.",
			"You're on the Xbox menus now; change it in Settings.", "");
	}
	shown = (int)config_integer("delta.hint_shown");
	if (delta_hint_due(shown, config_boolean("delta.menu_item")))
	{
		char key[DELTA_KEY_NAME_SIZE], title[64];

		guide_key_shown(key, sizeof(key));
#ifdef __APPLE__
		/* (macOS often keeps the controller's Home button) */
		delta_hint_text_for(title, sizeof(title), !ui_overlay_keyboard(), key, config_boolean("controls.delta_button"),
			config_boolean("delta.hold_back"), 1);
#else
		delta_hint_text_for(title, sizeof(title), !ui_overlay_keyboard(), key, config_boolean("controls.delta_button"),
			config_boolean("delta.hold_back"), 0);
#endif
		/* (counted once it is drawn: render_toast) */
		social_toast_push(&guide.toasts, _toast_hint, "hint", title, "Friends, party and settings", "");
	}
}

/* the next toast waiting, once the one before it is done (and none is a
follow offer's) */
static void toasts_next(void)
{
	struct social_toast toast;

	if ((guide.toast_shown && system_milliseconds() - guide.toast_time < TOAST_TIME) || guide.follow_active)
		return;
	if (social_toast_pop(&guide.toasts, &toast))
		toast_from((short)toast.kind, toast.key, toast.title, toast.line, toast.name);
}

static void clan_update(void)
{
	struct browser_profile_status status;
	unsigned long now = system_milliseconds();
	unsigned long interval = guide.open || guide.clan_acting ? CLAN_LOOK_INTERVAL : CLAN_LOOK_CLOSED;

	if (guide.clan_time && now - guide.clan_time < interval)
		return;
	guide.clan_time = now ? now : 1;
	browser_profile_get(&status, &clan_profile);
	guide.clan_state = status.state;
	if (status.serial != guide.clan_serial || guide.clan_loaded != (status.has_profile != 0))
	{
		guide.clan_serial = status.serial;
		guide.clan_loaded = status.has_profile != 0;
		clan_items_build();
		if (guide.clan_loaded)
			clan_invite_news();
	}
	if (!guide.clan_acting || status.action == BROWSER_PROFILE_ACTION_WORKING)
		return;
	guide.clan_acting = FALSE;
	if (status.message[0])
		message_set(status.message);
	if (guide.section == SOCIAL_SECTION_CLAN)
		guide.options = FALSE;
	browser_profile_action_seen();
}

/* the clans' invites waiting (the pause row's count) */
static short clan_invites_waiting(void)
{
	return guide.clan_loaded && clan_profile.linked && !clan_profile.has_clan ? (short)clan_profile.invite_count : 0;
}

/* ---------- the sections' rows */

static short section_rows(short section)
{
	switch (section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		return (short)guide.players.count;
	case SOCIAL_SECTION_SETTINGS:
		return settings_rows();
	case SOCIAL_SECTION_SERVICE_RECORD:
		return guide_record_rows();
	case SOCIAL_SECTION_WORKSHOP:
		return guide_workshop_rows();
	case SOCIAL_SECTION_PARTY:
	{
		struct party_item items[MAXIMUM_PARTY_ITEMS];

		return party_items(items);
	}
	case SOCIAL_SECTION_FRIENDS:
		if (friends_signed_in())
			return guide.friends_loaded ? guide.item_count : 0;
		return sign_in_section() != NONE ? 1 : 0;
	case SOCIAL_SECTION_RECENT:
		if (friends_signed_in())
			return guide.friends_loaded ? guide.recent_count : 0;
		return sign_in_section() != NONE ? 1 : 0;
	case SOCIAL_SECTION_CLAN:
		if (friends_signed_in())
			return guide.clan_loaded ? guide.clan_count : 0;
		return sign_in_section() != NONE ? 1 : 0;
	default:
		/* (SIGN IN, without a Delta profile) */
		return !guide.linked && sign_in_section() != NONE ? 1 : 0;
	}
}

/* whether a section's tab shows (WORKSHOP only with MCC on this computer) */
static boolean section_shown(short section)
{
	return section != SOCIAL_SECTION_WORKSHOP || guide_workshop_shown();
}

static void section_go(short section)
{
	/* (a section not shown is passed, the way the bumper went) */
	short step = section < guide.section ? -1 : 1;

	section = (short)((section + NUMBER_OF_SOCIAL_SECTIONS) % NUMBER_OF_SOCIAL_SECTIONS);
	while (!section_shown(section))
		section = (short)((section + step + NUMBER_OF_SOCIAL_SECTIONS) % NUMBER_OF_SOCIAL_SECTIONS);
	guide.section = section;
	guide.first_row = 0;
	guide.options = FALSE;
	guide.armed = NONE;
	guide.recent_options = FALSE;
	guide.recent_armed = NONE;
	guide.party_member = NONE;
	guide.party_armed = NONE;
	guide.code_entry = FALSE;
	/* (a report left half-made is dropped) */
	while (report_player_step() != _report_step_none)
		report_player_back();
	if (guide.section == SOCIAL_SECTION_SERVICE_RECORD)
		guide_record_open();
	if (guide.section == SOCIAL_SECTION_WORKSHOP)
		guide_workshop_open();
}

/* A on the chosen row */
static void activate(void)
{
	short row = guide.row[guide.section];

	switch (guide.section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		players_activate(row);
		return;
	case SOCIAL_SECTION_SERVICE_RECORD:
		guide_record_activate();
		return;
	case SOCIAL_SECTION_WORKSHOP:
		guide_workshop_activate();
		return;
	case SOCIAL_SECTION_SETTINGS:
		if (row >= settings_count())
			extra_row_activate(row);
		else
			setting_change(row, 1);
		return;
	case SOCIAL_SECTION_PARTY:
		party_activate();
		return;
	case SOCIAL_SECTION_FRIENDS:
		if (friends_signed_in())
		{
			if (guide.friends_loaded)
				friends_activate();
			return;
		}
		break;
	case SOCIAL_SECTION_RECENT:
		if (friends_signed_in())
		{
			if (guide.friends_loaded)
				recent_activate();
			return;
		}
		break;
	case SOCIAL_SECTION_CLAN:
		if (friends_signed_in())
		{
			if (guide.clan_loaded)
				clan_activate();
			return;
		}
		break;
	}
	/* (Quick Connect's code, shown here) */
	if (!friends_signed_in() && sign_in_section() != NONE)
		sign_in_activate();
}

static void move(short step)
{
	short rows = section_rows(guide.section);
	short *row = &guide.row[guide.section];

	if (rows <= 0)
		return;
	if (guide.section == SOCIAL_SECTION_WORKSHOP)
	{
		guide_workshop_move(step);
		return;
	}
	if (guide.section == SOCIAL_SECTION_FRIENDS && friends_signed_in())
	{
		friends_move(step);
		return;
	}
	if (guide.section == SOCIAL_SECTION_RECENT && friends_signed_in())
	{
		recent_move(step);
		return;
	}
	/* (in a player's options or a report: its choices) */
	if (guide.section == SOCIAL_SECTION_PLAYERS_HERE && guide.options)
	{
		short kinds[MAXIMUM_PLAYER_OPTIONS];
		char labels[MAXIMUM_PLAYER_OPTIONS][REPORT_PLAYER_CHOICE_SIZE];
		short count = player_choices(*row, kinds, labels);

		if (count > 0)
			guide.option = (short)PIN(guide.option + step, 0, count - 1);
		return;
	}
	if (guide.section == SOCIAL_SECTION_CLAN && friends_signed_in())
	{
		clan_move(step);
		return;
	}
	*row = (short)PIN(*row + step, 0, rows - 1);
	guide.party_armed = NONE;
}

/* what was just pressed (and a d-pad held, again and again) */
static unsigned int presses(void)
{
	unsigned int held = platform_guide_held();
	unsigned int pressed = held & ~guide.held;
	unsigned int directions = held & (GUIDE_INPUT_UP | GUIDE_INPUT_DOWN | GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT);
	unsigned long now = system_milliseconds();

	if (directions && directions == guide.repeat_held)
	{
		if (now - guide.repeat_time >= REPEAT_DELAY)
		{
			pressed |= directions;
			guide.repeat_time = now - REPEAT_DELAY + REPEAT_INTERVAL;
		}
	}
	else
	{
		guide.repeat_held = directions;
		guide.repeat_time = now;
	}
	guide.held = held;
	return pressed;
}

/* ---------- drawing */

/* the player's dot */
static unsigned int dot_color(int state)
{
	switch (state)
	{
	case SOCIAL_STATE_IN_GAME: return COLOR_DOT_GAME;
	case SOCIAL_STATE_ONLINE: return COLOR_DOT_ONLINE;
	case SOCIAL_STATE_AWAY: return COLOR_DOT_AWAY;
	default: return COLOR_DOT_OFFLINE;
	}
}

/* the dot drawn at x, y (its 6 by 6 square): in a game, a ring around it
too, so it stands apart from Online by its shape as well as its color */
static void dot_draw(float x, float y, int state)
{
	unsigned int color = dot_color(state);

	ui_overlay_rect(x, y, 6, 6, 3, color);
	if (state == SOCIAL_STATE_IN_GAME)
		ui_overlay_outline(x - 2.5f, y - 2.5f, 11, 11, 5.5f, 1.0f, color);
}

/* the ui.map's plasma map, read once it is in the texture cache (none:
the overlay's own noise) */
static void plasma_look(void)
{
	long tag;

	if (guide.plasma_tries >= PLASMA_TRIES || (!main_menu_is_active() && !game_map_loaded()))
		return;
	guide.plasma_tries++;
	tag = interface_get_tag_index(_interface_bitmap_iface_map3);
	if (tag != NONE)
	{
		struct bitmap_group *group = bitmap_group_get(tag);

		if (group && group->bitmaps.count > 0)
		{
			struct bitmap_data *bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, 0, struct bitmap_data);
			void *hardware_format = _texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE);

			if (hardware_format && ui_overlay_plasma_from_texture(hardware_format))
			{
				guide.plasma_tries = PLASMA_TRIES;
				return;
			}
		}
	}
	if (guide.plasma_tries == PLASMA_TRIES)
		platform_log("DELTA: no plasma map in ui.map; drawn from DELTA's own");
}

/* (WORKSHOP's rows and options, for the pointer) */
static void workshop_target(float x0, float y0, float x1, float y1, short kind, short index)
{
	target_add(x0, y0, x1, y1, kind == GUIDE_WORKSHOP_TARGET_ROW ? _target_row : kind == GUIDE_WORKSHOP_TARGET_OPTION ?
		_target_option : kind == GUIDE_WORKSHOP_TARGET_PAGE ? _target_page : _target_modal, index);
}

static void logs_target(float x0, float y0, float x1, float y1, short index)
{
	target_add(x0, y0, x1, y1, _target_logs, index);
}

static void render_card(void)
{
	ui_style_card(CONTENT_LEFT, CONTENT_RIGHT, HEADER_Y, guide.name, guide.status, guide.color, "DELTA");
}

static void render_tabs(void)
{
	char const *names[NUMBER_OF_SOCIAL_SECTIONS];
	float starts[NUMBER_OF_SOCIAL_SECTIONS], ends[NUMBER_OF_SOCIAL_SECTIONS];
	short sections[NUMBER_OF_SOCIAL_SECTIONS];
	short section, count = 0, selected = 0;

	/* (the tabs shown: WORKSHOP only with MCC) */
	for (section = 0; section < NUMBER_OF_SOCIAL_SECTIONS; section++)
	{
		if (!section_shown(section))
			continue;
		if (section == guide.section)
			selected = count;
		sections[count] = section;
		names[count++] = social_section_name(section);
	}
	ui_style_tabs(CONTENT_LEFT, CONTENT_RIGHT, TABS_Y, TABS_HEIGHT, names, count, selected,
		"Q", "E", starts, ends);
	for (section = 0; section < count; section++)
		target_add(starts[section], TABS_Y, ends[section], TABS_Y + TABS_HEIGHT, _target_tab, sections[section]);
}

/* a player's row: their dot, tag, status line and platform's logo */
static void render_row(struct social_row const *row, float x, float y, float width, boolean on, short index)
{
	char tag[SOCIAL_TAG_SIZE + 4];

	/* (a name of letters the menus' fonts lack: its ASCII letters, or
	"Player", never a blank row) */
	ui_overlay_name_shown(ui_style_text_font(UI_TEXT_NAME), row->tag, tag, sizeof(tag));
	ui_style_row(x, y, width, ROW_HEIGHT, on);
	if (row->state >= 0)
		dot_draw(x + 9, y + ROW_HEIGHT * 0.5f - 3, row->state);
	if (row->status[0])
	{
		ui_style_text(UI_TEXT_NAME, x + 22, y + 3, UI_ALIGN_LEFT, tag);
		ui_style_text(row->muted ? UI_TEXT_WARNING : UI_TEXT_STATUS, x + 22, y + 17, UI_ALIGN_LEFT, row->status);
	}
	else
		ui_style_text(UI_TEXT_NAME, x + 22, y + (ROW_HEIGHT - ui_style_text_size(UI_TEXT_NAME)) * 0.5f, UI_ALIGN_LEFT,
			tag);
	if (row->platform > 0)
		ui_overlay_platform(row->platform, 11.0f, x + width - 14, y + (ROW_HEIGHT - 11.0f) * 0.5f, COLOR_LABEL, 0);
	target_add(x, y, x + width, y + ROW_HEIGHT, _target_row, index);
}

/* a button of the detail pane (an action) */
static void render_action(char const *label, float x, float y, float width, boolean on, unsigned int text,
	short index)
{
	ui_style_row(x, y, width, 24, on);
	ui_overlay_text(UI_FONT_MENU, 10.0f, x + 10, y + 7, UI_ALIGN_LEFT, text, label);
	target_add(x, y, x + width, y + 24, _target_action, index);
}

static void render_players(void)
{
	short count = (short)guide.players.count, visible, index;
	short chosen = guide.row[SOCIAL_SECTION_PLAYERS_HERE];
	char heading[48];
	float y = BODY_Y + 16;

	if (!count)
		return;
	snprintf(heading, sizeof(heading), "PLAYERS (%d)", count);
	ui_style_text(UI_TEXT_HEADING, CONTENT_LEFT, BODY_Y, UI_ALIGN_LEFT, heading);
	visible = (short)((BODY_BOTTOM - y + ROW_GAP) / (ROW_HEIGHT + ROW_GAP));
	if (chosen < guide.first_row)
		guide.first_row = chosen;
	else if (chosen >= guide.first_row + visible)
		guide.first_row = (short)(chosen - visible + 1);
	for (index = guide.first_row; index < count && index < guide.first_row + visible; index++)
	{
		render_row(&guide.players.rows[index], CONTENT_LEFT, y, LIST_WIDTH, index == chosen, index);
		y += ROW_HEIGHT + ROW_GAP;
	}
	/* the chosen player, and what can be done */
	if (chosen >= 0 && chosen < count)
	{
		struct social_row const *row = &guide.players.rows[chosen];
		float x = DETAIL_LEFT, width = CONTENT_RIGHT - DETAIL_LEFT;

		ui_style_glass(x, BODY_Y, width, BODY_BOTTOM - BODY_Y, 8, ui_style_color(UI_COLOR_PANE), COLOR_ROW_EDGE);
		ui_style_text(UI_TEXT_TITLE, x + 12, BODY_Y + 12, UI_ALIGN_LEFT, row->tag);
		if (row->platform > 0)
			ui_overlay_platform(row->platform, 13.0f, x + width - 18, BODY_Y + 12, COLOR_LABEL, 0);
		if (row->status[0])
			ui_style_text(row->muted ? UI_TEXT_WARNING : UI_TEXT_LINE, x + 12, BODY_Y + 32, UI_ALIGN_LEFT, row->status);
		/* (its options, or a report's step: A on the row opens them) */
		{
			short kinds[MAXIMUM_PLAYER_OPTIONS];
			char labels[MAXIMUM_PLAYER_OPTIONS][REPORT_PLAYER_CHOICE_SIZE];
			short choices = player_choices(chosen, kinds, labels), index;
			float option_y = BODY_Y + 54;
			char const *message = report_player_message();

			if (report_player_step() != _report_step_none)
			{
				ui_style_text(UI_TEXT_HEADING, x + 12, option_y, UI_ALIGN_LEFT, report_player_title());
				option_y += 20;
			}
			for (index = 0; index < choices; index++)
			{
				boolean on = guide.options && index == guide.option;

				ui_style_row(x + 12, option_y, width - 24, OPTION_HEIGHT, on);
				ui_overlay_text(UI_FONT_MENU, 10.0f, x + 22, option_y + 7, UI_ALIGN_LEFT, COLOR_VALUE, labels[index]);
				target_add(x + 12, option_y, x + width - 12, option_y + OPTION_HEIGHT, _target_option, index);
				option_y += OPTION_HEIGHT + OPTION_GAP;
			}
			if (guide.acting)
				ui_style_text(UI_TEXT_LINE, x + 12, option_y + 4, UI_ALIGN_LEFT, "Sending");
			else if (message[0])
				ui_style_text(UI_TEXT_LINE, x + 12, option_y + 4, UI_ALIGN_LEFT, message);
			else if (guide.message[0] && system_milliseconds() - guide.message_time < MESSAGE_TIME)
				ui_style_text(message_style(), x + 12, option_y + 4, UI_ALIGN_LEFT, guide.message);
		}
	}
}

static float item_height(short kind)
{
	return kind == _item_heading ? HEADING_HEIGHT : ROW_HEIGHT + ROW_GAP;
}

/* the chosen item in view (a heading over it too, where it fits) */
static void friends_scroll(void)
{
	short chosen = guide.row[SOCIAL_SECTION_FRIENDS];
	float room = BODY_BOTTOM - BODY_Y + ROW_GAP;

	if (chosen < 0 || chosen >= guide.item_count)
		return;
	if (guide.first_row > chosen)
		guide.first_row = chosen;
	if (guide.first_row == chosen && chosen > 0 && guide.items[chosen - 1].kind == _item_heading)
		guide.first_row = (short)(chosen - 1);
	for (;;)
	{
		float used = 0;
		short index;

		for (index = guide.first_row; index <= chosen; index++)
			used += item_height(guide.items[index].kind);
		if (used <= room || guide.first_row >= chosen)
			break;
		guide.first_row++;
	}
}

/* an item's row as the list draws it */
static void item_row(struct friend_item const *item, struct social_row *row)
{
	csmemset(row, 0, sizeof(*row));
	switch (item->kind)
	{
	case _item_outgoing:
	{
		struct social_request const *request = &friends_state.outgoing[item->index];

		social_copy_text(row->tag, sizeof(row->tag), social_name(request->display, request->handle));
		social_copy_text(row->status, sizeof(row->status), "Request sent");
		row->state = -1;
		break;
	}
	case _item_blocked:
		social_copy_text(row->tag, sizeof(row->tag), friends_state.blocked[item->index]);
		social_copy_text(row->status, sizeof(row->status), "Blocked");
		row->state = -1;
		break;
	case _item_invite:
	{
		struct social_invite const *invite = &friends_state.invites[item->index];

		social_copy_text(row->tag, sizeof(row->tag), social_name(invite->display, invite->from));
		if (social_game_line(&invite->game, row->status, sizeof(row->status)) < 0)
			social_copy_text(row->status, sizeof(row->status), "Invited you");
		row->state = SOCIAL_STATE_IN_GAME;
		break;
	}
	case _item_friend:
	{
		struct social_friend const *buddy = &friends_state.friends[item->index];

		social_copy_text(row->tag, sizeof(row->tag), social_name(buddy->display, buddy->handle));
		social_friend_status(buddy, row->status, sizeof(row->status));
		row->state = buddy->playing ? SOCIAL_STATE_IN_GAME : buddy->online ? SOCIAL_STATE_ONLINE : SOCIAL_STATE_OFFLINE;
		break;
	}
	case _item_request:
	{
		struct social_request const *request = &friends_state.requests[item->index];

		social_copy_text(row->tag, sizeof(row->tag), social_name(request->display, request->handle));
		social_copy_text(row->status, sizeof(row->status), "Friend request");
		/* (no dot: not a friend yet) */
		row->state = -1;
		break;
	}
	}
}

/* the chosen item's panel: its name, status, a line about its game, then
its options */
static void render_friend_detail(struct friend_item const *item)
{
	struct social_row row;
	float x = DETAIL_LEFT, width = CONTENT_RIGHT - DETAIL_LEFT, y;
	char line[SOCIAL_STATUS_SIZE];
	short options[MAXIMUM_OPTIONS], count, index;
	boolean busy;

	line[0] = 0;
	ui_style_glass(x, BODY_Y, width, BODY_BOTTOM - BODY_Y, 8, ui_style_color(UI_COLOR_PANE), COLOR_ROW_EDGE);
	if (item->kind == _item_offline)
	{
		snprintf(line, sizeof(line), "OFFLINE (%d)", item->index);
		ui_style_text(UI_TEXT_HEADING, x + 12, BODY_Y + 14, UI_ALIGN_LEFT, line);
		return;
	}
	item_row(item, &row);
	ui_style_text(UI_TEXT_TITLE, x + 12, BODY_Y + 12, UI_ALIGN_LEFT, row.tag);
	if (row.status[0])
		ui_style_text(UI_TEXT_LINE, x + 12, BODY_Y + 32, UI_ALIGN_LEFT, row.status);
	switch (item->kind)
	{
	case _item_invite:
		social_copy_text(line, sizeof(line), friends_state.invites[item->index].game.name);
		break;
	case _item_friend:
	{
		struct social_friend const *buddy = &friends_state.friends[item->index];
		time_t since = (time_t)buddy->since;
		struct tm *date = buddy->since > 0 ? localtime(&since) : NULL;

		if (buddy->playing && buddy->game.listed)
			social_copy_text(line, sizeof(line), buddy->game.name);
		else if (date)
			strftime(line, sizeof(line), "Friends since %b %d", date);
		break;
	}
	}
	if (line[0])
		ui_style_text(UI_TEXT_LABEL, x + 12, BODY_Y + 48, UI_ALIGN_LEFT, line);
	count = item_options(item, options);
	busy = guide.acting || guide.clan_acting || guide.party_invite_handle[0];
	y = BODY_Y + 68;
	for (index = 0; index < count; index++)
	{
		boolean on = guide.options && index == guide.option;

		ui_style_row(x + 12, y, width - 24, OPTION_HEIGHT, on);
		ui_overlay_text(UI_FONT_MENU, 10.0f, x + 22, y + 7, UI_ALIGN_LEFT, busy ? fade(COLOR_VALUE, 0.5f) : COLOR_VALUE,
			option_label(options[index], guide.armed == options[index]));
		target_add(x + 12, y, x + width - 12, y + OPTION_HEIGHT, _target_option, index);
		y += OPTION_HEIGHT + OPTION_GAP;
	}
	/* (what the last action came to: a refusal's line, "Invite sent") */
	if (busy)
		ui_style_text(UI_TEXT_LINE, x + 12, y + 4, UI_ALIGN_LEFT, "Sending");
	else if (guide.message[0] && system_milliseconds() - guide.message_time < MESSAGE_TIME)
		ui_style_text(message_style(), x + 12, y + 4, UI_ALIGN_LEFT, guide.message);
}

/* a friend's row's mark, at its right: IN PARTY, else INVITED while an
invite of this player's waits for them */
static void render_friend_mark(char const *handle, float y)
{
	unsigned long now = system_milliseconds();
	char const *mark = party_has_handle(handle) ? "IN PARTY" :
		social_sent_has(&guide.sent, handle, SOCIAL_SENT_PARTY, now) ||
		social_sent_has(&guide.sent, handle, SOCIAL_SENT_GAME, now) ? "INVITED" : NULL;

	if (mark)
	{
		ui_overlay_text(UI_FONT_MENU, 9.5f, CONTENT_LEFT + LIST_WIDTH - 14, y + (ROW_HEIGHT - 9.5f) * 0.5f, UI_ALIGN_RIGHT,
			ui_style_color(UI_COLOR_NAME), mark);
	}
}

static void render_friends(void)
{
	short index, chosen = guide.row[SOCIAL_SECTION_FRIENDS];
	float y = BODY_Y, center = (CONTENT_LEFT + CONTENT_RIGHT) * 0.5f;
	char heading[48];
	int online = social_online_count(&friends_state);

	if (!guide.friends_loaded)
	{
		ui_style_text(UI_TEXT_LINE, center, (BODY_Y + BODY_BOTTOM) * 0.5f - 6, UI_ALIGN_CENTER,
			guide.friends_status == BROWSER_SOCIAL_FAILED ? "Can't reach the site" : "Loading");
		return;
	}
	friends_scroll();
	for (index = guide.first_row; index < guide.item_count; index++)
	{
		struct friend_item const *item = &guide.items[index];
		float height = item_height(item->kind);

		if (y + height - (item->kind == _item_heading ? 0 : ROW_GAP) > BODY_BOTTOM)
			break;
		if (item->kind == _item_heading)
		{
			if (item->index == _heading_invites)
				snprintf(heading, sizeof(heading), "INVITES (%d)", invites_waiting());
			else if (item->index == _heading_online)
				snprintf(heading, sizeof(heading), "ONLINE (%d OF %d)", online, friends_state.friend_count);
			else if (item->index == _heading_sent)
				snprintf(heading, sizeof(heading), "REQUESTS SENT (%d)", friends_state.outgoing_count);
			else if (item->index == _heading_blocked)
				snprintf(heading, sizeof(heading), "BLOCKED (%d)", friends_state.blocked_count);
			else
				snprintf(heading, sizeof(heading), "REQUESTS (%d)", friends_state.request_count);
			ui_style_text(UI_TEXT_HEADING, CONTENT_LEFT, y + 2, UI_ALIGN_LEFT, heading);
		}
		else if (item->kind == _item_offline)
		{
			ui_style_row(CONTENT_LEFT, y, LIST_WIDTH, ROW_HEIGHT, index == chosen && !guide.options);
			snprintf(heading, sizeof(heading), "OFFLINE (%d)", item->index);
			ui_overlay_text(UI_FONT_MENU, 9.5f, CONTENT_LEFT + 12, y + (ROW_HEIGHT - 9.5f) * 0.5f, UI_ALIGN_LEFT,
				COLOR_LABEL, heading);
			ui_overlay_text(UI_FONT_MENU, 11.0f, CONTENT_LEFT + LIST_WIDTH - 14, y + (ROW_HEIGHT - 11.0f) * 0.5f,
				UI_ALIGN_RIGHT, COLOR_LABEL, guide.offline_open ? "-" : "+");
			target_add(CONTENT_LEFT, y, CONTENT_LEFT + LIST_WIDTH, y + ROW_HEIGHT, _target_row, index);
		}
		else
		{
			struct social_row row;

			item_row(item, &row);
			render_row(&row, CONTENT_LEFT, y, LIST_WIDTH, index == chosen && !guide.options, index);
			if (item->kind == _item_friend)
				render_friend_mark(friends_state.friends[item->index].handle, y);
		}
		y += height;
	}
	if (!guide.item_count)
		ui_style_text(UI_TEXT_LINE, CONTENT_LEFT + LIST_WIDTH * 0.5f, y + 14, UI_ALIGN_CENTER, "No friends yet");
	if (chosen >= 0 && chosen < guide.item_count)
		render_friend_detail(&guide.items[chosen]);
}

#ifdef HALO_PORT_VOICE_AUDIO
/* the voice channel's state now: open, or why not (voice_game.h) */
static int channel_state(void)
{
	return voice_game_channel_state(voice_game_channel());
}

/* the line under voice's rows: why the channel can't be used, else what
voice is */
static char const *voice_help(short setting)
{
	int state = channel_state();

	if (setting == _setting_voice_channel && state != _voice_channel_open)
		return voice_game_channel_reason(state);
	if (setting != _setting_voice_chat && state == _voice_channel_voice_off)
		return voice_game_channel_reason(state);
	if (setting == _setting_voice_chat && voice_microphone() == _voice_microphone_denied)
		return "The microphone isn't allowed. You can still listen.";
	if (setting == _setting_voice_chat && voice_microphone() == _voice_microphone_missing)
		return "No microphone found. You can still listen.";
	return "Voice goes through the game's host. Nothing is recorded.";
}

/* a voice setting's row (the channel tagged when it can't be used now) */
static void render_voice_row(short setting, float y, boolean on)
{
	char value[32];

	voice_setting_value(setting, value, sizeof(value));
	ui_style_setting(CONTENT_LEFT, y, CONTENT_RIGHT - CONTENT_LEFT, SETTING_HEIGHT, 1.0f, voice_setting_labels[setting],
		setting == _setting_voice_channel && channel_state() != _voice_channel_open ? "NOT AVAILABLE" : NULL, value, on);
}
#endif

/* the clan's emblem, size square at x, y (its pixels made again only when
it changes) */
static void render_emblem(float x, float y, float size)
{
	int const *fields = clan_profile.clan.emblem;
	struct clan_emblem emblem;
	int serial;

	emblem.foreground = fields[0];
	emblem.background = fields[1];
	emblem.primary = fields[2];
	emblem.secondary = fields[3];
	emblem.background_color = fields[4];
	serial = clan_emblem_serial(&emblem);
	if (!guide.emblem_pixels)
	{
		guide.emblem_pixels = malloc(EMBLEM_PIXELS * EMBLEM_PIXELS * 4);
		if (!guide.emblem_pixels)
			return;
		guide.emblem_serial = 0;
	}
	if (serial != guide.emblem_serial)
	{
		clan_emblem_render(&emblem, EMBLEM_PIXELS, guide.emblem_pixels);
		guide.emblem_serial = serial;
	}
	ui_overlay_picture(serial, guide.emblem_pixels, EMBLEM_PIXELS, EMBLEM_PIXELS, x, y, size, size, 0xFFFFFFFF);
}

/* a CLAN item's row as the list draws it */
static void clan_item_row(struct friend_item const *item, struct social_row *row)
{
	csmemset(row, 0, sizeof(*row));
	switch (item->kind)
	{
	case _clan_member:
	{
		struct delta_profile_clan_member const *member = &clan_profile.clan.members[item->index];

		social_copy_text(row->tag, sizeof(row->tag), social_name(member->display, member->handle));
		social_clan_status(member->rank, member->you, member->online, member->playing, &member->game, row->status,
			sizeof(row->status));
		row->state = member->playing ? SOCIAL_STATE_IN_GAME : member->you || member->online ? SOCIAL_STATE_ONLINE :
			SOCIAL_STATE_OFFLINE;
		break;
	}
	case _clan_invited:
		social_copy_text(row->tag, sizeof(row->tag), clan_profile.clan_invited[item->index]);
		social_copy_text(row->status, sizeof(row->status), "Invited");
		row->state = -1;
		break;
	case _clan_invite:
	{
		struct delta_profile_invite const *invite = &clan_profile.invites[item->index];

		snprintf(row->tag, sizeof(row->tag), "[%s] %s", invite->tag, invite->name);
		snprintf(row->status, sizeof(row->status), invite->count == 1 ? "%d member" : "%d members", invite->count);
		row->state = -1;
		break;
	}
	}
}

/* the clan's card in the detail pane: its emblem, tag and name, motto and
members */
static float render_clan_card(float x, float width)
{
	char line[SOCIAL_STATUS_SIZE];
	float size = 48;

	render_emblem(x + width - size - 12, BODY_Y + 12, size);
	snprintf(line, sizeof(line), "[%s]", clan_profile.clan.tag);
	ui_style_text(UI_TEXT_TITLE, x + 12, BODY_Y + 12, UI_ALIGN_LEFT, line);
	ui_style_text(UI_TEXT_NAME, x + 12, BODY_Y + 32, UI_ALIGN_LEFT, clan_profile.clan.name);
	if (clan_profile.clan.motto[0])
		ui_style_text(UI_TEXT_LINE, x + 12, BODY_Y + 48, UI_ALIGN_LEFT, clan_profile.clan.motto);
	snprintf(line, sizeof(line), clan_profile.clan.count == 1 ? "%d member" : "%d members", clan_profile.clan.count);
	ui_style_text(UI_TEXT_LABEL, x + 12, BODY_Y + 66, UI_ALIGN_LEFT, line);
	return BODY_Y + 88;
}

static void render_clan_detail(struct friend_item const *item)
{
	struct social_row row;
	float x = DETAIL_LEFT, width = CONTENT_RIGHT - DETAIL_LEFT, y;
	short options[MAXIMUM_OPTIONS], count = 0, index;

	ui_style_glass(x, BODY_Y, width, BODY_BOTTOM - BODY_Y, 8, ui_style_color(UI_COLOR_PANE), COLOR_ROW_EDGE);
	/* (the clan's card: on its actions, and on the player's own line) */
	if (!item || item->kind == _clan_action ||
		(item->kind == _clan_member && clan_profile.clan.members[item->index].you))
	{
		y = clan_in() ? render_clan_card(x, width) : BODY_Y + 12;
	}
	else
	{
		clan_item_row(item, &row);
		ui_style_text(UI_TEXT_TITLE, x + 12, BODY_Y + 12, UI_ALIGN_LEFT, row.tag);
		if (row.status[0])
			ui_style_text(UI_TEXT_LINE, x + 12, BODY_Y + 32, UI_ALIGN_LEFT, row.status);
		if (item->kind == _clan_member && clan_profile.clan.members[item->index].handle[0])
		{
			char handle[SOCIAL_HANDLE_SIZE + 2];

			snprintf(handle, sizeof(handle), "@%s", clan_profile.clan.members[item->index].handle);
			ui_style_text(UI_TEXT_LABEL, x + 12, BODY_Y + 48, UI_ALIGN_LEFT, handle);
		}
		count = clan_item_options(item, options);
		y = BODY_Y + 68;
	}
	for (index = 0; index < count; index++)
	{
		boolean on = guide.options && index == guide.option;

		ui_style_row(x + 12, y, width - 24, OPTION_HEIGHT, on);
		ui_overlay_text(UI_FONT_MENU, 10.0f, x + 22, y + 7, UI_ALIGN_LEFT,
			guide.clan_acting ? fade(COLOR_VALUE, 0.5f) : COLOR_VALUE, option_label(options[index], guide.armed == options[index]));
		target_add(x + 12, y, x + width - 12, y + OPTION_HEIGHT, _target_option, index);
		y += OPTION_HEIGHT + OPTION_GAP;
	}
	if (guide.clan_acting)
		ui_style_text(UI_TEXT_LINE, x + 12, y + 4, UI_ALIGN_LEFT, "Sending");
	else if (guide.message[0] && system_milliseconds() - guide.message_time < MESSAGE_TIME)
		ui_style_text(message_style(), x + 12, y + 4, UI_ALIGN_LEFT, guide.message);
}

static void render_clan(void)
{
	short index, chosen = guide.row[SOCIAL_SECTION_CLAN];
	float y = BODY_Y, center = (CONTENT_LEFT + CONTENT_RIGHT) * 0.5f;
	char heading[48];

	if (!guide.clan_loaded)
	{
		ui_style_text(UI_TEXT_LINE, center, (BODY_Y + BODY_BOTTOM) * 0.5f - 6, UI_ALIGN_CENTER,
			guide.clan_state == BROWSER_PROFILE_FAILED ? "Can't reach the site" : "Loading");
		return;
	}
	if (!guide.clan_count)
	{
		char page[160];
		unsigned char modules[BROWSER_CONNECT_QR_SIZE * BROWSER_CONNECT_QR_SIZE];

		ui_style_text(UI_TEXT_LINE, center, (BODY_Y + BODY_BOTTOM) * 0.5f - 16, UI_ALIGN_CENTER, "No clan");
		if (browser_page_qr("/clans", page, sizeof(page), modules) > 0)
		{
			char line[200];

			snprintf(line, sizeof(line), "Start one at %s", page);
			ui_style_text(UI_TEXT_LABEL, center, (BODY_Y + BODY_BOTTOM) * 0.5f + 4, UI_ALIGN_CENTER, line);
		}
		return;
	}
	/* (the chosen item in view, as FRIENDS scrolls) */
	if (guide.first_row > chosen)
		guide.first_row = chosen;
	if (guide.first_row == chosen && chosen > 0 && guide.clan_items[chosen - 1].kind == _clan_heading)
		guide.first_row = (short)(chosen - 1);
	for (;;)
	{
		float used = 0;

		for (index = guide.first_row; index <= chosen && index < guide.clan_count; index++)
			used += item_height(guide.clan_items[index].kind == _clan_heading ? _item_heading : _item_friend);
		if (used <= BODY_BOTTOM - BODY_Y + ROW_GAP || guide.first_row >= chosen)
			break;
		guide.first_row++;
	}
	for (index = guide.first_row; index < guide.clan_count; index++)
	{
		struct friend_item const *item = &guide.clan_items[index];
		float height = item_height(item->kind == _clan_heading ? _item_heading : _item_friend);
		boolean on = index == chosen && !guide.options;

		if (y + height - (item->kind == _clan_heading ? 0 : ROW_GAP) > BODY_BOTTOM)
			break;
		if (item->kind == _clan_heading)
		{
			if (item->index == _clan_heading_members)
				snprintf(heading, sizeof(heading), "MEMBERS (%d)", clan_profile.clan.count);
			else if (item->index == _clan_heading_invited)
				snprintf(heading, sizeof(heading), "INVITES OUT (%d)", clan_profile.clan_invited_count);
			else
				snprintf(heading, sizeof(heading), "CLAN INVITES (%d)", clan_profile.invite_count);
			ui_style_text(UI_TEXT_HEADING, CONTENT_LEFT, y + 2, UI_ALIGN_LEFT, heading);
		}
		else if (item->kind == _clan_action)
		{
			boolean leave = item->index == _clan_action_leave;

			ui_style_row(CONTENT_LEFT, y, LIST_WIDTH, ROW_HEIGHT, on);
			ui_overlay_text(UI_FONT_MENU, 10.0f, CONTENT_LEFT + 12, y + (ROW_HEIGHT - 10.0f) * 0.5f, UI_ALIGN_LEFT,
				leave && guide.armed == ARMED_LEAVE ? COLOR_MUTED : COLOR_VALUE,
				!leave ? "INVITE FRIENDS" : guide.armed == ARMED_LEAVE ? "CONFIRM LEAVE" : "LEAVE CLAN");
			target_add(CONTENT_LEFT, y, CONTENT_LEFT + LIST_WIDTH, y + ROW_HEIGHT, _target_row, index);
		}
		else
		{
			struct social_row row;

			clan_item_row(item, &row);
			render_row(&row, CONTENT_LEFT, y, LIST_WIDTH, on, index);
		}
		y += height;
	}
	render_clan_detail(clan_chosen_item());
}

/* RECENT: the players, newest first; the chosen one's panel and options */
static void render_recent(void)
{
	short count = guide.recent_count, visible, index, chosen = guide.row[SOCIAL_SECTION_RECENT];
	float y = BODY_Y + 16, x = DETAIL_LEFT, width = CONTENT_RIGHT - DETAIL_LEFT;
	long long now = (long long)time(NULL);
	char heading[48];

	if (!guide.friends_loaded)
	{
		ui_style_text(UI_TEXT_LINE, (CONTENT_LEFT + CONTENT_RIGHT) * 0.5f, (BODY_Y + BODY_BOTTOM) * 0.5f - 6,
			UI_ALIGN_CENTER, guide.friends_status == BROWSER_SOCIAL_FAILED ? "Can't reach the site" : "Loading");
		return;
	}
	if (!count)
	{
		ui_style_text(UI_TEXT_LINE, CONTENT_LEFT + LIST_WIDTH * 0.5f, (BODY_Y + BODY_BOTTOM) * 0.5f - 6, UI_ALIGN_CENTER,
			"No recent players");
		return;
	}
	snprintf(heading, sizeof(heading), "RECENT PLAYERS (%d)", count);
	ui_style_text(UI_TEXT_HEADING, CONTENT_LEFT, BODY_Y, UI_ALIGN_LEFT, heading);
	visible = (short)((BODY_BOTTOM - y + ROW_GAP) / (ROW_HEIGHT + ROW_GAP));
	if (chosen < guide.first_row)
		guide.first_row = chosen;
	else if (chosen >= guide.first_row + visible)
		guide.first_row = (short)(chosen - visible + 1);
	for (index = guide.first_row; index < count && index < guide.first_row + visible; index++)
	{
		struct social_recent const *player = &guide.recent[index];
		struct social_row row;

		csmemset(&row, 0, sizeof(row));
		social_copy_text(row.tag, sizeof(row.tag), social_name(player->display, player->handle));
		social_recent_status(player, now, row.status, sizeof(row.status));
		/* (no dot: who's online is for friends) */
		row.state = -1;
		render_row(&row, CONTENT_LEFT, y, LIST_WIDTH, index == chosen && !guide.recent_options, index);
		y += ROW_HEIGHT + ROW_GAP;
	}
	/* the chosen player: their name, the last game, and the options */
	if (chosen >= 0 && chosen < count)
	{
		struct social_recent const *player = &guide.recent[chosen];
		short options[MAXIMUM_RECENT_OPTIONS], option_count = recent_option_list(player, options);
		char line[SOCIAL_STATUS_SIZE];

		ui_style_glass(x, BODY_Y, width, BODY_BOTTOM - BODY_Y, 8, ui_style_color(UI_COLOR_PANE), COLOR_ROW_EDGE);
		ui_style_text(UI_TEXT_TITLE, x + 12, BODY_Y + 12, UI_ALIGN_LEFT, social_name(player->display, player->handle));
		social_recent_status(player, now, line, sizeof(line));
		if (line[0])
			ui_style_text(UI_TEXT_LINE, x + 12, BODY_Y + 32, UI_ALIGN_LEFT, line);
		if (player->is_friend)
			ui_style_text(UI_TEXT_LABEL, x + 12, BODY_Y + 48, UI_ALIGN_LEFT, "Friend");
		else if (player->requested)
			ui_style_text(UI_TEXT_LABEL, x + 12, BODY_Y + 48, UI_ALIGN_LEFT, "Request sent");
		y = BODY_Y + 68;
		for (index = 0; index < option_count; index++)
		{
			boolean on = guide.recent_options && index == guide.recent_option;
			unsigned int color = options[index] == _recent_block && guide.recent_armed == _recent_block ? COLOR_MUTED :
				COLOR_VALUE;

			ui_style_row(x + 12, y, width - 24, OPTION_HEIGHT, on);
			ui_overlay_text(UI_FONT_MENU, 10.0f, x + 22, y + 7, UI_ALIGN_LEFT, guide.acting ? fade(color, 0.5f) : color,
				recent_option_label(options[index], guide.recent_armed == options[index]));
			target_add(x + 12, y, x + width - 12, y + OPTION_HEIGHT, _target_option, index);
			y += OPTION_HEIGHT + OPTION_GAP;
		}
		if (guide.acting)
			ui_style_text(UI_TEXT_LINE, x + 12, y + 4, UI_ALIGN_LEFT, "Sending");
		else if (guide.message[0] && system_milliseconds() - guide.message_time < MESSAGE_TIME)
			ui_style_text(message_style(), x + 12, y + 4, UI_ALIGN_LEFT, guide.message);
	}
}

static void render_settings(void)
{
	short setting, chosen = guide.row[SOCIAL_SECTION_SETTINGS];
	float y = BODY_Y;
	/* (the rows that fit above the help line, scrolled to the chosen one) */
	short shown = (short)((BODY_BOTTOM - BODY_Y - 18) / (SETTING_HEIGHT + ROW_GAP));
	short rows = settings_rows();
	short first = guide.settings_first;

	/* (the window moves only when the chosen row leaves it) */
	if (chosen < first)
		first = chosen;
	else if (chosen >= first + shown)
		first = (short)(chosen - shown + 1);
	if (first > rows - shown)
		first = (short)(rows - shown);
	if (first < 0)
		first = 0;
	guide.settings_first = first;
	for (setting = first; setting < rows && setting < first + shown; setting++)
	{
		struct settings_row const *row = settings_row_at(SETTINGS_GROUP_GUIDE, setting);
		boolean on = setting == chosen;

		if (row)
		{
			char value[96];

			settings_row_value(row, value, sizeof(value));
			if (!settings_row_arrows(row))
				ui_style_setting_plain();
			ui_style_setting(CONTENT_LEFT, y, CONTENT_RIGHT - CONTENT_LEFT, SETTING_HEIGHT, 1.0f, settings_row_label(row),
				NULL, value, on);
		}
#ifdef HALO_PORT_VOICE_AUDIO
		else if (voice_setting_of(setting) != NONE)
			render_voice_row(voice_setting_of(setting), y, on);
#endif
		else
		{
			ui_style_setting(CONTENT_LEFT, y, CONTENT_RIGHT - CONTENT_LEFT, SETTING_HEIGHT, 1.0f,
				extra_row_label(setting), NULL, NULL, on);
		}
		target_add(CONTENT_LEFT, y, CONTENT_RIGHT, y + SETTING_HEIGHT, _target_setting, setting);
		y += SETTING_HEIGHT + ROW_GAP;
	}
	/* (more rows than show: a slim bar in the panel's edge beside them, where
	no row's value is) */
	if (shown < rows)
	{
		float track_top = BODY_Y, track_height = shown * (SETTING_HEIGHT + ROW_GAP) - ROW_GAP;
		float thumb_height = MAX(12.0f, track_height * shown / rows);
		float thumb_top = track_top + (track_height - thumb_height) * first / MAX(1, rows - shown);

		ui_overlay_rect(CONTENT_RIGHT + 6, track_top, 3, track_height, 1.5f, 0xFFFFFF1A);
		ui_overlay_rect(CONTENT_RIGHT + 6, thumb_top, 3, thumb_height, 1.5f, COLOR_LABEL);
	}
	if (guide.message[0] && system_milliseconds() - guide.message_time < MESSAGE_TIME)
		ui_style_text(UI_TEXT_LINE, CONTENT_LEFT + 12, y + 4, UI_ALIGN_LEFT, guide.message);
	/* (STEAM ACCOUNT's: where its sign-in is kept, when that's worth saying) */
	else if (settings_row_at(SETTINGS_GROUP_GUIDE, chosen) &&
		!strcmp(settings_row_key(settings_row_at(SETTINGS_GROUP_GUIDE, chosen)), "game.workshop_steam") &&
		guide_workshop_link_note())
	{
		ui_style_text(UI_TEXT_LINE, CONTENT_LEFT, BODY_BOTTOM - 14, UI_ALIGN_LEFT, guide_workshop_link_note());
	}
#ifdef HALO_PORT_VOICE_AUDIO
	else if (voice_setting_of(chosen) != NONE)
		ui_style_text(UI_TEXT_LINE, CONTENT_LEFT, BODY_BOTTOM - 14, UI_ALIGN_LEFT, voice_help(voice_setting_of(chosen)));
#endif
}

enum
{
	PARTY_ROW_HEIGHT = 28,
	PARTY_ROW_GAP = 3,
	PARTY_SLOTS_HEIGHT = 26,
	COLOR_LEAVE = 0xFF9F9FFF,
};

/* JOIN WITH A CODE: the six letters, the one being picked lit */
static void render_code_entry(float x, float y, float width)
{
	float box = 26, gap = 6, dash = 10, left;
	short index;

	ui_style_text(UI_TEXT_LABEL, x, y, UI_ALIGN_LEFT, "PARTY CODE");
	y += 20;
	left = x + (width - (box * SOCIAL_PARTY_CODE_LENGTH + gap * (SOCIAL_PARTY_CODE_LENGTH - 2) + dash)) * 0.5f;
	for (index = 0; index < SOCIAL_PARTY_CODE_LENGTH; index++)
	{
		char letter[2];

		letter[0] = guide.code[index] ? guide.code[index] : ' ';
		letter[1] = 0;
		ui_style_row(left, y, box, 30, index == guide.code_cursor);
		ui_overlay_text(UI_FONT_MENU, 14.0f, left + box * 0.5f, y + 8, UI_ALIGN_CENTER, COLOR_VALUE, letter);
		left += box + (index == 2 ? dash : gap);
		if (index == 2)
			ui_overlay_text(UI_FONT_MENU, 14.0f, left - dash * 0.5f - gap * 0.5f, y + 8, UI_ALIGN_CENTER, COLOR_LABEL, "-");
	}
}

/* the party's link (halo://party/CODE) as a QR code, made once a code */
static void render_party_qr(float left, float top)
{
	char link[SOCIAL_PARTY_LINK_SIZE];
	float module;
	short x, y, run, size;

	if (strcmp(guide.qr_code, guide.party.code))
	{
		social_copy_text(guide.qr_code, sizeof(guide.qr_code), guide.party.code);
		guide.qr_size = social_party_link(guide.party.code, link, sizeof(link)) ? browser_text_qr(link, guide.qr) : 0;
	}
	size = (short)guide.qr_size;
	if (size <= 0 || top + PARTY_QR_WIDTH > BODY_BOTTOM)
		return;
	/* (black on white in every theme, with the quiet edge readers need) */
	module = (float)PARTY_QR_WIDTH / (size + 8);
	ui_overlay_rect(left, top, PARTY_QR_WIDTH, PARTY_QR_WIDTH, 4, 0xFFFFFFFF);
	for (y = 0; y < size; y++)
	{
		for (x = 0; x < size; x = (short)(x + run))
		{
			for (run = 0; x + run < size && guide.qr[y * size + x + run] == guide.qr[y * size + x]; run++)
				;
			if (guide.qr[y * size + x])
			{
				ui_overlay_rect(left + (x + 4) * module, top + (y + 4) * module, run * module + 0.3f, module + 0.3f, 0,
					0x000000FF);
			}
		}
	}
}

/* a PARTY row's one line, centered on its dot and icon: the name, and its
note ("Leader", "You", "Party of 3") right after it as a small tag in the
label color; the name cut short (with "...") before the tag would reach
right, the icon's side */
static void party_row_line(float x, float y, float right, char const *name, char const *note)
{
	/* (UI_TEXT_NAME's and UI_TEXT_LABEL's fonts, ui_overlay_style.c) */
	float const name_size = ui_style_text_size(UI_TEXT_NAME), note_size = ui_style_text_size(UI_TEXT_LABEL), gap = 6.0f;
	float const name_y = y + (PARTY_ROW_HEIGHT - name_size) * 0.5f;
	float note_width = note[0] ? ui_overlay_text_width(UI_FONT_MENU_SMALL, note_size, note) : 0.0f;
	float room = right - x - (note[0] ? gap + note_width : 0.0f), name_width;
	char shown[SOCIAL_TAG_SIZE + 4];

	/* (a name of letters the menus' fonts lack: its ASCII letters, or "Player") */
	ui_overlay_name_shown(UI_FONT_MENU, name, shown, sizeof(shown));
	if (ui_overlay_text_width(UI_FONT_MENU, name_size, shown) > room)
	{
		size_t length = strlen(shown);

		/* (a character off at a time, never half of one in UTF-8) */
		while (length > 0)
		{
			do
				length--;
			while (length > 0 && ((unsigned char)shown[length] & 0xC0) == 0x80);
			memcpy(shown + length, "...", 4);
			if (ui_overlay_text_width(UI_FONT_MENU, name_size, shown) <= room)
				break;
		}
	}
	name_width = ui_style_text(UI_TEXT_NAME, x, name_y, UI_ALIGN_LEFT, shown);
	/* (the tag on the name's baseline) */
	if (note[0])
		ui_style_text(UI_TEXT_LABEL, x + name_width + gap, name_y + (name_size - note_size) * 0.8f, UI_ALIGN_LEFT, note);
}

static void render_party(void)
{
	struct party_item items[MAXIMUM_PARTY_ITEMS];
	short count, index, chosen;
	float y = BODY_Y, x = DETAIL_LEFT, width = CONTENT_RIGHT - DETAIL_LEFT, action_y = BODY_Y + 18;
	char text[96], code[16];
	boolean any_list = FALSE;

	/* (the list as it is now, the row on the item it was on, and as drawn
	for the taps) */
	party_selection_keep();
	party_drawn_remember();
	count = party_items(items);
	chosen = guide.row[SOCIAL_SECTION_PARTY];
	if (chosen >= count)
		chosen = guide.row[SOCIAL_SECTION_PARTY] = (short)MAX(0, count - 1);
	/* (the list: the members, or the party invites) */
	if (guide.party.has)
	{
		snprintf(text, sizeof(text), "YOUR PARTY (%d OF %d)", guide.party.member_count, guide.party.largest);
		ui_style_text(UI_TEXT_HEADING, CONTENT_LEFT, y, UI_ALIGN_LEFT, text);
	}
	else if (friends_state.party_invite_count && guide.friends_loaded && friends_signed_in())
		ui_style_text(UI_TEXT_HEADING, CONTENT_LEFT, y, UI_ALIGN_LEFT, "PARTY INVITES");
	y += 18;
	for (index = 0; index < count; index++)
	{
		struct social_row row;
		boolean on = index == chosen && !guide.code_entry;

		if (party_item_button(&items[index]))
			continue;
		any_list = TRUE;
		csmemset(&row, 0, sizeof(row));
		if (items[index].kind == _party_item_member)
		{
			struct social_party_member const *member = &guide.party.members[items[index].index];

			social_copy_text(row.tag, sizeof(row.tag), member->name);
			social_copy_text(row.status, sizeof(row.status), member_note(member));
			row.platform = platform_number(member->platform);
			row.state = SOCIAL_STATE_ONLINE;
			on = on || (guide.party_member == items[index].index);
		}
		else
		{
			struct social_party_invite const *invite = &friends_state.party_invites[items[index].index];

			social_copy_text(row.tag, sizeof(row.tag), social_name(invite->display, invite->from));
			snprintf(row.status, sizeof(row.status), "Party of %d", invite->size);
			row.state = -1;
		}
		ui_style_row(CONTENT_LEFT, y, LIST_WIDTH, PARTY_ROW_HEIGHT, on);
		if (row.state >= 0)
			dot_draw(CONTENT_LEFT + 9, y + PARTY_ROW_HEIGHT * 0.5f - 3, row.state);
		party_row_line(CONTENT_LEFT + 22, y, CONTENT_LEFT + LIST_WIDTH - (row.platform > 0 ? 22 : 10), row.tag,
			row.status);
		if (row.platform > 0)
		{
			ui_overlay_platform(row.platform, 11.0f, CONTENT_LEFT + LIST_WIDTH - 14, y + (PARTY_ROW_HEIGHT - 11.0f) * 0.5f,
				COLOR_LABEL, 0);
		}
		target_add(CONTENT_LEFT, y, CONTENT_LEFT + LIST_WIDTH, y + PARTY_ROW_HEIGHT, _target_row, index);
		y += PARTY_ROW_HEIGHT + PARTY_ROW_GAP;
	}
	/* (the open slots, and the code that lets players in) */
	if (guide.party.has && guide.party.member_count < guide.party.largest)
	{
		int open = guide.party.largest - guide.party.member_count;

		social_party_code_shown(guide.party.code, code, sizeof(code));
		if (!strcmp(guide.party.who_can_join, "invite"))
			snprintf(text, sizeof(text), "%d open slot%s \xC2\xB7 Invite only", open, open == 1 ? "" : "s");
		else
			snprintf(text, sizeof(text), "%d open slot%s \xC2\xB7 Party code %s", open, open == 1 ? "" : "s", code);
		ui_style_glass(CONTENT_LEFT, y, LIST_WIDTH, PARTY_SLOTS_HEIGHT, 8, ui_style_color(UI_COLOR_PANE), COLOR_ROW_EDGE);
		ui_style_text(UI_TEXT_LABEL, CONTENT_LEFT + 12, y + (PARTY_SLOTS_HEIGHT - ui_style_text_size(UI_TEXT_LABEL)) * 0.5f,
			UI_ALIGN_LEFT, text);
	}
	else if (!any_list && !guide.party.has)
	{
		ui_style_text(UI_TEXT_LINE, CONTENT_LEFT + LIST_WIDTH * 0.5f, (BODY_Y + BODY_BOTTOM) * 0.5f - 6, UI_ALIGN_CENTER,
			"Not in a party");
	}

	/* (the buttons, or the code being picked) */
	if (guide.party_member != NONE && guide.party_member < guide.party.member_count)
		ui_style_text(UI_TEXT_HEADING, x, BODY_Y, UI_ALIGN_LEFT, guide.party.members[guide.party_member].name);
	else
		ui_style_text(UI_TEXT_HEADING, x, BODY_Y, UI_ALIGN_LEFT, "PARTY");
	if (guide.code_entry)
	{
		render_code_entry(x, action_y, width);
		action_y += 64;
	}
	else
	{
		for (index = 0; index < count; index++)
		{
			short action = items[index].index;
			boolean on = index == chosen;
			unsigned int color = action == _party_action_leave ? COLOR_LEAVE : COLOR_VALUE;
			char const *value = NULL, *label;
			unsigned int value_color = ui_style_color(UI_COLOR_NAME);

			if (!party_item_button(&items[index]))
				continue;
			label = items[index].kind == _party_item_voice ? "VOICE" : party_action_label(action);
			if (items[index].kind == _party_item_voice)
			{
#ifdef HALO_PORT_VOICE_AUDIO
				/* (the channel talked on; dimmed when it can't be used now,
				SETTINGS saying why) */
				value = voice_game_channel_name(voice_game_channel());
				if (voice_game_channel_state(voice_game_channel()) != _voice_channel_open)
					value_color = COLOR_DIMMED;
#endif
			}
			else if (action == _party_action_bring)
				value = config_boolean("delta.bring_party") ? "ON" : "OFF";
			else if (action == _party_action_follow)
			{
				int mode = social_follow_mode(config_string("delta.follow_leader"));

				value = mode == SOCIAL_FOLLOW_ALWAYS ? "ALWAYS" : mode == SOCIAL_FOLLOW_NEVER ? "NEVER" : "ASK";
			}
			else if (action == _party_action_who)
			{
				value = social_party_rule_shown(guide.party.who_can_join);
				/* (the leader's to change) */
				if (!guide.party.leader)
					color = fade(color, 0.55f);
			}
			if (guide.party_acting)
				color = fade(color, 0.5f);
			ui_style_row(x, action_y, width, OPTION_HEIGHT, on);
			ui_overlay_text(UI_FONT_MENU, 10.0f, x + 10, action_y + 7, UI_ALIGN_LEFT, color, label);
			if (value)
				ui_overlay_text(UI_FONT_MENU, 10.0f, x + width - 10, action_y + 7, UI_ALIGN_RIGHT, value_color, value);
			target_add(x, action_y, x + width, action_y + OPTION_HEIGHT, _target_row, index);
			action_y += OPTION_HEIGHT + OPTION_GAP;
		}
	}
	if (guide.party_acting)
		ui_style_text(UI_TEXT_LINE, x, action_y + 4, UI_ALIGN_LEFT, "Sending");
	else if (guide.party_message[0] && system_milliseconds() - guide.party_message_time < MESSAGE_TIME)
		ui_style_text(UI_TEXT_WARNING, x, action_y + 4, UI_ALIGN_LEFT, guide.party_message);
	/* (COPY PARTY LINK chosen: its link as a QR code too) */
	if (!guide.code_entry && chosen >= 0 && chosen < count && items[chosen].kind == _party_item_action &&
		items[chosen].index == _party_action_copy_link)
	{
		render_party_qr(x + width - PARTY_QR_WIDTH, action_y + 2);
	}
}

/* following the leader: the offer, its seconds and its two buttons, over
the menus or a game */
static void render_follow(void)
{
	struct ui_style_prompt prompts[2];
	float starts[2], ends[2], width = 300, height = 56, left = 320 - width * 0.5f, top = TOAST_GAME_Y - 22;
	unsigned long age = system_milliseconds() - guide.follow_time;
	long seconds = (long)((SOCIAL_FOLLOW_TIME - (age < SOCIAL_FOLLOW_TIME ? age : SOCIAL_FOLLOW_TIME) + 999) / 1000);
	char line[48];
	short index;

	guide.target_count = 0;
	ui_overlay_layer(4);
	ui_style_clock((unsigned int)system_milliseconds());
	ui_style_glass(left, top, width, height, 8, ui_style_color(UI_COLOR_GLASS_TOP), COLOR_PANEL_EDGE);
	ui_style_text(UI_TEXT_NAME, left + 12, top + 7, UI_ALIGN_LEFT, guide.follow_line);
	if (guide.follow_party[0])
		social_copy_text(line, sizeof(line), "Only join parties from people you know");
	else
		snprintf(line, sizeof(line), follow_timeout_joins() ? "Joining in %ld" : "Staying in %ld", seconds);
	ui_style_text(UI_TEXT_STATUS, left + 12, top + 22, UI_ALIGN_LEFT, line);
	prompts[0].button = UI_BUTTON_A;
	prompts[0].key = "ENTER";
	prompts[0].word = guide.follow_party[0] ? "JOIN" : "JOIN NOW";
	prompts[1].button = UI_BUTTON_B;
	prompts[1].key = "ESC";
	prompts[1].word = guide.follow_party[0] ? "NO" : "STAY";
	ui_style_prompts(left + width - 10, top + height - 19, 12.0f, prompts, 2, starts, ends);
	for (index = 0; index < 2; index++)
	{
		target_add(starts[index] - 4, top + height - 22, ends[index] + 4, top + height - 2, _target_prompt,
			(short)prompts[index].button);
	}
	ui_overlay_layer(0);
}

/* FRIENDS, PARTY and RECENT: SIGN IN without a Delta profile; nothing yet
with one (the site's friends come next) */
static void render_coming(void)
{
	float center = (CONTENT_LEFT + CONTENT_RIGHT) * 0.5f, width = 140;
	float y = (BODY_Y + BODY_BOTTOM) * 0.5f - 24;

	if (friends_signed_in() || sign_in_section() == NONE)
		return;
	if (guide.signing_in)
	{
		struct browser_connect const *connect = &guide.connect;
		char text[160];

		switch (connect->state)
		{
		case BROWSER_CONNECT_WAITING:
			snprintf(text, sizeof(text), "Enter this code at %s", connect->page);
			ui_style_text(UI_TEXT_LINE, center, y - 18, UI_ALIGN_CENTER, text);
			ui_style_row(center - 110, y + 2, 220, 50, TRUE);
			ui_overlay_text_glow(UI_FONT_MENU, 26.0f, center, y + 10, UI_ALIGN_CENTER, 0xFFFFFFFF, COLOR_PROMPT_GLOW,
				connect->code);
			snprintf(text, sizeof(text), "Expires in %d:%02d", connect->seconds / 60, connect->seconds % 60);
			ui_style_text(UI_TEXT_LINE, center, y + 62, UI_ALIGN_CENTER, text);
			break;
		case BROWSER_CONNECT_CONFIRM:
			snprintf(text, sizeof(text), connect->previous[0] ? "Sign in as @%s (from @%s)?" : "Sign in as @%s?",
				connect->handle, connect->previous);
			ui_overlay_text_glow(UI_FONT_MENU, 14.0f, center, y + 10, UI_ALIGN_CENTER, 0xFFFFFFFF, COLOR_PROMPT_GLOW, text);
			if (connect->answered)
				ui_style_text(UI_TEXT_LINE, center, y + 40, UI_ALIGN_CENTER, "Sending your answer\xE2\x80\xA6");
			break;
		case BROWSER_CONNECT_CONNECTED:
			snprintf(text, sizeof(text), "Signed in as @%s", connect->handle);
			ui_overlay_text_glow(UI_FONT_MENU, 14.0f, center, y + 10, UI_ALIGN_CENTER, 0xFFFFFFFF, COLOR_PROMPT_GLOW, text);
			break;
		case BROWSER_CONNECT_EXPIRED:
		case BROWSER_CONNECT_DECLINED:
		case BROWSER_CONNECT_FAILED:
			ui_style_text(UI_TEXT_LINE, center, y + 10, UI_ALIGN_CENTER,
				connect->state == BROWSER_CONNECT_EXPIRED ? "The code expired." :
				connect->state == BROWSER_CONNECT_DECLINED ? "Not signed in." : connect->message);
			break;
		default:
			ui_style_text(UI_TEXT_LINE, center, y + 10, UI_ALIGN_CENTER, "Getting a code\xE2\x80\xA6");
			break;
		}
		return;
	}
	ui_style_text(UI_TEXT_LINE, center, y, UI_ALIGN_CENTER, "Sign in to Delta to see friends");
	ui_style_row(center - width * 0.5f, y + 22, width, 26, TRUE);
	ui_overlay_text_glow(UI_FONT_MENU, 11.0f, center, y + 29, UI_ALIGN_CENTER, 0xFFFFFFFF, COLOR_PROMPT_GLOW,
		"SIGN IN");
	target_add(center - width * 0.5f, y + 22, center + width * 0.5f, y + 48, _target_sign_in, 0);
}

/* FRIENDS' shortcuts on the chosen item: Y joins, X invites */
static short shortcut_option(unsigned int button)
{
	struct friend_item const *item = chosen_item();
	short option = button == GUIDE_INPUT_Y ? _option_join : _option_invite;

	if (guide.section != SOCIAL_SECTION_FRIENDS || !friends_signed_in() || !guide.friends_loaded || guide.options ||
		!item || !item_has_option(item, option))
	{
		return NONE;
	}
	return option;
}

/* SIGN IN's A: its word as the sign-in goes (NULL: none now); B goes back
while signing in */
static char const *sign_in_word(boolean *back)
{
	if (!guide.signing_in)
		return "SIGN IN";
	*back = TRUE;
	switch (guide.connect.state)
	{
	case BROWSER_CONNECT_CONFIRM:
		return guide.connect.answered ? NULL : "YES";
	case BROWSER_CONNECT_CONNECTED:
		return "DONE";
	case BROWSER_CONNECT_EXPIRED:
	case BROWSER_CONNECT_DECLINED:
	case BROWSER_CONNECT_FAILED:
		return "NEW CODE";
	default:
		return NULL;
	}
}

static void prompt_add(struct ui_style_prompt *prompts, short *count, int button, char const *key, char const *word)
{
	prompts[*count].button = button;
	prompts[*count].key = key;
	prompts[(*count)++].word = word;
}

static void render_prompts(void)
{
	struct ui_style_prompt prompts[4];
	float starts[4], ends[4];
	char const *word = NULL;
	short count = 0, index;
	boolean back = FALSE, add_friend = FALSE;

	if (guide.about)
	{
		word = about_a_word(&guide.about_view);
		if (word)
			prompt_add(prompts, &count, UI_BUTTON_A, "ENTER", word);
		prompt_add(prompts, &count, UI_BUTTON_B, "ESC",
			about_b_word(&guide.about_view, guide.about_from_settings ? "BACK" : "CLOSE"));
		ui_style_prompts(CONTENT_RIGHT, PROMPTS_Y, 15.0f, prompts, count, starts, ends);
		for (index = 0; index < count; index++)
			target_add(starts[index] - 4, PROMPTS_Y - 4, ends[index] + 4, PROMPTS_Y + 19, _target_prompt,
				(short)prompts[index].button);
		return;
	}
	switch (guide.section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		if (guide.row[guide.section] < guide.players.count &&
			(guide.players.rows[guide.row[guide.section]].actions & SOCIAL_ACTION_MUTE))
		{
			word = "SELECT";
		}
		add_friend = guide.row[guide.section] < guide.players.count &&
			(guide.players.rows[guide.row[guide.section]].actions & SOCIAL_ACTION_ADD_FRIEND);
		back = guide.options;
		break;
	case SOCIAL_SECTION_RECENT:
		if (friends_signed_in())
		{
			if (guide.friends_loaded && recent_chosen())
				word = "SELECT";
			add_friend = recent_can_add();
			back = guide.recent_options;
			break;
		}
		if (section_rows(guide.section))
			word = "SIGN IN";
		break;
	case SOCIAL_SECTION_SETTINGS:
		word = guide.row[guide.section] >= settings_count() ? "OPEN" : "CHANGE";
		break;
	case SOCIAL_SECTION_SERVICE_RECORD:
		word = guide_record_prompt();
		break;
	case SOCIAL_SECTION_WORKSHOP:
		word = guide_workshop_prompt();
		back = guide_workshop_in_page();
		/* (Steam's list a page at a time: RT, Page Down on a keyboard) */
		if (guide_workshop_page_offered(1))
			prompt_add(prompts, &count, UI_BUTTON_RIGHT_TRIGGER, "PGDN", "NEXT PAGE");
		else if (guide_workshop_page_offered(-1))
			prompt_add(prompts, &count, UI_BUTTON_LEFT_TRIGGER, "PGUP", "PREVIOUS");
		break;
	case SOCIAL_SECTION_FRIENDS:
		if (friends_signed_in())
		{
			struct friend_item const *item = guide.friends_loaded ? chosen_item() : NULL;
			short options[MAXIMUM_OPTIONS];

			if (item && guide.options)
				word = "SELECT";
			else if (item && item->kind == _item_offline)
				word = guide.offline_open ? "HIDE" : "SHOW";
			else if (item && item_options(item, options) > 0)
				word = "SELECT";
			back = guide.options;
			break;
		}
		if (section_rows(guide.section))
			word = sign_in_word(&back);
		break;
	case SOCIAL_SECTION_CLAN:
		if (friends_signed_in())
		{
			struct friend_item const *item = guide.clan_loaded ? clan_chosen_item() : NULL;
			short options[MAXIMUM_OPTIONS];

			if (item && guide.options)
				word = "SELECT";
			else if (item && item->kind == _clan_action)
				word = item->index == _clan_action_invite ? "INVITE" : guide.armed == ARMED_LEAVE ?
					"CONFIRM" : "LEAVE";
			else if (item && clan_item_options(item, options) > 0)
				word = "SELECT";
			back = guide.options;
			break;
		}
		if (section_rows(guide.section))
			word = "SIGN IN";
		break;
	case SOCIAL_SECTION_PARTY:
	{
		struct party_item item;

		if (guide.code_entry)
		{
			prompt_add(prompts, &count, UI_BUTTON_A, "ENTER", "JOIN");
			prompt_add(prompts, &count, UI_BUTTON_Y, "TAB", "PASTE");
			back = TRUE;
		}
		else if (party_chosen(&item))
		{
			if (item.kind == _party_item_invite)
			{
				prompt_add(prompts, &count, UI_BUTTON_A, "ENTER", "JOIN");
				prompt_add(prompts, &count, UI_BUTTON_X, "M", "DECLINE");
			}
			else if (item.kind == _party_item_voice)
				word = "CHANGE";
			else if (item.kind == _party_item_action || !party_has_leader() ||
				(guide.party.leader && !guide.party.members[item.index].you))
			{
				word = "SELECT";
			}
		}
		back = back || guide.party_member != NONE || guide.party_armed != NONE;
		break;
	}
	default:
		if (section_rows(guide.section))
			word = sign_in_word(&back);
		break;
	}
	if (word)
		prompt_add(prompts, &count, UI_BUTTON_A, "ENTER", word);
	if (add_friend)
		prompt_add(prompts, &count, UI_BUTTON_Y, "TAB", "ADD FRIEND");
	if (shortcut_option(GUIDE_INPUT_Y) != NONE)
		prompt_add(prompts, &count, UI_BUTTON_Y, "TAB", "JOIN");
	if (shortcut_option(GUIDE_INPUT_X) != NONE)
		prompt_add(prompts, &count, UI_BUTTON_X, "M", "INVITE");
	prompt_add(prompts, &count, UI_BUTTON_B, "ESC", guide.signing_in && signing_in_here() &&
		guide.connect.state == BROWSER_CONNECT_CONFIRM && !guide.connect.answered ? "NO" : back ? "BACK" : "CLOSE");
	ui_style_prompts(CONTENT_RIGHT, PROMPTS_Y, 15.0f, prompts, count, starts, ends);
	for (index = 0; index < count; index++)
		target_add(starts[index] - 4, PROMPTS_Y - 4, ends[index] + 4, PROMPTS_Y + 19, _target_prompt, (short)prompts[index].button);
}

/* the key DELTA's prompts show (controls.delta's; on a Mac one that isn't
an F key, which macOS keeps for itself) */
void guide_key_shown(char *key, int size)
{
	void halo_input_shown_name(const char *binding, char *shown, size_t size);
	char name[DELTA_KEY_NAME_SIZE];

#ifdef __APPLE__
	delta_key_shown(config_string("controls.delta"), 1, name, sizeof(name));
#else
	delta_key_shown(config_string("controls.delta"), 0, name, sizeof(name));
#endif
	/* (as the keyboard labels it: Backslash shows as \) */
	halo_input_shown_name(name, key, (size_t)size);
}

/* the toast on screen now: in the menus, in a game only with
delta.notify_in_game, a friend online's in the menus alone */
static boolean toast_visible(void)
{
	if (!guide.toast_shown || system_milliseconds() - guide.toast_time >= TOAST_TIME)
		return FALSE;
	/* (quiet in a game unless asked for: a screenshot's or clip's, and a
	party's note, always show) */
	if (in_game() && !guide.toast_always && guide.toast_kind != _toast_party_note &&
		(!config_boolean("delta.notify_in_game") || guide.toast_kind == _toast_friend))
	{
		return FALSE;
	}
	return TRUE;
}

static void render_toast(void)
{
	unsigned long age = system_milliseconds() - guide.toast_time;
	float x = in_game() ? TOAST_GAME_X : TOAST_MENU_X, y = in_game() ? TOAST_GAME_Y : TOAST_MENU_Y;
	float share, text_right = x + TOAST_WIDTH - 10;

	if (!guide.toast_shown)
		return;
	if (age >= TOAST_TIME)
	{
		guide.toast_shown = FALSE;
		return;
	}
	/* (never over the loading screen) */
	if (!toast_visible())
		return;
#ifdef HALO_CAPTURE
	/* (a screenshot's toast is not in the next screenshot: the frame drawn
	while one is asked for is the one it is of) */
	if (guide.toast_capture && capture_screenshot_wanted())
		return;
#endif
	share = age < TOAST_FADE ? (float)age / TOAST_FADE :
		age > TOAST_TIME - TOAST_FADE ? (float)(TOAST_TIME - age) / TOAST_FADE : 1.0f;
	ui_overlay_layer(4);
	ui_style_clock((unsigned int)system_milliseconds());
	ui_style_glass(x, y, TOAST_WIDTH, TOAST_HEIGHT, 8, fade(ui_style_color(UI_COLOR_GLASS_TOP), share),
		fade(COLOR_PANEL_EDGE, share));
	/* the main menu's note: its words alone (they name the key), counted
	once drawn (delta.hint_shown) */
	if (guide.toast_kind == _toast_hint || guide.toast_kind == _toast_menus_retired)
	{
		if (!guide.toast_counted && guide.toast_kind == _toast_menus_retired)
		{
			guide.toast_counted = TRUE;
			config_menus_retired_seen();
		}
		else if (!guide.toast_counted)
		{
			char count[16];
			int shown = (int)config_integer("delta.hint_shown") + 1;

			guide.toast_counted = TRUE;
			snprintf(count, sizeof(count), "%d", shown);
			config_write("delta.hint_shown", count);
			platform_log("DELTA: the main menu's note, %d of %d", shown, DELTA_HINT_TIMES);
		}
		ui_style_text_faded(UI_TEXT_NAME, share, x + 12, y + 5, UI_ALIGN_LEFT, guide.toast_title);
		ui_style_text_faded(UI_TEXT_STATUS, share, x + 12, y + 20, UI_ALIGN_LEFT, guide.toast_line);
		ui_overlay_layer(0);
		return;
	}
	/* (a friend's: their initial on gold; else the player's own color) */
	if (guide.toast_kind != _toast_none)
	{
		ui_overlay_rect(x + 8, y + 7, 20, 20, 4, fade(ui_style_color(UI_COLOR_EMBLEM_FRIEND), share));
		if (guide.toast_initial[0])
		{
			ui_overlay_text(UI_FONT_MENU, 9.5f, x + 18, y + 11, UI_ALIGN_CENTER, fade(ui_style_color(UI_COLOR_EMBLEM_LETTER), share),
				guide.toast_initial);
		}
		/* (the key that opens it) */
		if (ui_overlay_keyboard())
		{
			char key[DELTA_KEY_NAME_SIZE];

			guide_key_shown(key, sizeof(key));
			text_right -= ui_overlay_key_width(key, 12.0f) + 6;
			ui_overlay_key(key, 12.0f, text_right + 6, y + 11, fade(ui_style_color(UI_COLOR_NAME), share));
		}
	}
	else
		ui_overlay_rect(x + 8, y + 7, 20, 20, 4, fade(guide.color ? guide.color : COLOR_PLAYER, share));
	ui_style_text_faded(UI_TEXT_NAME, share, x + 36, y + 5, UI_ALIGN_LEFT, guide.toast_title);
	ui_style_text_faded(UI_TEXT_STATUS, share, x + 36, y + 20, UI_ALIGN_LEFT, guide.toast_line);
	ui_overlay_layer(0);
}

#ifdef HALO_PORT_VOICE_AUDIO
/* ---------- the talking indicator

Quiet and off the HUD, in the Guide's look, at the top right: while this
player talks, a small glass pill with a level meter and the channel talked
on (and for a moment after the channel is switched); under it, while others
talk, their names, a few at most (display.voice_speakers). Nothing while
nobody talks. */

enum
{
	VOICE_RIGHT = 628, VOICE_TOP = 10,
	VOICE_PILL_HEIGHT = 18,
	VOICE_NAME_HEIGHT = 16,
	VOICE_NAMES = 4,
	/* the channel shown this long after it is switched (ms) */
	VOICE_CHANNEL_SHOWN = 2500
};

/* a meter of three bars, by the level (0 to 1) */
static void voice_meter(float x, float y, float level, unsigned int color)
{
	short bar;

	for (bar = 0; bar < 3; bar++)
	{
		float height = 4.0f + 3.0f * bar;
		boolean lit = level > 0.02f + 0.06f * bar;

		ui_overlay_rect(x + bar * 4.0f, y + 12.0f - height, 3.0f, height, 1.0f,
			lit ? color : (color & 0xFFFFFF00) | 0x50);
	}
}

static void render_voice_indicator(void)
{
	static int last_channel;
	static unsigned long switched_time;
	struct network_game *game = network_game_get_game();
	unsigned long now = system_milliseconds();
	int channel = voice_game_channel();
	boolean talking = voice_game_talking();
	float y = VOICE_TOP;
	short names = 0;
	long index;

	if (voice_mode() == _voice_mode_off || !game || !delta_peer_game_role())
		return;
	if (channel != last_channel)
	{
		if (last_channel)
			switched_time = now;
		last_channel = channel;
	}
	ui_overlay_layer(3);
	ui_style_clock((unsigned int)now);
	if (talking || now - switched_time < VOICE_CHANNEL_SHOWN)
	{
		char const *name = voice_game_channel_name(channel);
		float width = 34.0f + ui_overlay_text_width(UI_FONT_MENU, 9.0f, name);
		boolean open = voice_game_channel_state(channel) == _voice_channel_open;

		ui_style_glass(VOICE_RIGHT - width, y, width, VOICE_PILL_HEIGHT, 6, ui_style_color(UI_COLOR_GLASS_TOP),
			COLOR_PANEL_EDGE);
		voice_meter(VOICE_RIGHT - width + 8, y + 3, talking ? 0.05f + voice_input_level() * 4.0f : 0.0f,
			open ? 0x7FE07FFF : COLOR_DIMMED);
		ui_overlay_text(UI_FONT_MENU, 9.0f, VOICE_RIGHT - 8, y + 4, UI_ALIGN_RIGHT, open ? COLOR_VALUE : COLOR_DIMMED,
			name);
		y += VOICE_PILL_HEIGHT + 4;
	}
	if (!config_boolean("display.voice_speakers"))
	{
		ui_overlay_layer(0);
		return;
	}
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS && names < VOICE_NAMES; index++)
	{
		struct network_player *player = &game->players[index];
		short machine = player->machine_index;
		long other;
		boolean first = TRUE;
		char name[48];

		if (!network_player_is_valid(player) || machine == network_game_client_get_local_machine_index() ||
			chat_local_muted(machine, player->name) || !voice_game_machine_talking(machine))
		{
			continue;
		}
		/* (a machine's first player names it) */
		for (other = 0; other < index && first; other++)
			first = !(network_player_is_valid(&game->players[other]) && game->players[other].machine_index == machine);
		if (!first)
			continue;
		utf8_from_wide(name, sizeof(name), player->name, NUMBEROF(player->name));
		voice_meter(VOICE_RIGHT - ui_overlay_text_width(UI_FONT_MENU, 9.0f, name) - 18, y + 2, 0.3f, 0x7FE07FFF);
		ui_overlay_text(UI_FONT_MENU, 9.0f, VOICE_RIGHT, y + 3, UI_ALIGN_RIGHT, COLOR_VALUE, name);
		y += VOICE_NAME_HEIGHT;
		names++;
	}
	ui_overlay_layer(0);
}
#endif

/* ---------- public code */

boolean guide_active(void)
{
	return guide.open || guide.follow_active;
}

boolean guide_takes_player(void)
{
	return guide.open || guide.follow_active;
}

void guide_open(void)
{
	if (guide.open || !ui_overlay_available())
		return;
	/* (the Guide's key over the leader's offer: staying) */
	if (guide.follow_active)
		follow_end(FALSE);
	guide.open = TRUE;
	guide.opened_time = system_milliseconds();
	/* (what is held as it opens is not pressed in it) */
	guide.held = 0xFFFFFFFF;
	guide.repeat_held = 0;
	guide.target_count = 0;
	platform_guide_capture(TRUE);
	event_manager_flush();
	card_update();
	players_update();
	browser_profile_load(FALSE);
	/* (its friends asked for soon) */
	guide.options = FALSE;
	guide.armed = NONE;
	guide.where_time = 0;
	friends_update();
	guide.paused_game = pauses_game();
	if (guide.paused_game)
		ui_widgets_port_pause_game(TRUE);
	platform_log("DELTA: opened");
}

/* opened on PARTY (the menus' DELTA rows): start a party, invite friends,
bring the party to a game */
void guide_open_party(void)
{
	guide_open();
	if (guide.open)
		section_go(SOCIAL_SECTION_PARTY);
}

void guide_open_workshop(void)
{
	guide_open();
	if (guide.open)
		section_go(SOCIAL_SECTION_WORKSHOP);
}

void guide_open_setting(char const *key)
{
	short index;
	struct settings_row const *row;

	guide_open();
	if (!guide.open)
		return;
	section_go(SOCIAL_SECTION_SETTINGS);
	for (index = 0; (row = settings_row_at(SETTINGS_GROUP_GUIDE, index)) != NULL; index++)
	{
		if (!strcmp(settings_row_key(row), key))
		{
			guide.row[SOCIAL_SECTION_SETTINGS] = index;
			break;
		}
	}
}

void guide_about_open(void)
{
	guide_open();
	if (!guide.open)
		return;
	guide.about = TRUE;
	guide.about_from_settings = FALSE;
	about_reset(&guide.about_view);
}

void guide_close(void)
{
	if (!guide.open)
		return;
	guide.open = FALSE;
	guide.about = FALSE;
	guide.target_count = 0;
	if (guide_logs_modal())
		guide_logs_modal_back();
	guide.code_entry = FALSE;
	guide.party_member = NONE;
	guide.party_armed = NONE;
	guide.options = FALSE;
	while (report_player_step() != _report_step_none)
		report_player_back();
	sign_in_stop();
	if (!guide.follow_active)
		platform_guide_capture(FALSE);
	event_manager_flush();
	if (guide.paused_game)
		ui_widgets_port_pause_game(FALSE);
	guide.paused_game = FALSE;
}

void guide_toast(char const *title, char const *line)
{
	social_copy_text(guide.toast_title, sizeof(guide.toast_title), title);
	social_copy_text(guide.toast_line, sizeof(guide.toast_line), line);
	guide.toast_kind = _toast_none;
	guide.toast_time = system_milliseconds();
	guide.toast_shown = TRUE;
	guide.toast_always = FALSE;
	guide.toast_capture = FALSE;
}

#ifdef HALO_CAPTURE
/* the screenshots' (capture.c): the map's name for their files, and their
toasts */
static void capture_update(void)
{
	char title[64], line[160];
	char const *map = "menu";

	if (!main_menu_is_active() && game_map_loaded())
		map = game_engine_running() ? main_get_multiplayer_map_name() : main_get_map_name();
	capture_set_map(map);
	if (capture_take_notice(title, sizeof(title), line, sizeof(line)))
	{
		guide_toast(title, line);
		guide.toast_always = TRUE;
		guide.toast_capture = TRUE;
	}
}
#endif

wchar_t const *guide_pause_label(long string_list_tag)
{
	static wchar_t label[16];
	char text[16];
	long tag = pc_menu_pause_guide_label();
	short waiting, index;

	if (tag == NONE || string_list_tag != tag || (waiting = (short)(friends_waiting() + clan_invites_waiting())) <= 0)
		return NULL;
	snprintf(text, sizeof(text), "DELTA %d", MIN(waiting, 99));
	for (index = 0; text[index] && index < (short)NUMBEROF(label) - 1; index++)
		label[index] = (wchar_t)text[index];
	label[index] = 0;
	return label;
}

/* whether the multiplayer scoreboard shows (Back held on the first
controller, or its key: game_engine.c's in-game score) */
static boolean scoreboard_showing(void)
{
	struct gamepad_state const *gamepad;

	if (!game_engine_running() || !network_game_get_game())
		return FALSE;
	gamepad = input_get_gamepad_state(0);
	return gamepad && gamepad->buttons[_gamepad_binary_button_back];
}

void guide_update(boolean menu_open)
{
	if (!guide.party_ready)
	{
		guide.party_ready = TRUE;
		guide.party_member = NONE;
		guide.party_armed = NONE;
	}
#ifdef HALO_CAPTURE
	capture_update();
#endif
	friends_update();
	clan_update();
	hint_update(menu_open);
	toasts_next();
	recent_notice();
	/* (a report's answer, and its clip: report_player.c) */
	report_player_update();
	/* (logs sent with SEND MY LOGS' window closed: their code) */
	guide_logs_update();
	/* (WORKSHOP's folders watched while it shows, or an item waits on Steam) */
	guide_workshop_update(guide.open && guide.section == SOCIAL_SECTION_WORKSHOP);
	if (guide.open && !section_shown(guide.section))
		section_go((short)(guide.section + 1));
	/* (a toast waits out the intro's video and a map's loading: its time
	starts once it can show) */
	if (guide.toast_shown && (bink_playback_active() || (!main_menu_is_active() && !game_map_loaded())))
		guide.toast_time = system_milliseconds();
	/* (debug.delta_open and debug.delta_toast: for screenshots and tests,
	once a run) */
	if (!guide.debug_open_done && config_boolean("debug.delta_open") && (main_menu_is_active() || game_map_loaded()))
	{
		char const *wanted = config_string("debug.delta_section");
		short section;

		guide.debug_open_done = TRUE;
		guide_open();
		for (section = 0; wanted[0] && section < NUMBER_OF_SOCIAL_SECTIONS; section++)
		{
			char const *a = wanted, *b = social_section_name(section);

			while (*a && (*a | 0x20) == (*b | 0x20))
				a++, b++;
			if (!*a && !*b)
				section_go(section);
		}
	}
	/* (debug.delta_row: that row chosen once its section has it) */
	if (guide.debug_open_done && !guide.debug_row_done && guide.open && config_integer("debug.delta_row") >= 0 &&
		section_rows(guide.section) > config_integer("debug.delta_row"))
	{
		guide.debug_row_done = TRUE;
		guide.row[guide.section] = (short)config_integer("debug.delta_row");
		if (guide.section == SOCIAL_SECTION_FRIENDS)
			chosen_remember();
		if (guide.section == SOCIAL_SECTION_WORKSHOP)
			guide_workshop_row_click(guide.row[guide.section], FALSE);
	}
	/* (debug.party_test: party steps, one after another as each is
answered, for tests: "create,invite:grif") */
	if (guide.debug_party_step >= 0 && config_string("debug.party_test")[0] && main_menu_is_active() && menu_open &&
		!guide.party_acting && !guide.party_invite_handle[0])
	{
		char const *steps = config_string("debug.party_test");
		char step[64];
		short index = 0;
		size_t length;

		while (*steps && index < guide.debug_party_step)
		{
			if (*steps++ == ',')
				index++;
		}
		length = strcspn(steps, ",");
		if (!*steps || length >= sizeof(step))
			guide.debug_party_step = NONE;
		else
		{
			memcpy(step, steps, length);
			step[length] = 0;
			guide.debug_party_step++;
			card_update();
			platform_log("DELTA: party test step %s", !strncmp(step, "invite:", 7) ? "invite" : step);
			if (!strncmp(step, "join:", 5))
				party_act("join", step + 5);
			else if (!strncmp(step, "invite:", 7))
				party_invite_send(step + 7);
			else
				party_act(step, "");
		}
	}
	if (!guide.debug_toast_done && config_string("debug.delta_toast")[0] && main_menu_is_active() &&
		menu_open)
	{
		guide.debug_toast_done = TRUE;
		guide_toast(config_string("debug.delta_toast"), "Online \xC2\xB7 In the menus");
	}
	/* (F3, the Guide button, Back held in the menus: open or close; not
	over the virtual keyboard, which takes typing) */
	if (platform_guide_toggles())
	{
		if (guide.open)
			guide_close();
		else if (!virtual_keyboard_active())
		{
			/* (a toast up: its item) */
			boolean toast = toast_visible() && guide.toast_kind != _toast_none;

			/* (opened over the scoreboard: straight to the game's players,
			where REPORT is) */
			boolean scoreboard = scoreboard_showing();

			guide_open();
			if (toast && guide.open)
				toast_go();
			else if (scoreboard && guide.open)
				section_go(SOCIAL_SECTION_PLAYERS_HERE);
		}
	}
	debug_steps();
	if (guide.open)
	{
		/* (a game that ended under it lets go of its pause) */
		if (guide.paused_game && !pauses_game())
		{
			ui_widgets_port_pause_game(FALSE);
			guide.paused_game = FALSE;
		}
		card_update();
		players_update();
	}
}

static void handle(unsigned int pressed);

/* debug.delta_test: once the Guide opened by itself (debug.delta_open),
its steps pressed one at a time, DEBUG_STEP_INTERVAL apart, from
DEBUG_STEP_WAIT after it opened ("down;down;a":
up, down, left, right, a, b, x, y, lb, rb, start, back, lt, rt). Debug builds only: a release
build never presses the Guide's buttons for anyone (accepting an invite,
kicking from a clan) */
#define DEBUG_STEP_INTERVAL 600
#define DEBUG_STEP_WAIT 8000
#ifdef HALO_RELEASE
static void debug_steps(void)
{
}
#else
static void debug_steps(void)
{
	static char const *const words[] =
	{
		"up", "down", "left", "right", "a", "b", "x", "y", "lb", "rb", "start", "back", "lt", "rt",
	};
	char const *steps = config_string("debug.delta_test");
	unsigned long now = system_milliseconds();
	short step = 0, index;

	/* (the first a few seconds after it opened: the site's answers in) */
	if (!guide.open || !guide.debug_open_done || !steps[0] || now - guide.debug_step_time < DEBUG_STEP_INTERVAL ||
		now - guide.opened_time < DEBUG_STEP_WAIT)
	{
		return;
	}
	while (*steps && step < guide.debug_step)
	{
		if (*steps++ == ';')
			step++;
	}
	if (!*steps)
		return;
	guide.debug_step_time = now;
	guide.debug_step++;
	for (index = 0; index < (short)NUMBEROF(words); index++)
	{
		size_t length = strlen(words[index]);

		if (!strncmp(steps, words[index], length) && (steps[length] == ';' || !steps[length]))
		{
			handle(1u << index);
			platform_log("DELTA: test step %s", words[index]);
			return;
		}
	}
}
#endif

static void handle_pressed(unsigned int pressed);

/* what acts on PARTY's item chosen (A, X, Y, and Start and Back for good
measure): dropped for a frame the item under the row changed in; the d-pad,
the bumpers and B only move or go back */
#define PARTY_ACTING_INPUTS (GUIDE_INPUT_A | GUIDE_INPUT_X | GUIDE_INPUT_Y | GUIDE_INPUT_START | GUIDE_INPUT_BACK)

/* what was pressed (or clicked on the prompt bar), PARTY's row kept on its
item around it: a press in the frame the item under the row changed (KICK
gone as the leader left and the row on MAKE LEADER now, a member gone and
the row on WHO CAN JOIN) was aimed at the old item, so only its moves go
through (not the code being picked, nor a follow offer: neither is on the
list) */
static void handle(unsigned int pressed)
{
	if (guide.section == SOCIAL_SECTION_PARTY && party_selection_keep() && guide.open && !guide.code_entry &&
		!guide.about)
	{
		pressed &= ~(unsigned int)PARTY_ACTING_INPUTS;
	}
	handle_pressed(pressed);
	if (guide.section == SOCIAL_SECTION_PARTY)
		party_selection_remember();
}

static void handle_pressed(unsigned int pressed)
{
	short option;

	/* (following the leader: its offer takes the input) */
	if (guide.follow_active && !guide.open)
	{
		follow_handle(pressed);
		return;
	}
	if (guide.about)
	{
		if (pressed & GUIDE_INPUT_B)
		{
			if (!about_input(&guide.about_view, ABOUT_INPUT_B))
			{
				if (guide.about_from_settings)
					guide.about = FALSE;
				else
					guide_close();
			}
			return;
		}
		if (pressed & GUIDE_INPUT_UP)
			about_input(&guide.about_view, ABOUT_INPUT_UP);
		if (pressed & GUIDE_INPUT_DOWN)
			about_input(&guide.about_view, ABOUT_INPUT_DOWN);
		if (pressed & GUIDE_INPUT_A)
			about_input(&guide.about_view, ABOUT_INPUT_A);
		return;
	}
	/* (SEND MY LOGS' window: as Steam's below) */
	if (guide_logs_modal())
	{
		if (pressed & GUIDE_INPUT_A)
			guide_logs_modal_press();
		else if (pressed & GUIDE_INPUT_B)
			guide_logs_modal_back();
		else if (pressed & (GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT))
			guide_logs_modal_move((pressed & GUIDE_INPUT_LEFT) ? -1 : 1);
		return;
	}
	/* (Steam's sign-in window: A its button with the focus, B back, left
	and right the question's two buttons, nothing under it) */
	if (guide_workshop_link_modal())
	{
		if (pressed & GUIDE_INPUT_A)
			guide_workshop_link_modal_press();
		else if (pressed & GUIDE_INPUT_B)
			guide_workshop_link_modal_back();
		else if (pressed & (GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT))
			guide_workshop_link_modal_move((pressed & GUIDE_INPUT_LEFT) ? -1 : 1);
		return;
	}
	/* (WORKSHOP: LT and RT, Page Up and Page Down, a page of Steam's list on or back) */
	if (guide.section == SOCIAL_SECTION_WORKSHOP && (pressed & (GUIDE_INPUT_LEFT_TRIGGER | GUIDE_INPUT_RIGHT_TRIGGER)))
		guide_workshop_page_step((pressed & GUIDE_INPUT_RIGHT_TRIGGER) ? 1 : -1);
	/* (PARTY's code being picked: the d-pad is its letters) */
	if (guide.section == SOCIAL_SECTION_PARTY && guide.code_entry &&
		!(pressed & (GUIDE_INPUT_LEFT_BUMPER | GUIDE_INPUT_RIGHT_BUMPER)))
	{
		code_entry_handle(pressed);
		return;
	}
	if (pressed & GUIDE_INPUT_B)
	{
		/* (in a report: back a step) */
		if (guide.section == SOCIAL_SECTION_PLAYERS_HERE && report_player_step() != _report_step_none)
		{
			report_player_back();
			guide.option = 0;
			if (report_player_step() == _report_step_none)
				guide.options = FALSE;
			return;
		}
		/* (in FRIENDS' options panel, or PARTY's on a member: back to the
		list; signing in: a no, or the code dropped) */
		if (guide.options)
		{
			guide.options = FALSE;
			guide.armed = NONE;
		}
		else if (guide.section == SOCIAL_SECTION_WORKSHOP && guide_workshop_back())
			;
		else if (guide.section == SOCIAL_SECTION_RECENT && guide.recent_options)
		{
			guide.recent_options = FALSE;
			guide.recent_armed = NONE;
		}
		else if (guide.section == SOCIAL_SECTION_PARTY && (guide.party_member != NONE || guide.party_armed != NONE))
		{
			if (guide.party_member != NONE)
				guide.row[SOCIAL_SECTION_PARTY] = guide.party_member;
			guide.party_member = NONE;
			guide.party_armed = NONE;
		}
		else if (guide.signing_in && signing_in_here() && guide.connect.state == BROWSER_CONNECT_CONFIRM &&
			!guide.connect.answered)
		{
			browser_connect_answer(FALSE);
		}
		else if (guide.signing_in && signing_in_here())
			sign_in_stop();
		else
			guide_close();
		return;
	}
	if (guide.section == SOCIAL_SECTION_PARTY && (pressed & (GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT)))
		party_side();
	if (guide.section == SOCIAL_SECTION_PARTY && (pressed & GUIDE_INPUT_X))
		party_decline();
	if (pressed & GUIDE_INPUT_LEFT_BUMPER)
		section_go((short)(guide.section - 1));
	if (pressed & GUIDE_INPUT_RIGHT_BUMPER)
		section_go((short)(guide.section + 1));
	if (pressed & GUIDE_INPUT_UP)
		move(-1);
	if (pressed & GUIDE_INPUT_DOWN)
		move(1);
	if (guide.section == SOCIAL_SECTION_SETTINGS && guide.row[guide.section] < settings_count() &&
		(pressed & (GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT)))
		setting_change(guide.row[guide.section], (pressed & GUIDE_INPUT_LEFT) ? -1 : 1);
	/* (WORKSHOP's Show, Sort and page rows: left and right step their value) */
	if (guide.section == SOCIAL_SECTION_WORKSHOP && (pressed & (GUIDE_INPUT_LEFT | GUIDE_INPUT_RIGHT)))
		guide_workshop_side((pressed & GUIDE_INPUT_LEFT) ? -1 : 1);
	if (pressed & GUIDE_INPUT_A)
		activate();
	else if ((pressed & GUIDE_INPUT_X) && guide.section == SOCIAL_SECTION_PLAYERS_HERE)
		player_mute_toggle(guide.row[guide.section]);
	else if ((pressed & GUIDE_INPUT_Y) && guide.section == SOCIAL_SECTION_PLAYERS_HERE)
		player_add_friend(guide.row[guide.section]);
	else if ((pressed & GUIDE_INPUT_Y) && recent_can_add())
		recent_option_run(_recent_add);
	else if ((pressed & GUIDE_INPUT_Y) && (option = shortcut_option(GUIDE_INPUT_Y)) != NONE)
		option_run(option);
	else if ((pressed & GUIDE_INPUT_X) && (option = shortcut_option(GUIDE_INPUT_X)) != NONE)
		option_run(option);
}

void guide_process(void)
{
	if (!guide.open && !guide.follow_active)
		return;
	if (guide.signing_in)
	{
		int before = guide.connect.state;

		browser_connect_get(&guide.connect);
		if (guide.connect.state == BROWSER_CONNECT_CONFIRM && before != BROWSER_CONNECT_CONFIRM)
			guide.confirm_shown = system_milliseconds();
		/* (signed in: the card and the friends follow) */
		if (guide.connect.state == BROWSER_CONNECT_CONNECTED && guide.linked)
			guide.signing_in = FALSE;
	}
	handle(presses());
}

void guide_pointer(struct halo_ui_pointer const *pointer)
{
	short index;

	if (!guide.open && !guide.follow_active)
		return;
	if (pointer->right_clicks)
	{
		handle(GUIDE_INPUT_B);
		return;
	}
	if (guide.about && pointer->wheel_steps)
	{
		short step;

		for (step = 0; step < ABS(pointer->wheel_steps); step++)
			about_input(&guide.about_view, pointer->wheel_steps > 0 ? ABOUT_INPUT_UP : ABOUT_INPUT_DOWN);
	}
	/* (the wheel over a list: the d-pad, a row a notch, so the mouse reaches
	every row, WORKSHOP's last with its pages too) */
	/* (over a Workshop page's description: it scrolls, the focus kept) */
	else if (pointer->wheel_steps && guide.section == SOCIAL_SECTION_WORKSHOP && !guide_workshop_link_modal() &&
		guide_workshop_wheel(pointer->x, pointer->y, pointer->wheel_steps))
	{
	}
	else if (pointer->wheel_steps && !guide_workshop_link_modal() && !guide_logs_modal())
	{
		short step;

		for (step = 0; step < ABS(pointer->wheel_steps); step++)
			handle(pointer->wheel_steps > 0 ? GUIDE_INPUT_UP : GUIDE_INPUT_DOWN);
	}
	if (!pointer->left_clicks)
		return;
	if (guide.about && guide.about_view.licenses_x1 > guide.about_view.licenses_x0 &&
		pointer->click_x >= guide.about_view.licenses_x0 && pointer->click_x < guide.about_view.licenses_x1 &&
		pointer->click_y >= guide.about_view.licenses_y0 && pointer->click_y < guide.about_view.licenses_y1)
	{
		about_input(&guide.about_view, ABOUT_INPUT_A);
		return;
	}
	/* (a click on a Workshop page's description: its focus) */
	if (guide.section == SOCIAL_SECTION_WORKSHOP && !guide_workshop_link_modal() &&
		guide_workshop_click(pointer->click_x, pointer->click_y))
	{
		return;
	}
	for (index = 0; index < guide.target_count; index++)
	{
		struct guide_target const *target = &guide.targets[index];

		if (pointer->click_x < target->x0 || pointer->click_x >= target->x1 || pointer->click_y < target->y0 ||
			pointer->click_y >= target->y1)
		{
			continue;
		}
		switch (target->kind)
		{
		case _target_tab:
			section_go(target->index);
			break;
		case _target_row:
			if (guide.section == SOCIAL_SECTION_WORKSHOP)
			{
				guide_workshop_row_click(target->index, pointer->touch);
				break;
			}
			/* (a tap chooses the row; on the one chosen, it is its A) */
			if (guide.section == SOCIAL_SECTION_CLAN)
			{
				boolean again = guide.row[guide.section] == target->index && !guide.options;

				if (!again)
					guide.armed = NONE;
				guide.options = FALSE;
				guide.row[guide.section] = target->index;
				clan_chosen_remember();
				if (again || guide.clan_items[target->index].kind == _clan_action)
					activate();
				break;
			}
			if (guide.section == SOCIAL_SECTION_PARTY)
			{
				struct party_item items[MAXIMUM_PARTY_ITEMS];
				/* (the row back on its item first; the item under the
				cursor another now: nothing armed carries over) */
				boolean moved = party_selection_keep(), again = !moved && guide.row[guide.section] == target->index;

				/* (the list changed under the tap since it was drawn: it was
				aimed at another item, so nothing) */
				if (!party_tap_lands(target->index))
				{
					party_selection_remember();
					break;
				}
				/* (another item clicked: what was armed was on the one
				before) */
				if (!again)
					guide.party_armed = NONE;
				guide.row[guide.section] = target->index;
				/* (a button or VOICE at once; a member or an invite on its
				second) */
				if (again || (target->index < party_items(items) && party_item_button(&items[target->index])))
					activate();
				party_selection_remember();
				break;
			}
			if (guide.section == SOCIAL_SECTION_RECENT && friends_signed_in())
			{
				boolean again = guide.row[guide.section] == target->index && !guide.recent_options;

				guide.recent_options = FALSE;
				guide.recent_armed = NONE;
				guide.row[guide.section] = target->index;
				if (again)
					activate();
				break;
			}
			if (guide.section == SOCIAL_SECTION_FRIENDS)
			{
				boolean again = guide.row[guide.section] == target->index && !guide.options;

				guide.options = FALSE;
				guide.armed = NONE;
				guide.row[guide.section] = target->index;
				chosen_remember();
				/* (a click on the chosen row, or on the offline friends' row) */
				if (again || guide.items[target->index].kind == _item_offline)
					activate();
				break;
			}
			if (guide.row[guide.section] == target->index && pointer->touch)
				activate();
			guide.row[guide.section] = target->index;
			break;
		case _target_setting:
			guide.row[guide.section] = target->index;
			if (target->index >= settings_count())
				extra_row_activate(target->index);
			else
				setting_change(target->index, 1);
			break;
		case _target_option:
			if (guide.section == SOCIAL_SECTION_WORKSHOP)
			{
				guide_workshop_option_click(target->index);
				break;
			}
			if (guide.section == SOCIAL_SECTION_RECENT)
			{
				guide.recent_options = TRUE;
				if (guide.recent_option != target->index)
					guide.recent_armed = NONE;
				guide.recent_option = target->index;
				activate();
				break;
			}
			guide.options = TRUE;
			if (guide.option != target->index)
				guide.armed = NONE;
			guide.option = target->index;
			activate();
			break;
		case _target_action:
		case _target_sign_in:
			activate();
			break;
		case _target_player_action:
			if (target->index)
				player_add_friend(guide.row[guide.section]);
			else
				player_mute_toggle(guide.row[guide.section]);
			break;
		case _target_prompt:
			handle(target->index == UI_BUTTON_A ? GUIDE_INPUT_A : target->index == UI_BUTTON_X ? GUIDE_INPUT_X :
				target->index == UI_BUTTON_Y ? GUIDE_INPUT_Y : target->index == UI_BUTTON_LEFT_TRIGGER ?
				GUIDE_INPUT_LEFT_TRIGGER : target->index == UI_BUTTON_RIGHT_TRIGGER ? GUIDE_INPUT_RIGHT_TRIGGER :
				GUIDE_INPUT_B);
			break;
		case _target_close:
			guide_close();
			break;
		case _target_page:
			guide_workshop_arrow_click(target->index);
			break;
		case _target_modal:
			guide_workshop_link_modal_click(target->index);
			break;
		case _target_logs:
			guide_logs_modal_click(target->index);
			break;
		}
		return;
	}
	if (guide_logs_modal())
	{
		guide_logs_modal_click_outside(pointer->click_x, pointer->click_y);
		return;
	}
	/* (Steam's sign-in window: a click beside it does nothing; beside
	UNLINK's question, CANCEL) */
	if (guide_workshop_link_modal())
	{
		guide_workshop_link_modal_click_outside(pointer->click_x, pointer->click_y);
		return;
	}
	/* (a click outside the panel closes it; outside the leader's offer,
	nothing) */
	if (guide.open && (pointer->click_x < PANEL_X || pointer->click_x >= PANEL_RIGHT || pointer->click_y < PANEL_Y ||
		pointer->click_y >= PANEL_Y + PANEL_HEIGHT))
	{
		guide_close();
	}
}

void guide_render(void)
{
	if (!ui_overlay_available())
		return;
	/* (display.theme's palette, as it is now: a change shows at once) */
	ui_style_theme(ui_style_theme_named(config_string("display.theme")));
	if (!guide.open)
	{
#ifdef HALO_PORT_VOICE_AUDIO
		render_voice_indicator();
#endif
		if (guide.follow_active)
			render_follow();
		else
			render_toast();
		return;
	}
	plasma_look();
	guide.target_count = 0;
	ui_overlay_layer(4);
	ui_style_clock((unsigned int)system_milliseconds());
	/* (behind the panel: the shade, or the dashboard's cellwall,
	delta.guide_background, by itself with Dashboard Green; the shade where
	the renderer has no cellwall) */
	{
		char const *background = config_string("delta.guide_background");
		int cellwall = !strcmp(background, "cellwall") ||
			(strcmp(background, "plain") != 0 && ui_style_theme_current() == UI_THEME_GREEN);

		if (!cellwall || !ui_style_cellwall(-200, 0, 1040, 480))
			ui_overlay_rect(-200, 0, 1040, 480, 0, ui_style_color(UI_COLOR_SHADE));
		else
		{
			/* (the wall calmer near the panel: a soft dark edge around it,
			so its lines stay in the open margins) */
			unsigned int shade = ui_style_color(UI_COLOR_SHADE) & 0xFFFFFF00;
			int ring;

			for (ring = 6; ring >= 1; ring--)
			{
				ui_overlay_rect(PANEL_X - ring * 5, PANEL_Y - ring * 5, PANEL_WIDTH + ring * 10, PANEL_HEIGHT + ring * 10,
					12 + ring * 5, shade | (unsigned int)(0x2E - ring * 5));
			}
		}
	}
	ui_style_panel(PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT, 1.0f);
	if (guide.about)
	{
		ui_style_card(CONTENT_LEFT, CONTENT_RIGHT, HEADER_Y, NULL, NULL, 0, "ABOUT");
		about_render(&guide.about_view, CONTENT_LEFT, RULE_Y + 12, CONTENT_RIGHT, BODY_BOTTOM);
		render_prompts();
		ui_overlay_layer(0);
		return;
	}
	render_card();
	render_tabs();
	switch (guide.section)
	{
	case SOCIAL_SECTION_PLAYERS_HERE:
		render_players();
		break;
	case SOCIAL_SECTION_SETTINGS:
		render_settings();
		break;
	case SOCIAL_SECTION_SERVICE_RECORD:
		guide_record_render(CONTENT_LEFT, BODY_Y, CONTENT_RIGHT, BODY_BOTTOM, TRUE);
		break;
	case SOCIAL_SECTION_WORKSHOP:
		guide_workshop_render(CONTENT_LEFT, BODY_Y, CONTENT_RIGHT, BODY_BOTTOM, workshop_target);
		break;
	case SOCIAL_SECTION_FRIENDS:
		if (friends_signed_in())
			render_friends();
		else
			render_coming();
		break;
	case SOCIAL_SECTION_PARTY:
		render_party();
		break;
	case SOCIAL_SECTION_RECENT:
		if (friends_signed_in())
			render_recent();
		else
			render_coming();
		break;
	case SOCIAL_SECTION_CLAN:
		if (friends_signed_in())
			render_clan();
		else
			render_coming();
		break;
	default:
		render_coming();
		break;
	}
	render_prompts();
	/* (Steam's sign-in window over it all: only its button takes a click) */
	if (guide_workshop_link_modal())
	{
		guide.target_count = 0;
		ui_overlay_layer(5);
		guide_workshop_link_modal_render(PANEL_X, PANEL_Y, PANEL_RIGHT, PANEL_Y + PANEL_HEIGHT, workshop_target);
	}
	/* (SEND MY LOGS' window over it all: only its buttons take a click) */
	if (guide_logs_modal())
	{
		guide.target_count = 0;
		ui_overlay_layer(5);
		guide_logs_modal_render(PANEL_X, PANEL_Y, PANEL_RIGHT, PANEL_Y + PANEL_HEIGHT, logs_target);
	}
	ui_overlay_layer(0);
}

#endif
