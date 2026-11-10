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
#include "../platform/control_protocol.h"
#include "monocypher.h"

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

/* a ban's ref: the line keyed with this server's own key, never the line's
plain hash (which anyone could test guesses of its address against); the
same line keeps its ref, another server's key gives another */
static void test_refs(void)
{
	static const char line[] = "2026-11-09 10:00:00\tip=1.2.3.4\thwid=abc\tplayers=x\treason=r\t"
		"account=0123456789abcdef0123456789abcdef\n";
	struct server_shared_ban ban, again;
	unsigned char plain[16];
	char plain_text[33], path[1024];
	FILE *file;

	CHECK(server_bans_line_entry(line, 100, &ban) && server_bans_line_entry(line, 100, &again) &&
		!strcmp(ban.ref, again.ref));
	crypto_blake2b(plain, sizeof(plain), (const uint8_t *)line, strlen(line) - 1);
	control_hex_text(plain, sizeof(plain), plain_text);
	CHECK(strcmp(ban.ref, plain_text) != 0);
	snprintf(path, sizeof(path), "%s/shared_bans.key", data_root);
	file = fopen(path, "rb");
	CHECK(file != NULL);
	if (file)
	{
		unsigned char key[64];

		CHECK(fread(key, 1, sizeof(key), file) == 32);
		fclose(file);
	}
}

/* sv_unban S<digits>: an entry found by its ref's first digits, never by a
place in the list */
static void test_find(void)
{
	static struct server_shared_ban bans[3];
	struct server_shared_ban got;
	int index;

	memset(bans, 0, sizeof(bans));
	snprintf(bans[0].ref, sizeof(bans[0].ref), "aaaaaaaa00000000000000000000000a");
	snprintf(bans[1].ref, sizeof(bans[1].ref), "aaaaaaab00000000000000000000000b");
	snprintf(bans[2].ref, sizeof(bans[2].ref), "aaaaaaab11111111111111111111111c");
	for (index = 0; index < 3; index++)
	{
		bans[index].has_device = 1;
		memset(bans[index].device, 0x10 + index, 16);
	}
	server_bans_set_shared(bans, 3, 1, 0);
	CHECK(server_bans_shared_find("aaaaaaaa", &got, NULL) && !strcmp(got.ref, bans[0].ref));
	/* (two share the digits: neither, until enough are given) */
	CHECK(!server_bans_shared_find("aaaaaaab", &got, NULL));
	CHECK(server_bans_shared_find("aaaaaaab1", &got, NULL) && !strcmp(got.ref, bans[2].ref));
	/* (too few digits, or not lowercase hex) */
	CHECK(!server_bans_shared_find("aaaa", &got, NULL) && !server_bans_shared_find("AAAAAAAA", &got, NULL));
	/* the list sent again in another order: the same digits find the same ban */
	got = bans[0];
	bans[0] = bans[2];
	bans[2] = got;
	server_bans_set_shared(bans, 3, 2, 0);
	CHECK(server_bans_shared_find("aaaaaaaa", &got, NULL) && !strcmp(got.ref, "aaaaaaaa00000000000000000000000a"));
	/* exceptions full of bans no list has now: they go to make room */
	for (index = 0; index < 512; index++)
	{
		char ref[33];

		snprintf(ref, sizeof(ref), "%032x", index + 0x100);
		CHECK(server_bans_except(ref, 1));
	}
	CHECK(server_bans_except("aaaaaaaa00000000000000000000000a", 1));
	CHECK(server_bans_shared_find("aaaaaaaa", &got, &index) && index);
	CHECK(server_bans_except("aaaaaaaa00000000000000000000000a", 0));
	server_bans_set_shared(NULL, 0, 0, 0);
}

static void test_share(void)
{
	static char payload[256 * 1024];

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
	/* (the share sends at most 4 handles, each at most 31 characters) */
	{
		static char payload[8192];

		write_bans("x\taccount=0123456789abcdef0123456789abcdef\tprofile=a,b,c,d,e,"
			"abcdefghijabcdefghijabcdefghijabcdefghij\n");
		server_bans_set_sharing(1);
		server_bans_share_answered("x");
		CHECK(server_bans_share_payload(payload, sizeof(payload)) > 0 &&
			strstr(payload, "\"handles\": [\"a\", \"b\", \"c\", \"d\"]"));
		server_bans_set_sharing(0);
		write_bans("");
	}
	/* (many long bans: what a poll carries stays under its cap; the rest
	go when there is room) */
	{
		char path[1024];
		FILE *file;
		int index;

		snprintf(path, sizeof(path), "%s/bans.txt", data_root);
		file = fopen(path, "w");
		for (index = 0; file && index < 500; index++)
		{
			fprintf(file, "2026-11-09 10:00:00\tip=1.2.3.4\thwid=a\tplayers=x\treason=%0120d\taccount=%032x,"
				"%032x,%032x,%032x\tdevice=%032x\tprofile=aaaaaaaaaaaaaaaa,bbbbbbbbbbbbbbbb,cccccccccccccccc\n", index,
				index + 1, index + 2, index + 3, index + 4, index + 5);
		}
		if (file)
			fclose(file);
		server_bans_set_sharing(1);
		server_bans_share_answered("x");
		CHECK(server_bans_share_payload(payload, sizeof(payload)) > 0 && strlen(payload) < 100 * 1024);
		server_bans_set_sharing(0);
		write_bans("");
	}
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
	static char text[512 * 1024];
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
	/* a clip's token: the server's own, random, only when asked for */
	memset(&report, 0, sizeof(report));
	report.reason = SERVER_REPORT_CHEATING;
	report.target.delta = 1;
	snprintf(report.clip, sizeof(report.clip), "00000000000000000000000000000000");
	CHECK(server_reports_add(&report) && !report.clip[0]);
	report.want_clip = 1;
	CHECK(server_reports_add(&report) && strlen(report.clip) == 32 &&
		strspn(report.clip, "0123456789abcdef") == 32 && strcmp(report.clip, "00000000000000000000000000000000"));
	{
		char first_clip[SERVER_REPORT_ID_SIZE];

		snprintf(first_clip, sizeof(first_clip), "%s", report.clip);
		CHECK(server_reports_add(&report) && strcmp(report.clip, first_clip));
	}
	/* the queue keeps SERVER_REPORTS_KEPT: the closed go first, and an open
	one never goes for a new one (a flood is refused instead) */
	{
		unsigned int taken = 0;

		for (index = 0; index < SERVER_REPORTS_KEPT + 5; index++)
		{
			memset(&report, 0, sizeof(report));
			report.reason = SERVER_REPORT_OTHER;
			snprintf(report.target.name, sizeof(report.target.name), "p%u", index);
			taken += server_reports_add(&report) ? 1 : 0;
		}
		/* (room for 195 open beside the 5 kept, and the closed one's place) */
		CHECK(taken == SERVER_REPORTS_KEPT - 5 + 1);
	}
	CHECK(server_reports_open() == SERVER_REPORTS_KEPT);
	server_reports_text(1, 1, text, sizeof(text));
	CHECK(strstr(text, "OpenGuy") && !strstr(text, "closed by Odb") && strstr(text, "\"p0\"") && !strstr(text, "\"p199\""));
	CHECK(!strcmp(server_report_reason_name(SERVER_REPORT_GRIEFING), "griefing or betraying") &&
		!strcmp(server_report_reason_name(99), "?"));
}

/* the limits on reports: by who reports and who is reported, never by a
machine's slot */
static void test_report_limits(void)
{
	struct server_report_who a, b, c, verified, verified_back, target, other;
	long long now = 1000000;
	int index;

	memset(&a, 0, sizeof(a));
	b = c = target = other = verified = a;
	a.player = 0x10001;
	b.player = 0x20001;
	c.player = 0x30001;
	target.player = 0x40002;
	other.player = 0x50003;
	verified.has_account = 1;
	memset(verified.account, 0x77, 16);
	verified.player = 0x60004;
	/* (the same account back as another player after leaving) */
	verified_back = verified;
	verified_back.player = 0x70005;
	server_reports_limit_reset();
	CHECK(server_reports_limit_check(&a, &target, now) == SERVER_REPORT_LIMIT_OK);
	server_reports_limit_count(&a, &target, now);
	/* (once a game a player, from one reporter) */
	CHECK(server_reports_limit_check(&a, &target, now + 100) == SERVER_REPORT_LIMIT_ALREADY);
	/* (a new player in the slot a left: their own counts, never a's) */
	CHECK(server_reports_limit_check(&b, &target, now + 100) == SERVER_REPORT_LIMIT_OK);
	/* (3 a minute) */
	server_reports_limit_count(&a, &other, now + 1);
	c.player = 0x30009;
	server_reports_limit_count(&a, &c, now + 2);
	b.player = 0x20009;
	CHECK(server_reports_limit_check(&a, &b, now + 30) == SERVER_REPORT_LIMIT_TOO_SOON);
	CHECK(server_reports_limit_check(&a, &b, now + 61) == SERVER_REPORT_LIMIT_OK);
	/* (a verified reporter is counted by their account: back after leaving,
	as another player, the counts are theirs still) */
	server_reports_limit_count(&verified, &target, now);
	CHECK(server_reports_limit_check(&verified_back, &target, now + 100) == SERVER_REPORT_LIMIT_ALREADY);
	/* (at most 5 of one player a game, from everyone) */
	for (index = 0; index < 3; index++)
	{
		struct server_report_who someone = a;

		someone.player = 0x80000 + index;
		CHECK(server_reports_limit_check(&someone, &target, now) == SERVER_REPORT_LIMIT_OK);
		server_reports_limit_count(&someone, &target, now);
	}
	{
		struct server_report_who sixth = a;

		sixth.player = 0x90000;
		CHECK(server_reports_limit_check(&sixth, &target, now) == SERVER_REPORT_LIMIT_ALREADY);
		CHECK(server_reports_limit_check(&sixth, &other, now) == SERVER_REPORT_LIMIT_OK);
	}
	/* (10 a game) */
	{
		struct server_report_who busy = a, someone = a;

		busy.player = 0xA0000;
		for (index = 0; index < SERVER_REPORTS_PER_GAME; index++)
		{
			someone.player = 0xB0000 + index;
			server_reports_limit_count(&busy, &someone, now + 100 * index);
		}
		someone.player = 0xC0000;
		CHECK(server_reports_limit_check(&busy, &someone, now + 100000) == SERVER_REPORT_LIMIT_TOO_SOON);
	}
	/* (no room to count a new reporter: refused, never a count forgotten) */
	server_reports_limit_reset();
	for (index = 0; index < SERVER_REPORTS_REPORTERS_KEPT; index++)
	{
		struct server_report_who someone = a, someone_else = a;

		someone.player = 0x100000 + index;
		someone_else.player = 0x200000 + (index % 50);
		server_reports_limit_count(&someone, &someone_else, now);
	}
	{
		struct server_report_who late = a;

		late.player = 0x300000;
		CHECK(server_reports_limit_check(&late, &other, now) == SERVER_REPORT_LIMIT_TOO_SOON);
	}
	/* (a new game: they start over) */
	server_reports_limit_reset();
	CHECK(server_reports_limit_check(&a, &target, now) == SERVER_REPORT_LIMIT_OK);
}

int main(int argc, char **argv)
{
	data_root = argc > 1 ? argv[1] : ".";
	test_lines();
	test_refs();
	test_find();
	test_share();
	test_shared();
	test_reports();
	test_report_limits();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
