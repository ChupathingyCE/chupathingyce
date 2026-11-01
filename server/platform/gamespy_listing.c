/*
GAMESPY_LISTING.C

GameSpy-style status queries and master heartbeats (gamespy_listing.h),
written from the public descriptions of GameSpy's query and QR2 reporting
protocols and from our own captures of Halo Custom Edition 1.0.10's
dedicated server (its replies, and its heartbeats to a local stand-in
master).

One UDP socket, on its own thread:
  - "\basic\", "\status\", "\info\", "\rules\", "\players\" (GameSpy's first
    query form): "\key\value" pairs, then "\final\\queryid\N.1";
  - FE FD 00 <id:4> FF FF FF (QR2's): 00 <id>, then key NUL value NUL pairs,
    a NUL, the players' section (a 2-byte count, the column names, a row
    each) and the teams' (none). A query for listed keys gets 00 <id>, as
    the stock server answers it;
  - FE FD 02 <data> (QR2's echo, which a LAN browser broadcasts): 05 <data>;
  - each master gets a heartbeat (03 <instance:4>, keys, players, teams) at
    once, every 60 seconds and soon after the state changes, a keep-alive
    (08 <instance:4>) every 20 seconds, and the answer to its challenge
    (FE FD 01 <instance:4> <challenge>): 01 <instance:4> <response> NUL.
    The response is the challenge enciphered with the game's key (GameSpy's
    RC4 variant, its index stepped by each byte) in base64. As the server
    stops, a last heartbeat says it is gone (statechanged 2).

Replies are rate limited (a reply is many times its query's size).
Nothing here logs an address: the log lines say what happened, not to
whom.
*/

#include "gamespy_listing.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum
{
	MAXIMUM_MASTERS = 4,
	MASTER_NAME_SIZE = 128,
	HEARTBEAT_SECONDS = 60,
	KEEPALIVE_SECONDS = 20,
	/* a changed state's heartbeat, at most this often */
	STATECHANGED_SECONDS = 10,
	/* a master's name looked up again this often */
	RESOLVE_SECONDS = 300,
	/* replies a second, at most */
	REPLIES_PER_SECOND = 200,
	NOTICES = 16,
	NOTICE_SIZE = 160,
};

/* the game's name and key, as the masters know Custom Edition */
static const char game_name[] = "halom";
static const char game_key[] = "e4Rd9J";

struct master
{
	char host[MASTER_NAME_SIZE];
	int port;
	int resolved;
	struct sockaddr_in address;
	time_t resolve_time;
	int registered;
	time_t heartbeat_time;
	time_t keepalive_time;
};

static pthread_mutex_t listing_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	pthread_t thread;
	int running;
	int stop;
	int socket;
	int port;
	char gamever[GAMESPY_SHORT_TEXT_SIZE];
	int master_count;
	struct master masters[MAXIMUM_MASTERS];
	unsigned char instance[4];
	struct gamespy_status status;
	int have_status;
	int changed;
	time_t changed_time;
	unsigned int query_count;
	time_t reply_second;
	int replies;
	char notices[NOTICES][NOTICE_SIZE];
	int notice_first;
	int notice_count;
} listing;

/* ---------- text */

static void copy_text(char *out, int size, const char *text)
{
	if (size <= 0)
		return;
	snprintf(out, (size_t)size, "%s", text ? text : "");
}

/* (backslashes and control characters, which end a GameSpy value, as spaces) */
static void clean_text(char *text)
{
	for (; *text; text++)
	{
		if (*text == '\\' || (unsigned char)*text < 0x20 || (unsigned char)*text == 0x7f)
			*text = ' ';
	}
}

static void notice(const char *format, const char *a)
{
	int index;

	pthread_mutex_lock(&listing_mutex);
	if (listing.notice_count == NOTICES)
	{
		listing.notice_first = (listing.notice_first + 1) % NOTICES;
		listing.notice_count--;
	}
	index = (listing.notice_first + listing.notice_count) % NOTICES;
	snprintf(listing.notices[index], NOTICE_SIZE, format, a ? a : "");
	listing.notice_count++;
	pthread_mutex_unlock(&listing_mutex);
}

/* ---------- the map's name (gamespy_listing.h) */

void gamespy_map_name(const char *map, char *out, int size)
{
	const char *base = map ? map : "";
	const char *cursor;
	const char *at;
	char name[GAMESPY_TEXT_SIZE];

	for (cursor = base; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			base = cursor + 1;
	}
	at = strchr(base, '@');
	if (!at)
	{
		/* an Xbox map: Custom Edition's own map of the name is Gearbox's PC
		version, so the Xbox one goes by xbox_<name> */
		snprintf(out, (size_t)size, "xbox_%s", base);
		return;
	}
	snprintf(name, sizeof(name), "%.*s", (int)(at - base), base);
	/* a Custom Edition or Halo PC map by its own name; any other family's
	map (HaloMD's, a map pack's) by its name too, as no Custom Edition map
	is known to match it */
	copy_text(out, size, name);
}

/* ---------- replies */

struct writer
{
	unsigned char *out;
	int size;
	int length;
	int overflow;
};

static void put_bytes(struct writer *writer, const void *bytes, int length)
{
	if (writer->overflow || length < 0 || writer->length + length > writer->size)
	{
		writer->overflow = 1;
		return;
	}
	memcpy(writer->out + writer->length, bytes, (size_t)length);
	writer->length += length;
}

/* a string with its NUL */
static void put_string(struct writer *writer, const char *text)
{
	put_bytes(writer, text, (int)strlen(text) + 1);
}

static void put_byte(struct writer *writer, int value)
{
	unsigned char byte = (unsigned char)value;

	put_bytes(writer, &byte, 1);
}

static void put_text(struct writer *writer, const char *text)
{
	put_bytes(writer, text, (int)strlen(text));
}

struct pair
{
	const char *key;
	char value[GAMESPY_TEXT_SIZE];
};

enum
{
	MAXIMUM_PAIRS = 20,
};

/* the server's keys, in the stock server's order, then this server's own */
static int status_pairs(const struct gamespy_status *status, const char *gamever, struct pair *pairs)
{
	int count = 0;

#define PAIR_TEXT(name, text) (pairs[count].key = (name), copy_text(pairs[count].value, GAMESPY_TEXT_SIZE, (text)), \
	clean_text(pairs[count].value), count++)
#define PAIR_NUMBER(name, number) (pairs[count].key = (name), snprintf(pairs[count].value, GAMESPY_TEXT_SIZE, "%d", \
	(number)), count++)
	PAIR_TEXT("hostname", status->hostname);
	PAIR_TEXT("gamever", gamever);
	PAIR_TEXT("hostport", "");
	PAIR_NUMBER("maxplayers", status->maxplayers);
	PAIR_NUMBER("password", status->password ? 1 : 0);
	PAIR_TEXT("mapname", status->mapname);
	PAIR_NUMBER("dedicated", 1);
	PAIR_TEXT("gamemode", status->playing ? "openplaying" : "openwaiting");
	PAIR_NUMBER("game_classic", 0);
	PAIR_NUMBER("numplayers", status->numplayers);
	PAIR_TEXT("gametype", status->gametype);
	PAIR_NUMBER("teamplay", status->teamplay ? 1 : 0);
	PAIR_TEXT("gamevariant", status->gamevariant);
	PAIR_NUMBER("fraglimit", status->fraglimit);
	/* what answers: a client that knows the key can tell this server from a
	Custom Edition one */
	PAIR_TEXT("bridge", "chupathingy");
	PAIR_TEXT("bridge_version", status->version);
#undef PAIR_TEXT
#undef PAIR_NUMBER
	return count;
}

static int player_rows(const struct gamespy_status *status)
{
	int count = status->player_count;

	if (count < 0)
		count = 0;
	if (count > GAMESPY_MAXIMUM_PLAYERS)
		count = GAMESPY_MAXIMUM_PLAYERS;
	return count;
}

static void player_name(const struct gamespy_player *player, char *name, int size)
{
	copy_text(name, size, player->name);
	clean_text(name);
}

/* the keys, players and teams, QR2's way (after its header) */
static void put_qr2_sections(struct writer *writer, const struct gamespy_status *status, const char *gamever)
{
	struct pair pairs[MAXIMUM_PAIRS];
	int count = status_pairs(status, gamever, pairs);
	int rows = player_rows(status);
	int index;

	for (index = 0; index < count; index++)
	{
		put_string(writer, pairs[index].key);
		put_string(writer, pairs[index].value);
	}
	put_byte(writer, 0);
	/* the players: a count, the columns, a row each */
	put_byte(writer, rows >> 8);
	put_byte(writer, rows & 0xff);
	put_string(writer, "player_");
	put_string(writer, "score_");
	put_string(writer, "ping_");
	put_string(writer, "team_");
	put_byte(writer, 0);
	for (index = 0; index < rows; index++)
	{
		const struct gamespy_player *player = &status->players[index];
		char name[GAMESPY_SHORT_TEXT_SIZE];
		char number[16];

		player_name(player, name, sizeof(name));
		put_string(writer, name);
		snprintf(number, sizeof(number), "%d", player->score);
		put_string(writer, number);
		if (player->ping >= 0)
			snprintf(number, sizeof(number), "%d", player->ping);
		else
			number[0] = 0;
		put_string(writer, number);
		snprintf(number, sizeof(number), "%d", player->team);
		put_string(writer, number);
	}
	/* the teams: none */
	put_byte(writer, 0);
	put_byte(writer, 0);
	put_string(writer, "team_t");
	put_string(writer, "score_t");
	put_byte(writer, 0);
}

static void put_classic_pair(struct writer *writer, const char *key, const char *value)
{
	put_text(writer, "\\");
	put_text(writer, key);
	put_text(writer, "\\");
	put_text(writer, value);
}

int gamespy_reply(const struct gamespy_status *status, const char *gamever, const unsigned char *query, int length,
	unsigned char *out, int size)
{
	struct writer writer;

	writer.out = out;
	writer.size = size;
	writer.length = 0;
	writer.overflow = 0;
	if (!status || !query || length < 3)
		return 0;
	if (query[0] == 0xfe && query[1] == 0xfd)
	{
		/* QR2's echo: its data back */
		if (query[2] == 0x02 && length >= 7)
		{
			put_byte(&writer, 0x05);
			put_bytes(&writer, query + 3, length - 3);
		}
		/* QR2's query: everything, or (keys listed) just the header */
		else if (query[2] == 0x00 && length >= 7)
		{
			put_byte(&writer, 0x00);
			put_bytes(&writer, query + 3, 4);
			if (length >= 10 && query[7] == 0xff && query[8] == 0xff && query[9] == 0xff)
				put_qr2_sections(&writer, status, gamever);
		}
		else
			return 0;
	}
	else if (query[0] == '\\')
	{
		/* GameSpy's first form: one reply, whatever was asked */
		struct pair pairs[MAXIMUM_PAIRS];
		int count = status_pairs(status, gamever, pairs);
		int rows = player_rows(status);
		int index;
		char key[24], value[GAMESPY_SHORT_TEXT_SIZE];
		unsigned int query_number;

		for (index = 0; index < count; index++)
			put_classic_pair(&writer, pairs[index].key, pairs[index].value);
		for (index = 0; index < rows; index++)
		{
			const struct gamespy_player *player = &status->players[index];

			snprintf(key, sizeof(key), "player_%d", index);
			player_name(player, value, sizeof(value));
			put_classic_pair(&writer, key, value);
			snprintf(key, sizeof(key), "score_%d", index);
			snprintf(value, sizeof(value), "%d", player->score);
			put_classic_pair(&writer, key, value);
			snprintf(key, sizeof(key), "ping_%d", index);
			if (player->ping >= 0)
				snprintf(value, sizeof(value), "%d", player->ping);
			else
				value[0] = 0;
			put_classic_pair(&writer, key, value);
			snprintf(key, sizeof(key), "team_%d", index);
			snprintf(value, sizeof(value), "%d", player->team);
			put_classic_pair(&writer, key, value);
		}
		put_classic_pair(&writer, "final", "");
		query_number = ++listing.query_count;
		snprintf(value, sizeof(value), "%u.1", query_number);
		put_classic_pair(&writer, "queryid", value);
	}
	else
		return 0;
	return writer.overflow ? 0 : writer.length;
}

int gamespy_heartbeat(const struct gamespy_status *status, const char *gamever, const unsigned char instance[4],
	int statechanged, const char *localip, int localport, unsigned char *out, int size)
{
	struct writer writer;
	char number[16];

	writer.out = out;
	writer.size = size;
	writer.length = 0;
	writer.overflow = 0;
	put_byte(&writer, 0x03);
	put_bytes(&writer, instance, 4);
	put_string(&writer, "localip0");
	put_string(&writer, localip ? localip : "");
	put_string(&writer, "localport");
	snprintf(number, sizeof(number), "%d", localport);
	put_string(&writer, number);
	/* (no NAT negotiation here) */
	put_string(&writer, "natneg");
	put_string(&writer, "0");
	if (statechanged)
	{
		put_string(&writer, "statechanged");
		snprintf(number, sizeof(number), "%d", statechanged);
		put_string(&writer, number);
	}
	put_string(&writer, "gamename");
	put_string(&writer, game_name);
	if (statechanged == 2 || !status)
	{
		put_byte(&writer, 0);
		return writer.overflow ? 0 : writer.length;
	}
	put_qr2_sections(&writer, status, gamever);
	return writer.overflow ? 0 : writer.length;
}

/* ---------- the challenge's answer */

static void gamespy_encipher(const unsigned char *key, int key_length, unsigned char *data, int length)
{
	unsigned char state[256];
	unsigned char swap;
	int index;
	int x = 0, y = 0;

	for (index = 0; index < 256; index++)
		state[index] = (unsigned char)index;
	for (index = 0; index < 256; index++)
	{
		y = (key[x] + state[index] + y) & 0xff;
		x = (x + 1) % key_length;
		swap = state[index];
		state[index] = state[y];
		state[y] = swap;
	}
	x = 0;
	y = 0;
	for (index = 0; index < length; index++)
	{
		/* (GameSpy's variant: the index steps by the byte, plus one) */
		x = (x + data[index] + 1) & 0xff;
		y = (state[x] + y) & 0xff;
		swap = state[x];
		state[x] = state[y];
		state[y] = swap;
		data[index] ^= state[(state[x] + state[y]) & 0xff];
	}
}

static void base64(const unsigned char *data, int length, char *out, int size)
{
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int index;
	int used = 0;

	for (index = 0; index < length && used + 5 <= size; index += 3)
	{
		unsigned int value = (unsigned int)data[index] << 16;

		if (index + 1 < length)
			value |= (unsigned int)data[index + 1] << 8;
		if (index + 2 < length)
			value |= data[index + 2];
		out[used++] = alphabet[(value >> 18) & 63];
		out[used++] = alphabet[(value >> 12) & 63];
		out[used++] = index + 1 < length ? alphabet[(value >> 6) & 63] : '=';
		out[used++] = index + 2 < length ? alphabet[value & 63] : '=';
	}
	if (size > 0)
		out[used < size ? used : size - 1] = 0;
}

void gamespy_challenge_response(const unsigned char *challenge, int length, char *out, int size)
{
	unsigned char data[128];

	if (length > (int)sizeof(data))
		length = (int)sizeof(data);
	if (length < 0)
		length = 0;
	memcpy(data, challenge, (size_t)length);
	gamespy_encipher((const unsigned char *)game_key, (int)strlen(game_key), data, length);
	base64(data, length, out, size);
}

/* ---------- the socket's thread */

static int reply_allowed(void)
{
	time_t now = time(NULL);

	if (now != listing.reply_second)
	{
		listing.reply_second = now;
		listing.replies = 0;
	}
	return listing.replies++ < REPLIES_PER_SECOND;
}

static int same_address(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
	return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void resolve(struct master *master, time_t now)
{
	struct addrinfo hints, *result = NULL;
	char port[16];

	if (master->resolved && now - master->resolve_time < RESOLVE_SECONDS)
		return;
	master->resolve_time = now;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	snprintf(port, sizeof(port), "%d", master->port);
	if (getaddrinfo(master->host, port, &hints, &result) || !result)
	{
		if (master->resolved || !master->heartbeat_time)
			notice("gamespy: cannot look up the master %s", master->host);
		master->resolved = 0;
		return;
	}
	memcpy(&master->address, result->ai_addr, sizeof(master->address));
	master->resolved = 1;
	freeaddrinfo(result);
}

/* this machine's address toward a master (no packet is sent to find it) */
static void local_address(const struct sockaddr_in *toward, char *text, int size)
{
	struct sockaddr_in local;
	socklen_t local_size = sizeof(local);
	int probe = socket(AF_INET, SOCK_DGRAM, 0);

	copy_text(text, size, "");
	if (probe < 0)
		return;
	if (!connect(probe, (const struct sockaddr *)toward, sizeof(*toward)) &&
		!getsockname(probe, (struct sockaddr *)&local, &local_size))
	{
		inet_ntop(AF_INET, &local.sin_addr, text, (socklen_t)size);
	}
	close(probe);
}

static void send_heartbeat(struct master *master, const struct gamespy_status *status, int statechanged, time_t now)
{
	unsigned char packet[GAMESPY_PACKET_SIZE];
	char localip[INET_ADDRSTRLEN];
	int length;

	resolve(master, now);
	master->heartbeat_time = now;
	master->keepalive_time = now;
	if (!master->resolved)
		return;
	local_address(&master->address, localip, sizeof(localip));
	length = gamespy_heartbeat(status, listing.gamever, listing.instance, statechanged, localip, listing.port, packet,
		sizeof(packet));
	if (length > 0)
		sendto(listing.socket, packet, (size_t)length, 0, (const struct sockaddr *)&master->address,
			sizeof(master->address));
}

static void send_keepalive(struct master *master, time_t now)
{
	unsigned char packet[5];

	master->keepalive_time = now;
	if (!master->resolved)
		return;
	packet[0] = 0x08;
	memcpy(packet + 1, listing.instance, 4);
	sendto(listing.socket, packet, sizeof(packet), 0, (const struct sockaddr *)&master->address, sizeof(master->address));
}

/* a packet from a master: its challenge, or its word that the server is listed */
static int master_packet(const unsigned char *packet, int length, const struct sockaddr_in *from)
{
	int index;

	if (length < 7 || packet[0] != 0xfe || packet[1] != 0xfd || memcmp(packet + 3, listing.instance, 4))
		return 0;
	for (index = 0; index < listing.master_count; index++)
	{
		struct master *master = &listing.masters[index];

		if (!master->resolved || !same_address(&master->address, from))
			continue;
		if (packet[2] == 0x01)
		{
			unsigned char answer[8 + 200];
			char response[200];
			int challenge_length = 0;

			while (7 + challenge_length < length && packet[7 + challenge_length])
				challenge_length++;
			if (challenge_length > 64)
				return 1;
			gamespy_challenge_response(packet + 7, challenge_length, response, sizeof(response));
			answer[0] = 0x01;
			memcpy(answer + 1, listing.instance, 4);
			memcpy(answer + 5, response, strlen(response) + 1);
			sendto(listing.socket, answer, 5 + strlen(response) + 1, 0, (const struct sockaddr *)from, sizeof(*from));
			return 1;
		}
		if (packet[2] == 0x0a)
		{
			if (!master->registered)
				notice("gamespy: a master listed the server%s", "");
			master->registered = 1;
			return 1;
		}
		return 1;
	}
	return 0;
}

static void *listing_thread(void *unused)
{
	struct gamespy_status status;
	int have_status = 0;
	int first = 1;

	(void)unused;
	for (;;)
	{
		struct pollfd poll_socket;
		time_t now = time(NULL);
		int stop, changed;
		int index;

		pthread_mutex_lock(&listing_mutex);
		stop = listing.stop;
		changed = listing.changed;
		if (listing.have_status)
		{
			status = listing.status;
			have_status = 1;
		}
		if (changed && now - listing.changed_time >= STATECHANGED_SECONDS)
		{
			listing.changed = 0;
			listing.changed_time = now;
		}
		else
			changed = 0;
		pthread_mutex_unlock(&listing_mutex);

		if (stop)
		{
			for (index = 0; index < listing.master_count; index++)
				send_heartbeat(&listing.masters[index], NULL, 2, now);
			break;
		}
		if (have_status)
		{
			for (index = 0; index < listing.master_count; index++)
			{
				struct master *master = &listing.masters[index];

				if (first || changed || now - master->heartbeat_time >= HEARTBEAT_SECONDS)
					send_heartbeat(master, &status, first ? 3 : changed ? 1 : 0, now);
				else if (now - master->keepalive_time >= KEEPALIVE_SECONDS)
					send_keepalive(master, now);
			}
			first = 0;
		}

		poll_socket.fd = listing.socket;
		poll_socket.events = POLLIN;
		poll_socket.revents = 0;
		if (poll(&poll_socket, 1, 250) <= 0 || !(poll_socket.revents & POLLIN))
			continue;
		for (index = 0; index < 64; index++)
		{
			unsigned char packet[GAMESPY_PACKET_SIZE];
			unsigned char reply[GAMESPY_PACKET_SIZE];
			struct sockaddr_in from;
			socklen_t from_size = sizeof(from);
			ssize_t length = recvfrom(listing.socket, packet, sizeof(packet), MSG_DONTWAIT, (struct sockaddr *)&from,
				&from_size);
			int reply_length;

			if (length <= 0)
				break;
			if (from_size != sizeof(from) || from.sin_family != AF_INET)
				continue;
			if (master_packet(packet, (int)length, &from))
				continue;
			if (!have_status || !reply_allowed())
				continue;
			reply_length = gamespy_reply(&status, listing.gamever, packet, (int)length, reply, sizeof(reply));
			if (reply_length > 0)
				sendto(listing.socket, reply, (size_t)reply_length, 0, (const struct sockaddr *)&from, sizeof(from));
		}
	}
	return NULL;
}

/* ---------- public code */

static int parse_masters(const char *masters, char *problem, int problem_size)
{
	const char *cursor = masters ? masters : "";

	listing.master_count = 0;
	while (*cursor)
	{
		const char *end = strchr(cursor, ',');
		size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
		char entry[MASTER_NAME_SIZE + 8];
		char *colon;
		struct master *master;
		long port = 27900;

		while (length && (*cursor == ' ' || *cursor == '\t'))
		{
			cursor++;
			length--;
		}
		while (length && (cursor[length - 1] == ' ' || cursor[length - 1] == '\t'))
			length--;
		if (length)
		{
			if (length >= MASTER_NAME_SIZE || listing.master_count == MAXIMUM_MASTERS)
			{
				snprintf(problem, (size_t)problem_size, "the masters: at most %d, each host:port", MAXIMUM_MASTERS);
				return 0;
			}
			memcpy(entry, cursor, length);
			entry[length] = 0;
			colon = strrchr(entry, ':');
			if (colon)
			{
				char *number_end;

				*colon = 0;
				port = strtol(colon + 1, &number_end, 10);
				if (*number_end || port < 1 || port > 65535)
				{
					snprintf(problem, (size_t)problem_size, "the master %s: not host:port", entry);
					return 0;
				}
			}
			if (!entry[0])
			{
				snprintf(problem, (size_t)problem_size, "a master with no host");
				return 0;
			}
			master = &listing.masters[listing.master_count++];
			memset(master, 0, sizeof(*master));
			copy_text(master->host, sizeof(master->host), entry);
			master->port = (int)port;
		}
		if (!end)
			break;
		cursor = end + 1;
	}
	return 1;
}

int gamespy_listing_start(int port, const char *masters, const char *gamever, char *problem, int problem_size)
{
	struct sockaddr_in address;
	int one = 1;
	int index;

	copy_text(problem, problem_size, "");
	if (listing.running)
		return 1;
	if (port < 1 || port > 65535)
	{
		snprintf(problem, (size_t)problem_size, "the port %d is not 1 to 65535", port);
		return 0;
	}
	if (!parse_masters(masters, problem, problem_size))
		return 0;
	listing.socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (listing.socket < 0)
	{
		snprintf(problem, (size_t)problem_size, "no socket: %s", strerror(errno));
		return 0;
	}
	fcntl(listing.socket, F_SETFD, FD_CLOEXEC);
	setsockopt(listing.socket, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons((unsigned short)port);
	if (bind(listing.socket, (const struct sockaddr *)&address, sizeof(address)))
	{
		snprintf(problem, (size_t)problem_size, "UDP port %d: %s", port, strerror(errno));
		close(listing.socket);
		listing.socket = -1;
		return 0;
	}
	listing.port = port;
	copy_text(listing.gamever, sizeof(listing.gamever), gamever);
	/* (the instance key: any four bytes, the same for the server's life) */
	srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
	for (index = 0; index < 4; index++)
		listing.instance[index] = (unsigned char)(rand() & 0xff);
	listing.stop = 0;
	listing.have_status = 0;
	if (pthread_create(&listing.thread, NULL, listing_thread, NULL))
	{
		snprintf(problem, (size_t)problem_size, "no thread");
		close(listing.socket);
		listing.socket = -1;
		return 0;
	}
	listing.running = 1;
	return 1;
}

void gamespy_listing_publish(const struct gamespy_status *status)
{
	pthread_mutex_lock(&listing_mutex);
	if (listing.have_status && (strcmp(listing.status.hostname, status->hostname) ||
		strcmp(listing.status.mapname, status->mapname) || strcmp(listing.status.gametype, status->gametype) ||
		listing.status.numplayers != status->numplayers || listing.status.maxplayers != status->maxplayers ||
		listing.status.playing != status->playing))
	{
		listing.changed = 1;
	}
	listing.status = *status;
	listing.have_status = 1;
	pthread_mutex_unlock(&listing_mutex);
}

void gamespy_listing_stop(void)
{
	if (!listing.running)
		return;
	pthread_mutex_lock(&listing_mutex);
	listing.stop = 1;
	pthread_mutex_unlock(&listing_mutex);
	pthread_join(listing.thread, NULL);
	close(listing.socket);
	listing.socket = -1;
	listing.running = 0;
}

int gamespy_listing_running(void)
{
	return listing.running;
}

int gamespy_listing_next_notice(char *text, int size)
{
	int found = 0;

	pthread_mutex_lock(&listing_mutex);
	if (listing.notice_count)
	{
		copy_text(text, size, listing.notices[listing.notice_first]);
		listing.notice_first = (listing.notice_first + 1) % NOTICES;
		listing.notice_count--;
		found = 1;
	}
	pthread_mutex_unlock(&listing_mutex);
	return found;
}
