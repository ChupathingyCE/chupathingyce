/*
ARK_LEFT_OUT.C

The parts of Project Ark this tree leaves out (they stay in project-ark-dev):
its other content units, and its own copy of a Custom Edition map loader,
which this game has its own of. What the mode's other units call of them is
here: the left-out content is never found (every open fails, every question
answers no), and the two small cache file helpers the kit and the sound
code use are written here.
*/

#include "cseries.h"
#include "cache_file_formats.h"
#include "h3_animation.h"
#include "h3_cache.h"
#include "h3_campaign.h"
#include "h3_level.h"
#include "h3_spartan.h"

#include <stdint.h>
#include <stdio.h>

/* ---------- the cache file helpers */

/* CRC-32 as the cache checksum takes it: the reflected polynomial, no
inversions */
uint32_t cache_file_crc32_update(
	uint32_t crc,
	void const *bytes,
	uint32_t size)
{
	uint8_t const *at = bytes;
	uint32_t index;

	for (index = 0; index < size; index++)
	{
		int bit;

		crc ^= at[index];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
	}
	return crc;
}

static uint32_t read_le32(
	uint8_t const *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

/* a map's kind, from its header's first 0x800 bytes: the signatures at its
start and end, then its version */
enum cache_file_format cache_file_header_format(
	void const *header,
	int *has_opensauce_header)
{
	uint8_t const *bytes = header;

	*has_opensauce_header = 0;
	if (read_le32(bytes) != 0x68656164UL /* 'head' */ || read_le32(bytes + 0x7FC) != 0x666F6F74UL /* 'foot' */)
		return _cache_file_format_unrecognized;
	switch ((int32_t)read_le32(bytes + 4))
	{
	case CACHE_FILE_VERSION_XBOX:
		return _cache_file_format_xbox_cache;
	case CACHE_FILE_VERSION_CUSTOM_EDITION:
		*has_opensauce_header = read_le32(bytes + 0x70) == 0x79656C6FUL /* 'yelo' */;
		return _cache_file_format_custom_edition_cache;
	case CACHE_FILE_VERSION_MCC_CEA:
		return _cache_file_format_mcc_cea_cache;
	default:
		return _cache_file_format_other_cache;
	}
}

/* (the left-out loader converted MCC's sounds as it loaded a map: nothing to
point at) */
void custom_edition_cache_convert_mcc_sounds(
	struct mcc_sound_source const *source)
{
	(void)source;
}

/* ---------- the left-out content: never found */

struct h2_graph *h3_animation_graph_make(
	char const *biped_name,
	char const (*node_names)[32],
	struct h2_animation_pose const *node_defaults,
	short const *node_parents,
	short node_count,
	char *why,
	long why_size)
{
	(void)biped_name;
	(void)node_names;
	(void)node_defaults;
	(void)node_parents;
	(void)node_count;
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "not in this build");
	return NULL;
}

void h3_animation_source_close(
	struct h3_animation_source *opened)
{
	(void)opened;
}

struct h3_animation_source *h3_animation_source_open(
	struct h3_cache *cache,
	long jmad_tag,
	struct h2_graph_source *source,
	char *why,
	long why_size)
{
	(void)cache;
	(void)jmad_tag;
	(void)source;
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "not in this build");
	return NULL;
}

boolean h3_animation_wanted(
	char const *biped_name)
{
	(void)biped_name;
	return FALSE;
}

void const *h3_cache_at(
	struct h3_cache *cache,
	unsigned long address,
	unsigned long size)
{
	(void)cache;
	(void)address;
	(void)size;
	return NULL;
}

boolean h3_cache_block(
	struct h3_cache *cache,
	unsigned long address,
	unsigned long element_size,
	long *count,
	unsigned long *elements)
{
	(void)cache;
	(void)address;
	(void)element_size;
	if (count)
		*count = 0;
	if (elements)
		*elements = 0;
	return FALSE;
}

void h3_cache_close(
	struct h3_cache *cache)
{
	(void)cache;
}

long h3_cache_find_tag(
	struct h3_cache const *cache,
	unsigned long group_tag,
	char const *name)
{
	(void)cache;
	(void)group_tag;
	(void)name;
	return -1;
}

char const *h3_cache_name(
	struct h3_cache const *cache)
{
	(void)cache;
	return "";
}

struct h3_cache *h3_cache_open(
	char const *maps_directory,
	char const *name,
	char *why,
	long why_size)
{
	(void)maps_directory;
	(void)name;
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "not in this build");
	return NULL;
}

boolean h3_cache_read(
	struct h3_cache *cache,
	unsigned long address,
	unsigned long size,
	void *out)
{
	(void)cache;
	(void)address;
	(void)size;
	(void)out;
	return FALSE;
}

short h3_cache_read16(
	struct h3_cache *cache,
	unsigned long address)
{
	(void)cache;
	(void)address;
	return 0;
}

unsigned long h3_cache_read32(
	struct h3_cache *cache,
	unsigned long address)
{
	(void)cache;
	(void)address;
	return 0;
}

float h3_cache_readf(
	struct h3_cache *cache,
	unsigned long address)
{
	(void)cache;
	(void)address;
	return 0.0f;
}

long h3_cache_reference(
	struct h3_cache *cache,
	unsigned long address)
{
	(void)cache;
	(void)address;
	return -1;
}

char const *h3_cache_string(
	struct h3_cache const *cache,
	unsigned long string_id)
{
	(void)cache;
	(void)string_id;
	return "";
}

long h3_cache_tag_count(
	struct h3_cache const *cache)
{
	(void)cache;
	return 0;
}

boolean h3_cache_tag_get(
	struct h3_cache const *cache,
	long index,
	struct h3_cache_tag *tag)
{
	(void)cache;
	(void)index;
	(void)tag;
	return FALSE;
}

char const *h3_cache_tag_name(
	struct h3_cache const *cache,
	long index)
{
	(void)cache;
	(void)index;
	return "";
}

void h3_campaign_clear(void)
{
}

void h3_campaign_gather(
	struct h3_cache *cache,
	unsigned long scenario)
{
	(void)cache;
	(void)scenario;
}

void h3_campaign_squad_encounter(
	long h3_squad,
	short encounter)
{
	(void)h3_squad;
	(void)encounter;
}

boolean h3_level_direct_enabled(void)
{
	return FALSE;
}

boolean h3_level_direct_in_place_range(unsigned char **first, unsigned char **end)
{
	if (first)
		*first = NULL;
	if (end)
		*end = NULL;
	return FALSE;
}

boolean h3_level_direct_prepare(char const *maps_directory, char const *name, char *path, long path_size, char *why,
	long why_size)
{
	(void)maps_directory;
	(void)name;
	(void)path;
	(void)path_size;
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "not in this build");
	return FALSE;
}

struct h3_spartan *h3_model_read(
	struct h3_cache *cache,
	long mode_tag,
	char const *look,
	struct h3_cache_read_control const *control,
	char *why,
	long why_size)
{
	(void)cache;
	(void)mode_tag;
	(void)look;
	(void)control;
	if (why && why_size > 0)
		snprintf(why, (size_t)why_size, "not in this build");
	return NULL;
}

void h3_spartan_free(
	struct h3_spartan *spartan)
{
	(void)spartan;
}

boolean h3_spartan_maps_directory(
	char *out,
	unsigned long out_size)
{
	if (out && out_size)
		out[0] = 0;
	return FALSE;
}
