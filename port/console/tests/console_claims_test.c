/*
CONSOLE_CLAIMS_TEST.C

A console's claim of its own line (port/console/reports/console_claims.c)
on the host (tools/test_console_claims.py): the message against the site's
vector (puma's test_console_claim.py), names in hex, the map's name, the
identity file's player ID and moderator key against the site's, and whole
claims against a site the test plays: by the device key, by the identity
file, tried again while the game is not in, the player's opt-out, and a
console with no identity, which sends nothing.
*/

#include "port/console/reports/console_claims.h"
#include "port/console/link/console_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (p2p_crypto.c's from the platform layer, which this test leaves out) */
void posix_random_bytes(void *buffer, unsigned long size);
void platform_log(const char *format, ...);
void p2p_sha256(const void *data, int size, unsigned char *digest);
int p2p_ed25519_verify(const unsigned char *public_key, const void *message, int size, const unsigned char *signature);
void p2p_ed25519_public(const unsigned char *seed, unsigned char *public_key, unsigned char *x25519_secret);

void posix_random_bytes(void *buffer, unsigned long size)
{
	memset(buffer, 0x5a, size);
}

void platform_log(const char *format, ...)
{
	(void)format;
}

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

/* the site's vector (test_console_claim.py: VECTOR_MESSAGE, VECTOR_SHA256) */
#define VECTOR_SHA256 "f2440643f39d3270925f3a6747da719e6617bbfb654e16f1f2f46d9672e39e3b"
/* an identity file's key (bytes 32 to 63), its player ID and its moderator
key, as the site works them out (list_server.player_id_of, control.moderator_key) */
#define KEY_PLAYER_ID "5286018a32e839e913d51a7e16aea6d2"
#define KEY_MODERATOR "a77092fa0e6cfbf4a9d26c9af4a212eeee741b65ad447290913adc8a128e0261"
#define TEST_INVITE "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"
#define TEST_CHALLENGE "0123456789abcdef0123456789abcdef"

static void hex(const unsigned char *data, int size, char *text)
{
	console_link_hex(data, size, text);
}

static void test_pure(void)
{
	struct console_claim claim;
	char text[CONSOLE_CLAIMS_MESSAGE_SIZE], body[CONSOLE_CLAIMS_BODY_SIZE], digest_hex[65];
	char identity[CONSOLE_CLAIMS_IDENTITY_SIZE], signature[129], map[CONSOLE_CLAIMS_MAP_SIZE];
	unsigned char digest[32], key[32], public_key[32], seed[32], signature_bytes[64];
	static const unsigned short xbox[] = { 'X', 'b', 'o', 'x', 0 };
	static const unsigned short chief[] = { 'C', 'h', 'i', 'e', 'f', '#', 0, 'z' };
	static const unsigned short accented[] = { 0xe9, 0x263a, 0 };
	static const unsigned short empty[] = { 0 };
	char name[CONSOLE_CLAIMS_NAME_SIZE];
	int length, index;

	memset(&claim, 0, sizeof(claim));
	claim.hosted = 1;
	CHECK(console_claims_map("levels\\test\\bloodgulch\\bloodgulch", claim.map));
	CHECK(!strcmp(claim.map, "bloodgulch"));
	CHECK(console_claims_name_hex(xbox, 16, claim.host_hex, CONSOLE_CLAIMS_HOST_SIZE) == 8);
	CHECK(!strcmp(claim.host_hex, "58626f78"));
	/* (up to the 0: what follows it is not the name) */
	CHECK(console_claims_name_hex(chief, 8, claim.name_hex, CONSOLE_CLAIMS_NAME_SIZE) == 12);
	CHECK(!strcmp(claim.name_hex, "436869656623"));
	claim.team = 1;
	claim.score = -2;
	claim.kills = 12;
	claim.deaths = 3;
	claim.duration = 600;
	length = console_claims_message(&claim, "key:abababababababababababababababababababababababababababababababab", "xbox",
		TEST_CHALLENGE, 41, text, (int)sizeof(text));
	CHECK(length > 0);
	p2p_sha256(text, length, digest);
	hex(digest, 32, digest_hex);
	CHECK(!strcmp(digest_hex, VECTOR_SHA256));
	CHECK(!console_claims_message(&claim, "key:ab", "xbox", TEST_CHALLENGE, 41, text, 40));

	/* names and maps */
	CHECK(console_claims_name_hex(accented, 12, name, (int)sizeof(name)) == 10);
	CHECK(!strcmp(name, "c3a9e298ba"));
	CHECK(!console_claims_name_hex(empty, 12, name, (int)sizeof(name)));
	CHECK(!console_claims_name_hex(xbox, 16, name, 8));
	CHECK(console_claims_map("putput", map) && !strcmp(map, "putput"));
	CHECK(console_claims_map("maps/ce/Gephyrophobia", map) && !strcmp(map, "Gephyrophobia"));
	CHECK(!console_claims_map("blood gulch", map));
	CHECK(!console_claims_map("a\"b", map));
	CHECK(!console_claims_map("levels\\", map));
	CHECK(!console_claims_map(NULL, map));

	/* the identity file's ID and signature, as the site checks them */
	for (index = 0; index < 32; index++)
		key[index] = (unsigned char)(32 + index);
	console_claims_player_identity(key, identity);
	CHECK(!strcmp(identity, "player:" KEY_PLAYER_ID));
	length = console_claims_message(&claim, identity, "xbox", TEST_CHALLENGE, 41, text, (int)sizeof(text));
	console_claims_player_sign(key, text, length, signature);
	CHECK(console_link_unhex(KEY_MODERATOR, public_key, 32));
	CHECK(console_link_unhex(signature, signature_bytes, 64));
	CHECK(p2p_ed25519_verify(public_key, text, length, signature_bytes));
	CHECK(!p2p_ed25519_verify(public_key, text, length - 1, signature_bytes));

	/* the body: the identity's member, the fields, the signature last */
	length = console_claims_body(&claim, identity, "xbox", TEST_CHALLENGE, 41, signature, body, (int)sizeof(body));
	CHECK(length > 0);
	CHECK(strstr(body, "{\"player\": \"" KEY_PLAYER_ID "\", \"platform\": \"xbox\"") == body);
	CHECK(strstr(body, "\"role\": \"host\", \"invite\": \"\", \"map\": \"bloodgulch\"") != NULL);
	CHECK(strstr(body, "\"team\": 1, \"score\": -2, \"kills\": 12, \"deaths\": 3, \"duration\": 600, \"ago\": 41") != NULL);
	for (index = 0; index < 32; index++)
		seed[index] = (unsigned char)index;
	p2p_ed25519_public(seed, public_key, NULL);
	hex(public_key, 32, identity + 4);
	memcpy(identity, "key:", 4);
	CHECK(console_claims_body(&claim, identity, "xbox", TEST_CHALLENGE, 41, signature, body, (int)sizeof(body)) > 0);
	CHECK(strstr(body, "{\"console_key\": \"03a107bff3ce10be") == body);
	CHECK(!console_claims_body(&claim, "nobody", "xbox", TEST_CHALLENGE, 41, signature, body, (int)sizeof(body)));
	CHECK(!console_claims_body(&claim, identity, "xbox", TEST_CHALLENGE, 41, signature, body, 100));
}

/* ---------- the game's, as the test plays them */

static int report_joined_games = 1;

int config_boolean(const char *name)
{
	return !strcmp(name, "network.report_joined_games") ? report_joined_games : 0;
}

/* ---------- the site, as the test plays it */

static const char *folder;
static char key_path[512], link_path[512];
static int has_key;
/* the claim answers to give, in turn (0: no answer) */
static int statuses[8];
static int status_count, status_next;
static int requests, claims, slept;
static char last_body[CONSOLE_CLAIMS_BODY_SIZE];
static unsigned long now = 100000;

static int fake_request(const char *path, const char *body, int *status, char *reply, int reply_size)
{
	int answer;

	requests++;
	if (!strcmp(path, CONSOLE_CLAIMS_CHALLENGE_PATH))
	{
		CHECK(body == NULL);
		*status = 200;
		snprintf(reply, (size_t)reply_size, "{\"challenge\": \"" TEST_CHALLENGE "\", \"expires_in\": 300}");
		return 1;
	}
	CHECK(!strcmp(path, CONSOLE_CLAIMS_PATH));
	CHECK(body != NULL);
	claims++;
	snprintf(last_body, sizeof(last_body), "%s", body);
	answer = status_next < status_count ? statuses[status_next++] : 200;
	if (!answer)
		return 0;
	*status = answer;
	snprintf(reply, (size_t)reply_size, answer == 200 ? "ok 7 claimed\n" : "no such game or line yet\n");
	return 1;
}

static int fake_thread(void (*function)(void *context), void *context)
{
	/* (the thread run at once: the test waits for nothing) */
	function(context);
	return 1;
}

static void fake_sleep(unsigned long milliseconds)
{
	slept++;
	now += milliseconds;
}

static unsigned long fake_milliseconds(void)
{
	return now;
}

static int fake_player_key(unsigned char *key)
{
	int index;

	if (!has_key)
		return 0;
	for (index = 0; index < 32; index++)
		key[index] = (unsigned char)(32 + index);
	return 1;
}

static char last_log[256];

static void fake_log(const char *text)
{
	snprintf(last_log, sizeof(last_log), "%s", text);
	CHECK(strstr(text, "signature") == NULL);
}

static const struct console_claims_port claims_port =
{
	"xbox", fake_request, fake_thread, fake_sleep, fake_milliseconds, fake_player_key, fake_log,
};

void console_claims_port_start(void)
{
	console_claims_initialize(&claims_port);
}

/* the link's own hooks: linked by a key file and a link file the test writes */
static int link_request(const char *path, const char *body, int *status, char *reply, int reply_size)
{
	(void)path;
	(void)body;
	(void)status;
	(void)reply;
	(void)reply_size;
	return 0;
}

static int link_thread(void (*function)(void *context), void *context)
{
	(void)function;
	(void)context;
	return 0;
}

static void nothing(void)
{
}

static void link_random(void *buffer, int size)
{
	memset(buffer, 0x11, (size_t)size);
}

static unsigned long link_milliseconds(void)
{
	return now;
}

static void link_log(const char *text)
{
	(void)text;
}

static struct console_link_port link_port =
{
	"xbox", "Warthog test", key_path, link_path, link_request, NULL, link_thread, nothing, nothing, link_random,
	link_milliseconds, link_log,
};

static void write_file(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");

	if (file)
	{
		fputs(text, file);
		fclose(file);
	}
}

static int game_ended(int hosted)
{
	static const unsigned short host[] = { 'X', 'b', 'o', 'x', 0 };
	static const unsigned short name[] = { 'C', 'h', 'i', 'e', 'f', 0 };

	return console_claims_game_ended(hosted, hosted ? "" : TEST_INVITE, "levels\\test\\bloodgulch\\bloodgulch", host,
		16, name, 12, 0, 12, 12, 3, 600);
}

static void save_body(const char *file)
{
	char path[600];

	snprintf(path, sizeof(path), "%s/%s", folder, file);
	write_file(path, last_body);
}

static void test_console(void)
{
	char public_hex[65], message[CONSOLE_CLAIMS_MESSAGE_SIZE];
	unsigned char public_key[32], signature[64];
	const char *at;
	int length;

	/* no identity: nothing asked, nothing logged */
	link_port.key_path = key_path;
	console_link_initialize(&link_port);
	requests = 0;
	last_log[0] = 0;
	CHECK(!game_ended(1));
	CHECK(requests == 0 && !last_log[0]);

	/* the console's link: its device key signs */
	write_file(key_path, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n");
	write_file(link_path, "linked walter\n");
	console_link_initialize(&link_port);
	CHECK(console_link_linked_key(public_hex));
	requests = claims = slept = 0;
	CHECK(game_ended(1));
	CHECK(claims == 1 && slept == 1);
	CHECK(strstr(last_log, "claimed") != NULL);
	CHECK(strstr(last_body, "{\"console_key\": \"03a107bff3ce10be") == last_body);
	CHECK(strstr(last_body, "\"role\": \"host\", \"invite\": \"\"") != NULL);
	CHECK(strstr(last_body, "\"ago\": 15,") != NULL);
	save_body("claim_key.json");
	/* (the signature over the message the site rebuilds from the body) */
	{
		struct console_claim claim;

		memset(&claim, 0, sizeof(claim));
		claim.hosted = 1;
		strcpy(claim.map, "bloodgulch");
		strcpy(claim.host_hex, "58626f78");
		strcpy(claim.name_hex, "4368696566");
		claim.score = 12;
		claim.kills = 12;
		claim.deaths = 3;
		claim.duration = 600;
		char identity[CONSOLE_CLAIMS_IDENTITY_SIZE] = "key:";
		strcat(identity, public_hex);
		length = console_claims_message(&claim, identity, "xbox", TEST_CHALLENGE, 15, message, (int)sizeof(message));
		at = strstr(last_body, "\"signature\": \"");
		CHECK(at && console_link_unhex(at + 14, signature, 64));
		CHECK(console_link_unhex(public_hex, public_key, 32));
		CHECK(length && p2p_ed25519_verify(public_key, message, length, signature));
	}

	/* not in yet, no answer, then taken: tried again, a fresh challenge each time */
	statuses[0] = 404;
	statuses[1] = 0;
	statuses[2] = 200;
	status_count = 3;
	status_next = 0;
	requests = claims = slept = 0;
	CHECK(game_ended(0));
	CHECK(claims == 3 && requests == 6 && slept == 3);
	CHECK(strstr(last_body, "\"role\": \"joined\", \"invite\": \"" TEST_INVITE "\"") != NULL);
	CHECK(strstr(last_body, "\"ago\": 55,") != NULL);

	/* refused: not again */
	statuses[0] = 409;
	status_count = 1;
	status_next = 0;
	claims = 0;
	CHECK(game_ended(1));
	CHECK(claims == 1 && strstr(last_log, "not claimed (409") != NULL);

	/* never found: CONSOLE_CLAIMS_ATTEMPTS tries */
	statuses[0] = statuses[1] = statuses[2] = statuses[3] = 404;
	status_count = 4;
	status_next = 0;
	claims = 0;
	CHECK(game_ended(1));
	CHECK(claims == CONSOLE_CLAIMS_ATTEMPTS);
	CHECK(strstr(last_log, "no report of the game") != NULL);

	/* the identity file first, as the reports */
	has_key = 1;
	status_count = 0;
	claims = 0;
	CHECK(game_ended(1));
	CHECK(claims == 1);
	CHECK(strstr(last_body, "{\"player\": \"" KEY_PLAYER_ID "\"") == last_body);
	save_body("claim_player.json");
	has_key = 0;

	/* the player's opt-out: nothing */
	report_joined_games = 0;
	requests = 0;
	CHECK(!game_ended(1));
	CHECK(requests == 0);
	report_joined_games = 1;
}

int main(int argc, char **argv)
{
	folder = argc > 1 ? argv[1] : ".";
	snprintf(key_path, sizeof(key_path), "%s/delta_device.key", folder);
	snprintf(link_path, sizeof(link_path), "%s/delta_link.txt", folder);
	remove(key_path);
	remove(link_path);
	test_pure();
	test_console();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
