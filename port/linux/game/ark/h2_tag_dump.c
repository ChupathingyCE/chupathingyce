/* h2_tag_dump.c: a test-only dump of what this port reads from Halo 2's
tags, for verification against the H2EK's exported tag XML.

HALO_H2_TAG_DUMP=<dir>: when a Halo 2 map is opened, every tag of the
groups in h2_tag_dump_table.inc is read with the offsets THIS PORT'S readers
use (the table is transcribed from them: h2_ai.c, h2_biped.c, h2_vehicles.c,
h2_weapons.c, h2_projectiles.c, h2_damage.c, h2_models_load.inc,
h2_cinematics.c, h2_level.c; each row cites its reader) and written to
<dir>/<map>/<group>/<tag path>.tsv, one row a field:

	path	offset	kind	raw	value

path is the H2EK block path with element indexes (squads[3]/starting
locations[1]/facing), offset the element-relative offset the port reads,
raw the bytes as an integer (hex) or the float as printed, value the port's
interpretation (angles in degrees as the XML prints them, string ids as
text, tag references as group:path). A block row's value is
"count=N elem=0xNN" and is followed by every element (not only the first),
HALO_H2_TAG_DUMP_MAX elements at most (default 400; "truncated" row past
it). A local checker script matches each row
with the XML field of the same name and block path. Compiled out of
release builds, as h2_run.c is. */

/* (plain stdio: the dump goes to a host folder, not an Xbox path - no
halo_linux_fopen translation, port/linux/include/stdio.h) */
#define HALO_LINUX_PLATFORM_LAYER 1

#include "cseries.h"
#include "h2_cache.h"
#include "h2_tag_dump.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);

#if !defined(HALO_RELEASE)

enum h2_dump_kind
{
	_k_s8, _k_u8, _k_s16, _k_u16, _k_s32, _k_u32, _k_f32, _k_angle, _k_sid, _k_ref, _k_block,
	_k_p3, _k_v3, _k_bounds, _k_abounds, _k_rgbf, _k_argbf, _k_str32, _k_bidx, _k_flags8, _k_flags16,
	_k_flags32, _k_enum8, _k_enum16, _k_plane, _k_quat, _k_data, _k_euler2, _k_euler3, _k_p2, _k_v2,
	_k_sbounds, _k_fraction,
	NUMBER_OF_DUMP_KINDS
};

static char const *const h2_dump_kind_names[NUMBER_OF_DUMP_KINDS] =
{
	"s8", "u8", "s16", "u16", "s32", "u32", "f32", "angle", "sid", "ref", "block",
	"p3", "v3", "bounds", "abounds", "rgbf", "argbf", "str32", "bidx", "flags8", "flags16",
	"flags32", "enum8", "enum16", "plane", "quat", "data", "euler2", "euler3", "p2", "v2",
	"sbounds", "fraction"
};

struct h2_dump_field
{
	char const *name;
	unsigned short offset;
	unsigned char kind;
	short block; /* _k_block: the block's index in h2_dump_blocks; else -1 */
};

struct h2_dump_block
{
	char const *name;
	unsigned short element_size;
	unsigned short first_field;
	unsigned short field_count;
};

struct h2_dump_group
{
	unsigned long group_tag;
	char const *name;
	unsigned short first_field;
	unsigned short field_count;
};

/* (the generated table: h2_dump_fields[], h2_dump_blocks[], h2_dump_groups[];
tools/h2/tagdump_table.py makes it from tools/h2/tagdump_spec.tsv) */
#include "h2_tag_dump_table.inc"

/* (caches open on several threads at once: one dump at a time) */
static pthread_mutex_t dump_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *dump_file;
static long dump_max_elements = 400;
static long dump_rows;
/* (HALO_H2_TAG_DUMP_TRACE=1: every row flushed, to see where a bad table row
makes the dump fall over) */
static boolean dump_trace;

static unsigned long rd32(
	unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned short rd16(
	unsigned char const *p)
{
	return (unsigned short)(p[0] | (p[1] << 8));
}

static float rdf(
	unsigned char const *p)
{
	unsigned long u = rd32(p);
	float f;

	memcpy(&f, &u, sizeof(f));
	return f;
}

static long kind_size(
	int kind)
{
	switch (kind)
	{
	case _k_s8: case _k_u8: case _k_flags8: case _k_enum8: return 1;
	case _k_s16: case _k_u16: case _k_bidx: case _k_flags16: case _k_enum16: return 2;
	case _k_s32: case _k_u32: case _k_f32: case _k_angle: case _k_sid: case _k_flags32: case _k_fraction: case _k_sbounds: return 4;
	case _k_ref: case _k_block: case _k_bounds: case _k_abounds: case _k_data: case _k_euler2: case _k_p2: case _k_v2: return 8;
	case _k_p3: case _k_v3: case _k_rgbf: case _k_euler3: return 12;
	case _k_argbf: case _k_plane: case _k_quat: return 16;
	case _k_str32: return 32;
	}
	return 4;
}

static void write_escaped(
	char const *s)
{
	for (; *s; s++)
	{
		if (*s == '\t' || *s == '\n' || *s == '\r')
			fputc(' ', dump_file);
		else
			fputc(*s, dump_file);
	}
}

static void write_floats(
	unsigned char const *p,
	int n,
	float scale)
{
	int i;

	for (i = 0; i < n; i++)
		fprintf(dump_file, "%s%.6g", i ? "," : "", rdf(p + 4 * i) * scale);
}

static void write_field(
	struct h2_cache *cache,
	unsigned long address,
	struct h2_dump_field const *f,
	char const *path)
{
	unsigned char const *p = h2_cache_at(cache, address + f->offset, kind_size(f->kind));
	char text[256];

	fprintf(dump_file, "%s%s\t0x%X\t%s\t", path, f->name, (unsigned)f->offset, h2_dump_kind_names[f->kind]);
	dump_rows++;
	if (!p)
	{
		fprintf(dump_file, "-\tunreadable\n");
		return;
	}
	switch (f->kind)
	{
	case _k_s8: fprintf(dump_file, "0x%02X\t%d", p[0], (int)(signed char)p[0]); break;
	case _k_u8: case _k_flags8: case _k_enum8: fprintf(dump_file, "0x%02X\t%u", p[0], p[0]); break;
	case _k_s16: case _k_bidx: fprintf(dump_file, "0x%04X\t%d", rd16(p), (int)(short)rd16(p)); break;
	case _k_u16: case _k_flags16: case _k_enum16: fprintf(dump_file, "0x%04X\t%u", rd16(p), rd16(p)); break;
	case _k_s32: fprintf(dump_file, "0x%08lX\t%ld", rd32(p), (long)(int)rd32(p)); break;
	case _k_u32: case _k_flags32: fprintf(dump_file, "0x%08lX\t%lu", rd32(p), rd32(p)); break;
	case _k_f32: case _k_fraction: fprintf(dump_file, "0x%08lX\t%.6g", rd32(p), rdf(p)); break;
	case _k_angle: fprintf(dump_file, "0x%08lX\t%.6g", rd32(p), rdf(p) * (180.0 / 3.14159265358979)); break;
	case _k_sid:
		fprintf(dump_file, "0x%08lX\t", rd32(p));
		if (h2_cache_string(cache, rd32(p), text, sizeof(text)))
			write_escaped(text);
		else
			fprintf(dump_file, "%s", rd32(p) ? "?" : "");
		break;
	case _k_ref:
	{
		long tag = h2_cache_reference(cache, address + f->offset);
		unsigned long g = rd32(p);

		fprintf(dump_file, "0x%08lX\t", rd32(p + 4));
		if (tag != NONE)
		{
			fprintf(dump_file, "%c%c%c%c:", (int)(g >> 24) & 255, (int)(g >> 16) & 255, (int)(g >> 8) & 255, (int)g & 255);
			write_escaped(h2_cache_tag_name(cache, tag));
		}
		break;
	}
	case _k_data: fprintf(dump_file, "0x%08lX\tsize=%lu", rd32(p + 4), rd32(p)); break;
	case _k_bounds: case _k_p2: case _k_v2: case _k_euler2: fprintf(dump_file, "-\t"); write_floats(p, 2, 1.f); break;
	case _k_abounds: fprintf(dump_file, "-\t"); write_floats(p, 2, (float)(180.0 / 3.14159265358979)); break;
	case _k_euler3: fprintf(dump_file, "-\t"); write_floats(p, 3, (float)(180.0 / 3.14159265358979)); break;
	case _k_p3: case _k_v3: case _k_rgbf: fprintf(dump_file, "-\t"); write_floats(p, 3, 1.f); break;
	case _k_argbf: case _k_plane: case _k_quat: fprintf(dump_file, "-\t"); write_floats(p, 4, 1.f); break;
	case _k_sbounds: fprintf(dump_file, "0x%08lX\t%d,%d", rd32(p), (int)(short)rd16(p), (int)(short)rd16(p + 2)); break;
	case _k_str32:
		memcpy(text, p, 32);
		text[32] = 0;
		fprintf(dump_file, "-\t");
		write_escaped(text);
		break;
	default: fprintf(dump_file, "0x%08lX\t?", rd32(p)); break;
	}
	fputc('\n', dump_file);
	if (dump_trace)
		fflush(dump_file);
}

static void dump_fields(
	struct h2_cache *cache,
	unsigned long address,
	struct h2_dump_field const *fields,
	int field_count,
	char const *path,
	int depth)
{
	int i;

	for (i = 0; i < field_count; i++)
	{
		struct h2_dump_field const *f = &fields[i];

		if (f->kind != _k_block)
		{
			write_field(cache, address, f, path);
			continue;
		}
		{
			struct h2_dump_block const *b = &h2_dump_blocks[f->block];
			long count = 0, n;
			unsigned long elements = 0;
			boolean ok = h2_cache_block(cache, address + f->offset, b->element_size, &count, &elements);

			fprintf(dump_file, "%s%s\t0x%X\tblock\t0x%08lX\tcount=%ld elem=0x%X%s\n", path, f->name, (unsigned)f->offset,
				h2_cache_read32(cache, address + f->offset + 4), ok ? count : -1L, (unsigned)b->element_size, ok ? "" : " unreadable");
			dump_rows++;
			if (dump_trace)
				fflush(dump_file);
			if (!ok || depth > 6)
				continue;
			for (n = 0; n < count; n++)
			{
				char sub[512];

				/* (geometry under the third level - collision bsps' planes,
				edges... - cut to a few elements, as the kit's bsp export is) */
				if (n >= (depth >= 3 ? 8 : depth == 2 ? 64 : dump_max_elements))
				{
					fprintf(dump_file, "%s%s\t0x%X\ttruncated\t-\t%ld more\n", path, f->name, (unsigned)f->offset, count - n);
					break;
				}
				snprintf(sub, sizeof(sub), "%s%s[%ld]/", path, f->name, n);
				dump_fields(cache, elements + (unsigned long)n * b->element_size, &h2_dump_fields[b->first_field], b->field_count, sub, depth + 1);
			}
		}
	}
}

static void sanitize_path(
	char const *name,
	char *out,
	long out_size)
{
	long i;

	for (i = 0; i < out_size - 1 && name[i]; i++)
	{
		char c = name[i];

		out[i] = (c == '\\' || c == '/' || c == ':' || c == ' ' || c == '\'' || c == '"' || c == '!') ? '_' : c;
	}
	out[i] = 0;
}

static void mkdir_p(
	char const *path)
{
	char copy[1024];
	char *s;

	snprintf(copy, sizeof(copy), "%s", path);
	for (s = copy + 1; *s; s++)
	{
		if (*s == '/')
		{
			*s = 0;
			mkdir(copy, 0775);
			*s = '/';
		}
	}
	mkdir(copy, 0775);
}

void h2_tag_dump_cache(
	struct h2_cache *cache)
{
	static char dumped[16][H2_CACHE_NAME_SIZE];
	static int dumped_count;
	char const *dir = getenv("HALO_H2_TAG_DUMP"), *max = getenv("HALO_H2_TAG_DUMP_MAX");
	char const *map;
	char folder[1024], file[1536], index_path[1100], tag_name[H2_CACHE_NAME_SIZE];
	FILE *index;
	long i, count, tags = 0, rows = 0;
	unsigned long started = system_milliseconds();
	int g;

	if (!dir || !*dir || !cache)
		return;
	map = h2_cache_name(cache);
	/* (the shared maps' tags are in every map's index: dumped with it) */
	if (!strcasecmp(map, "shared") || !strcasecmp(map, "single_player_shared"))
		return;
	pthread_mutex_lock(&dump_lock);
	for (i = 0; i < dumped_count; i++)
	{
		if (!strcasecmp(dumped[i], map))
		{
			pthread_mutex_unlock(&dump_lock);
			return;
		}
	}
	if (dumped_count < 16)
		snprintf(dumped[dumped_count++], H2_CACHE_NAME_SIZE, "%s", map);
	if (max && atol(max) > 0)
		dump_max_elements = atol(max);
	dump_trace = getenv("HALO_H2_TAG_DUMP_TRACE") != NULL;
	snprintf(folder, sizeof(folder), "%s/%s", dir, map);
	mkdir_p(folder);
	snprintf(index_path, sizeof(index_path), "%s/index.tsv", folder);
	index = fopen(index_path, "w");
	if (index)
		fprintf(index, "group\ttag\tfile\taddress\tsize\trows\n");
	count = h2_cache_tag_count(cache);
	{
		struct h2_cache_tag t0;
		unsigned long g0 = 0;

		if (count > 0 && h2_cache_tag_get(cache, 0, &t0))
			g0 = t0.group_tag;
		platform_log("h2 tag dump: %s: %ld tags in the index (tag 0: group %08lX '%c%c%c%c', address %08lX, %s)", map, count, g0,
			(int)(g0 >> 24) & 255, (int)(g0 >> 16) & 255, (int)(g0 >> 8) & 255, (int)g0 & 255, count > 0 ? t0.address : 0UL,
			count > 0 ? h2_cache_tag_name(cache, 0) : "");
	}
	for (g = 0; g < (int)(sizeof(h2_dump_groups) / sizeof(h2_dump_groups[0])); g++)
	{
		struct h2_dump_group const *group = &h2_dump_groups[g];
		char group_folder[1100];

		snprintf(group_folder, sizeof(group_folder), "%s/%s", folder, group->name);
		mkdir_p(group_folder);
		for (i = 0; i < count; i++)
		{
			struct h2_cache_tag t;
			char const *name;

			if (!h2_cache_tag_get(cache, i, &t) || t.group_tag != group->group_tag || !t.address)
				continue;
			name = h2_cache_tag_name(cache, i);
			if (!name || !*name)
				continue;
			sanitize_path(name, tag_name, sizeof(tag_name));
			snprintf(file, sizeof(file), "%s/%s.tsv", group_folder, tag_name);
			dump_file = fopen(file, "w");
			if (!dump_file)
				continue;
			dump_rows = 0;
			fprintf(dump_file, "# tag\t%s\tgroup\t%s\taddress\t0x%08lX\tsize\t%lu\tmap\t%s\n", name, group->name, t.address, t.size, map);
			fprintf(dump_file, "path\toffset\tkind\traw\tvalue\n");
			dump_fields(cache, t.address, &h2_dump_fields[group->first_field], group->field_count, "", 0);
			fclose(dump_file);
			dump_file = NULL;
			if (index)
			{
				fprintf(index, "%s\t", group->name);
				fputs(name, index);
				fprintf(index, "\t%s/%s.tsv\t0x%08lX\t%lu\t%ld\n", group->name, tag_name, t.address, t.size, dump_rows);
			}
			tags++;
			rows += dump_rows;
		}
	}
	if (index)
		fclose(index);
	platform_log("h2 tag dump: %s: %ld tags, %ld rows to %s in %lu ms", map, tags, rows, folder, system_milliseconds() - started);
	pthread_mutex_unlock(&dump_lock);
}

#else

void h2_tag_dump_cache(
	struct h2_cache *cache)
{
	(void)cache;
}

#endif
