/*
SERVER_CONDUCT.C

A dedicated server's betrayal and AFK limits (server_conduct.h). The
players of the game in progress are looked at each frame, on the host
alone, from what the host already has: each player's betrayals (the game's
own statistics, which its carnage report shows) and when their input last
changed (network_distributed.c, as their machine sends it). A warning goes
to the player's game as sv_warn's does (a Chupathingy game's Delta Peer
notice), else as the server's line in their chat (an OpenCE game's chat,
while it is bridged); a kick and a ban are sv_kick's and sv_ban's. Nothing
of the game's wire is new or changed. Nobody's address is logged or shown:
it is kept in memory alone, as the key of a machine that told no hardware
id, to know a second kick from a first.
*/

#ifdef HALO_GAME_BROWSER

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_game_protocol.h"
#include "networking/network_server_manager.h"
#include "conduct_rules.h"
#include "dedicated.h"
#include "server_conduct.h"
#include "server_moderation.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef HALO_SERVER
/* (the game's own dedicated mode: no roles, audit or Delta Control) */
#define server_audit(via, actor, role, action, target, reason, ok, detail) ((void)0)
#define server_moderation_notice(machine, warning, title, text) ((void)(machine), FALSE)
#define server_moderation_machine_key(machine, key) ((void)(machine), (void)(key), FALSE)
#define server_roles_key_role(key, permissions, who, who_size) 0
#endif

/* (chat.c's: a server message to one machine) */
int chat_host_notice(int machine_index, char const *text);
/* (network_distributed.c's: when a player's input last changed, and the
host's bans) */
unsigned long network_distributed_player_input_time(short player_index);
void network_distributed_ban_until(long machine_index, unsigned long address, char const *names, char const *reason,
	unsigned long until);

/* ---------- constants */

enum
{
	/* a kick that could not be done (the machine still joining): tried
	again this long after */
	CONDUCT_RETRY_MILLISECONDS = 1000,
};

#define BETRAYAL_ACTOR "betrayal limit"
#define AFK_ACTOR "afk limit"

/* ---------- globals */

static struct
{
	int betrayal_limit;
	int afk_minutes;
	struct conduct_kick_history history;
	/* each player slot (the game's player's absolute index) */
	struct
	{
		/* the player it is (their datum index), NONE for none */
		long player_index;
		long machine_index;
		/* their betrayals as the game counted them when last looked at, and
		those counted against the limit since they were first seen */
		short statistics_betrayals;
		int betrayals;
		/* since when they are idle at the most (system_milliseconds: when
		they were first seen this game), and whether they were warned */
		unsigned long seen_time;
		boolean afk_warned;
		/* a kick to do (when to try again), 0 none; why; and whether it is
		a ban (a second automatic kick within a day) */
		unsigned long kick_time;
		boolean kick_for_afk;
		boolean kick_ban;
	} slots[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
} server_conduct;

/* ---------- private code */

static void forget_players(
	void)
{
	long index;

	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		csmemset(&server_conduct.slots[index], 0, sizeof(server_conduct.slots[index]));
		server_conduct.slots[index].player_index = NONE;
	}
}

static void player_name(
	struct player_datum const *player,
	char *text,
	long size)
{
	long index;

	for (index = 0; index < (long)NUMBEROF(player->name) && player->name[index] && index < size - 1; index++)
		text[index] = player_name_character_ascii(player->name[index]);
	text[index] = 0;
}

/* a message in a player's game: a Chupathingy game's moderation notice,
else a line in their chat (an OpenCE game's, while it is bridged). Whether
it was shown */
static boolean tell(
	long machine_index,
	char const *title,
	char const *text)
{
	char line[128];

	if (server_moderation_notice(machine_index, TRUE, title, text))
		return TRUE;
	snprintf(line, sizeof(line), "%s%s%s", title ? title : "Server warning", ": ", text);
	return chat_host_notice((int)machine_index, line) != 0;
}

/* whom a machine is, to know their second automatic kick from a first: its
hardware id, else the moderator key its game proved, else its address
(kept here alone: never logged, never shown) */
static void machine_key(
	long machine_index,
	char *key,
	long size)
{
	char const *hardware_id = network_game_server_machine_hardware_id(machine_index);
	unsigned char moderator_key[32];

	key[0] = 0;
	if (hardware_id && hardware_id[0])
		snprintf(key, (size_t)size, "h:%s", hardware_id);
	else if (server_moderation_machine_key(machine_index, moderator_key))
	{
		long index;

		snprintf(key, (size_t)size, "k:");
		for (index = 0; index < 16 && 2 + 2 * index + 2 < size; index++)
			snprintf(key + 2 + 2 * index, 3, "%02x", moderator_key[index]);
	}
	else if (network_game_server_machine_address(machine_index))
		snprintf(key, (size_t)size, "a:%08lx", network_game_server_machine_address(machine_index));
}

/* whether a machine's player is a moderator here (their game proved a key
with a role) */
static boolean machine_moderator(
	long machine_index)
{
	unsigned char key[32];

	return server_moderation_machine_key(machine_index, key) && server_roles_key_role(key, NULL, NULL, 0) > 0;
}

/* a slot's kick done (TRUE), or not yet (the machine still joining) */
static boolean kick(
	long slot,
	struct player_datum const *player)
{
	long machine_index = server_conduct.slots[slot].machine_index;
	boolean afk = server_conduct.slots[slot].kick_for_afk;
	char name[16];
	char reason[96];

	player_name(player, name, sizeof(name));
	if (afk)
	{
		snprintf(reason, sizeof(reason), "no input for %d minute%s", server_conduct.afk_minutes,
			server_conduct.afk_minutes == 1 ? "" : "s");
		tell(machine_index, "Kicked", "AFK: no input for too long");
		if (!network_game_server_drop_machine(machine_index, _rejection_code_game_is_closed))
			return FALSE;
		server_audit("server", AFK_ACTOR, SERVER_ROLE_OWNER, "sv_kick", name, reason, TRUE, NULL);
		error(_error_silent, "dedicated: kicked %s: %s", name, reason);
		return TRUE;
	}
	snprintf(reason, sizeof(reason), "%d betrayals in a game", server_conduct.slots[slot].betrayals);
	{
		unsigned long address = network_game_server_machine_address(machine_index);
		char ban_reason[128];
		boolean repeat = server_conduct.slots[slot].kick_ban;

		/* (a second automatic kick within a day: a short ban, sv_ban's) */
		if (repeat)
		{
			tell(machine_index, "Banned", "Too many betrayals, again: 30 minutes");
			if (!network_game_server_drop_machine(machine_index, _rejection_code_blacklisted_machine))
				return FALSE;
			snprintf(ban_reason, sizeof(ban_reason), "banned by the %s for 30m: %s, the second kick in a day",
				BETRAYAL_ACTOR, reason);
			network_distributed_ban_until(machine_index, address, name, ban_reason,
				(unsigned long)time(NULL) + CONDUCT_BAN_SECONDS);
			server_audit("server", BETRAYAL_ACTOR, SERVER_ROLE_OWNER, "sv_ban", name, reason, TRUE,
				"30m: the second automatic kick within 24 hours");
		}
		else
		{
			tell(machine_index, "Kicked", "Too many betrayals");
			if (!network_game_server_drop_machine(machine_index, _rejection_code_game_is_closed))
				return FALSE;
			server_audit("server", BETRAYAL_ACTOR, SERVER_ROLE_OWNER, "sv_kick", name, reason, TRUE, NULL);
		}
		error(_error_silent, "dedicated: %s %s: %s", repeat ? "banned for 30 minutes" : "kicked", name, reason);
	}
	return TRUE;
}

/* ---------- public code */

void server_conduct_set_betrayal_limit(
	int betrayals)
{
	server_conduct.betrayal_limit = betrayals < 0 ? 0 : MIN(betrayals, CONDUCT_MAXIMUM_BETRAYALS);
}

void server_conduct_set_afk_minutes(
	int minutes)
{
	server_conduct.afk_minutes = minutes < 0 ? 0 : MIN(minutes, CONDUCT_MAXIMUM_AFK_MINUTES);
}

int server_conduct_betrayal_limit(
	void)
{
	return server_conduct.betrayal_limit;
}

int server_conduct_afk_minutes(
	void)
{
	return server_conduct.afk_minutes;
}

void server_conduct_update(
	void)
{
	static boolean started;
	struct dedicated_status status;
	struct data_iterator iterator;
	struct player_datum *player;
	unsigned long now = system_milliseconds();
	boolean present[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long slot;

	if (!started)
	{
		started = TRUE;
		forget_players();
	}
	/* (in a game in progress alone: never the lobby or the carnage report,
	and every count starts over with the next game) */
	if (!server_conduct.betrayal_limit && !server_conduct.afk_minutes)
	{
		forget_players();
		return;
	}
	dedicated_server_get_status(&status);
	if (status.state != _dedicated_state_in_game || !global_network_game_server_get() || !game_in_progress() ||
		!game_engine_running() || !player_data)
	{
		forget_players();
		return;
	}
	csmemset(present, 0, sizeof(present));
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		long machine_index = player->network_player_data.machine_index;

		if (absolute < 0 || absolute >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS || player->quit_out_of_game ||
			machine_index < 0)
		{
			continue;
		}
		present[absolute] = TRUE;
		slot = absolute;
		/* (a player new to the slot: from now, and their betrayals as they are) */
		if (server_conduct.slots[slot].player_index != iterator.datum_index ||
			server_conduct.slots[slot].machine_index != machine_index)
		{
			csmemset(&server_conduct.slots[slot], 0, sizeof(server_conduct.slots[slot]));
			server_conduct.slots[slot].player_index = iterator.datum_index;
			server_conduct.slots[slot].machine_index = machine_index;
			server_conduct.slots[slot].statistics_betrayals = player->statistics.friendly_fire_kills;
			server_conduct.slots[slot].seen_time = now;
		}
		/* (a kick waiting: done, or tried again in a second) */
		if (server_conduct.slots[slot].kick_time)
		{
			if ((long)(now - server_conduct.slots[slot].kick_time) >= 0)
			{
				/* (done: the slot's next player starts afresh) */
				if (kick(slot, player))
					server_conduct.slots[slot].player_index = NONE;
				else
					server_conduct.slots[slot].kick_time = now + CONDUCT_RETRY_MILLISECONDS;
			}
			continue;
		}
		/* betrayals: the game's count went up */
		if (player->statistics.friendly_fire_kills > server_conduct.slots[slot].statistics_betrayals)
		{
			int previous = server_conduct.slots[slot].betrayals;
			int action;

			server_conduct.slots[slot].betrayals +=
				player->statistics.friendly_fire_kills - server_conduct.slots[slot].statistics_betrayals;
			action = conduct_betrayal_action(server_conduct.betrayal_limit, previous, server_conduct.slots[slot].betrayals);
			if (action == _conduct_warn)
			{
				char name[16];
				char reason[64];
				boolean shown;

				player_name(player, name, sizeof(name));
				shown = tell(machine_index, NULL, "Betrayals: one more and you are kicked");
				snprintf(reason, sizeof(reason), "%d betrayals in a game (kicked at %d)",
					server_conduct.slots[slot].betrayals, server_conduct.betrayal_limit);
				server_audit("server", BETRAYAL_ACTOR, SERVER_ROLE_OWNER, "sv_warn", name, reason, TRUE,
					shown ? NULL : "their game cannot show it");
				error(_error_silent, "dedicated: warned %s: %s", name, reason);
			}
			else if (action == _conduct_kick)
			{
				char key[CONDUCT_KEY_SIZE];

				/* (remembered once, as it is decided: whom, for a second
				kick's ban) */
				machine_key(machine_index, key, sizeof(key));
				server_conduct.slots[slot].kick_ban =
					conduct_note_automatic_kick(&server_conduct.history, key, (long long)time(NULL)) ? TRUE : FALSE;
				server_conduct.slots[slot].kick_time = now ? now : 1;
				server_conduct.slots[slot].kick_for_afk = FALSE;
			}
		}
		server_conduct.slots[slot].statistics_betrayals = player->statistics.friendly_fire_kills;
		/* AFK: since their input last changed, or since they came, whichever
		is later; only a machine whose input reaches the host this game (its
		players' inputs: network_distributed.c), and never a moderator */
		if (server_conduct.afk_minutes && !server_conduct.slots[slot].kick_time)
		{
			unsigned long input_time = network_distributed_player_input_time((short)absolute);
			unsigned long since;
			int action;

			if (!input_time || machine_moderator(machine_index))
				continue;
			since = (long)(input_time - server_conduct.slots[slot].seen_time) > 0 ? input_time :
				server_conduct.slots[slot].seen_time;
			if ((long)(now - since) < 0)
				since = now;
			action = conduct_afk_action(server_conduct.afk_minutes, now - since);
			if (action == _conduct_none)
				server_conduct.slots[slot].afk_warned = FALSE;
			else if (action == _conduct_warn && !server_conduct.slots[slot].afk_warned)
			{
				char name[16];
				boolean shown;

				server_conduct.slots[slot].afk_warned = TRUE;
				player_name(player, name, sizeof(name));
				shown = tell(machine_index, NULL, "AFK: move or you are kicked in 30 seconds");
				server_audit("server", AFK_ACTOR, SERVER_ROLE_OWNER, "sv_warn", name, "no input", TRUE,
					shown ? NULL : "their game cannot show it");
				error(_error_silent, "dedicated: warned %s: no input", name);
			}
			else if (action == _conduct_kick)
			{
				server_conduct.slots[slot].kick_time = now ? now : 1;
				server_conduct.slots[slot].kick_for_afk = TRUE;
			}
		}
	}
	/* (a slot whose player left: forgotten) */
	for (slot = 0; slot < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; slot++)
	{
		if (!present[slot] && server_conduct.slots[slot].player_index != NONE)
		{
			csmemset(&server_conduct.slots[slot], 0, sizeof(server_conduct.slots[slot]));
			server_conduct.slots[slot].player_index = NONE;
		}
	}
}

#endif
