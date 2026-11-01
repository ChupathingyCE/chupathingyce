/*
MAP_REPORT_TEST.C

map_report.c's header reading and JSON, run by tools/test_map_report.py:
prints each case's JSON (or "refused") for the test to parse and check.
*/

#include <stdio.h>
#include <string.h>

#include "map_report.h"

static unsigned char header[MAP_REPORT_HEADER_SIZE];

static void show(const struct map_report *report, int size)
{
	char out[2048];
	int length = map_report_json(report, out, size);

	if (length)
		printf("%s\n", out);
	else
		printf("refused\n");
}

int main(void)
{
	struct map_report report;
	int index;

	memset(&report, 0, sizeof(report));
	/* not a header */
	printf("%d\n", map_report_read_header(header, sizeof(header), &report));
	memcpy(header, "daeh", 4);
	header[4] = 0x61;
	header[5] = 0x02;
	memcpy(header + 0x40, "01.00.00.0609", 13);
	header[0x64] = 0x23;
	header[0x65] = 0x27;
	header[0x66] = 0x37;
	header[0x67] = 0xC5;
	memcpy(header + 0x7FC, "toof", 4);
	printf("%d\n", map_report_read_header(header, sizeof(header), &report));
	printf("%d\n", map_report_read_header(header, 100, &report));

	/* a Custom Edition map, whole */
	report.family = 1;
	strcpy(report.file, "extinction");
	report.size_low = 97888771UL;
	report.has_hash = 1;
	for (index = 0; index < MAP_REPORT_HASH_SIZE; index++)
		report.hash[index] = (unsigned char)index;
	strcpy(report.scenario, "levels\\extinction\\\"x\"\x01\xe9");
	report.gametypes = MAP_REPORT_SLAYER | MAP_REPORT_CTF | MAP_REPORT_RACE;
	report.spawns = 61;
	report.has_bounds = 1;
	report.bounds[0] = -10.5f;
	report.bounds[1] = 20.25f;
	report.bounds[2] = -3.0f;
	report.bounds[3] = 4.0f;
	report.bounds[4] = 0.0f;
	report.bounds[5] = 1.0e30f;
	show(&report, 2048);
	/* too small a buffer */
	show(&report, 40);
	/* a file over 4 GB */
	report.size_high = 1;
	report.has_bounds = 0;
	show(&report, 2048);
	/* without its hash */
	report.has_hash = 0;
	show(&report, 2048);
	/* an Xbox map needs none, nor a size */
	report.family = 0;
	report.size_low = 0;
	report.size_high = 0;
	show(&report, 2048);
	/* a name MAP could not carry, a family past the known ones */
	strcpy(report.file, "../x");
	show(&report, 2048);
	strcpy(report.file, "x");
	report.family = 9;
	show(&report, 2048);
	return 0;
}
