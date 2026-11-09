/*
CAPTURE.H

Screenshots (capture.c): the player's key (controls.screenshot, Print
Screen) asks for one, the renderer reads the game's finished frame back
without waiting for the GPU (d3d8_gl.c's pixel buffer and fence, bgfx's
read-back texture), and a thread of its own writes it as a PNG to
<data root>/screenshots/<map>_<YYYY-MM-DD_HH-MM-SS>.png. The frame is the
game's back buffer at its full size, the HUD and menus in it, without the
port's overlay (DELTA, toasts, the game list's screens). A toast says
when it is saved.

Nothing runs until the first screenshot: no thread, no buffers. The
desktop builds' only (HALO_CAPTURE: halo_console_features.h).
*/

#ifndef __HALO_CAPTURE_H
#define __HALO_CAPTURE_H

#ifdef HALO_CAPTURE

/* the input's: asks for a screenshot of the next frame (any thread) */
void capture_request_screenshot(void);

/* the renderer's, at each present: whether a screenshot is wanted, which
clears the ask */
int capture_screenshot_take(void);
/* the renderer's: the frame read back, width by height, its rows from the
top, `pitch` bytes apart; four bytes a pixel, blue first when bgra (else
red first). The pixels are malloc'd and become capture.c's */
void capture_screenshot_submit(unsigned char *pixels, long width, long height, long pitch, int bgra);
/* the same with pixels the renderer only lends (a mapped pixel buffer):
the writer copies them and then sets *released, after which the renderer
may let them go; until then they must stay where they are. 0 when they
were not taken (*released is set then too) */
int capture_screenshot_lend(const unsigned char *pixels, long width, long height, long pitch, int bgra,
	volatile int *released);

/* the game's, each frame: the map's name for the files ("menu" in the
menus), and the next line to show as a toast, if there is one (1) */
void capture_set_map(const char *name);
int capture_take_notice(char *title, unsigned long title_size, char *line, unsigned long line_size);

#endif

#endif
