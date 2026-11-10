/*
DELTA_FUZZ.C

libFuzzer's entry for Delta Peer (tools/test_delta_peer.py builds it with
-fsanitize=fuzzer,address,undefined where clang has libFuzzer, and runs it
for a bounded number of inputs): each input is one datagram, given to every
parser and to a host's and a client's session (each with a legacy table to
relay; the host with moderation, which takes any key's actions), which then
hand each other what they sent, a few rounds, so they shake hands as two
machines do and later inputs reach sessions under way; a frame of both now
and then; and the traits, platform key and chat bridge entry readers.

    clang -fsanitize=fuzzer,address,undefined -iquote port/linux/include \
        -I port/third_party/monocypher port/linux/tests/delta_fuzz.c port/linux/src/delta_peer.c \
        port/linux/src/delta_wire.c port/linux/src/delta_chat.c port/third_party/monocypher/monocypher.c \
        port/third_party/monocypher/monocypher-ed25519.c
*/

#include "../src/delta_peer.h"
#include "../src/delta_chat_bridge.h"

#include <stddef.h>
#include <string.h>

enum
{
	HOST_IPV4 = 0x0100007F,
	CLIENT_IPV4 = 0x0300007F,
	CLIENT_PORT = 40001
};

static struct delta_peer host, client;
static int ready;

/* (each session's sends are kept, a few, and handed to the other after the
input: the two shake hands, and inputs reach sessions under way) */
enum
{
	QUEUED_MAXIMUM = 8
};

struct queue
{
	int count;
	int sizes[QUEUED_MAXIMUM];
	unsigned char datagrams[QUEUED_MAXIMUM][DELTA_WIRE_MAXIMUM_DATAGRAM];
};

static struct queue from_host, from_client;

static void keep(void *context, delta_u32 ipv4, unsigned short port, const unsigned char *data, int size)
{
	struct queue *queue = context;

	(void)ipv4;
	(void)port;
	if (queue->count < QUEUED_MAXIMUM && size > 0 && size <= DELTA_WIRE_MAXIMUM_DATAGRAM)
	{
		memcpy(queue->datagrams[queue->count], data, (size_t)size);
		queue->sizes[queue->count++] = size;
	}
}

static void deliver(delta_u32 now)
{
	struct queue host_sent, client_sent;
	int round, index;

	for (round = 0; round < 3 && (from_host.count || from_client.count); round++)
	{
		host_sent = from_host;
		client_sent = from_client;
		from_host.count = 0;
		from_client.count = 0;
		for (index = 0; index < client_sent.count; index++)
			delta_peer_receive(&host, now, CLIENT_IPV4, CLIENT_PORT, client_sent.datagrams[index],
				client_sent.sizes[index]);
		for (index = 0; index < host_sent.count; index++)
			delta_peer_receive(&client, now, HOST_IPV4, DELTA_PEER_PORT, host_sent.datagrams[index],
				host_sent.sizes[index]);
	}
	from_host.count = 0;
	from_client.count = 0;
}

static delta_u32 fixed_random(void *context)
{
	(void)context;
	return 0x12345678u;
}

/* (the legacy table's relay: a serial, a small table to send, and a check
that takes nothing, so the pieces go together but are never taken) */
static delta_u32 table_serial(void *context)
{
	(void)context;
	return 3;
}

static int table_signed(void *context, unsigned char *buffer, int size)
{
	(void)context;
	if (size < 2000)
		return 0;
	memset(buffer, 'a', 2000);
	return 2000;
}

static int table_offer(void *context, const unsigned char *table, int size)
{
	(void)context;
	(void)table;
	(void)size;
	return 0;
}

/* (moderation: any key an owner, every action done) */
static int key_role(void *context, const unsigned char *key, delta_u32 *permissions, delta_u32 *ban_minutes)
{
	(void)context;
	(void)key;
	*permissions = 0x1FF;
	*ban_minutes = 60;
	return 3;
}

static int action(void *context, int machine_index, const unsigned char *key, int kind, int target, int minutes,
	const char *reason, char *result, int result_size)
{
	(void)context;
	(void)machine_index;
	(void)key;
	(void)kind;
	(void)target;
	(void)minutes;
	(void)reason;
	result[0] = 0;
	(void)result_size;
	return 1;
}

static const struct delta_peer_moderation_host moderation_host = { NULL, NULL, key_role, action, NULL };

/* (the host's chat relays every line to machine 1) */
static int chat_say(void *context, int machine_index, int scope, const char *text, int length)
{
	struct delta_wire_chat_line line;

	(void)context;
	memset(&line, 0, sizeof(line));
	line.scope = (unsigned char)scope;
	line.speaker_machine = (unsigned char)machine_index;
	line.length = length;
	memcpy(line.text, text, (size_t)length);
	delta_peer_chat_line(&host, 1, &line);
	return 0;
}

static const struct delta_peer_chat_host chat_host = { NULL, chat_say };

static void start(void)
{
	struct delta_peer_env env;
	struct delta_peer_local local;
	struct delta_peer_game_machine machines[2];
	signed char players[DELTA_PEER_MAXIMUM_PLAYERS];

	memset(&env, 0, sizeof(env));
	env.send_datagram = keep;
	env.random_number = fixed_random;
	env.legacy_table_serial = table_serial;
	env.legacy_table_signed = table_signed;
	env.legacy_table_offer = table_offer;
	memset(&local, 0, sizeof(local));
	/* (platform, profile and ce_maps; moderation and chat) */
	local.capabilities = 0x13 | (1u << _delta_capability_moderation) | (1u << _delta_capability_chat) |
		(1u << _delta_capability_server_messages);
	local.legacy_version = 18;
	delta_platform_policy_default(_delta_platform_pc_linux, &local.key);
	local.has_profile = 1;
	env.context = &from_host;
	delta_peer_initialize(&host, &env, &local);
	env.context = &from_client;
	delta_peer_initialize(&client, &env, &local);
	delta_peer_set_moderation_host(&host, &moderation_host);
	delta_peer_set_chat_host(&host, &chat_host);
	delta_peer_set_moderation_binding(&host, "0123456789abcdef0123456789abcdef");

	memset(machines, 0, sizeof(machines));
	machines[0].machine_index = 0;
	machines[0].local = 1;
	machines[1].machine_index = 1;
	machines[1].ipv4 = CLIENT_IPV4;
	memset(players, -1, sizeof(players));
	players[0] = 0;
	players[1] = 1;
	delta_peer_host_frame(&host, 1000, machines, 2, players);
	{
		struct delta_wire_map map;

		memset(&map, 0, sizeof(map));
		map.family = 1;
		map.flags = DELTA_WIRE_MAP_HASHED;
		strcpy(map.name, "fuzz");
		delta_peer_set_map(&host, &map);
	}
	delta_peer_client_frame(&client, 1000, 1, HOST_IPV4, DELTA_PEER_PORT, 1, 1, players);
	deliver(1000);
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	static struct delta_wire_roster roster;
	struct delta_wire_header header;
	struct delta_wire_hello hello;
	struct delta_wire_welcome welcome;
	struct delta_wire_profile profile;
	struct delta_wire_table table;
	delta_u32 serial;
	static delta_u32 now = 2000;

	if (!ready)
	{
		start();
		ready = 1;
	}
	if (size > DELTA_WIRE_MAXIMUM_DATAGRAM + 16)
		return 0;
	if (delta_wire_read_header(data, (int)size, &header))
	{
		delta_wire_read_hello(data + DELTA_WIRE_HEADER_SIZE, header.length, &hello);
		delta_wire_read_welcome(data + DELTA_WIRE_HEADER_SIZE, header.length, &welcome);
		delta_wire_read_profile(data + DELTA_WIRE_HEADER_SIZE, header.length, &profile);
		delta_wire_read_roster(data + DELTA_WIRE_HEADER_SIZE, header.length, &roster);
		if (delta_wire_read_table(data + DELTA_WIRE_HEADER_SIZE, header.length, &table) &&
			(table.offset + (delta_u32)table.length > table.total || table.total > DELTA_LEGACY_SIGNED_SIZE))
		{
			__builtin_trap();
		}
		delta_wire_read_table_have(data + DELTA_WIRE_HEADER_SIZE, header.length, &serial);
		{
			struct delta_wire_mod_challenge challenge;
			struct delta_wire_mod_proof proof;
			struct delta_wire_mod_state state;
			struct delta_wire_mod_action mod_action;
			struct delta_wire_mod_result result;
			struct delta_wire_mod_notice notice;
			struct delta_wire_mod_bind bind;
			struct delta_wire_mod_bind_answer answer;
			const unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

			if (delta_wire_read_mod_challenge(payload, header.length, &challenge) &&
				challenge.binding_length > DELTA_WIRE_MODERATION_BINDING_SIZE)
				__builtin_trap();
			delta_wire_read_mod_proof(payload, header.length, &proof);
			delta_wire_read_mod_state(payload, header.length, &state);
			if (delta_wire_read_mod_action(payload, header.length, &mod_action) &&
				mod_action.reason_length > DELTA_WIRE_MODERATION_REASON_SIZE)
				__builtin_trap();
			delta_wire_read_mod_result(payload, header.length, &result);
			delta_wire_read_mod_notice(payload, header.length, &notice);
			delta_wire_read_mod_bind(payload, header.length, &bind);
			delta_wire_read_mod_bind_answer(payload, header.length, &answer);
		}
		{
			struct delta_wire_chat_say say;
			struct delta_wire_chat_line line;
			struct delta_wire_chat_config config;
			struct delta_wire_chat_refused refused;
			const unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

			if (delta_wire_read_chat_say(payload, header.length, &say) &&
				(say.length < 1 || say.length > DELTA_WIRE_CHAT_SAY_TEXT_SIZE))
				__builtin_trap();
			if (delta_wire_read_chat_say(payload, header.length, &say))
				delta_wire_chat_text_valid(say.text, say.length, DELTA_WIRE_CHAT_SAY_CHARACTERS);
			if (delta_wire_read_chat_line(payload, header.length, &line) &&
				(line.length > DELTA_WIRE_CHAT_TEXT_SIZE || line.name_length > DELTA_WIRE_CHAT_NAME_SIZE))
				__builtin_trap();
			delta_wire_read_chat_config(payload, header.length, &config);
			delta_wire_read_chat_refused(payload, header.length, &refused);
		}
		{
			struct delta_wire_team_request request;
			struct delta_wire_team_result result;
			const unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

			if (delta_wire_read_team_request(payload, header.length, &request) && request.team > 1 &&
				request.team != DELTA_WIRE_TEAM_OTHER)
				__builtin_trap();
			if (delta_wire_read_team_result(payload, header.length, &result) &&
				(result.team > 1 || result.result > _delta_team_refused_last))
				__builtin_trap();
		}
		{
			struct delta_wire_host_rules rules;

			if (delta_wire_read_host_rules(data + DELTA_WIRE_HEADER_SIZE, header.length, &rules) &&
				((rules.flags & ~DELTA_WIRE_HOST_KNOWN_FLAGS) || rules.team_change > DELTA_WIRE_HOST_TEAM_CHANGE_ALLOW ||
					rules.team_change_margin > DELTA_WIRE_HOST_TEAM_CHANGE_MAXIMUM_MARGIN))
			{
				__builtin_trap();
			}
		}
		{
			struct delta_wire_voice_up up;
			struct delta_wire_voice_down down;
			struct delta_wire_voice_config config;
			const unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;
			const unsigned char *packet;

			/* (a frame's bytes are exactly what came, within the payload) */
			if (delta_wire_read_voice_up(payload, header.length, &up, &packet) &&
				(up.length ? (!packet || packet + up.length != payload + header.length ||
					up.channel >= NUMBER_OF_DELTA_VOICE_CHANNELS || !up.channel) : (packet || up.channel)))
			{
				__builtin_trap();
			}
			if (delta_wire_read_voice_down(payload, header.length, &down, &packet) &&
				(!down.length || !packet || packet + down.length != payload + header.length ||
					(down.route != _delta_voice_route_global && down.route != _delta_voice_route_proximity)))
			{
				__builtin_trap();
			}
			if (delta_wire_read_voice_config(payload, header.length, &config) &&
				(config.kbps < DELTA_WIRE_VOICE_MINIMUM_KBPS || config.kbps > DELTA_WIRE_VOICE_MAXIMUM_KBPS ||
					(config.channels & ~DELTA_VOICE_KNOWN_CONFIG)))
			{
				__builtin_trap();
			}
		}
		{
			struct delta_wire_badge badge;
			unsigned char nonce[DELTA_WIRE_BADGE_NONCE_SIZE];
			const unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

			delta_wire_read_badge_ask(payload, header.length, nonce);
			if (delta_wire_read_badge(payload, header.length, &badge) &&
				badge.certificate_length > DELTA_WIRE_BADGE_CERTIFICATE_SIZE)
				__builtin_trap();
		}
		{
			static struct delta_wire_unit_vitals vitals;
			static struct delta_wire_object_names names;
			const unsigned char *payload = data + DELTA_WIRE_HEADER_SIZE;

			if (delta_wire_read_unit_vitals(payload, header.length, &vitals) &&
				(vitals.count > DELTA_WIRE_MAXIMUM_UNIT_VITALS ||
					(vitals.count && (vitals.entries[vitals.count - 1].flags & ~DELTA_WIRE_VITALS_FLAGS))))
			{
				__builtin_trap();
			}
			if (delta_wire_read_object_names(payload, header.length, &names) &&
				(names.first + names.count > names.total || names.total > DELTA_WIRE_MAXIMUM_OBJECT_NAMES ||
					names.count > DELTA_WIRE_MAXIMUM_OBJECT_NAMES_ENTRIES ||
					(names.count && ((names.entries[names.count - 1].flags & ~DELTA_WIRE_NAME_FLAGS) ||
						(names.entries[names.count - 1].vitals_flags & ~DELTA_WIRE_VITALS_FLAGS)))))
			{
				__builtin_trap();
			}
		}
		{
			struct delta_wire_map map;

			if (delta_wire_read_map(data + DELTA_WIRE_HEADER_SIZE, header.length, &map) &&
				((map.family && !(map.flags & DELTA_WIRE_MAP_HASHED)) || map.name[DELTA_WIRE_MAP_NAME_SIZE]))
			{
				__builtin_trap();
			}
		}
		{
			struct delta_wire_traits traits;
			unsigned char again[DELTA_WIRE_MAXIMUM_DATAGRAM];

			/* (traits taken are traits that can be written again) */
			if (delta_wire_read_traits(data + DELTA_WIRE_HEADER_SIZE, header.length, &traits) &&
				!delta_wire_write_traits(again, header.session, &traits))
			{
				__builtin_trap();
			}
		}
	}
	/* (a chat line or chat settings entry, as the distributed chat messages
	carry them from OpenCE's machines) */
	{
		struct delta_chat_bridge_line line;

		delta_chat_bridge_read_line(data, (int)size, &line);
		delta_chat_bridge_read_config(data, (int)size);
	}
	/* (a platform key's 8 bytes, of any version) */
	if (size >= 8)
	{
		struct delta_platform_key key;

		delta_wire_read_key(data, &key);
	}
	/* (time moves, so the rate limits refill) */
	now += 50;
	delta_peer_receive(&host, now, CLIENT_IPV4, CLIENT_PORT, data, (int)size);
	/* (the client's session as it is, whatever an earlier input made it) */
	delta_peer_receive(&client, now, HOST_IPV4, DELTA_PEER_PORT, data, (int)size);
	/* (now and then a frame: the relay's sends, its passes) */
	if (size && data[0] % 8 == 0)
	{
		struct delta_peer_game_machine machines[2];
		signed char players[DELTA_PEER_MAXIMUM_PLAYERS];

		memset(machines, 0, sizeof(machines));
		machines[0].local = 1;
		machines[1].machine_index = 1;
		machines[1].ipv4 = CLIENT_IPV4;
		memset(players, -1, sizeof(players));
		players[0] = 0;
		players[1] = 1;
		delta_peer_host_frame(&host, now, machines, 2, players);
		delta_peer_client_frame(&client, now, 1, HOST_IPV4, DELTA_PEER_PORT, 1, 1, players);
	}
	deliver(now);
	return 0;
}
