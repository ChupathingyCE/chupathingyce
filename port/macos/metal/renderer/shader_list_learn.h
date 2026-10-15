/* The Metal renderer's map shader lists, the pure parts (d3d8_device.c,
   tools/test_metal_shader_lists.py): a map's list name from its path, and a
   list's text joined from the app's list and the one learned in earlier
   runs (the data folder's metal-shader-lists, debug.shader_list_warm). */
#ifndef HALO_METAL_SHADER_LIST_LEARN_H
#define HALO_METAL_SHADER_LIST_LEARN_H
#include <stddef.h>
#include <string.h>

/* the last part of a map's path ("levels\test\bloodgulch\bloodgulch" or
   "maps/bloodgulch" -> "bloodgulch"), cut to size - 1 characters; returns
   whether there was one (an empty path, or one ending in a separator, has
   none) */
static inline int metal_shader_list_map_name(const char *path, char *name, size_t size)
{
    const char *base = path, *cursor;
    size_t length;

    if (!size)
        return 0;
    name[0] = 0;
    if (!path)
        return 0;
    for (cursor = path; *cursor; cursor++)
    {
        if (*cursor == '\\' || *cursor == '/')
            base = cursor + 1;
    }
    length = strlen(base);
    if (!length)
        return 0;
    if (length >= size)
        length = size - 1;
    memcpy(name, base, length);
    name[length] = 0;
    return 1;
}

/* the bytes of first then second, a newline between them when first
   doesn't end in one, into text (capacity bytes; NULL to measure); returns
   the joined length, which is copied only when it fits */
static inline size_t metal_shader_list_join(const char *first, size_t first_size, const char *second,
    size_t second_size, char *text, size_t capacity)
{
    size_t separator = first_size && second_size && first[first_size - 1] != '\n' ? 1 : 0;
    size_t total = first_size + separator + second_size;

    if (total < first_size || total < second_size)
        return 0;
    if (text && total <= capacity)
    {
        if (first_size)
            memcpy(text, first, first_size);
        if (separator)
            text[first_size] = '\n';
        if (second_size)
            memcpy(text + first_size + separator, second, second_size);
    }
    return total;
}
#endif
