/*
METAL_REPORT.C

The Metal renderer's compatibility and failure reports (metal_report.h).
Reports come from the game's thread and from Metal's completion handlers, so
the table is behind a lock. It is bounded: past REPORT_DISTINCT problems,
further new ones are counted but not itemized.
*/

#include "metal_report.h"

#include <assert.h> /* IWYU pragma: keep (static_assert in C11) */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The logger's host-safe ABI only; platform.h brings in Xbox SDK headers
and cannot be included by a standalone native diagnostic/probe build. */
extern void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#define REPORT_DISTINCT 128
#define REPORT_OPERATION 48

struct report_entry
{
	enum metal_report_kind kind;
	char operation[REPORT_OPERATION];
	uint32_t state;
	uint32_t value;
	uint32_t count;
	uint64_t first_frame;
};

_Atomic(uint64_t) metal_report_frame;
_Atomic(uint32_t) metal_report_draw;

static pthread_mutex_t report_lock = PTHREAD_MUTEX_INITIALIZER;
static struct report_entry reports[REPORT_DISTINCT];
static uint32_t report_entries;
static uint32_t report_unitemized;
static uint32_t report_counts[METAL_REPORT_KINDS];
static int report_strict;

static const char *const kind_names[METAL_REPORT_KINDS] = {
	"unsupported Xbox state",
	"unsupported Xbox format",
	"invalid Xbox resource",
	"shader",
	"pipeline",
	"command buffer",
};

static_assert(sizeof(kind_names) / sizeof(kind_names[0]) == METAL_REPORT_KINDS, "a name for every kind");

static int kind_is_failure(enum metal_report_kind kind)
{
	return kind >= METAL_REPORT_SHADER;
}

/* the source file's name from the repository's port/macos/metal on (the
build compiles rewritten copies under build/, whose paths say nothing) */
static const char *short_file(const char *file)
{
	const char *metal = file ? strstr(file, "port/macos/metal/") : NULL;
	return metal ? metal + strlen("port/macos/metal/") : file ? file : "?";
}

void metal_report_configure(int strict)
{
	pthread_mutex_lock(&report_lock);
	report_strict = strict != 0;
	pthread_mutex_unlock(&report_lock);
}

/* Counters saturate: an error storm must not wrap into a clean-looking run. */
static void report_increment(uint32_t *count)
{
	assert(count != NULL);
	if (*count < UINT32_MAX)
		(*count)++;
}

/* Called with report_lock held. Only stored entries can be deduplicated;
excess reports are counted as occurrences, never claimed to be distinct. */
static int report_record(const struct metal_report *report, const char *operation, uint64_t frame)
{
	assert(report != NULL);
	assert(report_entries <= REPORT_DISTINCT);
	report_increment(&report_counts[report->kind]);
	for (uint32_t index = 0; index < report_entries; index++)
	{
		struct report_entry *entry = &reports[index];
		if (entry->kind == report->kind && entry->state == report->state &&
			entry->value == report->value && !strncmp(entry->operation, operation, REPORT_OPERATION - 1))
		{
			report_increment(&entry->count);
			return 0;
		}
	}
	if (report_entries == REPORT_DISTINCT)
	{
		report_increment(&report_unitemized);
		return 0;
	}
	struct report_entry *entry = &reports[report_entries++];
	entry->kind = report->kind;
	snprintf(entry->operation, sizeof(entry->operation), "%s", operation);
	entry->state = report->state;
	entry->value = report->value;
	entry->count = 1;
	entry->first_frame = frame;
	return 1;
}

int metal_report_issue(const struct metal_report *report)
{
	if (!report || (unsigned)report->kind >= METAL_REPORT_KINDS)
		return 0;
	const char *operation = report->operation ? report->operation : "?";
	uint64_t frame = atomic_load_explicit(&metal_report_frame, memory_order_relaxed);
	uint32_t draw = atomic_load_explicit(&metal_report_draw, memory_order_relaxed);
	pthread_mutex_lock(&report_lock);
	uint32_t previous_unitemized = report_unitemized;
	int recorded = report_record(report, operation, frame);
	int overflow_first = previous_unitemized == 0 && report_unitemized != 0;
	int strict = report_strict;
	pthread_mutex_unlock(&report_lock);

	if (recorded || strict)
	{
		platform_log("Metal %s: %s: %s (state %u, value 0x%08x) at frame %llu, draw %u%s%s%s%s [%s:%d]",
			kind_is_failure(report->kind) ? "failure" : "compatibility", kind_names[report->kind], operation,
			(unsigned)report->state, (unsigned)report->value, (unsigned long long)frame, (unsigned)draw,
			report->fallback ? "; " : "", report->fallback ? report->fallback : "",
			report->detail ? ":\n" : "", report->detail ? report->detail : "",
			short_file(report->file), report->line);
	}
	if (overflow_first)
		platform_log("Metal: report table full; further unitemized occurrences counted at exit");
	if (strict)
	{
		platform_log("Metal: stopping at the first report (debug.metal_strict)");
		abort();
	}
	return recorded;
}

uint32_t metal_report_count(enum metal_report_kind kind)
{
	uint32_t count;

	if ((unsigned)kind >= METAL_REPORT_KINDS)
		return 0;
	pthread_mutex_lock(&report_lock);
	count = report_counts[kind];
	pthread_mutex_unlock(&report_lock);
	return count;
}

uint32_t metal_report_distinct(void)
{
	uint32_t count;

	pthread_mutex_lock(&report_lock);
	count = report_entries;
	pthread_mutex_unlock(&report_lock);
	return count;
}

void metal_report_summary(void)
{
	uint32_t index;

	pthread_mutex_lock(&report_lock);
	if (report_entries)
	{
		platform_log("Metal reports this run: %u itemized problem(s), %u additional occurrence(s) not itemized",
			(unsigned)report_entries, (unsigned)report_unitemized);
		for (index = 0; index < report_entries; index++)
		{
			const struct report_entry *entry = &reports[index];
			platform_log("    %s %s: %s (state %u, value 0x%08x): %u time(s), first at frame %llu",
				kind_is_failure(entry->kind) ? "failure" : "compatibility", kind_names[entry->kind], entry->operation,
				(unsigned)entry->state, (unsigned)entry->value, (unsigned)entry->count,
				(unsigned long long)entry->first_frame);
		}
	}
	pthread_mutex_unlock(&report_lock);
}

void metal_report_reset(void)
{
	pthread_mutex_lock(&report_lock);
	memset(reports, 0, sizeof(reports));
	memset(report_counts, 0, sizeof(report_counts));
	report_entries = 0;
	report_unitemized = 0;
	report_strict = 0;
	metal_report_frame = 0;
	metal_report_draw = 0;
	pthread_mutex_unlock(&report_lock);
}
