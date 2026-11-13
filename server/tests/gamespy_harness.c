/*
GAMESPY_HARNESS.C

server/platform/gamespy_listing.c alone, for tools/test_gamespy_listing.py:
the listing started on a port with a list of masters, given a made-up
server's state, until its standard input ends (then stopped, which sends
the masters the last heartbeat). Its notices go to standard output.

  gamespy_harness <port> <masters> <gamever> <players> [joins]
  gamespy_harness map <map>     (the map's name as Custom Edition has it)
  gamespy_harness spawn         (a player's first spawn, as hex)
  gamespy_harness keepalive     (the server's keep-alive payload, as hex)
  gamespy_harness settings      (a game settings payload, as hex: the one
                                 our capture of Custom Edition's server has)
  gamespy_harness chat <kind> <player> <text>
                                (a payload of one chat line, as hex)
  gamespy_harness killed <object> <ticks>
                                (a payload of one unit killed, as hex)
  gamespy_harness scores        (a payload of Slayer's scores and a player's
                                 numbers, all zero as a new game's, as hex)
  gamespy_harness postgame <mode> [over]
                                (a payload of the game's end at that step,
                                 and game over after it, as hex)
  gamespy_harness replies <count> <address>...
                                (count queries in one second, from each
                                 address in turn: how many replies each
                                 address is allowed, one line each)

With joins on, the harness has no game: each join is refused as a game
between games would refuse it (reason 0), and the log says so.
*/

#include "../platform/gamespy_listing.h"
#include "../platform/ce_messages.h"
#include "../platform/ce_transport.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct gamespy_status status;
	char problem[160];
	char notice[160];
	int players;
	int index;

	if (argc == 3 && !strcmp(argv[1], "map"))
	{
		char name[GAMESPY_TEXT_SIZE];

		gamespy_map_name(argv[2], name, sizeof(name));
		printf("%s\n", name);
		return 0;
	}
	if (argc >= 4 && !strcmp(argv[1], "replies"))
	{
		int allowed[16] = { 0 };
		int addresses = argc - 3 < 16 ? argc - 3 : 16;
		int query;

		for (query = 0; query < atoi(argv[2]); query++)
		{
			index = query % addresses;
			allowed[index] += gamespy_reply_allowed((unsigned int)strtoul(argv[3 + index], NULL, 0), 1000);
		}
		for (index = 0; index < addresses; index++)
			printf("%d\n", allowed[index]);
		return 0;
	}
	if (argc >= 3 && !strcmp(argv[1], "updates"))
	{
		struct ce_client_update update;
		int argument;

		memset(&update, 0, sizeof(update));
		for (argument = 2; argument < argc; argument++)
		{
			unsigned char payload[256];
			int length = (int)strlen(argv[argument]) / 2;

			for (index = 0; index < length && index < (int)sizeof(payload); index++)
			{
				unsigned int byte;

				sscanf(argv[argument] + index * 2, "%2x", &byte);
				payload[index] = (unsigned char)byte;
			}
			printf("read %d", ce_payload_read_client_updates(payload, length, &update, NULL, NULL));
			printf(" id %d ticks %d flags %u yaw %.4f pitch %.4f throttle %d trigger %d weapon %d grenade %d medium %d\n",
				update.update_id, update.ticks, update.control_flags, update.yaw, update.pitch, update.throttle,
				update.primary_trigger, update.weapon, update.grenade, update.unknown_medium);
		}
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "direction"))
	{
		static const float vectors[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } };
		unsigned int out[2];

		for (index = 0; index < 3; index++)
		{
			ce_direction(vectors[index], out);
			printf("%u %u\n", out[0], out[1]);
		}
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "spawn"))
	{
		struct ce_weapon_create weapon;
		struct ce_biped_create biped;
		struct ce_player_spawn spawn;
		struct ce_payload payload;
		unsigned char out[512];
		int length;

		/* (the values our capture of Custom Edition's server sent for a
		player's first spawn on Blood Gulch) */
		memset(&weapon, 0, sizeof(weapon));
		weapon.object.definition = 0xe74605d2u;
		weapon.object.object = 49;
		weapon.object.owner = 0xffff;
		weapon.object.forward[0] = 524287;
		weapon.object.forward[1] = 262144;
		weapon.baseline = 1;
		memset(&biped, 0, sizeof(biped));
		biped.object.definition = 0xe2070093u;
		biped.object.object = 50;
		biped.object.player = 1;
		biped.object.forward[0] = 524287;
		biped.object.forward[1] = 313982;
		for (index = 0; index < 12; index++)
			biped.change_colors[index] = 1.0f;
		biped.unknown_6bits_a = 63;
		biped.baseline = 1;
		biped.object.position[0] = 84.90137481689453f;
		biped.object.position[1] = -161.71449279785156f;
		biped.object.position[2] = 0.10793768614530563f;
		biped.body = 7;
		biped.shield = 21;
		memset(&spawn, 0, sizeof(spawn));
		spawn.player = 1;
		spawn.biped = 50;
		spawn.unknown_medium = 0xffff;
		spawn.weapons[0] = 49;
		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_player_add(&payload, 0, 0xec700000u, 1);
		ce_payload_put_weapon_create(&payload, &weapon);
		ce_payload_put_biped_create(&payload, &biped);
		ce_payload_put_player_spawn(&payload, &spawn);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if ((argc == 3 || argc == 4) && !strcmp(argv[1], "postgame"))
	{
		static const unsigned char game_over[] = { 1, 0, 0, 0, 0, CE_MESSAGE_GAME_OVER };
		struct ce_payload payload;
		unsigned char out[32];
		int length;

		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_postgame_mode(&payload, atoi(argv[2]));
		if (argc == 4)
			ce_payload_put_message(&payload, game_over, sizeof(game_over));
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "keepalive"))
	{
		struct ce_payload payload;
		unsigned char out[32];
		int length;

		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_server_keep_alive(&payload, 0, 0);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if (argc == 5 && !strcmp(argv[1], "chat"))
	{
		struct ce_payload payload;
		unsigned char out[600];
		int length;

		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_chat(&payload, atoi(argv[2]), atoi(argv[3]), argv[4]);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "killed"))
	{
		struct ce_payload payload;
		unsigned char out[64];
		int length;

		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_unit_killed(&payload, atoi(argv[2]), atoi(argv[3]));
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "biped"))
	{
		struct ce_biped_update update;
		struct ce_payload payload;
		unsigned char out[32];
		int length;

		/* (a full-health biped, as our capture of Custom Edition's server
		has it) */
		memset(&update, 0, sizeof(update));
		update.object = 50;
		update.baseline = atoi(argv[2]);
		update.body = ce_body_bits(1.0f);
		update.shield = ce_shield_bits(1.0f);
		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_biped_update(&payload, &update);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "items"))
	{
		struct ce_object_create equipment;
		struct ce_weapon_update update;
		struct ce_action_result result;
		struct ce_payload payload;
		unsigned char out[128];
		int length;

		/* (the first equipment of our capture of Custom Edition's server
		after a join, on Blood Gulch; a dropped plasma pistol's update;
		a weapon swap) */
		memset(&equipment, 0, sizeof(equipment));
		equipment.definition = 3861578935u;
		equipment.object = 22;
		equipment.owner = 0xffff;
		equipment.forward[0] = 115;
		equipment.forward[1] = 262093;
		equipment.up[0] = 524171;
		equipment.up[1] = 786431;
		equipment.position[0] = 95.40251159667969f;
		equipment.position[1] = -157.43798828125f;
		equipment.position[2] = 1.7354302406311035f;
		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_equipment_create(&payload, &equipment, 2);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		memset(&update, 0, sizeof(update));
		update.object = 49;
		update.baseline = 3;
		update.position[0] = 96.89305114746094f;
		update.position[1] = -161.01425170898438f;
		update.position[2] = -0.27653852105140686f;
		update.age = 14;
		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_weapon_update(&payload, &update);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		memset(&result, 0, sizeof(result));
		result.player = 2;
		result.weapon_pickup = 1;
		result.object = 30;
		result.result = 6;
		result.seat = 0xffff;
		result.dropped = 49;
		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_action_result(&payload, &result);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "vehicles"))
	{
		struct ce_vehicle_create vehicle;
		struct ce_vehicle_update update;
		struct ce_payload payload;
		unsigned char out[128];
		int length;

		/* (the first vehicle of our capture of Custom Edition's server
		after a join, on Blood Gulch, and its first update) */
		memset(&vehicle, 0, sizeof(vehicle));
		vehicle.object.definition = 3822322272u;
		vehicle.object.object = 2;
		vehicle.object.owner = 0xffff;
		vehicle.object.forward[0] = 524287;
		vehicle.object.forward[1] = 117288;
		vehicle.object.position[0] = 46.34067153930664f;
		vehicle.object.position[1] = -64.70001220703125f;
		vehicle.object.position[2] = 1.6630089282989502f;
		vehicle.weapons[0] = 1;
		vehicle.baseline = 1;
		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_vehicle_create(&payload, &vehicle);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		memset(&update, 0, sizeof(update));
		update.object = 2;
		update.baseline = 2;
		update.time = 1329613061u;
		update.at_rest = 1;
		/* (the captured place, as its 31-bit numbers) */
		update.motion.position[0] = (float)(1083681804.0 / 2147483647.0 * 10000.0 - 5000.0);
		update.motion.position[1] = (float)(1059823774.0 / 2147483647.0 * 10000.0 - 5000.0);
		update.motion.position[2] = (float)(1074059448.0 / 2147483647.0 * 10000.0 - 5000.0);
		update.motion.forward[0] = 546256;
		update.motion.forward[1] = 121350;
		update.motion.up[0] = 51413;
		update.motion.up[1] = 980948;
		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_vehicle_update(&payload, &update);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		/* (a driver's own vehicle, then another's, moving) */
		update.motion.velocity[0] = 0.5f;
		update.motion.angular_velocity[2] = -0.25f;
		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_local_vehicle_update(&payload, 9, 33, 2, &update.motion);
		ce_payload_put_remote_vehicle_position(&payload, 3, 12, 5, 2, &update.motion);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "scores"))
	{
		struct ce_player_statistics statistics;
		struct ce_payload payload;
		int scores[CE_GAME_PLAYERS];
		unsigned char out[300];
		int length;

		memset(scores, 0, sizeof(scores));
		/* (a new player's first numbers: all zero) */
		memset(&statistics, 0, sizeof(statistics));
		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_slayer_scores(&payload, scores, scores);
		ce_payload_put_player_statistics(&payload, 1, &statistics);
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "settings"))
	{
		static const unsigned char begin_game[] = { 1, 0, 0, 0, 0, CE_MESSAGE_BEGIN_GAME };
		struct ce_game_settings settings;
		struct ce_payload payload;
		unsigned char out[1400];
		int length;

		ce_game_settings_slayer(&settings);
		snprintf(settings.name, sizeof(settings.name), "hcetest");
		snprintf(settings.map, sizeof(settings.map), "bloodgulch");
		settings.maximum_players = 4;
		settings.player_count = 1;
		settings.random_seed = 51721;
		snprintf(settings.players[0].name, sizeof(settings.players[0].name), "Noodle");
		settings.players[0].color = 0;
		settings.players[0].machine = 0;
		settings.players[0].controller = 0;
		settings.players[0].team = 0;
		settings.players[0].list_index = 0;
		ce_payload_begin(&payload, out, sizeof(out));
		ce_payload_put_game_settings(&payload, &settings);
		ce_payload_put_message(&payload, begin_game, sizeof(begin_game));
		length = ce_payload_end(&payload);
		for (index = 0; index < length; index++)
			printf("%02x", out[index]);
		printf("\n");
		return 0;
	}
	if (argc != 5 && !(argc == 6 && !strcmp(argv[5], "joins")))
	{
		fprintf(stderr, "usage: gamespy_harness <port> <masters> <gamever> <players>\n");
		return 2;
	}
	memset(&status, 0, sizeof(status));
	snprintf(status.hostname, sizeof(status.hostname), "[C] Test\\Server");
	gamespy_map_name("levels\\test\\bloodgulch\\bloodgulch", status.mapname, sizeof(status.mapname));
	snprintf(status.gametype, sizeof(status.gametype), "Slayer");
	snprintf(status.gamevariant, sizeof(status.gamevariant), "Team Slayer");
	snprintf(status.version, sizeof(status.version), "0.0.0-test");
	status.maxplayers = 16;
	status.teamplay = 1;
	status.fraglimit = 50;
	status.playing = 1;
	players = atoi(argv[4]);
	if (players > GAMESPY_MAXIMUM_PLAYERS + 4)
		players = GAMESPY_MAXIMUM_PLAYERS + 4;
	status.numplayers = players;
	for (index = 0; index < players && index < GAMESPY_MAXIMUM_PLAYERS; index++)
	{
		struct gamespy_player *player = &status.players[index];

		snprintf(player->name, sizeof(player->name), "Player%d", index + 1);
		player->score = index * 3;
		player->ping = index == 0 ? -1 : 20 + index;
		player->team = index & 1;
		status.player_count++;
	}
	if (!gamespy_listing_start(atoi(argv[1]), argv[2], argv[3], problem, sizeof(problem)))
	{
		printf("not started: %s\n", problem);
		return 1;
	}
	gamespy_listing_publish(&status);
	if (argc == 6)
		gamespy_listing_set_joins(1);
	printf("started\n");
	fflush(stdout);
	for (;;)
	{
		struct pollfd input = { 0, POLLIN, 0 };
		char buffer[256];

		while (gamespy_listing_next_notice(notice, sizeof(notice)))
		{
			printf("%s\n", notice);
			fflush(stdout);
		}
		/* (no game here: joins refused) */
		{
			struct ce_event event;

			while (ce_transport_next_event(&event))
			{
				if (event.type != CE_EVENT_JOIN)
					continue;
				ce_transport_refuse(event.peer, CE_REJECT_UNKNOWN);
				printf("harness: the join was refused (no game)\n");
				fflush(stdout);
			}
		}
		if (poll(&input, 1, 100) > 0)
		{
			ssize_t got = read(0, buffer, sizeof(buffer));

			if (got <= 0)
				break;
			/* (a line "change": another map, as a new game's) */
			if (!strncmp(buffer, "change", 6))
			{
				gamespy_map_name("timberland@ce", status.mapname, sizeof(status.mapname));
				gamespy_listing_publish(&status);
			}
		}
	}
	gamespy_listing_stop();
	/* (the notices still queued, the last ones the thread gave before it
	stopped among them: none is lost) */
	while (gamespy_listing_next_notice(notice, sizeof(notice)))
		printf("%s\n", notice);
	printf("stopped\n");
	return 0;
}
