/*
CONSOLE_REPORTS_TEST.C

A console's reports of the games it joins (port/console/reports/
console_reports.c) on the host (tools/test_console_reports.py): a report
sent with an identity file's key, one signed by a linked console's device
key (the signature checked here over the bytes the site checks), the
player's opt-out, a console with no identity, and the tries again. The
bodies sent are written to the folder given, for the Python side to read
as JSON.
*/

#include "port/console/reports/console_reports.h"
#include "port/console/link/console_link.h"
#include "port/linux/src/game_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (p2p_crypto.c's from the platform layer, which this test leaves out) */
void posix_random_bytes(void *buffer, unsigned long size);
void platform_log(const char *format, ...);
int p2p_ed25519_verify(const unsigned char *public_key, const void *message, int size, const unsigned char *signature);

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

/* ---------- the game's, as the test plays them */

static int report_joined_games = 1;
static int joined = 1;
#define TEST_INVITE "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"

int config_boolean(const char *name)
{
	return !strcmp(name, "network.report_joined_games") ? report_joined_games : 0;
}

int p2p_joined_invite(char *text, int size)
{
	if (!joined || size <= 64)
		return 0;
	strcpy(text, TEST_INVITE);
	return 1;
}

/* ---------- the site, as the test plays it */

static const char *folder;
static char key_path[512], link_path[512];
static int has_key;
/* the answers to give, in turn (0: no answer) */
static int statuses[8];
static int status_count, status_next;
static int requests, reports;
static char last_body[16384];

static int fake_request(const char *path, const char *body, int *status, char *reply, int reply_size)
{
	requests++;
	if (!strcmp(path, CONSOLE_REPORTS_CHALLENGE_PATH))
	{
		CHECK(body == NULL);
		*status = 200;
		snprintf(reply, (size_t)reply_size, "{\"challenge\": \"0123456789abcdef0123456789abcdef\", \"expires_in\": 300}");
		return 1;
	}
	CHECK(!strcmp(path, CONSOLE_REPORTS_PATH));
	CHECK(body && strlen(body) < sizeof(last_body));
	reports++;
	if (body)
		snprintf(last_body, sizeof(last_body), "%s", body);
	if (status_next < status_count && !statuses[status_next])
	{
		status_next++;
		return 0;
	}
	*status = status_next < status_count ? statuses[status_next++] : 200;
	snprintf(reply, (size_t)reply_size, *status == 200 ? "ok 42 unverified\n" : "not recorded: test\n");
	return 1;
}

static int fake_thread(void (*function)(void *), void *context)
{
	function(context);
	return 1;
}

static void fake_sleep(unsigned long milliseconds)
{
	(void)milliseconds;
}

static int fake_player_key(unsigned char *key)
{
	int index;

	if (!has_key)
		return 0;
	for (index = 0; index < CONSOLE_REPORTS_KEY_SIZE; index++)
		key[index] = (unsigned char)(index + 1);
	return 1;
}

static void fake_log(const char *text)
{
	printf("log: %s\n", text);
}

static void fake_lock(void) {}
static void fake_unlock(void) {}

static void fake_random(void *buffer, int size)
{
	memset(buffer, 0x33, (size_t)size);
}

static unsigned long fake_milliseconds(void)
{
	return 1000;
}

static int fake_link_request(const char *path, const char *body, int *status, char *reply, int reply_size)
{
	(void)path;
	(void)body;
	(void)status;
	(void)reply;
	(void)reply_size;
	return 0;
}

static const struct console_reports_port reports_port =
{
	"xbox", "Warthog 0.0.0-test", fake_request, fake_thread, fake_sleep, NULL, fake_player_key, fake_log,
};

static const struct console_link_port link_port =
{
	"xbox", "Warthog 0.0.0-test", key_path, link_path, fake_link_request, NULL, fake_thread, fake_lock,
	fake_unlock, fake_random, fake_milliseconds, fake_log,
};

void console_reports_port_start(void)
{
	console_reports_initialize(&reports_port);
}

/* ---------- the report */

static const unsigned short *name_of(const char *text, unsigned short *name)
{
	int index;

	memset(name, 0, BROWSER_PLAYER_NAME_LENGTH * sizeof(*name));
	for (index = 0; text[index] && index < BROWSER_PLAYER_NAME_LENGTH - 1; index++)
		name[index] = (unsigned short)(unsigned char)text[index];
	return name;
}

static struct browser_report_player lines[3];

static void make_lines(void)
{
	memset(lines, 0, sizeof(lines));
	name_of("Chief", lines[0].name);
	lines[0].team = 0;
	lines[0].place = 1;
	lines[0].score = 10;
	lines[0].kills = 10;
	lines[0].deaths = 4;
	lines[0].shots_fired = 100;
	lines[0].shots_hit = 40;
	lines[0].extra = "\"joined\": 0, \"best_spree\": 5, \"medals\": {\"killing_spree\": 1}, \"weapons\": {}";
	/* (an address the host's report would tag: never in a client's) */
	lines[0].address = 0x0100007f;
	name_of("Some\"one", lines[1].name);
	lines[1].team = 1;
	lines[1].place = 2;
	lines[1].kills = 3;
	lines[1].deaths = 6;
	lines[1].minimal = 1;
	lines[1].address = 0x0200a8c0;
	name_of("Other", lines[2].name);
	lines[2].team = 1;
	lines[2].place = 3;
	lines[2].kills = 1;
	lines[2].deaths = 4;
	lines[2].minimal = 1;
}

#define TEST_EXTRA "\"map\": \"bloodgulch\", \"engine\": 1, \"score_limit\": 15, \"host\": \"Host\", " \
	"\"reporter\": \"Chief\", \"client_joined\": 0, \"build\": \"Warthog 0.0.0-test\", \"network_version\": 41, " \
	"\"gametype\": \"Slayer\", \"stats\": 1, \"departed\": []"

static int send_report(void)
{
	make_lines();
	return console_reports_joined_game(1, 10, 4, 600, lines, 3, TEST_EXTRA);
}

static void keep(const char *name, const char *text)
{
	char path[600];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	file = fopen(path, "wb");
	CHECK(file != NULL);
	if (!file)
		return;
	fwrite(text, 1, strlen(text), file);
	fclose(file);
}

static void reset_site(void)
{
	status_count = status_next = requests = reports = 0;
	last_body[0] = 0;
}

static void test_size(void)
{
	char *report;

	make_lines();
	report = game_report_json(NULL, 1, 10, 4, 600, lines, 3, TEST_EXTRA, NULL, NULL);
	CHECK(report != NULL);
	if (report)
	{
		/* (sized from its lines: one full, two minimal) */
		CHECK(strlen(report) < game_report_size(NULL, lines, 3, TEST_EXTRA));
		CHECK(game_report_size(NULL, lines, 3, TEST_EXTRA) < 2048);
		CHECK(strstr(report, "\"tag\"") == NULL);
		CHECK(report[strlen(report) - 1] == '}');
		free(report);
	}
}

static void test_key_report(void)
{
	reset_site();
	has_key = 1;
	CHECK(send_report() == 1);
	CHECK(reports == 1);
	CHECK(!strncmp(last_body, "{\"key\": \"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\", "
		"\"platform\": \"xbox\", \"invite\": \"" TEST_INVITE "\", \"teams\": 1", strlen("{\"key\": ") + 66));
	CHECK(strstr(last_body, "\"platform\": \"xbox\", \"invite\": \"" TEST_INVITE "\", \"teams\": 1, ") != NULL);
	keep("key_report.json", last_body);
}

static void test_signed_report(void)
{
	FILE *file;
	const char *marker;
	char message[16384], public_hex[65];
	unsigned char public_key[32], signature[64];
	int index;

	reset_site();
	has_key = 0;
	/* (a linked console: its device key and link, as Link Profile keeps them) */
	file = fopen(key_path, "wb");
	fputs("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n", file);
	fclose(file);
	file = fopen(link_path, "wb");
	fputs("linked tester\n", file);
	fclose(file);
	console_link_initialize(&link_port);
	CHECK(console_link_linked_key(public_hex));
	CHECK(send_report() == 1);
	CHECK(requests == 2 && reports == 1);
	marker = strstr(last_body, ", \"signature\": \"");
	CHECK(marker != NULL);
	if (!marker)
		return;
	CHECK(strstr(marker + 1, ", \"signature\": \"") == NULL);
	CHECK(strlen(marker) == strlen(", \"signature\": \"\"}") + 128);
	CHECK(strstr(last_body, "\"console_key\": \"03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8\"") != NULL);
	CHECK(strstr(last_body, "\"challenge\": \"0123456789abcdef0123456789abcdef\"") != NULL);
	CHECK(strstr(last_body, "\"key\"") == NULL);
	/* (the site's check: the label and the bytes before the signature) */
	snprintf(message, sizeof(message), "%s%.*s", CONSOLE_REPORTS_SIGNATURE_LABEL, (int)(marker - last_body), last_body);
	CHECK(console_link_unhex(public_hex, public_key, 32));
	CHECK(console_link_unhex(marker + strlen(", \"signature\": \""), signature, 64));
	CHECK(p2p_ed25519_verify(public_key, message, (int)strlen(message), signature));
	message[strlen(CONSOLE_REPORTS_SIGNATURE_LABEL) + 3] ^= 1;
	CHECK(!p2p_ed25519_verify(public_key, message, (int)strlen(message), signature));
	keep("signed_report.json", last_body);
	for (index = 0; index < 64; index++)
		signature[index] = 0;
	/* (unlinked: nothing is sent) */
	remove(link_path);
	console_link_initialize(&link_port);
	reset_site();
	CHECK(send_report() == 0);
	CHECK(requests == 0);
	remove(key_path);
}

static void test_opt_out(void)
{
	reset_site();
	has_key = 1;
	report_joined_games = 0;
	CHECK(send_report() == 0);
	CHECK(requests == 0);
	report_joined_games = 1;
	/* (and a game not joined through an invite) */
	joined = 0;
	CHECK(send_report() == 0);
	CHECK(requests == 0);
	joined = 1;
}

static void test_tries(void)
{
	/* (no answer, then busy, then taken: three tries) */
	reset_site();
	has_key = 1;
	statuses[0] = 0;
	statuses[1] = 503;
	statuses[2] = 200;
	status_count = 3;
	CHECK(send_report() == 1);
	CHECK(reports == 3);
	/* (refused: never again) */
	reset_site();
	statuses[0] = 422;
	status_count = 1;
	CHECK(send_report() == 1);
	CHECK(reports == 1);
	/* (never an answer: CONSOLE_REPORTS_ATTEMPTS tries) */
	reset_site();
	for (status_count = 0; status_count < CONSOLE_REPORTS_ATTEMPTS; status_count++)
		statuses[status_count] = 0;
	CHECK(send_report() == 1);
	CHECK(reports == CONSOLE_REPORTS_ATTEMPTS);
}

int main(int argc, char **argv)
{
	folder = argc > 1 ? argv[1] : ".";
	snprintf(key_path, sizeof(key_path), "%s/delta_device.key", folder);
	snprintf(link_path, sizeof(link_path), "%s/delta_link.txt", folder);
	remove(key_path);
	remove(link_path);
	console_link_initialize(&link_port);
	test_size();
	test_key_report();
	test_signed_report();
	test_opt_out();
	test_tries();
	/* (no identity: nothing is sent) */
	reset_site();
	has_key = 0;
	CHECK(send_report() == 0);
	CHECK(requests == 0);
	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
