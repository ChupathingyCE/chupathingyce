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
#include "server_bans.h"
#include "server_reports.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* (Delta Peer's own headers: the moderation host's hooks and the verified
player they pass, shared with delta_peer.c so the layout can't drift; and
identity.h's certificate capabilities) */
#include "../../port/linux/src/delta_moderation.h"
#include "../../port/linux/src/identity.h"

enum
{
	LOCAL_PLAYERS = DELTA_WIRE_IDENTITY_LOCAL_PLAYERS,
};

/* (text chat's, port/linux/game/chat.h) */
void chat_set_moderator_check(int (*check)(int machine_index));
/* (network_distributed.c's: whether a machine's verified account or device
is banned in bans.txt) */
boolean network_distributed_identity_banned(long machine_index);
/* (delta_peer_game.c's: a machine's verified accounts, device and handles as
a ban line's fields; 0 when it has none) */
int delta_peer_game_machine_identity_text(int machine_index, char *text, int size);
/* (host_rules.c's: whether the host's joins are locked) */
boolean host_rules_joins_locked(void);

/* ---------- globals */

static struct
{
	boolean registered;
	/* each machine's proved key */
	boolean has_key[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	unsigned char keys[HALO_PORT_MAXIMUM_NETWORK_MACHINES][32];
	/* each signed-in player told their role and where it comes from (a
	notice, private to them), again when it changes */
	boolean tell[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	char told[HALO_PORT_MAXIMUM_NETWORK_MACHINES][128];
	/* each machine's verified players (Delta Peer's identity: docs/delta.md,
	"Verified names"), by local player; a machine to check against bans.txt
	once it proved someone */
	boolean verified[HALO_PORT_MAXIMUM_NETWORK_MACHINES][LOCAL_PLAYERS];
	struct delta_peer_identity identities[HALO_PORT_MAXIMUM_NETWORK_MACHINES][LOCAL_PLAYERS];
	boolean check_bans[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
} moderation;

/* a machine's role here: its moderator key's (when it signed in with one),
or one of its verified accounts' that may moderate (the moderators file's
account lines, the site's list by handle), whichever is higher; who and
where it comes from (source: may be NULL) */
static int machine_role(
	long machine_index,
	unsigned char const *key,
	unsigned int *permissions,
	char *who,
	int who_size,
	char *source,
	int source_size)
{
	char found[64];
	int best = 0;
	long local;

	if (who && who_size > 0)
		who[0] = 0;
	if (source && source_size > 0)
		source[0] = 0;
	if (key)
	{
		best = server_roles_key_role(key, NULL, who, who_size);
		if (best && source)
			server_roles_key_source(key, source, source_size);
	}
	if (machine_index >= 0 && machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES)
	{
		for (local = 0; local < LOCAL_PLAYERS; local++)
		{
			struct delta_peer_identity const *identity = &moderation.identities[machine_index][local];
			int role;

			if (!moderation.verified[machine_index][local])
				continue;
			role = server_roles_verified_role(identity->account, identity->handle, identity->bound,
				(identity->capabilities & IDENTITY_CAN_MODERATE) != 0, NULL, found, sizeof(found));
			if (role > best)
			{
				best = role;
				if (who && who_size > 0)
					snprintf(who, (size_t)who_size, "%s", found);
				if (source)
					server_roles_account_source(identity->account, identity->handle, source, source_size);
			}
		}
	}
	if (permissions)
		*permissions = server_role_permissions(best);
	return best;
}

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
		moderation.told[machine_index][0] = 0;
		moderation.tell[machine_index] = TRUE;
		server_key_short(key, text);
		error(_error_silent, "dedicated: machine %d proved its moderator key %s (%s%s%s)", machine_index, text,
			server_role_name(role), who[0] ? ": " : "", who);
	}
	else
	{
		moderation.has_key[machine_index] = FALSE;
		moderation.told[machine_index][0] = 0;
	}
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
static int hook_machine_role(
	void *context,
	int machine_index,
	unsigned char const *key,
	delta_u32 *permissions,
	delta_u32 *ban_minutes)
{
	unsigned int granted = 0;
	int role = machine_role(machine_index, key, &granted, NULL, 0, NULL, 0);

	(void)context;
	if (permissions)
		*permissions = (delta_u32)granted;
	if (ban_minutes)
		*ban_minutes = 7 * 24 * 60;
	return role;
}

/* a machine's player proved who they are, or no longer is: kept for its
role, sv_players and bans.txt's accounts (a banned account or device is
dropped once it proves itself: server_moderation_update) */
static void hook_machine_identity(
	void *context,
	int machine_index,
	int local_player,
	struct delta_peer_identity const *identity)
{
	(void)context;
	if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES || local_player < 0 ||
		local_player >= LOCAL_PLAYERS)
	{
		return;
	}
	/* (Delta Peer gives moderation bound proofs alone; one that is not
	bound is never kept, here too) */
	if (identity && !identity->bound)
		identity = NULL;
	if (identity)
	{
		moderation.identities[machine_index][local_player] = *identity;
		moderation.verified[machine_index][local_player] = TRUE;
		moderation.check_bans[machine_index] = TRUE;
		error(_error_silent, "dedicated: machine %d's player %d is verified as @%s (%s)", machine_index, local_player,
			identity->handle[0] ? identity->handle : "-", delta_peer_platform_name(identity->platform));
	}
	else
	{
		csmemset(&moderation.identities[machine_index][local_player], 0,
			sizeof(moderation.identities[machine_index][local_player]));
		moderation.verified[machine_index][local_player] = FALSE;
	}
	moderation.told[machine_index][0] = 0;
	moderation.tell[machine_index] = moderation.has_key[machine_index];
}

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
	char reason_word[COMMAND_LINE_MAXIMUM_LENGTH + 1] = "";
	char name[48];
	char actor[96];
	char who[64];
	char key_text[9];
	unsigned int permissions = 0;
	long target = 0;
	int role = machine_role(machine_index, key, &permissions, who, sizeof(who), NULL, 0);

	(void)context;
	if (reason && reason[0])
		quoted(reason, reason_word, sizeof(reason_word));
	if (action == _delta_moderation_action_warn || action == _delta_moderation_action_kick ||
		action == _delta_moderation_action_ban || action == _delta_moderation_action_mute ||
		action == _delta_moderation_action_unmute)
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
	case _delta_moderation_action_mute:
		if (minutes > 0)
			snprintf(line, sizeof(line), "sv_mute %ld %d", target, minutes > 24 * 60 ? 24 * 60 : minutes);
		else
			snprintf(line, sizeof(line), "sv_mute %ld", target);
		break;
	case _delta_moderation_action_unmute:
		snprintf(line, sizeof(line), "sv_unmute %ld", target);
		break;
	case _delta_moderation_action_end_game:
		snprintf(line, sizeof(line), "sv_end_game");
		break;
	case _delta_moderation_action_next_map:
		snprintf(line, sizeof(line), "sv_mapcycle_next");
		break;
	case _delta_moderation_action_lock:
		snprintf(line, sizeof(line), "sv_lock");
		break;
	case _delta_moderation_action_unlock:
		snprintf(line, sizeof(line), "sv_unlock");
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

/* MOD_STATE's actions byte: lock and unlock taken, and whether joins are
locked now */
static int hook_actions(
	void *context)
{
	(void)context;
	return DELTA_WIRE_MOD_STATE_LOCK | (host_rules_joins_locked() ? DELTA_WIRE_MOD_STATE_LOCKED : 0);
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

/* a signed-in player told their role here and where it comes from (the
moderators file, a panel account, their site profile), if it changed since
they were last told: an info notice to their machine alone, which the
Moderation screen shows under the role */
static void tell_role(
	long machine_index)
{
	unsigned char key[32];
	char source[96];
	char text[128];
	int role;

	if (!server_moderation_machine_key(machine_index, key))
		return;
	role = machine_role(machine_index, key, NULL, NULL, 0, source, sizeof(source));
	if (role)
		snprintf(text, sizeof(text), "Your role here: %s, from %s.", server_role_name(role), source);
	else if (moderation.told[machine_index][0])
		snprintf(text, sizeof(text), "You no longer have a role here.");
	else
		text[0] = 0;
	if (!text[0] || !strcmp(text, moderation.told[machine_index]))
		return;
	if (delta_peer_game_moderation_notice((int)machine_index, _delta_moderation_notice_info, text))
		snprintf(moderation.told[machine_index], sizeof(moderation.told[machine_index]), "%s", text);
}

/* (text chat's) whether a machine's player signed in with a key that has a
role here: their lines are marked as a moderator's */
static int machine_is_moderator(
	int machine_index)
{
	unsigned char key[32];

	return server_moderation_machine_key(machine_index, key) &&
		machine_role(machine_index, key, NULL, NULL, 0, NULL, 0) > 0;
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
			NULL, hook_machine_key, hook_key_role, hook_action, hook_bind_answer, hook_machine_identity,
			hook_machine_role, hook_actions
		};

		moderation.registered = TRUE;
		delta_peer_game_set_moderation_host(&host);
		chat_set_moderator_check(machine_is_moderator);
	}
	/* the roles changed elsewhere (the panel's accounts, the site's list):
	the moderators' games told again */
	if (server_roles_take_changed())
	{
		long machine;

		delta_peer_game_moderation_roles_changed();
		for (machine = 0; machine < HALO_PORT_MAXIMUM_NETWORK_MACHINES; machine++)
			moderation.tell[machine] = moderation.has_key[machine];
	}
	{
		long machine;

		for (machine = 0; machine < HALO_PORT_MAXIMUM_NETWORK_MACHINES; machine++)
		{
			if (moderation.tell[machine])
			{
				moderation.tell[machine] = FALSE;
				tell_role(machine);
			}
		}
	}
	/* players' reports (server_reports.c) */
	server_reports_update();
	/* the shared lists changed (the site's, an exception here): every
	machine that proved someone is checked again */
	if (server_bans_take_changed())
	{
		long machine;
		long local;

		for (machine = 0; machine < HALO_PORT_MAXIMUM_NETWORK_MACHINES; machine++)
		{
			for (local = 0; local < LOCAL_PLAYERS; local++)
			{
				if (moderation.verified[machine][local])
					moderation.check_bans[machine] = TRUE;
			}
		}
	}
	/* a machine that proved who its players are: dropped when one of their
	accounts, or the device, is banned (bans.txt's account= and device=, or
	a shared list's entry this server takes) */
	{
		long machine;

		for (machine = 0; machine < HALO_PORT_MAXIMUM_NETWORK_MACHINES; machine++)
		{
			long number;
			char line[192], result[160];

			if (!moderation.check_bans[machine])
				continue;
			moderation.check_bans[machine] = FALSE;
			if (!network_distributed_identity_banned(machine))
			{
				char identity[256], list_name[SERVER_BAN_LIST_NAME_SIZE], ref[SERVER_BAN_REF_SIZE];
				char reason[96];

				if (delta_peer_game_machine_identity_text((int)machine, identity, (int)sizeof(identity)) <= 0 ||
					!server_bans_shared_match(identity, (long long)time(NULL), list_name, (int)sizeof(list_name), ref,
						(int)sizeof(ref)))
				{
					continue;
				}
				number = server_commands_machine_player(machine, NULL, 0);
				if (!number)
					continue;
				error(_error_silent, "dedicated: machine %ld's verified account or device is on the shared list %s: "
					"dropped", machine, list_name);
				/* (the list's name kept to what a command's reason takes) */
				{
					char *cursor;

					snprintf(reason, sizeof(reason), "Banned on %s", list_name);
					for (cursor = reason; *cursor; cursor++)
					{
						if (*cursor == '"' || *cursor == ';' || (unsigned char)*cursor < 0x20 ||
							(unsigned char)*cursor > 0x7E)
						{
							*cursor = ' ';
						}
					}
				}
				snprintf(line, sizeof(line), "sv_kick %ld \"%s\"", number, reason);
				server_commands_run_as(line, "game", "shared ban list", 3, 0xFFFFFFFFu, result, sizeof(result));
				continue;
			}
			number = server_commands_machine_player(machine, NULL, 0);
			if (!number)
				continue;
			error(_error_silent, "dedicated: machine %ld's verified account or device is banned here: dropped",
				machine);
			snprintf(line, sizeof(line), "sv_kick %ld \"banned on this server\"", number);
			server_commands_run_as(line, "game", "bans.txt", 3, 0xFFFFFFFFu, result, sizeof(result));
		}
	}
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
	snprintf(notice, sizeof(notice), "%s%s%s", title ? title : "", title ? ": " : "", text && text[0] ? text : "-");
	return delta_peer_game_moderation_notice((int)machine_index, warning ? _delta_moderation_notice_warning :
		_delta_moderation_notice_info, notice) ? TRUE : FALSE;
}

boolean server_moderation_machine_profile(
	long machine_index,
	long local_player,
	char *handle,
	int handle_size,
	char const **platform,
	int *role)
{
	struct delta_peer_identity const *identity;

	if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES || local_player < 0 ||
		local_player >= LOCAL_PLAYERS || !moderation.verified[machine_index][local_player])
	{
		return FALSE;
	}
	identity = &moderation.identities[machine_index][local_player];
	/* (in the capitals the proof gave: Delta Peer showed it is the handle's) */
	snprintf(handle, (size_t)handle_size, "%s", identity->tag[0] ? identity->tag : identity->handle);
	if (platform)
		*platform = delta_peer_platform_name(identity->platform);
	if (role)
	{
		*role = server_roles_verified_role(identity->account, identity->handle, identity->bound,
			(identity->capabilities & IDENTITY_CAN_MODERATE) != 0, NULL, NULL, 0);
	}
	return TRUE;
}

boolean server_moderation_machine_account(
	long machine_index,
	long local_player,
	unsigned char *account,
	char *handle,
	int handle_size)
{
	if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES || local_player < 0 ||
		local_player >= LOCAL_PLAYERS || !moderation.verified[machine_index][local_player])
	{
		return FALSE;
	}
	csmemcpy(account, moderation.identities[machine_index][local_player].account, 16);
	snprintf(handle, (size_t)handle_size, "%s", moderation.identities[machine_index][local_player].handle);
	return TRUE;
}

void server_moderation_roles_changed(
	void)
{
	long machine;

	delta_peer_game_moderation_roles_changed();
	for (machine = 0; machine < HALO_PORT_MAXIMUM_NETWORK_MACHINES; machine++)
		moderation.tell[machine] = moderation.has_key[machine];
}

#endif
#endif
