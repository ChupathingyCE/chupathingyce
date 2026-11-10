/*
BANS_TEST.C

The shared ban lists' and the report queue's own logic
(server/platform/server_bans.c, report_queue.c): a bans.txt line as a
shared ban (accounts and devices only, never an address), what goes to the
site and when, matching an identity, exceptions, and the queue's limits,
JSON and what the site is sent. Built and run by tools/test_server_bans.py
in a data folder of its own.
*/

#include "../src/server_bans.h"
#include "../src/server_reports.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;
static const char *data_root;

#define CHECK(condition) check((condition) ? 1 : 0, #condition, __LINE__)

static void check(int ok, const char *text, int line)
{
	checks++;
	if (!ok)
	{
		failures++;
		printf("line %d: %s\n", line, text);
	}
}

const char *platform_data_root(void)
{
	return data_root;
}

static void write_bans(const char *text)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/bans.txt", data_root);
	file = fopen(path, "w");
	fputs(text, file);
	fclose(file);
}

static void test_lines(void)
{
	struct server_shared_ban ban;

	/* (an address and hardware id alone: nothing to share) */
	CHECK(!server_bans_line_entry("2026-11-09 10:00:00\tip=1.2.3.4\thwid=abc\tplayers=x\treason=y\n", 100, &ban));
	CHECK(!server_bans_line_entry("# a comment account=00000000000000000000000000000000", 100, &ban));
	CHECK(server_bans_line_entry("2026-11-09 10:00:00\tip=1.2.3.4\thwid=abc\tplayers=x\treason=banned by Odb\t"
		"account=0123456789abcdef0123456789abcdef,fedcba9876543210fedcba9876543210\tdevice=11111111111111111111111111111111"
		"\tprofile=cheater,alt\n", 100, &ban));
	CHECK(ban.account_count == 2 && ban.accounts[1][0] == 0xfe && ban.has_device && ban.device[15] == 0x11 &&
		!strcmp(ban.reason, "banned by Odb") && !strcmp(ban.handles, "cheater,alt") && strlen(ban.ref) == 32 &&
		ban.time > 0 && !ban.until);
	/* (nothing of the address goes anywhere) */
	CHECK(!strstr(ban.reason, "1.2.3.4") && !strstr(ban.handles, "1.2.3.4"));
	/* (a timed ban over: none; not over: until kept) */
	CHECK(!server_bans_line_entry("x\tuntil=50\taccount=0123456789abcdef0123456789abcdef\n", 100, &ban));
	CHECK(server_bans_line_entry("x\tuntil=500\taccount=0123456789abcdef0123456789abcdef\n", 100, &ban) &&
		ban.until == 500 && !ban.time);
	/* (an id that isn't 32 hex digits is not one) */
	CHECK(!server_bans_line_entry("x\taccount=0123\tdevice=zz\n", 100, &ban));
}

static void test_share(void)
{
	char payload[64 * 1024];

	/* sharing off, never shared: nothing */
	server_bans_set_sharing(0);
	write_bans("2026-11-09 10:00:00\tip=1.2.3.4\thwid=a\tplayers=x\treason=r\taccount=0123456789abcdef0123456789abcdef\n");
	CHECK(server_bans_share_payload(payload, sizeof(payload)) == 0);
	/* on: the set, without the address */
	server_bans_set_sharing(1);
	CHECK(server_bans_share_payload(payload, sizeof(payload)) > 0);
	CHECK(!strncmp(payload, "\"bans\": {\"digest\": \"", 20) && strstr(payload, "0123456789abcdef0123456789abcdef") &&
		!strstr(payload, "1.2.3.4") && !strstr(payload, "hwid"));
	/* the site says it has that digest: nothing more until it changes */
	{
		char digest[65];
		const char *at = payload + 20;

		memcpy(digest, at, 64);
		digest[64] = 0;
		server_bans_share_answered(digest);
		CHECK(server_bans_share_payload(payload, sizeof(payload)) == 0);
	}
	/* turned off: an empty set once, then nothing */
	server_bans_set_sharing(0);
	CHECK(server_bans_share_payload(payload, sizeof(payload)) > 0 &&
		!strcmp(payload, "\"bans\": {\"digest\": \"\", \"entries\": []}"));
	server_bans_share_answered("");
	CHECK(server_bans_share_payload(payload, sizeof(payload)) == 0);
	/* (no room: -1, nothing half written) */
	server_bans_set_sharing(1);
	server_bans_share_answered("x");
	CHECK(server_bans_share_payload(payload, 20) < 0 && !payload[0]);
	server_bans_set_sharing(0);
}

static void test_shared(void)
{
	static struct server_shared_ban bans[2];
	struct server_shared_ban got;
	char list[64], ref[40];
	int excepted = 0;

	memset(bans, 0, sizeof(bans));
	snprintf(bans[0].list_name, sizeof(bans[0].list_name), "milenko's bans");
	snprintf(bans[0].ref, sizeof(bans[0].ref), "0123456789abcdef0123456789abcdef");
	bans[0].account_count = 1;
	memset(bans[0].accounts[0], 0xaa, 16);
	snprintf(bans[1].list, sizeof(bans[1].list), "odb");
	snprintf(bans[1].ref, sizeof(bans[1].ref), "fedcba9876543210fedcba9876543210");
	bans[1].has_device = 1;
	memset(bans[1].device, 0xcc, 16);
	bans[1].until = 1000;
	server_bans_set_shared(bans, 2, 9, 0);
	CHECK(server_bans_take_changed() && !server_bans_take_changed());
	CHECK(server_bans_shared_version() == 9 && server_bans_shared_count() == 2);
	CHECK(server_bans_shared_match("account=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb,aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\tprofile=x", 100,
		list, sizeof(list), ref, sizeof(ref)) && !strcmp(list, "milenko's bans") &&
		!strcmp(ref, "0123456789abcdef0123456789abcdef"));
	CHECK(!server_bans_shared_match("account=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 100, list, sizeof(list), ref,
		sizeof(ref)));
	/* (a device's ban until 1000: at 999, and not at 1000; the list's id
	stands in for a name it lacks) */
	CHECK(server_bans_shared_match("device=cccccccccccccccccccccccccccccccc", 999, list, sizeof(list), NULL, 0) &&
		!strcmp(list, "odb"));
	CHECK(!server_bans_shared_match("device=cccccccccccccccccccccccccccccccc", 1000, list, sizeof(list), NULL, 0));
	CHECK(!server_bans_shared_match("", 1, NULL, 0, NULL, 0));
	/* an exception here: kept in shared_unbans.txt, and taken back */
	CHECK(server_bans_except("0123456789abcdef0123456789abcdef", 1) && server_bans_take_changed());
	CHECK(!server_bans_shared_match("account=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 100, NULL, 0, NULL, 0));
	CHECK(server_bans_shared_entry(0, &got, &excepted) && excepted);
	CHECK(!server_bans_except("not a ref", 1));
	CHECK(server_bans_except("0123456789abcdef0123456789abcdef", 0));
	CHECK(server_bans_shared_match("account=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 100, NULL, 0, NULL, 0));
	CHECK(!server_bans_shared_entry(2, &got, NULL));
	/* (unlinked: gone) */
	server_bans_set_shared(NULL, 0, 0, 0);
	CHECK(server_bans_shared_count() == 0);
}

static void test_reports(void)
{
	static struct server_report report;
	static char text[64 * 1024];
	char refs[1][SERVER_REPORT_ID_SIZE];
	unsigned int first, index;

	memset(&report, 0, sizeof(report));
	report.reason = SERVER_REPORT_CHEATING;
	snprintf(report.target.name, sizeof(report.target.name), "Bad \"Guy\"");
	snprintf(report.target.handle, sizeof(report.target.handle), "badguy");
	snprintf(report.target.account, sizeof(report.target.account), "0123456789abcdef0123456789abcdef");
	report.target.delta = 1;
	snprintf(report.reporter.name, sizeof(report.reporter.name), "Me");
	report.reporter.delta = 1;
	snprintf(report.map, sizeof(report.map), "bloodgulch");
	report.evidence[0] = 4;
	first = server_reports_add(&report);
	CHECK(first && strlen(report.ref) == 32 && server_reports_open() == 1);
	/* an OpenCE player's: kept here, never sent */
	memset(&report, 0, sizeof(report));
	report.reason = SERVER_REPORT_NAME;
	snprintf(report.target.name, sizeof(report.target.name), "OpenGuy");
	CHECK(server_reports_add(&report) == first + 1);
	CHECK(server_reports_site_payload(text, sizeof(text)) > 0);
	CHECK(strstr(text, "\"handle\": \"badguy\"") && strstr(text, "Bad \\\"Guy\\\"") && !strstr(text, "OpenGuy") &&
		!strstr(text, "\"id\""));
	server_reports_text(1, 0, text, sizeof(text));
	CHECK(strstr(text, "OpenGuy") && strstr(text, "\"open\": 2") && strstr(text, "\"delta\": false"));
	/* the site took the first: nothing more to send */
	snprintf(refs[0], sizeof(refs[0]), "%s", "");
	server_reports_text(1, 0, text, sizeof(text));
	{
		const char *at = strstr(text, "\"ref\": \"");

		/* (the newest is first: the OpenCE one; the Delta one's ref is the second) */
		at = strstr(at + 1, "\"ref\": \"");
		CHECK(at != NULL);
		if (at)
		{
			memcpy(refs[0], at + 8, 32);
			refs[0][32] = 0;
		}
	}
	server_reports_site_taken((const char (*)[SERVER_REPORT_ID_SIZE])refs, 1);
	CHECK(server_reports_site_payload(text, sizeof(text)) == 0);
	/* closed */
	CHECK(server_reports_close(first, "Odb") && !server_reports_close(first, "Odb") && server_reports_open() == 1);
	server_reports_text(0, 1, text, sizeof(text));
	CHECK(strstr(text, "closed by Odb") != NULL);
	server_reports_text(0, 0, text, sizeof(text));
	CHECK(!strstr(text, "closed by Odb"));
	/* the queue keeps SERVER_REPORTS_KEPT, the closed going first */
	for (index = 0; index < SERVER_REPORTS_KEPT + 5; index++)
	{
		memset(&report, 0, sizeof(report));
		report.reason = SERVER_REPORT_OTHER;
		snprintf(report.target.name, sizeof(report.target.name), "p%u", index);
		CHECK(server_reports_add(&report));
	}
	CHECK(server_reports_open() == SERVER_REPORTS_KEPT);
	CHECK(!strcmp(server_report_reason_name(SERVER_REPORT_GRIEFING), "griefing or betraying") &&
		!strcmp(server_report_reason_name(99), "?"));
}

int main(int argc, char **argv)
{
	data_root = argc > 1 ? argv[1] : ".";
	test_lines();
	test_share();
	test_shared();
	test_reports();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
