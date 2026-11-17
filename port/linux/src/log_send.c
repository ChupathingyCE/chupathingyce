/*
LOG_SEND.C

Send my logs (log_send.h): the files read into a multipart form as the
site's page sends it, each through crash_report_redact, and posted on a
thread of its own (posix_browser_send). Moved here from the setup screen
(launcher.c) so that the Delta Guide sends the same way.
*/

#include "log_send.h"

#ifdef HALO_GAME_BROWSER

#include "platform.h"
#include "port_config.h"
#include "posix.h"
#include "crash_report.h"
#include "browser_http.h"
#include "build_identity.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* where the logs go, unless debug.dump_url says otherwise */
#define LOG_SEND_URL "https://" LOG_SEND_SITE "/upload"

enum
{
	LOG_SEND_PATH_SIZE = 1024,
	/* debug.txt's end that is sent, at most (the site takes 10 MB a file) */
	LOG_SEND_LOG_TAIL = 4 * 1024 * 1024,
	LOG_SEND_SETTINGS_MAXIMUM = 1024 * 1024,
};

/* the upload, on a thread of its own: running while the thread has it
(state SENDING); the thread frees the body and sets done last */
static struct
{
	int running; /* set from the send thread: __atomic, as the dedicated server has no SDL */
	int done;
	int state;
	char url[512];
	char *body;
	size_t body_length;
	char content_type[96];
	char headers[256];
	int status;
	char response[2048];
	char error[256];
	char code[32];
	char failed[256];
} log_send;

struct text
{
	char *data;
	size_t length, size;
};

static void text_add(struct text *text, const void *data, size_t length)
{
	if (!text->data && text->size == (size_t)-1)
		return;
	if (text->length + length + 1 > text->size)
	{
		size_t size = (text->length + length + 1) * 2;
		char *grown = realloc(text->data, size);

		if (!grown)
		{
			free(text->data);
			text->data = NULL;
			text->size = (size_t)-1;
			return;
		}
		text->data = grown;
		text->size = size;
	}
	memcpy(text->data + text->length, data, length);
	text->length += length;
	text->data[text->length] = '\0';
}

static void text_string(struct text *text, const char *string)
{
	text_add(text, string, strlen(string));
}

static int ends_with(const char *name, const char *ending)
{
	size_t length = strlen(name), ending_length = strlen(ending), index;

	if (length < ending_length)
		return 0;
	for (index = 0; index < ending_length; index++)
	{
		if (tolower((unsigned char)name[length - ending_length + index]) != tolower((unsigned char)ending[index]))
			return 0;
	}
	return 1;
}

/* a file's text, its last maximum bytes from the start of a line, with
its addresses and home folder taken out; NULL when there is none */
static char *redacted_file(const char *path, size_t maximum, size_t *length)
{
	FILE *file = fopen(path, "rb");
	long size, start;
	char *data, *redacted;
	size_t read, out_size;
	const char *from;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	start = size > (long)maximum ? size - (long)maximum : 0;
	fseek(file, start, SEEK_SET);
	data = size > 0 ? malloc((size_t)(size - start) + 1) : NULL;
	if (!data)
	{
		fclose(file);
		return NULL;
	}
	read = fread(data, 1, (size_t)(size - start), file);
	fclose(file);
	data[read] = '\0';
	from = data;
	if (start > 0)
	{
		const char *line = memchr(data, '\n', read);

		if (line)
			from = line + 1;
	}
	out_size = 3 * (read - (size_t)(from - data)) + 1;
	redacted = malloc(out_size);
	if (!redacted)
	{
		free(data);
		return NULL;
	}
	*length = crash_report_redact(from, read - (size_t)(from - data), redacted, out_size);
	free(data);
	return redacted;
}

/* the newest crash report in the logs folder's crashes */
static int newest_crash_report(const char *logs_folder, char *path, size_t size)
{
	char folder[LOG_SEND_PATH_SIZE + 16], name[256];
	void *directory;
	time_t newest = 0;

	path[0] = '\0';
	snprintf(folder, sizeof(folder), "%s/crashes", logs_folder);
	directory = posix_directory_open(folder);
	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		char candidate[LOG_SEND_PATH_SIZE + 300];
		struct posix_file_information information;

		if (!ends_with(name, ".txt"))
			continue;
		snprintf(candidate, sizeof(candidate), "%s/%s", folder, name);
		if (posix_stat(candidate, &information) == 0 && (time_t)information.modification_seconds >= newest)
		{
			newest = (time_t)information.modification_seconds;
			snprintf(path, size, "%s", candidate);
		}
	}
	if (directory)
		posix_directory_close(directory);
	return path[0] != '\0';
}

static void form_field(struct text *body, const char *boundary, const char *name, const char *value)
{
	text_string(body, "--");
	text_string(body, boundary);
	text_string(body, "\r\nContent-Disposition: form-data; name=\"");
	text_string(body, name);
	text_string(body, "\"\r\n\r\n");
	text_string(body, value);
	text_string(body, "\r\n");
}

static int form_file(struct text *body, const char *boundary, const char *name, const char *path, size_t maximum)
{
	size_t length = 0;
	char *data = redacted_file(path, maximum, &length);

	if (!data)
		return 0;
	text_string(body, "--");
	text_string(body, boundary);
	text_string(body, "\r\nContent-Disposition: form-data; name=\"files\"; filename=\"");
	text_string(body, name);
	text_string(body, "\"\r\nContent-Type: text/plain\r\n\r\n");
	text_add(body, data, length);
	text_string(body, "\r\n");
	free(data);
	return 1;
}

/* the site's platform names (puma's dumps.py PLATFORMS) */
static const char *dump_platform(void)
{
	const char *platform = build_identity_platform();
	const char *deck = getenv("SteamDeck");

	/* (SteamOS: the site still takes it by its old name) */
	if (deck && !strcmp(deck, "1"))
		return "Steam Deck";
	if (!strcmp(platform, "macOS"))
		return "Mac";
	if (!strcmp(platform, "Windows") || !strcmp(platform, "Linux"))
		return platform;
	return "Other";
}

/* a string field of the site's answer ("ref", "error") */
static int json_string(const char *json, const char *name, char *value, size_t size)
{
	char key[64];
	const char *cursor;
	size_t length = 0;

	snprintf(key, sizeof(key), "\"%s\"", name);
	cursor = strstr(json, key);
	if (!cursor)
		return 0;
	cursor += strlen(key);
	while (*cursor == ' ' || *cursor == ':')
		cursor++;
	if (*cursor++ != '"')
		return 0;
	while (*cursor && *cursor != '"' && length + 1 < size)
	{
		if (*cursor == '\\' && cursor[1])
			cursor++;
		value[length++] = *cursor++;
	}
	value[length] = '\0';
	return 1;
}

static void *upload_thread(void *context)
{
	(void)context;
	log_send.status = posix_browser_send(log_send.url, log_send.body, log_send.body_length, log_send.content_type,
		log_send.headers, log_send.response, (int)sizeof(log_send.response), log_send.error,
		(int)sizeof(log_send.error));
	free(log_send.body);
	log_send.body = NULL;
	__atomic_store_n(&log_send.done, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

/* the thread's answer read (the main thread's, once done) */
static void upload_finished(void)
{
	char error[256];
	size_t index;

	__atomic_store_n(&log_send.done, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n(&log_send.running, 0, __ATOMIC_SEQ_CST);
	if (log_send.status == 200 && json_string(log_send.response, "ref", log_send.code, sizeof(log_send.code)))
	{
		for (index = 0; log_send.code[index]; index++)
			log_send.code[index] = (char)toupper((unsigned char)log_send.code[index]);
		platform_log("logs: sent");
		log_send.state = LOG_SEND_SENT;
		return;
	}
	platform_log("logs: not sent (status %d)", log_send.status);
	log_send.state = LOG_SEND_FAILED;
	if (log_send.status && json_string(log_send.response, "error", error, sizeof(error)))
		snprintf(log_send.failed, sizeof(log_send.failed), "%s", error);
	else if (!log_send.status)
		snprintf(log_send.failed, sizeof(log_send.failed), "We couldn't reach the site. Check your connection and try again.");
	else
		snprintf(log_send.failed, sizeof(log_send.failed), "The site couldn't take them right now. Try again in a while.");
}

int log_send_start(const char *logs_folder, int with_settings, char *error, int error_size)
{
	static const char boundary[] = "chupathingy-launcher-logs-5f3a";
	struct text body = { NULL, 0, 0 };
	char path[LOG_SEND_PATH_SIZE + 64], note[256];
	const char *url = *config_string("debug.dump_url") ? config_string("debug.dump_url") : LOG_SEND_URL;
	const char *host, *host_end;
	int files = 0;
	pthread_t thread;

	if (__atomic_load_n(&log_send.running, __ATOMIC_SEQ_CST))
	{
		snprintf(error, (size_t)error_size, "Your logs are still being sent.");
		return LOG_SEND_FAILED;
	}
	snprintf(note, sizeof(note), "Sent from the game: %s", build_identity());
	form_field(&body, boundary, "name", "Chupathingy player");
	form_field(&body, boundary, "platform", dump_platform());
	form_field(&body, boundary, "note", note);
	snprintf(path, sizeof(path), "%s/debug.txt", logs_folder);
	files += form_file(&body, boundary, "debug.txt", path, LOG_SEND_LOG_TAIL);
	if (newest_crash_report(logs_folder, path, sizeof(path)))
		files += form_file(&body, boundary, "crash_report.txt", path, CRASH_REPORT_MAXIMUM_FILE);
	if (with_settings)
	{
		config_folder(path, sizeof(path));
		strncat(path, "config.toml", sizeof(path) - strlen(path) - 1);
		files += form_file(&body, boundary, "config.toml", path, LOG_SEND_SETTINGS_MAXIMUM);
	}
	text_string(&body, "--");
	text_string(&body, boundary);
	text_string(&body, "--\r\n");
	if (!body.data || !files)
	{
		free(body.data);
		snprintf(error, (size_t)error_size, "There's no debug.txt yet. Play once, then send.");
		return LOG_SEND_NOTHING;
	}
	memset(log_send.response, 0, sizeof(log_send.response));
	log_send.error[0] = log_send.code[0] = log_send.failed[0] = '\0';
	log_send.status = 0;
	snprintf(log_send.url, sizeof(log_send.url), "%s", url);
	log_send.body = body.data;
	log_send.body_length = body.length;
	snprintf(log_send.content_type, sizeof(log_send.content_type), "multipart/form-data; boundary=%s", boundary);
	/* (the page's own origin, as its form sends: the site takes uploads
	from that page) */
	host = strstr(url, "://");
	host = host ? host + 3 : url;
	host_end = host + strcspn(host, ":/");
	snprintf(log_send.headers, sizeof(log_send.headers), "Origin: https://%.*s\r\n", (int)(host_end - host), host);
	__atomic_store_n(&log_send.done, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n(&log_send.running, 1, __ATOMIC_SEQ_CST);
	if (pthread_create(&thread, NULL, upload_thread, NULL) != 0)
	{
		__atomic_store_n(&log_send.running, 0, __ATOMIC_SEQ_CST);
		free(log_send.body);
		log_send.body = NULL;
		snprintf(error, (size_t)error_size, "The upload couldn't start.");
		return LOG_SEND_FAILED;
	}
	pthread_detach(thread);
	log_send.state = LOG_SEND_SENDING;
	platform_log("logs: sending (%d files%s)", files, with_settings ? ", with config.toml" : "");
	return LOG_SEND_SENDING;
}

int log_send_poll(char *code, int code_size, char *error, int error_size)
{
	if (log_send.state == LOG_SEND_SENDING && __atomic_load_n(&log_send.done, __ATOMIC_SEQ_CST))
		upload_finished();
	if (code && code_size > 0)
		snprintf(code, (size_t)code_size, "%s", log_send.state == LOG_SEND_SENT ? log_send.code : "");
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "%s", log_send.state == LOG_SEND_FAILED ? log_send.failed : "");
	return log_send.state;
}

void log_send_forget(void)
{
	if (log_send.state != LOG_SEND_SENDING)
		log_send.state = LOG_SEND_IDLE;
}

void log_send_folder_url(const char *path, char *url, int size)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t length;
	const unsigned char *cursor;

	if (size <= 0)
		return;
	length = (size_t)snprintf(url, (size_t)size, "file://%s", path[0] == '/' ? "" : "/");
	for (cursor = (const unsigned char *)path; *cursor && length + 4 < (size_t)size; cursor++)
	{
		unsigned char character = *cursor == '\\' ? '/' : *cursor;

		if (isalnum(character) || strchr("/-_.~:", character))
			url[length++] = (char)character;
		else
		{
			url[length++] = '%';
			url[length++] = hex[character >> 4];
			url[length++] = hex[character & 15];
		}
	}
	url[length] = '\0';
}

#else

/* (a build without the game browser's HTTP, the consoles: nothing is sent,
and the player is told so if a screen asks) */
#include <string.h>

int log_send_start(const char *logs_folder, int with_settings, char *error, int error_size)
{
	(void)logs_folder; (void)with_settings;
	if (error && error_size > 0)
	{
		strncpy(error, "This build cannot send logs.", (size_t)error_size - 1);
		error[error_size - 1] = 0;
	}
	return LOG_SEND_FAILED;
}

int log_send_poll(char *code, int code_size, char *error, int error_size)
{
	if (code && code_size > 0) code[0] = 0;
	if (error && error_size > 0) error[0] = 0;
	return LOG_SEND_IDLE;
}

void log_send_forget(void) { }

void log_send_folder_url(const char *path, char *url, int size)
{
	if (!url || size <= 0) return;
	strncpy(url, path ? path : "", (size_t)size - 1);
	url[size - 1] = 0;
}

#endif
