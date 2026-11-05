/*
DELTA_COMPAT_FUZZ.C

libFuzzer's entry for the join's reserved bytes (tools/test_host_fuzz.py
builds it): an advertisement's reserved bytes, at the length the input has
(in an allocation of its own, so that a read past it is caught) through the
tag, range, decision and wire readers; and a machine name of the join
request, whose first byte picks how many 16-bit characters it holds, through
the join mark's reader and writer.
*/

#include "../src/delta_compat.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static const char *const owns[] = {"chupa-29a", "chupa-41a", "chupa-32a", "bogus"};
	struct delta_wire_id wire, oldest;
	unsigned char *reserved;
	unsigned short *name;
	char theirs[24];
	int major, count, index;

	if (!size)
		return 0;
	reserved = malloc(size);
	if (!reserved)
		return 0;
	memcpy(reserved, data, size);
	delta_compat_read_tag(reserved, (int)size, &major, &wire, &oldest);
	delta_compat_read_range(reserved, (int)size, &wire, &oldest);
	delta_compat_speaks_delta(reserved, (int)size);
	delta_compat_opence_server(reserved, (int)size);
	delta_compat_host_wire(reserved, (int)size, data[0], &wire);
	for (index = 0; index < 4; index++)
	{
		delta_compat_decide(reserved, (int)size, data[0] % 4, owns[index], theirs, (int)sizeof(theirs));
		delta_compat_decide(reserved, (int)size, data[0] % 4, owns[index], theirs, 5);
		delta_compat_decide(reserved, (int)size, data[0] % 4, owns[index], NULL, 0);
	}
	delta_compat_opence_server_listing((int)data[0]);
	free(reserved);

	/* (the machine name: the field's 16-bit characters, the count from the first byte) */
	count = data[0] % 33;
	if ((size_t)(count * 2) <= size - 1 || count == 0)
	{
		name = malloc((size_t)(count ? count : 1) * sizeof(*name));
		if (name)
		{
			memcpy(name, data + 1, (size_t)count * sizeof(*name) <= size - 1 ? (size_t)count * sizeof(*name) : size - 1);
			delta_compat_join_marked(name, count);
			free(name);
		}
	}
	name = malloc((size_t)(count ? count : 1) * sizeof(*name));
	if (name)
	{
		memset(name, 0, (size_t)count * sizeof(*name));
		memcpy(name, data + 1, (size_t)count * sizeof(*name) <= size - 1 ? (size_t)count * sizeof(*name) : size - 1);
		if (delta_compat_join_mark(name, count))
			delta_compat_join_marked(name, count);
		free(name);
	}
	return 0;
}
