/*
MAP_REPORT.C

A map's identity and shape as JSON for the site's map catalog (see
map_report.h).
*/

#include <stdio.h>
#include <string.h>

#include "delta_wire.h"
#include "map_report.h"

/* the cache header (cache_files.c's struct cache_file_header) */
#define HEADER_VERSION 0x04
#define HEADER_BUILD 0x40
#define HEADER_CHECKSUM 0x64
#define HEADER_FOOTER 0x7FC

static const char *const family_names[] = { "xbox", "ce", "md", "pc" };
static const char *const gametype_names[] = { "slayer", "ctf", "oddball", "king", "race" };

static unsigned int read_u32(const unsigned char *data)
{
	return (unsigned int)data[0] | ((unsigned int)data[1] << 8) | ((unsigned int)data[2] << 16) |
		((unsigned int)data[3] << 24);
}

int map_report_read_header(const unsigned char *header, int size, struct map_report *report)
{
	unsigned int version;
	int index;

	if (!header || size < MAP_REPORT_HEADER_SIZE || memcmp(header, "daeh", 4) || memcmp(header + HEADER_FOOTER, "toof", 4))
		return 0;
	version = read_u32(header + HEADER_VERSION);
	report->cache_version = (int)(version & 0x7FFFFFFFU);
	report->header_checksum = read_u32(header + HEADER_CHECKSUM);
	for (index = 0; index < MAP_REPORT_BUILD_SIZE - 1 && header[HEADER_BUILD + index]; index++)
		report->build[index] = (char)header[HEADER_BUILD + index];
	report->build[index] = 0;
	return 1;
}

struct writer
{
	char *out;
	int size;
	int length;
	int full;
};

static void put(struct writer *writer, const char *text)
{
	int length = (int)strlen(text);

	if (writer->full || writer->length + length >= writer->size)
	{
		writer->full = 1;
		return;
	}
	memcpy(writer->out + writer->length, text, (size_t)length);
	writer->length += length;
	writer->out[writer->length] = 0;
}

/* a JSON string: quotes, backslashes and control characters escaped,
bytes past ASCII as \u00XX (the header's and tag paths' text is Latin-1) */
static void put_string(struct writer *writer, const char *text)
{
	char one[8];

	put(writer, "\"");
	for (; *text; text++)
	{
		unsigned char c = (unsigned char)*text;

		if (c == '"' || c == '\\')
		{
			one[0] = '\\';
			one[1] = (char)c;
			one[2] = 0;
		}
		else if (c < 0x20 || c >= 0x7F)
			sprintf(one, "\\u%04x", (unsigned int)c);
		else
		{
			one[0] = (char)c;
			one[1] = 0;
		}
		put(writer, one);
	}
	put(writer, "\"");
}

static void put_float(struct writer *writer, float value)
{
	char number[48];

	/* (a bound past what a map can have is not a number JSON takes) */
	if (!(value > -1.0e7f && value < 1.0e7f))
		value = 0.0f;
	sprintf(number, "%.3f", (double)value);
	put(writer, number);
}

int map_report_json(const struct map_report *report, char *out, int size)
{
	struct writer writer;
	char number[64];
	int index;
	int first;

	if (!report || !out || size <= 0)
		return 0;
	out[0] = 0;
	if (report->family < 0 || report->family >= (int)(sizeof(family_names) / sizeof(family_names[0])) ||
		!delta_wire_map_name_valid(report->file) || (report->family != 0 && !report->has_hash))
		return 0;
	writer.out = out;
	writer.size = size;
	writer.length = 0;
	writer.full = 0;

	put(&writer, "{\"family\":");
	put_string(&writer, family_names[report->family]);
	put(&writer, ",\"file\":");
	put_string(&writer, report->file);
	/* (the size as one number: below 2^53 it is exact in any JSON reader;
	none for a map whose identity is its name alone, an Xbox map's, whose
	file is compressed) */
	if (report->size_low || report->size_high)
	{
		sprintf(number, ",\"size\":%.0f", (double)report->size_high * 4294967296.0 + (double)report->size_low);
		put(&writer, number);
	}
	if (report->has_hash)
	{
		put(&writer, ",\"blake2b\":\"");
		for (index = 0; index < MAP_REPORT_HASH_SIZE; index++)
		{
			sprintf(number, "%02x", (unsigned int)report->hash[index]);
			put(&writer, number);
		}
		put(&writer, "\"");
	}
	sprintf(number, ",\"cache_version\":%d,\"header_checksum\":\"%08x\"", report->cache_version,
		report->header_checksum);
	put(&writer, number);
	put(&writer, ",\"build\":");
	put_string(&writer, report->build);
	put(&writer, ",\"scenario\":");
	put_string(&writer, report->scenario);
	put(&writer, ",\"gametypes\":[");
	for (index = 0, first = 1; index < (int)(sizeof(gametype_names) / sizeof(gametype_names[0])); index++)
	{
		if (!(report->gametypes & (1U << index)))
			continue;
		if (!first)
			put(&writer, ",");
		put_string(&writer, gametype_names[index]);
		first = 0;
	}
	sprintf(number, "],\"spawns\":%d", report->spawns < 0 ? 0 : report->spawns);
	put(&writer, number);
	if (report->has_bounds)
	{
		static const char *const axes[] = { "x", "y", "z" };

		put(&writer, ",\"bounds\":{");
		for (index = 0; index < 3; index++)
		{
			if (index)
				put(&writer, ",");
			put_string(&writer, axes[index]);
			put(&writer, ":[");
			put_float(&writer, report->bounds[index * 2]);
			put(&writer, ",");
			put_float(&writer, report->bounds[index * 2 + 1]);
			put(&writer, "]");
		}
		put(&writer, "}");
	}
	put(&writer, "}");
	if (writer.full)
	{
		out[0] = 0;
		return 0;
	}
	return writer.length;
}
