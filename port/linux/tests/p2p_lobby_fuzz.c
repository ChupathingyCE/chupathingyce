/*
P2P_LOBBY_FUZZ.C

libFuzzer's entry for the public games' listings a browser hears from the
brokers (port/linux/src/p2p_lobby.c, included whole to reach its readers;
tools/test_host_fuzz.py builds it). A listing is checked for its signature
before it is taken, which no input can make: the harness takes any
signature for good, so that the parsing and the table of games behind it
are reached. The first byte of an input says what the rest is: bit 0, a
retained copy; bit 1, the slot is the one the listing's key names (else
one the input's last 16 bytes name); bit 2, the listing's time is now;
bit 3, the slot is this machine's own.
*/

/* (the signature check: any signature is good) */
#define p2p_ed25519_verify fuzz_ed25519_verify
static int fuzz_ed25519_verify(const unsigned char *public_key, const void *message, int size,
	const unsigned char *signature);

#include "../src/p2p_lobby.c"

#include <stdint.h>

static int fuzz_ed25519_verify(const unsigned char *public_key, const void *message, int size,
	const unsigned char *signature)
{
	(void)public_key;
	(void)message;
	(void)size;
	(void)signature;
	return 1;
}

pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned long clock_now = 1000000;

unsigned long p2p_now(void)
{
	return clock_now;
}

void posix_random_bytes(void *buffer, unsigned long size)
{
	memset(buffer, 0x33, size);
}

void platform_log(const char *format, ...)
{
	(void)format;
}

int config_boolean(const char *name)
{
	(void)name;
	return 1;
}

int delta_legacy_minimum(void)
{
	return 29;
}

int delta_legacy_maximum(void)
{
	return 41;
}

int delta_legacy_announce(void)
{
	return 41;
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

const unsigned char *p2p_signing_key(void)
{
	static const unsigned char key[P2P_KEY_SIZE] = { 8 };

	return key;
}

void p2p_sign(const void *message, int size, unsigned char *signature)
{
	(void)message;
	(void)size;
	memset(signature, 0, P2P_SIGNATURE_SIZE);
}

void p2p_new_invite_if_listed(void)
{
}

void p2p_new_invite_for_password(const unsigned char *join_key)
{
	(void)join_key;
}

void p2p_signal_lobby_topics(int listed, int browsing)
{
	(void)listed;
	(void)browsing;
}

void p2p_signal_lobby_publish(const unsigned char *listing, int size, int closing)
{
	(void)listing;
	(void)size;
	(void)closing;
}

void p2p_signal_lobby_query(void)
{
}

void p2p_signal_lobby_quit(void)
{
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct p2p_listing games[MAXIMUM_GAMES];
	unsigned char *payload;
	unsigned char hash[P2P_KEY_HASH_SIZE];
	char text[2 * P2P_KEY_HASH_SIZE + 1];
	int flags, rounds;
	size_t payload_size;

	if (size < 2)
		return 0;
	flags = data[0];
	payload_size = size - 1;
	/* (a fresh table of games each time) */
	memset(&lobby, 0, sizeof(lobby));
	clock_now = 1000000;
	pthread_mutex_lock(&p2p_lock);
	lobby.browsing = 1;
	pthread_mutex_unlock(&p2p_lock);

	for (rounds = 0; rounds < 3; rounds++)
	{
		payload = malloc(payload_size);
		if (!payload)
			return 0;
		memcpy(payload, data + 1, payload_size);
		/* (each round a later sequence of the same listing) */
		if (payload_size > 9)
			payload[9] = (unsigned char)(payload[9] + rounds);
		if ((flags & 4) && payload_size > 14)
		{
			unsigned long now = (unsigned long)time(NULL);

			put_long(payload + 10, now);
		}
		if (flags & 8)
			p2p_key_hash(p2p_public_key(), hash);
		else if ((flags & 2) && payload_size >= 14 + P2P_KEY_SIZE)
		{
			unsigned char x25519[P2P_KEY_SIZE];

			if (p2p_ed25519_to_x25519(payload + 14, x25519))
				p2p_key_hash(x25519, hash);
			else
				memset(hash, 1, sizeof(hash));
		}
		else
		{
			memset(hash, 0, sizeof(hash));
			memcpy(hash, payload + payload_size - (payload_size < 16 ? payload_size : 16),
				payload_size < 16 ? payload_size : 16);
		}
		p2p_hex(hash, P2P_KEY_HASH_SIZE, text);
		pthread_mutex_lock(&p2p_lock);
		p2p_lobby_slot_heard(text, payload, (int)payload_size, flags & 1);
		p2p_lobby_update(NULL, 0, 0);
		pthread_mutex_unlock(&p2p_lock);
		p2p_lobby_games(games, MAXIMUM_GAMES);
		clock_now += 1000 * (unsigned long)(rounds + 1) * 40;
		free(payload);
	}
	return 0;
}
