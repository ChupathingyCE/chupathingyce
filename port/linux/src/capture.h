/*
CAPTURE.H

Screenshots (capture.c): the player's key (controls.screenshot, Print
Screen) asks for one, the renderer reads the picture back as the window
shows it (d3d8_gl.c's pixel buffer and fence, without waiting for the GPU;
bgfx's screenshot of the window), and a thread of its own writes it as a
PNG to <data root>/screenshots/<map>_<YYYY-MM-DD_HH-MM-SS>.png. The shot is
what is on screen: the game's frame with the HUD and menus, and the port's
overlay over it (DELTA, the game list's screens, the Chupathingy menus), at
the window's size without its black bars. A toast says when it is saved;
the screenshots' own toasts are left out of the next shot (guide.c).

Nothing runs until the first screenshot: no thread, no buffers. The
desktop builds' only (HALO_CAPTURE: halo_console_features.h).
*/

#ifndef __HALO_CAPTURE_H
#define __HALO_CAPTURE_H

#ifdef HALO_CAPTURE

/* the input's: asks for a screenshot of the next frame (any thread) */
void capture_request_screenshot(void);

/* whether a screenshot has been asked for and not yet taken (the game's,
as it draws: the frame it draws then is the one the shot is of) */
int capture_screenshot_wanted(void);
/* the renderer's, at each present: whether a screenshot is wanted, which
clears the ask */
int capture_screenshot_take(void);
/* the renderer's: the frame read back, width by height, its rows `pitch`
bytes apart, from the top (or from the bottom when bottom_up: a window's
read by OpenGL); four bytes a pixel, blue first when bgra (else red
first). The pixels are malloc'd and become capture.c's */
void capture_screenshot_submit(unsigned char *pixels, long width, long height, long pitch, int bgra, int bottom_up);
/* the same with pixels the renderer only lends (a mapped pixel buffer):
the writer copies them and then sets *released, after which the renderer
may let them go; until then they must stay where they are. 0 when they
were not taken (*released is set then too) */
int capture_screenshot_lend(const unsigned char *pixels, long width, long height, long pitch, int bgra,
	int bottom_up, volatile int *released);

/* ---------- clips (capture_clips.c): with clips.enabled, the last
clips.seconds of the game's picture and sound kept encoded in memory, and
saved as an MP4 to <data root>/clips/ when controls.clip is pressed. Off,
they cost nothing: no thread, no buffers, one test a frame. */

/* the input's: save the clip (any thread) */
void capture_request_clip(void);
/* the renderer's, at each present: whether a frame is wanted now (clips on,
its time come, and room for it), and the size to make it from a back buffer
of source_width by source_height (*width, *height: the GPU's scaled copy;
the bgfx renderer, which cannot scale in a blit, passes the source's on) */
int capture_clip_frame_due(long source_width, long source_height, long *width, long *height, double *time);
/* the renderer's: a frame lent until *released (capture_screenshot_lend's
rules), made at time (capture_clip_frame_due's) */
int capture_clip_frame_lend(const unsigned char *pixels, long width, long height, long pitch, int bgra,
	double time, volatile int *released);
/* the mixer's (dsound_sdl.c): its output, frames of channels floats at
48 kHz, from the audio thread */
void capture_clip_audio(const float *samples, unsigned long frames, unsigned long channels);

/* (capture.c's, for capture_clips.c: the files' names and the toasts) */
void capture_file_name(char *name, unsigned long size);
void capture_notice(const char *title, const char *line);

/* the game's, each frame: the map's name for the files ("menu" in the
menus), and the next line to show as a toast, if there is one (1) */
void capture_set_map(const char *name);
int capture_take_notice(char *title, unsigned long title_size, char *line, unsigned long line_size);

#endif

#endif
