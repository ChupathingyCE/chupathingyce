/*
SERVER_REPORTS.C

Players' reports, as the dedicated server takes them (server_reports.h;
docs/delta.md, Reports). The server is Delta Peer's report host: a
ChupathingyCE player's REPORT names another player of the game and a
reason. This unit checks it (the player there, not the reporter's own, a
player once a game from one machine, 3 a minute and 10 a game from one
machine) and makes the report the moderators see: both players' names,
their verified handles and accounts (docs/delta.md, "Verified names") when
they proved them, the map, the game type and the server's refusals of the
reported machine this game (host_evidence.h). Never an address. A clip is
asked for only when the server is linked to the site, where it goes.
*/

#ifdef HALO_GAME_BROWSER
#ifdef HALO_SERVER

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "dedicated.h"
#include "server_moderation.h"
#include "server_reports.h"
#include "host_evidence.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* (Delta Peer's, port/linux/src/delta_wire.h and delta_peer.h, which the
game's units do not include: plain types, the same layout) */
typedef unsigned int delta_u32;

struct delta_wire_report
{
	delta_u32 sequence;
	unsigned char target_machine;
	unsigned char target_player;
	unsigned char reason;
	unsigned char flags;
	unsigned char clip_token[16];
};

struct delta_peer_report_host
{
	void *context;
	int (*report)(void *context, int machine_index, struct delta_wire_report const *report, int *flags);
};

enum
{
	DELTA_WIRE_REPORT_CLIP = 0x01,
	DELTA_WIRE_REPORT_SEND_CLIP = 0x01,
	_delta_report_taken = 0,
	_delta_report_too_soon = 1,
	_delta_report_no_player = 2,
	_delta_report_not_taken = 3,
	_delta_report_already = 4,
};

void delta_peer_game_set_report_host(struct delta_peer_report_host const *host);
int delta_peer_game_moderation_capable(int machine_index);
/* (the link's: control_link.c) */
int server_link_linked(void);

/* ---------- constants */

enum
{
	REPORTS_PER_MINUTE = 3,
	REPORTS_PER_GAME = 10,
	/* the players one machine reported this game, remembered */
	REPORTED_KEPT = 16,
};

/* ---------- globals */

static struct
{
	boolean registered;
	/* the game the counts are for: they start over when a new one starts */
	boolean was_running;
	struct
	{
		/* when its last reports were taken (unix seconds), its count this
		game, and the players it reported (absolute index + 1; 0 none) */
		long long times[REPORTS_PER_MINUTE];
		int count;
		long reported[REPORTED_KEPT];
	} machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
} reports;

/* ---------- private code */

static void ascii_name(
	wchar_t const *name,
	long length,
	char *text,
	long size)
{
	long index;

	for (index = 0; index < length && name[index] && index < size - 1; index++)
		text[index] = player_name_character_ascii(name[index]);
	text[index] = 0;
}

/* a machine's local player's verified handle and account, when they
proved one (server_moderation.c) */
static void player_identity(
	long machine_index,
	long local_player,
	struct server_report_player *player)
{
	unsigned char account[16];
	char handle[SERVER_REPORT_HANDLE_SIZE];
	long index;

	player->delta = delta_peer_game_moderation_capable((int)machine_index) ? 1 : 0;
	if (!server_moderation_machine_account(machine_index, local_player, account, handle, sizeof(handle)))
		return;
	snprintf(player->handle, sizeof(player->handle), "%s", handle);
	for (index = 0; index < 16; index++)
		snprintf(player->account + 2 * index, 3, "%02x", account[index]);
}

/* the player at an absolute index of player_data, NONE none */
static long player_at(
	long absolute)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (!player_data)
		return NONE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) == absolute)
			return iterator.datum_index;
	}
	return NONE;
}

static int hook_report(
	void *context,
	int machine_index,
	struct delta_wire_report const *wire,
	int *flags)
{
	static struct server_report report;
	struct player_datum *target;
	struct dedicated_status status;
	long target_index;
	long long now = (long long)time(NULL);
	long slot;
	int index;

	(void)context;
	*flags = 0;
	if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES || !game_engine_running())
		return _delta_report_not_taken;
	target_index = player_at(wire->target_player);
	if (target_index == NONE)
		return _delta_report_no_player;
	target = player_get(target_index);
	if ((long)target->network_player_data.machine_index != (long)wire->target_machine ||
		(long)wire->target_machine == (long)machine_index)
	{
		return _delta_report_no_player;
	}
	/* (once a game a player, from one machine) */
	for (slot = 0; slot < REPORTED_KEPT; slot++)
	{
		if (reports.machines[machine_index].reported[slot] == (long)wire->target_player + 1)
			return _delta_report_already;
	}
	/* (3 a minute, 10 a game) */
	if (reports.machines[machine_index].count >= REPORTS_PER_GAME ||
		now - reports.machines[machine_index].times[0] < 60)
	{
		return _delta_report_too_soon;
	}
	csmemset(&report, 0, sizeof(report));
	report.time = now;
	report.reason = wire->reason;
	ascii_name(target->name, NUMBEROF(target->name), report.target.name, sizeof(report.target.name));
	player_identity(wire->target_machine, target->network_player_data.controller_index, &report.target);
	if (!server_commands_machine_player(machine_index, report.reporter.name, sizeof(report.reporter.name)))
		snprintf(report.reporter.name, sizeof(report.reporter.name), "machine %d", machine_index);
	player_identity(machine_index, 0, &report.reporter);
	report.target_number = server_commands_machine_player(wire->target_machine, NULL, 0);
	dedicated_server_get_status(&status);
	snprintf(report.map, sizeof(report.map), "%s", status.map);
	snprintf(report.game_type, sizeof(report.game_type), "%s", status.variant);
	report.evidence[0] = (int)host_evidence_count(wire->target_machine, _host_evidence_movement);
	report.evidence[1] = (int)host_evidence_count(wire->target_machine, _host_evidence_vehicle);
	report.evidence[2] = (int)host_evidence_count(wire->target_machine, _host_evidence_hit_report);
	report.evidence[3] = (int)host_evidence_count(wire->target_machine, _host_evidence_clock);
	/* (a clip only where it can go: a linked server's site, for a Delta
	player's report) */
	if ((wire->flags & DELTA_WIRE_REPORT_CLIP) && server_link_linked() && report.target.delta)
	{
		for (index = 0; index < 16; index++)
			snprintf(report.clip + 2 * index, 3, "%02x", wire->clip_token[index]);
		*flags = DELTA_WIRE_REPORT_SEND_CLIP;
	}
	if (!server_reports_add(&report))
		return _delta_report_not_taken;
	/* (counted) */
	for (slot = 0; slot < REPORTS_PER_MINUTE - 1; slot++)
		reports.machines[machine_index].times[slot] = reports.machines[machine_index].times[slot + 1];
	reports.machines[machine_index].times[REPORTS_PER_MINUTE - 1] = now;
	reports.machines[machine_index].count++;
	for (slot = 0; slot < REPORTED_KEPT && reports.machines[machine_index].reported[slot]; slot++)
		;
	if (slot < REPORTED_KEPT)
		reports.machines[machine_index].reported[slot] = (long)wire->target_player + 1;
	error(_error_silent, "dedicated: report #%u: %s reported %s for %s", report.id, report.reporter.name,
		report.target.name, server_report_reason_name(report.reason));
	return _delta_report_taken;
}

/* ---------- public code */

void server_reports_update(
	void)
{
	boolean running = game_engine_running();

	if (!reports.registered)
	{
		static struct delta_peer_report_host const host = { NULL, hook_report };

		reports.registered = TRUE;
		delta_peer_game_set_report_host(&host);
	}
	/* (a new game: each machine's counts start over) */
	if (running && !reports.was_running)
		csmemset(reports.machines, 0, sizeof(reports.machines));
	reports.was_running = running;
}

#endif
#endif
