/*
P2P_SIGNAL_SOCKETS_TEST.C

Counts internet play's connections to the signalling brokers
(port/linux/src/p2p_signal.c, included whole, as p2p_signal_fuzz.c does)
over hours of brokers that misbehave, on a simulated clock and sockets
(tools/test_p2p_signal_sockets.py builds and runs it; #1009). Each broker of
the list behaves one way for the whole run:

- one that answers and stays up;
- one that refuses the connection;
- one whose connection never completes;
- one that refuses the CONNECT (a CONNACK with an error);
- one that takes the connection, then drops it at once.

On top, the doorbell changes and the network is reconnected (as after a
sleep) every so often. Checked all along: at most one socket open per
broker, every socket closed once its broker's done with it; at the end, no
broker connected more often than its retries allow.

    p2p_signal_sockets_test

0 when every check held, 1 with why.
*/

#define p2p_open fuzz_open
#define p2p_equal fuzz_equal

#include "../src/p2p_signal.c"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#undef p2p_open
#undef p2p_equal

enum
{
	_behavior_stays_up,
	_behavior_refuses,
	_behavior_never_completes,
	_behavior_refuses_connect,
	_behavior_drops_at_once,
	NUMBER_OF_BEHAVIORS,

	FIRST_SOCKET = 100,
	MAXIMUM_SOCKETS = 100000,
	TICK = 100,
	HOURS = 3,
};

static const char *behavior_names[NUMBER_OF_BEHAVIORS] =
{
	"stays up", "refuses", "never completes", "refuses the CONNECT", "drops at once",
};

/* the list: two of ours, three others, one way each */
#define TEST_BROKERS \
	"up.chupathingy.org:1000,drops.chupathingy.org:1004,refuses.example.org:1001," \
	"hangs.example.org:1002,refuses-connect.example.org:1003"

struct fake_socket
{
	int open;
	int behavior;
	/* the connect done (writable once) */
	int completed;
	int connected_reported;
	/* what the broker has to say, and whether it hung up */
	unsigned char input[64];
	int input_size;
	int hung_up;
	/* a drop that comes the next tick */
	int drop_pending;
};

static unsigned long clock_now = 5000000;
static struct fake_socket sockets[MAXIMUM_SOCKETS];
static int next_socket = FIRST_SOCKET;
static int connects_by_behavior[NUMBER_OF_BEHAVIORS];
static int last_error;
static int failed;

static void fail(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	fputs("FAIL: ", stdout);
	vprintf(format, arguments);
	fputc('\n', stdout);
	va_end(arguments);
	failed = 1;
}

static struct fake_socket *fake(int socket)
{
	if (socket < FIRST_SOCKET || socket >= next_socket || !sockets[socket].open)
	{
		fail("socket %d used while not open", socket);
		return NULL;
	}
	return &sockets[socket];
}

static int open_sockets(void)
{
	int index, count = 0;

	for (index = FIRST_SOCKET; index < next_socket; index++)
		count += sockets[index].open;
	return count;
}

/* ---------- what p2p_signal.c calls */

unsigned long p2p_now(void)
{
	return clock_now;
}

int fuzz_open(const unsigned char *key, const unsigned char *sealed, int size, unsigned char *plaintext)
{
	(void)key;
	(void)sealed;
	(void)size;
	(void)plaintext;
	return -1;
}

int fuzz_equal(const void *first, const void *second, int size)
{
	return !memcmp(first, second, (size_t)size);
}

void posix_random_bytes(void *buffer, unsigned long size)
{
	memset(buffer, 0x21, size);
}

void platform_log(const char *format, ...)
{
	(void)format;
}

int posix_socket_last_error(void)
{
	return last_error;
}

int posix_socket(int family, int type, int protocol)
{
	(void)family;
	(void)type;
	(void)protocol;
	if (next_socket >= MAXIMUM_SOCKETS)
	{
		fail("ran out of sockets: %d made", next_socket - FIRST_SOCKET);
		last_error = EMFILE;
		return -1;
	}
	memset(&sockets[next_socket], 0, sizeof(sockets[next_socket]));
	sockets[next_socket].open = 1;
	sockets[next_socket].behavior = -1;
	return next_socket++;
}

int posix_socket_close(int socket)
{
	struct fake_socket *entry = fake(socket);

	if (entry)
		entry->open = 0;
	return entry ? 0 : -1;
}

int posix_socket_connect(int socket, const void *address, int address_length)
{
	const struct sockaddr_in *in = address;
	struct fake_socket *entry = fake(socket);
	int behavior;

	(void)address_length;
	if (!entry)
		return -1;
	behavior = ntohs(in->sin_port) - 1000;
	if (behavior < 0 || behavior >= NUMBER_OF_BEHAVIORS)
	{
		fail("a connection to port %d, not a test broker's", ntohs(in->sin_port));
		return -1;
	}
	entry->behavior = behavior;
	connects_by_behavior[behavior]++;
	if (behavior == _behavior_refuses)
	{
		last_error = ECONNREFUSED;
		return -1;
	}
	if (behavior != _behavior_never_completes)
		entry->completed = 1;
	last_error = WSAEINPROGRESS;
	return -1;
}

int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	struct fake_socket *entry = fake(socket);
	const unsigned char *bytes = buffer;

	(void)flags;
	if (!entry)
		return -1;
	if (!entry->completed || entry->hung_up)
	{
		last_error = ENOTCONN;
		return -1;
	}
	if (length > 0 && bytes[0] == 0x10 && entry->input_size == 0)
	{
		/* CONNECT: the broker's CONNACK (MQTT 5's, with no properties) */
		static const unsigned char accepted[] = { 0x20, 0x03, 0x00, 0x00, 0x00 };
		static const unsigned char refused[] = { 0x20, 0x03, 0x00, 0x87, 0x00 };

		memcpy(entry->input, entry->behavior == _behavior_refuses_connect ? refused : accepted, sizeof(accepted));
		entry->input_size = sizeof(accepted);
		if (entry->behavior == _behavior_drops_at_once)
			entry->drop_pending = 1;
	}
	else if (length > 0 && bytes[0] == 0xC0 && entry->input_size + 2 <= (int)sizeof(entry->input))
	{
		/* PINGREQ: PINGRESP */
		entry->input[entry->input_size++] = 0xD0;
		entry->input[entry->input_size++] = 0x00;
	}
	return length;
}

int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	struct fake_socket *entry = fake(socket);
	int size;

	(void)flags;
	if (!entry)
		return -1;
	if (entry->input_size)
	{
		size = entry->input_size < length ? entry->input_size : length;
		memcpy(buffer, entry->input, (size_t)size);
		memmove(entry->input, entry->input + size, (size_t)(entry->input_size - size));
		entry->input_size -= size;
		return size;
	}
	if (entry->hung_up)
		return 0;
	last_error = WSAEWOULDBLOCK;
	return -1;
}

int posix_socket_set_nonblocking(int socket, int nonblocking)
{
	(void)nonblocking;
	return fake(socket) ? 0 : -1;
}

char *config_file_read(const char *path, size_t *size)
{
	char *text = strdup(TEST_BROKERS);

	(void)path;
	if (text)
		*size = strlen(text);
	return text;
}

const char *config_string(const char *name)
{
	(void)name;
	return "brokers.txt";
}

const unsigned char *p2p_identifier(void)
{
	static const unsigned char identifier[P2P_IDENTIFIER_SIZE] = { 9 };

	return identifier;
}

void config_folder(char *path, size_t size)
{
	(void)size;
	path[0] = 0;
}

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[2 * index] = digits[bytes[index] >> 4];
		text[2 * index + 1] = digits[bytes[index] & 15];
	}
	text[2 * size] = 0;
}

void p2p_identifier_for(const unsigned char *public_key, unsigned char *identifier)
{
	memcpy(identifier, public_key, P2P_IDENTIFIER_SIZE);
}

void p2p_identifier_from_hash(const unsigned char *hash, unsigned char *identifier)
{
	memcpy(identifier, hash, P2P_IDENTIFIER_SIZE);
}

void p2p_key_hash(const unsigned char *public_key, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(public_key, P2P_KEY_SIZE, digest);
	memcpy(hash, digest, P2P_KEY_HASH_SIZE);
}

const unsigned char *p2p_public_key(void)
{
	static const unsigned char key[P2P_KEY_SIZE] = { 9 };

	return key;
}

int p2p_shared_secret(const unsigned char *public_key, unsigned char *shared)
{
	memcpy(shared, public_key, P2P_KEY_SIZE);
	return 1;
}

int p2p_local_candidates(struct p2p_candidate *candidates, int maximum_count)
{
	(void)candidates;
	(void)maximum_count;
	return 0;
}

int p2p_peer_offered(const unsigned char *identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count, int is_host)
{
	(void)identifier;
	(void)secret;
	(void)candidates;
	(void)count;
	(void)is_host;
	return 0;
}

int p2p_peer_turned_away(const unsigned char *identifier, int is_host)
{
	(void)identifier;
	(void)is_host;
	return 0;
}

int p2p_peer_reoffered(const unsigned char *identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count)
{
	(void)identifier;
	(void)secret;
	(void)candidates;
	(void)count;
	return 0;
}

unsigned long p2p_resolve(const char *host)
{
	(void)host;
	return 0x0100007F;
}

int p2p_probe_request_heard(const unsigned char *message, int size, unsigned char *answer, unsigned char *reply)
{
	(void)message;
	(void)size;
	(void)answer;
	(void)reply;
	return 0;
}

int p2p_probe_asking_tokens(unsigned char (*tokens)[P2P_TOKEN_SIZE], int maximum)
{
	(void)tokens;
	(void)maximum;
	return 0;
}

void p2p_probe_answer_heard(const unsigned char *token, const unsigned char *message, int size)
{
	(void)token;
	(void)message;
	(void)size;
}

void p2p_lobby_slot_heard(const char *hash_text, const unsigned char *payload, int size, int retained)
{
	(void)hash_text;
	(void)payload;
	(void)size;
	(void)retained;
}

void p2p_lobby_query_heard(void)
{
}

void p2p_lobby_slot_topic(const unsigned char *key_hash, char *topic, int size)
{
	(void)key_hash;
	snprintf(topic, (size_t)size, "lobby");
}

/* ---------- the run */

/* one pass of p2p.c's thread: the sockets signalling asks about, those of
them ready, then its update */
static void pass(void)
{
	int read[64], write[64], ready_read[64], ready_write[64];
	int read_count = 0, write_count = 0, ready_read_count = 0, ready_write_count = 0;
	int index;

	for (index = FIRST_SOCKET; index < next_socket; index++)
	{
		struct fake_socket *entry = &sockets[index];

		/* (a drop the broker makes right after its CONNACK was read) */
		if (entry->open && entry->drop_pending && !entry->input_size)
		{
			entry->drop_pending = 0;
			entry->hung_up = 1;
		}
	}
	p2p_signal_select_sets(read, &read_count, write, &write_count, 64);
	for (index = 0; index < read_count; index++)
	{
		struct fake_socket *entry = fake(read[index]);

		if (entry && (entry->input_size || entry->hung_up))
			ready_read[ready_read_count++] = read[index];
	}
	for (index = 0; index < write_count; index++)
	{
		struct fake_socket *entry = fake(write[index]);

		if (entry && entry->completed)
			ready_write[ready_write_count++] = write[index];
	}
	p2p_signal_update(ready_read, ready_read_count, ready_write, ready_write_count);
}

static void check_open(void)
{
	int index, count = open_sockets();

	if (count > signalling.broker_count)
		fail("%d sockets open for %d brokers", count, signalling.broker_count);
	for (index = 0; index < signalling.broker_count; index++)
	{
		const struct broker *broker = &signalling.brokers[index];

		if (broker->socket >= 0 && !sockets[broker->socket].open)
			fail("%s holds socket %d, closed", broker->host, broker->socket);
	}
	for (index = FIRST_SOCKET; index < next_socket; index++)
	{
		int owner, owned = 0;

		if (!sockets[index].open)
			continue;
		for (owner = 0; owner < signalling.broker_count; owner++)
			owned |= signalling.brokers[owner].socket == index;
		if (!owned)
			fail("socket %d is open and no broker's (a leak)", index);
	}
}

int main(void)
{
	static const char *doorbells[] =
	{
		"0123456789abcdef0123456789abcdef", "fedcba9876543210fedcba9876543210", "",
	};
	unsigned long ticks, end = (unsigned long)HOURS * 3600 * 1000 / TICK;
	int behavior;
	/* the most connections each way of failing may make in the run: a
	retry at most every RETRY_INTERVAL until its failures add up, then one
	every MAXIMUM_RETRY_INTERVAL, plus one for each reconnect asked */
	int reconnects = 0, doorbell_changes = 0;

	p2p_signal_doorbell(doorbells[0]);
	p2p_signal_start();
	if (signalling.broker_count != NUMBER_OF_BEHAVIORS)
		fail("%d brokers read, not %d", signalling.broker_count, NUMBER_OF_BEHAVIORS);
	for (ticks = 0; ticks < end && !failed; ticks++)
	{
		clock_now += TICK;
		pass();
		check_open();
		/* the doorbell changes every 20 minutes; the network comes back
		from a sleep every 45 */
		if (ticks && ticks % (20 * 60 * 1000 / TICK) == 0)
		{
			p2p_signal_doorbell(doorbells[++doorbell_changes % 3]);
			check_open();
		}
		if (ticks && ticks % (45 * 60 * 1000 / TICK) == 0)
		{
			p2p_signal_reconnect();
			reconnects++;
			check_open();
		}
	}
	if (!failed)
	{
		/* the one that stays up: once, again after each reconnect and
		doorbell change */
		int limit_up = 1 + reconnects + doorbell_changes;
		/* the others: backing off to MAXIMUM_RETRY_INTERVAL; a reconnect
		starts the count again */
		int limit_failing = (int)((unsigned long)HOURS * 3600 * 1000 / MAXIMUM_RETRY_INTERVAL) +
			(reconnects + doorbell_changes + 1) * 6;

		for (behavior = 0; behavior < NUMBER_OF_BEHAVIORS; behavior++)
		{
			int limit = behavior == _behavior_stays_up ? limit_up : limit_failing;

			printf("%-20s %4d connections (at most %d)\n", behavior_names[behavior],
				connects_by_behavior[behavior], limit);
			if (connects_by_behavior[behavior] > limit)
				fail("%s: %d connections in %d hours, more than %d", behavior_names[behavior],
					connects_by_behavior[behavior], HOURS, limit);
			if (!connects_by_behavior[behavior])
				fail("%s: never connected", behavior_names[behavior]);
		}
		if (signalling.brokers[0].state != _broker_ready)
			fail("the broker that stays up is not connected at the end");
	}
	printf("%d sockets made, %d open at the end\n", next_socket - FIRST_SOCKET, open_sockets());
	if (!failed)
		printf("ok\n");
	return failed;
}
