/*
CACHE_BYTE_SWAP.H

The maps' data in a big-endian console's byte order (the Xbox 360, the Wii
U): a cache file is the Xbox's, little-endian, and is swapped as it loads
(port/console/game/cache_byte_swap.c). Each function swaps only what is in
the Xbox's order, so a header that has been swapped already (the copy a
console keeps on its cache drive) is left as it is.
*/

#ifndef __CACHE_BYTE_SWAP_H
#define __CACHE_BYTE_SWAP_H

/* a cache file's header (0x800 bytes) */
void cache_file_header_byte_swap(void *header);

/* the tags a cache file's tag data holds, just read into the tag cache at
base (Xbox address base_address, size bytes): the tag header, the tag
index and the vertex and index buffers' descriptions; nonzero if they were
the Xbox's and are now the console's */
int cache_file_tags_byte_swap(void *base, unsigned long base_address, unsigned long size);

/* a structure BSP's data, just read (its header and its buffers'
descriptions); nonzero if they were the Xbox's */
int cache_file_structure_bsp_byte_swap(void *base, unsigned long base_address, unsigned long size);

#endif
