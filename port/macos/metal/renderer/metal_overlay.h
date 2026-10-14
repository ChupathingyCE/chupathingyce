/* Fixed-width boundary between native Metal and the LP64-rewritten UI.
   All coordinates are target pixels from the top left. */
#ifndef HALO_METAL_OVERLAY_H
#define HALO_METAL_OVERLAY_H
#include <stdint.h>

enum {
    METAL_OVERLAY_ATLAS_SIZE = 2048,
    METAL_OVERLAY_MAX_VERTICES = 8192 * 6,
};
struct metal_overlay_vertex {
    float x, y, u, v;
    float local_x, local_y, half_width, half_height;
    float radius, thickness;
    uint8_t color[4];
};
_Static_assert(sizeof(struct metal_overlay_vertex) == 44, "Metal overlay vertex ABI");
struct metal_overlay_frame {
    const struct metal_overlay_vertex *vertices;
    const uint8_t *atlas;
    uint32_t vertex_count;
    uint32_t atlas_dirty;
    float cutouts[4][4];
};
/* Drains one frame's queue, including when width/height are zero. Borrowed
   pointers remain valid until the next UI gather/prepare; copy before then. */
void metal_overlay_prepare(int width, int height, struct metal_overlay_frame *frame);
/* Draws into the game's back buffer before screenshot/readback and scaling. */
void macos_metal_overlay_draw(uint32_t target);
#endif
