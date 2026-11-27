/*
SPV3_RELEASE_FUZZ.C

libFuzzer's entry for SPV3's release as the setup screen takes it
(port/linux/src/spv3_installer.c; tools/test_host_fuzz.py builds it): the
bytes are a file the player picked, written to a scratch folder, told apart
(spv3_installer_is_release) and installed into a scratch data folder. Nothing
may be written outside that folder, and nothing but SPV3's own files under
maps_spv3 and lua/spv3, which no input made here can match: the harness
checks the folder holds nothing else afterwards.
*/

#include "../src/spv3_installer.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void platform_log(const char *format, ...)
{
	(void)format;
}

static char folder[256];

/* every file under path removed; abort if any is not where SPV3's go */
static void sweep(const char *path, int depth)
{
	DIR *directory = opendir(path);
	struct dirent *entry;

	if (!directory)
		return;
	while ((entry = readdir(directory)) != NULL)
	{
		char child[1024];
		struct stat information;

		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
		if (lstat(child, &information) != 0)
			continue;
		if (S_ISDIR(information.st_mode))
		{
			sweep(child, depth + 1);
			rmdir(child);
		}
		else
		{
			/* (only empty folders may be left: no file can be SPV3's) */
			abort();
		}
	}
	closedir(directory);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char release[300], destination[300], error[512];
	FILE *file;

	if (!folder[0])
	{
		snprintf(folder, sizeof(folder), "%s/spv3_release_fuzz_%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp",
			(int)getpid());
		mkdir(folder, 0755);
	}
	snprintf(release, sizeof(release), "%s/release.zip", folder);
	snprintf(destination, sizeof(destination), "%s/data", folder);
	mkdir(destination, 0755);
	file = fopen(release, "wb");
	if (!file)
		return 0;
	if (size)
		fwrite(data, 1, size, file);
	fclose(file);
	if (spv3_installer_is_release(release))
		spv3_installer_install(release, destination, NULL, NULL, error, sizeof(error));
	sweep(destination, 0);
	remove(release);
	return 0;
}
