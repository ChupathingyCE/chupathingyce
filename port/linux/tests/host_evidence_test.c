/*
HOST_EVIDENCE_TEST.C

Tests of the host's refusal counts (port/linux/game/host_evidence.c): each
kind counted for its machine alone, a machine's and every machine's reset,
indices and kinds out of range ignored, a count that stops, and the
summary sv_evidence and the control panel show. tools/test_host_evidence.py
builds and runs them.
*/

#include "../game/host_evidence.h"

#include <stdio.h>
#include <string.h>

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

static void test_counts(void)
{
	host_evidence_reset();
	CHECK(host_evidence_total(0) == 0);
	host_evidence_note(3, _host_evidence_movement);
	host_evidence_note(3, _host_evidence_movement);
	host_evidence_note(3, _host_evidence_hit_report);
	host_evidence_note(4, _host_evidence_clock);
	CHECK(host_evidence_count(3, _host_evidence_movement) == 2);
	CHECK(host_evidence_count(3, _host_evidence_hit_report) == 1);
	CHECK(host_evidence_count(3, _host_evidence_clock) == 0);
	CHECK(host_evidence_total(3) == 3);
	CHECK(host_evidence_count(4, _host_evidence_clock) == 1);
	CHECK(host_evidence_total(4) == 1);
	/* (line of sight, and the kind kept for later checks, count the same) */
	host_evidence_note(5, _host_evidence_line_of_sight);
	host_evidence_note(5, _host_evidence_malformed);
	CHECK(host_evidence_count(5, _host_evidence_line_of_sight) == 1);
	CHECK(host_evidence_count(5, _host_evidence_malformed) == 1);
}

static void test_out_of_range(void)
{
	host_evidence_reset();
	host_evidence_note(-1, _host_evidence_movement);
	host_evidence_note(HOST_EVIDENCE_MACHINES, _host_evidence_movement);
	host_evidence_note(0, NUMBER_OF_HOST_EVIDENCE_KINDS);
	host_evidence_note(0, (enum host_evidence_kind)-1);
	CHECK(host_evidence_total(0) == 0);
	CHECK(host_evidence_total(-1) == 0);
	CHECK(host_evidence_total(HOST_EVIDENCE_MACHINES) == 0);
	CHECK(host_evidence_count(0, NUMBER_OF_HOST_EVIDENCE_KINDS) == 0);
	/* (the last machine there may be is counted) */
	host_evidence_note(HOST_EVIDENCE_MACHINES - 1, _host_evidence_vehicle);
	CHECK(host_evidence_count(HOST_EVIDENCE_MACHINES - 1, _host_evidence_vehicle) == 1);
	host_evidence_reset_machine(-1);
	host_evidence_reset_machine(HOST_EVIDENCE_MACHINES);
	CHECK(host_evidence_total(HOST_EVIDENCE_MACHINES - 1) == 1);
}

static void test_reset(void)
{
	host_evidence_reset();
	host_evidence_note(1, _host_evidence_vehicle);
	host_evidence_note(2, _host_evidence_vehicle);
	/* (a new machine at an index: that index alone) */
	host_evidence_reset_machine(1);
	CHECK(host_evidence_total(1) == 0);
	CHECK(host_evidence_total(2) == 1);
	/* (a new game: every machine) */
	host_evidence_reset();
	CHECK(host_evidence_total(2) == 0);
}

static void test_maximum(void)
{
	unsigned long index;

	host_evidence_reset();
	for (index = 0; index < HOST_EVIDENCE_MAXIMUM_COUNT + 10; index++)
		host_evidence_note(0, _host_evidence_movement);
	CHECK(host_evidence_count(0, _host_evidence_movement) == HOST_EVIDENCE_MAXIMUM_COUNT);
}

static void test_names_and_summary(void)
{
	char text[HOST_EVIDENCE_SUMMARY_SIZE];
	char small[12];

	CHECK(!strcmp(host_evidence_kind_name(_host_evidence_movement), "movement"));
	CHECK(!strcmp(host_evidence_kind_name(_host_evidence_vehicle), "vehicle"));
	CHECK(!strcmp(host_evidence_kind_name(_host_evidence_hit_report), "hit_report"));
	CHECK(!strcmp(host_evidence_kind_name(_host_evidence_clock), "clock"));
	CHECK(!strcmp(host_evidence_kind_name(_host_evidence_line_of_sight), "line_of_sight"));
	CHECK(!strcmp(host_evidence_kind_name(_host_evidence_malformed), "malformed"));
	CHECK(host_evidence_kind_name(NUMBER_OF_HOST_EVIDENCE_KINDS) == NULL);
	CHECK(host_evidence_kind_name(-1) == NULL);

	host_evidence_reset();
	CHECK(host_evidence_summary(0, text, sizeof(text)) == 4 && !strcmp(text, "none"));
	host_evidence_note(0, _host_evidence_movement);
	host_evidence_note(0, _host_evidence_movement);
	host_evidence_note(0, _host_evidence_movement);
	host_evidence_note(0, _host_evidence_hit_report);
	CHECK(host_evidence_summary(0, text, sizeof(text)) == (int)strlen("movement 3, hit_report 1"));
	CHECK(!strcmp(text, "movement 3, hit_report 1"));
	/* (cut short at a whole kind, never in the middle of one) */
	host_evidence_summary(0, small, sizeof(small));
	CHECK(!strcmp(small, "movement 3"));
	CHECK(host_evidence_summary(0, NULL, 4) == 0);
}

int main(void)
{
	test_counts();
	test_out_of_range();
	test_reset();
	test_maximum();
	test_names_and_summary();
	if (failures)
	{
		printf("host_evidence_test: %d failures\n", failures);
		return 1;
	}
	printf("host_evidence_test: ok\n");
	return 0;
}
