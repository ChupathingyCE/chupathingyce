/*
ARK_FILES_TEST.C

Where Project Ark's files are found (port/linux/src/ark_files.c): run as
ark_files_test <data root> <MCC install, or ""> <expected>, where expected
is "none", "local" or the folder found in the install. Built and run by
tools/test_ark_mode.py; exits nonzero on a failure. The platform and Steam
calls the unit makes are given here: the data root and one MCC install.
*/

#include "../src/ark_files.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *data_root;
static const char *install;

const char *platform_data_root(void)
{
	return data_root;
}

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

int steam_workshop_mcc_install(int which, char *path, unsigned long size)
{
	if (which != 0 || !install[0] || strlen(install) >= size)
		return 0;
	snprintf(path, size, "%s", install);
	return 1;
}

int main(int argc, char **argv)
{
	char found[1024];
	int have;

	if (argc != 4)
	{
		printf("usage: ark_files_test <data root> <MCC install> <expected>\n");
		return 2;
	}
	data_root = argv[1];
	install = argv[2];
	have = ark_files_folder(found, sizeof(found));
	if (!strcmp(argv[3], "none"))
	{
		if (have)
		{
			printf("FAIL: found %s, expected none\n", found);
			return 1;
		}
		return 0;
	}
	if (!have)
	{
		printf("FAIL: found none, expected %s\n", argv[3]);
		return 1;
	}
	if (!strcmp(argv[3], "local"))
	{
		if (!ark_files_local())
		{
			printf("FAIL: found %s, not the data root's\n", found);
			return 1;
		}
		return 0;
	}
	if (ark_files_local() || strcmp(found, argv[3]))
	{
		printf("FAIL: found %s, expected %s\n", found, argv[3]);
		return 1;
	}
	return 0;
}
