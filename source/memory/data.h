/*
DATA.H

header included in hcex build.
*/

#ifndef __DATA_H
#define __DATA_H
#pragma once

/* ---------- headers */

#include "tag_files.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct data_array
{
	char name[TAG_STRING_LENGTH+1];
	short maximum_count;
	short size;
	boolean valid;
	boolean identifier_zero_invalid;
	unsigned long signature;
	short first_free_absolute_index;
	short count;
	short actual_count;
	short next_identifier;
	/* an Xbox address: a data array can be serialized in tag data (the
	scenario's script syntax) and is used there in place */
	XPTR(void) data;
};

/* port: what an array's data field holds for its own elements, as
data_initialize puts it there: their Xbox address in a 64-bit build, their
pointer in the others (in a build whose window is moved, HALO_WINDOW_REBASE,
an address in the window is the window's own, not the Xbox's) */
#ifdef HALO_64BIT
#define DATA_ARRAY_OWN_ELEMENTS(array) xbox_address((array) + 1)
#else
#define DATA_ARRAY_OWN_ELEMENTS(array) ((void *)((array) + 1))
#endif
#ifdef HALO_64BIT

typedef char data_array_size_assert[
	sizeof(struct data_array) == 0x38 ? 1 : -1];

#define DATA_ARRAY_DATA(array) XBOX_POINTER(void, (array)->data)
#endif

struct datum_header
{
	short identifier;
};

struct data_iterator
{
	struct data_array *data;
	short absolute_index;
	short pad;
	long datum_index;
	unsigned long signature;
};
#ifndef HALO_64BIT

typedef char data_iterator_size_assert[
	sizeof(struct data_iterator) == 0x10 ? 1 : -1];
#endif

/* ---------- prototypes/DATA.C */

long data_allocation_size(short maximum_count, short size);
void data_initialize(struct data_array *data, const char *name, short maximum_count, short size);
void *datum_try_and_get(struct data_array *data, long index);
void data_verify(struct data_array *data);
void *datum_get(struct data_array *data, long index);
struct data_array *data_new(const char *name, short maximum_count, short size);
void data_dispose(struct data_array *data);
void data_make_invalid(struct data_array *data);
long datum_new_at_index(struct data_array *data, long index);
long datum_new(struct data_array *data);
void datum_delete(struct data_array *data, long index);
void data_delete_all(struct data_array *data);
void data_iterator_new(struct data_iterator *iterator, struct data_array *data);
void *data_iterator_next(struct data_iterator *iterator);
long data_next_index(struct data_array *data, long index);
long data_prev_index(struct data_array *data, long index);
void data_compact(struct data_array *data);
void data_make_valid(struct data_array *data);

#ifdef HALO_CONSOLE_INLINE_ACCESSORS
/* port: a console's busiest units' datum_get (halo_port_features.h): the
datum without the call when the array is valid, the index has a salt or the
array takes a salt of 0, the index is in the array and its datum is in use
with that salt (datum_get's asserts, which source/memory keeps, and its
checks; datum_get_checked gives NULL if not). Anything else is the
function's, which asserts on it or returns NULL, as always */
static __inline__ void *datum_get_checked(
	struct data_array *data,
	long index)
{
	short identifier = index>>16;
	short absolute_index = index;

	if (data->valid && (identifier || !data->identifier_zero_invalid) &&
		absolute_index>=0 && absolute_index<data->count)
	{
		struct datum_header *header = (struct datum_header *)((byte *)xbox_pointer(data->data)+data->size*absolute_index);

		if (header->identifier && (!identifier || identifier==header->identifier))
			return header;
	}

	return NULL;
}

static __inline__ void *datum_get_fast(
	struct data_array *data,
	long index)
{
	void *header = datum_get_checked(data, index);

	return header ? header : (datum_get)(data, index);
}
#define datum_get(data, index) datum_get_fast((data), (index))
#endif


/* ---------- globals */

/* ---------- public code */

#endif // __DATA_H
