/*
CONSOLE_LINK_TEST.C

The console's Link Profile (port/console/link/console_link.c) on the host
(tools/test_console_link.py): the vectors the site's test_console_link.py
has, the answers' parsers, and a whole link against a site played by this
test, its requests made at once on this thread.
*/

#include "console_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

/* (p2p_crypto.c's from the platform layer, which this test leaves out) */
void posix_random_bytes(void *buffer, unsigned long size);
void platform_log(const char *format, ...);

void posix_random_bytes(void *buffer, unsigned long size)
{
	memset(buffer, 0x5a, size);
}

void platform_log(const char *format, ...)
{
	(void)format;
}

#define CHECK(condition) \
	do { if (!(condition)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

/* (the site's test_console_link.py) */
#define VECTOR_PUBLIC "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8"
#define VECTOR_ID "b37d2696fbb918043c74b18d617e29c0"
#define VECTOR_COMMIT "b631ed67e183f1202c52dd4e6c1fc46190079681a604285b94b8ccdd1a6fd50b"
#define VECTOR_CODE "RBWPZHGB"
#define VECTOR_CONFIRM "2be6aff7d8b9166e5c6c5fd9bfd317706a454fcdc5876a7495fbeea43d01d121" \
	"efb6af3780df46d911b4376bb8c3d2ff10887ec61dc2ea77848f33e22517170c"

static void test_vectors(void)
{
	unsigned char seed[32], secret[32], nonce[16], public_key[32], id[16], commitment[32];
	char hex[129], code[CONSOLE_LINK_CODE_LENGTH + 1], message[200], token[65];
	int index, length;

	for (index = 0; index < 32; index++)
	{
		seed[index] = (unsigned char)index;
		secret[index] = (unsigned char)(32 + index);
	}
	for (index = 0; index < 16; index++)
		nonce[index] = (unsigned char)(64 + index);
	console_link_public_key(seed, public_key);
	console_link_hex(public_key, 32, hex);
	CHECK(!strcmp(hex, VECTOR_PUBLIC));
	console_link_id(public_key, id);
	console_link_hex(id, 16, hex);
	CHECK(!strcmp(hex, VECTOR_ID));
	console_link_commitment(secret, commitment);
	console_link_hex(commitment, 32, hex);
	CHECK(!strcmp(hex, VECTOR_COMMIT));
	console_link_code(public_key, secret, nonce, code);
	CHECK(!strcmp(code, VECTOR_CODE));
	for (index = 0; index < 64; index++)
		token[index] = index % 2 ? 'b' : 'a';
	token[64] = 0;
	length = console_link_confirm_message(token, "walter", 1, message, sizeof(message));
	CHECK(length > 0);
	console_link_sign(seed, message, length, hex);
	CHECK(!strcmp(hex, VECTOR_CONFIRM));
	CHECK(!console_link_confirm_message(token, "walter", 1, message, 40));
}

static void test_parsers(void)
{
	unsigned char nonce[16], seed[32];
	char token[65], handle[CONSOLE_LINK_HANDLE_SIZE], previous[CONSOLE_LINK_HANDLE_SIZE];
	char display[CONSOLE_LINK_DISPLAY_SIZE], tag[CONSOLE_LINK_TAG_SIZE], challenge[33];
	char reply[300];
	int seconds = 0;

	strcpy(reply, "ok 000102030405060708090a0b0c0d0e0f 600 ");
	strcat(reply, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");
	CHECK(console_link_parse_start(reply, nonce, &seconds, token) && seconds == 600 && nonce[15] == 15 &&
		token[63] == 'f' && !token[64]);
	CHECK(!console_link_parse_start("ok 00 600 abc\n", nonce, &seconds, token));
	CHECK(!console_link_parse_start("too many codes, try again later\n", nonce, &seconds, token));

	CHECK(console_link_parse_status("pending\n", handle, previous) == CONSOLE_LINK_STATUS_PENDING);
	CHECK(console_link_parse_status("confirm walter\n", handle, previous) == CONSOLE_LINK_STATUS_CONFIRM &&
		!strcmp(handle, "walter") && !previous[0]);
	CHECK(console_link_parse_status("confirm walter guven\n", handle, previous) == CONSOLE_LINK_STATUS_CONFIRM &&
		!strcmp(previous, "guven"));
	CHECK(console_link_parse_status("connected walter\n", handle, previous) == CONSOLE_LINK_STATUS_CONNECTED);
	CHECK(console_link_parse_status("confirm Wa\n", handle, previous) == CONSOLE_LINK_STATUS_BAD && !handle[0]);
	CHECK(console_link_parse_status("declined\n", handle, previous) == CONSOLE_LINK_STATUS_DECLINED);
	CHECK(console_link_parse_status("expired\n", handle, previous) == CONSOLE_LINK_STATUS_EXPIRED);
	CHECK(console_link_parse_status("<html>", handle, previous) == CONSOLE_LINK_STATUS_BAD);

	CHECK(console_link_parse_challenge("{\"challenge\": \"0123456789abcdef0123456789abcdef\", \"expires_in\": 300}",
		challenge) && !strcmp(challenge, "0123456789abcdef0123456789abcdef"));
	CHECK(!console_link_parse_challenge("{\"challenge\": \"0123\"}", challenge));

	CHECK(console_link_parse_profile("linked walter\ndisplay Walter Mitty\ntag RVB\nend\n", handle, display, tag) == 1 &&
		!strcmp(handle, "walter") && !strcmp(display, "Walter Mitty") && !strcmp(tag, "RVB"));
	CHECK(console_link_parse_profile("linked walter\ndisplay W\xc3\xa9\nend\n", handle, display, tag) == 1 &&
		!strcmp(display, "W??") && !tag[0]);
	CHECK(console_link_parse_profile("unlinked\n", handle, display, tag) == 0);
	CHECK(console_link_parse_profile("linked walter\ndisplay W\n", handle, display, tag) == -1 && !handle[0]);
	CHECK(console_link_parse_profile("slow down\n", handle, display, tag) == -1);

	CHECK(console_link_parse_seed("  000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\r\n", 68, seed) &&
		seed[31] == 31);
	CHECK(!console_link_parse_seed("0000000000000000000000000000000000000000000000000000000000000000", 64, seed));
	CHECK(!console_link_parse_seed("0102", 4, seed));
}

/* ---------- a link, against a site this test plays */

static char key_path[512], link_path[512];
static unsigned long clock_now = 1000;
/* what the site answers to status next, and what it saw */
static const char *status_answer = "pending\n";
static char seen_commit[65], seen_key[65], seen_confirm[600], seen_profile[600];
static int requests, profile_linked = 1;

static int field(const char *body, const char *name, char *value, int size)
{
	char pattern[32];
	const char *at;
	int length = 0;

	snprintf(pattern, sizeof(pattern), "\"%s\": \"", name);
	at = strstr(body, pattern);
	if (!at)
		return 0;
	at += strlen(pattern);
	while (at[length] && at[length] != '"' && length < size - 1)
		length++;
	memcpy(value, at, (size_t)length);
	value[length] = 0;
	return 1;
}

static int fake_request(const char *path, const char *body, int *status, char *reply, int reply_size)
{
	requests++;
	*status = 200;
	if (!strcmp(path, "/v1/console/link/start"))
	{
		CHECK(field(body, "key", seen_key, sizeof(seen_key)) && strlen(seen_key) == 64);
		CHECK(field(body, "commit", seen_commit, sizeof(seen_commit)) && strlen(seen_commit) == 64);
		CHECK(strstr(body, "\"platform\": \"xbox360\"") != NULL);
		/* (the build's quote left out) */
		CHECK(strstr(body, "\"build\": \"Warthog  test\"") != NULL);
		snprintf(reply, (size_t)reply_size, "ok 000102030405060708090a0b0c0d0e0f 600 %s\n",
			"ab00000000000000000000000000000000000000000000000000000000000000");
		return 1;
	}
	if (!strcmp(path, "/v1/console/link/code"))
	{
		char secret_hex[65];
		unsigned char secret[32], commitment[32];
		char commit_hex[65];

		CHECK(field(body, "secret", secret_hex, sizeof(secret_hex)) && console_link_unhex(secret_hex, secret, 32));
		console_link_commitment(secret, commitment);
		console_link_hex(commitment, 32, commit_hex);
		CHECK(!strcmp(commit_hex, seen_commit));
		snprintf(reply, (size_t)reply_size, "ok 600\n");
		return 1;
	}
	if (!strcmp(path, "/v1/console/link/status"))
	{
		snprintf(reply, (size_t)reply_size, "%s", status_answer);
		return 1;
	}
	if (!strcmp(path, "/v1/console/link/confirm"))
	{
		snprintf(seen_confirm, sizeof(seen_confirm), "%s", body);
		snprintf(reply, (size_t)reply_size, strstr(body, "\"accept\": true") ? "connected walter\n" : "declined\n");
		return 1;
	}
	if (!strcmp(path, "/v1/claim_challenge"))
	{
		CHECK(body == NULL);
		snprintf(reply, (size_t)reply_size, "{\"challenge\": \"0123456789abcdef0123456789abcdef\", \"expires_in\": 300}");
		return 1;
	}
	if (!strcmp(path, "/v1/console/profile"))
	{
		snprintf(seen_profile, sizeof(seen_profile), "%s", body);
		snprintf(reply, (size_t)reply_size, profile_linked ? "linked walter\ndisplay Walter\ntag RVB\nend\n" :
			"unlinked\n");
		return 1;
	}
	*status = 404;
	return 1;
}

static int fake_thread(void (*function)(void *), void *context)
{
	function(context);
	return 1;
}

static void fake_lock(void) {}
static void fake_unlock(void) {}

static void fake_random(void *buffer, int size)
{
	static unsigned char next = 7;
	unsigned char *bytes = buffer;

	while (size-- > 0)
		*bytes++ = next++;
}

static unsigned long fake_milliseconds(void)
{
	return clock_now;
}

static void fake_log(const char *text)
{
	/* (never a key, a code or a token) */
	CHECK(strlen(text) < 64);
}

static void test_link(void)
{
	static const struct console_link_port port =
	{
		"xbox360", "Warthog \"test", key_path, link_path, fake_request, NULL, fake_thread, fake_lock, fake_unlock,
		fake_random, fake_milliseconds, fake_log
	};
	struct console_link_view view;
	unsigned char id[16], seed[32], public_key[32], expected[16];
	char message[200], signature[129], token[65];
	FILE *file;
	int length;

	remove(key_path);
	remove(link_path);
	console_link_initialize(&port);
	console_link_view(&view);
	CHECK(view.phase == CONSOLE_LINK_IDLE && !view.linked);
	CHECK(!console_link_player_id(id));
	/* (no key yet: the profile is not asked for) */
	console_link_refresh();
	CHECK(requests == 0);

	console_link_begin();
	console_link_view(&view);
	CHECK(view.phase == CONSOLE_LINK_CODE_SHOWN);
	CHECK(strlen(view.code) == 9 && view.code[4] == '-');
	CHECK(view.code_left == 600000);
	CHECK(console_link_qr_size() >= 21 && console_link_qr_size() <= CONSOLE_LINK_QR_MAXIMUM);
	CHECK(console_link_qr_dark(0, 0) && console_link_qr_dark(6, 6) && !console_link_qr_dark(1, 1));
	/* (the key kept, as 64 hex digits) */
	file = fopen(key_path, "rb");
	CHECK(file != NULL);
	if (file)
	{
		char text[100];
		int size = (int)fread(text, 1, sizeof(text), file);

		fclose(file);
		CHECK(console_link_parse_seed(text, size, seed));
		console_link_public_key(seed, public_key);
		console_link_hex(public_key, 32, message);
		CHECK(!strcmp(message, seen_key));
	}

	/* a poll every 3 seconds: pending, then the claim */
	clock_now += 1000;
	requests = 0;
	console_link_update();
	CHECK(requests == 0);
	clock_now += 2500;
	console_link_update();
	CHECK(requests == 1);
	status_answer = "confirm walter guven\n";
	clock_now += 3000;
	console_link_update();
	console_link_view(&view);
	CHECK(view.phase == CONSOLE_LINK_CONFIRM && !strcmp(view.asker, "walter") && !strcmp(view.previous, "guven"));

	console_link_answer(1);
	console_link_view(&view);
	CHECK(view.phase == CONSOLE_LINK_DONE && view.linked && !strcmp(view.handle, "walter"));
	/* (the answer signed over the token and the profile shown) */
	memset(token, 0, sizeof(token));
	strcpy(token, "ab00000000000000000000000000000000000000000000000000000000000000");
	length = console_link_confirm_message(token, "walter", 1, message, sizeof(message));
	console_link_sign(seed, message, length, signature);
	CHECK(strstr(seen_confirm, signature) != NULL);
	CHECK(console_link_player_id(id));
	console_link_id(public_key, expected);
	CHECK(!memcmp(id, expected, 16));

	/* the link kept across a restart, and the profile asked for */
	console_link_initialize(&port);
	CHECK(console_link_player_id(id));
	console_link_refresh();
	console_link_view(&view);
	CHECK(!strcmp(view.display, "Walter") && !strcmp(view.tag, "RVB"));
	CHECK(strstr(seen_profile, "\"challenge\": \"0123456789abcdef0123456789abcdef\"") != NULL);
	/* (at most once a minute) */
	requests = 0;
	console_link_refresh();
	CHECK(requests == 0);

	/* unlinked on the site: forgotten */
	profile_linked = 0;
	clock_now += 61000;
	console_link_refresh();
	console_link_view(&view);
	CHECK(!view.linked && !view.handle[0] && !console_link_player_id(id));
	file = fopen(link_path, "rb");
	CHECK(file == NULL);
	if (file)
		fclose(file);

	/* a code that runs out */
	status_answer = "pending\n";
	console_link_begin();
	clock_now += 600001;
	console_link_update();
	console_link_view(&view);
	CHECK(view.phase == CONSOLE_LINK_FAILED);
	console_link_cancel();
	console_link_view(&view);
	CHECK(view.phase == CONSOLE_LINK_IDLE);

	remove(key_path);
	remove(link_path);
}

int main(int argc, char **argv)
{
	const char *folder = argc > 1 ? argv[1] : ".";

	snprintf(key_path, sizeof(key_path), "%s/delta_device.key", folder);
	snprintf(link_path, sizeof(link_path), "%s/delta_link.txt", folder);
	test_vectors();
	test_parsers();
	test_link();
	if (failures)
	{
		printf("%d failed\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
