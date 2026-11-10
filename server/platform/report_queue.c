/*
REPORT_QUEUE.C

The moderators' queue of players' reports (server_reports.h): kept in
memory (SERVER_REPORTS_KEPT, the oldest closed going first), each also a
JSON line of reports.log in the data folder (with its closing), and sent
to the site by a linked server's poll until the site says it took it. A
report about an OpenCE player stays here: it never goes to the site. Never
an address.
*/

#include "../src/server_reports.h"
#include "control_protocol.h"

/* (the files and random bytes, on Linux and Windows) */
#include "server_host.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* the platform layer's (xbox_files.c): plain types */
const char *platform_data_root(void);

#define REPORTS_FILE "reports.log"

enum
{
	/* the reports one poll sends, at most */
	SITE_REPORTS_PER_POLL = 20,
};

static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	struct server_report reports[SERVER_REPORTS_KEPT];
	/* whether the site still has to take each */
	int for_site[SERVER_REPORTS_KEPT];
	int count;
	unsigned int next_id;
} queue;

static const char *const reason_names[] =
{
	"?", "cheating", "abuse in chat or voice", "griefing or betraying", "offensive name", "something else"
};

const char *server_report_reason_name(int reason)
{
	return reason > 0 && reason < (int)(sizeof(reason_names) / sizeof(reason_names[0])) ? reason_names[reason] : "?";
}

/* (the lock held) the open reports */
static int server_reports_open_locked_count(void)
{
	int index, count = 0;

	for (index = 0; index < queue.count; index++)
		count += !queue.reports[index].closed;
	return count;
}

static void data_file(const char *name, char *path, size_t size)
{
	const char *root = platform_data_root();

	snprintf(path, size, "%s%s%s", root ? root : "", root && root[0] ? "/" : "", name);
}

static int json_text(const char *text, char *out, size_t size)
{
	return control_json_string(text ? text : "", out, size);
}

/* a player as JSON: its length, -1 if it does not fit */
static int player_json(const struct server_report_player *player, int site, char *out, size_t size)
{
	char name[SERVER_REPORT_NAME_SIZE * 6 + 3], handle[SERVER_REPORT_HANDLE_SIZE * 6 + 3];
	size_t length;

	if (json_text(player->name, name, sizeof(name)) < 0 || json_text(player->handle, handle, sizeof(handle)) < 0)
		return -1;
	length = (size_t)snprintf(out, size, "{\"name\": %s, \"handle\": %s, \"account\": %s%s%s", name,
		player->handle[0] ? handle : "null", player->account[0] ? "\"" : "", player->account[0] ? player->account :
		"null", player->account[0] ? "\"" : "");
	if (!site && length < size)
		length += (size_t)snprintf(out + length, size - length, ", \"delta\": %s", player->delta ? "true" : "false");
	if (length < size)
		length += (size_t)snprintf(out + length, size - length, "}");
	return length < size ? (int)length : -1;
}

int server_report_json(const struct server_report *report, int site, char *out, int size)
{
	char target[512], reporter[512], map[SERVER_REPORT_MAP_SIZE * 6 + 3], type[SERVER_REPORT_GAME_TYPE_SIZE * 6 + 3];
	char closed_by[SERVER_REPORT_CLOSED_BY_SIZE * 6 + 3];
	size_t length;

	if (size <= 0 || player_json(&report->target, site, target, sizeof(target)) < 0 ||
		player_json(&report->reporter, site, reporter, sizeof(reporter)) < 0 || json_text(report->map, map, sizeof(map)) < 0 ||
		json_text(report->game_type, type, sizeof(type)) < 0 ||
		json_text(report->closed_by, closed_by, sizeof(closed_by)) < 0)
	{
		return -1;
	}
	length = (size_t)snprintf(out, (size_t)size, "{\"ref\": \"%s\", \"time\": %lld, \"reason\": %d, \"target\": %s, "
		"\"reporter\": %s, \"map\": %s, \"game_type\": %s, \"clip\": %s%s%s, \"evidence\": {\"movement\": %d, "
		"\"vehicle\": %d, \"hits\": %d, \"clock\": %d}", report->ref, report->time, report->reason, target, reporter, map,
		type, report->clip[0] ? "\"" : "", report->clip[0] ? report->clip : "null", report->clip[0] ? "\"" : "",
		report->evidence[0], report->evidence[1], report->evidence[2], report->evidence[3]);
	if (!site && length < (size_t)size)
	{
		length += (size_t)snprintf(out + length, (size_t)size - length, ", \"id\": %u, \"reason_name\": \"%s\", "
			"\"player\": %ld, \"closed\": %s, \"closed_by\": %s", report->id, server_report_reason_name(report->reason),
			report->target_number, report->closed ? "true" : "false", closed_by);
	}
	if (length < (size_t)size)
		length += (size_t)snprintf(out + length, (size_t)size - length, "}");
	return length < (size_t)size ? (int)length : -1;
}

/* (the lock held) a line of reports.log */
static void log_line(const struct server_report *report, const char *what)
{
	char path[1024], json[2048];
	FILE *file;

	if (server_report_json(report, 0, json, sizeof(json)) < 0)
		return;
	data_file(REPORTS_FILE, path, sizeof(path));
	file = fopen(path, "a");
	if (!file)
		return;
	fprintf(file, "{\"event\": \"%s\", \"report\": %s}\n", what, json);
	fclose(file);
}

unsigned int server_reports_add(struct server_report *report)
{
	unsigned char random[16];
	int index;

	if (!host_random_bytes(random, sizeof(random)))
		return 0;
	pthread_mutex_lock(&queue_mutex);
	if (queue.count == SERVER_REPORTS_KEPT)
	{
		/* (room: the oldest closed one, else the oldest) */
		for (index = 0; index < queue.count && !queue.reports[index].closed; index++)
			;
		if (index == queue.count)
			index = 0;
		memmove(&queue.reports[index], &queue.reports[index + 1], (size_t)(queue.count - index - 1) *
			sizeof(queue.reports[0]));
		memmove(&queue.for_site[index], &queue.for_site[index + 1], (size_t)(queue.count - index - 1) *
			sizeof(queue.for_site[0]));
		queue.count--;
	}
	report->id = ++queue.next_id;
	if (!report->id)
		report->id = ++queue.next_id;
	control_hex_text(random, sizeof(random), report->ref);
	if (!report->time)
		report->time = (long long)time(NULL);
	report->closed = 0;
	report->closed_by[0] = 0;
	queue.reports[queue.count] = *report;
	/* (OpenCE players' reports stay here) */
	queue.for_site[queue.count] = report->target.delta;
	queue.count++;
	log_line(report, "report");
	pthread_mutex_unlock(&queue_mutex);
	return report->id;
}

void server_reports_text(int json, int all, char *out, int size)
{
	size_t length = 0;
	int index, shown = 0;

	if (size <= 0)
		return;
	out[0] = 0;
	pthread_mutex_lock(&queue_mutex);
	if (json)
		length = (size_t)snprintf(out, (size_t)size, "{\"reports\": [");
	for (index = queue.count - 1; index >= 0 && length < (size_t)size; index--)
	{
		const struct server_report *report = &queue.reports[index];

		if (report->closed && !all)
			continue;
		if (json)
		{
			int written;

			if (shown && length + 2 < (size_t)size)
			{
				memcpy(out + length, ", ", 3);
				length += 2;
			}
			written = server_report_json(report, 0, out + length, size - (int)length);
			if (written < 0)
				break;
			length += (size_t)written;
		}
		else
		{
			char when[32];
			time_t at = (time_t)report->time;
			struct tm parts;

			gmtime_r(&at, &parts);
			strftime(when, sizeof(when), "%Y-%m-%d %H:%M UTC", &parts);
			length += (size_t)snprintf(out + length, (size_t)size - length, "#%u  %s  %s%s%s reported %s%s%s%s for %s "
				"(%s, refused: movement %d, vehicle %d, hits %d, clock %d)%s%s%s\n", report->id, when,
				report->reporter.name, report->reporter.handle[0] ? " @" : "", report->reporter.handle,
				report->target.name, report->target.handle[0] ? " @" : "", report->target.handle,
				report->target.delta ? "" : " (OpenCE)", server_report_reason_name(report->reason), report->map,
				report->evidence[0], report->evidence[1], report->evidence[2], report->evidence[3],
				report->clip[0] ? ", clip on the site" : "", report->closed ? ", closed by " : "",
				report->closed ? report->closed_by : "");
		}
		shown++;
	}
	if (json && length < (size_t)size)
		snprintf(out + length, (size_t)size - length, "], \"open\": %d}", server_reports_open_locked_count());
	else if (!json && !shown && length < (size_t)size)
		snprintf(out + length, (size_t)size - length, "no %sreports\n", all ? "" : "open ");
	pthread_mutex_unlock(&queue_mutex);
}

int server_reports_close(unsigned int id, const char *by)
{
	int index, found = 0;

	pthread_mutex_lock(&queue_mutex);
	for (index = 0; index < queue.count; index++)
	{
		struct server_report *report = &queue.reports[index];

		if (report->id != id || report->closed)
			continue;
		report->closed = 1;
		snprintf(report->closed_by, sizeof(report->closed_by), "%s", by && by[0] ? by : "-");
		log_line(report, "closed");
		found = 1;
		break;
	}
	pthread_mutex_unlock(&queue_mutex);
	return found;
}

int server_reports_open(void)
{
	int count;

	pthread_mutex_lock(&queue_mutex);
	count = server_reports_open_locked_count();
	pthread_mutex_unlock(&queue_mutex);
	return count;
}

int server_reports_site_payload(char *out, int size)
{
	size_t length = 0;
	int index, sent = 0;

	if (size <= 0)
		return -1;
	out[0] = 0;
	pthread_mutex_lock(&queue_mutex);
	for (index = 0; index < queue.count && sent < SITE_REPORTS_PER_POLL; index++)
	{
		int written;

		if (!queue.for_site[index])
			continue;
		if (!sent)
			length = (size_t)snprintf(out, (size_t)size, "\"reports\": [");
		else if (length + 2 < (size_t)size)
		{
			memcpy(out + length, ", ", 3);
			length += 2;
		}
		if (length >= (size_t)size)
			break;
		written = server_report_json(&queue.reports[index], 1, out + length, size - (int)length);
		if (written < 0)
			break;
		length += (size_t)written;
		sent++;
	}
	pthread_mutex_unlock(&queue_mutex);
	if (!sent)
	{
		out[0] = 0;
		return 0;
	}
	if (length + 2 > (size_t)size)
	{
		out[0] = 0;
		return -1;
	}
	out[length++] = ']';
	out[length] = 0;
	return (int)length;
}

void server_reports_site_taken(const char refs[][SERVER_REPORT_ID_SIZE], int count)
{
	int index, ref;

	pthread_mutex_lock(&queue_mutex);
	for (ref = 0; ref < count; ref++)
	{
		for (index = 0; index < queue.count; index++)
		{
			if (!strcmp(queue.reports[index].ref, refs[ref]))
				queue.for_site[index] = 0;
		}
	}
	pthread_mutex_unlock(&queue_mutex);
}
