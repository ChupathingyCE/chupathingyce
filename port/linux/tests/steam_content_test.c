/*
STEAM_CONTENT_TEST.C

Tests of the Steam content client (port/linux/src/steam_content): the
protobuf codec, a chunk's decode (decrypt, LZMA and Zstandard, SHA-1) and
a manifest's (encrypted names) from the recorded fixtures in
steam_content/ (tools/make_steam_content_fixtures.py), file names that
must not leave the item's folder, the logon message's LoginID, and the
player's words for Steam's errors. No network. Built and run by
tools/test_steam_content.py, from this folder.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/

#include "../src/steam_content/sc_internal.h"
#include "../src/steam_content/sc_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static unsigned char *read_file(const char *path, size_t *length)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data;
	long size;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = (unsigned char *)malloc((size_t)size + 1);
	if (data && fread(data, 1, (size_t)size, file) != (size_t)size)
	{
		free(data);
		data = NULL;
	}
	fclose(file);
	*length = (size_t)size;
	return data;
}

static void hex_bytes(const char *hex, unsigned char *out)
{
	int index;

	for (index = 0; index < 20; index++)
	{
		unsigned value;

		sscanf(hex + index * 2, "%2x", &value);
		out[index] = (unsigned char)value;
	}
}

static void test_proto(void)
{
	unsigned char buffer[64], inner[16];
	struct pb_writer writer, sub;
	struct pb_reader reader;
	struct pb_field field;
	const unsigned char *bytes;
	size_t length;
	char text[8];

	/* known encodings */
	pb_writer_init(&writer, buffer, sizeof(buffer));
	pb_put_varint(&writer, 1, 150);
	CHECK(writer.length == 3 && buffer[0] == 0x08 && buffer[1] == 0x96 && buffer[2] == 0x01);
	pb_writer_init(&writer, buffer, sizeof(buffer));
	pb_put_fixed32(&writer, 11, 0x43485501u);
	CHECK(writer.length == 5 && buffer[0] == 0x5D && buffer[1] == 0x01 && buffer[4] == 0x43);
	pb_writer_init(&writer, buffer, sizeof(buffer));
	pb_put_string(&writer, 2, "testing");
	CHECK(writer.length == 9 && buffer[0] == 0x12 && buffer[1] == 7 && !memcmp(buffer + 2, "testing", 7));

	/* a round trip, with a message inside, a 64-bit fixed and a negative int32 */
	pb_writer_init(&sub, inner, sizeof(inner));
	pb_put_varint(&sub, 1, 7);
	pb_writer_init(&writer, buffer, sizeof(buffer));
	pb_put_fixed64(&writer, 1, 0x0110000100000000ULL);
	pb_put_varint(&writer, 2, (uint64_t)(int64_t)-5);
	pb_put_bytes(&writer, 3, sub.data, sub.length);
	pb_put_varint(&writer, 300, 1);
	CHECK(!writer.failed);
	CHECK(pb_get_number(buffer, writer.length, 1, 0) == 0x0110000100000000ULL);
	CHECK(pb_int32(pb_get_number(buffer, writer.length, 2, 0)) == -5);
	CHECK(pb_get_bytes(buffer, writer.length, 3, &bytes, &length) && pb_get_number(bytes, length, 1, 0) == 7);
	CHECK(pb_get_number(buffer, writer.length, 300, 0) == 1);
	CHECK(pb_get_number(buffer, writer.length, 4, 99) == 99);
	CHECK(!pb_get_string(buffer, writer.length, 4, text, sizeof(text)) && text[0] == 0);

	/* a long string cut to the buffer */
	pb_writer_init(&writer, buffer, sizeof(buffer));
	pb_put_string(&writer, 1, "abcdefghijkl");
	CHECK(pb_get_string(buffer, writer.length, 1, text, sizeof(text)) && !strcmp(text, "abcdefg"));

	/* a full buffer fails, and says so */
	pb_writer_init(&writer, buffer, 4);
	pb_put_string(&writer, 1, "too long for it");
	CHECK(writer.failed && writer.length <= 4);

	/* broken messages: a length past the end, a truncated varint, a bad wire type, field 0 */
	{
		static const unsigned char past[] = { 0x0A, 0x05, 'a' };
		static const unsigned char truncated[] = { 0x08, 0x80 };
		static const unsigned char wire[] = { 0x0B, 0x00 };
		static const unsigned char zero[] = { 0x00, 0x01 };

		pb_reader_init(&reader, past, sizeof(past));
		CHECK(pb_next(&reader, &field) == -1);
		pb_reader_init(&reader, truncated, sizeof(truncated));
		CHECK(pb_next(&reader, &field) == -1);
		pb_reader_init(&reader, wire, sizeof(wire));
		CHECK(pb_next(&reader, &field) == -1);
		pb_reader_init(&reader, zero, sizeof(zero));
		CHECK(pb_next(&reader, &field) == -1);
		pb_reader_init(&reader, past, 0);
		CHECK(pb_next(&reader, &field) == 0);
	}
}

static const unsigned char test_key[32] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };

static void test_chunks(void)
{
	static const struct { const char *file; const char *sha; uint32_t size; } cases[] = {
		{ "steam_content/chunk_lzma.bin", "3b8bbfed7a234e049e3c9b228542a46b8328f09b", 6000 },
		{ "steam_content/chunk_zstd.bin", "22770b8cded40c2630823739656715a099248336", 3000 },
	};
	int index;

	for (index = 0; index < 2; index++)
	{
		struct sc_chunk chunk;
		size_t length;
		unsigned char *data = read_file(cases[index].file, &length), *out;
		char why[128];

		CHECK(data != NULL);
		if (!data)
			continue;
		memset(&chunk, 0, sizeof(chunk));
		hex_bytes(cases[index].sha, chunk.sha);
		chunk.size = cases[index].size;
		out = (unsigned char *)malloc(chunk.size);
		CHECK(sc_chunk_decode(test_key, data, length, &chunk, out, why, sizeof(why)));
		CHECK(!memcmp(out, "Blood Gulch ", 12));
		/* a changed byte: refused (padding, unpacking or SHA-1) */
		data[length / 2] ^= 1;
		CHECK(!sc_chunk_decode(test_key, data, length, &chunk, out, why, sizeof(why)));
		data[length / 2] ^= 1;
		/* the wrong SHA-1, the wrong size, the wrong key: refused */
		chunk.sha[0] ^= 1;
		CHECK(!sc_chunk_decode(test_key, data, length, &chunk, out, why, sizeof(why)));
		chunk.sha[0] ^= 1;
		chunk.size--;
		CHECK(!sc_chunk_decode(test_key, data, length, &chunk, out, why, sizeof(why)));
		chunk.size++;
		{
			unsigned char other[32];

			memcpy(other, test_key, 32);
			other[0] = 99;
			CHECK(!sc_chunk_decode(other, data, length, &chunk, out, why, sizeof(why)));
		}
		/* cut short: refused, never read past */
		CHECK(!sc_chunk_decode(test_key, data, length - 16, &chunk, out, why, sizeof(why)));
		CHECK(!sc_chunk_decode(test_key, data, 7, &chunk, out, why, sizeof(why)));
		free(out);
		free(data);
	}
}

static void test_manifest(void)
{
	struct sc_manifest manifest;
	size_t length;
	unsigned char *data = read_file("steam_content/manifest.zip", &length);
	char why[128];

	CHECK(data != NULL);
	if (!data)
		return;
	CHECK(sc_manifest_parse(data, length, test_key, &manifest, why, sizeof(why)));
	CHECK(manifest.file_count == 2 && manifest.chunk_count == 2 && manifest.total == 9000);
	if (manifest.file_count == 2 && manifest.chunk_count == 2)
	{
		CHECK(!strcmp(manifest.files[0].name, "maps") && (manifest.files[0].flags & SC_FILE_DIRECTORY));
		/* (Windows separators turned to '/') */
		CHECK(!strcmp(manifest.files[1].name, "maps/test.map") && manifest.files[1].size == 9000);
		CHECK(manifest.files[1].has_sha && manifest.files[1].first_chunk == 0 && manifest.files[1].chunk_count == 2);
		CHECK(manifest.chunks[1].offset == 6000 && manifest.chunks[1].size == 3000);
	}
	sc_manifest_free(&manifest);
	/* the wrong key: names don't decrypt */
	{
		unsigned char other[32];

		memcpy(other, test_key, 32);
		other[5] ^= 0x40;
		CHECK(!sc_manifest_parse(data, length, other, &manifest, why, sizeof(why)));
	}
	/* a broken zip */
	/* (the central directory's offset) */
	data[length - 6] ^= 0xFF;
	CHECK(!sc_manifest_parse(data, length, test_key, &manifest, why, sizeof(why)));
	CHECK(!sc_manifest_parse(data, 10, test_key, &manifest, why, sizeof(why)));
	free(data);
}

static void test_names(void)
{
	CHECK(sc_name_safe("maps/a.map"));
	CHECK(sc_name_safe("ModInfo.json"));
	CHECK(sc_name_safe("a/.hidden"));
	CHECK(!sc_name_safe(""));
	CHECK(!sc_name_safe("/etc/passwd"));
	CHECK(!sc_name_safe("../outside"));
	CHECK(!sc_name_safe("maps/../../outside"));
	CHECK(!sc_name_safe("maps/./a"));
	CHECK(!sc_name_safe("C:/Windows"));
	CHECK(!sc_name_safe("maps//a"));
	CHECK(!sc_name_safe("maps/"));
	CHECK(!sc_name_safe("a\\b"));
	CHECK(!sc_name_safe("bad\nname"));
	/* (Windows: devices with any extension, trailing dots and spaces, streams, its reserved marks) */
	CHECK(!sc_name_safe("CON"));
	CHECK(!sc_name_safe("maps/nul.txt"));
	CHECK(!sc_name_safe("Aux.map"));
	CHECK(!sc_name_safe("com1"));
	CHECK(!sc_name_safe("LPT9.log"));
	CHECK(sc_name_safe("COM0"));
	CHECK(sc_name_safe("console.txt"));
	CHECK(sc_name_safe("maps/null.map"));
	CHECK(!sc_name_safe("maps/a."));
	CHECK(!sc_name_safe("maps /a"));
	CHECK(!sc_name_safe("a.map:stream"));
	CHECK(!sc_name_safe("a?.map"));
	CHECK(!sc_name_safe("a|b"));
}

static void test_logon(void)
{
	unsigned char body[4096];
	const unsigned char *bytes;
	size_t length, size;

	/* the LoginID goes in both places, every time */
	length = sc_cm_logon_body(body, sizeof(body), "someone", "token.text", 0x9ABCDEF1u);
	CHECK(length > 0);
	CHECK(pb_get_number(body, length, 2, 0) == 0x9ABCDEF1u);
	CHECK(pb_get_bytes(body, length, 11, &bytes, &size) && pb_get_number(bytes, size, 1, 0) == 0x9ABCDEF1u);
	CHECK(pb_get_number(body, length, 8, 0) == 1);
	CHECK(pb_get_number(body, length, 1, 0) == SC_PROTOCOL_VERSION);
	CHECK(pb_get_bytes(body, length, 108, &bytes, &size) && size == 10 && !memcmp(bytes, "token.text", 10));
	CHECK(pb_get_bytes(body, length, 50, &bytes, &size) && size == 7);
	/* (a fixed32 on the wire, as Steam reads CMsgIPAddress.v4) */
	{
		struct pb_field field;

		CHECK(pb_get_bytes(body, length, 11, &bytes, &size) && pb_find(bytes, size, 1, &field) &&
			field.wire == PB_FIXED32);
	}
	/* never without one: no LoginID, no logon */
	CHECK(sc_cm_logon_body(body, sizeof(body), "someone", "token.text", 0) == 0);
	CHECK(sc_cm_logon_body(body, sizeof(body), "", "token.text", 5) == 0);
	CHECK(sc_cm_logon_body(body, sizeof(body), "someone", "", 5) == 0);
	CHECK(sc_cm_logon_body(body, 16, "someone", "token.text", 5) == 0);
}

static void test_errors(void)
{
	CHECK(!strcmp(steam_content_error_text(STEAM_CONTENT_NOT_OWNED), "Not owned"));
	CHECK(!strcmp(steam_content_error_text(STEAM_CONTENT_SIGN_IN), "Sign in"));
	CHECK(!strcmp(steam_content_error_text(STEAM_CONTENT_OFFLINE), "Offline"));
	CHECK(!strcmp(steam_content_error_text(STEAM_CONTENT_FAILED), "Failed"));
	CHECK(!strcmp(steam_content_error_text(12345), "Failed"));
	CHECK(!strcmp(steam_content_error_text(STEAM_CONTENT_OK), ""));
	/* the depot key refused: the account doesn't own MCC */
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_DEPOT_KEY, SC_RESULT_ACCESS_DENIED) ==
		STEAM_CONTENT_NOT_OWNED);
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_DEPOT_KEY, SC_RESULT_FAIL) == STEAM_CONTENT_NOT_OWNED);
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_MANIFEST, SC_RESULT_ACCESS_DENIED) ==
		STEAM_CONTENT_NOT_OWNED);
	/* the stored sign-in refused */
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_LOGON, SC_RESULT_EXPIRED) == STEAM_CONTENT_SIGN_IN);
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_LOGON, SC_RESULT_REVOKED) == STEAM_CONTENT_SIGN_IN);
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_LOGON, SC_RESULT_ACCESS_DENIED) == STEAM_CONTENT_SIGN_IN);
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_LOGON, SC_RESULT_INVALID_PASSWORD) ==
		STEAM_CONTENT_SIGN_IN);
	/* no answer, or Steam busy: offline */
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_LOGON, 0) == STEAM_CONTENT_OFFLINE);
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_LOGON, SC_RESULT_SERVICE_UNAVAILABLE) ==
		STEAM_CONTENT_OFFLINE);
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_DEPOT_KEY, SC_RESULT_TRY_ANOTHER_CM) ==
		STEAM_CONTENT_OFFLINE);
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_CONNECT, SC_RESULT_FAIL) == STEAM_CONTENT_OFFLINE);
	/* content servers */
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_CHUNK, 403) == STEAM_CONTENT_NOT_OWNED);
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_CHUNK, 404) == STEAM_CONTENT_FAILED);
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_CHUNK, 0) == STEAM_CONTENT_OFFLINE);
	/* anything else */
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_LOGON, 99) == STEAM_CONTENT_FAILED);
	CHECK(steam_content_error_from_result(STEAM_CONTENT_STEP_DEPOT_KEY, SC_RESULT_OK) == STEAM_CONTENT_OK);
}

/* the system's secret store, when asked (STEAM_CONTENT_TEST_SECRET=1: it may
ask the player on a Mac, so not by default): a token put, read back and
deleted (Windows: DPAPI; Linux: libsecret where there is a keyring) */
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>

/* an update's leftovers deleted, what the manifest has kept, nothing outside touched */
static void test_prune(void)
{
	static struct sc_file files[3];
	struct sc_manifest manifest;
	char folder[] = "prune_test";
	FILE *file;

	system("rm -rf prune_test prune_outside");
	mkdir(folder, 0755);
	mkdir("prune_test/maps", 0755);
	mkdir("prune_test/old", 0755);
	mkdir("prune_outside", 0755);
	file = fopen("prune_test/keep.txt", "w"); fclose(file);
	file = fopen("prune_test/stale.txt", "w"); fclose(file);
	file = fopen("prune_test/maps/a.map", "w"); fclose(file);
	file = fopen("prune_test/maps/gone.map", "w"); fclose(file);
	file = fopen("prune_test/old/x", "w"); fclose(file);
	file = fopen("prune_outside/precious", "w"); fclose(file);
	CHECK(symlink("../prune_outside", "prune_test/link") == 0);
	memset(&manifest, 0, sizeof(manifest));
	memset(files, 0, sizeof(files));
	snprintf(files[0].name, sizeof(files[0].name), "keep.txt");
	snprintf(files[1].name, sizeof(files[1].name), "maps");
	files[1].flags = SC_FILE_DIRECTORY;
	snprintf(files[2].name, sizeof(files[2].name), "maps/a.map");
	manifest.files = files;
	manifest.file_count = 3;
	sc_prune(folder, "", &manifest, 0);
	CHECK(access("prune_test/keep.txt", F_OK) == 0);
	CHECK(access("prune_test/maps/a.map", F_OK) == 0);
	CHECK(access("prune_test/stale.txt", F_OK) != 0);
	CHECK(access("prune_test/maps/gone.map", F_OK) != 0);
	CHECK(access("prune_test/old", F_OK) != 0);
	/* (the link removed, never followed: what it pointed at stays) */
	CHECK(access("prune_test/link", F_OK) != 0);
	CHECK(access("prune_outside/precious", F_OK) == 0);
	system("rm -rf prune_test prune_outside");
}
#else
static void test_prune(void) {}
#endif

static void test_secret_store(void)
{
	char token[64];

	if (!getenv("STEAM_CONTENT_TEST_SECRET"))
		return;
	snprintf(sc_host.data_folder, sizeof(sc_host.data_folder), ".");
	if (!sc_secret_store_name())
	{
		printf("no secret store here\n");
		return;
	}
	printf("secret store: %s\n", sc_secret_store_name());
	CHECK(sc_secret_put("test-account", "token-123.abc"));
	CHECK(sc_secret_get("test-account", token, sizeof(token)) && !strcmp(token, "token-123.abc"));
	sc_secret_delete("test-account");
	CHECK(!sc_secret_get("test-account", token, sizeof(token)));
}

int main(void)
{
	test_proto();
	test_chunks();
	test_manifest();
	test_names();
	test_logon();
	test_errors();
	test_secret_store();
	test_prune();
	if (failures)
	{
		fprintf(stderr, "%d failed\n", failures);
		return 1;
	}
	printf("steam_content tests passed\n");
	return 0;
}
