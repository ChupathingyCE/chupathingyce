/*
TAG_VALIDATE.C

A map's tags checked against their groups' schemas (tag_schema.h) as they
load, before anything else reads them (scenario_tags_load, and each
structure bsp as it loads: cache_files.c), so that the game can trust what
it reads in them as it trusted its own maps.

The game reads a map's tag data straight into the tag cache and uses it as
its structures: every pointer, count, index and enum in it is the map's,
and the game writes runtime values into tags as it runs. So each tag is
walked once through its group's schema:
- every block's and data's bytes must lie in the tags (or the bsp) and
  overlap no other's, which a map of the game's tools never does: a map
  whose tags overlap could have a runtime value the game writes to one tag
  change a pointer in another. Each one's bytes are marked as they are
  checked (a bit a byte, the claims below), so that the walk also never
  visits a byte twice: a map whose blocks point at each other cannot make
  it take longer than its size. The one overlap allowed is a shared one
  (shared_extent_overlap): Halo CE+'s maps store bytes that two tags,
  blocks or data have alike once, and point both at them. A root, block or
  data may start where an earlier one starts and read its bytes as the
  same structure or data, as long as nothing the walk corrects changes
  them (read as another, a check of one could write in bytes the other
  has already been checked to read). Where the
  loader gives the walk room past the tags (tag_validate_room_proc: a
  Custom Edition or HaloMD map's, whose tools overlapped tags' strings,
  fonts and blocks), one that overlaps another's any other way is copied
  there and walked as its own;
- counts are cut to what the game has room for, indices, enums and tags
  past what they name are corrected, runtime values reset (tag_schema.h);
- then, once every tag has been through it, the checks that look at other
  tags or at graphs (a bsp's nodes, a model's) run, and a tag's name that
  lies in any tag's bytes, where what is written could end it elsewhere,
  is none.
A map is refused if any pointer is wrong; anything else is corrected and
logged, and the map plays.

The validator is the game's only reader of a map that is not the game's own
data (a map made by other tools, downloaded, or converted from another
format): it reads nothing but the tags it is given, so tools/map_validate.c
runs it alone on a map file.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "cache/physical_memory_map.h"
#include "tag_schema.h"
#include "stubbs_mode.h"
#include "mcc_tag_validate.h" /* MCC callbacks have an independent context. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* NULL for Xbox/Custom Edition and in the asset-free standalone validator. */
struct mcc_validation_dispatch const *mcc_validation_callbacks;

unsigned long (*tag_validate_room_proc)(unsigned long size);

/* ---------- constants */

enum
{
	TAG_HEADER_SIGNATURE = 'tags',
	STRUCTURE_BSP_HEADER_SIGNATURE = 'sbsp',
	STRUCTURE_BSP_GROUP_TAG = 'sbsp',

	/* a vertex or index buffer in a header (a D3DResource: Common, Data,
	Lock) */
	BUFFER_SIZE = 12,

	/* how deep blocks go in a schema (the deepest are 6 or 7) */
	MAXIMUM_VALIDATION_DEPTH = 32,
	/* the longest a tag's name is, with its terminator (Halo's tools' paths:
	the game's buffers for them, game_state.c's, objects.c's, are this
	size) */
	MAXIMUM_TAG_NAME_LENGTH = 256,

	/* how many corrections are logged one by one, for each map or bsp */
	MAXIMUM_LOGGED_CORRECTIONS = 64,
	MAXIMUM_MESSAGE_LENGTH = 512,

	/* the claims: a bit for each byte of the largest tag cache a layout
	has (a Custom Edition map's: cache_files.c's CE_TAG_CACHE_SIZE; on a
	64-bit desktop, OpenSauce's, 0x02280000: tag_schema_opensauce.c) */
	CLAIM_BITS = 32,
#if defined(HALO_CUSTOM_EDITION) && defined(HALO_64BIT) && !defined(HALO_CONSOLE)
	CLAIM_CACHE_SIZE = TAG_CACHE_SIZE > 0x02280000 ? TAG_CACHE_SIZE : 0x02280000,
#elif defined(HALO_CUSTOM_EDITION)
	CLAIM_CACHE_SIZE = TAG_CACHE_SIZE > 0x01700000 ? TAG_CACHE_SIZE : 0x01700000,
#else
	CLAIM_CACHE_SIZE = TAG_CACHE_SIZE,
#endif
	CLAIM_WORDS = CLAIM_CACHE_SIZE / CLAIM_BITS,

	/* the shared extents' tables (shared_extent_claim): one for the tags,
	one for the bsp loaded, each a power of two of entries; how far one is
	searched for a place; how many times its region's size the shared bytes
	walked again may come to (a map whose blocks are all one cannot make
	the walk take much longer than its size); and how many shared bytes'
	runs each map (and each bsp) may have, checked at the end */
	SHARED_EXTENT_TAG_ENTRIES = 1 << 16,
	SHARED_EXTENT_BSP_ENTRIES = 1 << 14,
	MAXIMUM_SHARED_EXTENT_PROBES = 64,
	MAXIMUM_SHARED_WALK_FACTOR = 4,
	MAXIMUM_SHARED_OVERLAPS = 8192,
	/* how many of them may be shared as another structure or data (Halo
	CE+'s maps have one), each checked again after every check */
	MAXIMUM_SHARED_AS_OTHERS = 64,
	/* how many times tags whose blocks overlap are checked, until nothing
	more is corrected (tag_validate_tags_layout) */
	MAXIMUM_OVERLAP_PASSES = 4,
	/* how many of the roots, blocks and data copied apart are logged one by
	one (overlap_copied) */
	MAXIMUM_LOGGED_COPIES = 16,
	/* the most roots, blocks and data copied apart, and their most bytes
	in all (overlap_copied): the same on every platform, whatever room the
	loader has, so that a map's checked tags are the same everywhere (one
	whose copies there is no room for is refused) */
	MAXIMUM_COPIES = 4096,
	MAXIMUM_COPIED_BYTES = 0x400000,
};

/* the passes of the walk over a tag */
enum
{
	/* blocks' and data's counts and bytes */
	_pass_extents,
	/* indices, enums, tags, strings and runtime values */
	_pass_values,
	/* (into each block's elements, which take the same passes) */
	/* the checks (tag_schema_check_proc), after every tag's other passes */
	_pass_checks,
};

/* ---------- structures */

/* a tag in the header's table (cache_files.c's struct
cache_file_tag_instance) */
struct tag_validate_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	long tag_index;
	XPTR(char) name;
	XPTR(void) base_address;
	unsigned long unused[2];
};

/* the tags' header (cache_files.c's struct cache_file_tag_header) */
struct tag_validate_header
{
	XPTR(struct tag_validate_instance) instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	XPTR(byte) vertex_buffers;
	long index_buffer_count;
	XPTR(byte) index_buffers;
	unsigned long signature;
};

/* a bsp's header (cache_files.c's struct cache_file_structure_bsp_header):
its second buffers are its lightmaps' vertex buffers
(structure_bsp_header_register_vertex_buffers), which the validator keeps
as a header's index buffers */
struct tag_validate_structure_bsp_header
{
	XPTR(void) base_address;
	long vertex_buffer_count;
	XPTR(byte) vertex_buffers;
	long index_buffer_count;
	XPTR(byte) index_buffers;
	unsigned long signature;
};

typedef char verify_tag_validate_instance_size[sizeof(struct tag_validate_instance) == 0x20 ? 1 : -1];
typedef char verify_tag_validate_header_size[sizeof(struct tag_validate_header) == 0x24 ? 1 : -1];
/* Halo PC's tag header (cache_files.c's struct cache_file_ce_tag_header):
the Xbox's first four fields, then its model data's, then the signature */
enum
{
	PC_TAG_HEADER_SIZE = 0x28,
	PC_TAG_HEADER_SIGNATURE_OFFSET = 0x24,
};

static unsigned long pc_header_signature(
	struct tag_validate_header const *header)
{
	unsigned long signature;

	memcpy(&signature, (byte const *)header + PC_TAG_HEADER_SIGNATURE_OFFSET, sizeof(signature));
	return signature;
}

typedef char verify_tag_validate_structure_bsp_header_size[
	sizeof(struct tag_validate_structure_bsp_header) == 0x18 ? 1 : -1];

/* a tag's root, block or data, as claimed: where in the tag cache, how many
bytes, and what they were read as (the group's or block's definition, or
the data's field). One that starts where it starts may share its bytes
(shared_extent_overlap) */
struct shared_extent
{
	unsigned int offset;
	unsigned int size;
	void const *kind;
	boolean is_data;
	/* the longest of those that start here: its size and kind */
	boolean longest_is_data;
	unsigned int longest_size;
	void const *longest_kind;
};

/* bytes two extents share, and what they were when they were first shared:
nothing the walk corrects may change them (shared_overlaps_unchanged) */
struct shared_overlap
{
	unsigned int offset;
	unsigned int size;
	unsigned long hash;
	/* the tag that shares them, for the message */
	long tag_index;
	/* bytes walked again as another's (overlap_walked), not shared: what
	they were is not kept, but nothing is copied apart whose pointer is in
	them (overlap_copied) */
	boolean walked;
	/* shared as another structure or data than the first's (an Xbox map's,
	Halo CE+'s: shared_extent_overlap), checked again after every check
	(shared_as_others_unchanged) */
	boolean as_other;
};

/* an element being walked: its bytes, its definition, and where it is (for
messages) */
struct tag_validation_frame
{
	byte *base;
	struct tag_schema_definition const *definition;
	char const *field_name;
	long element_index;
};

struct tag_validation
{
	/* what the pointers being checked may point into */
	byte *region;
	unsigned long region_size;
	/* the header whose vertex and index buffers models (or the bsp) use */
	byte *vertex_buffers;
	long vertex_buffer_count;
	byte *index_buffers;
	long index_buffer_count;

	long tag_index;
	short pass;
	boolean refused;
	long corrections;
	/* the bytes of shared roots, blocks and data walked again
	(shared_extent_overlap) */
	unsigned long shared_size;

	struct tag_validation_frame frames[MAXIMUM_VALIDATION_DEPTH];
	short depth;
	/* the structure the field being checked is in (TAG_SCHEMA_STRUCTURE) */
	byte *structure;
	char const *field_name;
};

/* ---------- globals */

static struct
{
	/* the tags last checked (tag_validate_tags) */
	struct tag_validate_header *header;
	/* their layout, and its tag cache's size (no more than the claims') */
	struct tag_schema_layout const *layout;
	unsigned long tag_cache_size;
	long tag_data_size;
	long file_length;
	char map_name[64];
	long corrections;
	/* the roots, blocks and data that overlapped others' and were walked as
	their own (_tag_schema_layout_overlaps_walked_bit) */
	long overlaps;
	/* some of them could not be remembered (overlap_walked): nothing more
	is copied apart (overlap_copied) */
	boolean overlaps_unremembered;
} tag_validate_globals = { NULL, &tag_schema_xbox_layout, TAG_CACHE_SIZE };

/* the tags' and the loaded bsp's shared extents (shared_extent_claim): on
the console made with the claims (claims_ready), where without them nothing
is shared, and a map that shares is refused as one that overlaps */
#ifndef HALO_XBOX_CONSOLE
static struct shared_extent tag_validate_shared_tags[SHARED_EXTENT_TAG_ENTRIES];
static struct shared_extent tag_validate_shared_bsp[SHARED_EXTENT_BSP_ENTRIES];
static struct shared_overlap tag_validate_overlaps[MAXIMUM_SHARED_OVERLAPS];
#else
static struct shared_extent *tag_validate_shared_tags;
static struct shared_extent *tag_validate_shared_bsp;
static struct shared_overlap *tag_validate_overlaps;
#endif
/* the shared bytes' runs: the tags', then the loaded bsp's from
tag_overlap_count on */
static long tag_validate_overlap_count;
static long tag_validate_tag_overlap_count;
/* the shared overlaps read as another structure or data
(shared_extent_overlap): their places in tag_validate_overlaps, and how many
of them are the tags' (the rest, a bsp's) */
static long tag_validate_as_others[MAXIMUM_SHARED_AS_OTHERS];
static long tag_validate_as_other_count;
static long tag_validate_tag_as_other_count;

/* a bit for each byte of the tag cache that a tag's root, block or data
holds */
#ifndef HALO_XBOX_CONSOLE
static unsigned long tag_validate_claims[CLAIM_WORDS];
#else
/* port: on the console, 2.75 MB of the heap, made at the first map's load
on a console above its memory baseline (claims_ready, halo_console_memory.h:
128 MB or more on the Xbox). A stock 64 MB console has no room for them:
its maps load unchecked, as the Xbox game's did, and tag_validate_skipped
says so to the bsps' check */
static unsigned long *tag_validate_claims;
static boolean tag_validate_skipped;

static boolean claims_ready(
	void)
{
	static boolean refused;

	if (tag_validate_claims)
		return TRUE;
	if (refused)
		return FALSE;
	refused = TRUE;
	if (!HALO_CONSOLE_MEMORY_EXTRAS(FALSE, TRUE))
	{
		error(_error_silent, "maps load without the tag checks: a 64 MB console has no room for them (%lu KB)",
			(unsigned long)(CLAIM_WORDS * sizeof(unsigned long)) >> 10);
		return FALSE;
	}
	tag_validate_claims = malloc(CLAIM_WORDS * sizeof(unsigned long));
	tag_validate_shared_tags = malloc(SHARED_EXTENT_TAG_ENTRIES * sizeof(*tag_validate_shared_tags));
	tag_validate_shared_bsp = malloc(SHARED_EXTENT_BSP_ENTRIES * sizeof(*tag_validate_shared_bsp));
	tag_validate_overlaps = malloc(MAXIMUM_SHARED_OVERLAPS * sizeof(*tag_validate_overlaps));
	if (!tag_validate_shared_tags || !tag_validate_shared_bsp || !tag_validate_overlaps)
	{
		/* (then nothing is shared: shared_extent_find) */
		free(tag_validate_shared_tags);
		free(tag_validate_shared_bsp);
		free(tag_validate_overlaps);
		tag_validate_shared_tags = NULL;
		tag_validate_shared_bsp = NULL;
		tag_validate_overlaps = NULL;
	}
	if (!tag_validate_claims)
	{
		error(_error_silent, "maps load without the tag checks: no memory for them (%lu KB)",
			(unsigned long)(CLAIM_WORDS * sizeof(unsigned long)) >> 10);
		return FALSE;
	}
	return TRUE;
}
#endif


/* the tags that share an earlier tag's root (tag_validate_tags): a bit for
each tag in the table (at most UNSIGNED_SHORT_MAX), walked once, as that one */
static unsigned long tag_validate_shared_roots[0x10000 / CLAIM_BITS];

/* the roots, blocks and data copied apart from bytes another's overlap, in
the room past the tags (overlap_copied): where the copies are, as offsets
in the tag cache, and how many bytes and copies. Kept through every check
of the tags (tag_validate_tags_layout), as the copies are */
static struct
{
	unsigned long first;
	unsigned long end;
	unsigned long bytes;
	long count;
	/* each copy's offset and end, in the order made (and so in the tag
	cache's: the room only moves up) */
	unsigned long extents[MAXIMUM_COPIES][2];
} tag_validate_copies;

/* (in the Xbox address space on the 64-bit builds, where the tags'
pointers are Xbox addresses: xbox_address.h) */
#ifdef HALO_64BIT
static char *tag_validate_empty_name;
#else
static char const tag_validate_empty_name[] = "";
#endif

/* ---------- prototypes */

static void validate_element(struct tag_validation *validation, byte *base,
	struct tag_schema_definition const *definition, char const *field_name, long element_index);
static void validate_fields(struct tag_validation *validation, byte *base,
	struct tag_schema_definition const *definition);

/* ---------- private code */

/* the tags' table */
static struct tag_validate_instance *instances_get(
	struct tag_validate_header *header)
{
	return xbox_pointer(header->instances);
}

/* a vertex or index buffer's Data: its bytes' address before it is
registered (an Xbox address in the tags) */
static void *buffer_data_address(
	void const *buffer)
{
	return xbox_pointer(*(XPTR(void) const *)((byte const *)buffer + 4));
}

static struct tag_schema_group const *schema_group_get(
	unsigned long group_tag)
{
	struct tag_schema_group const *const *list;

	for (list = tag_validate_globals.layout->group_lists; *list; list++)
	{
		struct tag_schema_group const *group;

		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group;
		}
	}

	return NULL;
}

/* the layout's group taken for group_tag (group_aliases), or 0 */
static unsigned long group_alias_get(
	unsigned long group_tag)
{
	unsigned long const (*alias)[2] = tag_validate_globals.layout->group_aliases;

	for (; alias && (*alias)[0]; alias++)
	{
		if ((*alias)[0] == group_tag)
			return (*alias)[1];
	}

	return 0;
}

static char *tag_to_text(
	unsigned long group_tag,
	char text[5])
{
	short index;

	for (index = 0; index < 4; index++)
	{
		char c = (char)(group_tag >> (8 * (3 - index)));

		text[index] = c >= ' ' && c < 0x7F ? c : '?';
	}
	text[4] = 0;

	return text;
}

/* the name of tag_index, if it has one in the tags */
static char const *tag_name(
	long tag_index)
{
	struct tag_validate_header *header = tag_validate_globals.header;
	short absolute_index = (short)tag_index;
	char const *name;
	unsigned long offset;

	if (!header || absolute_index < 0 || absolute_index >= header->tag_count)
		return "?";
	name = xbox_pointer(instances_get(header)[absolute_index].name);
	if (name == tag_validate_empty_name)
		return name;
	offset = (unsigned long)(POINTER_BITS(name) - POINTER_BITS(header));
	if (POINTER_BITS(name) < POINTER_BITS(header) || offset >= (unsigned long)tag_validate_globals.tag_data_size ||
		!memchr(name, 0, tag_validate_globals.tag_data_size - offset))
	{
		return "?";
	}

	return name;
}

/* where the field being checked is: tag 'name' (grou): block[3].field */
static void validation_where(
	struct tag_validation *validation,
	char *where,
	size_t size)
{
	struct tag_validate_header *header = tag_validate_globals.header;
	short absolute_index = (short)validation->tag_index;
	char group[5] = "?";
	size_t length;
	short depth;

	if (header && absolute_index >= 0 && absolute_index < header->tag_count)
		tag_to_text(instances_get(header)[absolute_index].group_tag, group);
	snprintf(where, size, "tag '%s' (%s): ", tag_name(validation->tag_index), group);
	for (depth = 1; depth < validation->depth; depth++)
	{
		length = strlen(where);
		snprintf(where + length, size - length, "%s[%ld].",
			validation->frames[depth].field_name, validation->frames[depth].element_index);
	}
	length = strlen(where);
	snprintf(where + length, size - length, "%s", validation->field_name ? validation->field_name : "");
}

static void validation_message(
	struct tag_validation *validation,
	char const *what,
	char const *format,
	va_list arguments)
{
	char message[MAXIMUM_MESSAGE_LENGTH];
	size_t length;

	snprintf(message, sizeof(message), "the map '%s' %s: ", tag_validate_globals.map_name, what);
	length = strlen(message);
	validation_where(validation, message + length, sizeof(message) - length);
	length = strlen(message);
	snprintf(message + length, sizeof(message) - length, " ");
	length = strlen(message);
	vsnprintf(message + length, sizeof(message) - length, format, arguments);
	tag_validate_report(message);
}

/* the end (an offset in the tag cache) of the copy made past the tags that
the byte at offset is in (overlap_copied), or offset itself if it is in
none: only what the validator wrote there is a tag's */
static unsigned long copy_end(
	unsigned long offset)
{
	long low = 0, high = tag_validate_copies.count;

	while (low < high)
	{
		long middle = low + (high - low) / 2;

		if (tag_validate_copies.extents[middle][1] <= offset)
			low = middle + 1;
		else
			high = middle;
	}
	if (low < tag_validate_copies.count && tag_validate_copies.extents[low][0] <= offset)
		return tag_validate_copies.extents[low][1];

	return offset;
}

/* whether size bytes at address lie in the region being checked (or, for
the tags, in one of the copies made past them: overlap_copied) */
static boolean region_contains(
	struct tag_validation *validation,
	void const *address,
	unsigned long size)
{
	unsigned long offset = (unsigned long)(POINTER_BITS(address) - POINTER_BITS(validation->region));

	if (POINTER_BITS(address) >= POINTER_BITS(validation->region) &&
		offset <= validation->region_size &&
		size <= validation->region_size - offset)
	{
		return TRUE;
	}
	return validation->region == (byte *)tag_validate_globals.header &&
		POINTER_BITS(address) >= POINTER_BITS(validation->region) &&
		size <= copy_end(offset) - offset;
}

/* marks size bytes at address (in the tag cache, as the region is) as a
tag's: FALSE if any of them already were */
static boolean claim(
	void const *address,
	unsigned long size)
{
	byte const *tag_cache = (byte const *)tag_validate_globals.header;
	unsigned long first = (unsigned long)((byte const *)address - tag_cache);
	unsigned long end = first + size;
	unsigned long bit;

	if (!size)
		return TRUE;
	if (first > tag_validate_globals.tag_cache_size || size > tag_validate_globals.tag_cache_size - first)
		return FALSE;

	/* (whole words where they can be) */
	bit = first;
	while (bit < end)
	{
		unsigned long *word = &tag_validate_claims[bit / CLAIM_BITS];

		if (bit % CLAIM_BITS == 0 && end - bit >= CLAIM_BITS)
		{
			if (*word)
				return FALSE;
			*word = 0xFFFFFFFFUL;
			bit += CLAIM_BITS;
		}
		else
		{
			unsigned long mask = 1UL << (bit % CLAIM_BITS);

			if (*word & mask)
				return FALSE;
			*word |= mask;
			bit++;
		}
	}

	return TRUE;
}

/* unclaims the size bytes at the tag cache's offset first (a bsp's, as
another loads in its place) */
static void unclaim(
	unsigned long first,
	unsigned long size)
{
	unsigned long end = first + size;
	unsigned long bit;

	if (first > tag_validate_globals.tag_cache_size || size > tag_validate_globals.tag_cache_size - first)
		return;
	for (bit = first; bit < end; )
	{
		if (bit % CLAIM_BITS == 0 && end - bit >= CLAIM_BITS)
		{
			tag_validate_claims[bit / CLAIM_BITS] = 0;
			bit += CLAIM_BITS;
		}
		else
		{
			tag_validate_claims[bit / CLAIM_BITS] &= ~(1UL << (bit % CLAIM_BITS));
			bit++;
		}
	}
}

static void shared_extents_clear(
	struct shared_extent *entries,
	unsigned long count)
{
	if (entries)
		memset(entries, 0, count * sizeof(*entries));

	return;
}

/* the entry for offset (in the tag cache) in its table, the tags' or the
loaded bsp's past them: where it is, or the free place it would go (NULL
when there is neither, near enough) */
static struct shared_extent *shared_extent_find(
	unsigned long offset)
{
	boolean in_tags = offset < (unsigned long)tag_validate_globals.tag_data_size;
	struct shared_extent *entries = in_tags ? tag_validate_shared_tags : tag_validate_shared_bsp;
	unsigned long mask = (in_tags ? SHARED_EXTENT_TAG_ENTRIES : SHARED_EXTENT_BSP_ENTRIES) - 1;
	unsigned long index;
	short probe;

	if (!entries)
		return NULL;
	index = (unsigned long)((offset * 2654435761UL) & mask);
	for (probe = 0; probe < MAXIMUM_SHARED_EXTENT_PROBES; probe++)
	{
		struct shared_extent *entry = &entries[(index + probe) & mask];

		if (!entry->kind || entry->offset == offset)
			return entry;
	}

	return NULL;
}

/* claims size bytes at address read as kind, and remembers them for a later
one that starts there (shared_extent_overlap); FALSE if any of the bytes
already were claimed */
static boolean shared_extent_claim(
	void const *address,
	unsigned long size,
	void const *kind,
	boolean is_data)
{
	unsigned long offset = (unsigned long)(POINTER_BITS(address) - POINTER_BITS(tag_validate_globals.header));
	struct shared_extent *entry;

	if (!claim(address, size))
		return FALSE;
	entry = size ? shared_extent_find(offset) : NULL;
	/* (one that cannot be remembered is never shared) */
	if (entry && !entry->kind)
	{
		entry->offset = (unsigned int)offset;
		entry->size = (unsigned int)size;
		entry->kind = kind;
		entry->is_data = is_data;
		entry->longest_size = (unsigned int)size;
		entry->longest_kind = kind;
		entry->longest_is_data = is_data;
	}

	return TRUE;
}

/* whether a runtime value (a reset field: what the game writes as it runs)
of a structure of definition at base lies in the bytes [first, end) */
static boolean fields_reset_in(
	struct tag_schema_definition const *definition,
	unsigned long base,
	unsigned long first,
	unsigned long end,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth > MAXIMUM_VALIDATION_DEPTH)
		return TRUE;
	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		short index;

		if (field->type != _tag_schema_reset && field->type != _tag_schema_struct)
			continue;
		for (index = 0; index < field->count; index++)
		{
			unsigned long at = base + (unsigned long)field->offset + (unsigned long)(index * field->size);

			if (at >= end || at + (unsigned long)field->size <= first)
				continue;
			if (field->type == _tag_schema_reset ||
				fields_reset_in(field->definition, at, first, end, depth + 1))
			{
				return TRUE;
			}
		}
	}

	return FALSE;
}

/* whether any runtime value of an extent (its elements', or its root's)
lies in its first size bytes; data has none */
static boolean extent_reset_in(
	void const *kind,
	boolean is_data,
	unsigned long size)
{
	struct tag_schema_definition const *definition = kind;
	unsigned long element;

	if (is_data)
		return FALSE;
	for (element = 0; element < size; element += (unsigned long)definition->size)
	{
		if (fields_reset_in(definition, element, 0, size, 0))
			return TRUE;
	}

	return FALSE;
}

static unsigned long bytes_hash(
	byte const *bytes,
	unsigned long size)
{
	unsigned long hash = 2166136261UL;
	unsigned long index;

	for (index = 0; index < size; index++)
		hash = ((hash ^ bytes[index]) * 16777619UL) & 0xFFFFFFFFUL;

	return hash;
}

/* whether size bytes at address read as kind, which overlap bytes already
claimed, share them with an earlier root, block or data as Halo CE+'s maps
do: the overlap must start where that one starts, and anything past the
longest that starts there must be no one's (it is claimed now), and it
must be read as the same structure as the first and the longest of them
(a block of the same elements, or the same data): the two read and write
the same fields, and what one's check writes is what the other's reads
as checked. The bytes are remembered, and nothing the walk
corrects may change them (shared_overlaps_unchanged), so that each reads
what was checked for it. The one that shares is walked as its own */
static boolean shared_extent_overlap(
	struct tag_validation *validation,
	void const *address,
	unsigned long size,
	void const *kind,
	boolean is_data)
{
	unsigned long offset = (unsigned long)(POINTER_BITS(address) - POINTER_BITS(tag_validate_globals.header));
	struct shared_extent *entry = size ? shared_extent_find(offset) : NULL;
	struct shared_overlap *overlap;
	unsigned long shared;
	unsigned long longest;

	boolean as_other = entry && (entry->kind != kind || entry->longest_kind != kind);

	/* (read as another structure or data than the first and the longest
	that start there, only where the loader gives no room to copy them
	apart instead, overlap_copied: an Xbox map's, Halo CE+'s; and then
	checked again after every check, shared_as_others_unchanged, as one's
	check could write in bytes the other's check reads after its own were
	checked, a string's terminator in a block's pointer) */
	if (!entry || !entry->kind || entry->offset != offset ||
		(as_other && (tag_validate_room_proc || tag_validate_as_other_count >= MAXIMUM_SHARED_AS_OTHERS)) ||
		tag_validate_overlap_count >= MAXIMUM_SHARED_OVERLAPS ||
		size > validation->region_size * MAXIMUM_SHARED_WALK_FACTOR - validation->shared_size)
	{
		return FALSE;
	}
	shared = MIN(size, (unsigned long)entry->size);
	longest = MIN(size, (unsigned long)entry->longest_size);
	if ((entry->kind != kind &&
			(extent_reset_in(entry->kind, entry->is_data, shared) || extent_reset_in(kind, is_data, shared))) ||
		(entry->longest_kind != kind &&
			(extent_reset_in(entry->longest_kind, entry->longest_is_data, longest) ||
				extent_reset_in(kind, is_data, longest))))
	{
		return FALSE;
	}
	if (size > longest)
	{
		if (!claim((byte const *)address + longest, size - longest))
			return FALSE;
		entry->longest_size = (unsigned int)size;
		entry->longest_kind = kind;
		entry->longest_is_data = is_data;
	}
	shared = longest;
	validation->shared_size += size;
	overlap = &tag_validate_overlaps[tag_validate_overlap_count++];
	overlap->offset = (unsigned int)offset;
	overlap->size = (unsigned int)shared;
	overlap->hash = bytes_hash(address, shared);
	overlap->tag_index = validation->tag_index;
	overlap->walked = FALSE;
	overlap->as_other = as_other;
	if (as_other)
		tag_validate_as_others[tag_validate_as_other_count++] = tag_validate_overlap_count - 1;

	return TRUE;
}

/* whether the bytes shared as another structure or data
(shared_extent_overlap) are still as they were when they were first shared:
checked before the checks and after each, so that no check reads through
bytes another's corrected after they were checked for it. The map is
refused if not */
static boolean shared_as_others_unchanged(
	struct tag_validation *validation)
{
	/* (a bsp's own: the tags' were checked as they were, and the game's
	runtime values are in none of them) */
	long index = validation->region == (byte *)tag_validate_globals.header ? 0 : tag_validate_tag_as_other_count;

	for (; index < tag_validate_as_other_count && !validation->refused; index++)
	{
		struct shared_overlap const *overlap = &tag_validate_overlaps[tag_validate_as_others[index]];
		byte const *bytes = (byte const *)tag_validate_globals.header + overlap->offset;

		if (bytes_hash(bytes, overlap->size) != overlap->hash)
		{
			tag_validate_refuse(validation, "shares %lu bytes at %08lx with another tag that reads them as another "
				"structure, and a correction changed them", (unsigned long)overlap->size,
				(unsigned long)XBOX_ADDRESS(bytes));
			return FALSE;
		}
	}

	return TRUE;
}

/* whether the walk may write to an element of definition as it checks it:
a runtime value it resets, a block's or data's definition it clears, what
a check of it may correct, or a value it corrects (validate_value: a
reference's name, tag and group, a tag index, a block index, an enum, a
string's terminator). Only an element it never writes to may be walked
where another's bytes are (overlap_walked): a correction there would change
bytes the other was already checked to read (a block's or data's pointer
or count), and a later check, or the game, would read through them
unchecked */
static boolean definition_writes(
	struct tag_schema_definition const *definition,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth > MAXIMUM_VALIDATION_DEPTH)
		return TRUE;
	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		/* (every field the schema lists is one the walk checks, and
		corrects where it is wrong; a structure's are its fields') */
		if (field->type != _tag_schema_struct || definition_writes(field->definition, depth + 1))
			return TRUE;
	}

	return FALSE;
}

/* whether an element of definition has a check (its structures' too): a
check may correct what it reads in the element's blocks, those with no
fields of their own too (a collision bsp's leaves) */
static boolean definition_checks(
	struct tag_schema_definition const *definition,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth > MAXIMUM_VALIDATION_DEPTH)
		return TRUE;
	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		if (field->type == _tag_schema_check ||
			(field->type == _tag_schema_struct && definition_checks(field->definition, depth + 1)))
		{
			return TRUE;
		}
	}

	return FALSE;
}

/* whether size bytes at address, which overlap bytes already claimed in a
way shared_extent_overlap does not let them, may be walked as their own: in
a layout whose tools overlapped them (_tag_schema_layout_overlaps_walked_bit:
Halo PC retail's), and within what the walk may take again (as shared
bytes are). They are claimed, and counted */
static boolean overlap_walked(
	struct tag_validation *validation,
	void const *address,
	unsigned long size,
	struct tag_schema_definition const *definition)
{
	byte const *tag_cache = (byte const *)tag_validate_globals.header;
	unsigned long first = (unsigned long)((byte const *)address - tag_cache);
	unsigned long bit;
	short depth;

	/* (not one the walk may write to, definition_writes, nor one in an
	element whose check may, definition_checks: what it writes would change
	what the other reads after it was checked) */
	for (depth = 0; depth < validation->depth; depth++)
	{
		if (definition_checks(validation->frames[depth].definition, 0))
			return FALSE;
	}
	if (!TEST_FLAG(tag_validate_globals.layout->flags, _tag_schema_layout_overlaps_walked_bit) ||
		definition_writes(definition, 0) ||
		first > tag_validate_globals.tag_cache_size || size > tag_validate_globals.tag_cache_size - first ||
		size > validation->region_size * MAXIMUM_SHARED_WALK_FACTOR - validation->shared_size)
	{
		return FALSE;
	}
	validation->shared_size += size;
	for (bit = first; bit < first + size; bit++)
		tag_validate_claims[bit / CLAIM_BITS] |= 1UL << (bit % CLAIM_BITS);
	tag_validate_globals.overlaps++;
	/* (remembered, so that nothing whose pointer is in them is copied
	apart: overlap_copied; or, with no room to, nothing more is. They take
	places of the shared overlaps' MAXIMUM_SHARED_OVERLAPS, which only a
	Halo PC map that copies apart reaches them in: past it, it shares and
	copies no more, and what overlaps is made none or refused) */
	if (tag_validate_overlap_count < MAXIMUM_SHARED_OVERLAPS)
	{
		struct shared_overlap *overlap = &tag_validate_overlaps[tag_validate_overlap_count++];

		overlap->offset = (unsigned int)first;
		overlap->size = (unsigned int)size;
		overlap->hash = 0;
		overlap->tag_index = validation->tag_index;
		overlap->walked = TRUE;
	}
	else
	{
		tag_validate_globals.overlaps_unremembered = TRUE;
	}

	return TRUE;
}

/* size bytes at address, read as kind, which overlap bytes already claimed
in a way shared_extent_overlap does not let them: copied to room of their
own past the tags, where the loader gives it (tag_validate_room_proc: no
more than MAXIMUM_COPIES of them, and MAXIMUM_COPIED_BYTES in all), and
claimed there, so that neither what the walk corrects nor what the game
writes in one changes what another reads (Halo PC retail's tools shared a
string's bytes with another's, a font's characters with another tag's).
The pointer to them, pointer_size bytes at pointer, is then the copy's:
never one in bytes another root, block or data shares or walks again
(shared_extent_overlap, overlap_walked), as it would change what that one
reads after it was checked. Their copy, or NULL: not copied (then they
are walked where they are, made none or refused, as before), or, with no
room for it, the map refused */
static void *overlap_copied(
	struct tag_validation *validation,
	void const *address,
	unsigned long size,
	void const *kind,
	boolean is_data,
	void const *pointer,
	unsigned long pointer_size)
{
	byte const *tag_cache = (byte const *)tag_validate_globals.header;
	unsigned long pointer_offset = (unsigned long)(POINTER_BITS(pointer) - POINTER_BITS(tag_cache));
	unsigned long room;
	unsigned long offset;
	byte *copy;
	long index;

	/* (what is copied depends on the tags alone, never on the room the
	loader has: the same on every platform) */
	if (!tag_validate_room_proc || validation->region != tag_cache || !size ||
		tag_validate_globals.overlaps_unremembered || tag_validate_copies.count >= MAXIMUM_COPIES ||
		size > MAXIMUM_COPIED_BYTES - tag_validate_copies.bytes)
	{
		return NULL;
	}
	/* (the pointer in the tags' bytes, and in none another shares: the
	tag index, or a root, block or data that is only its own) */
	if (POINTER_BITS(pointer) < POINTER_BITS(tag_cache) || pointer_offset > tag_validate_globals.tag_cache_size ||
		pointer_size > tag_validate_globals.tag_cache_size - pointer_offset)
	{
		return NULL;
	}
	for (index = 0; index < tag_validate_overlap_count; index++)
	{
		struct shared_overlap const *overlap = &tag_validate_overlaps[index];

		if (pointer_offset < (unsigned long)overlap->offset + overlap->size &&
			(unsigned long)overlap->offset < pointer_offset + pointer_size)
		{
			return NULL;
		}
	}
	/* (past the tags and every earlier copy, and in the tag cache: the
	room is the loader's, and is not trusted to be. A map whose copies it
	has no room for is refused, never loaded without them: its tags would
	be other than another platform's) */
	room = tag_validate_room_proc(size);
	copy = room ? xbox_pointer(room) : NULL;
	offset = (unsigned long)(POINTER_BITS(copy) - POINTER_BITS(tag_cache));
	if (!copy || POINTER_BITS(copy) < POINTER_BITS(tag_cache) || offset < validation->region_size ||
		offset < tag_validate_copies.end || offset > tag_validate_globals.tag_cache_size ||
		size > tag_validate_globals.tag_cache_size - offset ||
		!shared_extent_claim(copy, size, kind, is_data))
	{
		tag_validate_refuse(validation, "has %lu bytes at %08lx, which overlap another's, and no room in the tag "
			"cache to copy them apart", size, (unsigned long)XBOX_ADDRESS(address));
		return NULL;
	}
	memcpy(copy, address, size);
	if (!tag_validate_copies.end)
		tag_validate_copies.first = offset;
	tag_validate_copies.end = offset + size;
	tag_validate_copies.extents[tag_validate_copies.count][0] = offset;
	tag_validate_copies.extents[tag_validate_copies.count][1] = offset + size;
	tag_validate_copies.bytes += size;
	tag_validate_copies.count++;
	if (tag_validate_copies.count <= MAXIMUM_LOGGED_COPIES)
	{
		char message[MAXIMUM_MESSAGE_LENGTH];
		size_t length;

		snprintf(message, sizeof(message), "the map '%s' has its own copy made: ", tag_validate_globals.map_name);
		length = strlen(message);
		validation_where(validation, message + length, sizeof(message) - length);
		length = strlen(message);
		snprintf(message + length, sizeof(message) - length, " has %lu bytes at %08lx, which overlap another's",
			size, (unsigned long)XBOX_ADDRESS(address));
		tag_validate_report(message);
	}

	return copy;
}

/* size bytes at address, read as kind, which overlap bytes already claimed,
pointed to by pointer_size bytes at pointer: shared with the one they
overlap, read as the same structure or data (shared_extent_overlap), or
given a copy of their own (overlap_copied), *copy then. FALSE if neither
(and then never shared as another: a check of either could write in bytes
the other's check reads after its own were checked, a string's terminator
in another tag's block); the map may then have been refused */
static boolean overlap_kept(
	struct tag_validation *validation,
	void const *address,
	unsigned long size,
	void const *kind,
	boolean is_data,
	void const *pointer,
	unsigned long pointer_size,
	void **copy)
{
	*copy = NULL;
	if (shared_extent_overlap(validation, address, size, kind, is_data))
		return TRUE;
	*copy = overlap_copied(validation, address, size, kind, is_data, pointer, pointer_size);

	return *copy != NULL;
}

/* data whose bytes overlap others' in a way shared_extent_overlap does not
let them, in a layout whose tools overlapped them (Halo PC retail's: a
HaloMD map's strings point into other tags' bytes, where its engine read
whatever was there): none, as data is only bytes the game reads, which
another tag's runtime values or corrections may change under it. TRUE if
it was made none */
static boolean overlap_data_none(
	struct tag_validation *validation,
	struct tag_data *data)
{
	if (!TEST_FLAG(tag_validate_globals.layout->flags, _tag_schema_layout_overlaps_walked_bit))
		return FALSE;
	tag_validate_correct(validation, "has %ld bytes at %08lx, overlapping another's: none", data->size,
		(unsigned long)data->address);
	data->size = 0;
	data->address = XBOX_NULL;

	return TRUE;
}

/* whether the bytes shared since first are as they were when they were
first shared: if the walk corrected them for one, another reads what was
not checked for it */
static boolean shared_overlaps_unchanged(
	struct tag_validation *validation,
	long first)
{
	long index;

	for (index = first; index < tag_validate_overlap_count; index++)
	{
		struct shared_overlap const *overlap = &tag_validate_overlaps[index];
		byte const *bytes = (byte const *)tag_validate_globals.header + overlap->offset;

		if (overlap->walked)
			continue;
		if (bytes_hash(bytes, overlap->size) != overlap->hash &&
			TEST_FLAG(tag_validate_globals.layout->flags, _tag_schema_layout_overlaps_walked_bit))
		{
			/* (checked again until nothing changes: tag_validate_tags_layout) */
			tag_validate_globals.overlaps++;
		}
		else if (bytes_hash(bytes, overlap->size) != overlap->hash)
		{
			validation->tag_index = overlap->tag_index;
			validation->depth = 0;
			validation->field_name = NULL;
			tag_validate_refuse(validation, "shares %lu bytes at %08lx with another tag, and a correction changed them",
				(unsigned long)overlap->size, (unsigned long)XBOX_ADDRESS(bytes));
			return FALSE;
		}
	}

	return TRUE;
}

/* whether a string ends in the region, or in the tags (where a bsp's tag
references' names are) */
static boolean string_valid(
	struct tag_validation *validation,
	char const *string)
{
	byte const *tags = (byte const *)tag_validate_globals.header;
	unsigned long offset = (unsigned long)(POINTER_BITS(string) - POINTER_BITS(validation->region));
	unsigned long tags_offset = (unsigned long)(POINTER_BITS(string) - POINTER_BITS(tags));

	/* (the name a nameless tag or reference is given) */
	if (string == tag_validate_empty_name)
		return TRUE;
	/* (and no longer than a tag's path is, MAXIMUM_TAG_NAME_LENGTH: the
	game copies and formats names into buffers of that size) */
	if (region_contains(validation, string, 1))
	{
		/* (one in the copies past the tags, overlap_copied, ends in them) */
		unsigned long left = offset < validation->region_size ? validation->region_size - offset :
			copy_end(offset) - offset;

		return memchr(string, 0, MIN(left, (unsigned long)MAXIMUM_TAG_NAME_LENGTH)) != NULL;
	}
	if (tags && POINTER_BITS(string) >= POINTER_BITS(tags) &&
		tags_offset < (unsigned long)tag_validate_globals.tag_data_size)
	{
		return memchr(string, 0, MIN(tag_validate_globals.tag_data_size - tags_offset,
			(unsigned long)MAXIMUM_TAG_NAME_LENGTH)) != NULL;
	}

	return FALSE;
}

/* the tag tag_index names, if it is one of the map's: NULL otherwise */
static struct tag_validate_instance *instance_get(
	long tag_index)
{
	struct tag_validate_header *header = tag_validate_globals.header;
	short absolute_index = (short)tag_index;
	struct tag_validate_instance *instance;

	if (!header || tag_index == NONE || absolute_index < 0 || absolute_index >= header->tag_count)
		return NULL;
	instance = &instances_get(header)[absolute_index];

	return instance->tag_index == tag_index ? instance : NULL;
}

/* whether a tag is of one of groups (a list ending with 0; NULL for any) */
static boolean instance_in_groups(
	struct tag_validate_instance const *instance,
	unsigned long const *groups)
{
	if (!groups)
		return TRUE;
	for (; *groups; groups++)
	{
		unsigned long alias = group_alias_get(*groups);

		if (instance->group_tag == *groups ||
			instance->parent_group_tags[0] == *groups ||
			instance->parent_group_tags[1] == *groups ||
			(alias && instance->group_tag == alias))
		{
			return TRUE;
		}
	}

	return FALSE;
}

static char const *groups_text(
	unsigned long const *groups,
	char *text,
	size_t size)
{
	char group[5];

	text[0] = 0;
	if (!groups)
		return "any";
	for (; *groups && strlen(text) + 6 < size; groups++)
	{
		if (text[0])
			strcat(text, ",");
		strcat(text, tag_to_text(*groups, group));
	}

	return text;
}

static long integer_get(
	byte const *address,
	short size,
	boolean is_unsigned)
{
	switch (size)
	{
	case 1:
		return is_unsigned ? (long)*address : (long)*(signed char const *)address;
	case 2:
		return is_unsigned ? (long)*(unsigned short const *)address : (long)*(short const *)address;
	default:
		return *(long const *)address;
	}
}

static void integer_set(
	byte *address,
	short size,
	long value)
{
	switch (size)
	{
	case 1:
		*address = (byte)value;
		break;
	case 2:
		*(short *)address = (short)value;
		break;
	default:
		*(long *)address = value;
		break;
	}
}

/* NONE in an integer of size bytes, as it reads (an unsigned byte's NONE is
0xFF) */
static long integer_none(
	short size,
	boolean is_unsigned)
{
	if (!is_unsigned)
		return NONE;

	return size == 1 ? 0xFF : size == 2 ? 0xFFFF : NONE;
}

/* the block a block index indexes */
static struct tag_block const *block_index_target(
	struct tag_validation *validation,
	struct tag_schema_field const *field)
{
	byte *base;

	if (field->target_level == TAG_SCHEMA_ROOT)
		base = validation->frames[0].base;
	else if (field->target_level == TAG_SCHEMA_STRUCTURE)
		base = validation->structure;
	else if (field->target_level >= 0 && field->target_level < validation->depth)
		base = validation->frames[validation->depth - 1 - field->target_level].base;
	else
		return NULL;

	return (struct tag_block const *)(base + field->target_offset);
}

/* a field's most elements or bytes in the layout being checked: the one
given (the field's), or the layout's own (tag_schema_layout's field_maximum) */
static long field_maximum(
	struct tag_schema_field const *field,
	long maximum)
{
	return tag_validate_globals.layout && tag_validate_globals.layout->field_maximum ?
		tag_validate_globals.layout->field_maximum(field, maximum) : maximum;
}

static void validate_block_extent(
	struct tag_validation *validation,
	struct tag_block *block,
	struct tag_schema_field const *field)
{
	struct tag_schema_definition const *definition = field->definition;
	/* (Halo PC's most, in its layout: TAG_SCHEMA_PC_BLOCK; or the layout's
	own where it has one, field_maximum) */
	long maximum = field_maximum(field, field->target_offset &&
		TEST_FLAG(tag_validate_globals.layout->flags, _tag_schema_layout_tool_maxima_kept_bit) ?
		field->target_offset : field->maximum);

	block->definition = XBOX_NULL;
	if (block->count < 0)
	{
		tag_validate_refuse(validation, "has %ld elements", block->count);
		return;
	}
	/* (a block of the tools' maximum, _tag_schema_tool_maximum_bit, is cut
	too, but in a layout whose tools went past them: Halo PC's) */
	if (maximum > 0 && block->count > maximum &&
		!(TEST_FLAG(field->flags, _tag_schema_tool_maximum_bit) &&
			TEST_FLAG(tag_validate_globals.layout->flags, _tag_schema_layout_tool_maxima_kept_bit)))
	{
		tag_validate_correct(validation, "has %ld elements, more than the game's %ld: cut to %ld",
			block->count, maximum, maximum);
		block->count = maximum;
	}
	if (block->count &&
		((unsigned long)block->count > validation->region_size / (unsigned long)definition->size ||
			!region_contains(validation, xbox_pointer(block->address), (unsigned long)block->count * definition->size)))
	{
		tag_validate_refuse(validation, "has %ld elements of %ld bytes at %08lx, outside the tags",
			block->count, definition->size, (unsigned long)block->address);
		return;
	}
	if (!shared_extent_claim(xbox_pointer(block->address), (unsigned long)block->count * definition->size,
			definition, FALSE))
	{
		void *copy;

		if (overlap_kept(validation, xbox_pointer(block->address), (unsigned long)block->count * definition->size,
				definition, FALSE, block, sizeof(*block), &copy))
		{
			if (copy)
				block->address = XBOX_ADDRESS(copy);
			return;
		}
		if (validation->refused)
			return;
	}
	else
	{
		if (!block->count)
			block->address = XBOX_NULL;
		return;
	}
	if (!overlap_walked(validation, xbox_pointer(block->address), (unsigned long)block->count * definition->size,
			definition))
	{
		/* (one the walk would write to: none, in a layout whose tools
		overlapped blocks, as its engine read whatever was there) */
		if (TEST_FLAG(tag_validate_globals.layout->flags, _tag_schema_layout_overlaps_walked_bit))
		{
			tag_validate_correct(validation, "has %ld elements of %ld bytes at %08lx, which overlap another's: none",
				block->count, definition->size, (unsigned long)block->address);
			block->count = 0;
			block->address = XBOX_NULL;
			return;
		}
		tag_validate_refuse(validation, "has %ld elements of %ld bytes at %08lx, which overlap another's",
			block->count, definition->size, (unsigned long)block->address);
		return;
	}
	if (!block->count)
		block->address = XBOX_NULL;

	return;
}

static void validate_data_extent(
	struct tag_validation *validation,
	struct tag_data *data,
	struct tag_schema_field const *field)
{
	long maximum = field_maximum(field, field->maximum);

	data->definition = XBOX_NULL;
	if (data->size < 0)
	{
		tag_validate_refuse(validation, "has %ld bytes", data->size);
		return;
	}
	if (maximum > 0 && data->size > maximum)
	{
		tag_validate_correct(validation, "has %ld bytes, more than the game's %ld: cut to %ld",
			data->size, maximum, maximum);
		data->size = maximum;
	}
	if (field->type == _tag_schema_file_data)
	{
		/* (in a file of the loader's, perhaps, which it checks it against:
		_tag_schema_layout_file_data_elsewhere_bit) */
		if (TEST_FLAG(tag_validate_globals.layout->flags, _tag_schema_layout_file_data_elsewhere_bit))
			return;
		if (data->file_offset < 0 || data->file_offset > tag_validate_globals.file_length - data->size)
		{
			tag_validate_refuse(validation, "has %ld bytes at %08lx, outside the map's %ld",
				data->size, data->file_offset, tag_validate_globals.file_length);
		}
		return;
	}
	/* (Stubbs mode: animation frame data the port read from the map file
	into a block of its own, stubbs_mode.c) */
	if (data->size && stubbs_mode_owns_data(xbox_pointer(data->address), data->size))
		return;
	/* (Halo PC retail's tools left some data pointing past the tags, which
	its engine read whatever was there: none, _tag_schema_layout_overlaps_walked_bit) */
	if (data->size && !region_contains(validation, xbox_pointer(data->address), data->size) &&
		TEST_FLAG(tag_validate_globals.layout->flags, _tag_schema_layout_overlaps_walked_bit))
	{
		tag_validate_correct(validation, "has %ld bytes at %08lx, outside the tags: none", data->size,
			(unsigned long)data->address);
		data->size = 0;
		data->address = XBOX_NULL;
		return;
	}
	if (data->size && region_contains(validation, xbox_pointer(data->address), data->size) &&
		!shared_extent_claim(xbox_pointer(data->address), data->size, field, TRUE))
	{
		void *copy;

		if (overlap_kept(validation, xbox_pointer(data->address), (unsigned long)data->size, field, TRUE, data,
				sizeof(*data), &copy))
		{
			if (copy)
				data->address = XBOX_ADDRESS(copy);
			return;
		}
		if (!validation->refused && !overlap_data_none(validation, data))
		{
			tag_validate_refuse(validation, "has %ld bytes at %08lx, outside the tags or overlapping another's",
				data->size, (unsigned long)data->address);
		}
		return;
	}
	if (data->size && !region_contains(validation, xbox_pointer(data->address), data->size))
	{
		tag_validate_refuse(validation, "has %ld bytes at %08lx, outside the tags or overlapping another's",
			data->size, (unsigned long)data->address);
		return;
	}
	if (!data->size)
		data->address = XBOX_NULL;

	return;
}

static void validate_tag_index(
	struct tag_validation *validation,
	long *tag_index,
	unsigned long *group_tag,
	unsigned long const *groups)
{
	struct tag_validate_instance *instance;
	char text[96];

	if (*tag_index == NONE)
		return;
	instance = instance_get(*tag_index);
	if (!instance || !instance_in_groups(instance, groups))
	{
		tag_validate_correct(validation, "names %08lx, not a tag of %s: none",
			*tag_index, groups_text(groups, text, sizeof(text)));
		*tag_index = NONE;
		return;
	}
	/* (what the game takes the reference's tag to be: the tag's group) */
	if (group_tag && *group_tag != instance->group_tag)
	{
		char said[5];
		char group[5];

		tag_validate_correct(validation, "says '%s' of a '%s' tag", tag_to_text(*group_tag, said),
			tag_to_text(instance->group_tag, group));
		*group_tag = instance->group_tag;
	}

	return;
}

static void validate_value(
	struct tag_validation *validation,
	byte *address,
	struct tag_schema_field const *field)
{
	boolean is_unsigned = TEST_FLAG(field->flags, _tag_schema_unsigned_bit);
	boolean none_allowed = TEST_FLAG(field->flags, _tag_schema_none_bit);

	switch (field->type)
	{
	case _tag_schema_reference:
	{
		struct tag_reference *reference = (struct tag_reference *)address;

		/* (a reference to no tag often has a name pointer that is not one,
		in the retail maps too: only one to a tag counts as a correction) */
		if (!string_valid(validation, xbox_pointer(reference->name)))
		{
			if (reference->index != NONE)
				tag_validate_correct(validation, "has a name outside the tags: none");
			reference->name = XBOX_ADDRESS(tag_validate_empty_name);
		}
		validate_tag_index(validation, &reference->index, &reference->group_tag, field->definition);
		break;
	}
	case _tag_schema_tag_index:
		validate_tag_index(validation, (long *)address, NULL, field->definition);
		break;
	case _tag_schema_block_index:
	{
		struct tag_block const *target = block_index_target(validation, field);
		long value = integer_get(address, field->size, is_unsigned);
		long none = integer_none(field->size, is_unsigned);
		long count = target ? target->count : 0;

		/* (an empty block's index is none, as below, whether or not it
		may be otherwise) */
		if ((value >= 0 && value < count && value != none) || ((none_allowed || !count) && value == none))
			break;
		tag_validate_correct(validation, "is %ld, past its block's %ld: %s",
			value, count, none_allowed || !count ? "none" : "0");
		integer_set(address, field->size, none_allowed || !count ? NONE : 0);
		break;
	}
	case _tag_schema_enum:
	{
		long value = integer_get(address, field->size, is_unsigned);
		long none = integer_none(field->size, is_unsigned);
		/* (Halo PC's count, in its layout; or the layout's own where it has
		one, field_maximum: OpenSauce's four grenade types) */
		long maximum = field_maximum(field, field->target_offset &&
			TEST_FLAG(tag_validate_globals.layout->flags, _tag_schema_layout_pc_shader_types_bit) ?
			field->target_offset : field->maximum);

		if ((value >= 0 && value < maximum && value != none) || (none_allowed && value == none))
			break;
		/* (a Halo PC map's short that names one of its values in the other
		byte order is that value: HaloMD's tools wrote some so, 256 for 1,
		as a shotgun's weapon type) */
		if (field->size == 2 && TEST_FLAG(tag_validate_globals.layout->flags, _tag_schema_layout_pc_header_bit))
		{
			unsigned short bytes = (unsigned short)value;
			long swapped = (long)(unsigned short)((bytes >> 8) | (bytes << 8));

			if (swapped < maximum && swapped != none)
			{
				tag_validate_correct(validation, "is %ld, past its %ld values: %ld, its bytes the other way round",
					value, maximum, swapped);
				integer_set(address, field->size, swapped);
				break;
			}
		}
		tag_validate_correct(validation, "is %ld, past its %ld values: %s",
			value, maximum, none_allowed ? "none" : "0");
		integer_set(address, field->size, none_allowed ? NONE : 0);
		break;
	}
	case _tag_schema_string:
		if (!memchr(address, 0, field->size))
		{
			tag_validate_correct(validation, "is not terminated");
			address[field->size - 1] = 0;
		}
		break;
	case _tag_schema_reset:
		if (field->size <= 4)
			integer_set(address, field->size, field->target_offset);
		else
			memset(address, 0, field->size);
		break;
	}

	return;
}

/* one pass over an element's fields (those of the structures in it too) */
static void validate_fields(
	struct tag_validation *validation,
	byte *base,
	struct tag_schema_definition const *definition)
{
	struct tag_schema_field const *field;
	byte *structure = validation->structure;

	validation->structure = base;
	for (field = definition->fields; field->type != _tag_schema_terminator && !validation->refused; field++)
	{
		short index;

		validation->field_name = field->name;
		for (index = 0; index < field->count && !validation->refused; index++)
		{
			byte *address = base + field->offset + index * field->size;

			switch (field->type)
			{
			case _tag_schema_struct:
				validate_fields(validation, address, field->definition);
				validation->field_name = field->name;
				break;

			case _tag_schema_block:
				if (validation->pass == _pass_extents)
				{
					validate_block_extent(validation, (struct tag_block *)address, field);
				}
				else
				{
					struct tag_block *block = (struct tag_block *)address;
					long element_index;

					/* (the elements, through every pass) */
					for (element_index = 0;
						element_index < block->count && !validation->refused;
						element_index++)
					{
						validate_element(
							validation,
							(byte *)xbox_pointer(block->address) + element_index * ((struct tag_schema_definition const *)field->definition)->size,
							field->definition,
							field->name,
							element_index);
					}
					validation->field_name = field->name;
				}
				break;

			case _tag_schema_data:
			case _tag_schema_file_data:
				if (validation->pass == _pass_extents)
					validate_data_extent(validation, (struct tag_data *)address, field);
				break;

			case _tag_schema_check:
				if (validation->pass == _pass_checks && !field->check(validation, base))
				{
					if (!validation->refused)
						tag_validate_refuse(validation, "failed its check");
				}
				/* (what it corrected read through by the next check only as
				it was checked: shared_as_others_unchanged) */
				if (validation->pass == _pass_checks && tag_validate_as_other_count)
					shared_as_others_unchanged(validation);
				break;

			default:
				if (validation->pass == _pass_values)
					validate_value(validation, address, field);
				break;
			}
		}
	}
	validation->structure = structure;

	return;
}

/* an element, and the elements of its blocks: in the first walk, its
extents then its values then its blocks' elements (so that an index's
block is checked before the index); in the checks' walk, its checks then
its blocks' elements */
static void validate_element(
	struct tag_validation *validation,
	byte *base,
	struct tag_schema_definition const *definition,
	char const *field_name,
	long element_index)
{
	struct tag_validation_frame *frame;
	short pass = validation->pass;

	if (validation->depth >= MAXIMUM_VALIDATION_DEPTH)
	{
		tag_validate_refuse(validation, "is too deep");
		return;
	}
	frame = &validation->frames[validation->depth++];
	frame->base = base;
	frame->definition = definition;
	frame->field_name = field_name;
	frame->element_index = element_index;

	if (pass == _pass_checks)
	{
		/* (its checks, then its blocks': validate_fields does both) */
		validate_fields(validation, base, definition);
	}
	else
	{
		validation->pass = _pass_extents;
		validate_fields(validation, base, definition);
		if (!validation->refused)
		{
			validation->pass = _pass_values;
			validate_fields(validation, base, definition);
		}
		validation->pass = pass;
	}
	validation->depth--;

	return;
}

/* the blocks' elements are walked in the values pass (validate_fields); the
first walk starts each tag there */
static void validate_tag(
	struct tag_validation *validation,
	long tag_index,
	void *root,
	struct tag_schema_definition const *definition,
	short pass)
{
	validation->tag_index = tag_index;
	validation->depth = 0;
	validation->structure = root;
	validation->field_name = NULL;
	validation->pass = pass;
	validate_element(validation, root, definition, NULL, NONE);

	return;
}

/* whether a definition's fields (and those of the structures in it) all
lie within it, as the walk assumes: a field past its definition's size
would be read and written in bytes that another tag may hold. A mistake
in a schema, reported (once) and the map refused, never trusted */
static boolean definition_fits(
	struct tag_schema_definition const *definition,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth > MAXIMUM_VALIDATION_DEPTH)
		return FALSE;
	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		/* (a structure's bytes are its definition's: the last of an array
		ends there) */
		long end = field->type == _tag_schema_struct && field->count >= 1 ?
			field->offset + (long)field->size * (field->count - 1) +
				((struct tag_schema_definition const *)field->definition)->size :
			field->offset + (long)field->size * field->count;

		if (field->type != _tag_schema_check &&
			(field->offset < 0 || field->count < 1 || end > definition->size))
		{
			char message[MAXIMUM_MESSAGE_LENGTH];

			snprintf(message, sizeof(message), "the tag schema '%s' has its field '%s' at %ld..%ld, past its %ld bytes",
				definition->name, field->name, field->offset, end, definition->size);
			tag_validate_report(message);
			return FALSE;
		}
		if ((field->type == _tag_schema_struct || field->type == _tag_schema_block) &&
			!definition_fits(field->definition, depth + 1))
		{
			return FALSE;
		}
		if (field->type == _tag_schema_struct &&
			((struct tag_schema_definition const *)field->definition)->size > field->size)
		{
			char message[MAXIMUM_MESSAGE_LENGTH];

			snprintf(message, sizeof(message), "the tag schema '%s' has its structure '%s' of %ld bytes in %d",
				definition->name, field->name, ((struct tag_schema_definition const *)field->definition)->size,
				field->size);
			tag_validate_report(message);
			return FALSE;
		}
	}

	return TRUE;
}

/* whether every group's schema of a layout fits (definition_fits), found
once for each of the last few layouts */
static boolean schemas_fit(
	struct tag_schema_layout const *layout)
{
	static struct tag_schema_layout const *layouts[4];
	static short fits[4];
	short slot;
	short fit;

	for (slot = 0; slot < (short)NUMBEROF(layouts); slot++)
	{
		if (layouts[slot] == layout)
			return (boolean)fits[slot];
	}
	{
		struct tag_schema_group const *const *list;

		fit = TRUE;
		for (list = layout->group_lists; *list && fit; list++)
		{
			struct tag_schema_group const *group;

			for (group = *list; group->group_tag && fit; group++)
			{
				if (group->definition && !definition_fits(group->definition, 0))
					fit = FALSE;
			}
		}
	}
	for (slot = 0; slot < (short)NUMBEROF(layouts) && layouts[slot]; slot++)
		;
	if (slot < (short)NUMBEROF(layouts))
	{
		layouts[slot] = layout;
		fits[slot] = fit;
	}

	return (boolean)fit;
}

/* a tag of the table through a pass of its group's schema, if it has one */
static void validate_instance(
	struct tag_validation *validation,
	struct tag_validate_instance *instance,
	short pass)
{
	struct tag_schema_group const *group = schema_group_get(instance->group_tag);
	short absolute_index = (short)instance->tag_index;

	/* (a tag that shares an earlier one's root was walked as that one) */
	if (absolute_index >= 0 &&
		(tag_validate_shared_roots[absolute_index / CLAIM_BITS] & (1UL << (absolute_index % CLAIM_BITS))))
	{
		return;
	}
	if (group && group->definition && instance->base_address)
		validate_tag(validation, instance->tag_index, xbox_pointer(instance->base_address), group->definition, pass);

	return;
}

static void validation_new(
	struct tag_validation *validation,
	void *region,
	unsigned long region_size)
{
	memset(validation, 0, sizeof(*validation));
	validation->region = region;
	validation->region_size = region_size;
	validation->tag_index = NONE;

	return;
}

/* a header's vertex and index buffers: in the region and claimed, their
data in the region */
static boolean validate_buffers(
	struct tag_validation *validation,
	byte *vertex_buffers,
	long vertex_buffer_count,
	byte *index_buffers,
	long index_buffer_count)
{
	long index;

	validation->vertex_buffers = vertex_buffers;
	validation->vertex_buffer_count = vertex_buffer_count;
	validation->index_buffers = index_buffers;
	validation->index_buffer_count = index_buffer_count;
	if (vertex_buffer_count < 0 || index_buffer_count < 0 ||
		(unsigned long)vertex_buffer_count > validation->region_size / BUFFER_SIZE ||
		(unsigned long)index_buffer_count > validation->region_size / BUFFER_SIZE ||
		(vertex_buffer_count && !region_contains(validation, vertex_buffers, vertex_buffer_count * BUFFER_SIZE)) ||
		(index_buffer_count && !region_contains(validation, index_buffers, index_buffer_count * BUFFER_SIZE)) ||
		!claim(vertex_buffers, vertex_buffer_count * BUFFER_SIZE) ||
		!claim(index_buffers, index_buffer_count * BUFFER_SIZE))
	{
		tag_validate_refuse(validation, "has its vertex or index buffers outside the tags");
		return FALSE;
	}
	for (index = 0; index < vertex_buffer_count + index_buffer_count; index++)
	{
		byte *buffer = index < vertex_buffer_count ?
			vertex_buffers + index * BUFFER_SIZE :
			index_buffers + (index - vertex_buffer_count) * BUFFER_SIZE;
		/* (the buffer's Data, its bytes' address before it is registered) */
		void *data = buffer_data_address(buffer);

		if (!region_contains(validation, data, 1))
		{
			tag_validate_refuse(validation, "has a vertex or index buffer's data at %08lx, outside the tags",
				(unsigned long)XBOX_ADDRESS(data));
			return FALSE;
		}
	}

	return TRUE;
}

/* ---------- public code */

boolean tag_validate_tags(
	void *tag_header,
	long tag_data_size,
	long file_length,
	char const *map_name)
{
	return tag_validate_tags_layout(stubbs_mode_active() ? &tag_schema_stubbs_layout : &tag_schema_xbox_layout,
		tag_header, tag_data_size, file_length, map_name);
}

static boolean validate_tags_once(
	struct tag_schema_layout const *layout,
	void *tag_header,
	long tag_data_size,
	long file_length,
	char const *map_name);

/* (in a layout whose overlapping blocks are walked as their own, a
correction made for one may have changed what another reads: the tags are
checked again until nothing more is corrected, a few times at most) */
boolean tag_validate_tags_layout(
	struct tag_schema_layout const *layout,
	void *tag_header,
	long tag_data_size,
	long file_length,
	char const *map_name)
{
	long corrections = 0;
	short pass;
	boolean valid = TRUE;

	memset(&tag_validate_copies, 0, sizeof(tag_validate_copies));
	for (pass = 0; pass < MAXIMUM_OVERLAP_PASSES && valid; pass++)
	{
		valid = validate_tags_once(layout, tag_header, tag_data_size, file_length, map_name);
		corrections += tag_validate_globals.corrections;
		if (!TEST_FLAG(layout->flags, _tag_schema_layout_overlaps_walked_bit) || !tag_validate_globals.overlaps ||
			!tag_validate_globals.corrections)
		{
			break;
		}
	}
	/* (corrections still made in the last pass may have changed what another
	overlapping block reads, unchecked: the map is refused, as one whose
	overlaps are not safe to play) */
	if (valid && pass == MAXIMUM_OVERLAP_PASSES)
	{
		struct tag_validation validation;

		validation_new(&validation, tag_header, (unsigned long)tag_data_size);
		tag_validate_refuse(&validation, "has overlapping blocks whose corrections still change others after %d checks",
			(int)MAXIMUM_OVERLAP_PASSES);
		valid = FALSE;
	}
	tag_validate_globals.corrections = corrections;
	if (tag_validate_copies.count)
	{
		char message[MAXIMUM_MESSAGE_LENGTH];

		snprintf(message, sizeof(message), "the map '%s' has %ld roots, blocks and data overlapping others' given "
			"their own copies (%lu bytes)", map_name, tag_validate_copies.count, tag_validate_copies.bytes);
		tag_validate_report(message);
	}

	return valid;
}

#if !defined(HALO_XBOX_CONSOLE) && !defined(HALO_CONSOLE_NO_FORGE)
/* port: another map's tags (Forge's AI characters on the multiplayer maps,
port/linux/game/forge_ai_tags.c), checked as tag_validate_tags checks a
map's, with what the loaded map's check keeps for its structure bsps (its
header, the claims, the shared extents) put back after */
struct tag_validate_kept_state
{
	__typeof__(tag_validate_globals) globals;
	struct shared_extent shared_tags[SHARED_EXTENT_TAG_ENTRIES];
	struct shared_extent shared_bsp[SHARED_EXTENT_BSP_ENTRIES];
	struct shared_overlap overlaps[MAXIMUM_SHARED_OVERLAPS];
	long overlap_count;
	long tag_overlap_count;
	long as_others[MAXIMUM_SHARED_AS_OTHERS];
	long as_other_count;
	long tag_as_other_count;
	unsigned long claims[CLAIM_WORDS];
	unsigned long shared_roots[0x10000 / CLAIM_BITS];
	__typeof__(tag_validate_copies) copies;
};

/* (while another map's tags are checked) where the loaded map's check
state waits: made by tag_validate_other_tags_room, before the other tags
take their memory */
static struct tag_validate_kept_state *tag_validate_kept;

boolean tag_validate_other_tags_room(
	boolean wanted)
{
	if (wanted && !tag_validate_kept)
		tag_validate_kept = malloc(sizeof(*tag_validate_kept));
	else if (!wanted && tag_validate_kept)
	{
		free(tag_validate_kept);
		tag_validate_kept = NULL;
	}
	return !wanted || tag_validate_kept != NULL;
}

boolean tag_validate_other_tags(
	void *tag_header,
	long tag_data_size,
	long file_length,
	char const *map_name)
{
	struct tag_validate_kept_state *kept = tag_validate_kept;
	boolean valid;

	if (!kept)
	{
		error(_error_silent, "the map '%s''s tags cannot be checked: no room for the loaded map's check (%lu KB)",
			map_name, (unsigned long)(sizeof(*kept) >> 10));
		return FALSE;
	}
	kept->globals = tag_validate_globals;
	memcpy(kept->shared_tags, tag_validate_shared_tags, sizeof(kept->shared_tags));
	memcpy(kept->shared_bsp, tag_validate_shared_bsp, sizeof(kept->shared_bsp));
	memcpy(kept->overlaps, tag_validate_overlaps, sizeof(kept->overlaps));
	kept->overlap_count = tag_validate_overlap_count;
	kept->tag_overlap_count = tag_validate_tag_overlap_count;
	memcpy(kept->as_others, tag_validate_as_others, sizeof(kept->as_others));
	kept->as_other_count = tag_validate_as_other_count;
	kept->tag_as_other_count = tag_validate_tag_as_other_count;
	memcpy(kept->claims, tag_validate_claims, sizeof(kept->claims));
	memcpy(kept->shared_roots, tag_validate_shared_roots, sizeof(kept->shared_roots));
	kept->copies = tag_validate_copies;
	valid = tag_validate_tags_layout(stubbs_mode_active() ? &tag_schema_stubbs_layout : &tag_schema_xbox_layout,
		tag_header, tag_data_size, file_length, map_name);
	tag_validate_globals = kept->globals;
	memcpy(tag_validate_shared_tags, kept->shared_tags, sizeof(kept->shared_tags));
	memcpy(tag_validate_shared_bsp, kept->shared_bsp, sizeof(kept->shared_bsp));
	memcpy(tag_validate_overlaps, kept->overlaps, sizeof(kept->overlaps));
	tag_validate_overlap_count = kept->overlap_count;
	tag_validate_tag_overlap_count = kept->tag_overlap_count;
	memcpy(tag_validate_as_others, kept->as_others, sizeof(kept->as_others));
	tag_validate_as_other_count = kept->as_other_count;
	tag_validate_tag_as_other_count = kept->tag_as_other_count;
	memcpy(tag_validate_claims, kept->claims, sizeof(kept->claims));
	memcpy(tag_validate_shared_roots, kept->shared_roots, sizeof(kept->shared_roots));
	tag_validate_copies = kept->copies;
	return valid;
}
#endif

static boolean validate_tags_once(
	struct tag_schema_layout const *layout,
	void *tag_header,
	long tag_data_size,
	long file_length,
	char const *map_name)
{
	struct tag_validate_header *header = tag_header;
	struct tag_validation validation;
	boolean pc_header = TEST_FLAG(layout->flags, _tag_schema_layout_pc_header_bit);
	long absolute_index;

	memset(&tag_validate_globals, 0, sizeof(tag_validate_globals));
	tag_validate_globals.layout = layout;
	/* (no more than the claims hold) */
	tag_validate_globals.tag_cache_size = MIN(layout->tag_cache_size, (unsigned long)CLAIM_CACHE_SIZE);
#ifdef HALO_XBOX_CONSOLE
	tag_validate_skipped = !claims_ready();
	if (tag_validate_skipped)
		return TRUE;
#endif
	tag_validate_globals.header = header;
	tag_validate_globals.tag_data_size = tag_data_size;
	/* (data in other files than the map is the loader's to check:
	_tag_schema_layout_file_data_elsewhere_bit) */
	tag_validate_globals.file_length = TEST_FLAG(layout->flags, _tag_schema_layout_file_data_elsewhere_bit) ?
		0x7FFFFFFF : file_length;
	snprintf(tag_validate_globals.map_name, sizeof(tag_validate_globals.map_name), "%s", map_name);
	memset(tag_validate_claims, 0, CLAIM_WORDS * sizeof(*tag_validate_claims));
	memset(tag_validate_shared_roots, 0, sizeof(tag_validate_shared_roots));
	shared_extents_clear(tag_validate_shared_tags, SHARED_EXTENT_TAG_ENTRIES);
	shared_extents_clear(tag_validate_shared_bsp, SHARED_EXTENT_BSP_ENTRIES);
	tag_validate_overlap_count = 0;
	tag_validate_tag_overlap_count = 0;
	tag_validate_as_other_count = 0;
	tag_validate_tag_as_other_count = 0;
	validation_new(&validation, tag_header, (unsigned long)tag_data_size);
#ifdef HALO_64BIT
	if (!tag_validate_empty_name)
		tag_validate_empty_name = malloc(1);
	if (!tag_validate_empty_name)
	{
		tag_validate_refuse(&validation, "cannot be checked: out of memory");
		return FALSE;
	}
	tag_validate_empty_name[0] = 0;
#endif

	if (!schemas_fit(layout))
	{
		tag_validate_refuse(&validation, "cannot be checked: a tag schema is wrong");
		return FALSE;
	}
	if (tag_data_size < (pc_header ? PC_TAG_HEADER_SIZE : (long)sizeof(*header)) ||
		(unsigned long)tag_data_size > tag_validate_globals.tag_cache_size ||
		(pc_header ? pc_header_signature(header) : header->signature) != TAG_HEADER_SIGNATURE ||
		header->tag_count <= 0 || header->tag_count > UNSIGNED_SHORT_MAX ||
		!region_contains(&validation, instances_get(header), header->tag_count * sizeof(struct tag_validate_instance)))
	{
		tag_validate_refuse(&validation, "has a damaged tag header");
		return FALSE;
	}
	/* (Halo PC's header has its model data where the Xbox's has its
	buffers, and no buffers: its models' are made as they load,
	port/linux/game/ce_models.c) */
	if (!claim(header, pc_header ? PC_TAG_HEADER_SIZE : sizeof(*header)) ||
		!claim(instances_get(header), header->tag_count * sizeof(struct tag_validate_instance)) ||
		(!pc_header &&
			!validate_buffers(&validation, xbox_pointer(header->vertex_buffers), header->vertex_buffer_count,
				xbox_pointer(header->index_buffers), header->index_buffer_count)))
	{
		if (!validation.refused)
			tag_validate_refuse(&validation, "has a damaged tag header");
		return FALSE;
	}

	/* the tag table: each tag's index, name, groups and root */
	for (absolute_index = 0; absolute_index < header->tag_count && !validation.refused; absolute_index++)
	{
		struct tag_validate_instance *instance = &instances_get(header)[absolute_index];
		struct tag_schema_group const *group = schema_group_get(instance->group_tag);
		unsigned long parent_group_tags[2];

		validation.tag_index = instance->tag_index;
		if ((short)instance->tag_index != absolute_index)
		{
			tag_validate_refuse(&validation, "has tag %08lx in the table's place %ld", instance->tag_index,
				absolute_index);
			break;
		}
		if (!string_valid(&validation, xbox_pointer(instance->name)))
		{
			instance->name = XBOX_ADDRESS(tag_validate_empty_name);
			tag_validate_correct(&validation, "has no name");
		}
		/* (a group's parents are what the game asks a tag's group by
		(tag_get): they are the group's, not the map's) */
		parent_group_tags[0] = group ? group->parent_group_tags[0] : NONE;
		parent_group_tags[1] = group ? group->parent_group_tags[1] : NONE;
		if (instance->parent_group_tags[0] != parent_group_tags[0] ||
			instance->parent_group_tags[1] != parent_group_tags[1])
		{
			char text[5];

			tag_validate_correct(&validation, "has the wrong parent groups for '%s'",
				tag_to_text(instance->group_tag, text));
			instance->parent_group_tags[0] = parent_group_tags[0];
			instance->parent_group_tags[1] = parent_group_tags[1];
		}
		/* (a structure bsp's root is where it loads, set as it does) */
		if (instance->group_tag == STRUCTURE_BSP_GROUP_TAG)
		{
			instance->base_address = XBOX_NULL;
			continue;
		}
		if (!instance->base_address)
		{
			tag_validate_refuse(&validation, "has no data");
			break;
		}
		if (group && group->definition &&
			(!region_contains(&validation, xbox_pointer(instance->base_address), group->definition->size) ||
				!shared_extent_claim(xbox_pointer(instance->base_address), group->definition->size,
					group->definition, FALSE)))
		{
			void *root = xbox_pointer(instance->base_address);

			if (region_contains(&validation, root, group->definition->size))
			{
				struct shared_extent const *entry = shared_extent_find(
					(unsigned long)(POINTER_BITS(root) - POINTER_BITS(header)));

				/* (a tag whose root is exactly an earlier one's, of its
				group: the same tag twice in the table, walked once) */
				if (entry && entry->kind == group->definition &&
					entry->offset == (unsigned long)(POINTER_BITS(root) - POINTER_BITS(header)) &&
					entry->size == (unsigned long)group->definition->size)
				{
					tag_validate_shared_roots[absolute_index / CLAIM_BITS] |= 1UL << (absolute_index % CLAIM_BITS);
					continue;
				}
				/* (or that shares an earlier one's bytes otherwise, or has
				them copied apart) */
				{
					void *copy;

					if (overlap_kept(&validation, root, (unsigned long)group->definition->size, group->definition,
							FALSE, &instance->base_address, sizeof(instance->base_address), &copy))
					{
						if (copy)
							instance->base_address = XBOX_ADDRESS(copy);
						continue;
					}
					if (validation.refused)
						break;
				}
				if (overlap_walked(&validation, root, group->definition->size, group->definition))
					continue;
			}
			tag_validate_refuse(&validation, "has its data at %08lx, outside the tags or overlapping another's",
				(unsigned long)instance->base_address);
			break;
		}
	}

	/* every tag through its schema, then every tag's checks */
	for (absolute_index = 0; absolute_index < header->tag_count && !validation.refused; absolute_index++)
		validate_instance(&validation, &instances_get(header)[absolute_index], _pass_values);
	/* (no check reads through bytes shared as another structure that a
	correction changed: shared_as_others_unchanged, again after each) */
	if (!validation.refused)
		shared_as_others_unchanged(&validation);
	for (absolute_index = 0; absolute_index < header->tag_count && !validation.refused; absolute_index++)
		validate_instance(&validation, &instances_get(header)[absolute_index], _pass_checks);
	if (!validation.refused)
		shared_overlaps_unchanged(&validation, 0);
	tag_validate_tag_overlap_count = tag_validate_overlap_count;
	tag_validate_tag_as_other_count = tag_validate_as_other_count;

	/* each tag's name, now that every root, block and data is claimed: a
	name in any of their bytes could lose its terminator to what is written
	there (the corrections above, the game's runtime values), and is none.
	(Tags may share a name: names are only read) */
	for (absolute_index = 0; absolute_index < header->tag_count && !validation.refused; absolute_index++)
	{
		struct tag_validate_instance *instance = &instances_get(header)[absolute_index];
		char const *name = xbox_pointer(instance->name);

		if (name == tag_validate_empty_name)
			continue;
		validation.tag_index = instance->tag_index;
		validation.depth = 0;
		validation.field_name = NULL;
		if (!string_valid(&validation, name) ||
			tag_validate_any_claimed(name, (unsigned long)strlen(name) + 1))
		{
			instance->name = XBOX_ADDRESS(tag_validate_empty_name);
			tag_validate_correct(&validation, "has its name in a tag's data: none");
		}
	}

	tag_validate_globals.corrections = validation.corrections;
	if (tag_validate_globals.overlaps && !validation.refused)
	{
		char message[MAXIMUM_MESSAGE_LENGTH];

		snprintf(message, sizeof(message), "the map '%s' has %ld roots, blocks and data overlapping others', as %s's "
			"tools left them: each checked as its own", tag_validate_globals.map_name, tag_validate_globals.overlaps,
			layout->name);
		tag_validate_report(message);
	}

	return !validation.refused;
}

boolean tag_validate_structure_bsp(
	long tag_index,
	void *base,
	long size)
{
	struct tag_validate_header *header = tag_validate_globals.header;
	struct tag_validate_structure_bsp_header *bsp_header = base;
	struct tag_validate_instance *instance = instance_get(tag_index);
	struct tag_schema_group const *group = schema_group_get(STRUCTURE_BSP_GROUP_TAG);
	struct tag_validation validation;
	unsigned long tag_cache_offset = (unsigned long)(POINTER_BITS(base) - POINTER_BITS(header));

#ifdef HALO_XBOX_CONSOLE
	/* (the map's tags were not checked either: tag_validate_tags) */
	if (tag_validate_skipped)
		return TRUE;
#endif
	validation_new(&validation, base, (unsigned long)size);
	validation.tag_index = tag_index;
	if (!header || !instance || instance->group_tag != STRUCTURE_BSP_GROUP_TAG ||
		size < (long)sizeof(*bsp_header) ||
		tag_cache_offset < (unsigned long)tag_validate_globals.tag_data_size ||
		tag_cache_offset > tag_validate_globals.tag_cache_size ||
		(unsigned long)size > tag_validate_globals.tag_cache_size - tag_cache_offset)
	{
		tag_validate_refuse(&validation, "has a structure bsp outside the tag cache");
		return FALSE;
	}
	/* (another bsp may have been where this one is) */
	unclaim(tag_validate_globals.tag_data_size,
		tag_validate_globals.tag_cache_size - tag_validate_globals.tag_data_size);
	shared_extents_clear(tag_validate_shared_bsp, SHARED_EXTENT_BSP_ENTRIES);
	tag_validate_overlap_count = tag_validate_tag_overlap_count;
	tag_validate_as_other_count = tag_validate_tag_as_other_count;

	if (bsp_header->signature != STRUCTURE_BSP_HEADER_SIGNATURE ||
		!claim(bsp_header, sizeof(*bsp_header)) ||
		!validate_buffers(&validation, xbox_pointer(bsp_header->vertex_buffers), bsp_header->vertex_buffer_count,
			xbox_pointer(bsp_header->index_buffers), bsp_header->index_buffer_count))
	{
		if (!validation.refused)
			tag_validate_refuse(&validation, "has a damaged structure bsp header");
		return FALSE;
	}
	if (group && group->definition)
	{
		if (!region_contains(&validation, xbox_pointer(bsp_header->base_address), group->definition->size) ||
			!claim(xbox_pointer(bsp_header->base_address), group->definition->size))
		{
			tag_validate_refuse(&validation, "has its structure bsp at %08lx, outside it",
				(unsigned long)bsp_header->base_address);
			return FALSE;
		}
		validate_tag(&validation, tag_index, xbox_pointer(bsp_header->base_address), group->definition, _pass_values);
		if (!validation.refused)
			shared_as_others_unchanged(&validation);
		if (!validation.refused)
			validate_tag(&validation, tag_index, xbox_pointer(bsp_header->base_address), group->definition, _pass_checks);
		if (!validation.refused)
			shared_overlaps_unchanged(&validation, tag_validate_tag_overlap_count);
	}
	tag_validate_globals.corrections += validation.corrections;

	return !validation.refused;
}

long tag_validate_corrections(
	void)
{
	return tag_validate_globals.corrections;
}

/* (for tools/map_validate.c's fuzzing: which bytes the last validation
found to be a tag's root, block or data) */
boolean tag_validate_claimed(
	void const *address)
{
	unsigned long offset = (unsigned long)(POINTER_BITS(address) - POINTER_BITS(tag_validate_globals.header));

	if (!tag_validate_globals.header || POINTER_BITS(address) < POINTER_BITS(tag_validate_globals.header) ||
		offset >= tag_validate_globals.tag_cache_size)
	{
		return FALSE;
	}

	return (tag_validate_claims[offset / CLAIM_BITS] & (1UL << (offset % CLAIM_BITS))) != 0;
}

/* whether any of the size bytes at address were found to be a tag's root,
block or data (for checks: a buffer's data the game draws from must not be
bytes it writes to as it runs) */
boolean tag_validate_any_claimed(
	void const *address,
	unsigned long size)
{
	if (mcc_validation_callbacks)
		return mcc_validation_callbacks->any_claimed(address, size);
	unsigned long offset = (unsigned long)(POINTER_BITS(address) - POINTER_BITS(tag_validate_globals.header));
	unsigned long bit;

	if (!tag_validate_globals.header || POINTER_BITS(address) < POINTER_BITS(tag_validate_globals.header) ||
		offset > tag_validate_globals.tag_cache_size || size > tag_validate_globals.tag_cache_size - offset)
	{
		return TRUE;
	}
	for (bit = offset; bit < offset + size; )
	{
		unsigned long word = tag_validate_claims[bit / CLAIM_BITS];

		if (bit % CLAIM_BITS == 0 && offset + size - bit >= CLAIM_BITS)
		{
			if (word)
				return TRUE;
			bit += CLAIM_BITS;
		}
		else
		{
			if (word & (1UL << (bit % CLAIM_BITS)))
				return TRUE;
			bit++;
		}
	}

	return FALSE;
}

void tag_validate_refuse(
	struct tag_validation *validation,
	char const *format,
	...)
{
	va_list arguments;

	if (mcc_validation_callbacks && mcc_validation_callbacks->owns(validation))
	{
		va_start(arguments, format);
		mcc_validation_callbacks->message(validation, TRUE, format, arguments);
		va_end(arguments);
		return;
	}

	validation->refused = TRUE;
	va_start(arguments, format);
	validation_message(validation, "cannot be played", format, arguments);
	va_end(arguments);

	return;
}

void tag_validate_non_negative(
	struct tag_validation *validation,
	char const *name,
	real *value)
{
	if (!(*value >= 0.0f))
	{
		tag_validate_correct(validation, "has a %s of %f: 0", name, *value);
		*value = 0.0f;
	}

	return;
}

void tag_validate_correct(
	struct tag_validation *validation,
	char const *format,
	...)
{
	va_list arguments;

	if (mcc_validation_callbacks && mcc_validation_callbacks->owns(validation))
	{
		va_start(arguments, format);
		mcc_validation_callbacks->message(validation, FALSE, format, arguments);
		va_end(arguments);
		return;
	}

	validation->corrections++;
	if (validation->corrections <= MAXIMUM_LOGGED_CORRECTIONS)
	{
		va_start(arguments, format);
		validation_message(validation, "is corrected", format, arguments);
		va_end(arguments);
	}
	else if (validation->corrections == MAXIMUM_LOGGED_CORRECTIONS + 1)
	{
		char message[MAXIMUM_MESSAGE_LENGTH];

		snprintf(message, sizeof(message), "the map '%s' has more corrections, not logged",
			tag_validate_globals.map_name);
		tag_validate_report(message);
	}

	return;
}

unsigned long tag_validate_layout_flags(
	struct tag_validation *validation)
{
	/* (an MCC map's tags are checked as the Xbox's layout, by its own
	walk: mcc_tag_validate.c) */
	if (mcc_validation_callbacks && mcc_validation_callbacks->owns(validation))
		return 0;

	return tag_validate_globals.layout->flags;
}

long tag_validate_file_length(
	struct tag_validation *validation)
{
	(void)validation;

	return tag_validate_globals.file_length;
}

boolean tag_validate_contains(
	struct tag_validation *validation,
	void const *address,
	unsigned long size)
{
	if (mcc_validation_callbacks && mcc_validation_callbacks->owns(validation))
		return mcc_validation_callbacks->contains(validation, address, size);
	return region_contains(validation, address, size);
}

void *tag_validate_tag_get(
	struct tag_validation *validation,
	long tag_index,
	unsigned long group_tag)
{
	if (mcc_validation_callbacks && mcc_validation_callbacks->owns(validation))
		return mcc_validation_callbacks->tag_get(validation, tag_index, group_tag);
	struct tag_validate_instance *instance = instance_get(tag_index);
	unsigned long groups[2];

	(void)validation;
	groups[0] = group_tag;
	groups[1] = 0;
	if (!instance || !instance->base_address || !instance_in_groups(instance, groups))
		return NULL;

	return xbox_pointer(instance->base_address);
}

static void *buffer_data(
	byte *buffers,
	long count,
	void const *buffer)
{
	unsigned long offset = (unsigned long)(POINTER_BITS(buffer) - POINTER_BITS(buffers));

	if (POINTER_BITS(buffer) < POINTER_BITS(buffers) || offset % BUFFER_SIZE ||
		offset / BUFFER_SIZE >= (unsigned long)count)
	{
		return NULL;
	}

	return buffer_data_address(buffer);
}

void *tag_validate_vertex_buffer_data(
	struct tag_validation *validation,
	void const *buffer)
{
	if (mcc_validation_callbacks && mcc_validation_callbacks->owns(validation))
		return mcc_validation_callbacks->buffer_data(validation, buffer, FALSE);
	return buffer_data(validation->vertex_buffers, validation->vertex_buffer_count, buffer);
}

void *tag_validate_index_buffer_data(
	struct tag_validation *validation,
	void const *buffer)
{
	if (mcc_validation_callbacks && mcc_validation_callbacks->owns(validation))
		return mcc_validation_callbacks->buffer_data(validation, buffer, TRUE);
	return buffer_data(validation->index_buffers, validation->index_buffer_count, buffer);
}
