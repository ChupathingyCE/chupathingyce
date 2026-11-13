/*
MCC_CACHE_FUZZ.C

libFuzzer's entry for a Halo 1 MCC cache file (tools/test_host_fuzz.py builds
it, target mcc_cache): a Workshop item's map, which anyone can publish. The
input is the map file itself, read through an exact-size allocation: the
version-13 reader's checks (mcc_cache_format.c, mcc_cache_inspect) and, for a
map they pass, its walk over the external bitmaps and sounds the tags name
(mcc_cache_visit_resources), as mcc_resources.c walks them.
*/

#include "../game/mcc_cache_format.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct memory
{
	const unsigned char *bytes;
	uint64_t size;
};

static int memory_read(void *context, uint32_t offset, uint32_t size, void *out)
{
	const struct memory *memory = context;

	if (offset > memory->size || size > memory->size - offset)
		return 0;
	memcpy(out, memory->bytes + offset, size);
	return 1;
}

static int visit(void *context, struct mcc_resource_reference const *reference)
{
	unsigned long *count = context;

	/* (the name is read as resource selection reads it) */
	*count += strlen(reference->tag_name) + reference->type;
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct mcc_cache_source source;
	struct mcc_cache_report report;
	struct memory memory;
	unsigned char *copy;
	unsigned long visited = 0;
	void *tags;

	copy = malloc(size ? size : 1);
	if (!copy)
		return 0;
	memcpy(copy, data, size);
	memory.bytes = copy;
	memory.size = size;
	source.context = &memory;
	source.read = memory_read;
	source.size = size;
	if (mcc_cache_inspect(&source, &report) == MCC_CACHE_OK)
	{
		tags = malloc(report.identity.tag_data_size);
		if (tags && mcc_cache_load(&source, tags, report.identity.tag_data_size, &report) == MCC_CACHE_OK)
			mcc_cache_visit_resources(tags, &report, visit, &visited);
		free(tags);
	}
	free(copy);
	return 0;
}
