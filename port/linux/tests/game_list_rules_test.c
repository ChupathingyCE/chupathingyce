/*
GAME_LIST_RULES_TEST.C

Tests of which listed games the game lists show (port/linux/src/
game_list_rules.h), with sample advertisements: the network version a host
announces, whether it speaks Delta, and this build's range. Built and run
by tools/test_game_list_rules.py; exits nonzero on a failure.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "../src/game_list_rules.h"
#include "../src/server_names.h"

#include <stdio.h>
#include <string.h>

/* the built-in blocked names list (delta_chat_words.c) */
extern const char delta_chat_blocked_words[];

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* (a release's range: the signed table's chupa-44a row, 11 to 45) */
enum { MINIMUM = 11, MAXIMUM = 45 };

static int shown(unsigned int version, int delta)
{
	return game_list_joinable(version, delta, MINIMUM, MAXIMUM);
}

int main(void)
{
	/* OpenCE hosts in the range: 41 to 44 today, older ones too */
	CHECK(shown(41, 0));
	CHECK(shown(42, 0));
	CHECK(shown(43, 0));
	CHECK(shown(44, 0));
	CHECK(shown(11, 0));
	CHECK(shown(26, 0));
	/* (the edges) */
	CHECK(shown(MINIMUM, 0));
	CHECK(shown(MAXIMUM, 0));
	/* OpenCE's newer numbers, past the range: hidden */
	CHECK(!shown(46, 0));
	CHECK(!shown(60, 0));
	/* older than the range, and no number at all: hidden */
	CHECK(!shown(10, 0));
	CHECK(!shown(0, 0));
	/* Delta hosts: their wire decides on joining, whatever number they
	announce for OpenCE's builds (a console server's 32, a newer 46) */
	CHECK(shown(32, 1));
	CHECK(shown(44, 1));
	CHECK(shown(46, 1));
	CHECK(shown(0, 1));
	/* a GOAT H1 game closed to other builds: version | 0x8000, never shown,
	whatever else it says */
	CHECK(!shown(0x8000u | 41, 0));
	CHECK(!shown(0x8000u | 44, 0));
	CHECK(!shown(0x8000u | 41, 1));
	CHECK(!shown(0x8000u, 0));
	CHECK(!shown(0xFFFFu, 0));
	/* a GOAT H1 game open to us advertises its plain number: by the range */
	CHECK(shown(41, 0));
	/* a range a table widened */
	CHECK(game_list_joinable(47, 0, 11, 47));
	CHECK(!game_list_joinable(48, 0, 11, 47));
	/* broker listings: only 35 and newer are listed */
	CHECK(GAME_LIST_BROKER_MINIMUM_VERSION == 35u);
	CHECK(!game_list_broker_listed(0));
	CHECK(!game_list_broker_listed(26));
	CHECK(!game_list_broker_listed(34));
	CHECK(game_list_broker_listed(35));
	CHECK(game_list_broker_listed(44));
	/* (the closed bit is not a newer number) */
	CHECK(!game_list_broker_listed(0x8000u | 26));
	/* blocked names, as the site compares them (case, look-alikes, spaces,
	digits at the end, repeated letters); the list's words are in ROT13 */
	{
		static struct server_names_list list, other;

		CHECK(server_names_list_parse(&list, delta_chat_blocked_words, strlen(delta_chat_blocked_words)) > 40);
		CHECK(server_names_list_blocked_text(&list, "Adolf Hitler"));
		CHECK(server_names_list_blocked_text(&list, "ADOLF H1TL3R"));
		CHECK(server_names_list_blocked_text(&list, "h i t l e r 88"));
		CHECK(server_names_list_blocked_text(&list, "Hiiitler"));
		CHECK(server_names_list_blocked_text(&list, "\xd0\xbditler"));
		CHECK(!server_names_list_blocked_text(&list, "Halo Night"));
		CHECK(!server_names_list_blocked_text(&list, "Blood Gulch 24/7"));
		CHECK(!server_names_list_blocked_text(&list, ""));
		/* a list of its own, apart from another */
		CHECK(server_names_list_parse(&other, "whole uryyb\n", 12) == 1);
		CHECK(server_names_list_blocked_text(&other, "hello"));
		CHECK(!server_names_list_blocked_text(&other, "hello there"));
		CHECK(!server_names_list_blocked_text(&list, "hello"));
		CHECK(!server_names_list_blocked_text(NULL, "Adolf Hitler"));
	}
	if (failures)
		printf("%d failed\n", failures);
	return failures != 0;
}
