/*
BRIDGING.H

What the Swift launcher calls in C: the game (port/ios/include/halo_ios.h),
the engine's disc extractor (chupathingyce's port/linux/src/xiso.h), both in
lib/libhalo.a, and the system's zlib, which reads the map zips (Zip.swift).
*/

#include <zlib.h>

/* port/ios/include/halo_ios.h */
int halo_ios_run(const char *data_root, const char *hardware_id, char *error, int error_size);
void halo_ios_set_invite(const char *link);
int halo_ios_open_link(const char *link);
typedef int (*halo_ios_fold_proc)(void *view, double *frame);
void halo_ios_set_fold_proc(halo_ios_fold_proc proc);
void halo_ios_fold_changed(void);

/* port/linux/src/xiso.h */
typedef void (*xiso_progress_proc)(void *context, const char *file, unsigned long long done,
	unsigned long long total);
int xiso_is_image(const char *image_path);
int xiso_extract_maps(const char *image_path, const char *destination, xiso_progress_proc progress, void *context,
	char *error, int error_size);
