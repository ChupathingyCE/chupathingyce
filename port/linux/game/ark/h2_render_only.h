/* h2_render_only.h: Halo 2's render-only nodes skipped in the tick for objects not drawn (h2_render_only.c) */

#ifndef __H2_RENDER_ONLY_H
#define __H2_RENDER_ONLY_H

struct h2_cache;

/* (h2_scripts.c) a Halo 2 level's map open: the object definitions' masks; dispose at the next */
void h2_render_only_map_loaded(struct h2_cache *cache);
void h2_render_only_dispose(void);
/* a definition's render-only nodes (bit n: this engine's model's node n; 0: none) */
unsigned long long h2_render_only_definition_mask(long definition_index);
/* the definitions with masks, one by one (which from 0): FALSE past the last */
unsigned char h2_render_only_definition_at(short which, long *definition_index, unsigned long long *mask);

/* (render_objects.c) a frame's objects found: the frame, then each */
void h2_render_only_frame_begin(void);
void h2_render_only_drawn(long object_index);

/* (objects.c) around an object's orientations and their preprocess: the
nodes skipped (model_animations.c reads h2_render_only_skip_mask) */
unsigned long long h2_render_only_begin(long object_index, long definition_index);
void h2_render_only_end(void);
extern unsigned long long h2_render_only_skip_mask;
/* (perf/ce-floor-2) how many definitions have render-only nodes (0: none -
h2_render_only_begin then only clears the skip mask; objects.c does that
without the call) */
extern long h2_render_only_definitions;
extern unsigned long h2_render_only_census[3];

/* (tick_hash.c) HALO_TICK_HASH_MASK_RENDER_ONLY: zeroed (0) and put back (1) */
void h2_render_only_hash(int restore);

#endif
