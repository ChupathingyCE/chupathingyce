/*
EVENTS_TEST.C

Delta Stats' event log and its compression (port/linux/src/event_log.c,
event_gzip.c), driven as the game's side drives them, for
tools/test_event_log.py, which checks what this prints:

  events_test sample            a short game's batch (JSON) on stdout
  events_test part              a part of a game, then its end: two batches,
                                one a line ("\f" between them)
  events_test limits CAPACITY   a long game kept in CAPACITY events: the
                                batch, then a line "kept N dropped N"
  events_test gzip IN OUT       IN compressed to OUT (gzip)
  events_test fuzz SEED COUNT   random calls, every batch written to stdout
                                ("\f" between them)
*/

#include "../../port/linux/src/event_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void game(char const *map, int engine, int teams)
{
	struct event_log_game game;
	unsigned char id[16];
	int index;

	memset(&game, 0, sizeof(game));
	snprintf(game.map, sizeof(game.map), "%s", map);
	snprintf(game.gametype, sizeof(game.gametype), "%s", teams ? "Team Slayer" : "Slayer");
	game.engine = engine;
	game.teams = teams;
	game.score_limit = 25;
	snprintf(game.server_name, sizeof(game.server_name), "Test \"server\"");
	snprintf(game.build, sizeof(game.build), "0.7.0b");
	snprintf(game.platform, sizeof(game.platform), "linux-x64");
	snprintf(game.playlist, sizeof(game.playlist), "team_slayer");
	game.start_time = 1791234567u;
	for (index = 0; index < 16; index++)
		id[index] = (unsigned char)(index * 17);
	event_log_begin(&game, id);
}

static int player(int tick, char const *name, char const *hardware_id, int team)
{
	struct event_log_player_identity identity;

	memset(&identity, 0, sizeof(identity));
	snprintf(identity.name, sizeof(identity.name), "%s", name);
	snprintf(identity.hardware_id, sizeof(identity.hardware_id), "%s", hardware_id);
	identity.client = hardware_id[0] ? EVENT_LOG_CLIENT_CHUPATHINGYCE : EVENT_LOG_CLIENT_OTHER;
	snprintf(identity.platform, sizeof(identity.platform), "%s", hardware_id[0] ? "linux" : "");
	identity.team = team;
	identity.color = hardware_id[0] ? 3 : -1;
	return event_log_player(tick, &identity);
}

static void kill(int tick, int killer, int victim, char const *damage, int kind, unsigned int bits)
{
	struct event_log_record record;

	memset(&record, 0, sizeof(record));
	record.tick = tick;
	record.type = EVENT_LOG_KILL;
	record.player = (short)killer;
	record.other = (short)victim;
	record.tag[0] = (short)event_log_tag(damage);
	record.tag[1] = record.tag[2] = EVENT_LOG_NONE;
	record.value[0] = kind;
	record.bits = bits | EVENT_LOG_KILL_KILLER_POSITION | EVENT_LOG_KILL_VICTIM_POSITION;
	record.position[0] = 60.5f + (float)(tick % 7);
	record.position[1] = -120.25f;
	record.position[2] = 1.0f;
	record.other_position[0] = 70.0f;
	record.other_position[1] = -118.0f + (float)(tick % 5);
	record.other_position[2] = 0.5f;
	event_log_add(&record);
}

static void simple(int tick, int type, int player_slot, int tag, int value0, int value1, float x, float y)
{
	struct event_log_record record;

	memset(&record, 0, sizeof(record));
	record.tick = tick;
	record.type = (short)type;
	record.player = (short)player_slot;
	record.other = EVENT_LOG_NONE;
	record.tag[0] = (short)tag;
	record.tag[1] = record.tag[2] = EVENT_LOG_NONE;
	record.value[0] = value0;
	record.value[1] = value1;
	record.position[0] = x;
	record.position[1] = y;
	record.position[2] = 0.25f;
	event_log_add(&record);
}

static void totals(int slot, int score, int place, int kills, int deaths, int team)
{
	struct event_log_player_totals line;

	memset(&line, 0, sizeof(line));
	line.score = score;
	line.place = place;
	line.kills = kills;
	line.deaths = deaths;
	line.team = team;
	line.assists = 1;
	event_log_player_totals(slot, &line);
}

static char *finish(int tick, int final, size_t *length)
{
	struct event_log_end end;

	memset(&end, 0, sizeof(end));
	end.tick = tick;
	end.reason = EVENT_LOG_END_SCORE;
	end.team_scores[0] = 3;
	end.team_scores[1] = 1;
	end.end_time = 1791234567u + (unsigned int)(tick / EVENT_LOG_TICKS_PER_SECOND);
	return event_log_finish(&end, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", final, length);
}

static void play_sample(void)
{
	int walter, jo, guest;
	int pistol = 0;

	game("levels\\test\\bloodgulch\\bloodgulch", 2, 1);
	walter = player(0, "Walter", "00112233445566778899AABBCCDDEEFF", 0);
	jo = player(0, "Jo \xE2\x9C\x93", "ffeeddccbbaa99887766554433221100", 1);
	guest = player(90, "guest\x01", "", 1);
	pistol = event_log_tag("weapons\\pistol");
	event_log_shots(walter, pistol, 10);
	event_log_hit(walter, pistol, 0.4f);
	event_log_hit(walter, pistol, 0.4f);
	event_log_shots(jo, event_log_tag("weapons\\assault rifle"), 40);
	event_log_hit(jo, event_log_tag("weapons\\assault rifle"), 0.1f);
	event_log_grenade(walter, 1);
	event_log_damage(walter, jo, 1.5f);
	simple(30, EVENT_LOG_SPAWN, walter, EVENT_LOG_NONE, 0, 0, 40.0f, -100.0f);
	simple(30, EVENT_LOG_POSITION, walter, EVENT_LOG_NONE, 0, 0, 41.0f, -101.0f);
	simple(30, EVENT_LOG_POSITION, jo, EVENT_LOG_NONE, 0, 0, 51.0f, -111.0f);
	simple(60, EVENT_LOG_PICKUP, walter, event_log_tag("weapons\\sniper rifle\\sniper rifle"), 0, 0, 42.0f, -102.0f);
	simple(65, EVENT_LOG_PICKUP, jo, event_log_tag("powerups\\active camouflage"), 0, 0, 52.0f, -102.0f);
	kill(100, walter, jo, "weapons\\pistol\\bullet", EVENT_LOG_DAMAGE_BULLET, EVENT_LOG_KILL_HEADSHOT);
	kill(150, walter, guest, "weapons\\pistol\\bullet", EVENT_LOG_DAMAGE_BULLET, 0);
	kill(400, jo, walter, "weapons\\assault rifle\\melee", EVENT_LOG_DAMAGE_MELEE, 0);
	kill(420, -1, guest, "globals\\falling", EVENT_LOG_DAMAGE_FALL, 0);
	kill(450, jo, jo, "weapons\\frag grenade\\explosion", EVENT_LOG_DAMAGE_GRENADE, EVENT_LOG_KILL_SUICIDE);
	simple(500, EVENT_LOG_OBJECTIVE, walter, EVENT_LOG_NONE, EVENT_LOG_OBJECTIVE_FLAG_GRAB, 0, 90.0f, -150.0f);
	simple(900, EVENT_LOG_RIDE, jo, event_log_tag("vehicles\\warthog\\mp_warthog"), EVENT_LOG_SEAT_DRIVER, 600, 0, 0);
	simple(910, EVENT_LOG_PING, jo, EVENT_LOG_NONE, 85, 0, 0, 0);
	event_log_moderation(EVENT_LOG_MODERATION_KICK, "guest\x01", "console", "team killing");
	event_log_moderation(EVENT_LOG_MODERATION_BAN, "Nobody", "admin:milenko", "");
	event_log_player_left(guest, 950, EVENT_LOG_LEFT_QUIT);
	totals(walter, 2, 1, 2, 1, 0);
	totals(jo, 1, 2, 1, 2, 1);
}

int main(int argc, char **argv)
{
	size_t length;
	char *json;

	if (argc >= 2 && !strcmp(argv[1], "sample"))
	{
		play_sample();
		json = finish(1200, 1, &length);
		if (!json)
			return 1;
		fwrite(json, 1, length, stdout);
		free(json);
		/* (nothing more once it is finished) */
		return finish(1300, 1, &length) == NULL ? 0 : 2;
	}
	if (argc >= 2 && !strcmp(argv[1], "abandon"))
	{
		/* a game torn down before its end (game_events_update): its batch
		thrown away through event_log_free, and nothing after, which
		event_log_free takes too */
		play_sample();
		json = finish(1200, 1, &length);
		if (!json)
			return 1;
		event_log_free(json);
		json = finish(1300, 1, &length);
		if (json)
			return 2;
		event_log_free(json);
		return 0;
	}
	if (argc >= 2 && !strcmp(argv[1], "medals"))
	{
		/* Walter's spree of five, which Jo ends from the grave */
		int walter, jo, guest, index;
		struct event_log_game game_with_playlist;

		play_sample();
		(void)game_with_playlist;
		walter = 0, jo = 1, guest = 2;
		event_log_player_rejoined(guest, 1300);
		for (index = 0; index < 5; index++)
			kill(2000 + 200 * index, walter, index % 2 ? jo : guest, "weapons\\pistol\\bullet", EVENT_LOG_DAMAGE_BULLET, 0);
		kill(3500, jo, walter, "weapons\\frag grenade\\explosion", EVENT_LOG_DAMAGE_GRENADE, EVENT_LOG_KILL_FROM_GRAVE);
		json = finish(3600, 1, &length);
		if (!json)
			return 1;
		fwrite(json, 1, length, stdout);
		free(json);
		return 0;
	}
	if (argc >= 2 && !strcmp(argv[1], "part"))
	{
		play_sample();
		json = finish(1000, 0, &length);
		if (!json)
			return 1;
		fwrite(json, 1, length, stdout);
		free(json);
		putchar('\f');
		kill(1100, 0, 1, "weapons\\pistol\\bullet", EVENT_LOG_DAMAGE_BULLET, 0);
		json = finish(1200, 1, &length);
		if (!json)
			return 1;
		fwrite(json, 1, length, stdout);
		free(json);
		return 0;
	}
	if (argc >= 3 && !strcmp(argv[1], "limits"))
	{
		int players[16];
		int tick, index;

		event_log_set_capacity(atoi(argv[2]));
		game("levels\\test\\bloodgulch\\bloodgulch", 2, 0);
		for (index = 0; index < 16; index++)
		{
			char name[16];

			snprintf(name, sizeof(name), "player%d", index);
			players[index] = player(0, name, "", -1);
		}
		/* an hour: positions every second, a kill every 3 seconds, a pickup
		every 5 */
		for (tick = 0; tick < 3600 * EVENT_LOG_TICKS_PER_SECOND; tick += EVENT_LOG_TICKS_PER_SECOND)
		{
			for (index = 0; index < 16; index++)
				simple(tick, EVENT_LOG_POSITION, players[index], EVENT_LOG_NONE, 0, 0, (float)index, (float)tick);
			if (tick % (3 * EVENT_LOG_TICKS_PER_SECOND) == 0)
				kill(tick, players[tick % 16], players[(tick + 1) % 16], "weapons\\pistol\\bullet", 1, 0);
			if (tick % (5 * EVENT_LOG_TICKS_PER_SECOND) == 0)
				simple(tick, EVENT_LOG_PICKUP, players[tick % 16], event_log_tag("weapons\\pistol\\pistol"), 0, 0, 1, 1);
		}
		index = event_log_count();
		json = finish(tick, 1, &length);
		if (!json)
			return 1;
		fwrite(json, 1, length, stdout);
		free(json);
		printf("kept %d dropped %d\n", index, event_log_dropped());
		return 0;
	}
	if (argc >= 4 && !strcmp(argv[1], "gzip"))
	{
		FILE *in = fopen(argv[2], "rb");
		FILE *out = fopen(argv[3], "wb");
		unsigned char *data = NULL, *packed;
		size_t size = 0, used = 0, read;

		if (!in || !out)
			return 1;
		do
		{
			if (used == size)
			{
				size = size ? size * 2 : 65536;
				data = realloc(data, size);
				if (!data)
					return 1;
			}
			read = fread(data + used, 1, size - used, in);
			used += read;
		} while (read);
		packed = event_gzip(data, used, &length);
		if (!packed)
			return 1;
		fwrite(packed, 1, length, out);
		fclose(in);
		fclose(out);
		free(packed);
		free(data);
		return 0;
	}
	if (argc >= 4 && !strcmp(argv[1], "fuzz"))
	{
		unsigned int seed = (unsigned int)atoi(argv[2]);
		int rounds = atoi(argv[3]);
		int round;

		srand(seed);
		event_log_set_capacity(200);
		for (round = 0; round < rounds; round++)
		{
			int step, steps = rand() % 600;

			game(rand() % 2 ? "bloodgulch" : "levels\\test\\\"odd\\x\\y", rand() % 6, rand() % 2);
			for (step = 0; step < steps; step++)
			{
				struct event_log_record record;
				char text[EVENT_LOG_TAG_SIZE + 8];
				int index;

				switch (rand() % 8)
				{
				case 0:
				{
					char name[EVENT_LOG_NAME_SIZE + 8];

					for (index = 0; index < (int)sizeof(name) - 1; index++)
						name[index] = (char)(1 + rand() % 255);
					name[rand() % (int)sizeof(name)] = 0;
					name[sizeof(name) - 1] = 0;
					player(rand() % 100000 - 50, name, rand() % 2 ? "0a1b2c3d" : "", rand() % 20 - 2);
					break;
				}
				case 1:
					for (index = 0; index < (int)sizeof(text) - 1; index++)
						text[index] = (char)(1 + rand() % 255);
					text[rand() % (int)sizeof(text)] = 0;
					text[sizeof(text) - 1] = 0;
					event_log_tag(text);
					break;
				case 2:
					event_log_shots(rand() % 70 - 3, rand() % 400 - 3, rand() % 50 - 5);
					event_log_hit(rand() % 70 - 3, rand() % 400 - 3, (float)(rand() % 100) - 10.0f);
					break;
				case 3:
					event_log_player_left(rand() % 70 - 3, rand() % 100000, rand() % 8 - 2);
					event_log_player_rejoined(rand() % 70 - 3, rand() % 100000);
					break;
				case 4:
					event_log_moderation(rand() % 9 - 1, rand() % 2 ? "x" : NULL, "console", rand() % 2 ? "why" : NULL);
					break;
				default:
					memset(&record, 0, sizeof(record));
					record.tick = rand() % 200000 - 100;
					record.type = (short)(rand() % (EVENT_LOG_NUMBER_OF_TYPES + 2) - 1);
					record.player = (short)(rand() % 70 - 3);
					record.other = (short)(rand() % 70 - 3);
					for (index = 0; index < 3; index++)
						record.tag[index] = (short)(rand() % 400 - 3);
					for (index = 0; index < 4; index++)
						record.value[index] = rand() % 2 ? rand() % 20 - 3 : rand() - RAND_MAX / 2;
					record.bits = (unsigned int)rand();
					for (index = 0; index < 3; index++)
					{
						record.position[index] = (float)(rand() % 20000 - 10000) / 1.5f;
						record.other_position[index] = rand() % 50 ? (float)rand() : 1e30f * 1e30f;
					}
					event_log_add(&record);
					break;
				}
			}
			json = finish(rand() % 300000, rand() % 4 != 0, &length);
			if (json)
			{
				fwrite(json, 1, length, stdout);
				free(json);
			}
			putchar('\f');
			if (event_log_recording())
			{
				json = finish(rand() % 300000, 1, &length);
				free(json);
			}
		}
		return 0;
	}
	fprintf(stderr, "usage: events_test sample|part|limits N|gzip IN OUT|fuzz SEED COUNT\n");
	return 2;
}
