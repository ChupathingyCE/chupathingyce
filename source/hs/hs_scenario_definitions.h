/*
HS_SCENARIO_DEFINITIONS.H

header included in hcex build.
*/

#ifndef __HS_SCENARIO_DEFINITIONS_H
#define __HS_SCENARIO_DEFINITIONS_H
#pragma once

/* ---------- constants */

enum
{
	_hs_script_startup = 0,
	_hs_script_dormant,
	_hs_script_continuous,
	_hs_script_static,
	_hs_script_stub,
	NUMBER_OF_HS_SCRIPT_TYPES,
};

/* ---------- headers */

#include "tag_files/tag_groups.h"

/* ---------- macros */

/* ---------- structures */

struct hs_script
{
	char name[32];
	short script_type;
	short return_type;
	long root_expression_index;
	byte unused[52];
};

struct hs_global
{
	char name[32];
	short type;
	short unused0;
	long unused1;
	long initialization_expression_index;
	byte unused2[48];
};

struct hs_reference
{
	byte unused[24];
	struct tag_reference reference;
};

struct hs_source_file
{
	char name[32];
	struct tag_data source;
};

/* port: a script value is a 32-bit slot; a short or boolean value is read
and written both as the slot's number ((short)value) and through its first
bytes (a short or boolean member over the slot, *(short *)&value). On the
Xbox the two agree; on a big-endian console the first bytes are the
number's high ones, so there a sub-32-bit view of a slot is moved to its
last bytes, where the number's low ones are, and both agree again */
#ifdef HALO_BIG_ENDIAN
#define HS_SLOT_MEMBER(type, name) byte name##_slot_pad[4 - sizeof(type)]; type name;
#define HS_SLOT_TRAILING_PAD(declaration)
#define HS_SLOT(type, slot) ((type *)((byte *)(slot) + 4 - sizeof(type)))
#else
#define HS_SLOT_MEMBER(type, name) type name;
#define HS_SLOT_TRAILING_PAD(declaration) declaration
#define HS_SLOT(type, slot) ((type *)(slot))
#endif
#define HS_UNION_SLOT(type, name) struct { HS_SLOT_MEMBER(type, name) };

struct hs_syntax_node
{
	short datum_header;
	union
	{
		short index;
		short constant_type;
		short function_index;
		short script_index;
	};
	short type;
	short flags;
	long next_node_index;
	union
	{
		long string_offset;
		long source_offset;
	};
	union
	{
		long data;
		HS_UNION_SLOT(boolean, boolean_value)
		real real_value;
		HS_UNION_SLOT(short, short_value)
	};
};

/* ---------- prototypes/EXAMPLE.C */

/* ---------- globals */

/* ---------- public code */

#endif // __HS_SCENARIO_DEFINITIONS_H
