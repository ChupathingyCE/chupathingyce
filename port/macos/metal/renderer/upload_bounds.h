/* Checked Xbox-address upload spans. Fixed-width arithmetic is shared by
   native tests and the LP64-rewritten frontend. */
#ifndef HALO_METAL_UPLOAD_BOUNDS_H
#define HALO_METAL_UPLOAD_BOUNDS_H
#include <stdint.h>
enum { METAL_UPLOAD_BYTES_MAX = 16 * 1024 * 1024, METAL_DRAW_VERTICES_MAX = 65536 };
static inline int metal_upload_span(uint32_t address, uint32_t first, uint32_t count,
    uint32_t stride, uint32_t base, uint32_t capacity, uint32_t *start, uint32_t *bytes)
{
    uint64_t offset = (uint64_t)first * stride;
    uint64_t length = stride ? (uint64_t)count * stride : 64;
    uint64_t begin = (uint64_t)address + offset;
    if (!count || count > METAL_DRAW_VERTICES_MAX || length > METAL_UPLOAD_BYTES_MAX ||
        begin < base || begin - base > capacity || length > capacity - (begin - base))
        return 0;
    *start = (uint32_t)begin;
    *bytes = (uint32_t)length;
    return 1;
}
#endif
