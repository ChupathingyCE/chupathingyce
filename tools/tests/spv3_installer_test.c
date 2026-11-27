/* spv3_installer_test.c: the SPV3 installer (port/linux/src/spv3_installer.c)
run once on the fuzz target's test release, by tools/test_spv3_installer.py,
which sets up the data folder for each case and checks it afterwards.

Usage: spv3_installer_test <release.zip> <data folder> [cancel-first]
  cancel-first: Cancel pressed at the first file's progress (while the files
  there are checked, when they are)
Prints "ok", or "error: <the line the player sees>".
   spv3_installer_test known-lua <path under the data folder> <file>
Prints "known" if the file is one of the installer's Lua files (that path, its
size and CRC-32: what SPV3 mode runs without lua.scripts), else "unknown". */

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

	if (argc == 4 && !strcmp(argv[1], "known-lua"))
	{
		static unsigned char data[1 << 20];
		FILE *file = fopen(argv[3], "rb");
		size_t size = file ? fread(data, 1, sizeof(data), file) : 0;

		if (file)
			fclose(file);
		printf("%s\n", file && spv3_installer_known_lua(argv[2], data, size) ? "known" : "unknown");
		return 0;
	}
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
