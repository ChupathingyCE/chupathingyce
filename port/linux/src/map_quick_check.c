/*
MAP_QUICK_CHECK.C

The quick checks of a map from a game's host (map_quick_check.h). Every
offset and count is checked with arithmetic that cannot overflow, and the
tag data is read into memory of exactly its size.
*/

#include "map_quick_check.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	HEADER_TAG_DATA_OFFSET = 0x10,
	HEADER_TAG_DATA_SIZE_OFFSET = 0x14,

	/* the tag data's header (ce_map_checks.c's struct ce_tag_header) */
	TAG_HEADER_SIZE = 0x28,
	TAG_HEADER_INSTANCES = 0x00,
	TAG_HEADER_SCENARIO = 0x04,
	TAG_HEADER_COUNT = 0x0c,
	TAG_HEADER_MODEL_DATA_OFFSET = 0x14,
	TAG_HEADER_VERTEX_DATA_SIZE = 0x1c,
	TAG_HEADER_MODEL_DATA_SIZE = 0x20,
	TAG_HEADER_SIGNATURE = 0x24,
	TAG_SIGNATURE = 0x74616773, /* 'tags' */

	/* a tag instance (ce_map_checks.h's struct ce_tag_instance) */
	INSTANCE_SIZE = 0x20,
	INSTANCE_GROUP = 0x00,
	INSTANCE_HANDLE = 0x0c,
	INSTANCE_NAME = 0x10,
	INSTANCE_DATA = 0x14,
	INSTANCE_INDEXED = 0x18,

	MAXIMUM_TAG_COUNT = 0xffff,
	/* a tag's name, with its terminator */
	MAXIMUM_TAG_NAME_LENGTH = 256,

	GROUP_SCENARIO = 0x73636e72, /* 'scnr' */
	GROUP_BSP = 0x73627370, /* 'sbsp' */
	GROUP_BITMAP = 0x6269746d, /* 'bitm' */
	GROUP_SOUND = 0x736e6421, /* 'snd!' */
	GROUP_FONT = 0x666f6e74, /* 'font' */
	GROUP_STRINGS = 0x75737472, /* 'ustr' */
	GROUP_HUD_TEXT = 0x686d7420, /* 'hmt ' */
};

/* ---------- private code */

static unsigned int read_32(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | (unsigned int)bytes[1] << 8 | (unsigned int)bytes[2] << 16 |
		(unsigned int)bytes[3] << 24;
}

static int refuse(char *error, int error_size, const char *format, ...)
{
	va_list arguments;

	if (error && error_size > 0)
	{
		va_start(arguments, format);
		vsnprintf(error, (size_t)error_size, format, arguments);
		va_end(arguments);
	}
	return 0;
}

static int within(unsigned int offset, unsigned int size, unsigned int limit)
{
	return offset <= limit && size <= limit - offset;
}

/* an address in the tag data (loaded at MAP_QUICK_CHECK_TAG_CACHE_BASE,
size bytes) with size bytes after it there: its offset in it, else -1 */
static long tag_offset(unsigned int address, unsigned int bytes, unsigned int size)
{
	unsigned int offset;

	if (address < MAP_QUICK_CHECK_TAG_CACHE_BASE)
		return -1;
	offset = address - MAP_QUICK_CHECK_TAG_CACHE_BASE;
	return within(offset, bytes, size) ? (long)offset : -1;
}

/* the tag data (size bytes) checked: 1, else 0 with why in error */
static int check_tags(const unsigned char *tags, unsigned int size, unsigned int file_size, char *error,
	int error_size)
{
	unsigned int count, scenario, index;
	long instances;

	if (read_32(tags + TAG_HEADER_SIGNATURE) != TAG_SIGNATURE)
		return refuse(error, error_size, "its tag data has no tag header");
	count = read_32(tags + TAG_HEADER_COUNT);
	if (!count || count > MAXIMUM_TAG_COUNT)
		return refuse(error, error_size, "it says it has %u tags", count);
	instances = tag_offset(read_32(tags + TAG_HEADER_INSTANCES), count * INSTANCE_SIZE, size);
	if (instances < 0 || instances % 4)
		return refuse(error, error_size, "its list of tags is not in its tag data");
	for (index = 0; index < count; index++)
	{
		const unsigned char *instance = tags + instances + (unsigned long)index * INSTANCE_SIZE;
		unsigned int group = read_32(instance + INSTANCE_GROUP);
		long name = tag_offset(read_32(instance + INSTANCE_NAME), 1, size);
		unsigned int room;

		if ((read_32(instance + INSTANCE_HANDLE) & 0xffff) != index)
			return refuse(error, error_size, "tag %u has another tag's handle", index);
		if (name < 0)
			return refuse(error, error_size, "tag %u's name is not in its tag data", index);
		room = size - (unsigned int)name;
		if (!memchr(tags + name, 0, room < MAXIMUM_TAG_NAME_LENGTH ? room : MAXIMUM_TAG_NAME_LENGTH))
			return refuse(error, error_size, "tag %u's name does not end", index);
		if (read_32(instance + INSTANCE_INDEXED))
		{
			if (group != GROUP_BITMAP && group != GROUP_SOUND && group != GROUP_FONT && group != GROUP_STRINGS &&
				group != GROUP_HUD_TEXT)
			{
				return refuse(error, error_size, "tag %u is kept in a resource map, which keeps no tags of its kind",
					index);
			}
		}
		else if (group == GROUP_BSP)
		{
			if (read_32(instance + INSTANCE_DATA))
				return refuse(error, error_size, "structure BSP tag %u is placed in the tag data", index);
		}
		else if (tag_offset(read_32(instance + INSTANCE_DATA), 4, size) < 0)
			return refuse(error, error_size, "tag %u's data is not in its tag data", index);
	}
	scenario = read_32(tags + TAG_HEADER_SCENARIO);
	if ((scenario & 0xffff) >= count ||
		read_32(tags + instances + (unsigned long)(scenario & 0xffff) * INSTANCE_SIZE + INSTANCE_HANDLE) != scenario ||
		read_32(tags + instances + (unsigned long)(scenario & 0xffff) * INSTANCE_SIZE + INSTANCE_GROUP) !=
			GROUP_SCENARIO)
	{
		return refuse(error, error_size, "its scenario is not one of its tags");
	}
	if (read_32(tags + TAG_HEADER_VERTEX_DATA_SIZE) > read_32(tags + TAG_HEADER_MODEL_DATA_SIZE) ||
		!within(read_32(tags + TAG_HEADER_MODEL_DATA_OFFSET), read_32(tags + TAG_HEADER_MODEL_DATA_SIZE), file_size))
	{
		return refuse(error, error_size, "its model data is not in the file");
	}
	return 1;
}

/* ---------- public code */

int map_quick_check_name(const char *name)
{
	return name && map_archive_name_valid(name) && !map_archive_stock_name(name);
}

unsigned int map_quick_check_limit(long megabytes)
{
	if (megabytes <= 0)
		megabytes = MAP_QUICK_CHECK_DEFAULT_LIMIT_MB;
	if (megabytes < MAP_QUICK_CHECK_MINIMUM_LIMIT_MB)
		megabytes = MAP_QUICK_CHECK_MINIMUM_LIMIT_MB;
	if (megabytes > MAP_QUICK_CHECK_MAXIMUM_LIMIT_MB)
		megabytes = MAP_QUICK_CHECK_MAXIMUM_LIMIT_MB;
	return (unsigned int)megabytes << 20;
}

int map_quick_check(map_archive_read_proc read, void *context, unsigned int file_size, const char *name,
	unsigned int limit, char *error, int error_size)
{
	unsigned char header[MAP_ARCHIVE_HEADER_SIZE];
	unsigned char *tags;
	unsigned int tag_data_offset, tag_data_size;
	int ok;

	if (error && error_size > 0)
		error[0] = 0;
	if (!map_quick_check_name(name))
		return refuse(error, error_size, "its name is not a map file's name we keep");
	if (file_size > limit)
		return refuse(error, error_size, "it is %u MB, more than the %u MB allowed", file_size >> 20, limit >> 20);
	if (file_size < MAP_ARCHIVE_HEADER_SIZE || !read(context, 0, header, sizeof(header)))
		return refuse(error, error_size, "it is too small to be a map");
	if (!map_archive_check_header(header, file_size, name, 0, error, error_size))
		return 0;
	tag_data_offset = read_32(header + HEADER_TAG_DATA_OFFSET);
	tag_data_size = read_32(header + HEADER_TAG_DATA_SIZE_OFFSET);
	if (tag_data_size < TAG_HEADER_SIZE)
		return refuse(error, error_size, "its tag data is too small");
	if (tag_data_size > MAP_QUICK_CHECK_TAG_CACHE_SIZE)
		return refuse(error, error_size, "its tag data is larger than the game's tag cache");
	tags = (unsigned char *)malloc(tag_data_size);
	if (!tags)
		return refuse(error, error_size, "out of memory");
	ok = read(context, tag_data_offset, tags, tag_data_size);
	if (!ok)
		refuse(error, error_size, "its tag data could not be read");
	else
		ok = check_tags(tags, tag_data_size, file_size, error, error_size);
	free(tags);
	return ok;
}
