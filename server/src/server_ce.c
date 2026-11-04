/*
SERVER_CE.C

Halo Custom Edition 1.0.10 players in the dedicated server's game
(server/platform/ce_transport.h, ce_messages.h), while the server plays a
map a Custom Edition client has too: a Custom Edition map, the same .map
file on both sides, so the tags' handles match without any table.

A Custom Edition player is one of the server machine's own players (as a
split screen's would be), whose actions come from its client's updates
(server_ce_local_action, from player_control.c) instead of a controller.
So everyone else's game sees a player like any other, and the game runs
it like any other.

What its client is told comes from the game's own state, each tick:
  - once it has loaded the map: every player (added), and each player's
    biped and weapons as they appear (created, spawned), and taken away
    as they go (deleted);
  - every tick: each other player's position and actions, which its game
    animates and moves.

Only in the server program (HALO_SERVER), and only while joins are taken
(HALO_DEDICATED_CE_ACCEPT=1, for now a development switch: ce_joins stays
off by default and refuses the joins otherwise).
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "items/weapons.h"
#include "objects/objects.h"
#include "units/units.h"
#include "dedicated.h"
#include "server_admin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HALO_SERVER

#include "../platform/ce_transport.h"
#include "../platform/ce_messages.h"
#include "../platform/gamespy_listing.h"

/* (network_server_manager_internal.h's) */
struct network_game *network_game_server_get_game(struct network_game_server *server);
/* (port/linux/game/network_distributed.c's: bans.txt) */
boolean network_distributed_banned(unsigned long address, char const *hardware_id);

enum
{
	/* a Custom Edition player is one of this machine's local players */
	CE_LOCAL_PLAYERS = MAXIMUM_NUMBER_OF_LOCAL_PLAYERS,
	/* the wire's object numbers (9 bits; 0 is none) */
	NETWORK_OBJECTS = 511,
	TRACKED_PLAYERS = 32,
	PAYLOAD_SIZE = 1200,
	/* a client's own biped's place, this often */
	LOCAL_UPDATE_TICKS = 15,
	/* a client's updates waiting, at most, before they are played faster */
	CAUGHT_UP_UPDATES = 2,
	/* a biped's vitality and grenades, at most this often */
	BIPED_UPDATE_TICKS = 3,
	BIPED_REFRESH_TICKS = 30,
	/* a moving or changed item's place and rounds, at most this often */
	ITEM_UPDATE_TICKS = 5,
	/* a join waiting for a game to start or a seat, at most (a client gives
	up on an unanswered join after about 15 seconds) */
	JOIN_WAIT_MILLISECONDS = 12000,
};

/* where a client was told an item is */
enum
{
	ITEM_UNKNOWN = 0,
	ITEM_WORLD,
	ITEM_HELD,
};

struct ce_client
{
	boolean active;
	int peer;
	char name[13];
	unsigned long address;
	boolean added;
	boolean loaded;
	/* what this client was told of each player (by absolute index):
	added, and the unit created for it */
	long announced[TRACKED_PLAYERS];
	long unit_sent[TRACKED_PLAYERS];
	/* each player's last unit it was told died: taken away once our game
	takes the body away */
	long body[TRACKED_PLAYERS];
	/* each player's biped as it was told last (biped_state), that
	update's baseline number and tick */
	int biped_sent[TRACKED_PLAYERS];
	int biped_baseline[TRACKED_PLAYERS];
	long biped_tick[TRACKED_PLAYERS];
	/* the items (weapons, equipment) it was told of, by wire number: the
	object, where it is (ITEM_*), on whom, its baseline number, where it
	was and its rounds as told last, and when */
	long item[NETWORK_OBJECTS + 1];
	char item_state[NETWORK_OBJECTS + 1];
	long item_holder[NETWORK_OBJECTS + 1];
	unsigned char item_baseline[NETWORK_OBJECTS + 1];
	float item_position[NETWORK_OBJECTS + 1][3];
	int item_rounds[NETWORK_OBJECTS + 1];
	long item_tick[NETWORK_OBJECTS + 1];
	int update_id;
	int sequence;
	int local_sequence;
	/* where its biped was as the update being played began (Custom
	Edition's own server tells a client that, every half second: its game
	puts its biped there and plays its updates since again) */
	float start_position[3];
	int start_update_id;
	boolean have_start;
	/* the scores and numbers it was told last (by absolute index) */
	int scores_sent[CE_GAME_PLAYERS];
	int team_scores_sent[CE_GAME_PLAYERS];
	struct ce_player_statistics statistics_sent[CE_GAME_PLAYERS];
	boolean statistics_known[CE_GAME_PLAYERS];
	boolean scores_known;
	boolean welcomed;
	/* the update being played (its ticks left), as the client's own game
	plays it: each update holds for its tick count, in order (but a client
	that got ahead is caught up: its game keeps only its last few updates
	to replay from our corrections) */
	struct ce_client_update current;
	boolean have_current;
	int ticks_left;
	long played_tick;
};

static struct
{
	boolean configured;
	boolean enabled;
	/* the line each Custom Edition player gets in their chat once in the
	game (ce_welcome; empty: none) */
	char welcome[CE_CHAT_SIZE + 1];
	boolean welcome_set;
	struct ce_client clients[CE_LOCAL_PLAYERS];
	/* each wire number's object */
	long objects[NETWORK_OBJECTS + 1];
	/* joins waiting for a game to start, or for a seat a leaving player still holds */
	struct ce_event waiting[CE_GAME_PLAYERS];
	unsigned long waiting_time[CE_GAME_PLAYERS];
	int waiting_count;
} ce;

/* ---------- objects' wire numbers */

static int network_object(
	long object_index,
	boolean allocate)
{
	int number;

	if (object_index == NONE)
		return 0;
	for (number = 1; number <= NETWORK_OBJECTS; number++)
	{
		if (ce.objects[number] == object_index)
			return number;
	}
	if (!allocate)
		return 0;
	for (number = 1; number <= NETWORK_OBJECTS; number++)
	{
		if (ce.objects[number] == NONE || !object_try_and_get(ce.objects[number]))
		{
			ce.objects[number] = object_index;
			return number;
		}
	}
	return 0;
}

static int network_player_number(
	long player_index)
{
	return (int)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) + 1;
}

/* ---------- the roster */

static void put_text(
	char *out,
	int size,
	wchar_t const *name)
{
	int index;

	for (index = 0; index < size - 1 && name[index]; index++)
		out[index] = name[index] >= 0x20 && name[index] < 0x7f ? (char)name[index] : '?';
	out[index] = 0;
}

static void game_settings_for(
	int peer,
	char const *name,
	struct ce_game_settings *settings)
{
	struct dedicated_status status;
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	int machine = 15;
	long index;

	dedicated_server_get_status(&status);
	ce_game_settings_slayer(settings);
	snprintf(settings->name, sizeof(settings->name), "%s", status.name);
	gamespy_map_name(status.map, settings->map, sizeof(settings->map));
	settings->maximum_players = CE_GAME_PLAYERS;
	settings->random_seed = (unsigned int)rand();
	/* (the game's players, then the joining one; each its own machine
	number, the joining one the number its challenge told it) */
	if (game)
	{
		for (index = 0; index < game->player_count && settings->player_count < CE_GAME_PLAYERS - 1; index++)
		{
			struct network_player *player = &game->players[index];
			struct ce_network_player *slot = &settings->players[settings->player_count];

			if (!network_player_is_valid(player))
				continue;
			if (machine == peer)
				machine--;
			put_text(slot->name, sizeof(slot->name), player->name);
			slot->color = player->primary_color_index;
			slot->machine = machine--;
			slot->controller = 0;
			slot->team = player->team_index == NONE ? 0 : player->team_index;
			slot->list_index = settings->player_count;
			settings->player_count++;
		}
	}
	{
		struct ce_network_player *slot = &settings->players[settings->player_count];

		snprintf(slot->name, sizeof(slot->name), "%s", name[0] ? name : "Player");
		slot->color = 0;
		slot->machine = peer;
		slot->controller = 0;
		slot->team = 0;
		slot->list_index = settings->player_count;
		settings->player_count++;
	}
}

/* ---------- joins */

static struct ce_client *client_for_peer(
	int peer,
	short *local_index)
{
	short index;

	for (index = 0; index < CE_LOCAL_PLAYERS; index++)
	{
		if (ce.clients[index].active && ce.clients[index].peer == peer)
		{
			if (local_index)
				*local_index = index;
			return &ce.clients[index];
		}
	}
	return NULL;
}

/* FALSE: no seat yet (one is still being given up) */
static boolean join(
	struct ce_event const *event)
{
	static const unsigned char begin_game[] = { 1, 0, 0, 0, 0, CE_MESSAGE_BEGIN_GAME };
	struct dedicated_status status;
	struct ce_game_settings settings;
	struct ce_payload payload;
	unsigned char out[PAYLOAD_SIZE];
	wchar_t wide_name[13];
	short index;
	int length;

	if (network_distributed_banned(event->address, NULL))
	{
		ce_transport_refuse(event->peer, CE_REJECT_BANNED);
		error(_error_silent, "ce: a Custom Edition player was turned away: banned");
		return TRUE;
	}
	dedicated_server_get_status(&status);
	/* (between games: it waits for the next game to start) */
	if (status.state != _dedicated_state_in_game)
		return FALSE;
	for (index = 0; index < CE_LOCAL_PLAYERS; index++)
	{
		if (!ce.clients[index].active && local_player_get_player_index(index) == NONE)
			break;
	}
	if (index == CE_LOCAL_PLAYERS)
	{
		boolean leaving = FALSE;

		for (index = 0; index < CE_LOCAL_PLAYERS; index++)
			leaving |= !ce.clients[index].active;
		if (leaving)
			return FALSE;
		ce_transport_refuse(event->peer, CE_REJECT_FULL);
		error(_error_silent, "ce: a Custom Edition player was turned away: no seat");
		return TRUE;
	}
	{
		int character;

		for (character = 0; character < 12 && event->name[character]; character++)
			wide_name[character] = (wchar_t)(unsigned char)event->name[character];
		wide_name[character] = 0;
		if (!character)
		{
			static char const fallback[] = "Player";

			for (character = 0; fallback[character]; character++)
				wide_name[character] = (wchar_t)fallback[character];
			wide_name[character] = 0;
		}
	}
	if (!network_game_client_add_named_player(global_network_game_client_get(), index, wide_name, 0))
	{
		ce_transport_refuse(event->peer, CE_REJECT_UNKNOWN);
		error(_error_silent, "ce: a Custom Edition player was turned away: the game did not add them");
		return TRUE;
	}
	csmemset(&ce.clients[index], 0, sizeof(ce.clients[index]));
	ce.clients[index].active = TRUE;
	ce.clients[index].peer = event->peer;
	ce.clients[index].address = event->address;
	snprintf(ce.clients[index].name, sizeof(ce.clients[index].name), "%s", event->name);
	{
		long player;

		for (player = 0; player < TRACKED_PLAYERS; player++)
		{
			ce.clients[index].announced[player] = NONE;
			ce.clients[index].unit_sent[player] = NONE;
			ce.clients[index].body[player] = NONE;
		}
		for (player = 0; player <= NETWORK_OBJECTS; player++)
			ce.clients[index].item[player] = NONE;
	}
	game_settings_for(event->peer, event->name, &settings);
	ce_payload_begin(&payload, out, sizeof(out));
	ce_payload_put_game_settings(&payload, &settings);
	ce_payload_put_message(&payload, begin_game, sizeof(begin_game));
	length = ce_payload_end(&payload);
	ce_transport_accept(event->peer, out, length);
	error(_error_silent, "ce: Custom Edition player \"%s\" joins as local player %d", event->name, index);
	return TRUE;
}

static void leave(
	int peer)
{
	short index;
	struct ce_client *client = client_for_peer(peer, &index);
	long player_index;

	if (!client)
		return;
	player_index = local_player_get_player_index(index);
	if (player_index != NONE)
	{
		struct player_datum *player = player_get(player_index);
		struct network_player network_player = player->network_player_data;

		network_game_client_request_remove_player(global_network_game_client_get(), &network_player);
	}
	error(_error_silent, "ce: Custom Edition player \"%s\" left", client->name);
	csmemset(client, 0, sizeof(*client));
}

/* ---------- what each client is told */

static void put_object_start(
	struct ce_object_create *create,
	long object_index,
	int number)
{
	struct object_datum *object = object_get(object_index);

	csmemset(create, 0, sizeof(*create));
	create->definition = (unsigned int)object->definition_index;
	create->object = number;
	create->owner = 0xffff;
	ce_direction(&object->object.forward.i, create->forward);
	ce_direction(&object->object.up.i, create->up);
	create->position[0] = object->object.position.x;
	create->position[1] = object->object.position.y;
	create->position[2] = object->object.position.z;
}

/* a weapon's rounds (each magazine's left and loaded) and its age, 6 bits
of 1 (what a client's game needs to fire it as its holder's actions say) */
static void put_weapon_state(
	struct ce_weapon_create *create,
	long weapon_index)
{
	struct weapon_datum *weapon = weapon_get(weapon_index);
	int index;
	real age = weapon->weapon.age;

	create->baseline = 1;
	for (index = 0; index < 2; index++)
	{
		create->ammunition[index] = weapon->weapon.magazines[index].rounds_total & 0xffff;
		create->magazine[index] = weapon->weapon.magazines[index].rounds_loaded & 0xffff;
	}
	age = age < 0.0f ? 0.0f : age > 1.0f ? 1.0f : age;
	create->age = (int)(age * 63.0f + 0.5f);
}

/* a weapon's rounds left, as one number (to see a change) */
static int weapon_rounds(
	long weapon_index)
{
	struct weapon_datum *weapon = weapon_get(weapon_index);

	return (weapon->weapon.magazines[0].rounds_total & 0xffff) | (weapon->weapon.magazines[1].rounds_total & 0xffff) << 16;
}

/* (what a client was told of an item) */
static void item_told(
	struct ce_client *client,
	int number,
	long object_index,
	int state,
	long holder,
	int baseline)
{
	struct object_datum *object = object_get(object_index);

	client->item[number] = object_index;
	client->item_state[number] = (char)state;
	client->item_holder[number] = holder;
	client->item_baseline[number] = (unsigned char)baseline;
	client->item_position[number][0] = object->object.position.x;
	client->item_position[number][1] = object->object.position.y;
	client->item_position[number][2] = object->object.position.z;
	client->item_rounds[number] = TEST_FLAG(_object_mask_weapon, object->object.type) ? weapon_rounds(object_index) : 0;
	client->item_tick[number] = game_time_get();
}

/* what a biped update carries, as one number (to see a change) */
static int biped_state(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);

	return (unit->unit.grenade_counts[0] & 7) | (unit->unit.grenade_counts[1] & 7) << 3 |
		ce_body_bits(unit->object.body_vitality) << 6 | ce_shield_bits(unit->object.shield_vitality) << 9;
}

static void send_unit(
	struct ce_client *client,
	long player_index,
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct ce_biped_create biped;
	struct ce_player_spawn spawn;
	struct ce_payload payload;
	unsigned char out[PAYLOAD_SIZE];
	int biped_number = network_object(unit_index, TRUE);
	int index, length;

	ce_payload_begin(&payload, out, sizeof(out));
	csmemset(&spawn, 0, sizeof(spawn));
	spawn.player = network_player_number(player_index);
	spawn.biped = biped_number;
	spawn.unknown_medium = 0xffff;
	for (index = 0; index < MAXIMUM_WEAPONS_PER_UNIT; index++)
	{
		long weapon_index = unit->unit.weapon_object_indices[index];
		struct ce_weapon_create weapon;
		int number;

		if (weapon_index == NONE || !object_try_and_get(weapon_index))
			continue;
		number = network_object(weapon_index, TRUE);
		csmemset(&weapon, 0, sizeof(weapon));
		put_object_start(&weapon.object, weapon_index, number);
		/* (a held weapon goes with its biped: no place of its own) */
		csmemset(weapon.object.position, 0, sizeof(weapon.object.position));
		put_weapon_state(&weapon, weapon_index);
		ce_payload_put_weapon_create(&payload, &weapon);
		spawn.weapons[index] = number;
		item_told(client, number, weapon_index, ITEM_HELD, unit_index, weapon.baseline);
	}
	csmemset(&biped, 0, sizeof(biped));
	put_object_start(&biped.object, unit_index, biped_number);
	biped.object.owner = 0;
	biped.object.player = network_player_number(player_index);
	for (index = 0; index < 12; index++)
		biped.change_colors[index] = 1.0f;
	biped.unknown_6bits_a = 63;
	biped.baseline = 1;
	biped.grenades[0] = unit->unit.grenade_counts[0] & 7;
	biped.grenades[1] = unit->unit.grenade_counts[1] & 7;
	biped.body = ce_body_bits(unit->object.body_vitality);
	biped.shield = ce_shield_bits(unit->object.shield_vitality);
	ce_payload_put_biped_create(&payload, &biped);
	ce_payload_put_player_spawn(&payload, &spawn);
	length = ce_payload_end(&payload);
	ce_transport_send(client->peer, out, length, TRUE);
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

		client->biped_sent[absolute] = biped_state(unit_index);
		client->biped_baseline[absolute] = 1;
		client->biped_tick[absolute] = game_time_get();
	}
}

/* (a player's biped as its HUD shows it, as it changes: vitality and
grenades) */
static void put_biped_update(
	struct ce_client *client,
	struct ce_payload *payload,
	long absolute,
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct ce_biped_update update;
	int state = biped_state(unit_index);

	/* (and again once a second: a client's own guesses of its damage
	are put right) */
	if (game_time_get() - client->biped_tick[absolute] < BIPED_UPDATE_TICKS ||
		(state == client->biped_sent[absolute] && game_time_get() - client->biped_tick[absolute] < BIPED_REFRESH_TICKS))
	{
		return;
	}
	client->biped_baseline[absolute] = (client->biped_baseline[absolute] + 1) & 0xff;
	client->biped_sent[absolute] = state;
	client->biped_tick[absolute] = game_time_get();
	csmemset(&update, 0, sizeof(update));
	update.object = network_object(unit_index, FALSE);
	update.baseline = client->biped_baseline[absolute];
	/* (our game's shield is the one: a client's own guess of it is put
	right) */
	update.apply_shield = TRUE;
	update.grenades[0] = unit->unit.grenade_counts[0] & 7;
	update.grenades[1] = unit->unit.grenade_counts[1] & 7;
	update.body = ce_body_bits(unit->object.body_vitality);
	update.shield = ce_shield_bits(unit->object.shield_vitality);
	ce_payload_put_biped_update(payload, &update);
}

static int throttle_bits(
	real_vector3d const *throttle)
{
	int bits = 0;

	if (throttle->i > 0.3f)
		bits |= 8;
	else if (throttle->i < -0.3f)
		bits |= 4;
	if (throttle->j > 0.3f)
		bits |= 2;
	else if (throttle->j < -0.3f)
		bits |= 1;
	return bits;
}

static void send_delete(
	struct ce_client *client,
	long object_index)
{
	struct ce_payload payload;
	unsigned char out[32];
	int number = network_object(object_index, FALSE);

	if (!number)
		return;
	ce_payload_begin(&payload, out, sizeof(out));
	ce_payload_put_object_delete(&payload, number);
	ce_transport_send(client->peer, out, ce_payload_end(&payload), TRUE);
}

static void send_chat(
	struct ce_client *client,
	char const *text)
{
	struct ce_payload payload;
	unsigned char out[PAYLOAD_SIZE];

	ce_payload_begin(&payload, out, sizeof(out));
	ce_payload_put_chat(&payload, CE_CHAT_SERVER, CE_CHAT_NO_PLAYER, text);
	ce_transport_send(client->peer, out, ce_payload_end(&payload), TRUE);
}

static void player_statistics(
	struct player_datum const *player,
	struct ce_player_statistics *statistics)
{
	int index;

	csmemset(statistics, 0, sizeof(*statistics));
	for (index = 0; index < 4; index++)
	{
		statistics->kills[index] = player->statistics.kills[index] & 0xffff;
		statistics->assists[index] = player->statistics.assists[index] & 0xffff;
	}
	statistics->betrayals = player->statistics.friendly_fire_kills & 0xffff;
	statistics->deaths = player->statistics.deaths & 0xffff;
	statistics->suicides = player->statistics.suicides & 0xffff;
	statistics->multiplayer_special = (int)player->multiplayer_special;
	statistics->speed_multiplier = player->speed_multiplier;
}

/* (each player's numbers and, in Slayer, the scores, as they change: what
the client's scoreboard and carnage report show) */
static void send_scores(
	struct ce_client *client)
{
	struct ce_payload payload;
	unsigned char out[PAYLOAD_SIZE];
	struct data_iterator iterator;
	struct player_datum *player;
	boolean any = FALSE;

	ce_payload_begin(&payload, out, sizeof(out));
	if (game_engine && game_engine->get_player_score && game_engine_get_variant()->game_engine_index == game_engine_slayer)
	{
		int scores[CE_GAME_PLAYERS], team_scores[CE_GAME_PLAYERS];
		int index;

		csmemset(scores, 0, sizeof(scores));
		for (index = 0; index < CE_GAME_PLAYERS; index++)
			team_scores[index] = (int)game_engine_get_team_score(index);
		data_iterator_new(&iterator, player_data);
		while ((player = data_iterator_next(&iterator)) != NULL)
		{
			long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

			if (absolute < CE_GAME_PLAYERS)
				scores[absolute] = (int)game_engine->get_player_score(iterator.datum_index, _get_score_individual);
		}
		if (!client->scores_known || csmemcmp(scores, client->scores_sent, sizeof(scores)) ||
			csmemcmp(team_scores, client->team_scores_sent, sizeof(team_scores)))
		{
			ce_payload_put_slayer_scores(&payload, scores, team_scores);
			csmemcpy(client->scores_sent, scores, sizeof(scores));
			csmemcpy(client->team_scores_sent, team_scores, sizeof(team_scores));
			client->scores_known = TRUE;
			any = TRUE;
		}
	}
	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct ce_player_statistics statistics;

		if (absolute >= CE_GAME_PLAYERS || client->announced[absolute] != iterator.datum_index)
			continue;
		player_statistics(player, &statistics);
		if (client->statistics_known[absolute] &&
			!csmemcmp(&statistics, &client->statistics_sent[absolute], sizeof(statistics)))
		{
			continue;
		}
		ce_payload_put_player_statistics(&payload, network_player_number(iterator.datum_index), &statistics);
		client->statistics_sent[absolute] = statistics;
		client->statistics_known[absolute] = TRUE;
		any = TRUE;
	}
	if (any)
		ce_transport_send(client->peer, out, ce_payload_end(&payload), TRUE);
}

/* the wire number of the player whose unit this is (0: none, or one the
client was not told of) */
static int unit_player_number(
	struct ce_client *client,
	long unit_index)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (unit_index == NONE)
		return 0;
	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

		if (player->unit_index == unit_index && absolute < TRACKED_PLAYERS && client->unit_sent[absolute] == unit_index)
			return network_player_number(iterator.datum_index);
	}
	return 0;
}

static void put_item_update(
	struct ce_client *client,
	struct ce_payload *payload,
	int number,
	long weapon_index)
{
	struct weapon_datum *weapon = weapon_get(weapon_index);
	struct ce_weapon_update update;
	int index;
	real age = weapon->weapon.age;

	csmemset(&update, 0, sizeof(update));
	update.object = number;
	update.baseline = (client->item_baseline[number] + 1) & 0xff;
	update.position[0] = weapon->object.position.x;
	update.position[1] = weapon->object.position.y;
	update.position[2] = weapon->object.position.z;
	for (index = 0; index < 2; index++)
		update.ammunition[index] = weapon->weapon.magazines[index].rounds_total & 0xffff;
	age = age < 0.0f ? 0.0f : age > 1.0f ? 1.0f : age;
	update.age = (int)(age * 63.0f + 0.5f);
	ce_payload_put_weapon_update(payload, &update);
}

/* the items in the world (weapons and equipment) as they appear, move and
go, and as players pick weapons up and let them go: a weapon picked up is
the action its client's game plays (a pickup, or a swap for the weapon
held); one let go is put where our game has it */
static void update_items(
	struct ce_client *client,
	struct ce_payload *reliable)
{
	struct object_iterator iterator;
	struct object_datum *object;
	int number;

	/* (items gone: taken away) */
	for (number = 1; number <= NETWORK_OBJECTS; number++)
	{
		if (client->item[number] != NONE && client->item_state[number] != ITEM_UNKNOWN &&
			(!object_try_and_get(client->item[number]) || ce.objects[number] != client->item[number]))
		{
			ce_payload_put_object_delete(reliable, number);
			client->item[number] = NONE;
			client->item_state[number] = ITEM_UNKNOWN;
		}
	}
	/* (pickups first, so a weapon let go for another is known as a swap) */
	object_iterator_new(&iterator, _object_mask_weapon, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		long holder = object->object.parent_object_index;
		int holder_player = holder == NONE ? 0 : unit_player_number(client, holder);
		struct ce_action_result result;
		int dropped = 0, other;

		number = network_object(iterator.index, FALSE);
		if (!number || !holder_player || client->item[number] != iterator.index ||
			client->item_state[number] != ITEM_WORLD)
		{
			continue;
		}
		/* (the weapon this player let go for it, this tick: a swap) */
		for (other = 1; other <= NETWORK_OBJECTS; other++)
		{
			struct object_datum *let_go;

			if (client->item_state[other] != ITEM_HELD || client->item_holder[other] != holder ||
				(let_go = object_try_and_get(client->item[other])) == NULL || let_go->object.parent_object_index != NONE)
			{
				continue;
			}
			dropped = other;
			/* (its client's game lets it go itself: where it lands is told
			as it moves) */
			client->item_state[other] = ITEM_WORLD;
			client->item_holder[other] = NONE;
			break;
		}
		csmemset(&result, 0, sizeof(result));
		result.player = holder_player;
		result.weapon_pickup = 1;
		result.object = number;
		result.result = dropped ? _player_action_result_swap_for_weapon : _player_action_result_add_weapon_to_inventory;
		result.seat = 0xffff;
		result.dropped = dropped;
		ce_payload_put_action_result(reliable, &result);
		item_told(client, number, iterator.index, ITEM_HELD, holder, client->item_baseline[number]);
	}
	object_iterator_new(&iterator, _object_mask_weapon | _object_mask_equipment, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		long holder = object->object.parent_object_index;
		boolean weapon = TEST_FLAG(_object_mask_weapon, object->object.type);
		int holder_player = holder == NONE ? 0 : unit_player_number(client, holder);

		/* (a full payload: the rest next tick) */
		if (reliable->position > (PAYLOAD_SIZE - 80) * 8)
			break;
		number = network_object(iterator.index, holder == NONE || (weapon && holder_player));
		if (!number)
			continue;
		if (client->item[number] != iterator.index || client->item_state[number] == ITEM_UNKNOWN)
		{
			struct ce_object_create create;

			/* (one in a vehicle's hands waits for vehicles; one that came
			to a player's hands but from the world waits for its player's
			next spawn) */
			if (holder != NONE)
				continue;
			put_object_start(&create, iterator.index, number);
			if (weapon)
			{
				struct ce_weapon_create created;

				csmemset(&created, 0, sizeof(created));
				created.object = create;
				put_weapon_state(&created, iterator.index);
				created.baseline = 2;
				ce_payload_put_weapon_create(reliable, &created);
			}
			else
				ce_payload_put_equipment_create(reliable, &create, 2);
			item_told(client, number, iterator.index, ITEM_WORLD, NONE, 2);
			continue;
		}
		if (!weapon)
			continue;
		/* (picked up by a player its client doesn't know: told at that
		player's spawn) */
		if (client->item_state[number] == ITEM_WORLD && holder != NONE)
			continue;
		if (client->item_state[number] == ITEM_HELD && holder == NONE)
		{
			/* (let go, not for another: put where it fell) */
			put_item_update(client, reliable, number, iterator.index);
			item_told(client, number, iterator.index, ITEM_WORLD, NONE, (client->item_baseline[number] + 1) & 0xff);
			continue;
		}
		if (client->item_state[number] == ITEM_HELD && holder != client->item_holder[number])
			continue;
		/* (moving, or its rounds changed: its place and rounds) */
		if (game_time_get() - client->item_tick[number] >= ITEM_UPDATE_TICKS)
		{
			float dx = object->object.position.x - client->item_position[number][0];
			float dy = object->object.position.y - client->item_position[number][1];
			float dz = object->object.position.z - client->item_position[number][2];
			boolean moved = holder == NONE && dx * dx + dy * dy + dz * dz > 0.05f * 0.05f;

			if (moved || weapon_rounds(iterator.index) != client->item_rounds[number])
			{
				put_item_update(client, reliable, number, iterator.index);
				item_told(client, number, iterator.index, client->item_state[number], client->item_holder[number],
					(client->item_baseline[number] + 1) & 0xff);
			}
		}
	}
}

static void update_client(
	struct ce_client *client,
	short local_index)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct ce_payload payload, reliable;
	unsigned char out[PAYLOAD_SIZE], reliable_out[PAYLOAD_SIZE];
	long own_player = local_player_get_player_index(local_index);
	boolean any = FALSE;
	int length;

	ce_payload_begin(&payload, out, sizeof(out));
	ce_payload_begin(&reliable, reliable_out, sizeof(reliable_out));
	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		long player_index = iterator.datum_index;
		long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

		if (absolute >= TRACKED_PLAYERS)
			continue;
		if (client->announced[absolute] != player_index)
		{
			struct ce_payload added;
			unsigned char added_out[64];
			int added_length;

			ce_payload_begin(&added, added_out, sizeof(added_out));
			ce_payload_put_player_add(&added, player->network_player_data.player_list_index,
				(unsigned int)player_index, network_player_number(player_index));
			added_length = ce_payload_end(&added);
			if (ce_transport_send(client->peer, added_out, added_length, TRUE))
			{
				client->announced[absolute] = player_index;
				client->unit_sent[absolute] = NONE;
			}
		}
		/* (a body our game took away: taken away on the client too) */
		if (client->body[absolute] != NONE && !object_try_and_get(client->body[absolute]))
		{
			send_delete(client, client->body[absolute]);
			client->body[absolute] = NONE;
		}
		if (player->unit_index != client->unit_sent[absolute])
		{
			long old_unit = client->unit_sent[absolute];

			if (old_unit != NONE)
			{
				/* (a unit that is still there died: its client plays the
				death and keeps the body until our game takes it away;
				one that is gone is taken away at once) */
				if (player->unit_index == NONE && object_try_and_get(old_unit) && network_object(old_unit, FALSE))
				{
					struct ce_payload killed;
					unsigned char killed_out[64];

					if (client->body[absolute] != NONE)
						send_delete(client, client->body[absolute]);
					ce_payload_begin(&killed, killed_out, sizeof(killed_out));
					ce_payload_put_unit_killed(&killed, network_object(old_unit, FALSE), (int)player->respawn_timer);
					ce_transport_send(client->peer, killed_out, ce_payload_end(&killed), TRUE);
					client->body[absolute] = old_unit;
				}
				else
					send_delete(client, old_unit);
			}
			client->unit_sent[absolute] = player->unit_index;
			if (player->unit_index != NONE)
				send_unit(client, player_index, player->unit_index);
		}
		if (player->unit_index != NONE && player->unit_index == client->unit_sent[absolute])
			put_biped_update(client, &reliable, absolute, player->unit_index);
		/* (its own biped, twice a second, as Custom Edition's server does:
		an update and where the biped was as that update began. Its game
		puts the biped there and plays its own updates from that one on
		again, so it ends where our game has it) */
		if (player_index == own_player && player->unit_index != NONE && client->have_start &&
			game_time_get() % LOCAL_UPDATE_TICKS == 0)
		{
			ce_payload_put_local_player_update(&payload, client->local_sequence, client->start_update_id,
				client->start_position);
			client->local_sequence = (client->local_sequence + 1) & 31;
			any = TRUE;
		}
		if (player_index != own_player && player->unit_index != NONE)
		{
			struct unit_datum *unit = unit_get(player->unit_index);
			struct ce_remote_actions actions;
			float position[3];

			csmemset(&actions, 0, sizeof(actions));
			actions.player = network_player_number(player_index);
			actions.update_id = client->update_id;
			actions.ticks = 1;
			actions.control_flags = unit->unit.control_flags & 0x7fff;
			ce_direction(&unit->unit.aiming_vector.i, actions.facing);
			actions.throttle = throttle_bits(&unit->unit.throttle);
			actions.primary_trigger = unit->unit.primary_trigger > 0.5f;
			actions.weapon = unit->unit.current_weapon_index < 0 ? 0 : unit->unit.current_weapon_index & 7;
			actions.grenade = unit->unit.current_grenade_index < 0 ? 0 : unit->unit.current_grenade_index & 3;
			ce_payload_put_remote_player_actions(&payload, &actions);
			position[0] = unit->object.position.x;
			position[1] = unit->object.position.y;
			position[2] = unit->object.position.z;
			ce_payload_put_remote_player_position(&payload, actions.player, client->update_id, client->sequence,
				position);
			any = TRUE;
		}
	}
	if (any)
	{
		length = ce_payload_end(&payload);
		ce_transport_send(client->peer, out, length, FALSE);
		client->update_id = (client->update_id + 1) & 63;
		client->sequence = (client->sequence + 1) & 7;
	}
	update_items(client, &reliable);
	if (reliable.position > 11)
		ce_transport_send(client->peer, reliable_out, ce_payload_end(&reliable), TRUE);
	send_scores(client);
}

/* ---------- public code */

void server_ce_update(
	void)
{
	struct ce_event event;
	struct dedicated_status status;
	static long last_tick = NONE;
	short index;

	if (!ce.configured)
	{
		char const *accept = getenv("HALO_DEDICATED_CE_ACCEPT");
		long number;

		ce.configured = TRUE;
		ce.enabled = accept && !strcmp(accept, "1");
		for (number = 0; number <= NETWORK_OBJECTS; number++)
			ce.objects[number] = NONE;
	}
	if (!ce.enabled)
		return;
	while (ce_transport_next_event(&event))
	{
		short local_index;
		struct ce_client *client = client_for_peer(event.peer, &local_index);

		switch (event.type)
		{
		case CE_EVENT_JOIN:
			if (!join(&event) && ce.waiting_count < CE_GAME_PLAYERS)
			{
				ce.waiting[ce.waiting_count] = event;
				ce.waiting_time[ce.waiting_count] = system_milliseconds();
				ce.waiting_count++;
			}
			break;
		case CE_EVENT_LOADED:
			if (client)
			{
				client->loaded = TRUE;
				if (!client->welcomed && server_ce_welcome()[0])
					send_chat(client, server_ce_welcome());
				client->welcomed = TRUE;
			}
			break;
		case CE_EVENT_LEFT:
			leave(event.peer);
			break;
		}
	}
	/* (joins waiting for a game or a seat: tried again, for as long as a
	client waits for its answer; then refused) */
	for (index = 0; index < ce.waiting_count; index++)
	{
		boolean answered = join(&ce.waiting[index]);

		if (!answered && system_milliseconds() - ce.waiting_time[index] > JOIN_WAIT_MILLISECONDS)
		{
			ce_transport_refuse(ce.waiting[index].peer, CE_REJECT_FULL);
			error(_error_silent, "ce: a Custom Edition player was turned away: no game started in time");
			answered = TRUE;
		}
		if (answered)
		{
			ce.waiting[index] = ce.waiting[ce.waiting_count - 1];
			ce.waiting_time[index] = ce.waiting_time[ce.waiting_count - 1];
			ce.waiting_count--;
			index--;
		}
	}
	dedicated_server_get_status(&status);
	if (status.state != _dedicated_state_in_game)
	{
		/* (the game ended: its Custom Edition players go with it, for now:
		their clients told, their players taken out of the game) */
		for (index = 0; index < CE_LOCAL_PLAYERS; index++)
		{
			if (ce.clients[index].active)
			{
				ce_transport_drop(ce.clients[index].peer);
				leave(ce.clients[index].peer);
			}
		}
		last_tick = NONE;
		return;
	}
	if (game_time_get() == last_tick)
		return;
	last_tick = game_time_get();
	for (index = 0; index < CE_LOCAL_PLAYERS; index++)
	{
		struct ce_client *client = &ce.clients[index];

		if (client->active && client->loaded)
			update_client(client, index);
	}
}

boolean server_ce_local_action(
	short local_player_index,
	struct player_action *action)
{
	struct ce_client *client;
	struct ce_client_update update;
	float forward, left;

	if (!ce.enabled || local_player_index < 0 || local_player_index >= CE_LOCAL_PLAYERS)
		return FALSE;
	client = &ce.clients[local_player_index];
	if (!client->active)
		return FALSE;
	/* (each tick, the update being played goes on for its tick count; then
	the next, in order. With none waiting, the last holds) */
	if (client->played_tick != game_time_get())
	{
		client->played_tick = game_time_get();
		if (client->ticks_left > 0)
			client->ticks_left--;
		if (client->ticks_left > 0 && ce_transport_pending_updates(client->peer) > CAUGHT_UP_UPDATES)
			client->ticks_left = 0;
		if (client->ticks_left <= 0 && ce_transport_next_update(client->peer, &update))
		{
			long unit_index = local_player_get_player_index(local_player_index) == NONE ? NONE :
				player_get(local_player_get_player_index(local_player_index))->unit_index;

			client->current = update;
			client->have_current = TRUE;
			client->ticks_left = update.ticks > 0 ? update.ticks : 1;
			/* (where its biped is now, before this tick moves it: where the
			update begins) */
			client->have_start = FALSE;
			if (unit_index != NONE)
			{
				struct object_datum *object = object_get(unit_index);

				client->start_position[0] = object->object.position.x;
				client->start_position[1] = object->object.position.y;
				client->start_position[2] = object->object.position.z;
				client->start_update_id = update.update_id;
				client->have_start = TRUE;
			}
		}
	}
	if (!client->have_current)
	{
		csmemset(action, 0, sizeof(*action));
		action->desired_weapon_index = NONE;
		action->desired_grenade_index = NONE;
		action->desired_zoom_level = NONE;
		return TRUE;
	}
	update = client->current;
	ce_throttle(update.throttle, &forward, &left);
	action->control_flags = update.control_flags;
	action->desired_facing.yaw = update.yaw;
	action->desired_facing.pitch = update.pitch;
	action->throttle.i = forward;
	action->throttle.j = left;
	action->primary_trigger = update.primary_trigger ? 1.0f : 0.0f;
	return TRUE;
}


void server_ce_set_welcome(
	char const *text)
{
	static char const default_welcome[] = "Welcome to a Chupathingy cross-play server! You're playing with Chupathingy "
		"players on PC, Mac, Linux, Android and Xbox. Info: chupathingy.org";

	snprintf(ce.welcome, sizeof(ce.welcome), "%s", text ? text : default_welcome);
	ce.welcome_set = TRUE;
}

char const *server_ce_welcome(
	void)
{
	if (!ce.welcome_set)
		server_ce_set_welcome(NULL);
	return ce.welcome;
}

int server_ce_say(
	char const *text)
{
	short index;
	int count = 0;

	for (index = 0; index < CE_LOCAL_PLAYERS; index++)
	{
		if (ce.clients[index].active && ce.clients[index].loaded)
		{
			send_chat(&ce.clients[index], text);
			count++;
		}
	}
	return count;
}

boolean server_ce_kick(
	long machine_index,
	long controller_index,
	unsigned long *address)
{
	short index;

	for (index = 0; index < CE_LOCAL_PLAYERS; index++)
	{
		struct ce_client *client = &ce.clients[index];
		long player_index = local_player_get_player_index(index);
		struct player_datum *player;

		if (!client->active || player_index == NONE)
			continue;
		player = player_get(player_index);
		if (player->network_player_data.machine_index != machine_index ||
			player->network_player_data.controller_index != controller_index)
		{
			continue;
		}
		if (address)
			*address = client->address;
		{
			unsigned char message[16], out[64];
			int length = ce_message_machine_rejected(CE_REJECT_KICKED, message, sizeof(message));

			length = length ? ce_payload_from_message(message, length, out, sizeof(out)) : 0;
			if (length)
				ce_transport_send(client->peer, out, length, TRUE);
		}
		ce_transport_drop(client->peer);
		leave(client->peer);
		return TRUE;
	}
	return FALSE;
}

#endif
