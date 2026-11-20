/*
H2_TEXTURE_SIDECAR.C

The PC quality tier's full-size textures of a Halo 2 level: the sidecar's
use (h2_texture_sidecar.h); h2_level.c makes it.
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "bitmaps/bitmap_group.h"
#include "cache/physical_memory_map.h"

#include "h2_level.h"
#include "h2_texture_sidecar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

void platform_log(const char *format, ...);
/* (xbox_kernel.c's on Linux and the Vita, the guest's under
halo_windows_api_names.h's name on Windows: stdcall everywhere, as
xdk_pdb.h has it. Declared cdecl, the i686 build's LTO took every call for
undefined behaviour and deleted the read waits around it: the base and
detail reads returned at once, and the cache file thread wrote into the
freed buffer and the dead flag later - heap corruption, Nov 3 2026) */
int __stdcall SwitchToThread(void);

/* ---------- constants */

enum
{
	/* the most tags a map has (the tag table's absolute indices) */
	MAXIMUM_TAGS = 65536,
	/* the flag the texture cache keeps for itself (xbox_texture_cache.c) */
	BITMAP_CACHED_FLAG = 0x80
};

/* ---------- structures */

struct sidecar_entry
{
	char name[H2_TEXTURE_SIDECAR_NAME_SIZE];
	unsigned short width, height, mipmaps, format, flags, kind;
	unsigned long offset, size;
	float bump_scale;
	unsigned char file;         /* which of the files (H2_TEXTURE_SIDECAR_FILE_*) */
	unsigned char given;        /* (the effects' pixel cache's) its tag's record matched: served */
};

/* ---------- globals */

static struct
{
	/* (the sidecar, and the effects' pixel cache: H2_TEXTURE_SIDECAR_FILE_*) */
	FILE *files[H2_TEXTURE_SIDECAR_FILES];
	volatile int lock;
	struct sidecar_entry *entries;
	long entry_count;
	/* by a tag's absolute index: its entry + 1 (0: not the sidecar's) */
	unsigned short *entry_of_tag;
	unsigned long bytes_given;
	long given;
} h2_texture_sidecar;

/* ---------- private code */

static unsigned long read_u32(unsigned char const *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned short read_u16(unsigned char const *p)
{
	return (unsigned short)(p[0] | (p[1] << 8));
}

static void lock(void)
{
	while (__atomic_exchange_n(&h2_texture_sidecar.lock, 1, __ATOMIC_ACQUIRE))
		SwitchToThread();
}

static void unlock(void)
{
	__atomic_store_n(&h2_texture_sidecar.lock, 0, __ATOMIC_RELEASE);
}

/* the level file's size and last 4 bytes; FALSE if it cannot be read */
static int level_identity(char const *level_path, unsigned long *size, unsigned long *tail)
{
	FILE *file = fopen(level_path, "rb");
	unsigned char bytes[4];
	long end;

	if (!file)
		return FALSE;
	if (fseek(file, 0, SEEK_END) != 0 || (end = ftell(file)) < 4 || fseek(file, end - 4, SEEK_SET) != 0 ||
		fread(bytes, 1, 4, file) != 4)
	{
		fclose(file);
		return FALSE;
	}
	fclose(file);
	*size = (unsigned long)end;
	*tail = read_u32(bytes);
	return TRUE;
}

/* one of the files' (which) header read and checked against its level's file */
static FILE *sidecar_open(char const *level_path, int which, unsigned char *header)
{
	char path[512];
	FILE *file;
	unsigned long size, tail;

	if (!level_identity(level_path, &size, &tail))
		return NULL;
	if (which == H2_TEXTURE_SIDECAR_FILE_EFFECTS)
		h2_texture_sidecar_fx_path(level_path, path, sizeof(path));
	else
		h2_texture_sidecar_path(level_path, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return NULL;
	if (fread(header, 1, H2_TEXTURE_SIDECAR_HEADER_SIZE, file) != H2_TEXTURE_SIDECAR_HEADER_SIZE ||
		read_u32(header) != H2_TEXTURE_SIDECAR_SIGNATURE || read_u32(header + 4) != H2_TEXTURE_SIDECAR_VERSION ||
		read_u32(header + 16) != size || read_u32(header + 20) != tail)
	{
		fclose(file);
		return NULL;
	}
	return file;
}

/* ---------- public code */

int h2_texture_sidecar_enabled(void)
{
#ifdef HALO_PC_TEXTURE_TIER
	char const *setting = getenv("HALO_H2_SIDECAR");

	return !setting || atoi(setting) != 0;
#else
	return FALSE;
#endif
}

void h2_texture_sidecar_path(char const *level_path, char *path, unsigned long path_size)
{
	size_t length = strlen(level_path);

	if (length > 4 && !strcmp(level_path + length - 4, ".map"))
		snprintf(path, path_size, "%.*s.texcache", (int)(length - 4), level_path);
	else
		snprintf(path, path_size, "%s.texcache", level_path);
}

void h2_texture_sidecar_fx_path(char const *level_path, char *path, unsigned long path_size)
{
	size_t length = strlen(level_path);

	if (length > 4 && !strcmp(level_path + length - 4, ".map"))
		snprintf(path, path_size, "%.*s.fxcache", (int)(length - 4), level_path);
	else
		snprintf(path, path_size, "%s.fxcache", level_path);
}

int h2_texture_sidecar_matches(char const *level_path)
{
	unsigned char header[H2_TEXTURE_SIDECAR_HEADER_SIZE];
	FILE *file = sidecar_open(level_path, H2_TEXTURE_SIDECAR_FILE_SIDECAR, header);

	if (!file)
		return FALSE;
	fclose(file);
	return TRUE;
}

/* one of the files (which) beside the level opened, its CRC-32 checked
and its raw entries read (count of them, malloc'd): NULL none or not usable */
static FILE *sidecar_file_read(char const *map_name, char const *level_path, int which, unsigned char **raw_out,
	unsigned long *count_out)
{
	unsigned char header[H2_TEXTURE_SIDECAR_HEADER_SIZE];
	unsigned char *raw = NULL, *buffer = NULL;
	unsigned long count, entries_offset, crc_wanted, crc, end, at;
	char const *what = which == H2_TEXTURE_SIDECAR_FILE_EFFECTS ? "effects' pixel cache" : "sidecar";
	FILE *file;

	*raw_out = NULL;
	*count_out = 0;
	file = sidecar_open(level_path, which, header);
	if (!file)
		return NULL;
	count = read_u32(header + 8);
	entries_offset = read_u32(header + 12);
	crc_wanted = read_u32(header + 24);
	/* (every byte after the header: its CRC-32) */
	fseek(file, 0, SEEK_END);
	end = (unsigned long)ftell(file);
	if (count == 0)
	{
		fclose(file);
		return NULL;
	}
	if (count > 16384 || entries_offset < H2_TEXTURE_SIDECAR_HEADER_SIZE ||
		entries_offset + count * H2_TEXTURE_SIDECAR_ENTRY_SIZE > end)
	{
		platform_log("h2 textures: %s's %s is damaged (its entries); not used", map_name, what);
		fclose(file);
		return NULL;
	}
	buffer = malloc(1 << 20);
	crc = crc32(0L, Z_NULL, 0);
	fseek(file, H2_TEXTURE_SIDECAR_HEADER_SIZE, SEEK_SET);
	for (at = H2_TEXTURE_SIDECAR_HEADER_SIZE; buffer && at < end;)
	{
		size_t n = fread(buffer, 1, end - at < (1UL << 20) ? end - at : (1UL << 20), file);

		if (!n)
			break;
		crc = crc32(crc, buffer, (uInt)n);
		at += n;
	}
	if (buffer)
		free(buffer);
	if (at != end || crc != crc_wanted)
	{
		platform_log("h2 textures: %s's %s is damaged (CRC-32 %08lx, its header %08lx); not used", map_name, what, crc, crc_wanted);
		fclose(file);
		return NULL;
	}
	raw = malloc(count * H2_TEXTURE_SIDECAR_ENTRY_SIZE);
	if (!raw || fseek(file, (long)entries_offset, SEEK_SET) != 0 || fread(raw, H2_TEXTURE_SIDECAR_ENTRY_SIZE, count, file) != count)
	{
		if (raw)
			free(raw);
		fclose(file);
		return NULL;
	}
	*raw_out = raw;
	*count_out = count;
	return file;
}

/* (the effects' pixel cache) the bitmap tag an entry is for: of the tags of
its name - a level can hold one sheet image twice, two tags of one name
(Lockout's h2\bitmaps\tracer_contrail) - the one whose record's pixels
offset is the entry's; else the first of the name (refused by the checks) */
static long fx_entry_tag(struct sidecar_entry const *entry)
{
	struct tag_iterator iterator;
	long tag_index, first = NONE;

	tag_iterator_new(&iterator, BITMAP_GROUP_TAG);
	for (tag_index = tag_iterator_next(&iterator); tag_index != NONE; tag_index = tag_iterator_next(&iterator))
	{
		struct bitmap_group *group;
		char const *name = tag_get_name(tag_index);

		if (!name || strcmp(name, entry->name))
			continue;
		if (first == NONE)
			first = tag_index;
		group = bitmap_group_get(tag_index);
		if (group && group->bitmaps.count >= 1 &&
			(unsigned long)TAG_BLOCK_GET_ELEMENT(&group->bitmaps, 0, struct bitmap_data)->pixels_offset == entry->offset)
		{
			return tag_index;
		}
	}
	return first;
}

void h2_texture_sidecar_map_loaded(char const *map_name)
{
	char level_path[512];
	unsigned char *raw[H2_TEXTURE_SIDECAR_FILES];
	FILE *files[H2_TEXTURE_SIDECAR_FILES];
	unsigned long counts[H2_TEXTURE_SIDECAR_FILES], count = 0, fx_count = 0;
	long i, which, missing = 0, kept = 0, bumps = 0, fx_given = 0, at;
	/* (a direct level - no file, HALO_H2_DIRECT: its sidecar's entries in
	memory, their pixels the texture source's records - h2_level.h) */
	int direct = FALSE;

	h2_texture_sidecar_unloaded();
	if (!map_name)
		return;
	{
		char file[96];

		snprintf(file, sizeof(file), "%s.map", map_name);
		h2_level_folder_file(file, level_path, sizeof(level_path));
	}
	/* the files: the PC tier's sidecar (where it is used), and the
	effects' pixel cache (every platform: h2_effects.c makes it) */
	for (which = 0; which < H2_TEXTURE_SIDECAR_FILES; which++)
	{
		files[which] = NULL;
		raw[which] = NULL;
		counts[which] = 0;
		if (which == H2_TEXTURE_SIDECAR_FILE_SIDECAR && !h2_texture_sidecar_enabled())
			continue;
		files[which] = sidecar_file_read(map_name, level_path, (int)which, &raw[which], &counts[which]);
		count += counts[which];
	}
	if (!files[H2_TEXTURE_SIDECAR_FILE_SIDECAR] && h2_texture_sidecar_enabled())
	{
		unsigned char const *entries;
		long entry_count;

		if (h2_level_direct_sidecar(&entries, &entry_count) &&
			(raw[H2_TEXTURE_SIDECAR_FILE_SIDECAR] = malloc((size_t)entry_count * H2_TEXTURE_SIDECAR_ENTRY_SIZE)) != NULL)
		{
			memcpy(raw[H2_TEXTURE_SIDECAR_FILE_SIDECAR], entries, (size_t)entry_count * H2_TEXTURE_SIDECAR_ENTRY_SIZE);
			counts[H2_TEXTURE_SIDECAR_FILE_SIDECAR] = (unsigned long)entry_count;
			count += (unsigned long)entry_count;
			direct = TRUE;
		}
	}
	if (!files[H2_TEXTURE_SIDECAR_FILE_SIDECAR] && !direct && h2_texture_sidecar_enabled())
	{
		FILE *level = fopen(level_path, "rb");

		/* (a Halo 2 level's: said; any other map has none) */
		if (level)
		{
			unsigned char head[0x800];

			if (fread(head, 1, sizeof(head), level) == sizeof(head) && read_u32(head + 0x700) == 0x68326C76 /* 'h2lv' */)
				platform_log("h2 textures: %s has no sidecar of its own: its textures as the level's file has them", map_name);
			fclose(level);
		}
	}
	if (!count)
	{
		for (which = 0; which < H2_TEXTURE_SIDECAR_FILES; which++)
		{
			if (files[which])
				fclose(files[which]);
		}
		return;
	}
	/* (malloc and memset, not calloc: cseries.h sends malloc and free to
	debug_memory.c's, and its free refuses a block the C library's calloc
	gave - the crash at map unload) */
	h2_texture_sidecar.entries = malloc(count * sizeof(struct sidecar_entry));
	h2_texture_sidecar.entry_of_tag = malloc(MAXIMUM_TAGS * sizeof(unsigned short));
	if (h2_texture_sidecar.entries)
		memset(h2_texture_sidecar.entries, 0, count * sizeof(struct sidecar_entry));
	if (h2_texture_sidecar.entry_of_tag)
		memset(h2_texture_sidecar.entry_of_tag, 0, MAXIMUM_TAGS * sizeof(unsigned short));
	if (!h2_texture_sidecar.entries || !h2_texture_sidecar.entry_of_tag)
	{
		for (which = 0; which < H2_TEXTURE_SIDECAR_FILES; which++)
		{
			if (files[which])
				fclose(files[which]);
			if (raw[which])
				free(raw[which]);
		}
		h2_texture_sidecar_unloaded();
		return;
	}
	for (which = 0; which < H2_TEXTURE_SIDECAR_FILES; which++)
		h2_texture_sidecar.files[which] = files[which];
	h2_texture_sidecar.entry_count = (long)count;
	at = 0;
	for (which = 0; which < H2_TEXTURE_SIDECAR_FILES; which++)
	{
		unsigned long entries_offset = 0;
		int in_memory = direct && which == H2_TEXTURE_SIDECAR_FILE_SIDECAR;

		if (!files[which] && !in_memory)
			continue;
		if (files[which])
		{
			unsigned char header[H2_TEXTURE_SIDECAR_HEADER_SIZE];

			if (fseek(files[which], 0, SEEK_SET) == 0 && fread(header, 1, sizeof(header), files[which]) == sizeof(header))
				entries_offset = read_u32(header + 12);
		}
	for (i = 0; i < (long)counts[which]; i++, at++)
	{
		unsigned char const *e = raw[which] + H2_TEXTURE_SIDECAR_ENTRY_SIZE * i;
		struct sidecar_entry *entry = &h2_texture_sidecar.entries[at];
		long tag_index;
		struct bitmap_group *group;
		struct bitmap_data *bitmap;

		memcpy(entry->name, e, H2_TEXTURE_SIDECAR_NAME_SIZE);
		entry->name[H2_TEXTURE_SIDECAR_NAME_SIZE - 1] = 0;
		entry->width = read_u16(e + 200);
		entry->height = read_u16(e + 202);
		entry->mipmaps = read_u16(e + 204);
		entry->format = read_u16(e + 206);
		entry->flags = read_u16(e + 208);
		entry->kind = read_u16(e + 210);
		entry->offset = read_u32(e + 212);
		entry->size = read_u32(e + 216);
		entry->file = (unsigned char)which;
		{
			unsigned long bits = read_u32(e + 220);

			memcpy(&entry->bump_scale, &bits, 4);
		}
		if (!entry->size || (!in_memory && (entry->offset < H2_TEXTURE_SIDECAR_HEADER_SIZE || entry->offset + entry->size > entries_offset)))
		{
			missing++;
			continue;
		}
		tag_index = which == H2_TEXTURE_SIDECAR_FILE_EFFECTS ? fx_entry_tag(entry) : tag_loaded('bitm', entry->name);
		if (tag_index == NONE)
		{
			missing++;
			continue;
		}
		group = bitmap_group_get(tag_index);
		if (!group || group->bitmaps.count < 1)
		{
			missing++;
			continue;
		}
		bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, 0, struct bitmap_data);
		/* (one already in the texture cache - none should be, as the level
		has just loaded - is left as it is) */
		if (bitmap->base_address || bitmap->cache_block_index != NONE)
		{
			kept++;
			continue;
		}
		/* (the effects' pixel cache: the bitmap as the level's tag has it,
		its pixels left out of the level's file - its size the entry's, no
		more: h2_effects.c) */
		if (which == H2_TEXTURE_SIDECAR_FILE_EFFECTS)
		{
			if (bitmap->width != (short)entry->width || bitmap->height != (short)entry->height ||
				bitmap->format != (short)entry->format || (unsigned long)bitmap->pixels_size != entry->size ||
				(unsigned long)bitmap->pixels_offset != entry->offset)
			{
				platform_log("h2 textures: %s: the effects' pixel cache's %s is %ux%u format %u (%lu bytes at %08lx), the level's tag "
					"%dx%d format %d (%ld bytes at %08lx): not used", map_name, entry->name, entry->width, entry->height, entry->format,
					entry->size, entry->offset, (int)bitmap->width, (int)bitmap->height, (int)bitmap->format, (long)bitmap->pixels_size,
					(unsigned long)bitmap->pixels_offset);
				missing++;
				continue;
			}
			entry->given = 1;
			h2_texture_sidecar.entry_of_tag[tag_index & (MAXIMUM_TAGS - 1)] = (unsigned short)(at + 1);
			fx_given++;
			fx_count += entry->size;
			continue;
		}
		bitmap->width = (short)entry->width;
		bitmap->height = (short)entry->height;
		bitmap->mipmap_count = (short)entry->mipmaps;
		bitmap->format = (short)entry->format;
		bitmap->flags = (unsigned short)((entry->flags & ~BITMAP_CACHED_FLAG) | (bitmap->flags & BITMAP_CACHED_FLAG));
		bitmap->registration_point.x = (short)(entry->width / 2);
		bitmap->registration_point.y = (short)(entry->height / 2);
		bitmap->pixels_size = (long)entry->size;
		group->mipmap_count = (short)entry->mipmaps;
		/* (a direct level's: read by the texture source at its record's offset) */
		if (in_memory)
			bitmap->pixels_offset = (long)entry->offset;
		else
			h2_texture_sidecar.entry_of_tag[tag_index & (MAXIMUM_TAGS - 1)] = (unsigned short)(at + 1);
		/* (a placeholder bump map: its shader - the entry's name less " bump" -
		given it, the bump map at 0x128 and its scale at 0x124 of the
		shader_environment) */
		if (entry->kind == H2_TEXTURE_SIDECAR_BUMP)
		{
			char shader_name[H2_TEXTURE_SIDECAR_NAME_SIZE];
			size_t length = strlen(entry->name);
			long shader_index;

			if (length > 5 && !strcmp(entry->name + length - 5, " bump"))
			{
				snprintf(shader_name, sizeof(shader_name), "%.*s", (int)(length - 5), entry->name);
				shader_index = tag_loaded('senv', shader_name);
				if (shader_index != NONE)
				{
					unsigned char *shader = (unsigned char *)tag_get('senv', shader_index);
					unsigned long group = 'bitm';

					memcpy(shader + 0x128, &group, 4);
					memcpy(shader + 0x128 + 12, &tag_index, 4);
					memcpy(shader + 0x124, &entry->bump_scale, 4);
					bumps++;
				}
			}
		}
		h2_texture_sidecar.given++;
		h2_texture_sidecar.bytes_given += entry->size;
	}
	}
	for (which = 0; which < H2_TEXTURE_SIDECAR_FILES; which++)
	{
		if (raw[which])
			free(raw[which]);
	}
#ifdef HALO_PC_TEXTURE_TIER
	/* (the PC tier: the level's textures sampled with anisotropy, 8 -
	HALO_H2_ANISOTROPY; d3d8_gl.c) */
	{
		extern int halo_texture_anisotropy_floor;
		char const *anisotropy = getenv("HALO_H2_ANISOTROPY");

		halo_texture_anisotropy_floor = h2_texture_sidecar.given ? (anisotropy ? atoi(anisotropy) : 8) : 0;
	}
#endif
	if (files[H2_TEXTURE_SIDECAR_FILE_SIDECAR] || direct)
		platform_log("h2 textures: %s: %ld of %ld bitmaps full size from the %s (%lu KB; %ld shaders given bump maps; "
			"%ld not in the level, %ld already read)", map_name, h2_texture_sidecar.given,
			(long)counts[H2_TEXTURE_SIDECAR_FILE_SIDECAR], direct ? "texture source (made again on demand: a direct level)" : "sidecar",
			h2_texture_sidecar.bytes_given / 1024, bumps, missing, kept);
	if (files[H2_TEXTURE_SIDECAR_FILE_EFFECTS])
		platform_log("h2 textures: %s: %ld of %ld effect sprite sheets' pixels from the effects' pixel cache (%lu KB)", map_name,
			fx_given, (long)counts[H2_TEXTURE_SIDECAR_FILE_EFFECTS], fx_count / 1024);
}

void h2_texture_sidecar_unloaded(void)
{
#ifdef HALO_PC_TEXTURE_TIER
	{
		extern int halo_texture_anisotropy_floor;

		halo_texture_anisotropy_floor = 0;
	}
#endif
	lock();
	{
		long which;

		for (which = 0; which < H2_TEXTURE_SIDECAR_FILES; which++)
		{
			if (h2_texture_sidecar.files[which])
				fclose(h2_texture_sidecar.files[which]);
			h2_texture_sidecar.files[which] = NULL;
		}
	}
	/* (the game's free, debug_memory.c's, takes no NULL) */
	if (h2_texture_sidecar.entries)
		free(h2_texture_sidecar.entries);
	if (h2_texture_sidecar.entry_of_tag)
		free(h2_texture_sidecar.entry_of_tag);
	h2_texture_sidecar.entries = NULL;
	h2_texture_sidecar.entry_of_tag = NULL;
	h2_texture_sidecar.entry_count = 0;
	h2_texture_sidecar.given = 0;
	h2_texture_sidecar.bytes_given = 0;
	unlock();
}

int h2_texture_sidecar_owns(long tag_index)
{
	unsigned short const *table = h2_texture_sidecar.entry_of_tag;

	return table && tag_index != NONE && table[tag_index & (MAXIMUM_TAGS - 1)] != 0;
}

/* the entry a read of the tag's record at offset is for, and where in it
the read begins (*skip): the tag's own (the PC tier's sidecar: from its
start), or for the effects' pixel cache the image whose pixels hold offset
- a sheet's other images' records carry its tag index (read as its first
image, they were drawn as stripes), and the texture streaming reads an
image's lower levels from past its start, as from a level's file; NULL none */
static struct sidecar_entry const *sidecar_entry_of_read(long tag_index, long offset, unsigned long *skip)
{
	struct sidecar_entry const *entry;
	long i;

	*skip = 0;
	if (!h2_texture_sidecar.entry_of_tag || tag_index == NONE || !h2_texture_sidecar.entry_of_tag[tag_index & (MAXIMUM_TAGS - 1)])
		return NULL;
	entry = &h2_texture_sidecar.entries[h2_texture_sidecar.entry_of_tag[tag_index & (MAXIMUM_TAGS - 1)] - 1];
	if (entry->file != H2_TEXTURE_SIDECAR_FILE_EFFECTS)
		return entry;
	for (i = 0; i < h2_texture_sidecar.entry_count; i++)
	{
		struct sidecar_entry const *e = &h2_texture_sidecar.entries[i];

		if (e->file == H2_TEXTURE_SIDECAR_FILE_EFFECTS && e->given && offset >= 0 && (unsigned long)offset >= e->offset &&
			(unsigned long)offset < e->offset + e->size)
		{
			*skip = (unsigned long)offset - e->offset;
			return e;
		}
	}
	return NULL;
}

int h2_texture_sidecar_offsets(long tag_index)
{
	unsigned short const *table = h2_texture_sidecar.entry_of_tag;

	return table && tag_index != NONE && table[tag_index & (MAXIMUM_TAGS - 1)] != 0 &&
		h2_texture_sidecar.entries[table[tag_index & (MAXIMUM_TAGS - 1)] - 1].file == H2_TEXTURE_SIDECAR_FILE_EFFECTS;
}

int h2_texture_sidecar_read(long tag_index, long offset, long size, void *buffer)
{
	int ok = FALSE;
	struct sidecar_entry const *entry;
	unsigned long skip;

	lock();
	entry = sidecar_entry_of_read(tag_index, offset, &skip);
	if (entry && h2_texture_sidecar.files[entry->file])
	{
		FILE *file = h2_texture_sidecar.files[entry->file];
		unsigned long left = entry->size - skip;
		unsigned long n = size >= 0 && (unsigned long)size < left ? (unsigned long)size : left;

		/* (through a scratch buffer, a megabyte at a time: the texture
		cache's pages the renderer watches are read-only until written by
		the program, which its fault handler sees - a read() into them
		fails instead (EFAULT), as the sky dome's did in a network game) */
		unsigned char *scratch = malloc(1 << 20);
		unsigned long done = 0;

		ok = scratch && fseek(file, (long)(entry->offset + skip), SEEK_SET) == 0;
		while (ok && done < n)
		{
			unsigned long piece = n - done < (1UL << 20) ? n - done : (1UL << 20);

			ok = fread(scratch, 1, piece, file) == piece;
			if (ok)
				memcpy((unsigned char *)buffer + done, scratch, piece);
			done += piece;
		}
		if (scratch)
			free(scratch);
		if (getenv("HALO_H2_SIDECAR_DEBUG"))
			platform_log("h2 textures: read %s %lu of %lu bytes from %lu (asked %ld): %s", entry->name, n, entry->size, skip, size,
				ok ? "ok" : "FAILED");
	}
	else if (getenv("HALO_H2_SIDECAR_DEBUG"))
		platform_log("h2 textures: read of tag %08lx at %08lx: not the sidecar's now", (unsigned long)tag_index, (unsigned long)offset);
	unlock();
	return ok;
}
