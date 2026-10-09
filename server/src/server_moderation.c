/*
SERVER_MODERATION.C

Delta Control in the game (server_moderation.h; server/docs/moderation.md).

A ChupathingyCE game that joins the server agrees Delta Peer's moderation
capability (docs/delta.md). Its player proves their moderator key when they
sign in from the game's Moderation screen (an Ed25519 signature of the
session's challenge, with a key made from their player key, which never
leaves their machine); Delta Peer checks it, and tells this unit the
machine's key. The key's role (server_roles.c: the moderators file, an
account bound to it, the site's list) decides what the game is told it may
do, and each action the game then asks for (each signed, each checked by
Delta Peer) is run here as a command of theirs (server_commands.c), with
the same permission checks, limits and audit as the console's, the API's
and the web page's. The target is any player, ChupathingyCE or OpenCE:
kicks and bans are the server's, through the game's own protocol.
*/

#ifdef HALO_GAME_BROWSER
#ifdef HALO_SERVER

#include "cseries.h"
#include "cseries/errors.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "command_line.h"
#include "dedicated.h"
#include "server_moderation.h"

#include <stdio.h>
#include <string.h>

/* (Delta Peer's, port/linux/src/delta_moderation.h and delta_peer.h, which
the game's units do not include: plain types, the same layout) */
typedef unsigned int delta_u32;

struct delta_peer_moderation_host
{
	void *context;
	void (*machine_key)(void *context, int machine_index, unsigned char const *key);
	int (*key_role)(void *context, unsigned char const *key, delta_u32 *permissions, delta_u32 *ban_minutes);
	int (*action)(void *context, int machine_index, unsigned char const *key, int action, int target_machine,
		int minutes, char const *reason, char *result, int result_size);
	void (*bind_answer)(void *context, int machine_index, delta_u32 request, int accepted, unsigned char const *key);
};

enum
{
	_delta_moderation_action_warn = 1,
	_delta_moderation_action_kick = 2,
	_delta_moderation_action_ban = 3,
	_delta_moderation_action_end_game = 4,
	_delta_moderation_action_next_map = 5,
	_delta_moderation_notice_warning = 1,
	_delta_moderation_notice_info = 2,
};

void delta_peer_game_set_moderation_host(struct delta_peer_moderation_host const *host);
int delta_peer_game_moderation_key(int machine_index, unsigned char *key);
int delta_peer_game_moderation_capable(int machine_index);
int delta_peer_game_moderation_notice(int machine_index, int kind, char const *text);
int delta_peer_game_moderation_bind(int machine_index, unsigned int request, char const *account, char const *server);
void delta_peer_game_moderation_roles_changed(void);

/* ---------- globals */

static struct
{
	boolean registered;
	/* each machine's proved key */
	boolean has_key[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	unsigned char keys[HALO_PORT_MAXIMUM_NETWORK_MACHINES][32];
} moderation;

/* ---------- Delta Peer's hooks (on the main thread, from its frame) */

static void hook_machine_key(
	void *context,
	int machine_index,
	unsigned char const *key)
{
	(void)context;
	if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES)
		return;
	if (key)
	{
		char text[9];
		char who[64];
		int role = server_roles_key_role(key, NULL, who, sizeof(who));

		csmemcpy(moderation.keys[machine_index], key, 32);
		moderation.has_key[machine_index] = TRUE;
		server_key_short(key, text);
		error(_error_silent, "dedicated: machine %d proved its moderator key %s (%s%s%s)", machine_index, text,
			server_role_name(role), who[0] ? ": " : "", who);
	}
	else
		moderation.has_key[machine_index] = FALSE;
}

static int hook_key_role(
	void *context,
	unsigned char const *key,
	delta_u32 *permissions,
	delta_u32 *ban_minutes)
{
	unsigned int granted = 0;
	int role = server_roles_key_role(key, &granted, NULL, 0);

	(void)context;
	if (permissions)
		*permissions = (delta_u32)granted;
	/* (a moderator's timed ban: control_roles.h's CONTROL_ROLES_TIMED_BAN_MINUTES) */
	if (ban_minutes)
		*ban_minutes = 7 * 24 * 60;
	return role;
}

/* a reason as a command's last word, in quotes (\" and \\ inside) */
static void quoted(
	char const *text,
	char *out,
	long size)
{
	long length = 0;

	if (size < 3)
		return;
	out[length++] = '"';
	for (; *text && length < size - 3; text++)
	{
		if (*text == '"' || *text == '\\')
		{
			if (length >= size - 4)
				break;
			out[length++] = '\\';
		}
		out[length++] = *text;
	}
	out[length++] = '"';
	out[length] = 0;
}

static int hook_action(
	void *context,
	int machine_index,
	unsigned char const *key,
	int action,
	int target_machine,
	int minutes,
	char const *reason,
	char *result,
	int result_size)
{
	char line[COMMAND_LINE_MAXIMUM_LENGTH + 1];
	char reason_word[80] = "";
	char name[48];
	char actor[96];
	char who[64];
	char key_text[9];
	unsigned int permissions = 0;
	long target = 0;
	int role = server_roles_key_role(key, &permissions, who, sizeof(who));

	(void)context;
	if (reason && reason[0])
		quoted(reason, reason_word, sizeof(reason_word));
	if (action == _delta_moderation_action_warn || action == _delta_moderation_action_kick || action == _delta_moderation_action_ban)
	{
		target = server_commands_machine_player(target_machine, NULL, 0);
		if (!target)
		{
			snprintf(result, (size_t)result_size, "that player is no longer in the game");
			return 0;
		}
	}
	switch (action)
	{
	case _delta_moderation_action_warn:
		/* (the game has no keyboard to type a reason with: a moderator's
		warning, then) */
		snprintf(line, sizeof(line), "sv_warn %ld %s", target, reason_word[0] ? reason_word :
			"\"a moderator warns you\"");
		break;
	case _delta_moderation_action_kick:
		snprintf(line, sizeof(line), "sv_kick %ld %s", target, reason_word);
		break;
	case _delta_moderation_action_ban:
		if (minutes > 0)
			snprintf(line, sizeof(line), "sv_ban %ld %dm %s", target, minutes, reason_word);
		else
			snprintf(line, sizeof(line), "sv_ban %ld forever %s", target, reason_word);
		break;
	case _delta_moderation_action_end_game:
		snprintf(line, sizeof(line), "sv_end_game");
		break;
	case _delta_moderation_action_next_map:
		snprintf(line, sizeof(line), "sv_mapcycle_next");
		break;
	default:
		snprintf(result, (size_t)result_size, "not an action this server takes");
		return 0;
	}
	/* (who: the name the role gave them, else their player's; and their key's
	beginning, the log's) */
	if (!server_commands_machine_player(machine_index, name, sizeof(name)))
		snprintf(name, sizeof(name), "machine%d", machine_index);
	server_key_short(key, key_text);
	snprintf(actor, sizeof(actor), "%s %s", who[0] ? who : name, key_text);
	{
		char *paren = strstr(actor, " (");

		/* ("alice (account) 1a2b3c4d": "alice 1a2b3c4d") */
		if (paren)
		{
			char *close = strchr(paren, ')');

			if (close)
				memmove(paren, close + 1, strlen(close + 1) + 1);
		}
	}
	return server_commands_run_as(line, "game", actor, role, permissions, result, result_size) ? 1 : 0;
}

static void hook_bind_answer(
	void *context,
	int machine_index,
	delta_u32 request_id,
	int accepted,
	unsigned char const *key)
{
	(void)context;
	(void)machine_index;
	/* (no key: the player never answered) */
	server_roles_bind_answer((unsigned int)request_id, !key ? SERVER_BIND_EXPIRED : accepted ? SERVER_BIND_ACCEPTED :
		SERVER_BIND_DECLINED, key);
}

/* ---------- public code */

void server_moderation_update(
	void)
{
	unsigned int request_id;
	int player_number;
	char account[40];

	if (!moderation.registered)
	{
		static struct delta_peer_moderation_host const host =
		{
			NULL, hook_machine_key, hook_key_role, hook_action, hook_bind_answer
		};

		moderation.registered = TRUE;
		delta_peer_game_set_moderation_host(&host);
	}
	/* the roles changed elsewhere (the panel's accounts, the site's list):
	the moderators' games told again */
	if (server_roles_take_changed())
		delta_peer_game_moderation_roles_changed();
	/* the control panel's bind requests: sent to the player's game, if it
	speaks Delta Peer's moderation */
	while (server_roles_bind_take(&request_id, &player_number, account, sizeof(account)))
	{
		struct dedicated_status status;
		long machine = server_commands_player_machine(player_number);

		dedicated_server_get_status(&status);
		if (machine < 0)
			server_roles_bind_answer(request_id, SERVER_BIND_NO_PLAYER, NULL);
		else if (!delta_peer_game_moderation_capable((int)machine) ||
			!delta_peer_game_moderation_bind((int)machine, request_id, account, status.name))
		{
			server_roles_bind_answer(request_id, SERVER_BIND_NOT_DELTA, NULL);
		}
		else
			server_roles_bind_answer(request_id, SERVER_BIND_SENT, NULL);
	}
}

boolean server_moderation_machine_key(
	long machine_index,
	unsigned char *key)
{
	if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES ||
		!moderation.has_key[machine_index])
	{
		return FALSE;
	}
	/* (Delta Peer's word, again: the session may have ended this frame) */
	if (!delta_peer_game_moderation_key((int)machine_index, key))
	{
		moderation.has_key[machine_index] = FALSE;
		return FALSE;
	}
	return TRUE;
}

boolean server_moderation_notice(
	long machine_index,
	boolean warning,
	char const *title,
	char const *text)
{
	char notice[128];

	if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES ||
		!delta_peer_game_moderation_capable((int)machine_index))
	{
		return FALSE;
	}
	snprintf(notice, sizeof(notice), "%s: %s", title, text && text[0] ? text : "-");
	return delta_peer_game_moderation_notice((int)machine_index, warning ? _delta_moderation_notice_warning :
		_delta_moderation_notice_info, notice) ? TRUE : FALSE;
}

void server_moderation_roles_changed(
	void)
{
	delta_peer_game_moderation_roles_changed();
}

#endif
#endif
