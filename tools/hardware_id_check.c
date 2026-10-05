/*
HARDWARE_ID_CHECK.C

A check of the hardware id a joining machine tells a host
(port/linux/src/hardware_id.c), built with the platform layer's flags by
tools/test_linux_port.py and run with two empty folders as its arguments:
a machine's id hashes as it always has (so Windows' and Linux's ids stay
what they were); one without makes its install's id once in the save root,
the same on every run there and another in another save root; a damaged
file is made again; a machine with an id writes no file; and this machine's
own id, where the system has one, reads the same twice. Prints PASS or the
failures.
*/

#include "p2p_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int hardware_id_machine_source(char *text, int size);
int hardware_id_install_source(const char *root, char *text, int size);
int hardware_id_of(const char *machine, const char *root, char *hex);

/* ---------- stand-ins for the rest of the platform layer */

void posix_random_bytes(void *buffer, unsigned long size)
{
	FILE *file = fopen("/dev/urandom", "rb");

	if (!file || fread(buffer, 1, size, file) != size)
		abort();
	fclose(file);
}

void platform_log(const char *format, ...) { (void)format; }
const char *platform_save_root(void) { return ""; }

/* ---------- the check */

static int failures;

static void expect(int condition, const char *what)
{
	if (!condition)
	{
		printf("failed: %s\n", what);
		failures++;
	}
}

static int file_exists(const char *root)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/hardware_id.key", root);
	file = fopen(path, "rb");
	if (file)
		fclose(file);
	return file != NULL;
}

int main(int argc, char **argv)
{
	char first[P2P_HARDWARE_ID_SIZE], second[P2P_HARDWARE_ID_SIZE], other[P2P_HARDWARE_ID_SIZE];
	char machine[256], again[256];
	char path[1024];
	FILE *file;

	if (argc != 3)
	{
		printf("usage: %s <empty folder> <another empty folder>\n", argv[0]);
		return 2;
	}

	/* a machine's id: the hash the game has always told (HMAC-SHA256 with
	its key, 16 bytes as hex); no file is made for it */
	expect(hardware_id_of("0123456789abcdef0123456789abcdef", argv[1], first) == 2, "a machine's id is used");
	expect(!strcmp(first, "3dd0fbf156d188d3eb69294cb78aa058"), "a machine's id hashes as it always has");
	expect(!file_exists(argv[1]), "a machine with an id writes no install id");
	expect(hardware_id_of("0123456789abcdef0123456789abcdef", argv[2], second) == 2 && !strcmp(first, second),
		"a machine's id is the same in any save root");

	/* none: the install's, made once in the save root */
	expect(hardware_id_of(NULL, argv[1], first) == 1, "an install id is made");
	expect(strlen(first) == 32 && strspn(first, "0123456789abcdef") == 32, "the id is 32 lowercase hex digits");
	expect(file_exists(argv[1]), "the install id is kept in the save root");
	expect(hardware_id_of("", argv[1], second) == 1 && !strcmp(first, second), "the same save root, the same id");
	expect(hardware_id_of(NULL, argv[2], other) == 1 && strcmp(first, other), "another save root, another id");

	/* the install id's hash, labelled apart from any machine's */
	snprintf(path, sizeof(path), "%s/hardware_id.key", argv[2]);
	file = fopen(path, "wb");
	expect(file != NULL, "the install id can be written");
	if (file)
	{
		fprintf(file, "%s\n", "abababababababababababababababababababababababababababababababab");
		fclose(file);
	}
	expect(hardware_id_of(NULL, argv[2], other) == 1 && !strcmp(other, "70c2a852f1fe68ef168db984f5360676"),
		"an install id hashes with its label");

	/* a damaged file: made again */
	file = fopen(path, "wb");
	if (file)
	{
		fputs("not an id\n", file);
		fclose(file);
	}
	expect(hardware_id_of(NULL, argv[2], other) == 1 && strlen(other) == 32 &&
		strcmp(other, "70c2a852f1fe68ef168db984f5360676"), "a damaged install id is made again");
	expect(hardware_id_of(NULL, argv[2], second) == 1 && !strcmp(other, second), "and kept from then on");

	/* no save root: none */
	expect(hardware_id_of(NULL, "", other) == 0 && !other[0], "no save root and no machine id: none");

	/* this machine's own id, if the system has one: the same each time */
	if (hardware_id_machine_source(machine, sizeof(machine)))
	{
		expect(hardware_id_machine_source(again, sizeof(again)) && !strcmp(machine, again),
			"this machine's id reads the same twice");
		printf("this machine has a system id\n");
	}
	else
		printf("this machine has no system id\n");

	printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
