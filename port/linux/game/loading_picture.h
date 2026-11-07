/*
LOADING_PICTURE.H

The picture the loading screen shows (loading_picture.c): Chupathingy's own,
or the Xbox's with display.xbox_loading_picture on.
*/

#ifndef __LOADING_PICTURE_H
#define __LOADING_PICTURE_H

#ifndef HALO_CONSOLE_NO_LOADING_PICTURE

/* puts Chupathingy's picture beside the copy of the disc's (z:'s maps
folder), when the game starts (progress_bar_initialize) */
void loading_picture_initialize(
	void);

/* the path of the picture a loading screen reads, as progress_bar.c reads
it: an uncompressed 24-bit TGA of 320 by 240 */
void loading_picture_path(
	char *path,
	long size);

#endif

#endif
