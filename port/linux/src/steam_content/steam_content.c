/*
STEAM_CONTENT.C

The Steam content client's interface and threads (steam_content.h): the
sign-in file, the QR sign-in, and the download of an item, its chunks
fetched by a few threads of their own.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "steam_content.h"
#include "sc_internal.h"
#include "sc_net.h"
#include "sc_proto.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define make_folder(path) _mkdir(path)
#define remove_file _unlink
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>
#define make_folder(path) mkdir(path, 0755)
#define remove_file unlink
#endif

#include "mbedtls/sha1.h"

#define SIGN_IN_FILE "steam_sign_in.txt"
#define DETAILS_HOST "api.steampowered.com"
#define DETAILS_PATH "/ISteamRemoteStorage/GetPublishedFileDetails/v1/"
#define CONTENT_FOLDER "steamapps/workshop/content/976730"
/* (as Steam does: an item is put together here, then moved to its content folder whole) */
#define DOWNLOADS_FOLDER "steamapps/workshop/downloads/976730"
#define CLIENT_NAME "Chupathingy"

enum
{
	MAXIMUM_ITEMS = 64,
	MAXIMUM_SERVERS = 16,
	/* each chunk tried this many times, on the next server each time */
	CHUNK_TRIES = 6,
	MAXIMUM_MANIFEST = 64 * 1024 * 1024,
};

struct steam_content_host sc_host;

struct item
{
	unsigned long long id;
	char library[STEAM_CONTENT_PATH_SIZE];
	struct steam_content_progress progress;
	int queued;
};

struct sign_in
{
	char account[128];
	char token[2048];
	uint32_t login_id;
	char persona[STEAM_CONTENT_PERSONA_SIZE];
	/* the token is in the system's secret store (sc_secret.c), not the file */
	int in_store;
};

static struct
{
	pthread_mutex_t lock;
	int initialised;
	/* the QR sign-in */
	int qr_state;
	int qr_error;
	char qr_text[STEAM_CONTENT_QR_SIZE];
	volatile int qr_cancel;
	int qr_running;
	int qr_generation;
	/* the downloads */
	int worker_running;
	volatile int stop;
	volatile int cancel_current;
	unsigned long long current;
	int count;
	struct item items[MAXIMUM_ITEMS];
	/* what the sign-in file says (the lock held to read or change) */
	int linked;
	char persona[STEAM_CONTENT_PERSONA_SIZE];
	/* an unlink's revoke at Steam still under way */
	int revoking;
	/* the stored token is in the system's secret store (0: the private file) */
	int token_in_store;
} client = { PTHREAD_MUTEX_INITIALIZER };

/* ---------- small things */

long long sc_milliseconds(void)
{
#ifdef _WIN32
	return (long long)GetTickCount64();
#else
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
#endif
}

void sc_log(const char *format, ...)
{
	char line[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	if (sc_host.log)
		sc_host.log("%s", line);
}

static void sleep_milliseconds(int milliseconds)
{
#ifdef _WIN32
	Sleep((DWORD)milliseconds);
#else
	struct timespec wait;

	wait.tv_sec = milliseconds / 1000;
	wait.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
	nanosleep(&wait, NULL);
#endif
}

/* ---------- the sign-in file */

static int sign_in_path(char *path, size_t size)
{
	return sc_host.data_folder[0] && snprintf(path, size, "%s/" SIGN_IN_FILE, sc_host.data_folder) < (int)size;
}

static int sign_in_read(struct sign_in *sign_in)
{
	char path[STEAM_CONTENT_PATH_SIZE + 32], line[2304];
	FILE *file;

	memset(sign_in, 0, sizeof(*sign_in));
	if (!sign_in_path(path, sizeof(path)) || !(file = fopen(path, "r")))
		return 0;
	while (fgets(line, sizeof(line), file))
	{
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "account=", 8))
			snprintf(sign_in->account, sizeof(sign_in->account), "%s", line + 8);
		else if (!strncmp(line, "token=", 6))
			snprintf(sign_in->token, sizeof(sign_in->token), "%s", line + 6);
		else if (!strncmp(line, "login_id=", 9))
			sign_in->login_id = (uint32_t)strtoul(line + 9, NULL, 16);
		else if (!strncmp(line, "persona=", 8))
			snprintf(sign_in->persona, sizeof(sign_in->persona), "%s", line + 8);
		else if (!strcmp(line, "store=system"))
			sign_in->in_store = 1;
	}
	memset(line, 0, sizeof(line));
	fclose(file);
	client.token_in_store = sign_in->in_store;
	if (sign_in->in_store && sign_in->account[0] && !sign_in->token[0] &&
		!sc_secret_get(sign_in->account, sign_in->token, sizeof(sign_in->token)))
	{
		sc_log("Steam: the sign-in is missing from %s", sc_secret_store_name() ? sc_secret_store_name() : "its store");
	}
	return sign_in->account[0] && sign_in->token[0] && sign_in->login_id;
}

/* written whole (a new file renamed over the old), readable by the player only */
static int sign_in_write(const struct sign_in *sign_in)
{
	char path[STEAM_CONTENT_PATH_SIZE + 32], fresh[STEAM_CONTENT_PATH_SIZE + 48];
	FILE *file;
	int ok;

	if (!sign_in_path(path, sizeof(path)) || snprintf(fresh, sizeof(fresh), "%s.new", path) >= (int)sizeof(fresh))
		return 0;
	remove_file(fresh);
#ifdef _WIN32
	file = fopen(fresh, "w");
#else
	{
		int descriptor = open(fresh, O_WRONLY | O_CREAT | O_TRUNC, 0600);

		file = descriptor >= 0 ? fdopen(descriptor, "w") : NULL;
		if (!file && descriptor >= 0)
			close(descriptor);
	}
#endif
	if (!file)
		return 0;
	/* (the token in the system's store when it takes it; else in this file only) */
	client.token_in_store = !sc_host.file_only && sc_secret_store_name() &&
		sc_secret_put(sign_in->account, sign_in->token);
	if (client.token_in_store)
		ok = fprintf(file, "account=%s\nstore=system\nlogin_id=%08x\npersona=%s\n", sign_in->account,
			(unsigned)sign_in->login_id, sign_in->persona) > 0;
	else
		ok = fprintf(file, "account=%s\ntoken=%s\nlogin_id=%08x\npersona=%s\n", sign_in->account, sign_in->token,
			(unsigned)sign_in->login_id, sign_in->persona) > 0;
	ok &= fclose(file) == 0;
#ifndef _WIN32
	chmod(fresh, 0600);
#else
	remove_file(path);
#endif
	if (!ok || rename(fresh, path) != 0)
	{
		remove_file(fresh);
		return 0;
	}
	return 1;
}

static void persona_clean(char *persona)
{
	/* (one line of printable text: the file's own format) */
	for (; *persona; persona++)
	{
		if ((unsigned char)*persona < 0x20)
			*persona = ' ';
	}
}

/* the persona name (and a renewed token) kept */
static void sign_in_update(const char *persona, const char *token)
{
	struct sign_in sign_in;

	pthread_mutex_lock(&client.lock);
	if (sign_in_read(&sign_in))
	{
		/* (a token still in the file moves to the system's store once it has one) */
		int changed = !sign_in.in_store && !sc_host.file_only && sc_secret_store_name() != NULL;

		if (persona && persona[0] && strcmp(persona, sign_in.persona))
		{
			snprintf(sign_in.persona, sizeof(sign_in.persona), "%s", persona);
			persona_clean(sign_in.persona);
			snprintf(client.persona, sizeof(client.persona), "%s", sign_in.persona);
			changed = 1;
		}
		if (token && token[0] && strcmp(token, sign_in.token) && strlen(token) < sizeof(sign_in.token))
		{
			snprintf(sign_in.token, sizeof(sign_in.token), "%s", token);
			changed = 1;
			sc_log("Steam: the sign-in was renewed");
		}
		if (changed && !sign_in_write(&sign_in))
			sc_log("Steam: the sign-in file could not be written");
	}
	pthread_mutex_unlock(&client.lock);
}

static uint32_t new_login_id(void)
{
	uint32_t id = 0;

	while (!id)
	{
		if (!sc_random(&id, sizeof(id)))
		{
			/* (no generator: the clock, still never zero) */
			id = (uint32_t)sc_milliseconds() ^ (uint32_t)time(NULL) ^ 0x43485500u;
		}
	}
	return id;
}

/* ---------- the CM work both the sign-in and a download start with */

/* logged on with the stored sign-in: STEAM_CONTENT_OK, else the player's word */
static int log_on(struct sc_cm *cm, const volatile int *cancel)
{
	struct sign_in sign_in;
	int result, eresult;

	pthread_mutex_lock(&client.lock);
	result = sign_in_read(&sign_in);
	pthread_mutex_unlock(&client.lock);
	if (!result)
		return STEAM_CONTENT_SIGN_IN;
	result = sc_cm_connect(cm, cancel);
	if (result != STEAM_CONTENT_OK)
		return result;
	eresult = sc_cm_logon(cm, sign_in.account, sign_in.token, sign_in.login_id);
	if (eresult != SC_RESULT_OK)
	{
		sc_log("Steam: the logon was refused (EResult %d)", eresult);
		sc_cm_close(cm);
		return steam_content_error_from_result(STEAM_CONTENT_STEP_LOGON, eresult);
	}
	/* (the account's name to show comes just after the logon) */
	{
		struct sc_message message;
		long long end = sc_milliseconds() + 3000;

		while (!cm->persona[0] && sc_milliseconds() < end && sc_cm_receive(cm, &message, 250) >= 0)
			;
	}
	/* (the refresh token renewed when Steam wants: GenerateAccessTokenForApp, renewal allowed) */
	{
		unsigned char body[2304], *response;
		struct pb_writer writer;
		size_t length;
		char token[2048] = "";

		pb_writer_init(&writer, body, sizeof(body));
		pb_put_string(&writer, 1, sign_in.token);
		pb_put_fixed64(&writer, 2, cm->steamid);
		pb_put_varint(&writer, 3, 1);
		if (!writer.failed && sc_cm_call(cm, "Authentication.GenerateAccessTokenForApp#1", body, writer.length, 1,
			&response, &length, &eresult))
		{
			if (eresult == SC_RESULT_OK)
				pb_get_string(response, length, 2, token, sizeof(token));
			free(response);
		}
		sign_in_update(cm->persona, token);
	}
	return STEAM_CONTENT_OK;
}

/* ---------- the QR sign-in */

static void qr_set(int generation, int state, int error, const char *text)
{
	pthread_mutex_lock(&client.lock);
	if (generation == client.qr_generation)
	{
		client.qr_state = state;
		client.qr_error = error;
		if (text)
			snprintf(client.qr_text, sizeof(client.qr_text), "%s", text);
	}
	pthread_mutex_unlock(&client.lock);
}

static void *qr_thread(void *context)
{
	int generation = (int)(intptr_t)context;
	struct sc_cm cm;
	unsigned char body[512], *response = NULL;
	struct pb_writer writer, details;
	unsigned char details_buffer[128];
	size_t length;
	int result, eresult;
	uint64_t client_id;
	unsigned char request_id[256];
	size_t request_length;
	const unsigned char *bytes;
	char url[STEAM_CONTENT_QR_SIZE];
	long long end, interval = 5000;
	float seconds;
	int reconnects = 0;

	result = sc_cm_connect(&cm, &client.qr_cancel);
	if (result != STEAM_CONTENT_OK)
	{
		qr_set(generation, STEAM_CONTENT_QR_FAILED, result, NULL);
		goto done;
	}
	pb_writer_init(&details, details_buffer, sizeof(details_buffer));
	pb_put_string(&details, 1, CLIENT_NAME);
	/* platform type 1, Steam client: the token then logs on to a CM */
	pb_put_varint(&details, 2, 1);
	pb_put_varint(&details, 3, 20);
	pb_writer_init(&writer, body, sizeof(body));
	pb_put_string(&writer, 1, CLIENT_NAME);
	pb_put_varint(&writer, 2, 1);
	pb_put_bytes(&writer, 3, details.data, details.length);
	pb_put_string(&writer, 4, "Client");
	if (!sc_cm_call(&cm, "Authentication.BeginAuthSessionViaQR#1", body, writer.length, 0, &response, &length,
		&eresult) || eresult != SC_RESULT_OK)
	{
		qr_set(generation, STEAM_CONTENT_QR_FAILED, steam_content_error_from_result(STEAM_CONTENT_STEP_CONNECT,
			eresult == SC_RESULT_OK ? 0 : eresult), NULL);
		goto done;
	}
	client_id = pb_get_number(response, length, 1, 0);
	pb_get_string(response, length, 2, url, sizeof(url));
	if (!pb_get_bytes(response, length, 3, &bytes, &request_length) || request_length > sizeof(request_id) ||
		!url[0])
	{
		qr_set(generation, STEAM_CONTENT_QR_FAILED, STEAM_CONTENT_FAILED, NULL);
		goto done;
	}
	memcpy(request_id, bytes, request_length);
	{
		uint32_t raw = (uint32_t)pb_get_number(response, length, 4, 0);

		memcpy(&seconds, &raw, sizeof(seconds));
		if (seconds >= 1.0f && seconds <= 30.0f)
			interval = (long long)(seconds * 1000.0f);
	}
	free(response);
	response = NULL;
	qr_set(generation, STEAM_CONTENT_QR_WAITING, STEAM_CONTENT_OK, url);
	sc_log("Steam: the sign-in QR is ready");
	end = sc_milliseconds() + STEAM_CONTENT_SIGN_IN_TIMEOUT;
	while (sc_milliseconds() < end)
	{
		struct sc_message message;
		long long wake = sc_milliseconds() + interval;
		char token[2048], account[128];

		/* (the CM's own messages read while waiting) */
		while (sc_milliseconds() < wake && !client.qr_cancel)
		{
			if (sc_cm_receive(&cm, &message, 250) < 0)
				break;
		}
		if (client.qr_cancel)
		{
			qr_set(generation, STEAM_CONTENT_QR_IDLE, STEAM_CONTENT_CANCELLED, NULL);
			goto done;
		}
		pb_writer_init(&writer, body, sizeof(body));
		pb_put_varint(&writer, 1, client_id);
		pb_put_bytes(&writer, 2, request_id, request_length);
		if (!sc_cm_call(&cm, "Authentication.PollAuthSessionStatus#1", body, writer.length, 0, &response, &length,
			&eresult))
		{
			/* (a CM closes a connection that hasn't logged on after a minute or
			so: the sign-in itself lives on at Steam, polled from another) */
			sc_cm_close(&cm);
			result = sc_cm_connect(&cm, &client.qr_cancel);
			if (result == STEAM_CONTENT_OK && ++reconnects <= 20)
				continue;
			qr_set(generation, STEAM_CONTENT_QR_FAILED, result == STEAM_CONTENT_OK ? STEAM_CONTENT_FAILED : result,
				NULL);
			goto done;
		}
		if (eresult != SC_RESULT_OK)
		{
			/* (the QR expired or was refused on the phone) */
			qr_set(generation, STEAM_CONTENT_QR_FAILED, steam_content_error_from_result(STEAM_CONTENT_STEP_LOGON,
				eresult) == STEAM_CONTENT_OFFLINE ? STEAM_CONTENT_OFFLINE : STEAM_CONTENT_FAILED, NULL);
			goto done;
		}
		if (pb_get_number(response, length, 1, 0))
			client_id = pb_get_number(response, length, 1, 0);
		if (pb_get_string(response, length, 2, url, sizeof(url)) && url[0])
			qr_set(generation, STEAM_CONTENT_QR_WAITING, STEAM_CONTENT_OK, url);
		if (pb_get_number(response, length, 5, 0))
		{
			pthread_mutex_lock(&client.lock);
			if (generation == client.qr_generation && client.qr_state == STEAM_CONTENT_QR_WAITING)
				client.qr_state = STEAM_CONTENT_QR_SCANNED;
			pthread_mutex_unlock(&client.lock);
		}
		if (pb_get_string(response, length, 3, token, sizeof(token)) && token[0] &&
			pb_get_string(response, length, 6, account, sizeof(account)) && account[0])
		{
			struct sign_in sign_in, old;
			int written;

			free(response);
			response = NULL;
			memset(&sign_in, 0, sizeof(sign_in));
			snprintf(sign_in.account, sizeof(sign_in.account), "%s", account);
			snprintf(sign_in.token, sizeof(sign_in.token), "%s", token);
			memset(token, 0, sizeof(token));
			pthread_mutex_lock(&client.lock);
			/* (the install's LoginID kept when one is stored; else a new one) */
			sign_in_read(&old);
			sign_in.login_id = old.login_id ? old.login_id : new_login_id();
			written = sign_in_write(&sign_in);
			memset(&old, 0, sizeof(old));
			client.linked = written;
			client.persona[0] = 0;
			pthread_mutex_unlock(&client.lock);
			memset(&sign_in, 0, sizeof(sign_in));
			sc_cm_close(&cm);
			if (!written)
			{
				sc_log("Steam: the sign-in file could not be written");
				qr_set(generation, STEAM_CONTENT_QR_FAILED, STEAM_CONTENT_FAILED, NULL);
				goto done_closed;
			}
			sc_log("Steam: signed in by QR");
			/* (logged on once: the name to show, and the sign-in checked) */
			result = log_on(&cm, &client.qr_cancel);
			if (result == STEAM_CONTENT_OK)
				sc_cm_close(&cm);
			qr_set(generation, result == STEAM_CONTENT_OK || result == STEAM_CONTENT_OFFLINE ? STEAM_CONTENT_QR_DONE :
				STEAM_CONTENT_QR_FAILED, result == STEAM_CONTENT_OFFLINE ? STEAM_CONTENT_OK : result, NULL);
			goto done_closed;
		}
		free(response);
		response = NULL;
	}
	sc_log("Steam: the sign-in QR timed out");
	qr_set(generation, STEAM_CONTENT_QR_FAILED, STEAM_CONTENT_SIGN_IN, NULL);
done:
	free(response);
	sc_cm_close(&cm);
done_closed:
	pthread_mutex_lock(&client.lock);
	if (generation == client.qr_generation)
		client.qr_running = 0;
	pthread_mutex_unlock(&client.lock);
	return NULL;
}

/* ---------- a download */

struct server
{
	char host[256];
	int tls;
};

struct download
{
	unsigned long long id;
	/* where it is put together, and where it goes once whole */
	char folder[STEAM_CONTENT_PATH_SIZE + 64];
	char final_folder[STEAM_CONTENT_PATH_SIZE + 64];
	unsigned char key[32];
	struct server servers[MAXIMUM_SERVERS];
	int server_count;
	struct sc_manifest manifest;
	/* each chunk's job: its file */
	int *chunk_file;
	pthread_mutex_t lock;
	int next_chunk;
	int failed;
	int error;
	char why[192];
	unsigned long long done;
};

static struct item *item_find(unsigned long long id)
{
	int index;

	for (index = 0; index < client.count; index++)
	{
		if (client.items[index].id == id)
			return &client.items[index];
	}
	return NULL;
}

static void progress_set(unsigned long long id, int state, int error, unsigned long long done,
	unsigned long long total)
{
	struct item *item;

	pthread_mutex_lock(&client.lock);
	if ((item = item_find(id)) != NULL)
	{
		item->progress.state = state;
		item->progress.error = error;
		item->progress.done = done;
		item->progress.total = total;
	}
	pthread_mutex_unlock(&client.lock);
}

/* the folders up to (not including) the last part of path made */
static int make_parents(const char *path)
{
	char copy[STEAM_CONTENT_PATH_SIZE + SC_MAXIMUM_NAME + 64];
	char *at;

	if (snprintf(copy, sizeof(copy), "%s", path) >= (int)sizeof(copy))
		return 0;
	for (at = copy + 1; *at; at++)
	{
		if (*at == '/')
		{
			*at = 0;
			if (make_folder(copy) != 0 && errno != EEXIST)
				return 0;
			*at = '/';
		}
	}
	return 1;
}

/* whether the file there is already whole (size and SHA-1) */
static int file_whole(const char *path, const struct sc_file *file)
{
	unsigned char buffer[65536], digest[20];
	mbedtls_sha1_context sha;
	FILE *handle = fopen(path, "rb");
	unsigned long long size = 0;
	size_t count;

	if (!handle)
		return 0;
	mbedtls_sha1_init(&sha);
	mbedtls_sha1_starts(&sha);
	while ((count = fread(buffer, 1, sizeof(buffer), handle)) > 0)
	{
		mbedtls_sha1_update(&sha, buffer, count);
		size += count;
	}
	fclose(handle);
	mbedtls_sha1_finish(&sha, digest);
	mbedtls_sha1_free(&sha);
	if (size != file->size)
		return 0;
	/* (an empty file has no SHA-1 to match in some manifests) */
	return !file->has_sha || !memcmp(digest, file->sha, 20) || (!file->size);
}

/* whether path is a link (or, on Windows, a reparse point): never written through */
static int is_link(const char *path)
{
#ifdef _WIN32
	DWORD attributes = GetFileAttributesA(path);

	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
	struct stat information;

	return lstat(path, &information) == 0 && S_ISLNK(information.st_mode);
#endif
}

/* a file's path under the item's folder: its name safe (sc_name_safe), and
neither the folder nor anything on the way a link, so nothing is written
outside the downloads folder */
static int file_path(const struct download *download, const struct sc_file *file, char *path, size_t size)
{
	size_t at;

	if (!sc_name_safe(file->name) || snprintf(path, size, "%s/%s", download->folder, file->name) >= (int)size)
		return 0;
	if (is_link(download->folder))
		return 0;
	for (at = strlen(download->folder) + 1; path[at]; at++)
	{
		if (path[at] == '/')
		{
			path[at] = 0;
			if (is_link(path))
				return 0;
			path[at] = '/';
		}
	}
	return !is_link(path);
}

/* the free bytes on the disk that has path; 0 when unknown */
static unsigned long long disk_free(const char *path)
{
#ifdef _WIN32
	ULARGE_INTEGER available;

	if (GetDiskFreeSpaceExA(path, &available, NULL, NULL))
		return available.QuadPart;
	return 0;
#else
	struct statvfs information;

	if (statvfs(path, &information) == 0)
		return (unsigned long long)information.f_bavail * (unsigned long long)information.f_frsize;
	return 0;
#endif
}

static unsigned long long item_limit(void)
{
	return sc_host.item_limit ? sc_host.item_limit : STEAM_CONTENT_DEFAULT_ITEM_LIMIT;
}

static unsigned long long file_limit(void)
{
	return sc_host.file_limit ? sc_host.file_limit : STEAM_CONTENT_DEFAULT_FILE_LIMIT;
}

/* the file made at its full size (its chunks then written into it) */
static int file_prepare(const char *path, unsigned long long size)
{
	FILE *handle = fopen(path, "wb");
	int ok;

	if (!handle)
		return 0;
	ok = 1;
	if (size)
	{
#ifdef _WIN32
		ok = _chsize_s(_fileno(handle), (long long)size) == 0;
#else
		ok = ftruncate(fileno(handle), (off_t)size) == 0;
#endif
	}
	ok &= fclose(handle) == 0;
	return ok;
}

static int file_write_at(const char *path, unsigned long long offset, const void *data, size_t length)
{
	FILE *handle = fopen(path, "r+b");
	int ok;

	if (!handle)
		return 0;
#ifdef _WIN32
	ok = _fseeki64(handle, (long long)offset, SEEK_SET) == 0;
#else
	ok = fseeko(handle, (off_t)offset, SEEK_SET) == 0;
#endif
	ok = ok && fwrite(data, 1, length, handle) == length;
	ok &= fclose(handle) == 0;
	return ok;
}

/* whether relative is one of the manifest's files or folders (Windows: any case) */
static int manifest_has(const struct sc_manifest *manifest, const char *relative)
{
	int index;

	for (index = 0; index < manifest->file_count; index++)
	{
#ifdef _WIN32
		if (!_stricmp(manifest->files[index].name, relative))
#else
		if (!strcmp(manifest->files[index].name, relative))
#endif
			return 1;
	}
	return 0;
}

/* what an update's new manifest no longer has, deleted from the item's
folder (nothing outside it: links are removed themselves, never followed).
relative is the folder's path inside the item ("" the item's own) */
void sc_prune(const char *folder, const char *relative, const struct sc_manifest *manifest, int depth)
{
	char path[STEAM_CONTENT_PATH_SIZE + SC_MAXIMUM_NAME + 64], inner[SC_MAXIMUM_NAME + 1];

	if (depth > 32)
		return;
#ifdef _WIN32
	{
		WIN32_FIND_DATAA found;
		HANDLE search;
		char pattern[STEAM_CONTENT_PATH_SIZE + SC_MAXIMUM_NAME + 72];

		if (snprintf(pattern, sizeof(pattern), "%s/%s%s*", folder, relative, relative[0] ? "/" : "") >=
			(int)sizeof(pattern) || (search = FindFirstFileA(pattern, &found)) == INVALID_HANDLE_VALUE)
		{
			return;
		}
		do
		{
			int directory = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				!(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
			int link = (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
			const char *name = found.cFileName;
#else
	{
		DIR *directory_handle;
		struct dirent *entry;

		if (snprintf(path, sizeof(path), "%s/%s", folder, relative) >= (int)sizeof(path) ||
			(directory_handle = opendir(path)) == NULL)
		{
			return;
		}
		while ((entry = readdir(directory_handle)) != NULL)
		{
			struct stat information;
			int directory, link;
			const char *name = entry->d_name;
#endif
			if (!strcmp(name, ".") || !strcmp(name, ".."))
				continue;
			if (snprintf(inner, sizeof(inner), "%s%s%s", relative, relative[0] ? "/" : "", name) >= (int)sizeof(inner) ||
				snprintf(path, sizeof(path), "%s/%s", folder, inner) >= (int)sizeof(path))
			{
				continue;
			}
#ifndef _WIN32
			if (lstat(path, &information) != 0)
				continue;
			link = S_ISLNK(information.st_mode);
			directory = S_ISDIR(information.st_mode);
#endif
			if (directory)
			{
				sc_prune(folder, inner, manifest, depth + 1);
				/* (a folder the item no longer has: gone once empty) */
				if (!manifest_has(manifest, inner))
#ifdef _WIN32
					_rmdir(path);
#else
					rmdir(path);
#endif
			}
			else if (link || !manifest_has(manifest, inner))
			{
				sc_log("Steam: a file the item no longer has removed");
				remove_file(path);
			}
#ifdef _WIN32
		} while (FindNextFileA(search, &found));
		FindClose(search);
	}
#else
		}
		closedir(directory_handle);
	}
#endif
}

static void download_fail(struct download *download, int error, const char *why)
{
	pthread_mutex_lock(&download->lock);
	if (!download->failed)
	{
		download->failed = 1;
		download->error = error;
		snprintf(download->why, sizeof(download->why), "%s", why);
	}
	pthread_mutex_unlock(&download->lock);
}

struct fetcher
{
	struct download *download;
	int number;
};

static void *fetch_thread(void *context)
{
	struct fetcher *fetcher = (struct fetcher *)context;
	struct download *download = fetcher->download;
	struct sc_http http;
	unsigned char *plain = NULL;
	size_t plain_size = 0;

	memset(&http, 0, sizeof(http));
	http.cancel = &client.cancel_current;
	for (;;)
	{
		const struct sc_chunk *chunk;
		const struct sc_file *file;
		char path[STEAM_CONTENT_PATH_SIZE + SC_MAXIMUM_NAME + 64], request[128], hex[41], error[192], why[128];
		int index, attempt, done = 0, last_status = 0, at;

		pthread_mutex_lock(&download->lock);
		index = download->failed ? -1 : download->next_chunk < download->manifest.chunk_count ?
			download->next_chunk++ : -1;
		/* (chunks of files already whole: passed over) */
		while (index >= 0 && download->chunk_file[index] < 0)
			index = download->next_chunk < download->manifest.chunk_count ? download->next_chunk++ : -1;
		pthread_mutex_unlock(&download->lock);
		if (index < 0 || client.cancel_current || client.stop)
			break;
		chunk = &download->manifest.chunks[index];
		file = &download->manifest.files[download->chunk_file[index]];
		if (chunk->size > plain_size)
		{
			free(plain);
			plain_size = chunk->size;
			plain = (unsigned char *)malloc(plain_size ? plain_size : 1);
			if (!plain)
			{
				download_fail(download, STEAM_CONTENT_FAILED, "out of memory");
				break;
			}
		}
		for (at = 0; at < 20; at++)
			snprintf(hex + at * 2, 3, "%02x", chunk->sha[at]);
		snprintf(request, sizeof(request), "/depot/%d/chunk/%s", STEAM_CONTENT_APP, hex);
		/* (a server each try, starting from this fetcher's own) */
		for (attempt = 0; attempt < CHUNK_TRIES && !done && !client.cancel_current && !client.stop; attempt++)
		{
			const struct server *server = &download->servers[(fetcher->number + attempt) % download->server_count];
			unsigned char *body;
			size_t length;
			int status;

			if (attempt >= download->server_count)
				sleep_milliseconds(500 * attempt);
			status = sc_http_request(&http, server->host, server->tls, "GET", request, NULL, NULL, 0, &body, &length,
				SC_MAXIMUM_CHUNK, error, sizeof(error));
			last_status = status;
			if (status == 200 && sc_chunk_decode(download->key, body, length, chunk, plain, why, sizeof(why)))
				done = 1;
			else if (status == 200)
				sc_log("Steam: chunk %s from %s: %s", hex, server->host, why);
			else
				sc_log("Steam: chunk %s from %s: %s%d", hex, server->host, status ? "HTTP " : error,
					status ? status : 0);
			free(body);
			if (!done)
				sc_http_close(&http);
		}
		if (!done)
		{
			if (!client.cancel_current && !client.stop)
				download_fail(download, last_status ? steam_content_error_from_result(STEAM_CONTENT_STEP_CHUNK,
					last_status) : STEAM_CONTENT_OFFLINE, "a chunk could not be fetched from any content server");
			break;
		}
		/* (inside its file, as the manifest was checked for: never past it) */
		if (chunk->offset > file->size || chunk->size > file->size - chunk->offset ||
			!file_path(download, file, path, sizeof(path)) || !file_write_at(path, chunk->offset, plain, chunk->size))
		{
			download_fail(download, STEAM_CONTENT_FAILED, "a file could not be written");
			break;
		}
		pthread_mutex_lock(&download->lock);
		download->done += chunk->size;
		pthread_mutex_unlock(&download->lock);
		progress_set(download->id, STEAM_CONTENT_ITEM_DOWNLOADING, 0, download->done, download->manifest.total);
	}
	free(plain);
	sc_http_close(&http);
	return NULL;
}

/* the item's manifest id from the Web API's GetPublishedFileDetails (hcontent_file) */
static int manifest_id(unsigned long long id, uint64_t *manifest, char *why, size_t why_size)
{
	struct sc_http http;
	char form[96], *at;
	unsigned char *body;
	size_t length;
	char error[160];
	int status;

	memset(&http, 0, sizeof(http));
	http.cancel = &client.cancel_current;
	snprintf(form, sizeof(form), "itemcount=1&publishedfileids%%5B0%%5D=%llu", id);
	status = sc_http_request(&http, DETAILS_HOST, 1, "POST", DETAILS_PATH, "application/x-www-form-urlencoded", form,
		strlen(form), &body, &length, 1 << 20, error, sizeof(error));
	sc_http_close(&http);
	if (status != 200)
	{
		snprintf(why, why_size, "the item's details were not given (%s%d)", status ? "HTTP " : error, status);
		free(body);
		return status ? STEAM_CONTENT_FAILED : STEAM_CONTENT_OFFLINE;
	}
	at = strstr((char *)body, "\"hcontent_file\"");
	if (!at || !strstr((char *)body, "\"consumer_app_id\":976730"))
	{
		snprintf(why, why_size, "the item is not an MCC Workshop item with content");
		free(body);
		return STEAM_CONTENT_FAILED;
	}
	at += 15;
	while (*at == ':' || *at == ' ' || *at == '"')
		at++;
	*manifest = strtoull(at, NULL, 10);
	free(body);
	if (!*manifest)
	{
		snprintf(why, why_size, "the item has no content");
		return STEAM_CONTENT_FAILED;
	}
	return STEAM_CONTENT_OK;
}

/* the CM's part: request code, depot key, content servers */
static int content_access(struct download *download, uint64_t manifest, uint64_t *request_code, char *why,
	size_t why_size)
{
	struct sc_cm cm;
	unsigned char body[256], *response;
	struct pb_writer writer;
	size_t length;
	int result, eresult;
	struct pb_reader reader;
	struct pb_field field;

	result = log_on(&cm, &client.cancel_current);
	if (result != STEAM_CONTENT_OK)
	{
		snprintf(why, why_size, "the logon failed");
		return result;
	}
	pb_writer_init(&writer, body, sizeof(body));
	pb_put_varint(&writer, 1, STEAM_CONTENT_APP);
	pb_put_varint(&writer, 2, STEAM_CONTENT_APP);
	pb_put_varint(&writer, 3, manifest);
	pb_put_string(&writer, 4, "public");
	if (!sc_cm_call(&cm, "ContentServerDirectory.GetManifestRequestCode#1", body, writer.length, 1, &response, &length,
		&eresult) || eresult != SC_RESULT_OK)
	{
		if (eresult)
			free(response);
		sc_cm_close(&cm);
		snprintf(why, why_size, "the manifest request code was refused (EResult %d)", eresult);
		return steam_content_error_from_result(STEAM_CONTENT_STEP_MANIFEST, eresult);
	}
	*request_code = pb_get_number(response, length, 1, 0);
	free(response);
	eresult = sc_cm_depot_key(&cm, STEAM_CONTENT_APP, STEAM_CONTENT_APP, download->key);
	if (eresult != SC_RESULT_OK)
	{
		sc_cm_close(&cm);
		snprintf(why, why_size, "the depot key was refused (EResult %d)", eresult);
		return steam_content_error_from_result(STEAM_CONTENT_STEP_DEPOT_KEY, eresult);
	}
	pb_writer_init(&writer, body, sizeof(body));
	pb_put_varint(&writer, 1, cm.cell_id);
	pb_put_varint(&writer, 2, 20);
	if (!sc_cm_call(&cm, "ContentServerDirectory.GetServersForSteamPipe#1", body, writer.length, 1, &response,
		&length, &eresult) || eresult != SC_RESULT_OK)
	{
		if (eresult)
			free(response);
		sc_cm_close(&cm);
		snprintf(why, why_size, "no content servers were given (EResult %d)", eresult);
		return steam_content_error_from_result(STEAM_CONTENT_STEP_CONNECT, eresult);
	}
	sc_cm_close(&cm);
	pb_reader_init(&reader, response, length);
	while (pb_next(&reader, &field) == 1 && download->server_count < MAXIMUM_SERVERS)
	{
		char type[32], host[256], https[32];
		struct server *server;

		if (field.number != 1 || field.wire != PB_BYTES)
			continue;
		pb_get_string(field.data, field.length, 1, type, sizeof(type));
		pb_get_string(field.data, field.length, 8, host, sizeof(host));
		https[0] = 0;
		pb_get_string(field.data, field.length, 12, https, sizeof(https));
		if ((strcmp(type, "CDN") && strcmp(type, "SteamCache")) || !host[0])
			continue;
		server = &download->servers[download->server_count++];
		snprintf(server->host, sizeof(server->host), "%s", host);
		server->tls = !strcmp(https, "mandatory") || !strcmp(https, "optional");
	}
	free(response);
	if (!download->server_count)
	{
		snprintf(why, why_size, "no content servers were given");
		return STEAM_CONTENT_FAILED;
	}
	/* (HTTPS servers only, when there are any: plain HTTP only where none
	offers it, and then the chunks' and files' SHA-1 are what is trusted) */
	{
		int index, kept = 0;

		for (index = 0; index < download->server_count; index++)
		{
			if (download->servers[index].tls)
				download->servers[kept++] = download->servers[index];
		}
		if (kept)
			download->server_count = kept;
		else
			sc_log("Steam: no content server offers HTTPS; using HTTP with SHA-1 checks");
	}
	return STEAM_CONTENT_OK;
}

static int download_item(struct download *download, const char *library, char *why, size_t why_size)
{
	uint64_t manifest = 0, request_code = 0;
	unsigned char *body = NULL;
	size_t length;
	char request[160], error[192], path[STEAM_CONTENT_PATH_SIZE + SC_MAXIMUM_NAME + 64];
	pthread_t threads[STEAM_CONTENT_WORKERS];
	struct fetcher fetchers[STEAM_CONTENT_WORKERS];
	int result, index, started = 0, status = 0, attempt;
	struct sc_http http;

	if (snprintf(download->folder, sizeof(download->folder), "%s/" DOWNLOADS_FOLDER "/%llu", library, download->id) >=
		(int)sizeof(download->folder) || snprintf(download->final_folder, sizeof(download->final_folder),
		"%s/" CONTENT_FOLDER "/%llu", library, download->id) >= (int)sizeof(download->final_folder))
	{
		snprintf(why, why_size, "the folder's path is too long");
		return STEAM_CONTENT_FAILED;
	}
	result = manifest_id(download->id, &manifest, why, why_size);
	if (result != STEAM_CONTENT_OK)
		return result;
	result = content_access(download, manifest, &request_code, why, why_size);
	if (result != STEAM_CONTENT_OK)
		return result;
	sc_log("Steam: item %llu: manifest %llu, %d content servers", download->id, (unsigned long long)manifest,
		download->server_count);
	/* the manifest, from the first server that gives it */
	memset(&http, 0, sizeof(http));
	http.cancel = &client.cancel_current;
	if (request_code)
		snprintf(request, sizeof(request), "/depot/%d/manifest/%llu/5/%llu", STEAM_CONTENT_APP,
			(unsigned long long)manifest, (unsigned long long)request_code);
	else
		snprintf(request, sizeof(request), "/depot/%d/manifest/%llu/5", STEAM_CONTENT_APP, (unsigned long long)manifest);
	for (attempt = 0; attempt < download->server_count && status != 200 && !client.cancel_current; attempt++)
	{
		free(body);
		body = NULL;
		status = sc_http_request(&http, download->servers[attempt].host, download->servers[attempt].tls, "GET",
			request, NULL, NULL, 0, &body, &length, MAXIMUM_MANIFEST, error, sizeof(error));
		if (status != 200)
			sc_log("Steam: the manifest from %s: %s%d", download->servers[attempt].host, status ? "HTTP " : error,
				status);
	}
	sc_http_close(&http);
	if (client.cancel_current)
	{
		free(body);
		return STEAM_CONTENT_CANCELLED;
	}
	if (status != 200)
	{
		free(body);
		snprintf(why, why_size, "no content server gave the manifest");
		return status ? steam_content_error_from_result(STEAM_CONTENT_STEP_CHUNK, status) : STEAM_CONTENT_OFFLINE;
	}
	result = sc_manifest_parse(body, length, download->key, &download->manifest, error, sizeof(error));
	free(body);
	if (!result)
	{
		snprintf(why, why_size, "%s", error);
		return STEAM_CONTENT_FAILED;
	}
	/* (the limits: an item, and each of its files) */
	if (download->manifest.total > item_limit())
	{
		snprintf(why, why_size, "the item is %llu MB, over the %llu MB limit",
			(unsigned long long)(download->manifest.total >> 20), item_limit() >> 20);
		return STEAM_CONTENT_TOO_LARGE;
	}
	for (index = 0; index < download->manifest.file_count; index++)
	{
		if (!(download->manifest.files[index].flags & SC_FILE_DIRECTORY) &&
			download->manifest.files[index].size > file_limit())
		{
			snprintf(why, why_size, "a file of the item is over the %llu MB limit", file_limit() >> 20);
			return STEAM_CONTENT_TOO_LARGE;
		}
	}
	download->chunk_file = (int *)malloc(sizeof(int) * (size_t)(download->manifest.chunk_count + 1));
	if (!download->chunk_file)
		return STEAM_CONTENT_FAILED;
	/* the folders, and each file: kept when whole, else made at its size */
	/* (an installed copy, for an update: moved aside to be put together again,
	its whole files kept; the map lists show it as installing meanwhile) */
	if (!is_link(download->final_folder) && !is_link(download->folder) && make_parents(download->folder))
	{
		struct stat information;

		if (stat(download->final_folder, &information) == 0 && stat(download->folder, &information) != 0 &&
			rename(download->final_folder, download->folder) != 0)
		{
			snprintf(why, why_size, "the installed copy could not be moved for the update");
			return STEAM_CONTENT_FAILED;
		}
	}
	if (!make_parents(download->folder) || (make_folder(download->folder) != 0 && errno != EEXIST))
	{
		snprintf(why, why_size, "the item's folder could not be made");
		return STEAM_CONTENT_FAILED;
	}
	{
		unsigned long long kept = 0;
		int kept_files = 0;

		for (index = 0; index < download->manifest.file_count; index++)
		{
			const struct sc_file *file = &download->manifest.files[index];
			int chunk, whole;

			if (!file_path(download, file, path, sizeof(path)) || !make_parents(path))
			{
				snprintf(why, why_size, "a folder could not be made");
				return STEAM_CONTENT_FAILED;
			}
			if (file->flags & SC_FILE_DIRECTORY)
			{
				if (make_folder(path) != 0 && errno != EEXIST)
				{
					snprintf(why, why_size, "a folder could not be made");
					return STEAM_CONTENT_FAILED;
				}
				continue;
			}
			whole = file_whole(path, file);
			if (whole)
			{
				kept += file->size;
				kept_files++;
			}
			else if (!file_prepare(path, file->size))
			{
				snprintf(why, why_size, "a file could not be made");
				return STEAM_CONTENT_FAILED;
			}
			for (chunk = 0; chunk < file->chunk_count; chunk++)
				download->chunk_file[file->first_chunk + chunk] = whole ? -1 : index;
		}
		/* (room on the disk for what is still to come, and a little more) */
		{
			unsigned long long needed = download->manifest.total - kept, room = disk_free(download->folder);

			if (room && room < needed + (64ull << 20))
			{
				snprintf(why, why_size, "%llu MB is needed and the disk has %llu MB free", needed >> 20, room >> 20);
				return STEAM_CONTENT_DISK_FULL;
			}
		}
		download->done = kept;
		sc_log("Steam: item %llu: %d files, %d chunks, %llu bytes; %d files already whole", download->id,
			download->manifest.file_count, download->manifest.chunk_count,
			(unsigned long long)download->manifest.total, kept_files);
		progress_set(download->id, STEAM_CONTENT_ITEM_DOWNLOADING, 0, download->done, download->manifest.total);
	}
	for (index = 0; index < STEAM_CONTENT_WORKERS; index++)
	{
		fetchers[index].download = download;
		fetchers[index].number = index;
		if (pthread_create(&threads[index], NULL, fetch_thread, &fetchers[index]) == 0)
			started++;
		else
			break;
	}
	if (!started)
		return STEAM_CONTENT_FAILED;
	for (index = 0; index < started; index++)
		pthread_join(threads[index], NULL);
	if (client.cancel_current || client.stop)
		return STEAM_CONTENT_CANCELLED;
	if (download->failed)
	{
		snprintf(why, why_size, "%s", download->why);
		return download->error;
	}
	/* every file checked whole */
	for (index = 0; index < download->manifest.file_count; index++)
	{
		const struct sc_file *file = &download->manifest.files[index];

		if (file->flags & SC_FILE_DIRECTORY)
			continue;
		if (!file_path(download, file, path, sizeof(path)) || !file_whole(path, file))
		{
			snprintf(why, why_size, "a file's SHA-1 is wrong after the download");
			return STEAM_CONTENT_FAILED;
		}
	}
	/* (an update: what the item no longer has deleted) */
	sc_prune(download->folder, "", &download->manifest, 0);
	/* (whole: moved to its content folder, where the game reads it) */
	if (!make_parents(download->final_folder) || is_link(download->final_folder) ||
		rename(download->folder, download->final_folder) != 0)
	{
		snprintf(why, why_size, "the item could not be moved to its folder");
		return STEAM_CONTENT_FAILED;
	}
	return STEAM_CONTENT_OK;
}

static void *worker_thread(void *context)
{
	(void)context;
	for (;;)
	{
		struct download *download;
		char library[STEAM_CONTENT_PATH_SIZE], why[256] = "";
		unsigned long long id = 0;
		int index, result;

		pthread_mutex_lock(&client.lock);
		for (index = 0; index < client.count && !id; index++)
		{
			if (client.items[index].queued)
			{
				client.items[index].queued = 0;
				id = client.items[index].id;
				snprintf(library, sizeof(library), "%s", client.items[index].library);
			}
		}
		if (!id || client.stop)
		{
			client.worker_running = 0;
			pthread_mutex_unlock(&client.lock);
			return NULL;
		}
		client.current = id;
		client.cancel_current = 0;
		pthread_mutex_unlock(&client.lock);
		progress_set(id, STEAM_CONTENT_ITEM_DOWNLOADING, 0, 0, 0);
		download = (struct download *)calloc(1, sizeof(*download));
		if (!download)
			result = STEAM_CONTENT_FAILED;
		else
		{
			long long start = sc_milliseconds();

			download->id = id;
			pthread_mutex_init(&download->lock, NULL);
			sc_log("Steam: item %llu: download started", id);
			result = download_item(download, library, why, sizeof(why));
			if (result == STEAM_CONTENT_OK)
				sc_log("Steam: item %llu: installed (%llu bytes, %lld s)", id,
					(unsigned long long)download->manifest.total, (sc_milliseconds() - start) / 1000);
			else
				sc_log("Steam: item %llu: %s (%s)", id, steam_content_error_text(result), why);
		}
		pthread_mutex_lock(&client.lock);
		{
			struct item *item = item_find(id);

			if (item)
			{
				item->progress.state = result == STEAM_CONTENT_OK ? STEAM_CONTENT_ITEM_INSTALLED :
					result == STEAM_CONTENT_CANCELLED ? STEAM_CONTENT_ITEM_NONE : STEAM_CONTENT_ITEM_FAILED;
				item->progress.error = result;
				if (download && result == STEAM_CONTENT_OK)
					item->progress.done = item->progress.total = download->manifest.total;
			}
			if (result == STEAM_CONTENT_SIGN_IN)
				client.linked = 0;
			client.current = 0;
		}
		pthread_mutex_unlock(&client.lock);
		if (download)
		{
			sc_manifest_free(&download->manifest);
			free(download->chunk_file);
			pthread_mutex_destroy(&download->lock);
			free(download);
		}
	}
}

/* ---------- the interface */

void steam_content_init(const struct steam_content_host *host)
{
	struct sign_in sign_in;

	pthread_mutex_lock(&client.lock);
	sc_host = *host;
	client.initialised = 1;
	client.linked = sign_in_read(&sign_in);
	snprintf(client.persona, sizeof(client.persona), "%s", client.linked ? sign_in.persona : "");
	memset(&sign_in, 0, sizeof(sign_in));
	pthread_mutex_unlock(&client.lock);
}

int steam_content_linked(char *persona, int size)
{
	int linked;

	pthread_mutex_lock(&client.lock);
	linked = client.linked;
	if (persona && size > 0)
		snprintf(persona, (size_t)size, "%s", linked ? client.persona : "");
	pthread_mutex_unlock(&client.lock);
	return linked;
}

void steam_content_sign_in_qr(void)
{
	pthread_t thread;
	int generation;

	pthread_mutex_lock(&client.lock);
	/* (a running one told to stop: its results are no longer taken) */
	client.qr_cancel = 1;
	generation = ++client.qr_generation;
	client.qr_state = STEAM_CONTENT_QR_STARTING;
	client.qr_error = STEAM_CONTENT_OK;
	client.qr_text[0] = 0;
	pthread_mutex_unlock(&client.lock);
	/* (the old thread sees the cancel within a quarter second) */
	{
		long long end = sc_milliseconds() + 2000;
		int running = 1;

		while (running && sc_milliseconds() < end)
		{
			pthread_mutex_lock(&client.lock);
			running = client.qr_running;
			pthread_mutex_unlock(&client.lock);
			if (running)
				sleep_milliseconds(20);
		}
	}
	pthread_mutex_lock(&client.lock);
	client.qr_cancel = 0;
	client.qr_running = 1;
	pthread_mutex_unlock(&client.lock);
	if (pthread_create(&thread, NULL, qr_thread, (void *)(intptr_t)generation) == 0)
		pthread_detach(thread);
	else
	{
		pthread_mutex_lock(&client.lock);
		client.qr_running = 0;
		client.qr_state = STEAM_CONTENT_QR_FAILED;
		client.qr_error = STEAM_CONTENT_FAILED;
		pthread_mutex_unlock(&client.lock);
	}
}

int steam_content_sign_in_poll(char *qr_text, int size, int *error)
{
	int state;

	pthread_mutex_lock(&client.lock);
	state = client.qr_state;
	if (qr_text && size > 0)
		snprintf(qr_text, (size_t)size, "%s", state == STEAM_CONTENT_QR_WAITING || state == STEAM_CONTENT_QR_SCANNED ?
			client.qr_text : "");
	if (error)
		*error = client.qr_error;
	pthread_mutex_unlock(&client.lock);
	return state;
}

void steam_content_sign_in_cancel(void)
{
	pthread_mutex_lock(&client.lock);
	client.qr_cancel = 1;
	client.qr_generation++;
	client.qr_state = STEAM_CONTENT_QR_IDLE;
	client.qr_text[0] = 0;
	pthread_mutex_unlock(&client.lock);
}

int steam_content_get_item(unsigned long long id, const char *library)
{
	struct item *item;
	pthread_t thread;
	int start = 0;

	if (!id || !library || !library[0] || strlen(library) >= STEAM_CONTENT_PATH_SIZE)
		return 0;
	pthread_mutex_lock(&client.lock);
	if (!client.linked)
	{
		pthread_mutex_unlock(&client.lock);
		return 0;
	}
	item = item_find(id);
	if (!item)
	{
		if (client.count >= MAXIMUM_ITEMS)
		{
			/* (a finished one's place taken) */
			int index;

			for (index = 0; index < client.count; index++)
			{
				if (!client.items[index].queued && client.items[index].id != client.current)
				{
					item = &client.items[index];
					break;
				}
			}
			if (!item)
			{
				pthread_mutex_unlock(&client.lock);
				return 0;
			}
		}
		else
			item = &client.items[client.count++];
		memset(item, 0, sizeof(*item));
		item->id = id;
	}
	if (client.current != id)
	{
		snprintf(item->library, sizeof(item->library), "%s", library);
		item->queued = 1;
		item->progress.state = STEAM_CONTENT_ITEM_QUEUED;
		item->progress.error = STEAM_CONTENT_OK;
	}
	if (!client.worker_running)
	{
		client.worker_running = 1;
		client.stop = 0;
		start = 1;
	}
	pthread_mutex_unlock(&client.lock);
	if (start)
	{
		if (pthread_create(&thread, NULL, worker_thread, NULL) == 0)
			pthread_detach(thread);
		else
		{
			pthread_mutex_lock(&client.lock);
			client.worker_running = 0;
			item->progress.state = STEAM_CONTENT_ITEM_FAILED;
			item->progress.error = STEAM_CONTENT_FAILED;
			item->queued = 0;
			pthread_mutex_unlock(&client.lock);
			return 0;
		}
	}
	return 1;
}

int steam_content_progress(unsigned long long id, struct steam_content_progress *progress)
{
	struct item *item;

	memset(progress, 0, sizeof(*progress));
	pthread_mutex_lock(&client.lock);
	item = item_find(id);
	if (item)
		*progress = item->progress;
	pthread_mutex_unlock(&client.lock);
	return item != NULL;
}

void steam_content_cancel(unsigned long long id)
{
	struct item *item;

	pthread_mutex_lock(&client.lock);
	item = item_find(id);
	if (item)
	{
		if (item->queued)
		{
			item->queued = 0;
			item->progress.state = STEAM_CONTENT_ITEM_NONE;
		}
		if (client.current == id)
			client.cancel_current = 1;
	}
	pthread_mutex_unlock(&client.lock);
}

/* the refresh token revoked at Steam (Authentication.RevokeToken, permanent), on a thread of its own */
static void *revoke_thread(void *context)
{
	struct sign_in *sign_in = (struct sign_in *)context;
	volatile int never = 0;
	struct sc_cm cm;
	unsigned char body[2304], *response;
	struct pb_writer writer;
	size_t length;
	int eresult = 0;

	if (sc_cm_connect(&cm, &never) == STEAM_CONTENT_OK)
	{
		if (sc_cm_logon(&cm, sign_in->account, sign_in->token, sign_in->login_id) == SC_RESULT_OK)
		{
			pb_writer_init(&writer, body, sizeof(body));
			pb_put_string(&writer, 1, sign_in->token);
			pb_put_varint(&writer, 2, 1);
			if (!writer.failed && sc_cm_call(&cm, "Authentication.RevokeToken#1", body, writer.length, 1, &response,
				&length, &eresult))
			{
				free(response);
			}
			memset(body, 0, sizeof(body));
		}
		sc_cm_close(&cm);
	}
	if (eresult == SC_RESULT_OK)
		sc_log("Steam: the link was revoked at Steam");
	else
		sc_log("Steam: the link could not be revoked at Steam (EResult %d); it is deleted here, and Steam lets it "
			"lapse unused", eresult);
	memset(sign_in, 0, sizeof(*sign_in));
	free(sign_in);
	pthread_mutex_lock(&client.lock);
	client.revoking = 0;
	pthread_mutex_unlock(&client.lock);
	return NULL;
}

const char *steam_content_store(void)
{
	int in_store;

	pthread_mutex_lock(&client.lock);
	in_store = client.linked ? client.token_in_store : !sc_host.file_only && sc_secret_store_name();
	pthread_mutex_unlock(&client.lock);
	return in_store && sc_secret_store_name() ? sc_secret_store_name() :
		"a private file in the data folder (not encrypted)";
}

int steam_content_store_secure(void)
{
	int secure;

	pthread_mutex_lock(&client.lock);
	secure = !client.linked || client.token_in_store;
	pthread_mutex_unlock(&client.lock);
	return secure;
}

void steam_content_sign_out(void)
{
	char path[STEAM_CONTENT_PATH_SIZE + 32];
	int index;
	struct sign_in *old = (struct sign_in *)calloc(1, sizeof(*old));
	pthread_t thread;

	steam_content_sign_in_cancel();
	pthread_mutex_lock(&client.lock);
	for (index = 0; index < client.count; index++)
	{
		if (client.items[index].queued)
		{
			client.items[index].queued = 0;
			client.items[index].progress.state = STEAM_CONTENT_ITEM_NONE;
		}
	}
	client.cancel_current = 1;
	client.linked = 0;
	client.persona[0] = 0;
	if (old && !sign_in_read(old))
	{
		free(old);
		old = NULL;
	}
	if (old && old->account[0])
		sc_secret_delete(old->account);
	if (sign_in_path(path, sizeof(path)))
		remove_file(path);
	pthread_mutex_unlock(&client.lock);
	/* (Steam told to forget the token too: the copy goes with the thread) */
	pthread_mutex_lock(&client.lock);
	client.revoking = old && old->token[0];
	pthread_mutex_unlock(&client.lock);
	if (old && old->token[0] && pthread_create(&thread, NULL, revoke_thread, old) == 0)
		pthread_detach(thread);
	else if (old)
	{
		pthread_mutex_lock(&client.lock);
		client.revoking = 0;
		pthread_mutex_unlock(&client.lock);
		memset(old, 0, sizeof(*old));
		free(old);
	}
	sc_log("Steam: signed out (the sign-in deleted)");
}

void steam_content_shutdown(void)
{
	/* (an unlink's revoke given its time: it is the player's ask of Steam) */
	long long end = sc_milliseconds() + 15000;
	int running = 1;

	pthread_mutex_lock(&client.lock);
	client.stop = 1;
	client.cancel_current = 1;
	client.qr_cancel = 1;
	pthread_mutex_unlock(&client.lock);
	while (running && sc_milliseconds() < end)
	{
		pthread_mutex_lock(&client.lock);
		running = client.worker_running || client.qr_running || client.revoking;
		pthread_mutex_unlock(&client.lock);
		if (running)
			sleep_milliseconds(20);
	}
}
#endif /* HALO_MCC */
