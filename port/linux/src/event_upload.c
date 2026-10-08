/*
EVENT_UPLOAD.C

Delta Stats' uploads (event_log.h, docs/delta.md "Delta Stats"): the
batches of the games this machine hosts, when network.report_events is on
(on by default; HALO_NET_REPORT_EVENTS=false turns it off), compressed and sent to the game list
(network.browser_url) as POST /v1/events, on a thread of this file's.

Who may send: a server with a token the game list's operator made for it
(network.events_token, HALO_EVENTS_TOKEN: "Authorization: Bearer"), or any
host whose game the list has from the same address (its invite is in the
batch), as for the carnage report. The token goes only to an HTTPS server,
or to one on this computer (a test).

A batch is sent a few seconds after its game ends (after the carnage
report, which the list matches it to), and parts of a long game every
network.events_part_minutes. One that gets no answer, or a 5xx or 429, is
sent again later, for up to a day; one refused otherwise is dropped (it
would be refused again). A newer part of a game replaces the one waiting.
At most MAXIMUM_WAITING batches wait; the oldest goes first.

network.events_folder (empty for none) keeps a copy of each batch as it
is sent, a file each (<game>-<part>.json), for the operator.
*/

#ifdef HALO_GAME_BROWSER

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "browser_http.h"
#include "build_identity.h"
#include "p2p.h"
#include "p2p_internal.h"
#include "event_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#endif

enum
{
	MAXIMUM_WAITING = 8,
	/* the first try this long after the game's end (its carnage report goes
	first), then again after each failure, twice as long each time */
	FIRST_DELAY = 5000,
	RETRY_DELAY = 30000,
	MAXIMUM_RETRY_DELAY = 30 * 60 * 1000,
	MAXIMUM_ATTEMPTS = 14,
	THREAD_INTERVAL = 500,
	TOKEN_LENGTH = 69,
};

struct waiting
{
	char game_id[40];
	unsigned char *data;
	size_t length;
	int attempts;
	unsigned long due;
};

static pthread_mutex_t upload_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t upload_once = PTHREAD_ONCE_INIT;

static struct
{
	int count;
	struct waiting waiting[MAXIMUM_WAITING];
	/* the CPU sample's last reading */
	double cpu_seconds;
	double wall_seconds;
} upload;

/* ---------- private code */

static void event_url(char *url, int size)
{
	const char *base = config_string("network.browser_url");
	size_t length = strlen(base);

	while (length && base[length - 1] == '/')
		length--;
	snprintf(url, (size_t)size, "%.*s/v1/events", (int)length, base);
}

/* the token goes to an HTTPS server, or one on this machine (a test) */
static int safe_for_token(const char *url)
{
	return !strncmp(url, "https://", 8) || !strncmp(url, "http://127.0.0.1", 16) ||
		!strncmp(url, "http://localhost", 16);
}

static int due(unsigned long when)
{
	return (long)(p2p_now() - when) >= 0;
}

static void send_one(void)
{
	struct waiting item;
	char url[512], headers[256], response[512], error[256];
	const char *token = config_string("network.events_token");
	int index, status, found = -1;

	pthread_mutex_lock(&upload_lock);
	for (index = 0; index < upload.count; index++)
	{
		if (due(upload.waiting[index].due))
		{
			found = index;
			break;
		}
	}
	if (found < 0)
	{
		pthread_mutex_unlock(&upload_lock);
		return;
	}
	item = upload.waiting[found];
	/* (out of the queue while it is sent: a newer part may come meanwhile) */
	memmove(&upload.waiting[found], &upload.waiting[found + 1], (size_t)(upload.count - found - 1) * sizeof(item));
	upload.count--;
	pthread_mutex_unlock(&upload_lock);

	event_url(url, sizeof(url));
	snprintf(headers, sizeof(headers), "Content-Encoding: gzip\r\n");
	if (token[0])
	{
		if (!safe_for_token(url) || strlen(token) > TOKEN_LENGTH)
		{
			platform_log("Delta Stats: network.events_token goes only to an https:// game list: game %s not sent",
				item.game_id);
			free(item.data);
			return;
		}
		snprintf(headers + strlen(headers), sizeof(headers) - strlen(headers), "Authorization: Bearer %s\r\n", token);
	}
	status = posix_browser_send(url, (const char *)item.data, item.length, "application/json", headers, response,
		sizeof(response), error, sizeof(error));
	memset(headers, 0, sizeof(headers));
	response[strcspn(response, "\r\n")] = 0;
	if (status == 200)
	{
		platform_log("Delta Stats: game %s sent (%u bytes): %s", item.game_id, (unsigned int)item.length, response);
		free(item.data);
		return;
	}
	if ((status && status < 500 && status != 429) || ++item.attempts >= MAXIMUM_ATTEMPTS)
	{
		platform_log("Delta Stats: game %s not taken (%d %s%s)", item.game_id, status, status ? response : error,
			status ? "" : ", given up");
		free(item.data);
		return;
	}
	{
		unsigned long delay = RETRY_DELAY;

		for (index = 1; index < item.attempts && delay < MAXIMUM_RETRY_DELAY; index++)
			delay *= 2;
		if (delay > MAXIMUM_RETRY_DELAY)
			delay = MAXIMUM_RETRY_DELAY;
		item.due = p2p_now() + delay;
		platform_log("Delta Stats: game %s not sent yet (%d %s): again in %lu s", item.game_id, status,
			status ? response : error, delay / 1000);
	}
	pthread_mutex_lock(&upload_lock);
	/* (unless a newer part came meanwhile, or the queue filled) */
	for (index = 0; index < upload.count; index++)
	{
		if (!strcmp(upload.waiting[index].game_id, item.game_id))
			break;
	}
	if (index == upload.count && upload.count < MAXIMUM_WAITING)
		upload.waiting[upload.count++] = item;
	else
		free(item.data);
	pthread_mutex_unlock(&upload_lock);
}

static void *upload_thread(void *unused)
{
	(void)unused;
	for (;;)
	{
		if (config_string("network.browser_url")[0])
			send_one();
		Sleep(THREAD_INTERVAL);
	}
	return NULL;
}

/* the batches waiting as the program exits (a dedicated server stopped
just after its game's end): sent now, each tried once */
static void flush_at_exit(void)
{
	int index, count;

	pthread_mutex_lock(&upload_lock);
	count = upload.count;
	for (index = 0; index < upload.count; index++)
		upload.waiting[index].due = p2p_now();
	pthread_mutex_unlock(&upload_lock);
	while (count-- > 0)
		send_one();
}

static void start_thread(void)
{
	pthread_t thread;

	atexit(flush_at_exit);

	if (pthread_create(&thread, NULL, upload_thread, NULL) == 0)
		pthread_detach(thread);
	else
		platform_log("Delta Stats: could not start its thread");
}

/* a copy of the batch in network.events_folder, if it names one */
static void keep_copy(const char *json, size_t length, const char *game_id)
{
	const char *folder = config_string("network.events_folder");
	char path[1024];
	FILE *file;
	int part = 0;
	const char *at = strstr(json, "\"part\": ");

	if (!folder[0])
		return;
	if (at)
		part = atoi(at + 8);
	snprintf(path, sizeof(path), "%s/%s-%d.json", folder, game_id, part);
	file = fopen(path, "wb");
	if (!file)
	{
		platform_log("Delta Stats: cannot write %s (network.events_folder)", path);
		return;
	}
	fwrite(json, 1, length, file);
	fclose(file);
}

/* moderators' actions waiting for the game's thread */
enum { MAXIMUM_MODERATION = 32 };
static struct
{
	int count;
	struct
	{
		int kind;
		char who[EVENT_LOG_NAME_SIZE];
		char by[64];
		char reason[EVENT_LOG_TAG_SIZE];
	} actions[MAXIMUM_MODERATION];
} moderation;

/* ---------- public code */

void event_upload_moderation(int kind, char const *who, char const *by, char const *reason)
{
	pthread_mutex_lock(&upload_lock);
	if (moderation.count < MAXIMUM_MODERATION)
	{
		moderation.actions[moderation.count].kind = kind;
		snprintf(moderation.actions[moderation.count].who, sizeof(moderation.actions[0].who), "%s", who ? who : "");
		snprintf(moderation.actions[moderation.count].by, sizeof(moderation.actions[0].by), "%s", by ? by : "");
		snprintf(moderation.actions[moderation.count].reason, sizeof(moderation.actions[0].reason), "%s", reason ? reason : "");
		moderation.count++;
	}
	pthread_mutex_unlock(&upload_lock);
}

void event_upload_moderation_drain(void)
{
	int index, count;

	pthread_mutex_lock(&upload_lock);
	count = moderation.count;
	for (index = 0; index < count; index++)
		event_log_moderation(moderation.actions[index].kind, moderation.actions[index].who, moderation.actions[index].by,
			moderation.actions[index].reason);
	moderation.count = 0;
	pthread_mutex_unlock(&upload_lock);
}

int event_upload_enabled(void)
{
	return config_boolean("network.report_events") && config_string("network.browser_url")[0];
}

int event_upload_event_limit(void)
{
	long limit = config_integer("network.events_limit");

	return limit < EVENT_LOG_MINIMUM_EVENTS ? EVENT_LOG_MINIMUM_EVENTS :
		limit > EVENT_LOG_MAXIMUM_EVENTS ? EVENT_LOG_MAXIMUM_EVENTS : (int)limit;
}

int event_upload_position_seconds(void)
{
	long seconds = config_integer("network.events_positions");

	return seconds < 0 ? 0 : seconds > 60 ? 60 : (int)seconds;
}

void event_upload_submit(char *json, size_t length, char const *game_id)
{
	struct waiting item;
	int index;

	if (!json)
		return;
	keep_copy(json, length, game_id);
	memset(&item, 0, sizeof(item));
	snprintf(item.game_id, sizeof(item.game_id), "%s", game_id);
	item.data = event_gzip((const unsigned char *)json, length, &item.length);
	free(json);
	if (!item.data)
	{
		platform_log("Delta Stats: out of memory for game %s", item.game_id);
		return;
	}
	item.due = p2p_now() + FIRST_DELAY;
	pthread_once(&upload_once, start_thread);
	pthread_mutex_lock(&upload_lock);
	for (index = 0; index < upload.count; index++)
	{
		/* (a newer part of the same game replaces the one waiting) */
		if (!strcmp(upload.waiting[index].game_id, item.game_id))
		{
			free(upload.waiting[index].data);
			upload.waiting[index] = item;
			pthread_mutex_unlock(&upload_lock);
			return;
		}
	}
	if (upload.count == MAXIMUM_WAITING)
	{
		platform_log("Delta Stats: game %s dropped (too many waiting)", upload.waiting[0].game_id);
		free(upload.waiting[0].data);
		memmove(&upload.waiting[0], &upload.waiting[1], (size_t)(upload.count - 1) * sizeof(item));
		upload.count--;
	}
	upload.waiting[upload.count++] = item;
	pthread_mutex_unlock(&upload_lock);
}

void event_upload_random(unsigned char *bytes, int count)
{
	posix_random_bytes(bytes, (posix_ulong)count);
}

void event_upload_system_sample(int *cpu_permille, int *resident_kb)
{
	*cpu_permille = -1;
	*resident_kb = -1;
	/* (not in the Android app's game, which has no getrusage: the sample's
	CPU and memory are left unknown there) */
#if !defined(_WIN32) && !defined(HALO_ANDROID)
	{
		struct rusage usage;
		struct timeval now;

		if (getrusage(RUSAGE_SELF, &usage) == 0 && gettimeofday(&now, NULL) == 0)
		{
			double cpu = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 + usage.ru_stime.tv_sec +
				usage.ru_stime.tv_usec / 1e6;
			double wall = now.tv_sec + now.tv_usec / 1e6;

			if (upload.wall_seconds > 0.0 && wall > upload.wall_seconds)
				*cpu_permille = (int)(1000.0 * (cpu - upload.cpu_seconds) / (wall - upload.wall_seconds) + 0.5);
			upload.cpu_seconds = cpu;
			upload.wall_seconds = wall;
		}
	}
#endif
#ifdef __linux__
	{
		FILE *file = fopen("/proc/self/statm", "r");
		unsigned long size, resident;

		if (file)
		{
			if (fscanf(file, "%lu %lu", &size, &resident) == 2)
				*resident_kb = (int)(resident * (unsigned long)sysconf(_SC_PAGESIZE) / 1024);
			fclose(file);
		}
	}
#endif
}

unsigned int event_upload_time(void)
{
	return (unsigned int)time(NULL);
}

void event_upload_invite(char *text, int size)
{
	if (!p2p_hosting_invite(text, size))
		text[0] = 0;
}

#endif
