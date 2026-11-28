/*
H3_CACHE.H

An MCC (PC) Halo 3 cache file (version 13, header 0x4000 bytes), read for
the game: its header and sections, its tag index and names, its string ids,
its tag data (read as it is wanted, through a small cache of blocks: the
section is tens of megabytes, more than the Vita can hold beside a level),
and its resources (the zone tag's resources, the play tag's pages - raw
deflate - in this map or in the shared maps beside it), read in one pass
per page for everything a level wants from it. Read only; every count,
offset and address checked against what was read: the file is anyone's.

Layouts: the file read against the owner's zanzibar.map and guardian.map
(tools/h3port's h3map.py on h3engine/guardian); the field names from
Assembly's Halo3MCC plugins (XboxChaos, read as a reference, no code).

An address here is an offset in the tag section: a tag's (its 32-bit
pointer times four, less the section's virtual base), a block's elements'.
*/

#ifndef __H3_CACHE_H
#define __H3_CACHE_H

#include "cseries.h"

/* ---------- constants */

enum
{
	H3_CACHE_HEADER_SIZE = 0x4000,
	H3_CACHE_VERSION = 13,
	H3_CACHE_NAME_SIZE = 256,
	H3_CACHE_WHY_SIZE = 160,
	/* the most bytes h3_cache_at gives at once */
	H3_CACHE_AT_MAXIMUM = 0x10000,
};

/* ---------- structures */

struct h3_cache;

struct h3_cache_tag
{
	unsigned long group_tag;
	unsigned long address;   /* 0: no data in this map */
};

/* a resource: its control (definition) bytes, fixed up - each pointer in
it the fixup's value: its kind in the top nibble (2: the control, 4: the
required page's data, 6: the optional page's), the offset in the low 28
bits - and where its data is */
struct h3_resource
{
	long index;
	unsigned char *control;
	unsigned long control_size;
	unsigned long root;         /* the root structure's offset in the control */
	unsigned char type;
	long page[2];               /* required, optional (NONE: none) */
	unsigned long offset[2];    /* the data's offset in each page */
	unsigned long size[2];      /* each page's bytes from there to its end */
};

/* a range of a page wanted: size bytes at offset (of the page's
uncompressed data) to out (NULL: the reader allocates them), done called
once they are all there (with out; the caller then owns an allocated one) */
struct h3_page_request
{
	long page;
	unsigned long offset;
	unsigned long size;
	unsigned char *out;
	void (*done)(void *context, struct h3_page_request *request);
	void *context;
	long tag;                   /* (the caller's) */
	boolean got;
};

/* a long read watched: a word that stops it (non-zero, the read fails at once;
NULL none) and a call for each few megabytes read from the file (NULL none) */
struct h3_cache_read_control
{
	int volatile const *cancel;
	void (*progress)(unsigned long bytes);
};

/* ---------- prototypes/H3_CACHE.C */

/* whether the file at path is an MCC Halo 3 map */
boolean h3_cache_file_is_halo3(
	char const *path);

/* maps_directory/name.map opened (its header, index, names and string ids
read); NULL and why if not */
struct h3_cache *h3_cache_open(
	char const *maps_directory,
	char const *name,
	char *why,
	long why_size);

void h3_cache_close(
	struct h3_cache *cache);

char const *h3_cache_name(
	struct h3_cache const *cache);
char const *h3_cache_scenario_path(
	struct h3_cache const *cache);

long h3_cache_tag_count(
	struct h3_cache const *cache);
boolean h3_cache_tag_get(
	struct h3_cache const *cache,
	long index,
	struct h3_cache_tag *tag);
/* a tag's name, or "" */
char const *h3_cache_tag_name(
	struct h3_cache const *cache,
	long index);
/* the first tag of group whose name is name (NULL: any) - NONE if none */
long h3_cache_find_tag(
	struct h3_cache const *cache,
	unsigned long group_tag,
	char const *name);
/* the tag a datum names (NONE: none, or no data here) */
long h3_cache_tag_from_datum(
	struct h3_cache const *cache,
	unsigned long datum);

/* size bytes of the tag data at address to out (FALSE: not all there) */
boolean h3_cache_read(
	struct h3_cache *cache,
	unsigned long address,
	unsigned long size,
	void *out);
/* size bytes (at most H3_CACHE_AT_MAXIMUM) at address, in one of the
cache's scratch buffers: good for the next 31 calls; NULL if not there */
void const *h3_cache_at(
	struct h3_cache *cache,
	unsigned long address,
	unsigned long size);
unsigned long h3_cache_read32(
	struct h3_cache *cache,
	unsigned long address);
short h3_cache_read16(
	struct h3_cache *cache,
	unsigned long address);
float h3_cache_readf(
	struct h3_cache *cache,
	unsigned long address);
/* a tag block at address: its count and its elements' address, every
element of element_size bytes there (FALSE: not) */
boolean h3_cache_block(
	struct h3_cache *cache,
	unsigned long address,
	unsigned long element_size,
	long *count,
	unsigned long *elements);
/* a tag reference at address (group, then the datum at +0xC): the tag
(NONE: none) */
/* a tag data reference at address: its size and its bytes' address */
boolean h3_cache_data(
	struct h3_cache *cache,
	unsigned long address,
	unsigned long *size,
	unsigned long *bytes);
long h3_cache_reference(
	struct h3_cache *cache,
	unsigned long address);
/* a string id's text ("" if not one) */
char const *h3_cache_string(
	struct h3_cache const *cache,
	unsigned long string_id);

/* a resource (of a datum's low 16 bits), its control copied and fixed
up; FALSE if it cannot be */
boolean h3_cache_resource_get(
	struct h3_cache *cache,
	long index,
	struct h3_resource *resource);
void h3_resource_free(
	struct h3_resource *resource);
/* a fixed-up pointer of its control at offset: its kind (2, 4, 6) and
offset; FALSE if it is none or past the control */
boolean h3_resource_pointer(
	struct h3_resource const *resource,
	unsigned long offset,
	unsigned long *kind,
	unsigned long *target);
/* a word of its control (0 past it) */
unsigned long h3_resource_read32(
	struct h3_resource const *resource,
	unsigned long offset);
/* a page's uncompressed size (0: no such page) */
unsigned long h3_cache_page_size(
	struct h3_cache *cache,
	long page);

/* the requests read, one pass over each page they name (its deflate
stream inflated once, a window at a time); FALSE (and why) if a page cannot
be read - the requests got before it are done - or control's word stopped
it. control may be NULL */
boolean h3_cache_pages_read(
	struct h3_cache *cache,
	struct h3_page_request *requests,
	long count,
	char *why,
	long why_size,
	struct h3_cache_read_control const *control);

#endif
