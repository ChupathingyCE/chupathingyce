/*
SERVER_BADGE_TEST.C

Server badges' certificates and proofs (port/linux/src/server_badge.c;
docs/server-badges.md), against the shared test certificate that command's
tools/test_server_badge.py makes and puma's test_server_badges.py reads.
Built by tools/test_server_badge.py twice: with the test badge key
(HALO_BADGE_TEST_KEY) and with this repository's own keys (no
SERVER_BADGE_TEST_SIGNED: a build without keys takes nothing).
*/

#include "server_badge.h"

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* (the shared test certificate: developer, "Milenko", for the test server
key, issued 1790000000 and good until 1821536000) */
static const char certificate_text[] =
	"4342444701020700803bb16a006f926c6f6421c59884c5326d6481d5d812d4e1398e8d1ed9384ce79d4ec9e286d529594d696c656e6b6f"
	"24a6259f21586863f10db74522f7ce7668dc4a00d64ea92c5d961319a743df0a82a408c079b1b7e59e78459c3666b100e2d44900a2ca44"
	"a0cea847df12974403";
enum { ISSUED = 1790000000u, EXPIRES = 1821536000u, DAY = 24 * 60 * 60 };
/* (SHA-256 of "chupathingy test server key": the test server's link key's seed) */
static const unsigned char server_seed[32] = {
	0x1a, 0xb5, 0xca, 0x5c, 0x92, 0x61, 0x2f, 0xd9, 0xb3, 0x76, 0x75, 0xb3, 0x62, 0xf9, 0xcb, 0x42,
	0xbb, 0x46, 0x0a, 0x83, 0x98, 0xd7, 0xf2, 0xef, 0x1b, 0x28, 0x01, 0x3f, 0x01, 0xe8, 0xf9, 0x35,
};

static void test_parse(const unsigned char *certificate, int size)
{
	struct server_badge badge;
	unsigned char copy[SERVER_BADGE_MAXIMUM_SIZE];

	CHECK(size == SERVER_BADGE_MINIMUM_SIZE + 7);
	CHECK(server_badge_parse(certificate, size, &badge));
	CHECK(badge.tier == _server_badge_tier_developer && !strcmp(badge.label, "Milenko"));
	CHECK(badge.issued == ISSUED && badge.expires == EXPIRES);
	CHECK(!strcmp(server_badge_tier_name(badge.tier), "developer"));
	CHECK(server_badge_tier_number("official") == _server_badge_tier_official &&
		server_badge_tier_number("community") == _server_badge_tier_community &&
		server_badge_tier_number("") == _server_badge_tier_none && server_badge_tier_number("admin") == 0);
	/* (malformed: cut short, another magic or format, a label that does not
	fit, a control character, expiry before issue) */
	CHECK(!server_badge_parse(certificate, size - 1, &badge));
	memcpy(copy, certificate, (size_t)size);
	copy[0] = 'X';
	CHECK(!server_badge_parse(copy, size, &badge));
	memcpy(copy, certificate, (size_t)size);
	copy[4] = 2;
	CHECK(!server_badge_parse(copy, size, &badge));
	memcpy(copy, certificate, (size_t)size);
	copy[6] = 8;
	CHECK(!server_badge_parse(copy, size, &badge));
	memcpy(copy, certificate, (size_t)size);
	copy[SERVER_BADGE_HEADER_SIZE] = 0x07;
	CHECK(!server_badge_parse(copy, size, &badge));
	memcpy(copy, certificate, (size_t)size);
	memcpy(copy + 12, copy + 8, 4);
	CHECK(!server_badge_parse(copy, size, &badge));
	/* (a tier this build does not know: read, and no badge) */
	memcpy(copy, certificate, (size_t)size);
	copy[5] = 9;
	CHECK(server_badge_parse(copy, size, &badge) && badge.tier == 9 && !server_badge_tier_name(9)[0]);
	CHECK(!server_badge_check(copy, size, ISSUED + 1, &badge));
}

static void test_text(void)
{
	unsigned char certificate[SERVER_BADGE_MAXIMUM_SIZE];
	char text[512];

	snprintf(text, sizeof(text), "# Chupathingy server badge\n\n  %s  \r\n", certificate_text);
	CHECK(server_badge_from_text(text, certificate, (int)sizeof(certificate)) == SERVER_BADGE_MINIMUM_SIZE + 7);
	CHECK(server_badge_from_text("4342zz\n", certificate, (int)sizeof(certificate)) == 0);
	CHECK(server_badge_from_text("# only a comment\n", certificate, (int)sizeof(certificate)) == 0);
	CHECK(server_badge_from_text("", certificate, (int)sizeof(certificate)) == 0);
}

static void test_signed(const unsigned char *certificate, int size)
{
	struct server_badge badge;
	unsigned char copy[SERVER_BADGE_MAXIMUM_SIZE];

#ifdef SERVER_BADGE_TEST_SIGNED
	CHECK(server_badge_has_keys());
	CHECK(server_badge_check(certificate, size, ISSUED + 1, &badge) && badge.tier == _server_badge_tier_developer);
	CHECK(server_badge_check(certificate, size, 0, &badge));
	/* (a day's leeway for a clock behind; expired at its expiry) */
	CHECK(server_badge_check(certificate, size, ISSUED - DAY + 60, &badge));
	CHECK(!server_badge_check(certificate, size, ISSUED - DAY - 60, &badge));
	CHECK(!server_badge_check(certificate, size, EXPIRES, &badge));
	/* (why, for the server panel) */
	CHECK(server_badge_verdict(certificate, size, ISSUED + 1, &badge) == _server_badge_good);
	CHECK(server_badge_verdict(certificate, size, EXPIRES, &badge) == _server_badge_expired);
	CHECK(server_badge_verdict(certificate, size, ISSUED - DAY - 60, &badge) == _server_badge_not_yet);
	CHECK(server_badge_verdict(certificate, 20, ISSUED + 1, &badge) == _server_badge_malformed);
	/* (a changed label, or signature: not the badge key's) */
	memcpy(copy, certificate, (size_t)size);
	copy[SERVER_BADGE_HEADER_SIZE] ^= 0x01;
	CHECK(!server_badge_check(copy, size, ISSUED + 1, &badge));
	memcpy(copy, certificate, (size_t)size);
	copy[size - 1] ^= 0x01;
	CHECK(!server_badge_check(copy, size, ISSUED + 1, &badge));
	CHECK(server_badge_verdict(copy, size, ISSUED + 1, &badge) == _server_badge_not_signed);
#else
	/* (no keys: nothing taken) */
	CHECK(!server_badge_has_keys());
	CHECK(!server_badge_check(certificate, size, ISSUED + 1, &badge));
	CHECK(server_badge_verdict(certificate, size, ISSUED + 1, &badge) == _server_badge_not_signed);
	(void)copy;
#endif
}

static void test_proof(const unsigned char *certificate, int size)
{
	unsigned char secret[64], public_key[32], seed[32], signature[64];
	unsigned char nonce[SERVER_BADGE_NONCE_SIZE], message[SERVER_BADGE_MESSAGE_SIZE];
	static const char announce_expected[] = "chupathingy server badge announce v1\n"
		"abababababababababababababababababababababababababababababababab\n1790000000\n";
	char invite[65];
	struct server_badge badge;
	int length, index;

	CHECK(server_badge_parse(certificate, size, &badge));
	memcpy(seed, server_seed, sizeof(seed));
	crypto_ed25519_key_pair(secret, public_key, seed);
	CHECK(!memcmp(public_key, badge.server_key, sizeof(public_key)));

	/* the proof to a joining game: the key's signature of its nonce, the
	invite it joined and the session */
	for (index = 0; index < SERVER_BADGE_NONCE_SIZE; index++)
		nonce[index] = (unsigned char)(index * 3);
	memset(invite, 0, sizeof(invite));
	for (index = 0; index < 64; index++)
		invite[index] = index % 2 ? 'b' : 'a';
	length = server_badge_proof_message(message, nonce, invite, 0x0102abcdu);
	CHECK(length == (int)strlen("chupathingy server badge proof v2\n") + SERVER_BADGE_NONCE_SIZE + 64 + 1 + 8 + 1);
	CHECK(!memcmp(message, "chupathingy server badge proof v2\n", 34));
	CHECK(!memcmp(message + 34 + SERVER_BADGE_NONCE_SIZE, invite, 64) &&
		!memcmp(message + 34 + SERVER_BADGE_NONCE_SIZE + 64, "\n0102abcd\n", 10));
	crypto_ed25519_sign(signature, secret, message, (size_t)length);
	CHECK(server_badge_signature_valid(&badge, message, length, signature));
	/* (the invite is signed in lowercase) */
	invite[0] = 'A';
	CHECK(server_badge_signature_valid(&badge, message, server_badge_proof_message(message, nonce, invite, 0x0102abcdu),
		signature));
	invite[0] = 'a';
	/* (another nonce, another host's invite, another session: no proof) */
	nonce[0] ^= 1;
	length = server_badge_proof_message(message, nonce, invite, 0x0102abcdu);
	CHECK(!server_badge_signature_valid(&badge, message, length, signature));
	nonce[0] ^= 1;
	invite[63] = 'c';
	length = server_badge_proof_message(message, nonce, invite, 0x0102abcdu);
	CHECK(!server_badge_signature_valid(&badge, message, length, signature));
	invite[63] = 'b';
	length = server_badge_proof_message(message, nonce, invite, 0x0102abceu);
	CHECK(!server_badge_signature_valid(&badge, message, length, signature));
	/* (and the first drafts' proof, of the nonce alone, is none) */
	memcpy(message, "chupathingy server badge proof v1\n", 34);
	memcpy(message + 34, nonce, SERVER_BADGE_NONCE_SIZE);
	crypto_ed25519_sign(signature, secret, message, 34 + SERVER_BADGE_NONCE_SIZE);
	length = server_badge_proof_message(message, nonce, invite, 0x0102abcdu);
	CHECK(!server_badge_signature_valid(&badge, message, length, signature));
	/* (no invite, or one too long: nothing to sign) */
	CHECK(server_badge_proof_message(message, nonce, "", 1) == 0);
	CHECK(server_badge_proof_message(message, nonce, NULL, 1) == 0);
	{
		char long_invite[SERVER_BADGE_INVITE_SIZE + 2];

		memset(long_invite, 'a', sizeof(long_invite) - 1);
		long_invite[sizeof(long_invite) - 1] = 0;
		CHECK(server_badge_proof_message(message, nonce, long_invite, 1) == 0);
	}

	/* the announcement to the list: the invite and the time, as puma's
	server_badges.announce_message has them */
	length = server_badge_announce_message(message, invite, 1790000000ull);
	CHECK(length == (int)strlen(announce_expected) && !memcmp(message, announce_expected, (size_t)length));
	crypto_ed25519_sign(signature, secret, message, (size_t)length);
	CHECK(server_badge_signature_valid(&badge, message, length, signature));
	crypto_wipe(secret, sizeof(secret));
}

/* the hosts a game joined: a listed badge shown only as proven */
static void test_joined(void)
{
	static struct server_badge_joined_set set;
	struct server_badge proven;
	char invite[SERVER_BADGE_INVITE_SIZE + 1];
	unsigned char key[SERVER_BADGE_KEY_SIZE], other_key[SERVER_BADGE_KEY_SIZE];
	int index;

	memset(&proven, 0, sizeof(proven));
	proven.tier = _server_badge_tier_community;
	strcpy(proven.label, "My clan");
	memset(key, 0x11, sizeof(key));
	memset(other_key, 0x22, sizeof(other_key));
	memcpy(proven.server_key, key, sizeof(key));
	memset(invite, 'a', SERVER_BADGE_INVITE_SIZE);
	invite[SERVER_BADGE_INVITE_SIZE] = 0;

	/* (a host not joined: the list's word) */
	CHECK(server_badge_joined_allows(&set, invite, _server_badge_tier_official, "", NULL));
	/* (proven: the same badge only) */
	server_badge_joined_note(&set, invite, &proven);
	CHECK(server_badge_joined_allows(&set, invite, _server_badge_tier_community, "My clan", key));
	CHECK(server_badge_joined_allows(&set, invite, _server_badge_tier_community, "My clan", NULL));
	CHECK(!server_badge_joined_allows(&set, invite, _server_badge_tier_official, "My clan", key));
	CHECK(!server_badge_joined_allows(&set, invite, _server_badge_tier_community, "Milenko", key));
	CHECK(!server_badge_joined_allows(&set, invite, _server_badge_tier_community, "My clan", other_key));
	/* (an invite in capitals is the same invite) */
	invite[0] = 'A';
	CHECK(!server_badge_joined_allows(&set, invite, _server_badge_tier_official, "My clan", key));
	invite[0] = 'a';
	/* (none proven: no badge) */
	server_badge_joined_note(&set, invite, NULL);
	CHECK(!server_badge_joined_allows(&set, invite, _server_badge_tier_community, "My clan", key));
	/* (several hosts each kept, the oldest forgotten past the set's size) */
	for (index = 0; index < SERVER_BADGE_JOINED_COUNT - 1; index++)
	{
		invite[1] = (char)('b' + index);
		server_badge_joined_note(&set, invite, NULL);
	}
	invite[1] = 'a';
	CHECK(!server_badge_joined_allows(&set, invite, _server_badge_tier_community, "My clan", key));
	invite[1] = 'b';
	CHECK(!server_badge_joined_allows(&set, invite, _server_badge_tier_community, "My clan", key));
	invite[1] = 'z';
	server_badge_joined_note(&set, invite, NULL);
	invite[1] = 'a';
	CHECK(server_badge_joined_allows(&set, invite, _server_badge_tier_community, "My clan", key));
	invite[1] = 'b';
	CHECK(!server_badge_joined_allows(&set, invite, _server_badge_tier_community, "My clan", key));
	/* (no invite: nothing noted) */
	server_badge_joined_note(&set, "", &proven);
	CHECK(server_badge_joined_allows(&set, "", _server_badge_tier_official, "", NULL));
}

int main(void)
{
	unsigned char certificate[SERVER_BADGE_MAXIMUM_SIZE];
	int size = server_badge_from_text(certificate_text, certificate, (int)sizeof(certificate));

	test_parse(certificate, size);
	test_text();
	test_signed(certificate, size);
	test_proof(certificate, size);
	test_joined();
	if (failures)
	{
		fprintf(stderr, "%d checks failed\n", failures);
		return 1;
	}
	printf("server_badge_test: ok\n");
	return 0;
}
