/*
SERVER_REPORTS.C

Players' reports, as the dedicated server takes them (server_reports.h;
docs/delta.md, Reports). The server is Delta Peer's report host: a
ChupathingyCE player's REPORT names another player of the game and a
reason. This unit checks it (the player there, not the reporter's own, a
player once a game from one reporter, 3 a minute and 10 a game from one
reporter, at most 5 a game of one player: each counted by their verified
account, else by the player this game, never by the machine's slot) and
makes the report the moderators see: both players' names,
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
	int (*report)(void *context, int machine_index, struct delta_wire_report const *report, int *flags,
		unsigned char *clip_token);
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

/* ---------- globals */

static struct
{
	boolean registered;
	/* the game the counts are for: they start over when a new one starts */
	boolean was_running;
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
proved one bound to this server (server_moderation.c hears of no other) */
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

/* a machine's first player in the game (its lowest controller), NONE none */
static long machine_player(
	long machine_index)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long found = NONE;
	long controller = 0x7FFFFFFF;

	if (!player_data)
		return NONE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if ((long)player->network_player_data.machine_index == machine_index && !player->quit_out_of_game &&
			(long)player->network_player_data.controller_index < controller)
		{
			found = iterator.datum_index;
			controller = (long)player->network_player_data.controller_index;
		}
	}
	return found;
}

/* who a machine's local player is, for the counts */
static void who_is(
	long machine_index,
	long local_player,
	long player_index,
	struct server_report_who *who)
{
	char handle[SERVER_REPORT_HANDLE_SIZE];

	csmemset(who, 0, sizeof(*who));
	who->player = player_index;
	who->has_account = server_moderation_machine_account(machine_index, local_player, who->account, handle,
		sizeof(handle)) ? 1 : 0;
}

static int hex_value(
	char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0;
}

static int hook_report(
	void *context,
	int machine_index,
	struct delta_wire_report const *wire,
	int *flags,
	unsigned char *clip_token)
{
	static struct server_report report;
	struct player_datum *target;
	struct dedicated_status status;
	struct server_report_who reporter_who, target_who;
	long target_index, reporter_index;
	int limit;
	long long now = (long long)time(NULL);
	int index;

	(void)context;
	*flags = 0;
	if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES || !game_engine_running())
		return _delta_report_not_taken;
	/* (the target: the player at that absolute index of the game's
	players, on the machine the report names) */
	target_index = player_at(wire->target_player);
	if (target_index == NONE)
		return _delta_report_no_player;
	target = player_get(target_index);
	if ((long)target->network_player_data.machine_index != (long)wire->target_machine ||
		(long)wire->target_machine == (long)machine_index)
	{
		return _delta_report_no_player;
	}
	reporter_index = machine_player(machine_index);
	if (reporter_index == NONE)
		return _delta_report_not_taken;
	/* (counted by who reports and who is reported: a verified account, else
	the player this game. A player back after leaving, or a new one in a
	slot someone left, is someone else; a verified one never is) */
	who_is(machine_index, (long)player_get(reporter_index)->network_player_data.controller_index, reporter_index,
		&reporter_who);
	who_is((long)wire->target_machine, (long)target->network_player_data.controller_index, target_index, &target_who);
	limit = server_reports_limit_check(&reporter_who, &target_who, now);
	if (limit != SERVER_REPORT_LIMIT_OK)
		return limit == SERVER_REPORT_LIMIT_ALREADY ? _delta_report_already : _delta_report_too_soon;
	csmemset(&report, 0, sizeof(report));
	report.time = now;
	report.reason = wire->reason;
	ascii_name(target->name, NUMBEROF(target->name), report.target.name, sizeof(report.target.name));
	player_identity(wire->target_machine, target->network_player_data.controller_index, &report.target);
	if (!server_commands_machine_player(machine_index, report.reporter.name, sizeof(report.reporter.name)))
		snprintf(report.reporter.name, sizeof(report.reporter.name), "machine %d", machine_index);
	player_identity(machine_index, player_get(reporter_index)->network_player_data.controller_index, &report.reporter);
	report.target_number = server_commands_machine_player(wire->target_machine, NULL, 0);
	dedicated_server_get_status(&status);
	snprintf(report.map, sizeof(report.map), "%s", status.map);
	snprintf(report.game_type, sizeof(report.game_type), "%s", status.variant);
	report.evidence[0] = (int)host_evidence_count(wire->target_machine, _host_evidence_movement);
	report.evidence[1] = (int)host_evidence_count(wire->target_machine, _host_evidence_vehicle);
	report.evidence[2] = (int)host_evidence_count(wire->target_machine, _host_evidence_hit_report);
	report.evidence[3] = (int)host_evidence_count(wire->target_machine, _host_evidence_clock);
	/* (a clip only where it can go: a linked server's site, for a Delta
	player's report. Its token is made here, never the reporter's: the
	site takes a clip only for a token a report of this server names) */
	report.want_clip = (wire->flags & DELTA_WIRE_REPORT_CLIP) && server_link_linked() && report.target.delta;
	if (!server_reports_add(&report))
		return _delta_report_not_taken;
	if (report.want_clip && report.clip[0])
	{
		for (index = 0; index < 16; index++)
			clip_token[index] = (unsigned char)(hex_value(report.clip[2 * index]) << 4 | hex_value(report.clip[2 * index + 1]));
		*flags = DELTA_WIRE_REPORT_SEND_CLIP;
	}
	server_reports_limit_count(&reporter_who, &target_who, now);
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
	/* (a new game: the counts start over) */
	if (running && !reports.was_running)
		server_reports_limit_reset();
	reports.was_running = running;
}

#endif
#endif
