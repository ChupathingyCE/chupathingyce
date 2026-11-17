/*
MAP_SHARE_FILES.C

Map sharing's files (map_share_files.h): the hashes, the host's catalog of
the map it serves, and the fetch's part file and result.
*/

#include "platform.h"
#include "map_share_files.h"
#include "monocypher.h"
#include "thread_stop.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	CATALOG_PATH_SIZE = 256,
	/* no map shared is larger (the clients' cap for a map is 768 MiB) */
	CATALOG_MAXIMUM_SIZE = 0x30000000,
};

enum
{
	_catalog_empty,
	_catalog_hashing,
	_catalog_hashed,
	_catalog_failed,
};

/* ---------- globals */

static struct
{
	pthread_mutex_t lock;
	char path[CATALOG_PATH_SIZE];
	unsigned int size;
	unsigned int time_low;
	unsigned int time_high;
	int state;
	/* each start of a hash: a thread's result is kept only if it is still
	the one asked for */
	unsigned int generation;
	unsigned char hash[MAP_SHARE_HASH_SIZE];
	unsigned char *pieces;
	unsigned int piece_count;
} catalog = { PTHREAD_MUTEX_INITIALIZER };

struct catalog_job
{
	char path[CATALOG_PATH_SIZE];
	unsigned int size;
	unsigned int generation;
};

/* ---------- hashes */

void map_share_piece_hash(const void *data, unsigned int size, unsigned char *hash)
{
	crypto_blake2b(hash, MAP_SHARE_HASH_SIZE, (const unsigned char *)data, size);
}

void map_share_file_hash(unsigned int size, const unsigned char *pieces, unsigned int count, unsigned char *hash)
{
	static const char tag[] = "OpenCE map 1";
	crypto_blake2b_ctx whole;
	unsigned char size_bytes[4];

	size_bytes[0] = (unsigned char)size;
	size_bytes[1] = (unsigned char)(size >> 8);
	size_bytes[2] = (unsigned char)(size >> 16);
	size_bytes[3] = (unsigned char)(size >> 24);
	crypto_blake2b_init(&whole, MAP_SHARE_HASH_SIZE);
	crypto_blake2b_update(&whole, (const unsigned char *)tag, sizeof(tag) - 1);
	crypto_blake2b_update(&whole, size_bytes, sizeof(size_bytes));
	crypto_blake2b_update(&whole, pieces, (size_t)count * MAP_SHARE_HASH_SIZE);
	crypto_blake2b_final(&whole, hash);
}

unsigned int map_share_piece_count(unsigned int size)
{
	return (unsigned int)(((unsigned long long)size + MAP_SHARE_PIECE_SIZE - 1) / MAP_SHARE_PIECE_SIZE);
}

/* ---------- the catalog */

static int file_read(HANDLE file, unsigned int offset, void *buffer, unsigned int size)
{
	DWORD read = 0;

	if (SetFilePointer(file, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return 0;
	return ReadFile(file, buffer, size, &read, NULL) && read == size;
}

static void *catalog_thread(void *argument)
{
	struct catalog_job *job = (struct catalog_job *)argument;
	unsigned int count = map_share_piece_count(job->size);
	unsigned char *pieces = (unsigned char *)malloc((size_t)(count ? count : 1) * MAP_SHARE_HASH_SIZE);
	unsigned char *buffer = (unsigned char *)malloc(MAP_SHARE_PIECE_SIZE);
	unsigned char hash[MAP_SHARE_HASH_SIZE];
	HANDLE file = CreateFileA(job->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
		NULL);
	unsigned int index;
	int ok = pieces && buffer && file != INVALID_HANDLE_VALUE;

	for (index = 0; ok && index < count; index++)
	{
		unsigned int offset = index * (unsigned int)MAP_SHARE_PIECE_SIZE;
		unsigned int length = job->size - offset < MAP_SHARE_PIECE_SIZE ? job->size - offset : MAP_SHARE_PIECE_SIZE;

		if (thread_stop_requested() || !file_read(file, offset, buffer, length))
			ok = 0;
		else
			map_share_piece_hash(buffer, length, pieces + (size_t)index * MAP_SHARE_HASH_SIZE);
	}
	if (file != INVALID_HANDLE_VALUE)
		CloseHandle(file);
	free(buffer);
	if (ok)
		map_share_file_hash(job->size, pieces, count, hash);
	pthread_mutex_lock(&catalog.lock);
	if (catalog.generation == job->generation && catalog.state == _catalog_hashing)
	{
		catalog.state = ok ? _catalog_hashed : _catalog_failed;
		if (ok)
		{
			memcpy(catalog.hash, hash, sizeof(hash));
			free(catalog.pieces);
			catalog.pieces = pieces;
			catalog.piece_count = count;
			pieces = NULL;
		}
	}
	pthread_mutex_unlock(&catalog.lock);
	free(pieces);
	free(job);
	return NULL;
}

int map_share_catalog_file(const char *path, unsigned int *size, unsigned char *hash)
{
	FILETIME written;
	DWORD high = 0;
	unsigned int file_size;
	HANDLE file;
	int result;

	if (!path || !path[0] || strlen(path) >= CATALOG_PATH_SIZE)
		return -1;
	file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return -1;
	file_size = (unsigned int)GetFileSize(file, &high);
	memset(&written, 0, sizeof(written));
	GetFileTime(file, NULL, NULL, &written);
	CloseHandle(file);
	if (high || !file_size || file_size > CATALOG_MAXIMUM_SIZE)
		return -1;
	pthread_mutex_lock(&catalog.lock);
	/* (a file of another path, or this one changed: hashed again) */
	if (catalog.state == _catalog_empty || strcmp(catalog.path, path) || catalog.size != file_size ||
		catalog.time_low != (unsigned int)written.dwLowDateTime ||
		catalog.time_high != (unsigned int)written.dwHighDateTime)
	{
		struct catalog_job *job = (struct catalog_job *)malloc(sizeof(*job));

		free(catalog.pieces);
		catalog.pieces = NULL;
		catalog.piece_count = 0;
		snprintf(catalog.path, sizeof(catalog.path), "%s", path);
		catalog.size = file_size;
		catalog.time_low = (unsigned int)written.dwLowDateTime;
		catalog.time_high = (unsigned int)written.dwHighDateTime;
		catalog.generation++;
		catalog.state = _catalog_hashing;
		if (!job)
			catalog.state = _catalog_failed;
		else
		{
			snprintf(job->path, sizeof(job->path), "%s", path);
			job->size = file_size;
			job->generation = catalog.generation;
			if (thread_stop_create("map sharing hash", catalog_thread, job) != 0)
			{
				free(job);
				catalog.state = _catalog_failed;
			}
		}
	}
	result = catalog.state == _catalog_hashed ? 1 : catalog.state == _catalog_hashing ? 0 : -1;
	if (result == 1)
	{
		*size = catalog.size;
		memcpy(hash, catalog.hash, MAP_SHARE_HASH_SIZE);
	}
	pthread_mutex_unlock(&catalog.lock);
	return result;
}

int map_share_catalog_piece_hashes(unsigned int first, unsigned int count, unsigned char *hashes)
{
	int ok;

	pthread_mutex_lock(&catalog.lock);
	ok = catalog.state == _catalog_hashed && catalog.pieces && first <= catalog.piece_count &&
		count <= catalog.piece_count - first;
	if (ok)
		memcpy(hashes, catalog.pieces + (size_t)first * MAP_SHARE_HASH_SIZE, (size_t)count * MAP_SHARE_HASH_SIZE);
	pthread_mutex_unlock(&catalog.lock);
	return ok;
}

int map_share_catalog_read(unsigned int offset, void *buffer, unsigned int size)
{
	char path[CATALOG_PATH_SIZE];
	HANDLE file;
	int ok;

	pthread_mutex_lock(&catalog.lock);
	ok = catalog.state == _catalog_hashed && offset <= catalog.size && size <= catalog.size - offset;
	memcpy(path, catalog.path, sizeof(path));
	pthread_mutex_unlock(&catalog.lock);
	if (!ok)
		return 0;
	file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	ok = file_read(file, offset, buffer, size);
	CloseHandle(file);
	return ok;
}

void map_share_catalog_clear(void)
{
	pthread_mutex_lock(&catalog.lock);
	free(catalog.pieces);
	catalog.pieces = NULL;
	catalog.piece_count = 0;
	catalog.path[0] = 0;
	catalog.state = _catalog_empty;
	catalog.generation++;
	pthread_mutex_unlock(&catalog.lock);
}

/* ---------- the fetch's files */

void *map_share_part_open(const char *path)
{
	return fopen(path, "w+b");
}

int map_share_part_write(void *part, unsigned int offset, const void *data, unsigned int size)
{
	FILE *file = (FILE *)part;

	if (!file || fseek(file, (long)offset, SEEK_SET) != 0)
		return 0;
	return fwrite(data, 1, size, file) == size;
}

int map_share_part_close(void *part, const char *path, const char *final, int keep)
{
	FILE *file = (FILE *)part;
	int ok = 1;

	if (file)
	{
		ok = fflush(file) == 0;
		ok = fclose(file) == 0 && ok;
	}
	if (keep && ok && final)
	{
		/* (a file of the final name is never replaced) */
		if (map_share_file_exists(final))
			ok = 0;
		else
			ok = rename(path, final) == 0;
	}
	else
		ok = 0;
	if (!ok)
		remove(path);
	return ok;
}

int map_share_write_text(const char *path, const char *text)
{
	char temporary[1024];
	FILE *file;
	size_t length = strlen(text);
	int ok;

	if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary))
		return 0;
	file = fopen(temporary, "wb");
	if (!file)
		return 0;
	ok = fwrite(text, 1, length, file) == length;
	ok = fclose(file) == 0 && ok;
	remove(path);
	if (!ok || rename(temporary, path) != 0)
	{
		remove(temporary);
		return 0;
	}
	return 1;
}

int map_share_file_exists(const char *path)
{
	FILE *file = fopen(path, "rb");

	if (!file)
		return 0;
	fclose(file);
	return 1;
}
