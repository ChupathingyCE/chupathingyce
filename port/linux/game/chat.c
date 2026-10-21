/*
CHAT.C

Text chat between Chupathingy machines (chat.h; docs/delta.md, Chat), over
Delta Peer (delta_chat_game.h). OpenCE machines take no part: they never
agree to the chat capability, so they are never sent a line and never say
one.

The host (a player's, or a dedicated server) routes. Delta Peer checks a
line's session, sequence, rate, repeat and text, and its machine's mute;
here its speaker is named from the host's own player list, the host's mode
and filter are applied, and the line goes to each machine whose player may
hear it (everyone, or the speaker's team). The host's own player, if it has
one, is a listener and a speaker like any other. Nothing of what is said is
written down.

Each copy shows the lines in the HUD's own font, at the lower left above
the motion tracker: a few at once, each fading after a while, the name in
its team's color. T says a line to everyone and Y to the team (the keys are
config.toml's controls.chat_all and _team); while the line is typed the
keyboard drives nothing else. H (controls.chat_log) shows the recent lines
and the players, where M mutes one for this copy alone. A controller has
quick lines: D-pad up picks the next, D-pad down says it.

Settings: chat.enabled, chat.show, chat.filter (with the blocked names
list built in and the player's own chat.filter_file), chat.streamer_mode
and chat.quick_messages; a player's host network.chat and
network.chat_filter; a dedicated server its own (chat_host_set).

debug.chat_test runs a script of lines, for the automated tests.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "input/input.h"
#include "interface/hud_messaging.h"
#include "interface/ui_widget.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "halo_keyboard.h"
#include "network_coop.h"
#include "../src/delta_chat.h"
#include "../src/delta_chat_game.h"
#include "../src/delta_moderation.h"

#include "chat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int config_boolean(char const *name);
char const *config_string(char const *name);
unsigned long config_changes(void);
void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	/* the lines kept for the log, and shown over the game at once */
	LOG_LINES = 48,
	SHOWN_LINES = 4,
	LOG_SHOWN_LINES = 12,
	/* a line shows this long, then fades for this long (milliseconds) */
	LINE_TIME = 6000,
	FADE_TIME = 1000,
	/* a refusal or note of this copy's shows a little shorter */
	NOTE_TIME = 5000,
	/* a line's text and name as kept (UCS-2, as the game's fonts) */
	TEXT_LENGTH = DELTA_WIRE_CHAT_TEXT_SIZE + 1,
	NAME_LENGTH = 24,
	/* the line typed: bytes (ASCII typed; UTF-8 on the wire) */
	INPUT_SIZE = DELTA_WIRE_CHAT_SAY_CHARACTERS + 1,
	MAXIMUM_MUTES = 32,
	MAXIMUM_QUICK = 12,
	QUICK_SIZE = 64,
	/* the quick line picked shows this long before it is put away */
	QUICK_TIME = 4000,
	MACHINES = 128,
	PLAYERS = 128,
	NO_TEAM = -1,
};

/* a line's look: who said it */
enum
{
	_entry_player,
	_entry_moderator,
	_entry_server,
	_entry_system,
	/* this copy's own word (a refusal, a hint): never sent */
	_entry_note,
};

struct chat_entry
{
	unsigned long time;
	short kind;
	short scope;
	short team;
	short machine;
	/* a notice the banner shows already: the log has it, the HUD does not */
	boolean log_only;
	/* its blocked words starred out (chat.filter mask): the stars drawn dim */
	boolean masked;
	wchar_t name[NAME_LENGTH];
	wchar_t text[TEXT_LENGTH];
};

struct chat_mute
{
	short machine;
	wchar_t name[NAME_LENGTH];
};

/* ---------- globals */

static struct
{
	boolean ready;
	/* the log */
	struct chat_entry log[LOG_LINES];
	unsigned long log_count;
	/* the client's: the host's lines read so far, its refusals, the
	server's notices (moderation's banner) */
	unsigned int received_read;
	unsigned int refused_read;
	unsigned int notice_read;
	boolean said_no_host;
	/* the line typed */
	boolean typing;
	short typing_scope;
	char input[INPUT_SIZE];
	short input_length;
	short sent_scope;
	unsigned long actions_held;
	/* the log open, and the player chosen in it */
	boolean log_open;
	short log_choice;
	/* the controller's quick lines: picked, and since when */
	short quick_index;
	unsigned long quick_time;
	boolean quick_shown;
	byte dpad_up, dpad_down;
	/* mutes of this copy's (the game's machine and the name) */
	struct chat_mute mutes[MAXIMUM_MUTES];
	short mute_count;
	/* the filter of what is shown (the built-in list and the player's),
	read again when config.toml changes */
	struct delta_chat_filter filter;
	boolean filter_loaded;
	unsigned long filter_read_at;
	/* the host's */
	boolean host_registered;
	boolean host_set;
	int host_mode;
	int host_filtering;
	/* (seconds a player's chat pauses past the rate; set: a dedicated
	server's) */
	boolean host_cooldown_set;
	int host_cooldown;
	/* a dedicated server's welcome (empty: none), and the machines sent it
	since their player joined */
	char host_welcome[DELTA_WIRE_CHAT_TEXT_SIZE + 1];
	boolean welcomed[MACHINES];
	struct delta_chat_filter host_filter;
	int (*moderator_check)(int machine_index);
	/* the part played last frame (a new game starts the log's state again) */
	int role;
	/* debug.chat_test: where its script is, and when it started */
	char const *test_next;
	unsigned long test_start;
} chat;

/* ---------- text */

/* UTF-8 (length bytes) to the game's UCS-2, characters past it as '?' */
static void utf8_to_wide(char const *text, int length, wchar_t *out, int size)
{
	unsigned char const *bytes = (unsigned char const *)text;
	int index = 0, count = 0;

	while (index < length && count < size - 1)
	{
		unsigned long code = bytes[index];
		int more = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : code >= 0xC0 ? 1 : 0;
		int step;

		if (more)
			code &= more == 1 ? 0x1F : more == 2 ? 0x0F : 0x07;
		for (step = 1; step <= more && index + step < length; step++)
			code = (code << 6) | (bytes[index + step] & 0x3F);
		index += more + 1;
		out[count++] = code < 0x20 || code > 0xFFFF ? L'?' : (wchar_t)code;
	}
	out[count] = 0;
}

/* the game's UCS-2 to UTF-8: its length */
static int wide_to_utf8(wchar_t const *text, int maximum_characters, char *out, int size)
{
	int length = 0, index;

	for (index = 0; index < maximum_characters && text[index]; index++)
	{
		unsigned long code = (unsigned short)text[index];

		if (code < 0x80)
		{
			if (length + 1 > size - 1)
				break;
			out[length++] = (char)code;
		}
		else if (code < 0x800)
		{
			if (length + 2 > size - 1)
				break;
			out[length++] = (char)(0xC0 | (code >> 6));
			out[length++] = (char)(0x80 | (code & 0x3F));
		}
		else
		{
			if (length + 3 > size - 1 || (code >= 0xD800 && code <= 0xDFFF))
				break;
			out[length++] = (char)(0xE0 | (code >> 12));
			out[length++] = (char)(0x80 | ((code >> 6) & 0x3F));
			out[length++] = (char)(0x80 | (code & 0x3F));
		}
	}
	out[length] = 0;
	return length;
}

static void wide_from_ascii(char const *text, wchar_t *out, int size)
{
	int index;

	for (index = 0; text[index] && index < size - 1; index++)
		out[index] = (wchar_t)(unsigned char)text[index];
	out[index] = 0;
}

static boolean same_wide(wchar_t const *a, wchar_t const *b)
{
	while (*a && *a == *b)
	{
		a++;
		b++;
	}
	return *a == *b;
}

/* ---------- the game's players */

static struct network_game *chat_game(void)
{
	return network_game_get_game();
}

/* whether the game has teams (co-op: everyone is one team) */
static boolean game_teams(struct network_game const *game)
{
	if (network_coop_active())
		return TRUE;
	return game && game->variant.universal_variant.teams;
}

/* a machine's first player (its index in the game's list), or NONE */
static long machine_player(struct network_game const *game, long machine_index)
{
	long index;

	if (!game || machine_index < 0)
		return NONE;
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		if (network_player_is_valid((struct network_player *)&game->players[index]) &&
			game->players[index].machine_index == machine_index)
		{
			return index;
		}
	}
	return NONE;
}

static short player_team(struct network_game const *game, long player_index)
{
	if (network_coop_active())
		return 0;
	if (!game || player_index < 0 || player_index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS ||
		!network_player_is_valid((struct network_player *)&game->players[player_index]))
	{
		return NO_TEAM;
	}
	return game->players[player_index].team_index;
}

/* ---------- the log */

static struct chat_entry *log_add(short kind, short scope, short team, short machine, wchar_t const *name,
	wchar_t const *text)
{
	struct chat_entry *entry = &chat.log[chat.log_count % LOG_LINES];

	csmemset(entry, 0, sizeof(*entry));
	entry->time = system_milliseconds();
	entry->kind = kind;
	entry->scope = scope;
	entry->team = team;
	entry->machine = machine;
	if (name)
		ustrncpy(entry->name, name, NAME_LENGTH - 1);
	ustrncpy(entry->text, text, TEXT_LENGTH - 1);
	chat.log_count++;
	return entry;
}

static void note(char const *text)
{
	wchar_t wide[TEXT_LENGTH];

	wide_from_ascii(text, wide, TEXT_LENGTH);
	log_add(_entry_note, 0, NO_TEAM, NONE, NULL, wide);
}

void chat_note(char const *text)
{
	if (text && text[0])
		note(text);
}

static boolean muted(short machine, wchar_t const *name)
{
	short index;

	for (index = 0; index < chat.mute_count; index++)
	{
		if (chat.mutes[index].machine == machine && same_wide(chat.mutes[index].name, name))
			return TRUE;
	}
	return FALSE;
}

/* the filter of what is shown: the built-in list, and the player's file */
static void filter_read(void)
{
	char const *file = config_string("chat.filter_file");
	static char text[32768];
	int length;

	if (chat.filter_loaded && chat.filter_read_at == config_changes())
		return;
	chat.filter_loaded = TRUE;
	chat.filter_read_at = config_changes();
	delta_chat_filter_clear(&chat.filter);
	delta_chat_filter_add(&chat.filter, delta_chat_blocked_words, (int)strlen(delta_chat_blocked_words), TRUE);
	length = delta_peer_game_read_save_file(file, text, (int)sizeof(text));
	if (length > 0)
	{
		platform_log("chat: %d words of your own blocked (%s)", delta_chat_filter_add(&chat.filter, text, length,
			FALSE), file);
	}
}

/* a player's or the server's line heard (or the host's own): kept, as the
player's settings say */
static void line_heard(struct delta_wire_chat_line const *line)
{
	struct network_game *game = chat_game();
	char const *filter_mode = config_string("chat.filter");
	char const *show = config_string("chat.show");
	boolean streamer = config_boolean("chat.streamer_mode");
	boolean server = line->kind == _delta_chat_kind_server || line->kind == _delta_chat_kind_system;
	char masked[DELTA_WIRE_CHAT_TEXT_SIZE + 1];
	wchar_t name[NAME_LENGTH], text[TEXT_LENGTH];
	short team = NO_TEAM;
	short machine = line->speaker_machine == DELTA_WIRE_NO_MACHINE ? NONE : line->speaker_machine;
	int blocked;

	if (server)
	{
		if (!config_boolean("display.server_messages"))
			return;
	}
	else
	{
		long local = local_player_get_player_index(local_player_get_next(NONE));
		short own_team = NO_TEAM;

		if (!config_boolean("chat.enabled") || !strcmp(show, "off"))
			return;
		team = player_team(game, line->speaker_player == 255 ? NONE : line->speaker_player);
		if (local != NONE)
			own_team = (short)player_get(local)->team_index;
		if (!strcmp(show, "team") && (!game_teams(game) || team != own_team) &&
			machine != network_game_client_get_local_machine_index())
		{
			return;
		}
	}
	utf8_to_wide(line->name, line->name_length, name, NAME_LENGTH);
	if (!server && machine != NONE && muted(machine, name))
		return;
	filter_read();
	blocked = delta_chat_filter_check(&chat.filter, line->text, line->length, masked, (int)sizeof(masked));
	if (blocked && !server)
	{
		if (!strcmp(filter_mode, "hide"))
			return;
		if (!strcmp(filter_mode, "show") && !streamer)
			snprintf(masked, sizeof(masked), "%.*s", line->length, line->text);
	}
	else
		snprintf(masked, sizeof(masked), "%.*s", line->length, line->text);
	utf8_to_wide(masked, (int)strlen(masked), text, TEXT_LENGTH);
	/* (a streamer shows a player's team, never their name) */
	if (streamer && !server && machine != network_game_client_get_local_machine_index())
	{
		wide_from_ascii(team == 0 && game_teams(game) ? "Red" : team == 1 && game_teams(game) ? "Blue" : "Player",
			name, NAME_LENGTH);
	}
	log_add(server ? (line->kind == _delta_chat_kind_server ? _entry_server : _entry_system) :
		line->kind == _delta_chat_kind_moderator ? _entry_moderator : _entry_player, line->scope, team, machine, name,
		text)->masked = blocked && !server && (strlen(masked) != (size_t)line->length ||
		memcmp(masked, line->text, (size_t)line->length) != 0);
}

/* ---------- the host's */

/* every machine of the game that may hear a line, sent it; the host's own
player shown it */
static int route(struct delta_wire_chat_line const *line, short speaker_team, boolean teams)
{
	struct network_game *game = chat_game();
	short local = network_game_client_get_local_machine_index();
	boolean seen[MACHINES];
	int sent = 0;
	long index;

	if (!game)
		return 0;
	csmemset(seen, 0, sizeof(seen));
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		struct network_player const *player = &game->players[index];
		short machine = player->machine_index;
		boolean server = line->kind == _delta_chat_kind_server || line->kind == _delta_chat_kind_system;

		if (!network_player_is_valid((struct network_player *)player) || machine < 0 || machine >= MACHINES ||
			seen[machine])
		{
			continue;
		}
		seen[machine] = TRUE;
		if (!server && !delta_chat_hears(line->scope, teams, speaker_team, player_team(game, index)))
			continue;
		if (machine == local)
		{
			line_heard(line);
			sent++;
		}
		else if (delta_peer_game_chat_line(machine, line))
			sent++;
	}
	return sent;
}

/* the host's word on a line of a machine's player, and the line sent on */
static int host_line(int machine_index, int scope, char const *text, int length)
{
	struct network_game *game = chat_game();
	long player = machine_player(game, machine_index);
	boolean teams = game_teams(game);
	struct delta_wire_chat_line line;
	int blocked = 0, reason;

	if (player == NONE)
		return _delta_chat_refused_off;
	if (chat.host_filtering)
		blocked = delta_chat_filter_check(&chat.host_filter, text, length, NULL, 0) > 0;
	reason = delta_chat_host_decide(chat.host_mode, scope, teams, FALSE, blocked);
	if (reason)
		return reason;
	csmemset(&line, 0, sizeof(line));
	line.kind = chat.moderator_check && chat.moderator_check(machine_index) ? _delta_chat_kind_moderator :
		_delta_chat_kind_player;
	line.scope = (unsigned char)scope;
	line.speaker_machine = (unsigned char)machine_index;
	line.speaker_player = (unsigned char)player;
	line.name_length = wide_to_utf8(game->players[player].name, NUMBEROF(game->players[player].name), line.name,
		(int)sizeof(line.name));
	line.length = MIN(length, DELTA_WIRE_CHAT_TEXT_SIZE);
	csmemcpy(line.text, text, line.length);
	route(&line, player_team(game, player), teams);
	return _delta_chat_refused_none;
}

static int hook_say(void *context, int machine_index, int scope, char const *text, int length)
{
	(void)context;
	return host_line(machine_index, scope, text, length);
}

static struct delta_peer_chat_host const chat_host = { NULL, hook_say };

static void host_settings(void)
{
	int mode = chat.host_mode;
	int filtering = chat.host_filtering;

#ifndef HALO_SERVER
	if (!chat.host_set)
	{
		char const *text = config_string("network.chat");

		mode = !strcmp(text, "off") ? _delta_chat_mode_off : !strcmp(text, "team") ? _delta_chat_mode_team :
			_delta_chat_mode_on;
		filtering = config_boolean("network.chat_filter");
	}
#endif
	chat.host_mode = mode;
	chat.host_filtering = filtering;
	delta_peer_game_set_chat_mode(mode, filtering ? DELTA_CHAT_CONFIG_FILTERED : 0);
	delta_peer_game_set_chat_cooldown(chat.host_cooldown_set ? chat.host_cooldown : DELTA_PEER_CHAT_COOLDOWN / 1000);
}

/* the welcome, sent once to each machine whose player joined since (a
Chupathingy machine that agreed to server_messages; an OpenCE one never
does); a machine whose players have all left forgotten */
static void host_welcome(void)
{
	struct network_game *game = chat_game();
	short local = network_game_client_get_local_machine_index();
	boolean present[MACHINES];
	struct delta_wire_chat_line line;
	long index;

	csmemset(present, 0, sizeof(present));
	if (game)
	{
		for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
		{
			short machine = game->players[index].machine_index;

			if (network_player_is_valid(&game->players[index]) && machine >= 0 && machine < MACHINES)
				present[machine] = TRUE;
		}
	}
	csmemset(&line, 0, sizeof(line));
	line.kind = _delta_chat_kind_server;
	line.speaker_machine = DELTA_WIRE_NO_MACHINE;
	line.speaker_player = 255;
	line.length = (int)strlen(chat.host_welcome);
	csmemcpy(line.text, chat.host_welcome, line.length);
	for (index = 0; index < MACHINES; index++)
	{
		if (!present[index])
			chat.welcomed[index] = FALSE;
		else if (!chat.welcomed[index] && index != local)
		{
			/* (with no welcome, those here now are not sent one set later) */
			if (!line.length)
				chat.welcomed[index] = TRUE;
			else if (delta_peer_game_chat_capable((int)index, _delta_chat_kind_server))
				chat.welcomed[index] = delta_peer_game_chat_line((int)index, &line) != 0;
		}
	}
}

void chat_host_set(int mode, int filter)
{
	chat.host_set = TRUE;
	chat.host_mode = mode < _delta_chat_mode_off || mode > _delta_chat_mode_on ? _delta_chat_mode_on : mode;
	chat.host_filtering = filter != 0;
	delta_peer_game_set_chat_mode(chat.host_mode, chat.host_filtering ? DELTA_CHAT_CONFIG_FILTERED : 0);
}

void chat_host_set_cooldown(int seconds)
{
	chat.host_cooldown_set = TRUE;
	chat.host_cooldown = seconds < 0 ? 0 : seconds;
	delta_peer_game_set_chat_cooldown(chat.host_cooldown);
}

int chat_host_cooldown(void)
{
	return chat.host_cooldown_set ? chat.host_cooldown : DELTA_PEER_CHAT_COOLDOWN / 1000;
}

void chat_host_set_welcome(char const *text)
{
	int length = text ? (int)strlen(text) : 0;

	while (length > 0 && text[length - 1] == ' ')
		length--;
	if (length > DELTA_WIRE_CHAT_TEXT_SIZE)
		length = DELTA_WIRE_CHAT_TEXT_SIZE;
	if (length && delta_wire_chat_text_valid(text, length, 0) < 0)
		length = 0;
	if (length)
		csmemcpy(chat.host_welcome, text, length);
	chat.host_welcome[length] = 0;
}

char const *chat_host_welcome(void)
{
	return chat.host_welcome;
}

int chat_host_mode(void)
{
	return chat.host_set || delta_peer_game_role() == 2 ? chat.host_mode : _delta_chat_mode_on;
}

int chat_host_filter(void)
{
	return chat.host_set || delta_peer_game_role() == 2 ? chat.host_filtering : TRUE;
}

void chat_set_moderator_check(int (*check)(int machine_index))
{
	chat.moderator_check = check;
}

int chat_server_say(char const *text)
{
	struct delta_wire_chat_line line;
	int length;

	if (!text || delta_peer_game_role() != 2)
		return 0;
	csmemset(&line, 0, sizeof(line));
	line.kind = _delta_chat_kind_server;
	line.speaker_machine = DELTA_WIRE_NO_MACHINE;
	line.speaker_player = 255;
	length = (int)strlen(text);
	while (length > 0 && text[length - 1] == ' ')
		length--;
	line.length = MIN(length, DELTA_WIRE_CHAT_TEXT_SIZE);
	csmemcpy(line.text, text, line.length);
	if (delta_wire_chat_text_valid(line.text, line.length, 0) < 0)
		return 0;
	return route(&line, NO_TEAM, FALSE);
}

/* ---------- this copy's player */

static char const *refusal_text(int reason, int scope, int paused_seconds)
{
	static char paused[48];

	/* (a host of 0.8.0 on says how long the player's chat is paused) */
	if (reason == _delta_chat_refused_too_fast && paused_seconds > 0)
	{
		snprintf(paused, sizeof(paused), "Chat paused for %d second%s", paused_seconds, paused_seconds == 1 ? "" : "s");
		return paused;
	}
	switch (reason)
	{
	case _delta_chat_refused_off: return "Chat is off in this game";
	case _delta_chat_refused_scope: return scope == _delta_chat_scope_team ? "No teams in this game" :
		"Team chat only in this game";
	case _delta_chat_refused_muted: return "You are muted";
	case _delta_chat_refused_too_fast: return "Not sent: slow down";
	case _delta_chat_refused_blocked: return "Not sent: blocked words";
	case _delta_chat_refused_bad_text: return "Not sent: unsupported text";
	default: return "Not sent";
	}
}

/* the player's line said: to the host, or (hosting) routed here */
static void say(short scope, char const *text, int length)
{
	int role = delta_peer_game_role();

	length = delta_chat_trim((char *)text, length);
	if (length <= 0)
		return;
	if (delta_wire_chat_text_valid(text, length, DELTA_WIRE_CHAT_SAY_CHARACTERS) < 0)
	{
		note(refusal_text(_delta_chat_refused_bad_text, scope, 0));
		return;
	}
	chat.sent_scope = scope;
	if (role == 2)
	{
		int reason = host_line(network_game_client_get_local_machine_index(), scope, text, length);

		if (reason)
			note(refusal_text(reason, scope, 0));
	}
	else if (!delta_peer_game_chat_say(scope, text, length))
		note("Chat needs a Chupathingy host");
}

/* whether the player may open the line now: the host takes it, its mode
lets them */
static boolean may_talk(short scope)
{
	struct delta_wire_chat_config config;
	int role = delta_peer_game_role();

	if (!config_boolean("chat.enabled"))
		return FALSE;
	if (role == 1 && !delta_peer_game_chat_ready())
	{
		if (!chat.said_no_host)
			note("Chat needs a Chupathingy host");
		chat.said_no_host = TRUE;
		return FALSE;
	}
	if (role == 1 && delta_peer_game_chat_config(&config))
	{
		if (config.flags & DELTA_CHAT_CONFIG_MUTED)
		{
			note("You are muted");
			return FALSE;
		}
		if (config.mode == _delta_chat_mode_off || (config.mode == _delta_chat_mode_team &&
			scope == _delta_chat_scope_all))
		{
			note(refusal_text(config.mode == _delta_chat_mode_off ? _delta_chat_refused_off :
				_delta_chat_refused_scope, scope, 0));
			return FALSE;
		}
	}
	return role != 0;
}

static void typing_begin(short scope)
{
	struct key_stroke key;

	chat.typing = TRUE;
	chat.typing_scope = scope;
	chat.input[0] = 0;
	chat.input_length = 0;
	chat.log_open = FALSE;
	/* (the key that opened it typed nothing) */
	while (input_get_key(&key))
		;
	platform_chat_typing(TRUE);
}

static void typing_end(void)
{
	chat.typing = FALSE;
	chat.input[0] = 0;
	chat.input_length = 0;
	platform_chat_typing(chat.log_open);
}

static void typing_keys(void)
{
	struct key_stroke key;

	while (chat.typing && input_get_key(&key))
	{
		if (key.key_code == _key_escape)
			typing_end();
		else if (key.key_code == _key_return)
		{
			char text[INPUT_SIZE];

			csmemcpy(text, chat.input, chat.input_length + 1);
			typing_end();
			say(chat.typing_scope, text, (int)strlen(text));
		}
		else if (key.key_code == _key_backspace)
		{
			if (chat.input_length)
				chat.input[--chat.input_length] = 0;
		}
		else if ((unsigned char)key.ascii_code >= ' ' && (unsigned char)key.ascii_code <= '~' &&
			chat.input_length < INPUT_SIZE - 1)
		{
			chat.input[chat.input_length++] = key.ascii_code;
			chat.input[chat.input_length] = 0;
		}
	}
}

/* the players the log lists (this copy's own left out): their machines and
names */
static short log_players(short *machines, wchar_t (*names)[NAME_LENGTH], short maximum)
{
	struct network_game *game = chat_game();
	short local = network_game_client_get_local_machine_index();
	short count = 0;
	long index;

	if (!game)
		return 0;
	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS && count < maximum; index++)
	{
		struct network_player const *player = &game->players[index];

		if (!network_player_is_valid((struct network_player *)player) || player->machine_index == local)
			continue;
		machines[count] = player->machine_index;
		ustrncpy(names[count], player->name, MIN(NAME_LENGTH - 1, (long)NUMBEROF(player->name)));
		names[count][MIN(NAME_LENGTH - 1, (long)NUMBEROF(player->name))] = 0;
		count++;
	}
	return count;
}

static void mute_toggle(short machine, wchar_t const *name)
{
	short index;

	for (index = 0; index < chat.mute_count; index++)
	{
		if (chat.mutes[index].machine == machine && same_wide(chat.mutes[index].name, name))
		{
			chat.mutes[index] = chat.mutes[--chat.mute_count];
			return;
		}
	}
	if (chat.mute_count < MAXIMUM_MUTES)
	{
		chat.mutes[chat.mute_count].machine = machine;
		ustrncpy(chat.mutes[chat.mute_count].name, name, NAME_LENGTH - 1);
		chat.mute_count++;
	}
}

static void log_keys(void)
{
	struct key_stroke key;
	short machines[PLAYERS];
	static wchar_t names[PLAYERS][NAME_LENGTH];
	short count = log_players(machines, names, PLAYERS);

	while (chat.log_open && input_get_key(&key))
	{
		if (key.key_code == _key_escape || key.key_code == _key_return)
		{
			chat.log_open = FALSE;
			platform_chat_typing(FALSE);
		}
		else if (key.key_code == _key_up_arrow && chat.log_choice > 0)
			chat.log_choice--;
		else if (key.key_code == _key_down_arrow && chat.log_choice < count - 1)
			chat.log_choice++;
		else if ((key.ascii_code == 'm' || key.ascii_code == 'M') && chat.log_choice < count)
			mute_toggle(machines[chat.log_choice], names[chat.log_choice]);
		else if (key.ascii_code == 'h' || key.ascii_code == 'H')
		{
			chat.log_open = FALSE;
			platform_chat_typing(FALSE);
		}
	}
	if (chat.log_choice >= count)
		chat.log_choice = (short)MAX(0, count - 1);
}

/* the controller's quick lines: chat.quick_messages, split at '|' */
static short quick_lines(char lines[MAXIMUM_QUICK][QUICK_SIZE])
{
	char const *text = config_string("chat.quick_messages");
	short count = 0;

	while (text && *text && count < MAXIMUM_QUICK)
	{
		size_t length = strcspn(text, "|");

		snprintf(lines[count], QUICK_SIZE, "%.*s", (int)MIN(length, (size_t)QUICK_SIZE - 1), text);
		if (delta_chat_trim(lines[count], (int)strlen(lines[count])) > 0)
			count++;
		text += length;
		if (*text == '|')
			text++;
	}
	return count;
}

static void quick_buttons(void)
{
	struct gamepad_state const *pad;
	char lines[MAXIMUM_QUICK][QUICK_SIZE];
	short count;
	boolean up, down;
	short controller = 0;

	if (!input_has_gamepad(controller) || !(pad = input_get_gamepad_state(controller)))
		return;
	up = pad->buttons[_gamepad_binary_button_dpad_up] == 1;
	down = pad->buttons[_gamepad_binary_button_dpad_down] == 1;
	if (chat.quick_shown && system_milliseconds() - chat.quick_time > QUICK_TIME)
		chat.quick_shown = FALSE;
	if (!up && !down)
		return;
	count = quick_lines(lines);
	if (!count)
		return;
	if (up)
	{
		chat.quick_index = chat.quick_shown ? (short)((chat.quick_index + 1) % count) :
			(short)MIN(chat.quick_index, count - 1);
		chat.quick_shown = TRUE;
		chat.quick_time = system_milliseconds();
	}
	else if (chat.quick_shown)
	{
		short scope = game_teams(chat_game()) ? _delta_chat_scope_team : _delta_chat_scope_all;

		chat.quick_shown = FALSE;
		if (may_talk(scope))
			say(scope, lines[MIN(chat.quick_index, count - 1)], (int)strlen(lines[MIN(chat.quick_index, count - 1)]));
	}
}

/* ---------- debug.chat_test: "<seconds>:<what>[:<text>]" steps, separated
by ';', from when this copy joins a game: all and team say a line, server
says one as the host's server, mute and unmute mute a machine as a moderator
does (the host), localmute one for this copy alone, log and close the log */

static void test_step(void)
{
	char const *script = config_string("debug.chat_test");
	char step[256];
	char const *end;
	unsigned long at;
	char *what, *text;

	if (!script || !script[0])
		return;
	if (!chat.test_next)
	{
		chat.test_next = script;
		chat.test_start = system_milliseconds();
	}
	if (!*chat.test_next)
		return;
	at = (unsigned long)(atof(chat.test_next) * 1000.0);
	if (system_milliseconds() - chat.test_start < at)
		return;
	end = strchr(chat.test_next, ';');
	snprintf(step, sizeof(step), "%.*s", (int)(end ? end - chat.test_next : (long)strlen(chat.test_next)),
		chat.test_next);
	chat.test_next = end ? end + 1 : chat.test_next + strlen(chat.test_next);
	what = strchr(step, ':');
	if (!what)
		return;
	what++;
	text = strchr(what, ':');
	if (text)
		*text++ = 0;
	else
		text = "";
	platform_log("chat test: %s", what);
	if (!strcmp(what, "all") || !strcmp(what, "team"))
	{
		short scope = !strcmp(what, "team") ? _delta_chat_scope_team : _delta_chat_scope_all;

		if (may_talk(scope))
			say(scope, text, (int)strlen(text));
	}
	else if (!strcmp(what, "server"))
		chat_server_say(text);
	else if (!strcmp(what, "mute") || !strcmp(what, "unmute"))
		delta_peer_game_chat_mute(atoi(text), !strcmp(what, "mute"), 0);
	else if (!strcmp(what, "localmute"))
	{
		struct network_game *game = chat_game();
		long player = machine_player(game, atoi(text));

		if (player != NONE)
			mute_toggle((short)atoi(text), game->players[player].name);
	}
	else if (!strcmp(what, "log"))
	{
		chat.log_open = TRUE;
		platform_chat_typing(TRUE);
	}
	else if (!strcmp(what, "close"))
	{
		chat.log_open = FALSE;
		platform_chat_typing(FALSE);
	}
	else if (!strcmp(what, "type"))
	{
		typing_begin(_delta_chat_scope_all);
		snprintf(chat.input, sizeof(chat.input), "%s", text);
		chat.input_length = (short)strlen(chat.input);
	}
}

/* ---------- each frame */

void chat_update(void)
{
	int role = delta_peer_game_role();

	if (!chat.ready)
	{
		chat.ready = TRUE;
		chat.quick_index = 0;
		if (!chat.host_set)
		{
			chat.host_mode = _delta_chat_mode_on;
			chat.host_filtering = TRUE;
		}
		filter_read();
		delta_chat_filter_clear(&chat.host_filter);
		delta_chat_filter_add(&chat.host_filter, delta_chat_blocked_words, (int)strlen(delta_chat_blocked_words),
			TRUE);
	}
	if (!chat.host_registered)
	{
		chat.host_registered = TRUE;
		delta_peer_game_set_chat_host(&chat_host);
	}
	if (role != chat.role)
	{
		/* (a new game, or none: what was typed and muted goes) */
		chat.role = role;
		chat.received_read = delta_peer_game_chat_received();
		chat.refused_read = delta_peer_game_chat_refused(NULL);
		chat.said_no_host = FALSE;
		csmemset(chat.welcomed, 0, sizeof(chat.welcomed));
		chat.mute_count = 0;
		chat.test_next = NULL;
		if (chat.typing)
			typing_end();
		if (chat.log_open)
		{
			chat.log_open = FALSE;
			platform_chat_typing(FALSE);
		}
	}
	if (role == 2)
	{
		host_settings();
		host_welcome();
	}
	if (role == 1)
	{
		struct delta_peer_game_moderation moderation;
		unsigned int received = delta_peer_game_chat_received();
		struct delta_wire_chat_refused refused;
		unsigned int refusals = delta_peer_game_chat_refused(&refused);

		/* (a new host: counted from 0 again) */
		if (received < chat.received_read)
			chat.received_read = 0;
		for (; chat.received_read < received; chat.received_read++)
		{
			struct delta_wire_chat_line line;

			if (delta_peer_game_chat_get(chat.received_read, &line))
				line_heard(&line);
		}
		if (refusals < chat.refused_read)
			chat.refused_read = 0;
		if (refusals != chat.refused_read)
		{
			chat.refused_read = refusals;
			note(refusal_text(refused.reason, chat.sent_scope, refused.paused_seconds));
		}
		/* (the server's warnings and notices: the banner shows them; the log
		keeps them) */
		delta_peer_game_client_moderation(&moderation);
		if (moderation.notice_count != chat.notice_read)
		{
			if (moderation.notice_count > chat.notice_read && moderation.notice[0])
			{
				wchar_t text[TEXT_LENGTH];
				wchar_t name[NAME_LENGTH];

				utf8_to_wide(moderation.notice, (int)strlen(moderation.notice), text, TEXT_LENGTH);
				wide_from_ascii(moderation.notice_kind == _delta_moderation_notice_warning ? "Server warning" : "Server",
					name, NAME_LENGTH);
				log_add(_entry_server, 0, NO_TEAM, NONE, name, text)->log_only = TRUE;
			}
			chat.notice_read = moderation.notice_count;
		}
	}
	if (role && local_player_get_next(NONE) != NONE)
	{
		unsigned long held = halo_keyboard_actions(0);
		unsigned long pressed = held & ~chat.actions_held;

		chat.actions_held = held;
		/* (escape always closes the line or the log) */
		if (platform_chat_escape() && (chat.typing || chat.log_open))
		{
			struct key_stroke key;

			chat.log_open = FALSE;
			if (chat.typing)
				typing_end();
			else
				platform_chat_typing(FALSE);
			while (input_get_key(&key))
				;
		}
		if (chat.typing)
			typing_keys();
		else if (chat.log_open)
			log_keys();
		else
		{
			if (pressed & (1UL << HALO_KEYBOARD_CHAT_ALL) && may_talk(_delta_chat_scope_all))
				typing_begin(_delta_chat_scope_all);
			else if (pressed & (1UL << HALO_KEYBOARD_CHAT_TEAM) && may_talk(_delta_chat_scope_team))
				typing_begin(_delta_chat_scope_team);
			else if (pressed & (1UL << HALO_KEYBOARD_CHAT_LOG))
			{
				struct key_stroke key;

				chat.log_open = TRUE;
				chat.log_choice = 0;
				while (input_get_key(&key))
					;
				platform_chat_typing(TRUE);
			}
			if (config_boolean("chat.enabled") && !chat.typing)
				quick_buttons();
		}
		test_step();
	}
	else
	{
		chat.actions_held = 0;
		if (chat.typing)
			typing_end();
		if (chat.log_open)
		{
			chat.log_open = FALSE;
			platform_chat_typing(FALSE);
		}
	}
}

/* ---------- drawing */

/* The lines are drawn as the HUD's messages are: its message font, in its
message tint, a size smaller than its messages (so that they keep out of the
way and break into fewer lines) and with a heavier shadow than the font's own,
as Halo PC's chat has, to read over bright ground. They sit at
the lower left, anchored to the screen's lower left corner as the motion
sensor is, a little above it, and keep to the left half of the screen;
the HUD's units are the same at every aspect ratio and with the high-res
HUD's bitmaps, so the lines keep their place beside it. */

enum
{
	/* the lines' left edge, and their bottom above the screen's bottom (in
	the HUD's units: the motion sensor's left edge, and above its top) */
	CHAT_LEFT = 4,
	CHAT_ABOVE_BOTTOM = 92,
	/* the lines end this short of the screen's middle */
	CHAT_SHORT_OF_MIDDLE = 32,
};

/* the lines' size, against the HUD's messages' */
#define CHAT_SCALE 0.8f
/* the heavier shadow: the line again, in black this much as solid, a unit
down and right of it (under the font's own shadow, a unit further) */
#define CHAT_SHADOW_ALPHA 0.6f

/* the HUD's message tint (the state messages': "Hold BACK for score") */
static real_argb_color hud_tint(void)
{
	real_argb_color tint;

	hud_get_text_color(&tint);
	/* (a map without one: a pale steel blue, as the stock HUD's) */
	if (tint.alpha <= 0.0f)
	{
		tint.alpha = 0.85f;
		tint.red = 0.62f;
		tint.green = 0.74f;
		tint.blue = 0.86f;
	}
	/* (a little more solid than the HUD's own messages: lines are read) */
	tint.alpha = MAX(tint.alpha, 0.8f);
	return tint;
}

/* the tint with this color in it (a name's, a server line's), as bright */
static real_argb_color tinted(real_argb_color tint, real red, real green, real blue, real amount)
{
	tint.red = tint.red * (1.0f - amount) + red * amount;
	tint.green = tint.green * (1.0f - amount) + green * amount;
	tint.blue = tint.blue * (1.0f - amount) + blue * amount;
	return tint;
}

static void entry_colors(struct chat_entry const *entry, real_argb_color *name, real_argb_color *text)
{
	real_argb_color const tint = hud_tint();
	boolean teams = game_teams(chat_game());

	/* (the words: the tint paled, as the HUD's numbers; names stand out) */
	*text = tinted(tint, 0.92f, 0.95f, 1.0f, 0.5f);
	switch (entry->kind)
	{
	case _entry_server:
		/* (a muted amber, the HUD's warnings') */
		*name = tinted(tint, 0.92f, 0.72f, 0.36f, 0.8f);
		*text = tinted(tint, 0.92f, 0.78f, 0.50f, 0.6f);
		break;
	case _entry_system:
	case _entry_note:
		*name = tint;
		*text = tint;
		name->alpha *= 0.75f;
		text->alpha *= 0.75f;
		break;
	default:
		/* (the team's color, muted toward the tint; no team, the tint a
		little brighter) */
		if (teams && entry->team == 0)
			*name = tinted(tint, 0.95f, 0.40f, 0.36f, 0.85f);
		else if (teams && entry->team == 1)
			*name = tinted(tint, 0.32f, 0.52f, 1.0f, 0.85f);
		else if (entry->kind == _entry_moderator)
			*name = tinted(tint, 0.50f, 0.84f, 0.54f, 0.8f);
		else
			*name = tint;
		break;
	}
}

/* an entry's words: its name part ("Name: ", "[Team] Name: ") and its text */
static void entry_words(struct chat_entry const *entry, wchar_t *name, int size)
{
	wchar_t prefix[16] = { 0 };
	int length = 0;

	if (entry->kind == _entry_note || (!entry->name[0] && entry->kind != _entry_server))
	{
		name[0] = 0;
		return;
	}
	if (entry->scope == _delta_chat_scope_team && entry->kind != _entry_server)
		wide_from_ascii("[Team] ", prefix, NUMBEROF(prefix));
	while (prefix[length] && length < size - 1)
	{
		name[length] = prefix[length];
		length++;
	}
	{
		wchar_t const *who = entry->kind == _entry_server && !entry->name[0] ? L"Server" : entry->name;
		int index;

		for (index = 0; who[index] && length < size - 1; index++)
			name[length++] = who[index];
	}
	if (entry->kind == _entry_moderator && length < size - 6)
	{
		static wchar_t const tag[] = L" (mod)";
		int index;

		for (index = 0; tag[index]; index++)
			name[length++] = tag[index];
	}
	if (length < size - 2)
	{
		name[length++] = L':';
		name[length++] = L' ';
	}
	name[length] = 0;
}

/* the parts of an entry's line, each in its own color */
enum
{
	_part_name,
	_part_text,
	_part_stars,
	_part_caret,
	NUMBER_OF_PARTS
};

enum
{
	/* a line as drawn: its name part, its text and the caret */
	WHOLE_LENGTH = NAME_LENGTH + 16 + TEXT_LENGTH + 2,
};

/* the width of a line's characters from first, count of them, in the font
set (the game's Unicode strings never wrap: the lines are broken here) */
static short text_width(wchar_t const *text, long first, long count)
{
	wchar_t buffer[WHOLE_LENGTH];
	rectangle2d bounds, text_bounds, cursor;

	count = MIN(count, (long)NUMBEROF(buffer) - 1);
	csmemcpy(buffer, text + first, count * sizeof(wchar_t));
	buffer[count] = 0;
	if (!count)
		return 0;
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = SHORT_MAX / 2;
	bounds.y1 = SHORT_MAX / 2;
	draw_string_set_indents(0, 0);
	draw_unicode_string_compute_bounds(&bounds, buffer, &text_bounds, &cursor);
	return cursor.x0;
}

/* where the line from first breaks to fit width: past its last whole word
that fits (a word too long for a line alone, where it reaches the edge) */
static long line_break(wchar_t const *text, long first, long length, short width)
{
	long end = first, fits = first;

	while (end < length)
	{
		long next = end;

		while (next < length && text[next] == L' ')
			next++;
		while (next < length && text[next] != L' ')
			next++;
		if (text_width(text, first, next - first) > width)
			break;
		fits = end = next;
	}
	if (fits == first)
	{
		/* (one long word: as many of its characters as fit, at least one) */
		for (fits = first + 1; fits < length && text_width(text, first, fits + 1 - first) <= width; fits++)
			;
	}
	return fits;
}

/* one entry, its bottom at bottom: its top. Its name part in the name's
color, its text in the text's (a masked line's stars dimmer), broken into
lines that fit the area; the caret, when typing, after the text. */
static short draw_entry(long font_index, short line_height, struct chat_entry const *entry, rectangle2d const *area,
	short bottom, real alpha, boolean caret)
{
	wchar_t name[NAME_LENGTH + 16];
	wchar_t whole[WHOLE_LENGTH];
	byte part[WHOLE_LENGTH];
	long breaks[16];
	real_argb_color colors[NUMBER_OF_PARTS];
	short width = (short)(area->x1 - area->x0);
	short line_count = 0, line, top, pass;
	long index, length = 0;

	entry_words(entry, name, NUMBEROF(name));
	for (index = 0; name[index] && length < WHOLE_LENGTH - 2; index++)
	{
		part[length] = _part_name;
		whole[length++] = name[index];
	}
	for (index = 0; entry->text[index] && length < WHOLE_LENGTH - 2; index++)
	{
		part[length] = entry->masked && entry->text[index] == L'*' ? _part_stars : _part_text;
		whole[length++] = entry->text[index];
	}
	if (caret)
	{
		part[length] = _part_caret;
		whole[length++] = L'_';
	}
	whole[length] = 0;
	if (!length)
		return bottom;

	entry_colors(entry, &colors[_part_name], &colors[_part_text]);
	colors[_part_name].alpha *= alpha;
	colors[_part_text].alpha *= alpha;
	colors[_part_stars] = colors[_part_text];
	colors[_part_stars].alpha *= 0.55f;
	colors[_part_caret] = colors[_part_name];
	colors[_part_caret].alpha *= (system_milliseconds() / 500) % 2 ? 1.0f : 0.2f;

	/* (its lines: where each starts) */
	draw_string_set_draw_mode(font_index, NONE, 0, 0, &colors[_part_text]);
	for (index = 0; index < length && line_count < (short)NUMBEROF(breaks); )
	{
		breaks[line_count++] = index;
		index = line_break(whole, index, length, width);
		while (index < length && whole[index] == L' ')
			index++;
	}
	top = (short)(bottom - line_count * line_height);
	/* (the shadow first, all of it, so that no run's falls over another's
	letters; then the letters) */
	for (pass = 0; pass < 2; pass++)
	for (line = 0; line < line_count; line++)
	{
		long end = line + 1 < line_count ? breaks[line + 1] : length;
		long run;

		for (index = breaks[line]; index < end; index = run)
		{
			wchar_t buffer[WHOLE_LENGTH];
			rectangle2d bounds = *area;

			for (run = index; run < end && part[run] == part[index]; run++)
				;
			csmemcpy(buffer, whole + index, (run - index) * sizeof(wchar_t));
			buffer[run - index] = 0;
			bounds.x0 = (short)(area->x0 + text_width(whole, breaks[line], index - breaks[line]));
			bounds.x1 = (short)MAX(area->x1, bounds.x0 + 1);
			bounds.y0 = (short)(top + line * line_height);
			bounds.y1 = (short)(bounds.y0 + line_height);
			if (!pass)
			{
				real_argb_color shadow = *global_real_argb_black;

				shadow.alpha = colors[part[index]].alpha * CHAT_SHADOW_ALPHA;
				bounds.x0++;
				bounds.x1++;
				bounds.y0++;
				bounds.y1++;
				draw_string_set_draw_mode(font_index, NONE, 0, 0, &shadow);
			}
			else
				draw_string_set_draw_mode(font_index, NONE, 0, 0, &colors[part[index]]);
			draw_string_set_indents(0, 0);
			rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, buffer);
		}
	}
	return top;
}

void chat_draw(short local_player_index)
{
	long font_index = hud_get_font_index();
	struct font_header *font;
	rectangle2d const window = render.camera.window_bounds;
	rectangle2d area = window;
	unsigned long now = system_milliseconds();
	short line_height, bottom;
	long shown = 0, index;
	real anchor_x, anchor_y;

	/* (not over a menu: the pause menu, the pregame's) */
	if (local_player_index != local_player_get_next(NONE) || font_index == NONE || !delta_peer_game_role() ||
		ui_widgets_active_for_local_player(local_player_index))
	{
		return;
	}
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	/* (the lower left, above the motion sensor, short of the middle) */
	area.x0 = (short)(window.x0 - render.camera.viewport_bounds.x0 + CHAT_LEFT);
	area.x1 = (short)((window.x0 + window.x1) / 2 - render.camera.viewport_bounds.x0 - CHAT_SHORT_OF_MIDDLE);
	area.y0 = (short)(window.y0 - render.camera.viewport_bounds.y0);
	area.y1 = (short)(window.y1 - render.camera.viewport_bounds.y0);
	bottom = (short)(area.y1 - CHAT_ABOVE_BOTTOM);
	/* (drawn CHAT_SCALE times the size about the lines' lower left: laid
	out over the area as it is before the scale) */
	anchor_x = (real)area.x0;
	anchor_y = (real)bottom;
	area.x1 = (short)(area.x0 + (area.x1 - area.x0) / CHAT_SCALE);
	area.y0 = (short)(bottom - (bottom - area.y0) / CHAT_SCALE);
	rasterizer_text_set_scale(CHAT_SCALE, anchor_x, anchor_y);

	/* the line typed (Halo PC's "Say:"), or the quick line picked */
	if (chat.typing || chat.quick_shown)
	{
		struct chat_entry entry;
		char lines[MAXIMUM_QUICK][QUICK_SIZE];
		char text[INPUT_SIZE + QUICK_SIZE + 32];

		csmemset(&entry, 0, sizeof(entry));
		entry.kind = _entry_player;
		entry.team = NO_TEAM;
		if (chat.typing)
		{
			wide_from_ascii(chat.typing_scope == _delta_chat_scope_team ? "Say to team" : "Say", entry.name,
				NAME_LENGTH);
			snprintf(text, sizeof(text), "%s", chat.input);
		}
		else
		{
			short count = quick_lines(lines);

			wide_from_ascii("Quick", entry.name, NAME_LENGTH);
			snprintf(text, sizeof(text), "%s   D-pad down: send", count ? lines[MIN(chat.quick_index, count - 1)] :
				"");
		}
		utf8_to_wide(text, (int)strlen(text), entry.text, TEXT_LENGTH);
		bottom = draw_entry(font_index, line_height, &entry, &area, bottom, 1.0f, chat.typing);
		bottom = (short)(bottom - line_height / 3);
	}

	/* the log: its recent lines, then its players and their mutes */
	if (chat.log_open)
	{
		short machines[PLAYERS];
		static wchar_t names[PLAYERS][NAME_LENGTH];
		short count = log_players(machines, names, PLAYERS);
		short row;
		struct chat_entry entry;

		csmemset(&entry, 0, sizeof(entry));
		entry.kind = _entry_note;
		wide_from_ascii("M: mute or unmute   H: close", entry.text,
			TEXT_LENGTH);
		bottom = draw_entry(font_index, line_height, &entry, &area, bottom, 1.0f, FALSE);
		for (row = (short)MIN(count, 8) - 1; row >= 0; row--)
		{
			short player = (short)(MAX(0, chat.log_choice - 7) + row);
			wchar_t text[TEXT_LENGTH];
			char prefix[8];

			if (player >= count)
				continue;
			csmemset(&entry, 0, sizeof(entry));
			entry.kind = player == chat.log_choice ? _entry_player : _entry_system;
			entry.team = NO_TEAM;
			snprintf(prefix, sizeof(prefix), "%s", player == chat.log_choice ? "> " : "  ");
			wide_from_ascii(prefix, text, TEXT_LENGTH);
			ustrncpy(text + 2, names[player], NAME_LENGTH - 1);
			if (muted(machines[player], names[player]))
			{
				wchar_t const *suffix = L"   (muted)";
				long end = ustrlen(text);

				ustrncpy(text + end, suffix, TEXT_LENGTH - end - 1);
			}
			ustrncpy(entry.text, text, TEXT_LENGTH - 1);
			bottom = draw_entry(font_index, line_height, &entry, &area, bottom, 1.0f, FALSE);
		}
		if (!count)
		{
			csmemset(&entry, 0, sizeof(entry));
			entry.kind = _entry_note;
			wide_from_ascii("No other players", entry.text, TEXT_LENGTH);
			bottom = draw_entry(font_index, line_height, &entry, &area, bottom, 1.0f, FALSE);
		}
		bottom = (short)(bottom - line_height / 2);
	}

	for (index = (long)chat.log_count - 1; index >= 0 && index >= (long)chat.log_count - LOG_LINES; index--)
	{
		struct chat_entry const *entry = &chat.log[index % LOG_LINES];
		unsigned long age = now - entry->time;
		unsigned long time = entry->kind == _entry_note ? NOTE_TIME : LINE_TIME;
		real alpha = 1.0f;

		if (chat.log_open || chat.typing)
		{
			if (shown >= LOG_SHOWN_LINES)
				break;
		}
		else
		{
			if (shown >= SHOWN_LINES || age >= time + FADE_TIME)
				break;
			if (entry->log_only)
				continue;
			if (age > time)
				alpha = 1.0f - (real)(age - time) / (real)FADE_TIME;
		}
		bottom = draw_entry(font_index, line_height, entry, &area, bottom, alpha, FALSE);
		shown++;
		if (bottom < area.y0 + line_height * 4)
			break;
	}
	rasterizer_text_set_scale(1.0f, 0.0f, 0.0f);
}

#endif
