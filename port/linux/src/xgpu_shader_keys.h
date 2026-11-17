/*
XGPU_SHADER_KEYS.H

A map's shader key list (xgpu_shader_keys.c): what each of its draws' shaders
are made from, so that a renderer can make them all while the map's loading
screen is up (d3d8_shader_prep.inl) rather than at the draw that first needs
one. A key is the translators' input, not their output: the game's vertex
program (by a hash of its words), the variant drawn, and the pixel shader key
(xgpu.h). The GLSL, and what each driver or backend makes of it, follow from
it on the machine that loads the list, so one list serves OpenGL, its 3.3
path, bgfx's Vulkan and Metal, either byte order and any word size.

The lists are text (port/assets/shaders/README.md has the format): the stock maps' come
with the game (port/assets/shaders/stock.keys, made by tools/shader_keys.py),
and each map played adds its own to the cache folder (shader_keys/<map>.keys),
so a custom map's new shaders are made at its loading screen from its second
load on.
*/

#ifndef __HALO_LINUX_XGPU_SHADER_KEYS_H
#define __HALO_LINUX_XGPU_SHADER_KEYS_H

#ifdef XGPU_SHADER_KEYS_TEST
/* (port/linux/tests/shader_keys_test.c: the key's types alone) */
#include "nv2a_pixel_key.h"
#else
#include "xgpu.h"
#endif

#include <stdio.h>

/* the variant of the vertex program: its immediate-mode form (every input
register a float), the per-pixel lighting form (GL), the pixel shader
drawn with a depth bias (bgfx), and the Light effects' form (GL) */
enum
{
	_xgpu_key_immediate_bit = 1 << 0,
	_xgpu_key_lit_bit = 1 << 1,
	_xgpu_key_depth_bias_bit = 1 << 2,
	/* (GL) the Light effects' form of the program (plasma_model_shader.c) */
	_xgpu_key_plasma_bit = 1 << 3,
};

struct xgpu_program_key
{
	/* xgpu_vertex_code_hash of the vertex program's words */
	unsigned long long vertex_code;
	/* the declaration's packed attributes (nv2a_vertex_shader_to_glsl) */
	unsigned long packed_mask;
	unsigned long flags;
	struct nv2a_pixel_shader_key pixel;
};

struct xgpu_shader_key_list
{
	struct xgpu_program_key *keys;
	unsigned long count, capacity;
};

/* FNV-1a, 64 bits, of the program's words as little-endian bytes (the same
on every machine: the words are the Xbox's values) */
unsigned long long xgpu_vertex_code_hash(const DWORD *words, unsigned long count);

/* adds to list the keys of text (size bytes) for map: those under its "map"
line, and those before any "map" line (a map's own file has none); FALSE
for a text that is not a key list of this format */
BOOL xgpu_shader_keys_parse(const char *text, unsigned long size, const char *map, struct xgpu_shader_key_list *list);
void xgpu_shader_key_list_free(struct xgpu_shader_key_list *list);

/* a map's own list, written as its draws are made (one line a key it has
not had; a pixel shader key once, by number, for the keys using it) */
struct xgpu_shader_key_writer;
/* NULL if path can't be written; the keys already in it are not added again */
struct xgpu_shader_key_writer *xgpu_shader_key_writer_open(const char *path);
void xgpu_shader_key_writer_add(struct xgpu_shader_key_writer *writer, const struct xgpu_program_key *key);
void xgpu_shader_key_writer_flush(struct xgpu_shader_key_writer *writer);
void xgpu_shader_key_writer_close(struct xgpu_shader_key_writer *writer);

#ifndef XGPU_SHADER_KEYS_TEST
/* the stock maps' list (port/assets/shaders/stock.keys), embedded in the
game by tools/embed_assets.py: its text, or NULL with *size 0 */
const char *xgpu_shader_keys_stock(unsigned long *size);
#endif

#endif
