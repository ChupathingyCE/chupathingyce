/* spv3_installer_test.c: the SPV3 installer (port/linux/src/spv3_installer.c)
run once on the fuzz target's test release, by tools/test_spv3_installer.py,
which sets up the data folder for each case and checks it afterwards.

Usage: spv3_installer_test <release.zip> <data folder> [cancel-first]
  cancel-first: Cancel pressed at the first file's progress (while the files
  there are checked, when they are)
Prints "ok", or "error: <the line the player sees>". */

#include "spv3_installer.h"

#include <stdio.h>
#include <string.h>

void platform_log(const char *format, ...)
{
	(void)format;
}

static void cancel_first(void *context, const char *file, unsigned long long done, unsigned long long total)
{
	(void)context;
	(void)done;
	(void)total;
	if (strcmp(file, "manifest.bin") != 0)
		spv3_installer_cancel();
}

int main(int argc, char **argv)
{
	char error[512] = "";
	int ok;

	if (argc < 3)
		return 2;
	ok = spv3_installer_install(argv[1], argv[2], argc > 3 && !strcmp(argv[3], "cancel-first") ? cancel_first : NULL,
		NULL, error, sizeof(error));
	if (ok)
		printf("ok\n");
	else
		printf("error: %s\n", error);
	return 0;
}
