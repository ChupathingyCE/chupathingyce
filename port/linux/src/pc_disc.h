/*
PC_DISC.H

Halo PC's maps out of an image of its retail CD (pc_disc.c).
*/

#ifndef __HALO_LINUX_PC_DISC_H
#define __HALO_LINUX_PC_DISC_H

#include "cabinet.h"

/* whether the file is a CD image (ISO 9660); an Xbox disc image of a whole
disc is one too, so ask xiso.c first */
int pc_disc_is_image(const char *path);

/* writes the Halo PC disc image's maps that <destination>/maps_pc lacks
there; returns nonzero on success, or 0 with the reason (for the player) in
error. Called from any thread */
int pc_disc_extract_maps(const char *image_path, const char *destination, cabinet_progress_proc progress,
	void *context, char *error, int error_size);

#endif
