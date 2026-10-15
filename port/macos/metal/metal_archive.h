/* The Metal pipeline archive's segment files (gpu_metal.m): each run writes
   what it compiled to PREFIX<seconds>-<process>.binarchive, PREFIX naming
   the build, GPU and system an archive is good for; earlier segments are
   read only. Pure, for tools/test_metal_archive.py. */
#ifndef HALO_METAL_ARCHIVE_H
#define HALO_METAL_ARCHIVE_H
#include <string.h>

/* segments read at launch before they are all deleted and the archive
   starts again (each run that compiles anything adds one) */
#define METAL_ARCHIVE_SEGMENTS_MAX 16

/* whether a file in the archive folder is a segment for prefix: prefix,
   digits, '-', digits, ".binarchive" and nothing more (not the unsegmented
   PREFIX-less "binarchive" of older builds, a half-written ".new", or
   another build's) */
static inline int metal_archive_segment_is_current(const char *file, const char *prefix)
{
    static const char suffix[] = ".binarchive";
    size_t length = strlen(prefix), digits;

    if (strncmp(file, prefix, length))
        return 0;
    file += length;
    for (digits = 0; file[digits] >= '0' && file[digits] <= '9'; digits++)
        ;
    if (!digits || file[digits] != '-')
        return 0;
    file += digits + 1;
    for (digits = 0; file[digits] >= '0' && file[digits] <= '9'; digits++)
        ;
    return digits && !strcmp(file + digits, suffix);
}
#endif
