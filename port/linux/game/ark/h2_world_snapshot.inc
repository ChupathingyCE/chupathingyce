/*
H2_WORLD_SNAPSHOT.INC

(port) h2_world.h: a structure BSP's world, read from the MCC map once
(h2_world_load.inc, the room path: a level made without its BSPs gathered),
kept as a file - the BSP's room as the read left it (its tables, collision,
vertices and triangles, written at the room's own address), the runs'
records and the lightmap atlases' texels - and read back into the same room
at the next switch to that BSP, instead of gathering the map again.

Why: a Halo 2 cutscene switches BSPs between its scenes (Cairo Station's
intro seven times: 01_bsp_0, high_0, 01_bsp_1, high_0 ...) and every switch
gathered the BSP's world from the map on the game thread - 6.5 to 10.6 s
each on the Vita (count 1.8-4.2 s, fill 2.6-3.0, collision 0.6-1.5, the
runs 1.4), 0.8 s on the PC - while the scene's sounds ran on: the owner's
"laggy, audio not synced" Cairo cutscene (dev Vita run 20261108-140906).
The real engine's zone set keeps the cutscene's BSPs loaded; the Vita has no
room for three worlds, so the finished bytes are kept on the card and read
back (one sequential read). A skipped cutscene's revert to the first BSP and
a level's later loads read theirs the same way.

What is kept is what the build made, byte for byte: the file's payload is
summed (FNV-1a) as it is written and as it is read, and a snapshot is only
read when its keys match - the level and BSP, the room's address and size,
the map file (its size and first 4 KB), the level's loaded tag table (every
tag's name, index and group: the BSP's tables name tags by index), the
atlas texel format and the switches the build reads (world_snapshot_key_build),
and the derive code's own version (H2_WORLD_DERIVE_VERSION, below). A
mismatch, a short file or a wrong sum reads nothing and the map is gathered
as before. HALO_H2_WORLD_SNAPSHOT=0 turns it off (nothing read or
written); HALO_H2_WORLD_SNAPSHOT_MB caps the folder (384 MB by default:
other levels' snapshots go first when a write would pass it).

The write happens right after the read, on the game thread, before the
engine post-processes the loaded BSP (it writes into the room: a writer
thread saw the bytes move), as <level>.bspN.world in the levels folder
(d:\halo2\levels, beside the texture sidecars), through a .tmp renamed
into place - the first switch to a BSP pays the write once.
*/

/* (port/linux/src/posix_files.c) the folder's free room and its listing */
int posix_disk_space(const char *path, unsigned long *free_low, unsigned long *free_high, unsigned long *total_low,
	unsigned long *total_high);
void *posix_directory_open(const char *path);
int posix_directory_next(void *directory, char *name, unsigned long name_size);
void posix_directory_close(void *directory);

/* The version of the code that makes a BSP's world from the map - what the
snapshot holds: the room path's gather (h2_world_load.inc), the passes over
the world it calls in h2_level.c (the runs, the atlases' texels, the
collision written into the room) and the layouts written. BUMP IT whenever
a change makes that output differ by a byte. Until Nov 2026 the build's
compile time stood here instead (__DATE__ __TIME__ of h2_level.c), so every
new build gathered every BSP from the map again at its first visit - on the
first launch after an update with the rest of the level's load (the train
build, Cairo, Nov 11 2026); a build whose derive is unchanged now reads the
last one's snapshots. HALO_H2_WORLD_SNAPSHOT_VERIFY=1 proves a change left it
unchanged: a snapshot whose keys all match is not read but gathered from
the map again, and its sum compared with the file's ("h2 world: snapshot
verify: ... SAME" or "DIFFERENT: bump H2_WORLD_DERIVE_VERSION"). */
#define H2_WORLD_DERIVE_VERSION 1

enum
{
	WORLD_SNAPSHOT_MAGIC = 0x48325753UL,   /* 'H2WS' */
	WORLD_SNAPSHOT_VERSION = 1,
	WORLD_SNAPSHOT_CHUNK = 1024 * 1024,
	WORLD_SNAPSHOT_MB_DEFAULT = 384,
	WORLD_SNAPSHOT_FREE_MB_MIN = 256,
	WORLD_SNAPSHOT_MAP_HEAD = 4096
};

struct world_snapshot_header
{
	unsigned long magic, version, header_bytes;
	char level[64];
	unsigned long bsp;
	unsigned long room_address, room_size, room_used;
	unsigned long apart_size;
	unsigned long texel_bytes, atlas_side, atlas_count;
	unsigned long group_count, ranges_total;
	unsigned long key_map, key_tags, key_build;
	unsigned long payload_bytes, sum;
	unsigned long reserved[6];
};

struct world_snapshot_group
{
	char path[160];
	long atlas, vertex_count, triangle_count, range_count, breakable_surface_index;
	long takes_decals;
	/* offsets of its vertices, lightmap vertices and triangles: into the
	room (in_room, apart 0), into the block apart (apart 1), else copies
	held in the payload after the ranges (in_room 0: offsets into that part) */
	unsigned long vertices, lightmap_vertices, triangles;
	long in_room, apart;
	float centroid[3];
};

static struct
{
	/* what is written: the header (its sum filled as the payload goes),
	the groups' records and ranges (copies), and what is read in place */
	struct world_snapshot_header header;
	unsigned char *records;
	unsigned long records_bytes;
	char path[300], temporary[300];
	unsigned long long started;
	char level[64];
	long bsp;
	/* copies of runs not in the room (a level with its world's geometry
	apart from the tag cache keeps those in the apart block; a run whose
	buffers hold their own copies has let its vertices go: not kept) */
} world_snapshot_writer;

static unsigned long world_snapshot_fnv(unsigned long sum, void const *bytes, unsigned long count)
{
	unsigned char const *b = bytes;
	unsigned long q;

	for (q = 0; q < count; q++)
		sum = ((sum ^ b[q]) * 16777619UL) & 0xFFFFFFFFUL;
	return sum;
}

/* (HALO_H2_WORLD_SNAPSHOT_VERIFY=1) the snapshot set aside to be gathered
again and compared: its path, sum and payload's size */
static struct
{
	char path[300];
	unsigned long sum, payload_bytes;
	unsigned long same, different;
} world_snapshot_verify;

static boolean world_snapshot_verifying(void)
{
	char const *setting = getenv("HALO_H2_WORLD_SNAPSHOT_VERIFY");

	return setting && atoi(setting) != 0;
}

static boolean world_snapshot_enabled(void)
{
	char const *setting = getenv("HALO_H2_WORLD_SNAPSHOT");

	return !setting || atoi(setting) != 0;
}

static void world_snapshot_path(char const *level, long bsp, char *path, unsigned long size)
{
	snprintf(path, size, H2_LEVEL_FOLDER "\\%s.bsp%ld.world", level, bsp);
}

/* the map file's identity: its size and its first 4 KB (the MCC header
holds the map's build string and checksum) */
static unsigned long world_snapshot_key_map(char const *maps_directory, char const *name)
{
	char path[512];
	FILE *file;
	unsigned char head[WORLD_SNAPSHOT_MAP_HEAD];
	unsigned long sum = 2166136261UL, size_low = 0;
	long got;

	snprintf(path, sizeof(path), "%s%s.map", maps_directory, name);
	file = fopen(path, "rb");
	if (!file)
		return 0;
	got = (long)fread(head, 1, sizeof(head), file);
	if (fseek(file, 0, SEEK_END) == 0)
		size_low = (unsigned long)ftell(file);
	fclose(file);
	if (got <= 0)
		return 0;
	sum = world_snapshot_fnv(sum, head, (unsigned long)got);
	sum = world_snapshot_fnv(sum, &size_low, sizeof(size_low));
	return sum ? sum : 1;
}

/* the level's loaded tags: each one's group, index and name, in order (the
BSP's tables written by bsp_write name the level's tags by index: a level
made differently has another table) */
static unsigned long world_snapshot_key_tags(void)
{
	struct world_tag_instance const *instances = (struct world_tag_instance const *)global_tag_instances;
	unsigned long sum = 2166136261UL;
	long i;

	if (!instances)
		return 0;
	sum = world_snapshot_fnv(sum, &halo_loaded_tag_count, sizeof(halo_loaded_tag_count));
	for (i = 0; i < halo_loaded_tag_count; i++)
	{
		unsigned long group = (unsigned long)instances[i].group_tag, index = (unsigned long)instances[i].tag_index;

		sum = world_snapshot_fnv(sum, &group, sizeof(group));
		sum = world_snapshot_fnv(sum, &index, sizeof(index));
		if (instances[i].name)
			sum = world_snapshot_fnv(sum, instances[i].name, (unsigned long)strlen(instances[i].name) + 1);
	}
	return sum ? sum : 1;
}

/* what the build reads besides the map: the layouts this code writes, the
switches the gather looks at, and the derive code's version (a pass changed
makes new ones: H2_WORLD_DERIVE_VERSION) */
static unsigned long world_snapshot_key_build(unsigned long texel_bytes)
{
	static char const *const switches[] = { "HALO_H2_NO_SKY", "HALO_H2_SKY_IN_BSP", "HALO_H2_ONE_SKY", "HALO_H2_NO_WATER",
		"HALO_H2_WATER_HEIGHT", "HALO_H2_NATIVE_BSP", "HALO_H2_WORLD_FROM_MAP", "HALO_H2_ATLAS_TEXELS", "HALO_H2_GEOMETRY_KEPT",
		"HALO_H2_WORLD_INVENTORY", "HALO_H2_CINEMATIC_NO_SCALE" };
	unsigned long sum = 2166136261UL, v;
	unsigned long k;

	v = WORLD_SNAPSHOT_VERSION; sum = world_snapshot_fnv(sum, &v, sizeof(v));
	v = sizeof(struct h2_world_range); sum = world_snapshot_fnv(sum, &v, sizeof(v));
	v = sizeof(struct world_snapshot_group); sum = world_snapshot_fnv(sum, &v, sizeof(v));
	v = WORLD_ROOM_ALIGN; sum = world_snapshot_fnv(sum, &v, sizeof(v));
	v = LIGHTMAP_ATLAS; sum = world_snapshot_fnv(sum, &v, sizeof(v));
	v = texel_bytes; sum = world_snapshot_fnv(sum, &v, sizeof(v));
	v = (unsigned long)texture_cache_hardware_format_size(); sum = world_snapshot_fnv(sum, &v, sizeof(v));
	v = H2_WORLD_DERIVE_VERSION; sum = world_snapshot_fnv(sum, &v, sizeof(v));
	for (k = 0; k < sizeof(switches) / sizeof(switches[0]); k++)
	{
		char const *value = getenv(switches[k]);

		sum = world_snapshot_fnv(sum, switches[k], (unsigned long)strlen(switches[k]) + 1);
		sum = world_snapshot_fnv(sum, value ? value : "", (unsigned long)(value ? strlen(value) : 0) + 1);
	}
	return sum ? sum : 1;
}

/* ---------- the atlases' blocks and hardware bitmaps (world_atlas_close's,
for texels read from a file) */

static unsigned char *world_snapshot_atlas_block(short a, unsigned long texel_bytes)
{
	unsigned long block_bytes = (unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * texel_bytes + ATLAS_POOL_SLACK;
	unsigned char *block = world_atlas_pool_bytes[a] == block_bytes && !getenv("HALO_H2_WORLD_ATLAS_FRESH") ?
		world_atlas_pool[a] : NULL;

	if (block)
		world_atlas_pool_turn[a]++;
	else
	{
		block = platform_contiguous_side_alloc(block_bytes, 4096);
		if (!block)
			block = window_alloc(block_bytes);
		if (block)
		{
			world_atlas_pool[a] = block;
			world_atlas_pool_bytes[a] = block_bytes;
			world_atlas_pool_turn[a] = 0;
		}
	}
	return block ? block + (world_atlas_pool_turn[a] & 1) * ATLAS_POOL_SLACK : NULL;
}

static void world_snapshot_atlas_bitmap(short a, unsigned char *texels, unsigned long texel_bytes)
{
	struct bitmap_data *bitmap = &h2_world.atlas_bitmaps[a];

	memset(bitmap, 0, sizeof(*bitmap));
	bitmap->signature = 'bitm';
	bitmap->width = LIGHTMAP_ATLAS;
	bitmap->height = LIGHTMAP_ATLAS;
	bitmap->depth = 1;
	bitmap->type = 0;
	bitmap->format = texel_bytes == 4 ? 11 : 6;
	bitmap->flags = 1;
	bitmap->cache_block_index = NONE;
	bitmap->pixels_size = (long)((unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * texel_bytes);
	bitmap->base_address = texels;
	texture_cache_hardware_format_make(bitmap, h2_world.atlas_hardware + a * texture_cache_hardware_format_size());
	bitmap->hardware_format = h2_world.atlas_hardware + a * texture_cache_hardware_format_size();
	h2_world.atlas_texels[a] = texels;
}

/* ---------- reading one */

struct world_snapshot_reader
{
	FILE *file;
	unsigned long sum, left;
	boolean failed;
};

/* count bytes of the payload into to (summed) */
static boolean world_snapshot_read(struct world_snapshot_reader *r, void *to, unsigned long count)
{
	unsigned char *at = to;

	if (r->failed || count > r->left)
	{
		if (!r->failed)
			platform_log("h2 world: snapshot read: %lu bytes wanted, %lu left", count, r->left);
		return r->failed = TRUE, FALSE;
	}
	while (count)
	{
		unsigned long piece = MIN(count, WORLD_SNAPSHOT_CHUNK);
		unsigned long got = (unsigned long)fread(at, 1, piece, r->file);

		if (got != piece)
		{
			platform_log("h2 world: snapshot read: %lu of %lu bytes at %ld (%lu left)", got, piece, ftell(r->file), r->left);
			return r->failed = TRUE, FALSE;
		}
		r->sum = world_snapshot_fnv(r->sum, at, piece);
		at += piece;
		count -= piece;
		r->left -= piece;
	}
	return TRUE;
}

/* the same into memory a read() cannot be given (the atlases' side blocks
and the geometry block apart are the GPU's: the desktop's mapped buffers
refuse the kernel's copy - "0 of 1048576 bytes", runs/snapC3): through a
scratch block, copied in */
/* (world_snapshot_scratch_make) the scratch block's bytes */
static unsigned long world_snapshot_scratch_bytes = WORLD_SNAPSHOT_CHUNK;

/* the scratch block: 1 MB, else less - a switch in the play finds the C heap
with little left in one piece (the dev Vita's Cairo at Quality, Nov 11: 1.3
MB free, BSP 0's snapshot "no scratch block", so the old BSP was not loaded
again after the new one was refused); the bounce reads in its size, 64 KB
at the least */
static unsigned char *world_snapshot_scratch_make(void)
{
	unsigned long bytes;

	for (bytes = WORLD_SNAPSHOT_CHUNK; bytes >= WORLD_SNAPSHOT_CHUNK / 16; bytes /= 2)
	{
		unsigned char *scratch = malloc(bytes);

		if (scratch)
		{
			if (bytes != WORLD_SNAPSHOT_CHUNK)
				platform_log("h2 world: the snapshot's scratch block %lu KB (the C heap had no %d KB in one piece)", bytes / 1024,
					(int)(WORLD_SNAPSHOT_CHUNK / 1024));
			world_snapshot_scratch_bytes = bytes;
			return scratch;
		}
	}
	return NULL;
}

static boolean world_snapshot_read_bounce(struct world_snapshot_reader *r, void *to, unsigned long count, unsigned char *scratch)
{
	unsigned char *at = to;

	while (count)
	{
		unsigned long piece = MIN(count, world_snapshot_scratch_bytes);

		if (!world_snapshot_read(r, scratch, piece))
			return FALSE;
		memcpy(at, scratch, piece);
		at += piece;
		count -= piece;
	}
	return TRUE;
}

/* the BSP's world read from its snapshot into room; FALSE (nothing kept,
the world as world_last_free leaves it) when there is none that fits */
static boolean world_snapshot_restore(char const *maps_directory, char const *name, long bsp, unsigned char *room,
	unsigned long room_size)
{
	struct world_snapshot_header h;
	struct world_snapshot_reader r;
	char path[300];
	unsigned long texel_bytes = h2_lighting_atlas_32bit() ? 4 : 2;
	unsigned long key_map, key_tags, key_build;
	unsigned long long started = vita_host_time_us(), opened;
	struct world_snapshot_group *records = NULL;
	struct h2_world_range *ranges = NULL;
	unsigned long range_at = 0;
	unsigned char *scratch = NULL;
	long i;
	short a;
	char const *why = NULL;

	if (!world_snapshot_enabled() || !room || !maps_directory || !name)
		return FALSE;
	world_snapshot_path(name, bsp, path, sizeof(path));
	memset(&r, 0, sizeof(r));
	r.file = fopen(path, "rb");
	if (!r.file)
		return FALSE;
	setvbuf(r.file, NULL, _IONBF, 0);
	if (fread(&h, 1, sizeof(h), r.file) != sizeof(h) || h.magic != WORLD_SNAPSHOT_MAGIC || h.version != WORLD_SNAPSHOT_VERSION ||
		h.header_bytes != sizeof(h))
	{
		fclose(r.file);
		platform_log("h2 world: %s: not a snapshot of this version: gathered from the map", path);
		remove(path);
		return FALSE;
	}
	key_map = world_snapshot_key_map(maps_directory, name);
	key_tags = world_snapshot_key_tags();
	key_build = world_snapshot_key_build(texel_bytes);
	if (strcmp(h.level, name) || h.bsp != (unsigned long)bsp || h.room_address != (unsigned long)room || h.room_size != room_size)
		why = "another level, BSP or room";
	else if (h.key_map != key_map || !key_map)
		why = "the map changed";
	else if (h.key_tags != key_tags || !key_tags)
		why = "the level's tags changed";
	else if (h.key_build != key_build)
		why = "another build of the game";
	else if (h.room_used > room_size || h.texel_bytes != texel_bytes || h.atlas_side != LIGHTMAP_ATLAS ||
		h.atlas_count > MAXIMUM_ATLASES || h.group_count > 100000 || h.ranges_total > 10000000)
		why = "its sizes";
	if (why)
	{
		fclose(r.file);
		platform_log("h2 world: %s: the snapshot does not fit (%s): gathered from the map, the file replaced", path, why);
		remove(path);
		return FALSE;
	}
	if (world_snapshot_verifying())
	{
		fclose(r.file);
		snprintf(world_snapshot_verify.path, sizeof(world_snapshot_verify.path), "%s", path);
		world_snapshot_verify.sum = h.sum;
		world_snapshot_verify.payload_bytes = h.payload_bytes;
		/* (=2: the file kept beside it as .verify, to be compared byte by byte) */
		if (atoi(getenv("HALO_H2_WORLD_SNAPSHOT_VERIFY")) == 2)
		{
			char kept[320];

			snprintf(kept, sizeof(kept), "%s.verify", path);
			remove(kept);
			rename(path, kept);
		}
		platform_log("h2 world: %s: its keys match (derive version %d): gathered from the map again to verify it (sum %08lx)",
			path, H2_WORLD_DERIVE_VERSION, h.sum);
		return FALSE;
	}
	opened = vita_host_time_us();
	r.sum = 2166136261UL;
	r.left = h.payload_bytes;
	/* the runs' records and their ranges */
	records = calloc(h.group_count + 1, sizeof(*records));
	ranges = h.ranges_total ? calloc(h.ranges_total, sizeof(*ranges)) : NULL;
	h2_world.groups = calloc(h.group_count + 1, sizeof(*h2_world.groups));
	h2_world.atlas_hardware = calloc(MAXIMUM_ATLASES, texture_cache_hardware_format_size());
	if (!records || (h.ranges_total && !ranges) || !h2_world.groups || !h2_world.atlas_hardware)
		why = "out of memory for its records";
	if (!why && (!world_snapshot_read(&r, records, h.group_count * sizeof(*records)) ||
		(h.ranges_total && !world_snapshot_read(&r, ranges, h.ranges_total * sizeof(*ranges)))))
		why = "its records could not be read";
	/* the runs' records made the world's (their pointers into the room set
	once it is read; a run whose buffers hold copies has its bytes in the
	payload here, each part a block of the window's, as the build's were) */
	for (i = 0; !why && i < (long)h.group_count; i++)
	{
		struct world_snapshot_group const *s = &records[i];
		struct h2_world_group *g = &h2_world.groups[i];

		snprintf(g->path, sizeof(g->path), "%s", s->path);
		g->shader_tag = NONE;
		g->atlas = (short)s->atlas;
		g->vertex_count = s->vertex_count;
		g->triangle_count = s->triangle_count;
		g->breakable_surface_index = (short)s->breakable_surface_index;
		g->takes_decals = (signed char)s->takes_decals;
		g->in_room = s->in_room != 0;
		memcpy(g->centroid, s->centroid, sizeof(g->centroid));
		if (s->vertex_count <= 0 || s->triangle_count <= 0 || s->range_count < 0 || range_at + (unsigned long)s->range_count > h.ranges_total)
		{
			why = "a run's counts";
			break;
		}
		g->range_count = s->range_count;
		if (s->range_count)
		{
			g->ranges = malloc((unsigned long)s->range_count * sizeof(*g->ranges));
			if (!g->ranges)
			{
				why = "out of memory for a run's ranges";
				break;
			}
			memcpy(g->ranges, ranges + range_at, (unsigned long)s->range_count * sizeof(*g->ranges));
			range_at += (unsigned long)s->range_count;
		}
		h2_world.group_count++;
		if (!g->in_room)
		{
			g->vertices = window_alloc((unsigned long)g->vertex_count * 32);
			g->lightmap_vertices = window_alloc((unsigned long)g->vertex_count * 8);
			g->triangles = malloc((unsigned long)g->triangle_count * 6);
			if (!g->vertices || !g->lightmap_vertices || !g->triangles)
				why = "no room for a copied run";
			else if (!world_snapshot_read(&r, g->vertices, (unsigned long)g->vertex_count * 32) ||
				!world_snapshot_read(&r, g->lightmap_vertices, (unsigned long)g->vertex_count * 8) ||
				!world_snapshot_read(&r, g->triangles, (unsigned long)g->triangle_count * 6))
				why = "a copied run could not be read";
		}
	}
	/* the room itself, and the block apart from it - the room through the
	scratch block too: it is GPU-mapped memory, and a read() straight into
	it left Vita3K's renderer with the last BSP's bytes (its write tracking
	sees the CPU's copies, not the kernel's: the bridge drew with its walls
	black, cut10-runs/v3k/runs/testA); the hardware reads what the CPU wrote
	either way */
	if (!why && !scratch && !(scratch = world_snapshot_scratch_make()))
		why = "no scratch block";
	if (!why && !world_snapshot_read_bounce(&r, room, h.room_used, scratch))
		why = "the room could not be read";
	if (!why && h.apart_size)
	{
		h2_world.apart = halo_world_geometry_acquire(h.apart_size);
		if (!h2_world.apart)
			why = "no memory for its geometry apart from the tag cache";
		else
		{
			h2_world.apart_size = h.apart_size;
			if (!scratch && !(scratch = world_snapshot_scratch_make()))
				why = "no scratch block";
			else if (!world_snapshot_read_bounce(&r, h2_world.apart, h.apart_size, scratch))
				why = "its geometry apart from the tag cache could not be read";
		}
	}
	for (i = 0; !why && i < h2_world.group_count; i++)
	{
		struct world_snapshot_group const *s = &records[i];
		struct h2_world_group *g = &h2_world.groups[i];
		unsigned char *base = s->apart ? h2_world.apart : room;
		unsigned long extent = s->apart ? h.apart_size : h.room_used;

		if (!g->in_room)
			continue;
		if ((s->apart && !h2_world.apart) || s->vertices + (unsigned long)g->vertex_count * 32 > extent ||
			s->lightmap_vertices + (unsigned long)g->vertex_count * 8 > extent || s->triangles + (unsigned long)g->triangle_count * 6 > extent)
		{
			platform_log("h2 world: snapshot run %ld: vertices %lu (%ld x 32), lightmap %lu, triangles %lu (%ld x 6) against %lu (apart %ld)",
				i, s->vertices, g->vertex_count, s->lightmap_vertices, s->triangles, g->triangle_count, extent, s->apart);
			why = "a run outside the room";
			break;
		}
		g->vertices = base + s->vertices;
		g->lightmap_vertices = base + s->lightmap_vertices;
		g->triangles = (unsigned short *)(base + s->triangles);
	}
	/* the atlases' texels, into their blocks */
	for (a = 0; !why && a < (short)h.atlas_count; a++)
	{
		unsigned char *texels = world_snapshot_atlas_block(a, texel_bytes);

		if (!texels)
			why = "no side block for an atlas";
		else if (!scratch && !(scratch = world_snapshot_scratch_make()))
			why = "no scratch block";
		else if (!world_snapshot_read_bounce(&r, texels, (unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * texel_bytes, scratch))
			why = "an atlas could not be read";
		else
			world_snapshot_atlas_bitmap(a, texels, texel_bytes);
		h2_world.atlas_count = (short)(a + 1);
	}
	if (!why && (r.left != 0 || r.sum != h.sum))
		why = r.left ? "bytes left over" : "its sum is wrong";
	fclose(r.file);
	free(records);
	free(ranges);
	free(scratch);
	if (why)
	{
		platform_log("h2 world: %s: the snapshot could not be read (%s): gathered from the map, the file replaced", path, why);
		/* (what was taken, let go: the groups and their copies, the atlases'
		hardware, the block apart; the atlases' blocks stay pooled) */
		world_last_free();
		remove(path);
		return FALSE;
	}
	h2_world.room_size = room_size;
	h2_world.room_used = h.room_used;
	h2_world.room_contiguous = platform_is_contiguous(room);
	h2_world.buffers = calloc(2 * (h.group_count + 1), sizeof(struct vertex_buffer));
	if (!h2_world.buffers)
	{
		platform_log("h2 world: %s: out of memory for the snapshot's buffers: gathered from the map", path);
		world_last_free();
		return FALSE;
	}
	for (i = 0; i < h2_world.group_count; i++)
		h2_world.groups[i].buffers = &h2_world.buffers[2 * i];
	world_buffers_make();
	h2_world.loaded = TRUE;
	world_boxes_make();
	world_weather_tags_find();
	h2_world.us_total = vita_host_time_us() - started;
	h2_world.us_take = vita_host_time_us() - opened;
	platform_log("h2 world: %s: its world read from its snapshot - %ld runs, %d lightmap atlases (structure BSP %ld) in %lu ms "
		"(%lu KB: the keys %lu ms, the file %lu ms); the room %lu of %lu KB%s", name, h2_world.group_count, (int)h2_world.atlas_count,
		bsp, (unsigned long)(h2_world.us_total / 1000), (h.payload_bytes + (unsigned long)sizeof(h)) / 1024,
		(unsigned long)((opened - started) / 1000), (unsigned long)(h2_world.us_take / 1000), h2_world.room_used / 1024,
		h2_world.room_size / 1024, h2_world.room_contiguous ? ", read by the GPU where it is" : ", NOT in the contiguous window: the triangles copied");
	return TRUE;
}

/* ---------- writing one */

/* the folder's snapshots other than this level's, oldest first by name
order (no stamps on every platform): dropped while the folder would pass
its cap with this one added */
static void world_snapshot_trim(char const *level, unsigned long adding)
{
	char folder_xbox[64], folder[512], name[256], path[600];
	void *directory;
	unsigned long total = 0, cap_mb = WORLD_SNAPSHOT_MB_DEFAULT;
	char const *setting = getenv("HALO_H2_WORLD_SNAPSHOT_MB");
	long passes;

	if (setting && atol(setting) > 0)
		cap_mb = (unsigned long)atol(setting);
	snprintf(folder_xbox, sizeof(folder_xbox), "%s", H2_LEVEL_FOLDER);
	platform_translate_path(folder_xbox, folder, sizeof(folder));
	for (passes = 0; passes < 2; passes++)
	{
		directory = posix_directory_open(folder);
		if (!directory)
			return;
		total = 0;
		while (posix_directory_next(directory, name, sizeof(name)))
		{
			size_t length = strlen(name);
			FILE *file;
			unsigned long bytes = 0;

			if (length < 7 || strcmp(name + length - 6, ".world"))
				continue;
			snprintf(path, sizeof(path), "%s/%s", folder, name);
			file = fopen(path, "rb");
			if (file)
			{
				if (fseek(file, 0, SEEK_END) == 0)
					bytes = (unsigned long)ftell(file);
				fclose(file);
			}
			if (passes == 1 && total + adding > cap_mb * 1024UL * 1024UL && strncmp(name, level, strlen(level)))
			{
				if (remove(path) == 0)
					platform_log("h2 world: snapshot %s dropped (the folder past %lu MB)", name, cap_mb);
				continue;
			}
			total += bytes;
		}
		posix_directory_close(directory);
		if (total + adding <= cap_mb * 1024UL * 1024UL)
			return;
	}
}

static boolean world_snapshot_write_bytes(FILE *file, void const *bytes, unsigned long count, unsigned long *sum)
{
	unsigned char const *at = bytes;

	while (count)
	{
		unsigned long piece = MIN(count, WORLD_SNAPSHOT_CHUNK);

		if (fwrite(at, 1, piece, file) != piece)
			return FALSE;
		*sum = world_snapshot_fnv(*sum, at, piece);
		at += piece;
		count -= piece;
	}
	return TRUE;
}

/* the world just read from the map (room path) written now, before the
engine's own post-processing of the loaded BSP touches the room (it does:
the bytes moved under a writer thread, runs/snapB) - what the file holds is
the build's output to the byte, and the restored room is post-processed as
a fresh build's is. One sequential write; the first switch to a BSP pays it */
static void world_snapshot_write(void)
{
	struct world_snapshot_header *h = &world_snapshot_writer.header;
	unsigned char const *room = (unsigned char const *)h->room_address;
	FILE *file;
	unsigned long sum = 2166136261UL;
	boolean ok;
	short a;

	h2_level_folder_create();
	world_snapshot_trim(world_snapshot_writer.level, h->payload_bytes + sizeof(*h));
	file = fopen(world_snapshot_writer.temporary, "wb");
	if (!file)
	{
		platform_log("h2 world: snapshot %s: cannot be written", world_snapshot_writer.temporary);
		return;
	}
	setvbuf(file, NULL, _IONBF, 0);
	/* (the header first with no sum, written again at the end) */
	ok = fwrite(h, 1, sizeof(*h), file) == sizeof(*h);
	ok = ok && world_snapshot_write_bytes(file, world_snapshot_writer.records, world_snapshot_writer.records_bytes, &sum);
	ok = ok && world_snapshot_write_bytes(file, room, h->room_used, &sum);
	if (ok && h2_world.apart)
		ok = world_snapshot_write_bytes(file, h2_world.apart, h2_world.apart_size, &sum);
	for (a = 0; ok && a < h2_world.atlas_count; a++)
		ok = world_snapshot_write_bytes(file, h2_world.atlas_texels[a], (unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * h->texel_bytes, &sum);
	if (ok)
	{
		h->sum = sum;
		ok = fseek(file, 0, SEEK_SET) == 0 && fwrite(h, 1, sizeof(*h), file) == sizeof(*h);
	}
	if (fclose(file) != 0)
		ok = FALSE;
	if (ok)
	{
		remove(world_snapshot_writer.path);
		ok = rename(world_snapshot_writer.temporary, world_snapshot_writer.path) == 0;
	}
	if (!ok)
	{
		remove(world_snapshot_writer.temporary);
		platform_log("h2 world: snapshot %s: not written", world_snapshot_writer.path);
	}
	else
		platform_log("h2 world: snapshot %s written: %lu KB in %lu ms, sum %08lx (the next switch to structure BSP %ld reads it)",
			world_snapshot_writer.path, (h->payload_bytes + (unsigned long)sizeof(*h)) / 1024,
			(unsigned long)((vita_host_time_us() - world_snapshot_writer.started) / 1000), sum, world_snapshot_writer.bsp);
	if (ok && world_snapshot_verify.path[0] && !strcmp(world_snapshot_verify.path, world_snapshot_writer.path))
	{
		boolean same = world_snapshot_verify.sum == sum && world_snapshot_verify.payload_bytes == h->payload_bytes;

		if (same)
			world_snapshot_verify.same++;
		else
			world_snapshot_verify.different++;
		platform_log("h2 world: snapshot verify: %s: %s (the file's sum %08lx, %lu bytes; gathered now %08lx, %lu bytes)%s; "
			"%lu same, %lu different so far", world_snapshot_writer.path, same ? "SAME" : "DIFFERENT", world_snapshot_verify.sum,
			world_snapshot_verify.payload_bytes, sum, h->payload_bytes,
			same ? "" : ": the derive's output changed - bump H2_WORLD_DERIVE_VERSION", world_snapshot_verify.same,
			world_snapshot_verify.different);
		world_snapshot_verify.path[0] = 0;
	}
}

/* (nothing left to wait for: the write is synchronous; kept as the hook
world_last_free calls) */
static void world_snapshot_write_finish(void)
{
}

/* the world just read from the map (room path) written as a snapshot, on a
thread of its own */
static void world_snapshot_write_begin(char const *maps_directory, char const *name, long bsp, unsigned char *room,
	unsigned long room_size)
{
	struct world_snapshot_header *h = &world_snapshot_writer.header;
	unsigned long texel_bytes = h2_lighting_atlas_32bit() ? 4 : 2;
	unsigned long ranges_total = 0, copies_bytes = 0, at, range_at, copy_at;
	struct world_snapshot_group *records;
	struct h2_world_range *ranges;
	unsigned char *copies;
	long i;
	short a;

	if (!world_snapshot_enabled() || h2_world.group_count < 1 || !room || !maps_directory || !name)
		return;
	for (a = 0; a < h2_world.atlas_count; a++)
		if (!h2_world.atlas_texels[a])
			return;
	/* (room on the card: not below the floor) */
	{
		char folder_xbox[64], folder[512];
		unsigned long free_low = 0, free_high = 0, total_low = 0, total_high = 0;

		snprintf(folder_xbox, sizeof(folder_xbox), "%s", H2_LEVEL_FOLDER);
		platform_translate_path(folder_xbox, folder, sizeof(folder));
		if (posix_disk_space(folder, &free_low, &free_high, &total_low, &total_high) == 0 && !free_high &&
			free_low < (unsigned long)WORLD_SNAPSHOT_FREE_MB_MIN * 1024UL * 1024UL)
		{
			platform_log("h2 world: no snapshot of structure BSP %ld: %lu MB free on the card", bsp, free_low >> 20);
			return;
		}
	}
	for (i = 0; i < h2_world.group_count; i++)
	{
		struct h2_world_group const *g = &h2_world.groups[i];

		ranges_total += (unsigned long)g->range_count;
		if (!g->in_room)
		{
			/* (a run whose buffers hold their own copies has let its
			vertices go: nothing to keep of it) */
			if (!g->vertices || !g->lightmap_vertices || !g->triangles)
				return;
			copies_bytes += (unsigned long)g->vertex_count * 32 + (unsigned long)g->vertex_count * 8 + (unsigned long)g->triangle_count * 6;
		}
	}
	world_snapshot_writer.records_bytes = (unsigned long)h2_world.group_count * sizeof(*records) + ranges_total * sizeof(*ranges) + copies_bytes;
	world_snapshot_writer.records = calloc(1, world_snapshot_writer.records_bytes + 16);
	if (!world_snapshot_writer.records)
		return;
	records = (struct world_snapshot_group *)world_snapshot_writer.records;
	ranges = (struct h2_world_range *)(world_snapshot_writer.records + (unsigned long)h2_world.group_count * sizeof(*records));
	copies = world_snapshot_writer.records + (unsigned long)h2_world.group_count * sizeof(*records) + ranges_total * sizeof(*ranges);
	range_at = 0;
	copy_at = 0;
	for (i = 0; i < h2_world.group_count; i++)
	{
		struct h2_world_group const *g = &h2_world.groups[i];
		struct world_snapshot_group *s = &records[i];
		unsigned char const *base = g->in_room ? (h2_world.apart && halo_world_geometry_holds(g->triangles, (unsigned long)g->triangle_count * 6) ?
			h2_world.apart : room) : NULL;

		snprintf(s->path, sizeof(s->path), "%s", g->path);
		s->atlas = g->atlas;
		s->vertex_count = g->vertex_count;
		s->triangle_count = g->triangle_count;
		s->range_count = g->range_count;
		s->breakable_surface_index = g->breakable_surface_index;
		s->takes_decals = g->takes_decals;
		s->in_room = g->in_room;
		s->apart = base == h2_world.apart && h2_world.apart ? 1 : 0;
		memcpy(s->centroid, g->centroid, sizeof(s->centroid));
		if (base)
		{
			s->vertices = (unsigned long)(g->vertices - base);
			s->lightmap_vertices = (unsigned long)(g->lightmap_vertices - base);
			s->triangles = (unsigned long)((unsigned char const *)g->triangles - base);
		}
		else
		{
			at = copy_at;
			memcpy(copies + at, g->vertices, (unsigned long)g->vertex_count * 32);
			s->vertices = at;
			at += (unsigned long)g->vertex_count * 32;
			memcpy(copies + at, g->lightmap_vertices, (unsigned long)g->vertex_count * 8);
			s->lightmap_vertices = at;
			at += (unsigned long)g->vertex_count * 8;
			memcpy(copies + at, g->triangles, (unsigned long)g->triangle_count * 6);
			s->triangles = at;
			at += (unsigned long)g->triangle_count * 6;
			copy_at = at;
		}
		if (g->range_count && g->ranges)
			memcpy(ranges + range_at, g->ranges, (unsigned long)g->range_count * sizeof(*ranges));
		range_at += (unsigned long)g->range_count;
	}
	memset(h, 0, sizeof(*h));
	h->magic = WORLD_SNAPSHOT_MAGIC;
	h->version = WORLD_SNAPSHOT_VERSION;
	h->header_bytes = sizeof(*h);
	snprintf(h->level, sizeof(h->level), "%s", name);
	h->bsp = (unsigned long)bsp;
	h->room_address = (unsigned long)room;
	h->room_size = room_size;
	h->room_used = h2_world.room_used;
	h->apart_size = h2_world.apart ? h2_world.apart_size : 0;
	h->texel_bytes = texel_bytes;
	h->atlas_side = LIGHTMAP_ATLAS;
	h->atlas_count = (unsigned long)h2_world.atlas_count;
	h->group_count = (unsigned long)h2_world.group_count;
	h->ranges_total = ranges_total;
	h->key_map = world_snapshot_key_map(maps_directory, name);
	h->key_tags = world_snapshot_key_tags();
	h->key_build = world_snapshot_key_build(texel_bytes);
	h->payload_bytes = world_snapshot_writer.records_bytes + h->room_used + h->apart_size +
		h->atlas_count * (unsigned long)LIGHTMAP_ATLAS * LIGHTMAP_ATLAS * texel_bytes;
	if (!h->key_map || !h->key_tags)
	{
		free(world_snapshot_writer.records);
		world_snapshot_writer.records = NULL;
		return;
	}
	world_snapshot_path(name, bsp, world_snapshot_writer.path, sizeof(world_snapshot_writer.path));
	snprintf(world_snapshot_writer.temporary, sizeof(world_snapshot_writer.temporary), "%s.tmp", world_snapshot_writer.path);
	snprintf(world_snapshot_writer.level, sizeof(world_snapshot_writer.level), "%s", name);
	world_snapshot_writer.bsp = bsp;
	world_snapshot_writer.started = vita_host_time_us();
	world_snapshot_write();
	free(world_snapshot_writer.records);
	world_snapshot_writer.records = NULL;
}
