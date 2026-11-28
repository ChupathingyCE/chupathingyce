/*
H2_MODELS_LOAD.INC (included at the end of h2_level.c)

(port, feat/h2-models-at-load) A Halo 2 level's models and animations -
render models, collision models, animation graphs - made from its MCC map as
the level loads, by the level builder's own code, so that nothing of them is
in the made level and a fix to them needs no new level.

The bipeds' (h2_biped.c: the players' and the campaign's characters'):
- HALO_H2_MODELS_AT_LOAD=1 as a level is made: their mode, coll and antr tags
  are left out of it (h2_bipeds_write's no_models; their instances' data 0),
  their range of the table recorded for the tags made at load (h2_load_tags.h,
  maker 'h2md', its room what a models_only pass took and a quarter more);
  as it loads, h2_models_load_make makes them again after the map's tags
  (h2_bipeds_gather, h2_bipeds_write's models_only) and places them.
- HALO_H2_MODELS_AT_LOAD=1 as a level made with them loads: they are made
  again into memory of their own and compared, field by field, with the
  level's (h2_models_map_loaded's log line).

The host: offsets below scratch_at are the level's tags (the loaded tag
cache's, or as a level is made its region), offsets from it the models' own
memory, whose addresses begin at scratch_at past the tags' base. A level's
tags are read through its table (instances: 32 bytes, group at 0, name at
16, data at 20), as the tags made at load are made before the game's own
table is (cache_files.c).
*/

/* (as a level is made) HALO_H2_MODELS_AT_LOAD=1: its models made at load */
H2_ALIGN static boolean h2_models_at_load_build(void)
{
	return h2_level_switch("HALO_H2_MODELS_AT_LOAD", H2_LEVEL_MODELS_AT_LOAD_DEFAULT);
}

/* (the level loading now had its bipeds' models made at load: nothing to
compare them with) */
static boolean models_load_made = FALSE;

struct models_load_context
{
	struct build *build;            /* (the converters': a build of the map's cache alone at load) */
	struct region scratch;          /* the models made */
	/* the region written: the scratch, or (whole: the weapons' and vehicles'
	tags, whose graphs copy the kit's) one spanning the tags from their base -
	the tag cache itself as a level loads (fixed: never moved), the level's
	own tags as it is made (measured, then cut back), a copy as they are
	checked; its offsets are then the host's */
	struct region *r;
	boolean whole;
	unsigned char *base;            /* (load) the loaded tag cache's */
	struct region *main;            /* (made) the level's tag data */
	unsigned char *instances;       /* (load) the level's table */
	unsigned long main_instances;   /* (made) its table, an offset of main */
	unsigned long scratch_at;       /* the host's offsets from here are the scratch's */
	unsigned long *roots;           /* a made root (host offset) a tag index, 0 none */
	long tag_count;
	/* (made, the weapons' models) the level's shaders' numbering */
	long shader_first, soso_first;
	short const *shader_soso;
	/* (the weapons' models) their buffer table (a host offset) and entries */
	unsigned long vertex_table;
	long buffers;
};

H2_ALIGN static unsigned char *models_load_at(void *context, unsigned long offset)
{
	struct models_load_context *c = context;

	if (c->whole)
		return region_at(c->r, offset);
	if (offset >= c->scratch_at)
		return region_at(&c->scratch, offset - c->scratch_at);
	return c->main ? region_at(c->main, offset) : c->base + offset;
}

/* (the region written) an offset of it as the host's */
H2_ALIGN static unsigned long models_load_root(struct models_load_context const *c, unsigned long offset)
{
	return c->whole ? offset : c->scratch_at + offset;
}

/* (the level's table) a tag's instance; an address in the tags their bytes
(NULL none), and their offset */
H2_ALIGN static unsigned char const *models_load_instance(struct models_load_context const *c, long index)
{
	return c->main ? region_at(c->main, c->main_instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)index) :
		c->instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)index;
}

H2_ALIGN static unsigned long models_load_offset_of(struct models_load_context const *c, unsigned long address)
{
	unsigned long link = c->main ? c->main->link : (unsigned long)c->base;

	return address >= link && address - link < c->scratch_at ? address - link : 0;
}

H2_ALIGN static char const *models_load_name(struct models_load_context const *c, long index)
{
	unsigned long at = models_load_offset_of(c, read32(models_load_instance(c, index) + 16));

	return at ? (char const *)models_load_at((void *)c, at) : NULL;
}

H2_ALIGN static unsigned long models_load_group(struct models_load_context const *c, long index)
{
	return read32(models_load_instance(c, index));
}
/* (as the models are written at load) the references' names the level's
table's (region_reference writes each reference's name's address from
reference_names: the build's, if one was made in this session, else none) */
static unsigned long *models_load_saved_names;
static long models_load_saved_name_count;

H2_ALIGN static void models_load_names_begin(struct models_load_context const *c)
{
	unsigned long *names = c->main ? NULL : calloc((size_t)(c->tag_count + 1), sizeof(*names));
	long i;

	models_load_saved_names = reference_names;
	models_load_saved_name_count = reference_name_count;
	if (c->main)
		return;
	for (i = 0; names && i < c->tag_count; i++)
		names[i] = read32(models_load_instance(c, i) + 16);
	reference_names = names;
	reference_name_count = names ? c->tag_count : 0;
}

H2_ALIGN static void models_load_names_end(struct models_load_context const *c)
{
	if (!c->main)
		free(reference_names);
	reference_names = models_load_saved_names;
	reference_name_count = models_load_saved_name_count;
}

H2_ALIGN static unsigned long models_load_alloc(void *context, unsigned long size)
{
	struct models_load_context *c = context;

	return c->scratch_at + region_alloc(&c->scratch, size);
}

H2_ALIGN static unsigned long models_load_block(void *context, unsigned long field, long count, unsigned long size)
{
	struct models_load_context *c = context;

	if (field < c->scratch_at)
	{
		c->scratch.b.failed = TRUE;
		return 0;
	}
	return c->scratch_at + region_block(&c->scratch, field - c->scratch_at, count, size);
}

H2_ALIGN static void models_load_data(void *context, unsigned long field, void const *bytes, unsigned long size)
{
	struct models_load_context *c = context;

	if (field < c->scratch_at)
	{
		c->scratch.b.failed = TRUE;
		return;
	}
	region_data(&c->scratch, field - c->scratch_at, bytes, size);
}

H2_ALIGN static void models_load_reference(void *context, unsigned long field, unsigned long group, unsigned long id)
{
	struct models_load_context *c = context;

	if (field < c->scratch_at)
	{
		c->scratch.b.failed = TRUE;
		return;
	}
	region_reference(&c->scratch, field - c->scratch_at, group, id);
}

H2_ALIGN static boolean models_load_failed(void *context)
{
	return ((struct models_load_context *)context)->scratch.b.failed;
}

H2_ALIGN static unsigned long models_load_tag_id(void *context, long index)
{
	(void)context;
	return tag_id(index);
}

/* (a tag's root: the one made here, else the level's - its offset) */
H2_ALIGN static unsigned long models_load_root_of(void *context, long index)
{
	struct models_load_context *c = context;

	if (index < 0 || index >= c->tag_count)
		return 0;
	if (c->roots[index])
		return c->roots[index];
	return models_load_offset_of(c, read32(models_load_instance(c, index) + 20));
}

H2_ALIGN static void models_load_set_root(void *context, long index, unsigned long offset)
{
	struct models_load_context *c = context;

	if (index >= 0 && index < c->tag_count)
		c->roots[index] = offset;
}

/* (the level's tag of a group and name: the first, as the kit's are first) */
H2_ALIGN static long models_load_find(struct models_load_context const *c, char const *name, unsigned long group)
{
	long index;

	for (index = 0; index < c->tag_count; index++)
	{
		char const *n;

		if ((!group || models_load_group(c, index) == group) && (n = models_load_name(c, index)) != NULL && !strcmp(n, name))
			return index;
	}
	return NONE;
}

H2_ALIGN static long models_load_kit_find(void *context, unsigned long group, char const *name)
{
	return models_load_find(context, name, group);
}

/* (gathering) the bitmaps a model's shaders draw: none made here, only
whether one would be (the shaders' tags are the level's) - a bitmap tag's
first image is drawn as bitmap_for takes it, a made one always is */
H2_ALIGN static long models_load_bitmap_for(void *context, long h2_tag)
{
	struct bitmap described;

	return bitmap_describe(((struct models_load_context *)context)->build, h2_tag, &described) ? 0 : NONE;
}

H2_ALIGN static long models_load_bitmap_made(void *context, char const *path, unsigned short width, unsigned short height,
	unsigned short format, unsigned short mipmaps, unsigned char *pixels, unsigned long size)
{
	(void)context; (void)path; (void)width; (void)height; (void)format; (void)mipmaps; (void)size;
	free(pixels);
	return 0;
}

/* (a Jackal's shield: the level's own plasma shader of its Halo 2 one -
shader_index's, a plasma pass as the builder takes it -, found among the
level's tags by its path) */
H2_ALIGN static short models_load_level_shader(void *context, long h2_shader)
{
	struct models_load_context *c = context;
	short shader = shader_index(c->build, h2_shader);
	long index;

	if (shader == NONE || !c->build->shaders[shader].plasma_pass)
		return NONE;
	index = models_load_find(c, c->build->shaders[shader].path, 0);
	if (getenv("HALO_H2_MODELS_AT_LOAD_LOG"))
		platform_log("h2 models at load: level shader %s: tag %ld", c->build->shaders[shader].path, index);
	return index != NONE && index < 0x7FFF ? (short)index : NONE;
}

H2_ALIGN static unsigned long models_load_level_shader_id(void *context, short level_shader)
{
	(void)context;
	return tag_id(level_shader);
}

H2_ALIGN static boolean models_load_collision_convert(void *context, unsigned long field, unsigned long h2, short material)
{
	struct models_load_context *c = context;

	if (field < c->scratch_at)
		return FALSE;
	return collision_convert(c->build, &c->scratch, field - c->scratch_at, h2, material);
}

/* (at load) the gathers' cache trimmed of the inflated data it keeps, past
the world read's budget (h2_world_load.inc's: 2 MB on a small C heap, 6
on the PC's) - the kept cache a load's opens are served would otherwise
grow by a segment a tag read, the whole gather long */
H2_ALIGN static unsigned long models_load_trim_bytes(void)
{
	unsigned long in_use = 0, capacity = 0;

	platform_heap_usage(&in_use, &capacity);
	return capacity && capacity <= 64UL * 1024 * 1024 ? 2UL * 1024 * 1024 : 6UL * 1024 * 1024;
}

H2_ALIGN static void models_load_trim(void *context)
{
	struct models_load_context *c = context;

	/* (as a level is made its own build trims at its own points) */
	if (!c->main && c->build->gather_trim_bytes)
		h2_cache_trim_past(c->build->cache, c->build->gather_trim_bytes);
}

H2_ALIGN static void models_load_host_make(struct h2_biped_host *host, struct models_load_context *c)
{
	memset(host, 0, sizeof(*host));
	host->context = c;
	host->cache = c->build->cache;
	host->trim = models_load_trim;
	host->bitmap_for = models_load_bitmap_for;
	host->bitmap_made = models_load_bitmap_made;
	host->alloc = models_load_alloc;
	host->at = models_load_at;
	host->block = models_load_block;
	host->data = models_load_data;
	host->reference = models_load_reference;
	host->failed = models_load_failed;
	host->tag_id = models_load_tag_id;
	host->root_of = models_load_root_of;
	host->set_root = models_load_set_root;
	host->kit_find = models_load_kit_find;
	host->level_shader = models_load_level_shader;
	host->level_shader_id = models_load_level_shader_id;
	host->collision_convert = models_load_collision_convert;
	/* (as a level is made, its parts' vertices may be in the build's spill
	file: read back as they are written) */
	host->spill_get = biped_host_spill_get;
	host->link = c->main ? c->main->link : (unsigned long)c->base;
	host->models_only = TRUE;
}

/* ---------- the effects' references in the collision models made at load

The effects pass (h2_effects.c, after the bipeds' and vehicles' tags are
written) writes effect references into their collision models: a biped's
shield damaged, depleted and recharging effects and its damage regions'
destroyed effects, a vehicle's body damaged and depleted effects. A
collision model made at load would lose them, so as a level is made each
such collision model is written as a stub (its root, materials and regions:
what the pass reads and writes), the pass runs, every effect reference the
stubs then hold is recorded in the level's record file beside it
(<level>.h2rec in H2_LEVEL_FOLDER - not a tag: one tag more moves every
tag's data by its instance and name, and a script global holding an
address would differ -: 'h2er', the level's tag count, a count, then 12
bytes each: the tag, the block field of its root holding it (0xFFFF: the
root itself), the byte offset from the block's elements or the root, the
effect's tag id as written - a reference copied from the kit's keeps the
kit's own id word) and the stubs' instances are emptied; a maker writes
them again into the collision model it makes. */

enum
{
	MODELS_RECORD_MAGIC = 0x68326572,  /* 'h2er' */
	MODELS_RECORD_ENTRY = 12,
	MODELS_RECORD_ROOT = 0xFFFF,
};

struct models_record
{
	unsigned char *bytes;
	unsigned long size, capacity;
	long count;
};

H2_ALIGN static boolean models_record_add(struct models_record *rec, long tag, unsigned long block, unsigned long offset, unsigned long effect_id)
{
	unsigned char *e;

	if (rec->size + MODELS_RECORD_ENTRY > rec->capacity)
	{
		unsigned long capacity = rec->capacity ? rec->capacity * 2 : 64 * MODELS_RECORD_ENTRY;
		unsigned char *grown = realloc(rec->bytes, capacity);

		if (!grown)
			return FALSE;
		rec->bytes = grown;
		rec->capacity = capacity;
	}
	e = rec->bytes + rec->size;
	write16(e, (unsigned short)tag);
	write16(e + 2, (unsigned short)block);
	write32(e + 4, offset);
	write32(e + 8, effect_id);
	rec->size += MODELS_RECORD_ENTRY;
	rec->count++;
	return TRUE;
}

/* a host offset of an address written in the tags: the region written's
(whole), the scratch's, or the level's */
H2_ALIGN static unsigned long models_load_host_of_address(struct models_load_context const *c, unsigned long address)
{
	if (!address)
		return 0;
	if (c->whole)
		return address >= c->r->link && address - c->r->link < c->r->b.size ? address - c->r->link : 0;
	if (address >= c->scratch.link && address - c->scratch.link < c->scratch.b.size)
		return c->scratch_at + (address - c->scratch.link);
	return models_load_offset_of(c, address);
}

/* a reference written at a host offset: into the region written, or the
scratch (never the level's own tags) */
H2_ALIGN static void models_load_write_reference(struct models_load_context *c, unsigned long field, unsigned long group, unsigned long id)
{
	if (c->whole)
		region_reference(c->r, field, group, id);
	else
		models_load_reference(c, field, group, id);
}

/* the effect references of a struct of layout at host offset at (its bytes
counted from origin) into the record as tag's, in block (the root's field,
MODELS_RECORD_ROOT the root); the root's blocks' elements walked once */
H2_ALIGN static boolean models_record_collect_struct(struct models_load_context *c, struct models_record *rec, long tag, unsigned short layout,
	unsigned long at, unsigned long origin, unsigned long block, unsigned long root, int depth)
{
	long count, i;

	if (layout == HALO_TAG_LAYOUT_NONE || layout == HALO_TAG_LAYOUT_UNKNOWN || depth > 8)
		return TRUE;
	count = halo_tag_layout_field_count(layout);
	for (i = 0; i < count; i++)
	{
		struct halo_tag_layout_field field;
		unsigned long f;

		if (!halo_tag_layout_field(layout, i, &field))
			continue;
		f = at + field.offset;
		if (field.kind == HALO_TAG_LAYOUT_FIELD_STRUCT)
		{
			if (!models_record_collect_struct(c, rec, tag, field.child, f, origin, block, root, depth + 1))
				return FALSE;
		}
		else if (field.kind == HALO_TAG_LAYOUT_FIELD_REFERENCE)
		{
			unsigned long id = read32(models_load_at(c, f + 12));

			if (read32(models_load_at(c, f)) == 'effe' && id != 0xFFFFFFFFUL && (long)(id & 0xFFFF) < c->tag_count &&
				!models_record_add(rec, tag, block, f - origin, id))
			{
				return FALSE;
			}
		}
		else if (field.kind == HALO_TAG_LAYOUT_FIELD_BLOCK && block == MODELS_RECORD_ROOT)
		{
			long n = (long)read32(models_load_at(c, f)), e;
			unsigned long elements = models_load_host_of_address(c, read32(models_load_at(c, f + 4)));

			if (n <= 0 || !elements || !field.child_size)
				continue;
			for (e = 0; e < n; e++)
			{
				if (!models_record_collect_struct(c, rec, tag, field.child, elements + field.child_size * (unsigned long)e, elements,
					f - root, root, depth + 1))
				{
					return FALSE;
				}
			}
		}
	}
	return TRUE;
}

/* the collision model at host offset root's effect references recorded as
tag's: its layout's, and an explosive's stage effects (h2_explosive_port in
the resistance's unused words, which the layout does not know: the effects
pass's _h2fx_target_object_stage writes them) */
H2_ALIGN static boolean models_record_collect(struct models_load_context *c, struct models_record *rec, long tag, unsigned long root)
{
	unsigned long port = root + offsetof(struct damage_resistance, unused);
	int k;

	if (!root || !models_record_collect_struct(c, rec, tag, halo_tag_layout_root('coll'), root, root, MODELS_RECORD_ROOT, root, 0))
		return FALSE;
	if (read32(models_load_at(c, port + offsetof(struct h2_explosive_port, mark))) != H2_EXPLOSIVE_MARK)
		return TRUE;
	for (k = 0; k < 3; k++)
	{
		unsigned long f = port + offsetof(struct h2_explosive_port, stage_effect) + 16UL * (unsigned long)k;
		unsigned long id = read32(models_load_at(c, f + 12));

		if (read32(models_load_at(c, f)) == 'effe' && id != 0xFFFFFFFFUL && (long)(id & 0xFFFF) < c->tag_count &&
			!models_record_add(rec, tag, MODELS_RECORD_ROOT, f - root, id))
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* the record's references of tag written into the collision model made at
host offset root: how many */
H2_ALIGN static long models_record_apply(struct models_load_context *c, struct models_record const *rec, long tag, unsigned long root)
{
	long i, written = 0;

	for (i = 0; i < rec->count && root; i++)
	{
		unsigned char const *e = rec->bytes + MODELS_RECORD_ENTRY * i;
		/* (read16 is signed: the words unsigned) */
		unsigned long block = (unsigned short)read16(e + 2), offset = read32(e + 4), field;

		if ((long)(unsigned short)read16(e) != tag)
			continue;
		if (block == MODELS_RECORD_ROOT)
			field = root + offset;
		else
		{
			unsigned long elements = models_load_host_of_address(c, read32(models_load_at(c, root + block + 4)));

			if (!elements)
				continue;
			field = elements + offset;
		}
		/* (one the made collision model already holds - a reference copied
		from the kit's -: left as it is, its name's length word the kit's;
		one the effects pass changed written as the pass wrote it) */
		if (read32(models_load_at(c, field + 12)) == read32(e + 8))
			continue;
		models_load_write_reference(c, field, 'effe', read32(e + 8));
		written++;
	}
	return written;
}

/* the record the last make computed (models_load_record_write), kept in
memory for the level's load in the same run: a direct level (DESIGN.md piece
D) has no record file, the make's record is the one its makers apply. One
level's: the next make replaces it */
static struct
{
	char level_name[128];
	long tag_count;
	struct models_record rec;
	boolean kept;
} models_record_made;

/* the level's record read: the make's in memory (models_record_made) when
this run made this level, else its record file (FALSE: none, or not this
level's - its tag count differs) */
H2_ALIGN static boolean models_record_read(struct models_load_context *c, struct models_record *rec, char const *map_name)
{
	char file[160], path[512];
	unsigned char head[12];
	FILE *in;
	boolean ok = FALSE;

	memset(rec, 0, sizeof(*rec));
	/* (the record the make computed in this run: no file read) */
	if (models_record_made.kept && models_record_made.tag_count == c->tag_count && !strcmp(models_record_made.level_name, map_name))
	{
		rec->bytes = malloc(models_record_made.rec.size ? models_record_made.rec.size : 1);
		if (!rec->bytes)
			return FALSE;
		memcpy(rec->bytes, models_record_made.rec.bytes, models_record_made.rec.size);
		rec->size = rec->capacity = models_record_made.rec.size;
		rec->count = models_record_made.rec.count;
		if (getenv("HALO_H2_MODELS_AT_LOAD_LOG"))
			platform_log("h2 models at load: %s: the record from the make (%ld effects' references, in memory)", map_name, rec->count);
		return TRUE;
	}
	snprintf(file, sizeof(file), "%s.h2rec", map_name);
	h2_level_folder_file(file, path, sizeof(path));
	if (!(in = fopen(path, "rb")))
		return FALSE;
	if (fread(head, 1, sizeof(head), in) == sizeof(head) && read32(head) == MODELS_RECORD_MAGIC &&
		(long)read32(head + 4) == c->tag_count && (long)read32(head + 8) >= 0 && (long)read32(head + 8) < 0x100000)
	{
		rec->count = (long)read32(head + 8);
		rec->size = MODELS_RECORD_ENTRY * (unsigned long)rec->count;
		rec->bytes = malloc(rec->size ? rec->size : 1);
		ok = rec->bytes && fread(rec->bytes, 1, rec->size, in) == rec->size;
	}
	else
		platform_log("h2 models at load: %s: its record file is not this level's", map_name);
	fclose(in);
	if (!ok)
	{
		free(rec->bytes);
		memset(rec, 0, sizeof(*rec));
	}
	return ok;
}

H2_ALIGN static void models_record_free(struct models_record *rec)
{
	free(rec->bytes);
	memset(rec, 0, sizeof(*rec));
}

/* (a level made with its models, checked) the level's own collision model
tag's effect references written into the one made again: the record a
builder would have made of it, applied - what h2_effects.c wrote is then
part of the comparison */
H2_ALIGN static void models_record_self_apply(struct models_load_context *c, long tag)
{
	struct models_record rec;
	unsigned long root = models_load_offset_of(c, read32(models_load_instance(c, tag) + 20));

	memset(&rec, 0, sizeof(rec));
	if (root && c->roots[tag] && models_record_collect(c, &rec, tag, root))
	{
		long written;

		models_load_names_begin(c);
		written = models_record_apply(c, &rec, tag, c->roots[tag]);
		models_load_names_end(c);
		if (getenv("HALO_H2_MODELS_AT_LOAD_LOG"))
			platform_log("h2 models at load: tag %ld (%s): %ld effects' references of the level's, %ld written again", tag,
				models_load_name(c, tag) ? models_load_name(c, tag) : "?", rec.count, written);
	}
	else if (getenv("HALO_H2_MODELS_AT_LOAD_LOG"))
		platform_log("h2 models at load: tag %ld: no effects' references collected (root %lu, made %lu)", tag, root, c->roots[tag]);
	models_record_free(&rec);
}

/* (h2_level.c, after the effects pass) the collision models made at load -
the bipeds' (their tags from models_first, models_count) and the vehicles'
own (the weapons' tags from weapons_first; NONE none) -: their stubs' effect
references recorded, their instances emptied. The record is kept in memory
for this run's load of the level (models_record_made); file (the file sink's
level, loaded by a later run) also writes it to the level's record file */
static boolean models_load_record_write(struct region *tags, unsigned long instances, long tag_count, char const *level_name,
	long models_first, long models_count, long weapons_first, long object_collision_first, long object_collision_count,
	long objcoll_first, long objcoll_count, boolean file)
{
	struct models_load_context c;
	struct models_record rec;
	unsigned char head[12];
	char path[512];
	long i, colls = 0;
	boolean ok = TRUE;

	memset(&c, 0, sizeof(c));
	memset(&rec, 0, sizeof(rec));
	c.r = &c.scratch;
	c.main = tags;
	c.main_instances = instances;
	c.tag_count = tag_count;
	c.scratch_at = 0x40000000UL;
	c.scratch.link = tags->link + c.scratch_at;
	/* (a stub's instance emptied as it is recorded: the maker places the
	whole one) */
	for (i = models_first; models_first != NONE && i < models_first + models_count && ok; i++)
	{
		unsigned char *instance = region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)i);
		unsigned long root = models_load_offset_of(&c, read32(instance + 20));

		if (read32(instance) != 'coll' || !root)
			continue;
		ok = models_record_collect(&c, &rec, i, root);
		write32(instance + 20, 0);
		colls++;
	}
	/* (the objects' collision models: Halo 2's and the animations') */
	for (i = 0; i < object_collision_count + objcoll_count && ok; i++)
	{
		long index = i < object_collision_count ? object_collision_first + i : objcoll_first + i - object_collision_count;
		unsigned char *instance = region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)index);
		unsigned long root = models_load_offset_of(&c, read32(instance + 20));

		if (read32(instance) != 'coll' || !root)
			continue;
		ok = models_record_collect(&c, &rec, index, root);
		write32(instance + 20, 0);
		colls++;
	}
	if (weapons_first != NONE)
	{
		long vehicle_tags[64], n = h2_vehicles_collision_tags(vehicle_tags, 64), k;

		for (k = 0; k < n && ok; k++)
		{
			long index = weapons_first + h2w.count * H2W_TAGS_PER_WEAPON + vehicle_tags[k];
			unsigned char *instance = region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)index);
			unsigned long root = models_load_offset_of(&c, read32(instance + 20));

			if (read32(instance) != 'coll' || !root)
				continue;
			ok = models_record_collect(&c, &rec, index, root);
			write32(instance + 20, 0);
			colls++;
		}
	}
	/* (the last make's record forgotten whatever this one comes to) */
	models_record_free(&models_record_made.rec);
	models_record_made.kept = FALSE;
	if (ok && strlen(level_name) < sizeof(models_record_made.level_name))
	{
		strcpy(models_record_made.level_name, level_name);
		models_record_made.tag_count = tag_count;
		models_record_made.rec = rec;
		models_record_made.kept = TRUE;
		memset(&rec, 0, sizeof(rec));
		rec.count = models_record_made.rec.count;
	}
	if (ok && file)
	{
		struct models_record const *kept = models_record_made.kept ? &models_record_made.rec : &rec;
		FILE *out;

		write32(head, MODELS_RECORD_MAGIC);
		write32(head + 4, (unsigned long)tag_count);
		write32(head + 8, (unsigned long)kept->count);
		h2_level_folder_create();
		snprintf(path, sizeof(path), "%s\\%s.h2rec", H2_LEVEL_FOLDER, level_name);
		out = fopen(path, "wb");
		ok = out && fwrite(head, 1, sizeof(head), out) == sizeof(head) &&
			(!kept->size || fwrite(kept->bytes, 1, kept->size, out) == kept->size);
		if (out && fclose(out) != 0)
			ok = FALSE;
	}
	platform_log("h2 models at load: %ld effects' references of %ld collision models made at load recorded (in memory%s%s%s)%s", rec.count,
		colls, file ? ", and in " : "", file ? level_name : "", file ? ".h2rec" : "", ok ? "" : " - FAILED");
	models_record_free(&rec);
	return ok;
}

/* (the bipeds' gathered) their first tag in the level's table, by their
first's name: every one of them where the level has it, else NONE */
H2_ALIGN static long models_load_bipeds_first(struct models_load_context const *c, struct h2_bipeds const *bipeds, char const *map_name)
{
	long count = h2_bipeds_tag_count(bipeds), first = NONE, i;
	unsigned long group;
	char name[300];
	char const *n;

	if (count > 0 && h2_bipeds_tag_describe(bipeds, 0, &group, name, sizeof(name)))
		first = models_load_find(c, name, group);
	for (i = 0; first != NONE && i < count; i++)
	{
		if (!h2_bipeds_tag_describe(bipeds, i, &group, name, sizeof(name)) || first + i >= c->tag_count ||
			!(n = models_load_name(c, first + i)) || strcmp(n, name) || models_load_group(c, first + i) != group)
		{
			platform_log("h2 models at load: %s: the bipeds' tag %ld (%s) is not the level's", map_name, i, name);
			first = NONE;
		}
	}
	return first;
}

/* the bipeds' models, graphs and collision models written (models_only)
through the context's host, from first; their parts' buffer tables the
models' own (FALSE: not made) */
H2_ALIGN static boolean models_load_bipeds_write(struct models_load_context *c, struct h2_bipeds *bipeds, long first,
	unsigned long *vertex_table, long *buffers)
{
	struct h2_biped_host host;
	long parts = h2_bipeds_buffer_count(bipeds);

	models_load_host_make(&host, c);
	host.vertex_table = models_load_alloc(c, 12UL * (unsigned long)(parts + 1));
	host.index_table = models_load_alloc(c, 12UL * (unsigned long)(parts + 1));
	if (vertex_table)
		*vertex_table = host.vertex_table;
	if (buffers)
		*buffers = parts;
	{
		boolean written;

		models_load_names_begin(c);
		written = h2_bipeds_write(bipeds, &host, first) && !c->scratch.b.failed;
		models_load_names_end(c);
		return written;
	}
}

/* (h2_level.c, as a level is made with HALO_H2_MODELS_AT_LOAD=1) a
models_only pass of the bipeds over the level's tags (tags, its table at
instances): their collision models' outcome for the no_models pass, and the
room they take made at load (0: none could be made) */
static unsigned long models_load_bipeds_measure(struct build *build, struct h2_bipeds *bipeds, struct region *tags,
	unsigned long instances, long tag_count)
{
	struct models_load_context c;
	unsigned long room = 0;

	memset(&c, 0, sizeof(c));
	c.build = build;
	c.r = &c.scratch;
	c.main = tags;
	c.main_instances = instances;
	c.tag_count = tag_count;
	c.scratch_at = 0x40000000UL;
	c.scratch.link = tags->link + c.scratch_at;
	c.roots = calloc((size_t)(tag_count + 1), sizeof(*c.roots));
	if (c.roots && models_load_bipeds_write(&c, bipeds, 0, NULL, NULL))
		room = c.scratch.b.size;
	bytes_free(&c.scratch.b);
	free(c.roots);
	return room;
}

/* (h2_load_tags.c's maker 'h2md', as a level made with HALO_H2_MODELS_AT_LOAD=1
loads: its tags read and relocated, before they are checked) the bipeds'
models, graphs and collision models made from the level's MCC map after the
map's tags and placed */
boolean h2_models_load_make(struct h2_load_tags *make, long first, long count, char const *map_name)
{
	char const *directory = map_name ? h2_maps_directory_of(map_name) : NULL;
	struct models_load_context c;
	struct h2_bipeds *bipeds = NULL;
	struct h2_cache *cache;
	char why[160];
	unsigned long used, limit, at, vertex_table = 0;
	unsigned long long started = vita_host_time_us(), gathered = 0, bytes0 = 0, bytes1 = 0;
	unsigned long reads0 = 0, reads1 = 0, chunks0 = 0, chunks1 = 0;
	long i, buffers = 0, placed = 0, effects = 0;
	boolean made = FALSE;

	/* (what an allocation that gets no memory was for: h2_build_heap_unit) */
	h2_build_heap_unit("the bipeds' models, graphs and collision models made at load");
	if (!directory || !(cache = h2_cache_open(directory, map_name, why, sizeof(why))))
	{
		platform_log("h2 models at load: %s.map cannot be read: %s", map_name, directory ? why : "no folder");
		return FALSE;
	}
	memset(&c, 0, sizeof(c));
	c.r = &c.scratch;
	c.base = (unsigned char *)h2_load_tags_link(make);
	c.instances = h2_load_tags_instance(make, 0);
	c.tag_count = h2_load_tags_count(make);
	h2_load_tags_room(make, &used, &limit);
	at = (used + 15) & ~15UL;
	c.scratch_at = at;
	c.scratch.link = (unsigned long)c.base + at;
	c.build = calloc(1, sizeof(*c.build));
	c.roots = calloc((size_t)(c.tag_count + 1), sizeof(*c.roots));
	if (!c.build || !c.roots)
		goto done;
	c.build->cache = cache;
	c.build->gather_trim_bytes = models_load_trim_bytes();
	{
		struct h2_biped_host gather;

		models_load_host_make(&gather, &c);
		h2_cache_resource_stats(&reads0, &chunks0, &bytes0);
		bipeds = h2_bipeds_gather(&gather);
		gathered = vita_host_time_us();
		h2_cache_resource_stats(&reads1, &chunks1, &bytes1);
	}
	if (!bipeds || models_load_bipeds_first(&c, bipeds, map_name) != first || h2_bipeds_tag_count(bipeds) != count)
	{
		platform_log("h2 models at load: %s: the bipeds made from the map are not the level's (%ld tags from %ld, the level's %ld from %ld)",
			map_name, h2_bipeds_tag_count(bipeds), bipeds ? models_load_bipeds_first(&c, bipeds, map_name) : NONE, count, first);
		goto done;
	}
	if (!models_load_bipeds_write(&c, bipeds, first, &vertex_table, &buffers) || at > limit || c.scratch.b.size > limit - at)
	{
		platform_log("h2 models at load: %s: the bipeds' models could not be made (%lu KB, %lu KB of room)", map_name,
			c.scratch.b.size / 1024, at < limit ? (limit - at) / 1024 : 0);
		goto done;
	}
	/* (the effects' references the level recorded for their collision
	models, written into the ones made) */
	{
		struct models_record rec;

		if (models_record_read(&c, &rec, map_name))
		{
			models_load_names_begin(&c);
			for (i = first; i < first + count; i++)
			{
				if (c.roots[i] && models_load_group(&c, i) == 'coll' && !read32(models_load_instance(&c, i) + 20))
					effects += models_record_apply(&c, &rec, i, c.roots[i]);
			}
			models_load_names_end(&c);
			if (getenv("HALO_H2_MODELS_AT_LOAD_LOG"))
				platform_log("h2 models at load: %s: the level's record: %ld effects' references, %ld written", map_name, rec.count, effects);
		}
		else
			platform_log("h2 models at load: %s: the level has no record of its collision models' effects", map_name);
		models_record_free(&rec);
	}
	memcpy(c.base + at, c.scratch.b.data, c.scratch.b.size);
	h2_load_tags_room_used(make, at + c.scratch.b.size);
	for (i = first; i < first + count; i++)
	{
		if (c.roots[i] && !read32(models_load_instance(&c, i) + 20))
		{
			h2_load_tags_place(make, i, c.base + c.roots[i]);
			placed++;
		}
	}
	/* (their parts' buffers on a table of their own: checked with the
	map's tags and registered after, as the level's are -
	tags_header_register_vertex_and_index_buffers; the check nulls a part's
	buffers that are not on a table it knows, and the part is not drawn) */
	if (!h2_load_tags_buffers_add(c.base + vertex_table, buffers, c.base + vertex_table + 12UL * (unsigned long)(buffers + 1), buffers))
	{
		platform_log("h2 models at load: %s: the bipeds' buffer table could not be kept", map_name);
		goto done;
	}
	made = TRUE;
	models_load_made = TRUE;
	platform_log("h2 models at load: %s: the bipeds' models, graphs and collision models made from the MCC map: %ld tags placed, "
		"%lu KB, %ld buffers, %ld effects' references, %.0f ms (gathered in %.0f: %lu resource reads inflating %lu chunks, %.1f MB)", map_name,
		placed, c.scratch.b.size / 1024, buffers, effects, (double)(vita_host_time_us() - started) / 1000.0,
		(double)(gathered - started) / 1000.0, reads1 - reads0, chunks1 - chunks0, (double)(bytes1 - bytes0) / 1048576.0);
done:
	if (bipeds)
		h2_bipeds_free(bipeds);
	bytes_free(&c.scratch.b);
	free(c.roots);
	free(c.build);
	h2_cache_close(cache);
	return made;
}

/* ---------- the proof: a made-again tag against the level's */

struct models_compare
{
	struct models_load_context *c;
	unsigned long bytes, differ;
	long tags, tags_differ;
	unsigned long effect_bytes;
	char first[240], effect_first[200];
	char const *tag;
};

/* a pointer of the made-again models (size bytes there), NULL if it is
not one */
H2_ALIGN static unsigned char *models_compare_made(struct models_load_context *c, unsigned long p, unsigned long size)
{
	unsigned long link = c->r->link;

	if (!p)
		return NULL;
	if (p >= link)
		return p - link <= c->r->b.size && size <= c->r->b.size - (p - link) ? region_at(c->r, p - link) : NULL;
	return (unsigned char *)p;
}

H2_ALIGN static void models_compare_note(struct models_compare *m, char const *what, unsigned long offset)
{
	if (!m->first[0])
		snprintf(m->first, sizeof(m->first), "%s: %s at +0x%lX", m->tag, what, offset);
}

/* the pointers' words of a struct of layout at at (blocks', datas' and
references' names' addresses), marked in mask (size bytes) */
H2_ALIGN static void models_compare_mark(unsigned short layout, unsigned char *mask, unsigned long size, unsigned long at, int depth)
{
	long count, i;

	if (layout == HALO_TAG_LAYOUT_NONE || layout == HALO_TAG_LAYOUT_UNKNOWN || depth > 16)
		return;
	count = halo_tag_layout_field_count(layout);
	for (i = 0; i < count; i++)
	{
		struct halo_tag_layout_field field;
		unsigned long f, word = 0;

		if (!halo_tag_layout_field(layout, i, &field))
			continue;
		f = at + field.offset;
		switch (field.kind)
		{
		case HALO_TAG_LAYOUT_FIELD_BLOCK: word = f + 4; break;
		case HALO_TAG_LAYOUT_FIELD_DATA: word = f + 12; break;
		case HALO_TAG_LAYOUT_FIELD_REFERENCE:
			/* (a reference's bytes their distance from its start, one more:
			its group, its name's length and its tag id told apart from the
			rest, its name's address a pointer) */
			if (f + 16 <= size)
			{
				int k;

				for (k = 0; k < 16; k++)
					mask[f + k] = (unsigned char)(k + 1);
			}
			word = f + 4;
			break;
		case HALO_TAG_LAYOUT_FIELD_POINTER: word = f; break;
		case HALO_TAG_LAYOUT_FIELD_STRUCT:
			models_compare_mark(field.child, mask, size, f, depth + 1);
			continue;
		default:
			continue;
		}
		if (word + 4 <= size)
			memset(mask + word, 255, 4);
	}
}

H2_ALIGN static void models_compare_children(struct models_compare *m, unsigned short layout, unsigned char const *loaded,
	unsigned char const *made, unsigned long offset, int depth);

/* a struct of layout (size bytes) of the level's (loaded) against the made
again one (made): its bytes but its pointers, then what its blocks and
datas point to */
H2_ALIGN static void models_compare_struct(struct models_compare *m, unsigned short layout, unsigned char const *loaded,
	unsigned char const *made, unsigned long size, unsigned long offset, int depth)
{
	unsigned char *mask = calloc(1, size ? size : 1);
	unsigned long b;

	if (!mask)
		return;
	models_compare_mark(layout, mask, size, 0, 0);
	/* (an explosive's collision model: its stage effects' references in the
	resistance's unused words - h2_explosive_port, which the layout does not
	know; their name words pointers the loader never relocated) */
	if (layout == halo_tag_layout_root('coll') && size >= 0x298 &&
		read32(loaded + offsetof(struct damage_resistance, unused) + offsetof(struct h2_explosive_port, mark)) == H2_EXPLOSIVE_MARK)
	{
		int r, k;

		for (r = 0; r < 3; r++)
		{
			unsigned long f = offsetof(struct damage_resistance, unused) + offsetof(struct h2_explosive_port, stage_effect) + 16UL * (unsigned long)r;

			for (k = 0; k < 16; k++)
				mask[f + k] = (unsigned char)(k + 1);
			memset(mask + f + 4, 255, 4);
		}
	}
	m->bytes += size;
	for (b = 0; b < size; b++)
	{
		/* (an effect's reference: what h2_effects.c writes into a collision
		model after it is made - the shield's and regions' effects -, the
		effects' part, not the model's: counted apart) */
		if (mask[b] && mask[b] != 255 && loaded[b] != made[b])
		{
			unsigned long start = b - (unsigned long)(mask[b] - 1);

			if (read32(loaded + start) == 'effe' || read32(made + start) == 'effe')
			{
				m->effect_bytes++;
				if (!m->effect_first[0])
					snprintf(m->effect_first, sizeof(m->effect_first), "%s +0x%lX: %08lx, made again %08lx", m->tag, offset + start,
						read32(loaded + start + 12), read32(made + start + 12));
				continue;
			}
		}
		if ((!mask[b] || mask[b] != 255) && loaded[b] != made[b])
		{
			m->differ++;
			if (!m->first[0])
				snprintf(m->first, sizeof(m->first), "%s: a byte at +0x%lX (the word there %08lx, made again %08lx)", m->tag,
					offset + b, read32(loaded + (b & ~3UL)), read32(made + (b & ~3UL)));
			if (getenv("HALO_H2_MODELS_AT_LOAD_LOG") && m->differ < 64)
				platform_log("h2 models at load: %s: +0x%lX: %08lx, made again %08lx", m->tag, offset + b, read32(loaded + (b & ~3UL)),
					read32(made + (b & ~3UL)));
		}
	}
	free(mask);
	models_compare_children(m, layout, loaded, made, offset, depth);
}

/* what a struct's blocks and datas (its struct fields' too) point to */
H2_ALIGN static void models_compare_children(struct models_compare *m, unsigned short layout, unsigned char const *loaded,
	unsigned char const *made, unsigned long offset, int depth)
{
	long count, i;

	if (layout == HALO_TAG_LAYOUT_NONE || layout == HALO_TAG_LAYOUT_UNKNOWN || depth > 16)
		return;
	count = halo_tag_layout_field_count(layout);
	for (i = 0; i < count; i++)
	{
		struct halo_tag_layout_field field;
		unsigned long f;

		if (!halo_tag_layout_field(layout, i, &field))
			continue;
		f = field.offset;
		if (field.kind == HALO_TAG_LAYOUT_FIELD_STRUCT)
			models_compare_children(m, field.child, loaded + f, made + f, offset + f, depth + 1);
		else if (field.kind == HALO_TAG_LAYOUT_FIELD_BLOCK)
		{
			long n = (long)read32(loaded + f), e;
			unsigned long bytes = n > 0 ? (unsigned long)n * field.child_size : 0;
			unsigned char const *a, *c;

			if ((long)read32(made + f) != n)
			{
				m->differ += 4;
				models_compare_note(m, "a block's count", offset + f);
				continue;
			}
			if (n <= 0 || !field.child_size)
				continue;
			a = (unsigned char const *)read32(loaded + f + 4);
			c = models_compare_made(m->c, read32(made + f + 4), bytes);
			if (!a || !c)
			{
				m->differ += bytes;
				models_compare_note(m, a ? "a block not made" : "a block not loaded", offset + f);
				continue;
			}
			for (e = 0; e < n; e++)
				models_compare_struct(m, field.child, a + field.child_size * e, c + field.child_size * e, field.child_size,
					0, depth + 1);
		}
		else if (field.kind == HALO_TAG_LAYOUT_FIELD_DATA)
		{
			unsigned long n = read32(loaded + f);
			unsigned char const *a, *c;

			if (read32(made + f) != n)
			{
				m->differ += 4;
				models_compare_note(m, "a data's size", offset + f);
				continue;
			}
			if (!n)
				continue;
			a = (unsigned char const *)read32(loaded + f + 12);
			c = models_compare_made(m->c, read32(made + f + 12), n);
			m->bytes += n;
			if (!a || !c)
			{
				m->differ += n;
				models_compare_note(m, "a data not there", offset + f);
			}
			else if (memcmp(a, c, n))
			{
				unsigned long k;

				for (k = 0; k < n; k++)
					m->differ += a[k] != c[k];
				models_compare_note(m, "a data's bytes", offset + f);
			}
		}
	}
}

/* a model's parts' vertices and strips (pointers of no layout: the
Xbox's buffers): CE_MODEL_GEOMETRY_SIZE 0x30 (parts at 0x24),
CE_MODEL_PART_SIZE 0x68 (strip count 0x48, strip 0x4C, vertex count 0x58,
vertices 0x60), 32 bytes a compressed vertex */
H2_ALIGN static void models_compare_model_buffers(struct models_compare *m, unsigned char const *loaded, unsigned char const *made)
{
	long g, geometry_count = (long)read32(loaded + 0xD0);
	unsigned char const *a = (unsigned char const *)read32(loaded + 0xD0 + 4);
	unsigned char const *c = models_compare_made(m->c, read32(made + 0xD0 + 4), (unsigned long)geometry_count * 0x30);

	if (geometry_count <= 0 || !a || !c || (long)read32(made + 0xD0) != geometry_count)
		return;
	for (g = 0; g < geometry_count; g++)
	{
		long p, part_count = (long)read32(a + 0x30 * g + 0x24);
		unsigned char const *pa = (unsigned char const *)read32(a + 0x30 * g + 0x24 + 4);
		unsigned char const *pc = models_compare_made(m->c, read32(c + 0x30 * g + 0x24 + 4), (unsigned long)part_count * 0x68);

		if (part_count <= 0 || !pa || !pc || (long)read32(c + 0x30 * g + 0x24) != part_count)
			continue;
		for (p = 0; p < part_count; p++)
		{
			unsigned char const *x = pa + 0x68 * p, *y = pc + 0x68 * p;
			unsigned long vertex_bytes = read32(x + 0x58) * 32UL, strip_bytes = (read32(x + 0x48) + 2UL) * 2UL;
			unsigned char const *va = (unsigned char const *)read32(x + 0x60), *vc;
			unsigned char const *sa = (unsigned char const *)read32(x + 0x4C), *sc;

			if (read32(x + 0x58) != read32(y + 0x58) || read32(x + 0x48) != read32(y + 0x48))
				continue;
			vc = models_compare_made(m->c, read32(y + 0x60), vertex_bytes);
			sc = models_compare_made(m->c, read32(y + 0x4C), strip_bytes);
			m->bytes += vertex_bytes + strip_bytes;
			if (!va || !vc || memcmp(va, vc, vertex_bytes))
			{
				m->differ += vertex_bytes;
				models_compare_note(m, "a part's vertices", 0x68 * (unsigned long)p);
			}
			if (!sa || !sc || memcmp(sa, sc, strip_bytes))
			{
				m->differ += strip_bytes;
				models_compare_note(m, "a part's strip", 0x68 * (unsigned long)p);
			}
		}
	}
}

/* one tag of the level's (index, its group) against its made-again root */
H2_ALIGN static void models_compare_tag(struct models_compare *m, struct models_load_context *c, long index, unsigned long group)
{
	unsigned long before_bytes = m->bytes, before_differ = m->differ, at;
	unsigned long size = group == 'mode' ? 0xE8 : group == 'coll' ? 0x298 : group == 'antr' ? 0x80 : 0;
	unsigned char const *loaded, *made;

	if (!size || index < 0 || index >= c->tag_count)
		return;
	m->tags++;
	m->tag = models_load_name(c, index);
	at = models_load_offset_of(c, read32(models_load_instance(c, index) + 20));
	loaded = at ? c->base + at : NULL;
	made = c->roots[index] ? models_load_at(c, c->roots[index]) : NULL;
	if (!loaded || !made)
	{
		m->tags_differ++;
		models_compare_note(m, made ? "the level's tag has no data" : "not made again", 0);
		return;
	}
	models_compare_struct(m, halo_tag_layout_root(group), loaded, made, size, 0, 0);
	if (group == 'mode')
		models_compare_model_buffers(m, loaded, made);
	if (m->differ != before_differ)
		m->tags_differ++;
	if (getenv("HALO_H2_MODELS_AT_LOAD_LOG"))
		platform_log("h2 models at load: %c%c%c%c %s: %lu bytes, %lu differ%s%s", (int)(group >> 24), (int)(group >> 16 & 255),
			(int)(group >> 8 & 255), (int)(group & 255), m->tag ? m->tag : "?", m->bytes - before_bytes, m->differ - before_differ,
			m->first[0] ? "; first: " : "", m->first);
}

/* ---------- the weapons' and vehicles' models (h2_weapons.c, h2_vehicles.c)

Their render models are the level's made models (build->models, written by
model_made_write as h2\models\<n>, then h2w_model_write's nodes and markers);
made again here from a build of the map's cache alone, each is the level's
tag its weapon's, vehicle's or item's tag names (its numbering is the whole
level's), its shaders the level's by their paths. */

/* (model_made_write's, at load) a part's shader: the level's tag of its
path - an environment shader's model shader (soso, "<path> model") if the
level has one, as it was made */
H2_ALIGN static void models_load_shader_reference(struct model_made_writer *w, unsigned long field, short shader)
{
	struct models_load_context *c = w->context;
	struct shader const *sh = &w->build->shaders[shader];
	long index = NONE;
	unsigned long group = sh->group;

	if (sh->group == 'senv')
	{
		char name[300];

		snprintf(name, sizeof(name), "%s model", sh->path);
		if ((index = models_load_find(c, name, 'soso')) != NONE)
			group = 'soso';
	}
	if (index == NONE)
		index = models_load_find(c, sh->path, sh->group);
	region_reference(w->tags, field, group, index != NONE ? tag_id(index) : 0xFFFFFFFFUL);
}

/* the level's model tag a tag's reference at offset names (NONE none) */
H2_ALIGN static long models_load_referenced(struct models_load_context *c, long index, unsigned long offset)
{
	unsigned long at, id;

	if (index == NONE || !(at = models_load_offset_of(c, read32(models_load_instance(c, index) + 20))))
		return NONE;
	id = read32(models_load_at(c, at + offset));
	return id == 0xFFFFFFFFUL || (long)(id & 0xFFFF) >= c->tag_count ? NONE : (long)(id & 0xFFFF);
}

struct models_weapon_pair
{
	long model;                 /* the build's model */
	struct h2w_model const *h2w;    /* (a weapon's, item's or vehicle's) its nodes and markers, NULL an object's */
	long level;                 /* the level's model tag */
};

/* the weapons', grenades', powerups', ammunition's, vehicles' and
debris's models gathered (h2_weapons_gather on c's build), each with the
level's tag it is: their count */
H2_ALIGN static long models_load_weapon_pairs(struct models_load_context *c, struct models_weapon_pair *pairs, long size)
{
	long count = 0, i, tag;
	char name[300];

#define MODELS_PAIR(m, index, offset) \
	do { long level_ = models_load_referenced(c, (index), (offset)); \
		if ((m)->model != NONE && level_ != NONE && count < size) \
		{ pairs[count].model = (m)->model; pairs[count].h2w = (m); pairs[count++].level = level_; } } while (0)
	for (i = 0; i < h2w.count; i++)
	{
		struct h2w_weapon const *w = h2w.weapons[i];

		snprintf(name, sizeof(name), "h2\\weapons\\%s", w->name);
		tag = models_load_find(c, name, 'weap');
		MODELS_PAIR(&w->world, tag, offsetof(struct weapon_definition, object.model) + 12);
		MODELS_PAIR(&w->first_person, tag, offsetof(struct weapon_definition, weapon.interface_definition.first_person_model) + 12);
		MODELS_PAIR(&w->first_person_elite, tag, offsetof(struct weapon_definition, weapon.port_elite_first_person_model));
		snprintf(name, sizeof(name), "h2\\weapons\\%s ammo", w->name);
		MODELS_PAIR(&w->ammo_model, models_load_find(c, name, 'eqip'), offsetof(struct item_definition, object.model) + 12);
	}
	for (i = 0; i < H2W_GRENADE_COUNT; i++)
		MODELS_PAIR(&h2w.grenades[i], models_load_find(c, h2w_grenades[i].ce, 'eqip'), offsetof(struct item_definition, object.model) + 12);
	for (i = 0; i < H2W_POWERUP_COUNT; i++)
		MODELS_PAIR(&h2w.powerups[i], models_load_find(c, h2w_powerups[i].ce, 'eqip'), offsetof(struct item_definition, object.model) + 12);
	for (i = 0; i < h2v.count; i++)
	{
		snprintf(name, sizeof(name), "h2\\vehicles\\%s", h2v.vehicles[i]->name);
		MODELS_PAIR(&h2v.vehicles[i]->model, models_load_find(c, name, 'vehi'), offsetof(struct unit_definition, object.model) + 12);
	}
	for (i = 0; i < h2v.debris_count; i++)
	{
		snprintf(name, sizeof(name), "h2\\vehicles\\debris\\%s", h2v.debris[i]->name);
		MODELS_PAIR(&h2v.debris[i]->model, models_load_find(c, name, 'garb'), offsetof(struct item_definition, object.model) + 12);
	}
#undef MODELS_PAIR
	return count;
}

/* each pair's model written into c's region (model_made_write, then
h2w_model_write) as the level's tag it is: FALSE if one could not be */
H2_ALIGN static boolean models_load_weapons_write(struct models_load_context *c, struct models_weapon_pair const *pairs, long count)
{
	struct model_made_writer writer;
	long i, parts = 0;

	for (i = 0; i < c->build->model_count; i++)
		parts += c->build->models[i].part_count;
	memset(&writer, 0, sizeof(writer));
	writer.build = c->build;
	writer.tags = c->r;
	writer.vertex_table = region_alloc(c->r, 12UL * (unsigned long)(parts + 1));
	writer.index_table = region_alloc(c->r, 12UL * (unsigned long)(parts + 1));
	if (!c->main)
	{
		writer.shader_reference = models_load_shader_reference;
		writer.context = c;
	}
	else
	{
		/* (as the level is made: its own shaders' numbering) */
		writer.shader_first = c->shader_first;
		writer.soso_first = c->soso_first;
		writer.shader_soso = c->shader_soso;
	}
	models_load_names_begin(c);
	for (i = 0; i < count; i++)
	{
		unsigned long root;

		if (c->roots[pairs[i].level])
			continue;
		root = model_made_write(&writer, pairs[i].model);
		if (!root || c->r->b.failed)
		{
			models_load_names_end(c);
			return FALSE;
		}
		if (pairs[i].h2w)
			h2w_model_write(c->r, root, pairs[i].h2w);
		c->roots[pairs[i].level] = models_load_root(c, root);
	}
	models_load_names_end(c);
	c->buffers = parts;
	c->vertex_table = models_load_root(c, writer.vertex_table);
	return !c->r->b.failed;
}

/* the weapons' first-person animation graphs (h2_weapons_write's: each
weapon's second tag the Master Chief's, its seventh the Elite's - an empty
graph where it has none), the weapons' first tag of the level weapons_first:
FALSE if one could not be written */
H2_ALIGN static boolean models_load_weapon_graphs_write(struct models_load_context *c, long weapons_first, long sound_first)
{
	long i;

	models_load_names_begin(c);
	for (i = 0; i < h2w.count && !c->r->b.failed; i++)
	{
		struct h2w_weapon const *w = h2w.weapons[i];
		long index = weapons_first + H2W_TAGS_PER_WEAPON * i;
		unsigned long root = region_alloc(c->r, sizeof(struct animation_graph));

		if (w->graph)
			h2w_graph_write(c->r, root, w->graph, sound_first);
		c->roots[index + 1] = models_load_root(c, root);
		root = region_alloc(c->r, sizeof(struct animation_graph));
		if (w->first_person_elite.model != NONE && w->graph_elite)
			h2w_graph_write(c->r, root, w->graph_elite, sound_first);
		c->roots[index + 6] = models_load_root(c, root);
	}
	models_load_names_end(c);
	return !c->r->b.failed;
}

/* the vehicles' animation graphs (h2v_graph_write_from: the kit vehicle's
graph copied and given Halo 2's animations), each vehicle's own tag after
its body's, the weapons' first tag of the level weapons_first: FALSE if one
could not be written. The writer sets the graph's instance itself and names
the graph in the vehicle's tag: the instance is taken back (the maker places
it), the name is the level's own already */
H2_ALIGN static boolean models_load_vehicle_graphs_write(struct models_load_context *c, long weapons_first, char const *map_name)
{
	long vehicles_first = weapons_first + h2w.count * H2W_TAGS_PER_WEAPON, i;
	unsigned long instances = c->main ? c->main_instances : (unsigned long)(c->instances - c->base);

	models_load_names_begin(c);
	for (i = 0; i < h2v.count && !c->r->b.failed; i++)
	{
		struct h2v_vehicle const *v = h2v.vehicles[i];
		long index = vehicles_first + h2v_graph_tag(v), kit, base = NONE;
		unsigned long vehicle_root = models_load_offset_of(c, read32(models_load_instance(c, vehicles_first + v->tag) + 20)), kit_root;
		/* (the graph's instance as the region written has it: the writer sets
		it there - the level's own table as the level is made or loads, a
		copy's as it is checked) */
		unsigned char *instance = region_at(c->r, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)index);

		if (!v->graph)
			continue;
		kit = models_load_find(c, h2v_kinds[v->kind].base, 'vehi');
		kit_root = kit != NONE ? models_load_offset_of(c, read32(models_load_instance(c, kit) + 20)) : 0;
		if (kit_root)
			base = (short)read16(models_load_at(c, kit_root + offsetof(struct unit_definition, object.animation_graph) + 12));
		if (!vehicle_root || base == NONE)
		{
			platform_log("h2 models at load: %s: the %s has no vehicle tag or no kit graph to make its graph on", map_name, v->name);
			models_load_names_end(c);
			return FALSE;
		}
		h2v_graph_write_from(c->r, instances, vehicle_root, base, index, v);
		if (c->r->b.failed || !read32(instance + 20))
		{
			platform_log("h2 models at load: %s: the %s's graph could not be written", map_name, v->name);
			models_load_names_end(c);
			return FALSE;
		}
		c->roots[index] = read32(instance + 20) - c->r->link;
		write32(instance + 20, 0);
	}
	models_load_names_end(c);
	return !c->r->b.failed;
}

/* the vehicles' own collision models (h2_vehicles_collision_write), each
the tag after its vehicle's force model's values; FALSE if the region failed */
H2_ALIGN static boolean models_load_vehicle_collisions_write(struct models_load_context *c, long weapons_first)
{
	long vehicles_first = weapons_first + h2w.count * H2W_TAGS_PER_WEAPON, i;

	unsigned long instances = c->main ? c->main_instances : (unsigned long)(c->instances - c->base);

	/* (h2_vehicles_write's damage writes name the kit's smoke and fire
	explosion: the level's, by name - as the level is made the kit's were
	found) */
	if (!c->main)
	{
		h2v.kit_smoke = models_load_find(c, "weapons\\frag grenade\\effects\\smoke", 'effe');
		h2v.kit_fire_explosion = models_load_find(c, "weapons\\frag grenade\\effects\\fire explosion", 'effe');
	}
	models_load_names_begin(c);
	for (i = 0; i < h2v.count && !c->r->b.failed; i++)
	{
		struct h2v_vehicle const *v = h2v.vehicles[i];
		long index = vehicles_first + h2v_physics_tag(v) + 1;
		unsigned long root;

		if (!h2v_own_collision(v))
			continue;
		root = region_alloc(c->r, 664);
		if (c->r->b.failed)
			break;
		h2_vehicles_collision_write(c->build, c->r, root, i);
		h2v_collision_damage_write(c->r, instances, root, v);
		c->roots[index] = models_load_root(c, root);
	}
	models_load_names_end(c);
	return !c->r->b.failed;
}

/* (at load) the level's record of the effects' references written into the
vehicles' collision models made (how many) */
H2_ALIGN static long models_load_vehicle_collisions_effects(struct models_load_context *c, long weapons_first, char const *map_name)
{
	struct models_record rec;
	long vehicles_first = weapons_first + h2w.count * H2W_TAGS_PER_WEAPON, i, written = 0;

	if (!models_record_read(c, &rec, map_name))
		return 0;
	models_load_names_begin(c);
	for (i = 0; i < h2v.count; i++)
	{
		long index = vehicles_first + h2v_physics_tag(h2v.vehicles[i]) + 1;

		if (h2v_own_collision(h2v.vehicles[i]) && c->roots[index])
			written += models_record_apply(c, &rec, index, c->roots[index]);
	}
	models_load_names_end(c);
	models_record_free(&rec);
	return written;
}

/* ---------- the level's objects (scenery, crates, machines, controls: h2_level.c's
objects_gather; one of each (tag, placement variant), its model, its graph (a
machine's or control's - h2_objanim.c - or a gear's) and its collision model
(Halo 2's, or its animation's node by node). Their tags stay in the level
("h2\objects\<name>", their model, graph and collision model named there);
what they name is made here. */

/* (at load) the level's objects gathered on c's build as the level's builder
gathers them (the same limits, no spill file): their count */
H2_ALIGN static long models_load_objects_gather(struct models_load_context *c)
{
	struct h2_cache *cache = c->build->cache;
	struct h2_cache_tag scenario;
	long netgame = 0;
	unsigned long items;
	boolean campaign = !(h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) &&
		h2_cache_block(cache, scenario.address + 0x120, 0x90, &netgame, &items) && netgame > 0);
	boolean nodes = h2_objanim_scenery_nodes;

	c->build->object_limit = campaign ? MAXIMUM_LEVEL_OBJECTS : 99;
	h2_objanim_scenery_nodes = campaign;
	spill_suspended = TRUE;
	objects_gather(c->build);
	spill_suspended = FALSE;
	h2_objanim_scenery_nodes = nodes;
	return c->build->object_count;
}

/* the objects' first tag in the level's table (object_first: their tags in
the gather's order, "h2\objects\<name>" - two objects may share a name, so
the first is found by name and the rest follow it), NONE if the level's are
not these */
H2_ALIGN static long models_load_objects_first(struct models_load_context *c, char const *map_name)
{
	long first = NONE, i;
	char name[300];
	char const *n;

	if (c->build->object_count > 0)
	{
		snprintf(name, sizeof(name), "h2\\objects\\%s", c->build->objects[0].name);
		first = models_load_find(c, name, c->build->objects[0].group);
	}
	for (i = 0; first != NONE && i < c->build->object_count; i++)
	{
		snprintf(name, sizeof(name), "h2\\objects\\%s", c->build->objects[i].name);
		if (first + i >= c->tag_count || !(n = models_load_name(c, first + i)) || strcmp(n, name) ||
			models_load_group(c, first + i) != c->build->objects[i].group)
		{
			platform_log("h2 models at load: %s: the object %ld (%s) is not the level's", map_name, i, name);
			first = NONE;
		}
	}
	return first;
}

/* the objects' models paired with the level's model tags their object tags
(from objects_first) name (from count on): the count */
H2_ALIGN static long models_load_object_pairs(struct models_load_context *c, struct models_weapon_pair *pairs, long size, long count,
	long objects_first, char const *map_name)
{
	long i;

	for (i = 0; i < c->build->object_count; i++)
	{
		struct object_made const *object = &c->build->objects[i];
		long tag = objects_first + i, level;

		level = models_load_referenced(c, tag, 0x28 + 12);
		if (level == NONE || object->model == NONE)
		{
			platform_log("h2 models at load: %s: the object %s has no model in the level", map_name, object->name);
			return NONE;
		}
		if (count < size)
		{
			pairs[count].model = object->model;
			pairs[count].h2w = NULL;
			pairs[count++].level = level;
		}
	}
	return count;
}

/* the objects' graphs and collision models written as the level names
them (effects: the level's record written into the collision models made -
not as the level is made; checked: each from the level's own) */
H2_ALIGN static boolean models_load_objects_write(struct models_load_context *c, long objects_first, char const *map_name, boolean effects)
{
	struct objanim_host_context objanim_context;
	struct h2_objanim_host objanim_host;
	struct models_record rec;
	boolean recorded = effects && !c->main && models_record_read(c, &rec, map_name);
	long i, colls = 0;

	if (!recorded)
		memset(&rec, 0, sizeof(rec));
	objanim_context.build = c->build;
	objanim_context.tags = c->r;
	objanim_host_make(&objanim_host, &objanim_context);
	models_load_names_begin(c);
	for (i = 0; i < c->build->object_count && !c->r->b.failed; i++)
	{
		struct object_made const *object = &c->build->objects[i];
		long tag = objects_first + i, graph, coll;
		unsigned long root;

		graph = models_load_referenced(c, tag, 0x38 + 12);
		coll = models_load_referenced(c, tag, 0x70 + 12);
		if (graph != NONE && !c->roots[graph])
		{
			root = object->anim && h2_objanim_has_graph(object->anim) ? h2_objanim_write_graph(object->anim, &objanim_host) :
				gear_graph_write(c->r, object);
			if (!root)
			{
				platform_log("h2 models at load: %s: the object %s's graph could not be written", map_name, object->name);
				break;
			}
			c->roots[graph] = models_load_root(c, root);
		}
		if (coll != NONE && !c->roots[coll])
		{
			if (object->anim && h2_objanim_has_collision(object->anim))
				root = h2_objanim_write_collision(object->anim, &objanim_host);
			else if (object->collision != NONE)
			{
				root = region_alloc(c->r, 664);
				if (!c->r->b.failed && !object_collision_write(c->build, c->r, root, object))
					root = 0;
			}
			else
				continue;
			if (!root || c->r->b.failed)
			{
				platform_log("h2 models at load: %s: the object %s's collision model could not be written", map_name, object->name);
				break;
			}
			c->roots[coll] = models_load_root(c, root);
			colls++;
			if (recorded)
				models_record_apply(c, &rec, coll, c->roots[coll]);
			else if (effects && !c->main)
			{
				models_load_names_end(c);
				models_record_self_apply(c, coll);
				models_load_names_begin(c);
			}
		}
	}
	models_load_names_end(c);
	models_record_free(&rec);
	return i == c->build->object_count && !c->r->b.failed;
}

/* (as a level is made) the weapons' and vehicles' models gathered, each the
level's model tag model_first + its number */
H2_ALIGN static long models_load_weapon_pairs_made(struct models_weapon_pair *pairs, long size, long model_first)
{
	long count = 0, i;

#define MODELS_PAIR_MADE(m) \
	do { if ((m)->model != NONE && count < size) \
		{ pairs[count].model = (m)->model; pairs[count].h2w = (m); pairs[count++].level = model_first + (m)->model; } } while (0)
	for (i = 0; i < h2w.count; i++)
	{
		MODELS_PAIR_MADE(&h2w.weapons[i]->world);
		MODELS_PAIR_MADE(&h2w.weapons[i]->first_person);
		MODELS_PAIR_MADE(&h2w.weapons[i]->first_person_elite);
		MODELS_PAIR_MADE(&h2w.weapons[i]->ammo_model);
	}
	for (i = 0; i < H2W_GRENADE_COUNT; i++)
		MODELS_PAIR_MADE(&h2w.grenades[i]);
	for (i = 0; i < H2W_POWERUP_COUNT; i++)
		MODELS_PAIR_MADE(&h2w.powerups[i]);
	for (i = 0; i < h2v.count; i++)
		MODELS_PAIR_MADE(&h2v.vehicles[i]->model);
	for (i = 0; i < h2v.debris_count; i++)
		MODELS_PAIR_MADE(&h2v.debris[i]->model);
#undef MODELS_PAIR_MADE
	return count;
}

/* (the level's weapons' tags, h2_weapons_tag_name from weapons_first) the
weapons' first tag in the level's table, by its first's name - every weapon's
and vehicle's tag, and every sound the weapons' gather named (the graphs'
sound references: the level's list begins with them - the effects' gather,
after the weapons' at build, adds its own sounds and the tags after the
sounds move) where the level has it, else NONE */
H2_ALIGN static long models_load_weapons_first(struct models_load_context const *c, char const *map_name, long *sound_first)
{
	/* (the weapons' tags and the vehicles' own: the effects pass adds
	debris tags after the vehicles' at build - the PC tier's particle
	models -, which the load's gather does not have, so the sounds are
	found in the level, after them) */
	long count = h2w.count * H2W_TAGS_PER_WEAPON + h2v_blast_first(), first = NONE, i;
	unsigned long group;
	char name[300];
	char const *n;

	*sound_first = NONE;
	if (h2w.count > 0)
	{
		snprintf(name, sizeof(name), "h2\\weapons\\%s", h2w.weapons[0]->name);
		first = models_load_find(c, name, 'weap');
	}
	for (i = 0; first != NONE && i < count; i++)
	{
		h2_weapons_tag_name(i, &group, name, sizeof(name));
		if (first + i >= c->tag_count || !(n = models_load_name(c, first + i)) || strcmp(n, name) || models_load_group(c, first + i) != group)
		{
			platform_log("h2 models at load: %s: the weapons' tag %ld (%s) is not the level's (there: %s)", map_name, i, name,
				first + i < c->tag_count && models_load_name(c, first + i) ? models_load_name(c, first + i) : "nothing");
			first = NONE;
		}
	}
	/* (the sounds the weapons' gather named, in the level's order: the
	first 'snd!' from the vehicles' tags on, every one after it named as
	the gather named it) */
	for (i = first != NONE ? first + count : c->tag_count; i < c->tag_count && *sound_first == NONE; i++)
	{
		if (models_load_group(c, i) == 'snd!' && (n = models_load_name(c, i)) != NULL && !strncmp(n, "sound\\h2\\", 9))
			*sound_first = i;
	}
	for (i = 0; first != NONE && *sound_first != NONE && i < h2w.sound_count; i++)
	{
		snprintf(name, sizeof(name), "sound\\h2\\%s", h2w.sounds[i]);
		if (*sound_first + i >= c->tag_count || !(n = models_load_name(c, *sound_first + i)) || strcmp(n, name) ||
			models_load_group(c, *sound_first + i) != 'snd!')
		{
			platform_log("h2 models at load: %s: the weapons' sound %ld (%s) is not the level's (there: %s)", map_name, i, name,
				*sound_first + i < c->tag_count && models_load_name(c, *sound_first + i) ? models_load_name(c, *sound_first + i) : "nothing");
			first = NONE;
		}
	}
	if (first != NONE && *sound_first == NONE && h2w.sound_count > 0)
	{
		platform_log("h2 models at load: %s: the weapons' sounds are not in the level", map_name);
		first = NONE;
	}
	return first;
}

/* (h2_level.c, as a level is made with HALO_H2_MODELS_AT_LOAD=1, after
h2_weapons_write) the room the weapons' and vehicles' tags made at load
take - the level's models first .. end (their numbers), the weapons'
first-person graphs -, measured by writing them as the load does after the
level's tags (tags, then cut back to its size): 0 if a model of these is not
a weapon's, item's or vehicle's, or a tag of the range from
model_first + first to the vehicles' last has neither data nor a maker */
static unsigned long models_load_weapons_measure(struct build *build, struct region *tags, unsigned long instances, long tag_count,
	long model_first, long first, long end, long weapons_first, boolean objects, long shader_first, long soso_first, short const *shader_soso)
{
	struct models_load_context c;
	struct models_weapon_pair *pairs = calloc(1024, sizeof(*pairs));
	unsigned long room = 0, start = tags->b.size;
	long count, i, range_end = weapons_first + h2w_sound_first(), objects_first = NONE, sound_first = NONE;

	memset(&c, 0, sizeof(c));
	c.build = build;
	c.r = tags;
	c.whole = TRUE;
	c.main = tags;
	c.main_instances = instances;
	c.tag_count = tag_count;
	c.scratch_at = start;
	c.shader_first = shader_first;
	c.soso_first = soso_first;
	c.shader_soso = shader_soso;
	c.roots = calloc((size_t)(tag_count + 1), sizeof(*c.roots));
	if (!pairs || !c.roots)
		goto done;
	count = models_load_weapon_pairs_made(pairs, 1024, model_first);
	if (objects && ((objects_first = models_load_objects_first(&c, "the level")) == NONE ||
		(count = models_load_object_pairs(&c, pairs, 1024, count, objects_first, "the level")) == NONE))
	{
		goto done;
	}
	/* (the level's sounds' first tag, as the maker finds it) */
	if (models_load_weapons_first(&c, "the level", &sound_first) != weapons_first)
	{
		platform_log("h2 models at load: the level's weapons' tags are not as the maker would find them: the weapons' models saved");
		goto done;
	}
	/* (a pair below the models made at load names a model the level's own
	loop wrote - a struct never made whose model index is 0, say: left out,
	named) */
	{
		long kept = 0;

		for (i = 0; i < count; i++)
		{
			if (pairs[i].level < model_first + first || pairs[i].level >= model_first + end)
				platform_log("h2 models at load: a %s model pair names the level's model %ld, not one made at load (%ld..%ld): left out",
					pairs[i].h2w ? "weapon's or vehicle's" : "object's", pairs[i].level - model_first, first, end - 1);
			else
				pairs[kept++] = pairs[i];
			if (getenv("HALO_H2_MODELS_AT_LOAD_LOG"))
				platform_log("h2 models at load: pair: model %ld -> level tag %ld (%s)", pairs[i].model, pairs[i].level, pairs[i].h2w ? "h2w" : "object");
		}
		count = kept;
	}
	if (!models_load_weapons_write(&c, pairs, count) || !models_load_weapon_graphs_write(&c, weapons_first, sound_first) ||
		!models_load_vehicle_graphs_write(&c, weapons_first, "the level") || !models_load_vehicle_collisions_write(&c, weapons_first) ||
		(objects && !models_load_objects_write(&c, objects_first, "the level", FALSE)))
	{
		goto done;
	}
	for (i = model_first + first; i < model_first + end && c.roots[i]; i++)
		;
	if (i < model_first + end)
	{
		platform_log("h2 models at load: the level's model %ld is no weapon's, item's or vehicle's: the weapons' models saved", i - model_first);
		goto done;
	}
	for (i = model_first + first; i < range_end; i++)
	{
		if (!c.roots[i] && !read32(region_at(tags, instances + XBOX_TAG_INSTANCE_SIZE * (unsigned long)i + 20)))
		{
			platform_log("h2 models at load: the level's tag %ld has no data and no maker: the weapons' models saved", i);
			goto done;
		}
	}
	room = tags->b.size - start;
done:
	/* (the measure cut off: the level's tags as they were) */
	if (!tags->b.failed && tags->b.size >= start)
		tags->b.size = start;
	free(pairs);
	free(c.roots);
	return room;
}

/* a build's models and bitmaps gathered for the weapons' models let go */
H2_ALIGN static void models_load_weapons_build_free(struct build *build)
{
	long i;

	if (!build)
		return;
	for (i = 0; i < build->model_count; i++)
	{
		long p;

		for (p = 0; p < build->models[i].part_count; p++)
		{
			free(build->models[i].parts[p].vertices);
			free(build->models[i].parts[p].strip);
			free(build->models[i].parts[p].nodes);
		}
		free(build->models[i].parts);
		free(build->models[i].regions);
	}
	free(build->models);
	for (i = 0; i < build->bitmap_count; i++)
		free(build->bitmaps[i].pixels);
	free(build->bitmaps);
	for (i = 0; i < build->object_count; i++)
		h2_objanim_free(build->objects[i].anim);
	free(build->objects);
	free(build->object_placements);
	free(build->material_shader);
}

/* the weapons' and vehicles' tags made at load written through c (the
weapons gathered on c's build first), each of the level's tags from first,
count of them, with no data given one: FALSE (why logged) if any could not be */
H2_ALIGN static boolean models_load_weapons_make(struct models_load_context *c, long first, long count, char const *map_name)
{
	struct models_weapon_pair *pairs = calloc(1024, sizeof(*pairs));
	long i, pair_count, weapons_first, objects_first = NONE, sound_first = NONE;
	unsigned long long phase[6] = { 0, 0, 0, 0, 0, 0 }, chunk_bytes[3] = { 0, 0, 0 };
	unsigned long reads[3] = { 0, 0, 0 }, chunks[3] = { 0, 0, 0 };
	boolean made = FALSE, objects;

	if (!pairs)
		goto done;
	/* (the objects first, as the level's builder gathers them: their models
	before the weapons') */
	phase[0] = vita_host_time_us();
	h2_cache_resource_stats(&reads[0], &chunks[0], &chunk_bytes[0]);
	objects = models_load_objects_gather(c) > 0;
	phase[1] = vita_host_time_us();
	h2_cache_resource_stats(&reads[1], &chunks[1], &chunk_bytes[1]);
	h2w_gather_models_only = TRUE;
	h2_weapons_gather(c->build);
	h2w_gather_models_only = FALSE;
	phase[2] = vita_host_time_us();
	h2_cache_resource_stats(&reads[2], &chunks[2], &chunk_bytes[2]);
	if ((weapons_first = models_load_weapons_first(c, map_name, &sound_first)) == NONE)
	{
		platform_log("h2 models at load: %s: the weapons made from the map are not the level's", map_name);
		goto done;
	}
	pair_count = models_load_weapon_pairs(c, pairs, 1024);
	/* (the objects' models: those the level names - a level made with its
	objects' models has them with data: made again, compared or unplaced) */
	if (objects && ((objects_first = models_load_objects_first(c, map_name)) == NONE ||
		(pair_count = models_load_object_pairs(c, pairs, 1024, pair_count, objects_first, map_name)) == NONE))
	{
		goto done;
	}
	if (!models_load_weapons_write(c, pairs, pair_count) || (phase[3] = vita_host_time_us()) == 0 ||
		!models_load_weapon_graphs_write(c, weapons_first, sound_first) || !models_load_vehicle_graphs_write(c, weapons_first, map_name) ||
		!models_load_vehicle_collisions_write(c, weapons_first) || (phase[4] = vita_host_time_us()) == 0 ||
		(objects && !models_load_objects_write(c, objects_first, map_name, TRUE)))
	{
		platform_log("h2 models at load: %s: the weapons' and vehicles' models and graphs could not be made (%lu KB written)", map_name,
			c->r->b.size / 1024);
		goto done;
	}
	for (i = first; i < first + count; i++)
	{
		if (!c->roots[i] && !read32(models_load_instance(c, i) + 20))
		{
			platform_log("h2 models at load: %s: its tag %ld (%s) is no weapon's, item's or vehicle's made from the map", map_name, i,
				models_load_name(c, i) ? models_load_name(c, i) : "?");
			goto done;
		}
	}
	phase[5] = vita_host_time_us();
	platform_log("h2 models at load: %s: phases: objects gathered %.0f ms (%ld objects, %ld models, %ld bitmaps described; %lu resource "
		"reads inflating %lu chunks, %.1f MB), weapons and vehicles gathered %.0f ms (%lu reads, %lu chunks, %.1f MB), %ld models written "
		"%.0f ms, the weapons' and vehicles' graphs and collision models %.0f ms, the objects' graphs and collision models %.0f ms", map_name,
		(double)(phase[1] - phase[0]) / 1000.0, c->build->object_count, c->build->model_count, c->build->bitmap_count,
		reads[1] - reads[0], chunks[1] - chunks[0], (double)(chunk_bytes[1] - chunk_bytes[0]) / 1048576.0,
		(double)(phase[2] - phase[1]) / 1000.0, reads[2] - reads[1], chunks[2] - chunks[1], (double)(chunk_bytes[2] - chunk_bytes[1]) / 1048576.0,
		pair_count, (double)(phase[3] - phase[2]) / 1000.0, (double)(phase[4] - phase[3]) / 1000.0, (double)(phase[5] - phase[4]) / 1000.0);
	/* (the effects' references the level recorded for the vehicles'
	collision models; a level checked against its own: each from the level's
	own collision model) */
	if (count > 0)
		models_load_vehicle_collisions_effects(c, weapons_first, map_name);
	else
	{
		for (i = 0; i < h2v.count; i++)
		{
			long index = weapons_first + h2w.count * H2W_TAGS_PER_WEAPON + h2v_physics_tag(h2v.vehicles[i]) + 1;

			if (h2v_own_collision(h2v.vehicles[i]))
				models_record_self_apply(c, index);
		}
	}
	made = TRUE;
done:
	free(pairs);
	return made;
}

/* (h2_load_tags.c's maker 'h2mw', as a level made with HALO_H2_MODELS_AT_LOAD=1
loads) the weapons' and vehicles' models and the weapons' first-person graphs
made from the level's MCC map straight into the tag cache after the map's
tags (a fixed region over its room) and placed */
boolean h2_models_weapons_load_make(struct h2_load_tags *make, long first, long count, char const *map_name)
{
	char const *directory = map_name ? h2_maps_directory_of(map_name) : NULL;
	struct models_load_context c;
	struct h2_cache *cache;
	char why[160];
	unsigned long used, limit, at;
	unsigned long long started = vita_host_time_us();
	long i, placed = 0;
	boolean made = FALSE;

	/* (what an allocation that gets no memory was for: h2_build_heap_unit) */
	h2_build_heap_unit("the weapons', vehicles' and objects' models, graphs and collision models made at load");
	if (!directory || !(cache = h2_cache_open(directory, map_name, why, sizeof(why))))
	{
		platform_log("h2 models at load: %s.map cannot be read: %s", map_name, directory ? why : "no folder");
		return FALSE;
	}
	memset(&c, 0, sizeof(c));
	c.r = &c.scratch;
	c.whole = TRUE;
	c.base = (unsigned char *)h2_load_tags_link(make);
	c.instances = h2_load_tags_instance(make, 0);
	c.tag_count = h2_load_tags_count(make);
	h2_load_tags_room(make, &used, &limit);
	at = (used + 15) & ~15UL;
	c.scratch_at = at;
	/* (the tag cache's room: never moved, past the limit it fails) */
	c.scratch.b.data = c.base;
	c.scratch.b.size = at <= limit ? at : limit;
	c.scratch.b.capacity = limit;
	c.scratch.b.fixed = TRUE;
	c.scratch.b.failed = at > limit;
	c.scratch.link = (unsigned long)c.base;
	c.build = calloc(1, sizeof(*c.build));
	c.roots = calloc((size_t)(c.tag_count + 1), sizeof(*c.roots));
	if (!c.build || !c.roots)
		goto done;
	c.build->cache = cache;
	c.build->gather_trim_bytes = models_load_trim_bytes();
	if (!models_load_weapons_make(&c, first, count, map_name))
	{
		if (c.scratch.b.failed)
			platform_log("h2 models at load: %s: the weapons' and vehicles' tags did not fit their room (%lu KB)", map_name,
				(limit - at) / 1024);
		goto done;
	}
	h2_load_tags_room_used(make, c.scratch.b.size);
	for (i = first; i < first + count; i++)
	{
		if (!read32(models_load_instance(&c, i) + 20))
		{
			h2_load_tags_place(make, i, c.base + c.roots[i]);
			placed++;
		}
	}
	/* (as the bipeds': the table checked with the map's tags, registered after) */
	if (!h2_load_tags_buffers_add(c.base + c.vertex_table, c.buffers, c.base + c.vertex_table + 12UL * (unsigned long)(c.buffers + 1), c.buffers))
	{
		platform_log("h2 models at load: %s: the weapons' buffer table could not be kept", map_name);
		goto done;
	}
	made = TRUE;
	models_load_made = TRUE;
	platform_log("h2 models at load: %s: the objects', weapons' and vehicles' models, graphs and collision models (%ld objects, %ld weapons, "
		"%ld vehicles) made from the MCC map: %ld tags placed, %lu KB, %.0f ms", map_name, c.build->object_count, h2w.count, h2v.count,
		placed, (c.scratch.b.size - at) / 1024, (double)(vita_host_time_us() - started) / 1000.0);
done:
	h2_weapons_dispose();
	models_load_weapons_build_free(c.build);
	/* (the region's memory the tag cache's: not freed) */
	memset(&c.scratch, 0, sizeof(c.scratch));
	free(c.roots);
	free(c.build);
	h2_cache_close(cache);
	return made;
}

/* (HALO_H2_MODELS_AT_LOAD=1, a level made with its models) the weapons' and
vehicles' models and the weapons' graphs made again, after a copy of the
loaded tags (the vehicles' graphs copy the kit's), and compared with the
level's */
H2_ALIGN static void models_load_weapons_check(char const *map_name, struct h2_cache *cache, unsigned char *base)
{
	struct models_load_context c;
	struct models_compare m;
	unsigned long long started = vita_host_time_us();
	unsigned long copied = (unsigned long)TAG_CACHE_SIZE;
	long i;

	memset(&c, 0, sizeof(c));
	memset(&m, 0, sizeof(m));
	c.r = &c.scratch;
	c.whole = TRUE;
	c.base = base;
	c.scratch_at = copied;
	c.scratch.link = (unsigned long)base;
	c.instances = (unsigned char *)read32(c.base);
	c.tag_count = (long)read32(c.base + 12);
	c.build = calloc(1, sizeof(*c.build));
	c.roots = calloc((size_t)(c.tag_count + 1), sizeof(*c.roots));
	if (!c.build || !c.roots || !bytes_put(&c.scratch.b, base, copied))
	{
		platform_log("h2 models at load: %s: no memory to check the weapons' models", map_name);
		goto done;
	}
	c.build->cache = cache;
	if (!models_load_weapons_make(&c, 0, 0, map_name))
		goto done;
	m.c = &c;
	for (i = 0; i < c.tag_count; i++)
	{
		if (c.roots[i])
			models_compare_tag(&m, &c, i, models_load_group(&c, i));
	}
	platform_log("h2 models at load: %s: the objects', weapons' and vehicles' %ld models, graphs and collision models (%ld objects, %ld weapons, "
		"%ld vehicles) made again from the MCC map in %.0f ms (%lu KB): %s - %lu bytes compared, %lu differ, %ld of %ld tags%s%s", map_name,
		m.tags, c.build->object_count, h2w.count, h2v.count,
		(double)(vita_host_time_us() - started) / 1000.0, (c.scratch.b.size - copied) / 1024, m.differ ? "DIFFERENT" : "the same as the level's",
		m.bytes, m.differ, m.tags_differ, m.tags, m.first[0] ? "; first: " : "", m.first);
done:
	h2_weapons_dispose();
	models_load_weapons_build_free(c.build);
	bytes_free(&c.scratch.b);
	free(c.roots);
	free(c.build);
}

/* (h2_level.c) a level's tags loaded (cache_files.c): with
HALO_H2_MODELS_AT_LOAD=1 its bipeds' models, graphs and collision models
made again from its MCC map and compared with the level's */
void h2_models_map_loaded(char const *map_name)
{
	char const *setting = getenv("HALO_H2_MODELS_AT_LOAD");
	char const *directory = map_name ? h2_maps_directory_of(map_name) : NULL;
	struct models_load_context c;
	struct h2_biped_host host;
	struct h2_bipeds *bipeds = NULL;
	struct h2_cache *cache;
	struct models_compare m;
	char why[160], name[300];
	unsigned long group;
	unsigned long long started;
	long first = NONE, count, i;

	if (models_load_made)
	{
		models_load_made = FALSE;
		return;
	}
	if (!setting || strcmp(setting, "1") || !directory || !h2_level_is_halo2_map(directory, map_name))
		return;
	started = vita_host_time_us();
	if (!(cache = h2_cache_open(directory, map_name, why, sizeof(why))))
	{
		platform_log("h2 models at load: %s.map cannot be read: %s", map_name, why);
		return;
	}
	memset(&c, 0, sizeof(c));
	c.r = &c.scratch;
	c.base = (unsigned char *)physical_memory_get_tag_cache_base_address();
	c.scratch_at = (unsigned long)TAG_CACHE_SIZE;
	c.scratch.link = (unsigned long)c.base + c.scratch_at;
	c.instances = (unsigned char *)read32(c.base);
	c.tag_count = (long)read32(c.base + 12);
	c.build = calloc(1, sizeof(*c.build));
	c.roots = calloc((size_t)(c.tag_count + 1), sizeof(*c.roots));
	if (!c.build || !c.roots)
		goto done;
	c.build->cache = cache;
	models_load_host_make(&host, &c);
	bipeds = h2_bipeds_gather(&host);
	count = h2_bipeds_tag_count(bipeds);
	first = models_load_bipeds_first(&c, bipeds, map_name);
	if (first == NONE)
	{
		platform_log("h2 models at load: %s: no bipeds' tags to compare (%ld made again)", map_name, count);
		goto done;
	}
	/* (the level's own made at load: nothing to compare) */
	for (i = first; i < first + count; i++)
		if (!read32(models_load_instance(&c, i) + 20))
			goto done;
	if (!models_load_bipeds_write(&c, bipeds, first, NULL, NULL))
	{
		platform_log("h2 models at load: %s: the bipeds' models could not be made again (%lu KB)", map_name, c.scratch.b.size / 1024);
		goto done;
	}
	/* (what the effects pass wrote into the level's collision models: as the
	record would carry it, written into the made-again ones) */
	for (i = first; i < first + count; i++)
	{
		if (models_load_group(&c, i) == 'coll')
			models_record_self_apply(&c, i);
	}
	memset(&m, 0, sizeof(m));
	m.c = &c;
	for (i = 0; i < count; i++)
	{
		unsigned long before_bytes = m.bytes, before_differ = m.differ;
		unsigned long size;
		unsigned char const *loaded, *made;

		/* (h2_bipeds_write numbered them the level's) */
		if (!h2_bipeds_tag_describe(bipeds, first + i, &group, name, sizeof(name)))
			continue;
		size = group == 'mode' ? 0xE8 : group == 'coll' ? 0x298 : group == 'antr' ? 0x80 : 0;
		if (!size)
			continue;
		m.tags++;
		m.tag = models_load_name(&c, first + i);
		{
			unsigned long at = models_load_offset_of(&c, read32(models_load_instance(&c, first + i) + 20));

			loaded = at ? c.base + at : NULL;
		}
		made = c.roots[first + i] ? models_load_at(&c, c.roots[first + i]) : NULL;
		if (!loaded || !made)
		{
			m.tags_differ++;
			models_compare_note(&m, made ? "the level's tag has no data" : "not made again", 0);
			continue;
		}
		models_compare_struct(&m, halo_tag_layout_root(group), loaded, made, size, 0, 0);
		if (group == 'mode')
			models_compare_model_buffers(&m, loaded, made);
		if (m.differ != before_differ)
			m.tags_differ++;
		if (getenv("HALO_H2_MODELS_AT_LOAD_LOG"))
			platform_log("h2 models at load: %c%c%c%c %s: %lu bytes, %lu differ%s%s", (int)(group >> 24), (int)(group >> 16 & 255),
				(int)(group >> 8 & 255), (int)(group & 255), m.tag, m.bytes - before_bytes, m.differ - before_differ,
				m.first[0] ? "; first: " : "", m.first);
	}
	platform_log("h2 models at load: %s: the bipeds' %ld models, graphs and collision models made again from the MCC map in %.0f ms "
		"(%lu KB): %s - %lu bytes compared, %lu differ, %ld of %ld tags%s%s; %lu bytes of effects' references the effects "
		"pass wrote after (h2_effects.c)%s%s", map_name, m.tags,
		(double)(vita_host_time_us() - started) / 1000.0, c.scratch.b.size / 1024, m.differ ? "DIFFERENT" : "the same as the level's",
		m.bytes, m.differ, m.tags_differ, m.tags, m.first[0] ? "; first: " : "", m.first, m.effect_bytes,
		m.effect_first[0] ? ", first: " : "", m.effect_first);
	models_load_weapons_check(map_name, cache, c.base);
done:
	if (bipeds)
		h2_bipeds_free(bipeds);
	bytes_free(&c.scratch.b);
	free(c.roots);
	free(c.build);
	h2_cache_close(cache);
}
