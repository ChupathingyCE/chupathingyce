/*
P2P_DOORBELL_TEST.C

Tests of Delta's doorbell in internet play's signalling
(port/linux/src/p2p_signal.c, included whole, as p2p_signal_fuzz.c does),
over real sockets to a local mosquitto set up as our brokers are
(tools/test_doorbell.py starts it). Every broker name resolves to this
machine; "ours" are those under .chupathingy.org.

    p2p_doorbell_test BROKERS_FILE DOORBELL OURS_PORT OTHER_PORT

With only the doorbell wanted, it connects to our broker alone, names the
doorbell as its user name there, subscribes, prints "listening", and exits
0 once a ring came ("ring"); then it starts the rest of signalling and
checks the other broker is connected without a user name. 1 on a failure,
with why.
*/

#define p2p_open fuzz_open
#define p2p_equal fuzz_equal

#include "../src/p2p_signal.c"

#include <stdint.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#undef p2p_open
#undef p2p_equal


unsigned long p2p_now(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return (unsigned long)now.tv_sec * 1000 + (unsigned long)now.tv_usec / 1000;
}

int fuzz_open(const unsigned char *key, const unsigned char *sealed, int size, unsigned char *plaintext)
{
	(void)key;
	if (size < P2P_SEAL_OVERHEAD)
		return -1;
	memcpy(plaintext, sealed + P2P_NONCE_SIZE, (size_t)(size - P2P_SEAL_OVERHEAD));
	return size - P2P_SEAL_OVERHEAD;
}

int fuzz_equal(const void *first, const void *second, int size)
{
	(void)first;
	(void)second;
	(void)size;
	return 1;
}

void posix_random_bytes(void *buffer, unsigned long size)
{
	memset(buffer, 0x21, size);
}

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	fputs("log: ", stderr);
	vfprintf(stderr, format, arguments);
	fputc('\n', stderr);
	va_end(arguments);
}

int posix_socket_last_error(void)
{
	return errno == EINPROGRESS ? WSAEINPROGRESS : errno == EWOULDBLOCK || errno == EAGAIN ? WSAEWOULDBLOCK : errno;
}

/* each socket's port, and the CONNECT it sent first (the tests read the
user name in it) */
static unsigned short socket_ports[1024];
static unsigned char socket_connects[1024][256];
static int socket_connect_sizes[1024];
static int socket_count_by_port[65536];

int posix_socket(int family, int type, int protocol)
{
	int descriptor = socket(family, type, protocol);

	if (descriptor >= 0 && descriptor < 1024)
	{
		socket_ports[descriptor] = 0;
		socket_connect_sizes[descriptor] = 0;
	}
	return descriptor;
}

int posix_socket_close(int socket)
{
	return close(socket);
}

int posix_socket_connect(int socket, const void *address, int address_length)
{
	const struct sockaddr_in *in = address;

	if (socket < 1024)
	{
		socket_ports[socket] = ntohs(in->sin_port);
		socket_count_by_port[socket_ports[socket]]++;
	}
	return connect(socket, address, (socklen_t)address_length);
}

int posix_socket_send(int socket, const void *buffer, int length, int flags)
{
	if (socket < 1024 && !socket_connect_sizes[socket] && length > 0 && ((const unsigned char *)buffer)[0] == 0x10)
	{
		int size = length < 256 ? length : 256;

		memcpy(socket_connects[socket], buffer, (size_t)size);
		socket_connect_sizes[socket] = size;
	}
	return (int)send(socket, buffer, (size_t)length, flags);
}

int posix_socket_recv(int socket, void *buffer, int length, int flags)
{
	return (int)recv(socket, buffer, (size_t)length, flags);
}

int posix_socket_set_nonblocking(int socket, int nonblocking)
{
	int flags = fcntl(socket, F_GETFL, 0);

	return fcntl(socket, F_SETFL, nonblocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}

char *config_file_read(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *text;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	text = malloc((size_t)length + 1);
	if (text && fread(text, 1, (size_t)length, file) != (size_t)length)
	{
		free(text);
		text = NULL;
	}
	fclose(file);
	if (text)
	{
		text[length] = 0;
		if (size)
			*size = (size_t)length;
	}
	return text;
}

static const char *brokers_file = "";

const char *config_string(const char *name)
{
	return !strcmp(name, "network.brokers_file") ? brokers_file : "";
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
	return public_key[0] != 0xFF;
}

int p2p_local_candidates(struct p2p_candidate *candidates, int maximum_count)
{
	int index;

	for (index = 0; index < 2 && index < maximum_count; index++)
	{
		candidates[index].address = 0x0100007F + (unsigned long)index;
		candidates[index].port = 0x1234;
	}
	return index;
}

int p2p_peer_offered(const unsigned char *identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count, int is_host)
{
	(void)identifier;
	(void)secret;
	(void)candidates;
	(void)count;
	return is_host != 2;
}

int p2p_peer_turned_away(const unsigned char *identifier, int is_host)
{
	(void)is_host;
	return identifier[1] == 0xEE;
}

int p2p_peer_reoffered(const unsigned char *identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count)
{
	(void)identifier;
	(void)secret;
	(void)candidates;
	return count >= 0;
}

/* every broker's name is this machine (the test's mosquitto) */
unsigned long p2p_resolve(const char *host)
{
	(void)host;
	return htonl(0x7F000001);
}

int p2p_probe_request_heard(const unsigned char *message, int size, unsigned char *answer, unsigned char *reply)
{
	unsigned char id[P2P_PROBE_ID_SIZE], key[P2P_PROBE_KEY_SIZE];
	struct p2p_probe_address addresses[P2P_PROBE_MAXIMUM_ADDRESSES];
	int count;

	if (!p2p_probe_get_request(message, size, id, reply, addresses, &count))
		return 0;
	memset(key, 0x77, sizeof(key));
	memset(answer, 0, P2P_PROBE_MAXIMUM_MESSAGE);
	return p2p_probe_put_answer(answer, id, key, addresses, count);
}

int p2p_probe_asking_tokens(unsigned char (*tokens)[P2P_TOKEN_SIZE], int maximum)
{
	int index;

	for (index = 0; index < 2 && index < maximum; index++)
		memset(tokens[index], index, P2P_TOKEN_SIZE);
	return index;
}

void p2p_probe_answer_heard(const unsigned char *token, const unsigned char *message, int size)
{
	unsigned char id[P2P_PROBE_ID_SIZE], key[P2P_PROBE_KEY_SIZE];
	struct p2p_probe_address addresses[P2P_PROBE_MAXIMUM_ADDRESSES];
	int count, signed_size;

	(void)token;
	p2p_probe_get_answer(message, size, id, key, addresses, &count, &signed_size);
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


static int fail(const char *why)
{
	printf("FAIL %s\n", why);
	return 1;
}

static void step(int wait)
{
	int read[16], write[16], read_count = 0, write_count = 0, index, highest = -1;
	fd_set reads, writes;
	struct timeval timeout;

	p2p_signal_select_sets(read, &read_count, write, &write_count, 16);
	FD_ZERO(&reads);
	FD_ZERO(&writes);
	for (index = 0; index < read_count; index++)
	{
		FD_SET(read[index], &reads);
		highest = read[index] > highest ? read[index] : highest;
	}
	for (index = 0; index < write_count; index++)
	{
		FD_SET(write[index], &writes);
		highest = write[index] > highest ? write[index] : highest;
	}
	timeout.tv_sec = 0;
	timeout.tv_usec = wait * 1000;
	select(highest + 1, &reads, &writes, NULL, &timeout);
	read_count = write_count = 0;
	for (index = 0; index <= highest; index++)
	{
		if (FD_ISSET(index, &reads))
			read[read_count++] = index;
		if (FD_ISSET(index, &writes))
			write[write_count++] = index;
	}
	p2p_signal_update(read, read_count, write, write_count);
}

static int contains(const unsigned char *data, int size, const char *text)
{
	int length = (int)strlen(text), at;

	for (at = 0; at + length <= size; at++)
	{
		if (!memcmp(data + at, text, (size_t)length))
			return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	unsigned long start;
	int ours, other, index, listening = 0, rings = 0;

	if (argc != 5)
		return fail("usage: p2p_doorbell_test BROKERS_FILE DOORBELL OURS_PORT OTHER_PORT");
	setvbuf(stdout, NULL, _IONBF, 0);
	brokers_file = argv[1];
	ours = atoi(argv[3]);
	other = atoi(argv[4]);
	/* (not a doorbell: nothing starts) */
	p2p_signal_doorbell("doorbell/#");
	if (signalling.started)
		return fail("a bad doorbell started signalling");
	p2p_signal_doorbell(argv[2]);
	if (!signalling.started || !signalling.doorbell_only)
		return fail("the doorbell did not start signalling with our brokers alone");
	start = p2p_now();
	while (p2p_now() - start < 15000 && !rings)
	{
		step(100);
		if (!listening)
		{
			for (index = 0; index < signalling.broker_count; index++)
			{
				struct broker *broker = &signalling.brokers[index];

				if (broker->ours && broker->state == _broker_ready && broker->topics[_topic_doorbell][0])
				{
					listening = 1;
					printf("listening\n");
				}
			}
		}
		rings += p2p_signal_doorbell_rings();
	}
	if (!rings)
		return fail("no ring");
	printf("ring\n");
	if (socket_count_by_port[other])
		return fail("the doorbell alone connected to a broker that isn't ours");
	/* (our broker's CONNECT named the doorbell as its user name) */
	for (index = 0; index < 1024; index++)
	{
		if (socket_ports[index] == ours && socket_connect_sizes[index] &&
			!contains(socket_connects[index], socket_connect_sizes[index], argv[2]))
			return fail("our broker's CONNECT has no user name");
	}
	/* (the rest of signalling: the other broker too, with no user name) */
	p2p_signal_start();
	start = p2p_now();
	while (p2p_now() - start < 5000)
	{
		int ready = 0;

		step(100);
		for (index = 0; index < signalling.broker_count; index++)
			ready += signalling.brokers[index].state == _broker_ready;
		if (ready == signalling.broker_count)
			break;
	}
	for (index = 0; index < signalling.broker_count; index++)
	{
		if (signalling.brokers[index].state != _broker_ready)
		{
			fprintf(stderr, "%s: state %d, failures %d\n", signalling.brokers[index].host,
				signalling.brokers[index].state, signalling.brokers[index].failures);
			return fail("a broker is not connected");
		}
	}
	for (index = 0; index < 1024; index++)
	{
		if (socket_ports[index] == other && socket_connect_sizes[index] &&
			contains(socket_connects[index], socket_connect_sizes[index], argv[2]))
			return fail("a broker that isn't ours was told the doorbell");
	}
	/* (no doorbell: the subscription dropped, the connection kept) */
	p2p_signal_doorbell("");
	for (index = 0; index < signalling.broker_count; index++)
	{
		if (signalling.brokers[index].topics[_topic_doorbell][0])
			return fail("the doorbell's topic stayed");
	}
	printf("ok\n");
	return 0;
}
