/*
MAP_REPO_TEST.C

Chupathingy maps' reader and client (port/linux/src/map_index.c,
map_repo.c) driven by tools/test_map_repo.py: built with a test's keys
(HALO_MAPS_TEST_KEY, HALO_MAPS_TEST_RECOVERY_KEY), its transport plain HTTP
to a fake repository on this machine (tools/test_map_repo.py's), and its
files the host's.

    map_repo_test index DOCUMENT SIGNATURE NOW
    map_repo_test mirrors DOCUMENT SIGNATURE NOW
    map_repo_test find DOCUMENT NAME VERSION
    map_repo_test blacklisted DOCUMENT SHA256|- MAP_HASH|-
    map_repo_test choose DOCUMENT SHA256
    map_repo_test download BASE_URL NOW NAME VERSION PATH [BASE_URL NOW]...
    map_repo_test blocked NOW SHA256|- MAP_HASH|- [BASE_URL]
    map_repo_test refreshes BASE_URL NOW[:index]...

With MAP_REPO_TEST_SAVE (a folder), what is kept between runs is kept
there, and read back first (its result printed as "load"); with
MAP_REPO_TEST_USE_AT (a time), the download and "has" are asked at that
time rather than the last refresh's; with MAP_REPO_TEST_INDEX_ONLY, the
refreshes ask for the index alone (as does a refresh's NOW:index).
*/

#include "map_index.h"
#include "map_repo.h"
#include "playlist_manifest.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* (what slurp read, freed at exit, as is the index held: the sanitizers'
leak check) */
static char *slurped[8];
static int slurped_count;

static void free_all(void)
{
	while (slurped_count)
		free(slurped[--slurped_count]);
	map_repo_reset();
}

static char *slurp(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *data;
	long length;

	if (!file)
	{
		fprintf(stderr, "no file %s\n", path);
		exit(2);
	}
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = (char *)malloc((size_t)length + 1);
	*size = fread(data, 1, (size_t)length, file);
	data[*size] = 0;
	fclose(file);
	if (slurped_count < (int)(sizeof(slurped) / sizeof(*slurped)))
		slurped[slurped_count++] = data;
	return data;
}

static int hex_hash(const char *text, unsigned char *hash)
{
	int index;

	if (strlen(text) != 64)
		return 0;
	for (index = 0; index < 32; index++)
	{
		unsigned int byte;

		if (sscanf(text + index * 2, "%2x", &byte) != 1)
			return 0;
		hash[index] = (unsigned char)byte;
	}
	return 1;
}

/* ---------- plain HTTP to this machine (the fake repository) */

struct response
{
	int status;
	char *body;
	size_t size;
};

static int http_get(const char *url, struct response *response, char *why, int why_size)
{
	struct sockaddr_in address;
	char request[512], *data = NULL, *end;
	size_t size = 0, capacity = 0;
	const char *host, *path;
	int port, sock;

	memset(response, 0, sizeof(*response));
	if (strncmp(url, "http://127.0.0.1:", 17))
	{
		snprintf(why, (size_t)why_size, "not this machine's address");
		return 0;
	}
	host = url + 17;
	port = atoi(host);
	path = strchr(host, '/');
	if (!path)
		path = "/";
	sock = socket(AF_INET, SOCK_STREAM, 0);
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons((unsigned short)port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (sock < 0 || connect(sock, (struct sockaddr *)&address, sizeof(address)) != 0)
	{
		if (sock >= 0)
			close(sock);
		snprintf(why, (size_t)why_size, "no answer");
		return 0;
	}
	snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n", path);
	if (write(sock, request, strlen(request)) < 0)
	{
		close(sock);
		snprintf(why, (size_t)why_size, "no answer");
		return 0;
	}
	for (;;)
	{
		ssize_t count;

		if (capacity - size < 65536)
		{
			capacity = capacity * 2 + 65536;
			data = (char *)realloc(data, capacity + 1);
		}
		count = read(sock, data + size, capacity - size);
		if (count <= 0)
			break;
		size += (size_t)count;
	}
	close(sock);
	if (!data || size < 12)
	{
		free(data);
		snprintf(why, (size_t)why_size, "no answer");
		return 0;
	}
	data[size] = 0;
	response->status = atoi(data + 9);
	end = strstr(data, "\r\n\r\n");
	if (!end)
	{
		free(data);
		snprintf(why, (size_t)why_size, "no headers");
		return 0;
	}
	end += 4;
	response->size = size - (size_t)(end - data);
	response->body = (char *)malloc(response->size + 1);
	memcpy(response->body, end, response->size);
	response->body[response->size] = 0;
	free(data);
	return 1;
}

static int get_count;

enum
{
	/* the game's get reads 64 KB of a response at most
	(posix_browser.c's request_as), so the test's does too */
	GET_MOST = 65536,
};

static int test_get(void *context, const char *url, char *buffer, int size, char *why, int why_size)
{
	struct response response;
	size_t count;

	(void)context;
	get_count++;
	if (!http_get(url, &response, why, why_size))
		return 0;
	if (response.size > GET_MOST)
		response.size = GET_MOST;
	count = response.size < (size_t)size - 1 ? response.size : (size_t)size - 1;
	memcpy(buffer, response.body, count);
	buffer[count] = 0;
	free(response.body);
	return response.status;
}

static int test_get_file(void *context, const char *url, const char *path, unsigned int maximum, unsigned char *sha256,
	unsigned int *received, int *status, char *why, int why_size)
{
	struct playlist_sha256 hash;
	struct response response;
	FILE *file;

	(void)context;
	*received = 0;
	printf("GET %s\n", url);
	if (!http_get(url, &response, why, why_size))
		return 0;
	*status = response.status;
	if (response.status != 200)
	{
		snprintf(why, (size_t)why_size, "status %d", response.status);
		free(response.body);
		return 0;
	}
	if (response.size > maximum)
	{
		snprintf(why, (size_t)why_size, "more than %u bytes", maximum);
		free(response.body);
		return 0;
	}
	file = fopen(path, "wb");
	if (!file || fwrite(response.body, 1, response.size, file) != response.size)
	{
		snprintf(why, (size_t)why_size, "could not write");
		if (file)
			fclose(file);
		free(response.body);
		return 0;
	}
	fclose(file);
	playlist_sha256_begin(&hash);
	playlist_sha256_add(&hash, response.body, response.size);
	playlist_sha256_end(&hash, sha256);
	*received = (unsigned int)response.size;
	free(response.body);
	return 1;
}

/* where a document is streamed: the save folder (MAP_REPO_TEST_SAVE), or
the temporary folder */
static void fetch_path(const char *name, char *path, size_t size)
{
	const char *save = getenv("MAP_REPO_TEST_SAVE");

	if (save)
		snprintf(path, size, "%s/%s", save, name);
	else
		snprintf(path, size, "%s/map_repo_test_%d_%s", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid(),
			name);
}

static int test_get_document(void *context, const char *url, const char *name, char *buffer, long maximum, long *size,
	int *status, char *why, int why_size)
{
	struct response response;
	char path[1024];
	FILE *file;
	size_t count;
	int ok;

	(void)context;
	*size = -1;
	*status = 0;
	get_count++;
	if (!http_get(url, &response, why, why_size))
		return 0;
	*status = response.status;
	if (response.status != 200)
	{
		free(response.body);
		snprintf(why, (size_t)why_size, "status %d", response.status);
		return 0;
	}
	if (response.size > (size_t)maximum)
	{
		free(response.body);
		snprintf(why, (size_t)why_size, "it is larger than the %ld bytes allowed", maximum);
		return 0;
	}
	/* (through a file, as the game's: written, then read back) */
	fetch_path(name, path, sizeof(path));
	file = fopen(path, "wb");
	ok = file && fwrite(response.body, 1, response.size, file) == response.size;
	if (file)
		ok = fclose(file) == 0 && ok;
	free(response.body);
	file = ok ? fopen(path, "rb") : NULL;
	count = file ? fread(buffer, 1, (size_t)maximum + 1, file) : 0;
	if (file)
		fclose(file);
	remove(path);
	if (!file || count != response.size)
	{
		snprintf(why, (size_t)why_size, "could not write %s", name);
		return 0;
	}
	buffer[count] = 0;
	*size = (long)count;
	return 1;
}

static void test_remove(void *context, const char *path)
{
	(void)context;
	remove(path);
}

static void kept_path(const char *name, char *path, size_t size)
{
	snprintf(path, size, "%s/%s", getenv("MAP_REPO_TEST_SAVE"), name);
}

static long test_load(void *context, const char *name, char *buffer, long size)
{
	char path[1024];
	FILE *file;
	size_t count;

	(void)context;
	kept_path(name, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return -1;
	count = fread(buffer, 1, (size_t)size, file);
	fclose(file);
	return count ? (long)count : -1;
}

static void test_save(void *context, const char *name, const char *data, long size)
{
	char path[1024];
	FILE *file;

	(void)context;
	kept_path(name, path, sizeof(path));
	file = fopen(path, "wb");
	if (file)
	{
		fwrite(data, 1, (size_t)size, file);
		fclose(file);
	}
}

static struct map_repo_transport transport = { NULL, test_get, test_get_document, test_get_file, test_remove, NULL, NULL,
	NULL };

static int refresh_flags(int force)
{
	return (force ? MAP_REPO_REFRESH_FORCE : 0) | (getenv("MAP_REPO_TEST_INDEX_ONLY") ? MAP_REPO_REFRESH_INDEX_ONLY : 0);
}

int main(int count, char **arguments)
{
	char why[200] = "";
	size_t size = 0, signature_size = 0;

	atexit(free_all);
	if (getenv("MAP_REPO_TEST_SAVE"))
	{
		long long now = count > 3 && !strcmp(arguments[1], "blocked") ? atoll(arguments[2]) :
			count > 3 ? atoll(arguments[3]) : 0;
		int held;

		transport.load = test_load;
		transport.save = test_save;
		held = map_repo_load(&transport, now, why, (int)sizeof(why));
		printf("load %d %s\n", held, why);
		why[0] = 0;
	}
	if (count >= 5 && !strcmp(arguments[1], "blocked"))
	{
		unsigned char sha256[32], map_hash[32];
		long long now = atoll(arguments[2]);
		int has_sha = hex_hash(arguments[3], sha256), has_map = hex_hash(arguments[4], map_hash);

		if (count >= 6)
		{
			printf("refresh %d %s\n", map_repo_refresh(&transport, arguments[5], now, refresh_flags(1), why,
				(int)sizeof(why)), why);
		}
		if (map_repo_blocked(has_sha ? sha256 : NULL, has_map ? map_hash : NULL, now, why, (int)sizeof(why)))
			printf("blocked %s\n", why);
		else
			printf("clear\n");
		return 0;
	}
	if (count >= 4 && !strcmp(arguments[1], "refreshes"))
	{
		int argument;

		for (argument = 3; argument < count; argument++)
		{
			int held;

			why[0] = 0;
			/* (NOW:index: that refresh for the index alone) */
			held = map_repo_refresh(&transport, arguments[2], atoll(arguments[argument]), refresh_flags(0) |
				(strstr(arguments[argument], ":index") ? MAP_REPO_REFRESH_INDEX_ONLY : 0), why, (int)sizeof(why));
			printf("refresh %d %d\n", held, get_count);
		}
		return 0;
	}
	if (count == 5 && !strcmp(arguments[1], "index"))
	{
		struct map_index_info info;
		char *document = slurp(arguments[2], &size), *signature = slurp(arguments[3], &signature_size);

		if (!map_index_check(document, size, signature, signature_size, atoll(arguments[4]), &info, why,
			(int)sizeof(why)))
		{
			printf("refused: %s\n", why);
			return 0;
		}
		printf("ok %u %d %d %d %d\n", info.serial, info.map_count, info.block_count, info.halt, info.refresh);
		return 0;
	}
	if (count == 5 && !strcmp(arguments[1], "mirrors"))
	{
		static struct map_mirrors mirrors;
		char *document = slurp(arguments[2], &size), *signature = slurp(arguments[3], &signature_size);

		if (!map_mirrors_check(document, size, signature, signature_size, atoll(arguments[4]), &mirrors, why,
			(int)sizeof(why)))
		{
			printf("refused: %s\n", why);
			return 0;
		}
		printf("ok %u %d\n", mirrors.serial, mirrors.count);
		return 0;
	}
	if (count == 5 && !strcmp(arguments[1], "find"))
	{
		struct map_index_info info;
		struct map_index_entry entry;
		char *document = slurp(arguments[2], &size);
		char text[65];

		if (!map_index_parse(document, size, &info, why, (int)sizeof(why)))
		{
			printf("refused: %s\n", why);
			return 0;
		}
		if (!map_index_find(document, size, arguments[3], (unsigned int)strtoul(arguments[4], NULL, 16),
			MAP_INDEX_KIND_CE, &entry, why, (int)sizeof(why)))
		{
			printf("none: %s\n", why);
			return 0;
		}
		map_index_hash_text(entry.sha256, text);
		printf("found %s %s %u %08x\n", entry.name, text, entry.size, entry.version);
		return 0;
	}
	if (count == 5 && !strcmp(arguments[1], "blacklisted"))
	{
		struct map_index_block block;
		unsigned char sha256[32], map_hash[32];
		char *document = slurp(arguments[2], &size);
		int has_sha = hex_hash(arguments[3], sha256), has_map = hex_hash(arguments[4], map_hash);

		if (map_index_blacklisted(document, size, has_sha ? sha256 : NULL, has_map ? map_hash : NULL, &block))
			printf("blocked %s\n", block.reason);
		else
			printf("clear\n");
		return 0;
	}
	if (count == 4 && !strcmp(arguments[1], "choose"))
	{
		static struct map_mirrors mirrors;
		unsigned char sha256[32];
		char *document = slurp(arguments[2], &size);
		int order[8], chosen, index;

		if (!map_mirrors_parse(document, size, &mirrors, why, (int)sizeof(why)) || !hex_hash(arguments[3], sha256))
		{
			printf("refused: %s\n", why);
			return 0;
		}
		chosen = map_mirrors_choose(&mirrors, sha256, order, 8);
		for (index = 0; index < chosen; index++)
			printf("%s%s", index ? " " : "", mirrors.mirrors[order[index]].id);
		printf("\n");
		return 0;
	}
	/* (each refresh in turn, then the download: a refresh's result is
	printed, the last refresh's time the download's) */
	if (count >= 7 && !strcmp(arguments[1], "download"))
	{
		struct map_index_entry entry;
		long long now = 0;
		int missing = 0, argument;

		for (argument = 7; argument + 1 < count; argument += 2)
		{
			now = atoll(arguments[argument + 1]);
			why[0] = 0;
			printf("refresh %d %s\n", map_repo_refresh(&transport, arguments[argument], now, refresh_flags(1), why,
				(int)sizeof(why)),
				why);
		}
		why[0] = 0;
		now = atoll(arguments[3]);
		printf("refresh %d %s\n", map_repo_refresh(&transport, arguments[2], now, refresh_flags(1), why,
			(int)sizeof(why)), why);
		if (getenv("MAP_REPO_TEST_USE_AT"))
			now = atoll(getenv("MAP_REPO_TEST_USE_AT"));
		printf("has %d\n", map_repo_has(arguments[4], (unsigned int)strtoul(arguments[5], NULL, 16), now));
		if (!map_repo_download(&transport, arguments[4], (unsigned int)strtoul(arguments[5], NULL, 16), arguments[6], now,
			&entry, &missing, why, (int)sizeof(why)))
		{
			printf("failed %d %s\n", missing, why);
			return 0;
		}
		printf("downloaded %s %u\n", entry.name, entry.size);
		return 0;
	}
	fprintf(stderr, "usage: see the file's comment\n");
	return 2;
}
