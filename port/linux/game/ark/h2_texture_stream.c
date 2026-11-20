/*
H2_TEXTURE_STREAM.C
Texture streaming for a Halo 2 level (h2_texture_stream.h).

The Xbox kept a level's small texture levels resident and streamed the large
ones from the disc as surfaces needed them. A level made by h2_level.c holds
every bitmap whole, its levels one after another from the largest, as Halo's
bitmaps are laid out; the game's texture cache read each whole into its
window (22 MB, 16 KB pages) on the first draw, blocking the drawing thread
("texture cache wait" in the frame-hitch line), and evicted others to make
room, to read them again later.

With HALO_H2_TEXTURE_STREAMING=1, once a Halo 2 level's tags are loaded, each
large power-of-two 2D DXT texture's record is made to describe its base
alone - its levels of 64 px and under (HALO_H2_TEXTURE_BASE=<px>) - as a
Custom Edition map's large textures are drawn from their second level
(custom_edition_bitmaps.c, levels_dropped): the pixels offset moved past the
levels above, the size, width, height and level count lowered. The bases are
read from the level file at the load, one after another, into a block of
their own outside the memory window (platform_contiguous_side_alloc), with a
hardware format each: the texture cache never loads or evicts them, and no
draw waits for a texture. The Vita's decoder (vita_textures.c) asks here for
the levels above the base when a surface is drawn with them, reads them from
the level file on its own thread and swaps the whole chain in; the texture
pool's recycling lets them go again.

The bitmap records are the tag cache's: the next map's tags replace them.
The base block is kept across maps (grow-only; its size is the default's or
HALO_H2_TEXTURE_BASE_MB), its bytes marked with a new generation at each
load, so that a texture decoded from an address last map is decoded again.
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "tag_files/tag_groups.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "h2_texture_stream.h"
#include "load_profile.h"

/* (h2_level.h: the loading screen drawn between pieces of a long step) */
void h2_load_draw(void);
void h2_load_draw_coarse(void);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HALO_LINUX
#include "render_epoch.h" /* HALO_ENV_ONCE */
#endif

void platform_log(const char *format, ...);
boolean custom_edition_maps_h2_level(char const *level_name);
/* (xbox_kernel.c's on Linux and the Vita, the guest's under
halo_windows_api_names.h's name on Windows: stdcall everywhere, as
xdk_pdb.h has it. Declared cdecl, the i686 build's LTO took every call for
undefined behaviour and deleted the read waits around it: the base and
detail reads returned at once, and the cache file thread wrote into the
freed buffer and the dead flag later - heap corruption, Nov 3 2026) */
int __stdcall SwitchToThread(void);
/* (xbox_texture_cache.c: a hardware format for a bitmap at its base address) */
void texture_cache_hardware_format_make(struct bitmap_data *bitmap, void *texture);
unsigned long texture_cache_hardware_format_size(void);
short cache_file_read_bitmap(long tag_index, long offset, long size, void *buffer, boolean *completion_flag_reference,
	boolean blocking);
/* (port/linux/src/xbox_memory.c: a block outside the memory window whose
addresses the D3D textures take as the window's) */
void *platform_contiguous_side_alloc(unsigned long size, unsigned long alignment);
void platform_contiguous_free(void *address);
void platform_contiguous_side_mark(unsigned long address, unsigned long size, unsigned long serial);

/* a prefix compared regardless of case (the tag names' folders) */
static int prefix_is(char const *name, char const *prefix)
{
	while (*prefix)
	{
		char a = *name++, b = *prefix++;
		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (a != b)
			return 0;
	}
	return 1;
}

/* ---------- constants */

enum
{
	/* (xbox_texture_cache.c's, bitmap_extract.c's) */
	_stream_bitmap_type_2d = 0,
	_stream_bitmap_format_dxt1 = 14,
	_stream_bitmap_format_dxt3 = 15,
	_stream_bitmap_format_dxt5 = 16,
	_stream_bitmap_format_p8_bump = 17,
	_stream_bitmap_has_power_of_two_dimensions_bit = 0,
	_stream_bitmap_compressed_bit = 1,
	_stream_bitmap_palettized_bit = 2,
	_stream_bitmap_linear_bit = 4,
	_stream_bitmap_cached_bit = 7,
	_stream_bitmap_group_type_2d_textures = 0,

	BASE_DEFAULT_PIXELS = 64,
	/* a file read's size is rounded up to this (cache_files_windows.c) */
	READ_SECTOR = 512,
	/* the most levels a texture has above its base (4096 px over 64) */
	MAXIMUM_DROPPED = 8,
	ADDRESS_SLOTS = H2_TEXTURE_STREAM_MAXIMUM_SOURCES * 2,
	/* the bases' memory comes in pieces of this much (a side block is 4 MB
	at most), taken as a level needs them and kept for the next */
	PIECE_BYTES = 2 << 20,
	MAXIMUM_PIECES = 32,
	/* a texture no larger than this that cannot stream (small, or without a
	level 64 px or under) is resident whole: in the texture cache it took a
	16 KB page and a read of its own */
	WHOLE_RESIDENT_BYTES = 16384
};

/* ---------- structures */

struct stream_source
{
	struct bitmap_data *bitmap;
	long tag_index;
	/* the bitmap's record before the base was cut: its pixels' file offset
	(the pixel data's included), its size, dimensions and level count */
	long pixels_offset, pixels_size;
	short width, height, mipmap_count, format;
	/* the levels above the base, each's bytes in the file */
	unsigned long dropped;
	unsigned long level_bytes[MAXIMUM_DROPPED];
	/* the base in the block, and the hardware format made for it */
	unsigned char *base;
	unsigned long base_bytes;
	void *hardware_format;
};

/* ---------- globals */

static struct
{
	int active;
	unsigned long serial;
	struct stream_source *sources;
	long source_count;
	/* by the base's address: the source's index + 1 (open addressing) */
	struct
	{
		unsigned long address;
		long source_plus_one;
	} *by_address;
	struct
	{
		unsigned char *base;
		unsigned long used;
	} pieces[MAXIMUM_PIECES];
	long piece_count;
	unsigned long block_size, block_used;
	unsigned char *hardware_formats;
	unsigned long base_total, detail_total, skipped, over_block;
	/* the bitmaps left to the texture cache, by why: not a 2D power-of-two
	one, not DXT, 64 px or under, too few levels, the rest */
	unsigned long skipped_kind, skipped_format, skipped_small, skipped_levels, skipped_other, skipped_empty;
	/* the textures resident whole (no detail to stream) */
	unsigned long whole;
	unsigned long long load_us;
} h2_texture_stream;

/* ---------- private code */

static unsigned long address_slot(unsigned long address)
{
	unsigned long hash = address * 2654435761ul;
	return (hash >> 7) & (ADDRESS_SLOTS - 1);
}

static void by_address_put(unsigned long address, long source)
{
	unsigned long slot = address_slot(address), tries;
	for (tries = 0; tries < ADDRESS_SLOTS; tries++, slot = (slot + 1) & (ADDRESS_SLOTS - 1))
	{
		if (!h2_texture_stream.by_address[slot].source_plus_one || h2_texture_stream.by_address[slot].address == address)
		{
			h2_texture_stream.by_address[slot].address = address;
			h2_texture_stream.by_address[slot].source_plus_one = source + 1;
			return;
		}
	}
}

static struct stream_source *by_address_get(unsigned long address)
{
	unsigned long slot = address_slot(address), tries;
	if (!h2_texture_stream.by_address)
		return NULL;
	for (tries = 0; tries < ADDRESS_SLOTS; tries++, slot = (slot + 1) & (ADDRESS_SLOTS - 1))
	{
		if (!h2_texture_stream.by_address[slot].source_plus_one)
			return NULL;
		if (h2_texture_stream.by_address[slot].address == address)
			return &h2_texture_stream.sources[h2_texture_stream.by_address[slot].source_plus_one - 1];
	}
	return NULL;
}

static unsigned long base_pixels(void)
{
	char const *setting = getenv("HALO_H2_TEXTURE_BASE");
	long pixels = setting ? atol(setting) : BASE_DEFAULT_PIXELS;
	return pixels >= 4 && pixels <= 1024 ? (unsigned long)pixels : BASE_DEFAULT_PIXELS;
}

static unsigned long block_size_wanted(void)
{
	char const *setting = getenv("HALO_H2_TEXTURE_BASE_MB");
	long megabytes = setting ? atol(setting) : H2_TEXTURE_STREAM_BASE_DEFAULT_MB;
	return (megabytes >= 1 && megabytes <= 64 ? (unsigned long)megabytes : H2_TEXTURE_STREAM_BASE_DEFAULT_MB) << 20;
}

/* the level's bytes in the file: a DXT level's blocks (a level under 4 px
takes a block, as bitmap_mipmap_get_pixel_data_size counts them), another
format's pixels */
static unsigned long dxt_level_bytes(short width, short height, short format, unsigned long level)
{
	unsigned long w = (unsigned long)width >> level, h = (unsigned long)height >> level;
	if (format >= _stream_bitmap_format_dxt1 && format <= _stream_bitmap_format_dxt5)
	{
		unsigned long block = format == _stream_bitmap_format_dxt1 ? 8 : 16;
		if (w < 4)
			w = 4;
		if (h < 4)
			h = 4;
		return (w / 4) * (h / 4) * block;
	}
	if (!w)
		w = 1;
	if (!h)
		h = 1;
	return w * h * (unsigned long)bitmap_format_get_bits_per_pixel(format) / 8;
}

/* room for a base in the pieces: NULL when the budget is spent or no block
is to be had */
static unsigned char *block_alloc(unsigned long padded)
{
	long index;
	if (h2_texture_stream.block_used + padded > h2_texture_stream.block_size || padded > PIECE_BYTES)
		return NULL;
	for (index = 0; index < h2_texture_stream.piece_count; index++)
		if (h2_texture_stream.pieces[index].used + padded <= PIECE_BYTES)
			break;
	if (index == h2_texture_stream.piece_count)
	{
		unsigned char *base;
		if (index >= MAXIMUM_PIECES || (base = platform_contiguous_side_alloc(PIECE_BYTES, 4096)) == NULL)
			return NULL;
		h2_texture_stream.pieces[index].base = base;
		h2_texture_stream.pieces[index].used = 0;
		h2_texture_stream.piece_count++;
	}
	h2_texture_stream.pieces[index].used += padded;
	h2_texture_stream.block_used += padded;
	return h2_texture_stream.pieces[index].base + h2_texture_stream.pieces[index].used - padded;
}

/* the bitmap made base-resident: FALSE for one the stream leaves to the
texture cache */
static boolean source_make(struct bitmap_data *bitmap, long tag_index, unsigned long base)
{
	struct stream_source *source;
	unsigned long dropped = 0, prefix = 0, level, base_bytes, padded;
	unsigned long largest = (unsigned long)(bitmap->width > bitmap->height ? bitmap->width : bitmap->height);
	if (bitmap->type != _stream_bitmap_type_2d ||
		!TEST_FLAG(bitmap->flags, _stream_bitmap_has_power_of_two_dimensions_bit) ||
		TEST_FLAG(bitmap->flags, _stream_bitmap_linear_bit) || TEST_FLAG(bitmap->flags, _stream_bitmap_palettized_bit))
	{
		h2_texture_stream.skipped_kind++;
		return FALSE;
	}
	/* (DXT, or a format of whole pixels a level; not a palette's indices,
	whose palette the decoder reads with them) */
	if (bitmap->format == _stream_bitmap_format_p8_bump || bitmap->format < 0 ||
		bitmap_format_get_bits_per_pixel(bitmap->format) <= 0 ||
		(TEST_FLAG(bitmap->flags, _stream_bitmap_compressed_bit) !=
			(bitmap->format >= _stream_bitmap_format_dxt1 && bitmap->format <= _stream_bitmap_format_dxt5)))
	{
		h2_texture_stream.skipped_format++;
		return FALSE;
	}
	/* (levels_dropped holds the Xbox's padding in a level's record: not read) */
	if (!TEST_FLAG(bitmap->flags, _stream_bitmap_cached_bit) || bitmap->cache_block_index != NONE || bitmap->base_address)
	{
		h2_texture_stream.skipped_other++;
		return FALSE;
	}
	if (bitmap->pixels_size <= 0)
	{
		h2_texture_stream.skipped_empty++;
		return FALSE;
	}
	/* (port) a bitmap whose pixels are the PC tier's sidecar's (read whole,
	at no offset) is the texture cache's; the effects' pixel cache's sprite
	sheets are streamed as the level's own bitmaps are - that cache serves
	reads at their offsets (h2_texture_sidecar.h): resident from the load
	as in a level that keeps them, so the game's first use of one (a decal,
	made only when its texture is in) is the same */
	{
		int h2_texture_sidecar_owns(long tag_index);
		int h2_texture_sidecar_offsets(long tag_index);

		if (h2_texture_sidecar_owns(tag_index) && !h2_texture_sidecar_offsets(tag_index))
		{
			h2_texture_stream.skipped_other++;
			return FALSE;
		}
	}
	while ((largest >> dropped) > base && dropped < MAXIMUM_DROPPED)
		dropped++;
	if (largest <= base || (largest >> dropped) > base || (long)dropped > (long)bitmap->mipmap_count)
	{
		/* (no base to cut: resident whole if small, else the cache's) */
		if ((unsigned long)bitmap->pixels_size > WHOLE_RESIDENT_BYTES)
		{
			if (largest <= base)
				h2_texture_stream.skipped_small++;
			else
				h2_texture_stream.skipped_levels++;
			return FALSE;
		}
		dropped = 0;
		h2_texture_stream.whole++;
	}
	if (h2_texture_stream.source_count >= H2_TEXTURE_STREAM_MAXIMUM_SOURCES)
		return FALSE;
	for (level = 0; level < dropped; level++)
		prefix += dxt_level_bytes(bitmap->width, bitmap->height, bitmap->format, level);
	if ((long)prefix >= bitmap->pixels_size)
		return FALSE;
	base_bytes = (unsigned long)bitmap->pixels_size - prefix;
	/* (the read's size is rounded up to a sector: room for it) */
	padded = (base_bytes + READ_SECTOR - 1) & ~(unsigned long)(READ_SECTOR - 1);
	source = &h2_texture_stream.sources[h2_texture_stream.source_count];
	memset(source, 0, sizeof(*source));
	source->base = block_alloc(padded);
	if (!source->base)
	{
		h2_texture_stream.over_block++;
		return FALSE;
	}
	source->bitmap = bitmap;
	source->tag_index = tag_index;
	source->pixels_offset = bitmap->pixels_offset;
	source->pixels_size = bitmap->pixels_size;
	source->width = bitmap->width;
	source->height = bitmap->height;
	source->mipmap_count = bitmap->mipmap_count;
	source->format = bitmap->format;
	source->dropped = dropped;
	for (level = 0; level < dropped; level++)
		source->level_bytes[level] = dxt_level_bytes(bitmap->width, bitmap->height, bitmap->format, level);
	source->base_bytes = base_bytes;
	source->hardware_format =
		h2_texture_stream.hardware_formats + h2_texture_stream.source_count * texture_cache_hardware_format_size();
	/* the record: its base alone */
	bitmap->pixels_offset += (long)prefix;
	bitmap->pixels_size = (long)base_bytes;
	bitmap->width = (short)((unsigned long)bitmap->width >> dropped);
	bitmap->height = (short)((unsigned long)bitmap->height >> dropped);
	bitmap->mipmap_count = (short)(bitmap->mipmap_count - (short)dropped);
	bitmap->levels_dropped = (short)(BITMAP_LEVELS_DROPPED_STREAM | dropped);
	h2_texture_stream.source_count++;
	h2_texture_stream.base_total += base_bytes;
	h2_texture_stream.detail_total += prefix;
	return TRUE;
}

/* the base read from the level file into the block, and its hardware format
made (the game's cache file thread serves the read; waited for here, at the
load) */
static boolean source_read(struct stream_source *source)
{
	volatile boolean done = FALSE;
	struct bitmap_data *bitmap = source->bitmap;
	cache_file_read_bitmap(source->tag_index, bitmap->pixels_offset, bitmap->pixels_size, source->base, (boolean *)&done,
		TRUE);
	while (!done)
		SwitchToThread();
	if (done != TRUE)
		return FALSE;
	bitmap->base_address = source->base;
	texture_cache_hardware_format_make(bitmap, source->hardware_format);
	bitmap->hardware_format = source->hardware_format;
	by_address_put((unsigned long)source->base, source - h2_texture_stream.sources);
	return TRUE;
}

/* (the bases read several at once: each read non-blocking on the cache
file thread, a derived one's parked for the texture source's two workers -
h2_texture_source.c -, while textures.dat's are served beside them; one at a
time, the owner's Vita took 11.7 s for Zanzibar's 639. HALO_H2_BASES_IN_FLIGHT,
1 the old blocking reads) */
enum { BASES_IN_FLIGHT_MAXIMUM = 16 };

static long bases_in_flight(void)
{
	static long count = -1;

	if (count < 0)
	{
		char const *setting = getenv("HALO_H2_BASES_IN_FLIGHT");

		count = setting ? atol(setting) : 8;
		if (count < 1)
			count = 1;
		if (count > BASES_IN_FLIGHT_MAXIMUM)
			count = BASES_IN_FLIGHT_MAXIMUM;
	}
	return count;
}

static boolean source_read_begin(struct stream_source *source, volatile boolean *done)
{
	struct bitmap_data *bitmap = source->bitmap;

	*done = FALSE;
	return cache_file_read_bitmap(source->tag_index, bitmap->pixels_offset, bitmap->pixels_size, source->base, (boolean *)done,
		FALSE) != NONE;
}

static void source_read_end(struct stream_source *source)
{
	struct bitmap_data *bitmap = source->bitmap;

	bitmap->base_address = source->base;
	texture_cache_hardware_format_make(bitmap, source->hardware_format);
	bitmap->hardware_format = source->hardware_format;
	by_address_put((unsigned long)source->base, source - h2_texture_stream.sources);
}

/* a source whose base could not be read: its record as it was */
static void source_undo(struct stream_source *source)
{
	struct bitmap_data *bitmap = source->bitmap;
	bitmap->pixels_offset = source->pixels_offset;
	bitmap->pixels_size = source->pixels_size;
	bitmap->width = source->width;
	bitmap->height = source->height;
	bitmap->mipmap_count = source->mipmap_count;
	bitmap->levels_dropped = 0;
	bitmap->base_address = NULL;
	bitmap->hardware_format = NULL;
	source->bitmap = NULL;
}

static void stream_clear(void)
{
	h2_texture_stream.active = 0;
	h2_texture_stream.source_count = 0;
	h2_texture_stream.block_used = 0;
	{
		long index;
		for (index = 0; index < h2_texture_stream.piece_count; index++)
			h2_texture_stream.pieces[index].used = 0;
	}
	h2_texture_stream.base_total = h2_texture_stream.detail_total = h2_texture_stream.skipped =
		h2_texture_stream.over_block = 0;
	h2_texture_stream.skipped_kind = h2_texture_stream.skipped_format = h2_texture_stream.skipped_small =
		h2_texture_stream.skipped_levels = h2_texture_stream.skipped_other = h2_texture_stream.skipped_empty =
		h2_texture_stream.whole = 0;
	if (h2_texture_stream.by_address)
		memset(h2_texture_stream.by_address, 0, sizeof(h2_texture_stream.by_address[0]) * ADDRESS_SLOTS);
}

static boolean stream_allocate(void)
{
	if (!h2_texture_stream.sources)
		h2_texture_stream.sources = calloc(H2_TEXTURE_STREAM_MAXIMUM_SOURCES, sizeof(struct stream_source));
	if (!h2_texture_stream.by_address)
		h2_texture_stream.by_address = calloc(ADDRESS_SLOTS, sizeof(h2_texture_stream.by_address[0]));
	if (!h2_texture_stream.hardware_formats)
		h2_texture_stream.hardware_formats =
			calloc(H2_TEXTURE_STREAM_MAXIMUM_SOURCES, texture_cache_hardware_format_size());
	h2_texture_stream.block_size = block_size_wanted();
	return h2_texture_stream.sources && h2_texture_stream.by_address && h2_texture_stream.hardware_formats;
}

/* ---------- public code */

int h2_texture_stream_enabled(void)
{
	static int enabled = -1;
	if (enabled < 0)
	{
		/* on by default for Halo 2 levels (the only maps it touches) since
		the Vita3K image check of Nov 3; HALO_H2_TEXTURE_STREAMING=0 off */
		char const *setting = getenv("HALO_H2_TEXTURE_STREAMING");
		enabled = !setting || atoi(setting) != 0;
	}
	return enabled;
}

/* (cache_files.c, as a map's tags go) the bases' blocks given back with
the level, not kept for the next: its make has their room, as the first
level's had (an insider.5 tester's Vita: Turf's held into Zanzibar's make,
which ran the system out of user RAM); the next level's load makes them
again. A decode still reading one is waited for (xbox_memory.c side_free) */
void h2_texture_stream_map_unloaded(void)
{
	long index;
	unsigned long bytes = 0;

	stream_clear();
	for (index = 0; index < h2_texture_stream.piece_count; index++)
	{
		if (h2_texture_stream.pieces[index].base)
		{
			platform_contiguous_free(h2_texture_stream.pieces[index].base);
			bytes += PIECE_BYTES;
		}
		h2_texture_stream.pieces[index].base = NULL;
		h2_texture_stream.pieces[index].used = 0;
	}
	if (bytes)
		platform_log("h2 texture streaming: the bases' %lu KB given back with the map", bytes / 1024);
	h2_texture_stream.piece_count = 0;
}

void h2_texture_stream_map_loaded(char const *map_name)
{
	struct tag_iterator iterator;
	long tag_index, read = 0, failed = 0;
	unsigned long base = base_pixels(), index;
	unsigned long long started;
	stream_clear();
	if (!h2_texture_stream_enabled() || !custom_edition_maps_h2_level(map_name))
		return;
	if (!stream_allocate())
		return;
	started = halo_load_profile_now();
	tag_iterator_new(&iterator, BITMAP_GROUP_TAG);
	for (tag_index = tag_iterator_next(&iterator); tag_index != NONE; tag_index = tag_iterator_next(&iterator))
	{
		struct bitmap_group *group = tag_get(BITMAP_GROUP_TAG, tag_index);
		char const *name = tag_get_name(tag_index);
		long bitmap_index;
		/* (HALO_H2_TEXTURE_STREAM_DEBUG=1: each bitmap over 1 MB left to the
		texture cache, and why) */
		static int debug = -1;

		if (debug < 0)
			debug = getenv("HALO_H2_TEXTURE_STREAM_DEBUG") && atoi(getenv("HALO_H2_TEXTURE_STREAM_DEBUG")) != 0;
		if (!group || group->type != _stream_bitmap_group_type_2d_textures || !name ||
			prefix_is(name, "ui\\") || prefix_is(name, "interface\\"))
		{
			if (debug && group && name)
				for (bitmap_index = 0; bitmap_index < group->bitmaps.count; bitmap_index++)
				{
					struct bitmap_data *bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, bitmap_index, struct bitmap_data);

					if (bitmap->pixels_size > (1L << 20))
						platform_log("h2 texture streaming: %s #%ld (%ldx%ld format %d, %ld KB) the cache's: its group's type %d", name,
							bitmap_index, (long)bitmap->width, (long)bitmap->height, (int)bitmap->format, (long)bitmap->pixels_size / 1024,
							(int)group->type);
				}
			continue;
		}
		for (bitmap_index = 0; bitmap_index < group->bitmaps.count; bitmap_index++)
		{
			struct bitmap_data *bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, bitmap_index, struct bitmap_data);
			unsigned long kind = h2_texture_stream.skipped_kind, format = h2_texture_stream.skipped_format,
				other = h2_texture_stream.skipped_other, levels = h2_texture_stream.skipped_levels;

			if (!source_make(bitmap, tag_index, base))
			{
				h2_texture_stream.skipped++;
				if (debug && bitmap->pixels_size > (1L << 20))
					platform_log("h2 texture streaming: %s #%ld (%ldx%ld format %d flags %x, %ld KB) the cache's: %s", name, bitmap_index,
						(long)bitmap->width, (long)bitmap->height, (int)bitmap->format, (unsigned)bitmap->flags,
						(long)bitmap->pixels_size / 1024, kind != h2_texture_stream.skipped_kind ? "its kind" :
						format != h2_texture_stream.skipped_format ? "its format" : other != h2_texture_stream.skipped_other ?
						"loaded already, or another's" : levels != h2_texture_stream.skipped_levels ? "no level small enough" : "other");
			}
		}
	}
	h2_texture_stream.serial++;
	{
		volatile boolean done[BASES_IN_FLIGHT_MAXIMUM];
		long slot_source[BASES_IN_FLIGHT_MAXIMUM], slots = bases_in_flight(), busy = 0, k;

		for (k = 0; k < slots; k++)
			slot_source[k] = NONE;
		index = 0;
		while (index < (unsigned long)h2_texture_stream.source_count || busy)
		{
			boolean ended = FALSE;

			/* (the loading screen drawn between reads: h2_level.h) */
			h2_load_draw_coarse();
			for (k = 0; k < slots && index < (unsigned long)h2_texture_stream.source_count; k++)
			{
				struct stream_source *source = &h2_texture_stream.sources[index];

				if (slot_source[k] != NONE)
					continue;
				if (slots == 1)
				{
					/* (one at a time: blocking, as before) */
					if (source_read(source))
						read++;
					else
						done[k] = _cache_file_read_failed, slot_source[k] = (long)index, busy++;
					index++;
					break;
				}
				if (!source_read_begin(source, &done[k]))
				{
					/* (no request slot free and none of ours to wait for: this one
					read blocking) */
					if (!busy)
					{
						if (source_read(source))
							read++;
						else
							done[k] = _cache_file_read_failed, slot_source[k] = (long)index, busy++;
						index++;
					}
					break;
				}
				slot_source[k] = (long)index++;
				busy++;
			}
			for (k = 0; k < slots; k++)
			{
				struct stream_source *source;

				if (slot_source[k] == NONE || !done[k])
					continue;
				source = &h2_texture_stream.sources[slot_source[k]];
				if (done[k] == TRUE)
				{
					source_read_end(source);
					read++;
				}
				else
				{
					failed++;
					platform_log("h2 texture streaming: base of tag %08lx (%s) at %08lx, %ld bytes not read",
						(unsigned long)source->tag_index, tag_get_name(source->tag_index), (unsigned long)source->bitmap->pixels_offset,
						source->bitmap->pixels_size);
					source_undo(source);
				}
				slot_source[k] = NONE;
				busy--;
				ended = TRUE;
			}
			if (busy && !ended)
				SwitchToThread();
		}
	}
	/* (the block's bytes are this map's now: a texture decoded from them
	last map is decoded again) */
	for (index = 0; index < (unsigned long)h2_texture_stream.piece_count; index++)
		if (h2_texture_stream.pieces[index].used)
			platform_contiguous_side_mark((unsigned long)h2_texture_stream.pieces[index].base,
				h2_texture_stream.pieces[index].used, h2_texture_stream.serial);
	h2_texture_stream.active = read > 0;
	h2_texture_stream.load_us = halo_load_profile_now() - started;
	platform_log("h2 texture streaming: %ld textures' bases (%lu px and under; %lu small ones whole) resident, %lu KB of %lu "
		"KB read in %.0f ms; %lu KB of detail streamed on demand; %lu bitmaps left to the texture cache (%lu not 2D "
		"power-of-two, %lu not whole pixels, %lu small but over 16 KB, %lu without a level %lu px or under, %lu without "
		"pixels, %lu loaded already or another's, %lu no room), %lu failed to read",
		read, base, h2_texture_stream.whole, h2_texture_stream.base_total / 1024, h2_texture_stream.block_size / 1024,
		h2_texture_stream.load_us / 1000.0, h2_texture_stream.detail_total / 1024, h2_texture_stream.skipped,
		h2_texture_stream.skipped_kind, h2_texture_stream.skipped_format, h2_texture_stream.skipped_small,
		h2_texture_stream.skipped_levels, base, h2_texture_stream.skipped_empty, h2_texture_stream.skipped_other,
		h2_texture_stream.over_block, (unsigned long)failed);
	h2_texture_stream_report();
}

void *h2_texture_stream_resident(struct bitmap_data *bitmap)
{
	if (!h2_texture_stream.active || bitmap->cache_block_index != NONE || !bitmap->base_address || !bitmap->hardware_format)
	{
		return NULL;
	}
	return by_address_get((unsigned long)bitmap->base_address) ? bitmap->hardware_format : NULL;
}

int h2_texture_stream_detail(unsigned long base_address, unsigned long levels, struct h2_texture_stream_detail *detail)
{
	struct stream_source *source;
	unsigned long level, offset = 0, bytes = 0;
	if (!h2_texture_stream.active)
		return 0;
	source = by_address_get(base_address);
	if (!source || !source->bitmap || !levels || levels > source->dropped)
		return 0;
	for (level = 0; level < source->dropped - levels; level++)
		offset += source->level_bytes[level];
	for (; level < source->dropped; level++)
		bytes += source->level_bytes[level];
	detail->tag_index = source->tag_index;
	detail->file_offset = source->pixels_offset + (long)offset;
	detail->bytes = (long)bytes;
	detail->dropped = source->dropped;
	detail->levels = levels;
	return 1;
}

int h2_texture_stream_read(struct h2_texture_stream_detail const *detail, void *buffer)
{
	volatile boolean done = FALSE;
	if (!h2_texture_stream.active || detail->bytes <= 0)
		return 0;
	cache_file_read_bitmap(detail->tag_index, detail->file_offset, detail->bytes, buffer, (boolean *)&done, FALSE);
	while (!done)
		SwitchToThread();
	return done == TRUE;
}

unsigned long h2_texture_stream_serial(void)
{
	return h2_texture_stream.serial;
}

void h2_texture_stream_report(void)
{
	if (h2_texture_stream.active)
	{
		/* (the resident bases' bytes summed - FNV-1a over each in turn: a
		level whose bitmaps come from elsewhere - the sidecar, textures.dat
		at load - shows the same sum as the one that wrote them, or not) */
		unsigned long hash = 2166136261UL;
		long i;

		for (i = 0; i < h2_texture_stream.source_count; i++)
		{
			struct stream_source const *source = &h2_texture_stream.sources[i];
			unsigned long k;

			unsigned long one = 2166136261UL;

			if (!source->base)
				continue;
			for (k = 0; k < source->base_bytes; k++)
			{
				hash = (hash ^ source->base[k]) * 16777619UL;
				one = (one ^ source->base[k]) * 16777619UL;
			}
			/* (HALO_H2_STREAM_SUMS=1: each base's own sum, by tag) */
			if (HALO_ENV_ONCE("HALO_H2_STREAM_SUMS"))
				platform_log("h2 texture streaming: base %ld tag %08lx at %08lx, %lu bytes: sum %08lx (%s)", i,
					(unsigned long)source->tag_index, (unsigned long)source->bitmap->pixels_offset, source->base_bytes, one,
					tag_get_name(source->tag_index));
		}
		platform_log("h2 texture streaming: %ld bases resident (%lu KB, their bytes' sum %08lx), %lu KB of detail in the file",
			h2_texture_stream.source_count, h2_texture_stream.base_total / 1024, hash, h2_texture_stream.detail_total / 1024);
	}
}
