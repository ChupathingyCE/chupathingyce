/*
CONTENT_MAP_HEADER.H

Is this a map? A cache file's header asked once, header-only: its 'head', its
version (Xbox 5, Halo PC 7, MCC 13, Custom Edition 609; Chimera's compressed
maps have 0x861A0000 added to it), its 'foot' and OpenSauce's 'yelo' block,
from the file's bytes, little-endian on every processor. It only says what
the header is; each loader still reads the rest its own way. C89 too
(map_report.c).
*/

#ifndef CONTENT_MAP_HEADER_H
#define CONTENT_MAP_HEADER_H

#include "content_bytes.h"

/* ---------- constants */

#define CONTENT_MAP_HEADER_SIZE 0x800

#define CONTENT_MAP_VERSION_XBOX 5
#define CONTENT_MAP_VERSION_HALO_PC 7
#define CONTENT_MAP_VERSION_MCC 13
#define CONTENT_MAP_VERSION_CUSTOM_EDITION 609
/* (Chimera's chimera-compress: the real version in the low half) */
#define CONTENT_MAP_COMPRESSED_MARK 0x861A0000UL

#define CONTENT_MAP_HEADER_SIGNATURE 0x68656164UL /* 'head' */
#define CONTENT_MAP_FOOTER_SIGNATURE 0x666F6F74UL /* 'foot' */
#define CONTENT_MAP_OPENSAUCE_SIGNATURE 0x79656C6FUL /* 'yelo' */

#define CONTENT_MAP_VERSION_OFFSET 0x04
#define CONTENT_MAP_OPENSAUCE_OFFSET 0x70
#define CONTENT_MAP_FOOTER_OFFSET 0x7FC

/* ---------- types */

struct content_map_info
{
	/* the version as the header has it, and without a compressed map's mark */
	uint32_t version;
	uint32_t cache_version;
	int compressed;
	/* 1 when 'foot' ends the header, 0 when not, -1 when the bytes stop short */
	int footer;
	/* 1 when OpenSauce's 'yelo' block follows the header's fields, 0 when
	not, -1 when the bytes stop short */
	int opensauce;
};

/* ---------- code */

/* whether bytes (size of them) start as a cache file's header does: 'head'
and its version (8 bytes at least); info says what else of it is there */
CONTENT_BYTES_INLINE int content_map_probe(const void *bytes, size_t size, struct content_map_info *info)
{
	const unsigned char *header = (const unsigned char *)bytes;
	struct content_map_info found;

	if (!header || size < CONTENT_MAP_VERSION_OFFSET + 4 || content_u32le(header) != CONTENT_MAP_HEADER_SIGNATURE)
		return 0;
	found.version = content_u32le(header + CONTENT_MAP_VERSION_OFFSET);
	found.compressed = (found.version & 0xFFFF0000UL) == CONTENT_MAP_COMPRESSED_MARK;
	found.cache_version = found.compressed ? found.version & 0xFFFFUL : found.version;
	found.footer = size < CONTENT_MAP_FOOTER_OFFSET + 4 ? -1 :
		content_u32le(header + CONTENT_MAP_FOOTER_OFFSET) == CONTENT_MAP_FOOTER_SIGNATURE;
	found.opensauce = size < CONTENT_MAP_OPENSAUCE_OFFSET + 4 ? -1 :
		content_u32le(header + CONTENT_MAP_OPENSAUCE_OFFSET) == CONTENT_MAP_OPENSAUCE_SIGNATURE;
	if (info)
		*info = found;
	return 1;
}

#endif
