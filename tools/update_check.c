/*
UPDATE_CHECK.C

A check of the self-updater's list of update files and signatures
(port/linux/src/update_manifest.c, update_signature.c), built by
tools/test_update_manifest.py with a test key (HALO_UPDATE_TEST_KEY). It
does what it is told and prints the answer:

	find LIST VERSION PLATFORM CONFIGURATION KIND
	              "found TAG NAME SIZE SHA256 [MAIN]", "missing: WHY" or "refused: WHY"
	document FILE SIGNATURE
	              "signed" or "not signed: WHY" (the file's exact bytes)
	zip FILE ASSET VERSION SIGNATURE
	              "signed" or "not signed: WHY" (a zip's .sig, as before the list)
	sha256 FILE   the file's SHA-256
*/

#include "update_manifest.h"
#include "update_signature.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_file(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data;
	long length;

	if (!file || fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET))
	{
		fprintf(stderr, "cannot read %s\n", path);
		exit(2);
	}
	data = malloc((size_t)length + 1);
	if (!data || fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		fprintf(stderr, "cannot read %s\n", path);
		exit(2);
	}
	fclose(file);
	*size = (size_t)length;
	return data;
}

int main(int argc, char **argv)
{
	char error[256] = "";
	size_t size, signature_size;
	unsigned char *data, *signature;

	if (argc == 7 && !strcmp(argv[1], "find"))
	{
		char tag[160], name[160], main_name[160], sha256[UPDATE_SHA256_TEXT_SIZE];
		unsigned long long file_size = 0;
		int result;

		data = read_file(argv[2], &size);
		result = update_manifest_find((const char *)data, size, argv[3], argv[4], argv[5], argv[6], tag, sizeof(tag),
			name, sizeof(name), &file_size, sha256, sizeof(sha256), main_name, sizeof(main_name), error, sizeof(error));
		if (result == _update_manifest_found)
			printf("found %s %s %llu %s%s%s\n", tag, name, file_size, sha256, main_name[0] ? " " : "", main_name);
		else
			printf("%s: %s\n", result == _update_manifest_missing ? "missing" : "refused", error);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "document"))
	{
		data = read_file(argv[2], &size);
		signature = read_file(argv[3], &signature_size);
		if (update_signature_check_document(data, size, (const char *)signature, signature_size, error,
			sizeof(error)))
		{
			printf("signed\n");
		}
		else
			printf("not signed: %s\n", error);
		return 0;
	}
	if (argc == 6 && !strcmp(argv[1], "zip"))
	{
		data = read_file(argv[2], &size);
		signature = read_file(argv[5], &signature_size);
		if (update_signature_check(data, size, argv[3], argv[4], (const char *)signature, signature_size, error,
			sizeof(error)))
		{
			printf("signed\n");
		}
		else
			printf("not signed: %s\n", error);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "sha256"))
	{
		char hex[UPDATE_SHA256_TEXT_SIZE];

		data = read_file(argv[2], &size);
		update_sha256(data, size, hex);
		printf("%s\n", hex);
		return 0;
	}
	fprintf(stderr, "usage: see update_check.c\n");
	return 2;
}
