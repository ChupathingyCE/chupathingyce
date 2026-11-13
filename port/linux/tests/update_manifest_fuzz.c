/*
UPDATE_MANIFEST_FUZZ.C

libFuzzer's entry for the self-updater's lists (update_manifest.c; tools/
test_host_fuzz.py builds it, target update_manifest): a release's list, a
channel's feed and its file, and the version words, on any bytes. The list
comes in an allocation of exactly its size (the reader has its length, not
a terminator); the versions are its first line.
*/

#include "../src/update_manifest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char tag[64], name[128], sha256[UPDATE_SHA256_TEXT_SIZE], main_name[128], error[256], version[128], first[256];
	unsigned long long file_size = 0;
	char *text;
	size_t line;
	int comparable;

	text = (char *)malloc(size ? size : 1);
	if (!text)
		return 0;
	memcpy(text, data, size);
	update_manifest_find(text, size, "0.9.1", "linux", "release", "zip", tag, sizeof(tag), name, sizeof(name),
		&file_size, sha256, sizeof(sha256), main_name, sizeof(main_name), error, sizeof(error));
	update_manifest_about(text, size, "testing", version, sizeof(version), error, sizeof(error));
	update_manifest_find_channel(text, size, "nightly", "0.9.1-nightly.202611101530", "windows", "debug", "appimage",
		tag, sizeof(tag), name, sizeof(name), &file_size, sha256, sizeof(sha256), main_name, sizeof(main_name),
		error, sizeof(error));
	/* (small sizes too: every copy-out checks the room it has) */
	update_manifest_find(text, size, "0.9.1", "linux", "release", "zip", tag, 2, name, 2, &file_size, sha256,
		sizeof(sha256), main_name, 1, error, 8);
	line = 0;
	while (line < size && line + 1 < sizeof(first) && data[line] != '\n')
	{
		first[line] = (char)data[line];
		line++;
	}
	first[line] = 0;
	update_version_compare(first, "0.9.1a-testing.202611101530", &comparable);
	update_version_newer(first, "0.9.0");
	update_version_release(first);
	update_version_prerelease(first, version, 4);
	update_sha256(data, size, sha256);
	free(text);
	return 0;
}
