/*
P2P_JOIN_PASSWORD_TEST.C

Tests of a password game's invites (port/linux/src/p2p_crypto.c, as p2p.c
and p2p_lobby.c use them): the host's link holds a door, never the token it
listens with; the token comes back only with the password, for this host;
a wrong or empty password opens nothing; older versions take the link for
no invite; and the server browser's sealed token opens with the same
password to the same token. Built by tools/test_p2p_join_password.py.
*/

#include "p2p_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- stand-ins */

pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned char random_next = 1;

void posix_random_bytes(void *buffer, unsigned long size)
{
	unsigned char *bytes = buffer;
	unsigned long index;

	for (index = 0; index < size; index++)
		bytes[index] = random_next++;
}

void platform_log(const char *format, ...)
{
	(void)format;
}

/* ---------- checks */

static int failures;

static void check(int ok, const char *what)
{
	if (!ok)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void hex(const unsigned char *bytes, int size, char *text)
{
	int index;

	for (index = 0; index < size; index++)
		sprintf(text + 2 * index, "%02x", bytes[index]);
}

/* a host of the hash (the key it runs with), with a password: its invite's
link and the token it listens with, as p2p.c's make_invite makes them */
static void host_invite(const unsigned char *key_hash, const char *password, char *link, unsigned char *token)
{
	unsigned char key[P2P_PASSWORD_KEY_SIZE];
	unsigned char door[P2P_TOKEN_SIZE];
	unsigned char check_bytes[P2P_INVITE_CHECK_SIZE];

	p2p_join_password_key(password, key_hash, key);
	posix_random_bytes(door, sizeof(door));
	p2p_locked_invite_token(key, door, token, check_bytes);
	p2p_locked_invite_text(key_hash, door, check_bytes, link);
}

/* what an older version reads of a link: 64 digits right after
halo://join/, or nothing */
static int older_version_reads(const char *link)
{
	const char *start = strstr(link, "halo://join/");
	int digits = 0;

	if (!start)
		return 0;
	start += 12;
	while ((start[digits] >= '0' && start[digits] <= '9') || (start[digits] >= 'a' && start[digits] <= 'f'))
		digits++;
	return digits == 2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE);
}

int main(void)
{
	unsigned char key_hash[P2P_KEY_HASH_SIZE], other_hash[P2P_KEY_HASH_SIZE];
	unsigned char token[P2P_TOKEN_SIZE], opened[P2P_TOKEN_SIZE], other_token[P2P_TOKEN_SIZE];
	unsigned char hash[P2P_KEY_HASH_SIZE], door[P2P_TOKEN_SIZE], check_bytes[P2P_INVITE_CHECK_SIZE];
	char link[P2P_LOCKED_LINK_SIZE], other_link[P2P_LOCKED_LINK_SIZE];
	char token_text[2 * P2P_TOKEN_SIZE + 1];
	char text[256];

	memset(key_hash, 0x11, sizeof(key_hash));
	memset(other_hash, 0x22, sizeof(other_hash));
	host_invite(key_hash, "hunter2", link, token);

	/* the host's link */
	check(strlen(link) == P2P_LOCKED_LINK_SIZE - 1, "a locked link is its full length");
	check(!strncmp(link, "halo://join/p", 13), "a locked link is marked");
	hex(token, P2P_TOKEN_SIZE, token_text);
	token_text[2 * P2P_TOKEN_SIZE] = 0;
	check(!strstr(link, token_text), "a locked link does not hold the token the host listens with");
	check(!older_version_reads(link), "an older version reads a locked link as no invite");

	/* a joiner with the link */
	check(p2p_parse_invite(link, hash, door, check_bytes) == 2, "a locked link parses as locked");
	check(!memcmp(hash, key_hash, sizeof(hash)), "a locked link holds its host's key hash");
	check(memcmp(door, token, sizeof(door)) != 0, "a locked link's door is not the token");
	memset(opened, 0, sizeof(opened));
	check(!p2p_locked_invite_open("hunter3", hash, door, check_bytes, opened), "a wrong password opens nothing");
	check(!p2p_locked_invite_open("", hash, door, check_bytes, opened), "no password opens nothing");
	check(!p2p_locked_invite_open(NULL, hash, door, check_bytes, opened), "a missing password opens nothing");
	check(!p2p_locked_invite_open("Hunter2", hash, door, check_bytes, opened), "a password's case counts");
	{
		static const unsigned char zero[P2P_TOKEN_SIZE];

		check(!memcmp(opened, zero, sizeof(zero)), "a wrong password leaves no token");
	}
	check(p2p_locked_invite_open("hunter2", hash, door, check_bytes, opened) && !memcmp(opened, token, sizeof(token)),
		"the password opens the token the host listens with");
	/* (another host's door, or this door said to be another host's, is not
	opened by the same password) */
	check(!p2p_locked_invite_open("hunter2", other_hash, door, check_bytes, opened) ||
		memcmp(opened, token, sizeof(token)) != 0, "a door opens only for its own host");
	host_invite(other_hash, "hunter2", other_link, other_token);
	check(memcmp(other_token, token, sizeof(token)) != 0, "the same password gives another host another token");
	/* a link edited to another check opens nothing */
	check_bytes[0] ^= 1;
	check(!p2p_locked_invite_open("hunter2", hash, door, check_bytes, opened), "an altered check opens nothing");

	/* the same link, as a bare code, in other words, or in capitals */
	check(p2p_parse_invite(link + 12, hash, door, check_bytes) == 2, "a bare locked code parses");
	snprintf(text, sizeof(text), "join me: %s tonight", link);
	check(p2p_parse_invite(text, hash, door, check_bytes) == 2, "a locked link in other words parses");
	{
		char upper[P2P_LOCKED_LINK_SIZE];
		int index;

		for (index = 0; link[index]; index++)
			upper[index] = (char)(link[index] >= 'a' && link[index] <= 'z' ? link[index] - 32 : link[index]);
		upper[index] = 0;
		check(p2p_parse_invite(upper, hash, door, check_bytes) == 2 && !memcmp(hash, key_hash, sizeof(hash)),
			"a locked link in capitals parses");
	}
	/* a locked link cut short is no invite */
	snprintf(text, sizeof(text), "%.*s", (int)strlen(link) - 2, link);
	check(p2p_parse_invite(text, hash, door, check_bytes) == 0, "a locked link cut short is no invite");

	/* an open game's links, as before */
	{
		unsigned char bytes[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
		char code[2 * sizeof(bytes) + 1];

		memcpy(bytes, key_hash, P2P_KEY_HASH_SIZE);
		memcpy(bytes + P2P_KEY_HASH_SIZE, token, P2P_TOKEN_SIZE);
		hex(bytes, sizeof(bytes), code);
		code[2 * sizeof(bytes)] = 0;
		snprintf(text, sizeof(text), "halo://join/%s", code);
		check(p2p_parse_invite(text, hash, door, NULL) == 1 && !memcmp(door, token, sizeof(token)),
			"an open game's link parses as before");
		check(p2p_parse_invite(code, hash, door, NULL) == 1, "an open game's bare code parses as before");
		code[2 * (P2P_IDENTIFIER_SIZE + P2P_TOKEN_SIZE)] = 0;
		snprintf(text, sizeof(text), "halo://join/%s", code);
		check(p2p_parse_invite(text, hash, door, NULL) == -1, "an older version's link is told apart");
		check(p2p_parse_invite("hello there", hash, door, NULL) == 0, "words are no invite");
		check(p2p_parse_invite("play", hash, door, NULL) == 0, "a word beginning with p is no invite");
	}

	/* the server browser's listing (OpenCE's browsers too): its sealed token
	opens with the same password to the same token */
	{
		unsigned char signing_key[P2P_KEY_SIZE];
		unsigned char listing_key[P2P_PASSWORD_KEY_SIZE];
		unsigned char sealed[P2P_SEALED_TOKEN_SIZE];
		unsigned char unsealed[P2P_TOKEN_SIZE];

		memset(signing_key, 0x33, sizeof(signing_key));
		p2p_password_key("hunter2", signing_key, listing_key);
		p2p_seal_token(listing_key, signing_key, token, sealed);
		p2p_password_key("hunter3", signing_key, listing_key);
		check(!p2p_unseal_token(listing_key, signing_key, sealed, unsealed), "a wrong password opens no listing");
		p2p_password_key("hunter2", signing_key, listing_key);
		check(p2p_unseal_token(listing_key, signing_key, sealed, unsealed) && !memcmp(unsealed, token, sizeof(token)),
			"the listing opens to the token the locked link opens to");
	}

	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
