/*
HOST_EVIDENCE.C

The host's refusals of each client machine, counted (host_evidence.h).
*/

#include "host_evidence.h"

#include <stdio.h>
#include <string.h>

/* ---------- globals */

static unsigned long host_evidence_counts[HOST_EVIDENCE_MACHINES][NUMBER_OF_HOST_EVIDENCE_KINDS];

static char const *const host_evidence_kind_names[NUMBER_OF_HOST_EVIDENCE_KINDS] =
{
	"movement",
	"vehicle",
	"hit_report",
	"clock",
	"line_of_sight",
	"malformed",
};

/* ---------- private code */

static int machine_valid(
	long machine_index)
{
	return machine_index >= 0 && machine_index < HOST_EVIDENCE_MACHINES;
}

/* ---------- public code */

void host_evidence_note(
	long machine_index,
	enum host_evidence_kind kind)
{
	unsigned long *count;

	if (!machine_valid(machine_index) || (int)kind < 0 || kind >= NUMBER_OF_HOST_EVIDENCE_KINDS)
		return;
	count = &host_evidence_counts[machine_index][kind];
	if (*count < HOST_EVIDENCE_MAXIMUM_COUNT)
		(*count)++;
}

void host_evidence_reset_machine(
	long machine_index)
{
	if (machine_valid(machine_index))
		memset(host_evidence_counts[machine_index], 0, sizeof(host_evidence_counts[machine_index]));
}

void host_evidence_reset(
	void)
{
	memset(host_evidence_counts, 0, sizeof(host_evidence_counts));
}

unsigned long host_evidence_count(
	long machine_index,
	enum host_evidence_kind kind)
{
	if (!machine_valid(machine_index) || (int)kind < 0 || kind >= NUMBER_OF_HOST_EVIDENCE_KINDS)
		return 0;
	return host_evidence_counts[machine_index][kind];
}

unsigned long host_evidence_total(
	long machine_index)
{
	unsigned long total = 0;
	int kind;

	if (!machine_valid(machine_index))
		return 0;
	for (kind = 0; kind < NUMBER_OF_HOST_EVIDENCE_KINDS; kind++)
		total += host_evidence_counts[machine_index][kind];
	return total;
}

char const *host_evidence_kind_name(
	int kind)
{
	return kind >= 0 && kind < NUMBER_OF_HOST_EVIDENCE_KINDS ? host_evidence_kind_names[kind] : NULL;
}

int host_evidence_summary(
	long machine_index,
	char *text,
	size_t size)
{
	size_t length = 0;
	int kind;

	if (!text || !size)
		return 0;
	text[0] = 0;
	for (kind = 0; kind < NUMBER_OF_HOST_EVIDENCE_KINDS; kind++)
	{
		unsigned long count = host_evidence_count(machine_index, (enum host_evidence_kind)kind);
		int written;

		if (!count)
			continue;
		written = snprintf(text + length, size - length, "%s%s %lu", length ? ", " : "",
			host_evidence_kind_names[kind], count);
		if (written < 0 || (size_t)written >= size - length)
		{
			text[length] = 0;
			break;
		}
		length += (size_t)written;
	}
	if (!length)
		length = (size_t)snprintf(text, size, "none");
	return (int)(length < size ? length : size - 1);
}
