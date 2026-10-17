/*
XISO.H

The maps folder and the Bink movies out of an Xbox disc image (xiso.c).
*/

#ifndef __HALO_LINUX_XISO_H
#define __HALO_LINUX_XISO_H

/* called as the copy goes: the file being copied, and the bytes copied of
all the files' */
typedef void (*xiso_progress_proc)(void *context, const char *file, unsigned long long done,
	unsigned long long total);

/* whether the file is an Xbox disc image (of the game partition or a whole
disc) */
int xiso_is_image(const char *image_path);

/* copies the image's maps folder to <destination>/maps, then the .bik files
of its bink folder that <destination>/bink lacks (a movie that cannot be
copied does not fail it); returns nonzero on success, or 0 with the reason (for the player) in error. Called from any
thread */
int xiso_extract_maps(const char *image_path, const char *destination, xiso_progress_proc progress, void *context,
	char *error, int error_size);

#endif
