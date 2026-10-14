/* The cutscene window's expansion in head-tracked stereo (stereo.c times
it; the presenter, port/ios/host/host_stereo.m, draws it). When a cutscene's
film hands over to the full view, the film's rectangle grows out to the
whole view over HALO_STEREO_EXPANSION_SECONDS, at the letterbox bars' own
rate (cinematics.c: one letterbox amount a second, linear), with the
picture inside it already the head-tracked view at the player's eyes. The
film has already eased into a window onto the world through the screen
(stereo.c's portal_eyes), so the first frame shows the world where the film
did.

The window is a rectangle of angles in the screen's frame (x right, y up, z
toward the viewer), about one eye: a direction d is inside when
atan2(d.x, -d.z) lies between its left and right and atan2(d.y, -d.z)
between its down and up. For directions toward the screen that is exactly
the screen's quad as the eye sees it, so the window begins as the film's
rectangle; it ends past the views' edges, or at +-pi (everything) when a
view looks away from the screen.

HALO_STEREO_WINDOW_SOURCE is written in the subset of C and the Metal
Shading Language both take (as host_stereo_vignette.h's mask): the eye
shader compiles it as text, and the probes and the side-by-side view's
debug screenshot run it as C. Its math functions are called as (atan2)(...)
so that no function-like macro rewrites them. */
#ifndef HALO_STEREO_WINDOW_H
#define HALO_STEREO_WINDOW_H

#include <math.h>

/* the expansion's length (the letterbox's 1/TICKS_PER_SECOND a tick from
in to out), and how far past the views' edges it ends, in radians */
#define HALO_STEREO_EXPANSION_SECONDS 1.0f
#define HALO_STEREO_EXPANSION_MARGIN 0.05f
#define HALO_STEREO_WINDOW_PI 3.14159265f

/* 0 outside the window, 1 inside it on the picture, 2 inside it on a bar
(each bars times 0.125 of its height in angle, cinematics.c's bar at the
letterbox's full amount),
for a direction (x, y, z) in the screen's frame */
#define HALO_STEREO_WINDOW_SOURCE \
static inline int halo_stereo_window_test(float x, float y, float z, float left, float right, float down, float up, \
	float bars) \
{ \
	float across = (float)(atan2)(x, -z); \
	float rise = (float)(atan2)(y, -z); \
	float bar = 0.125f * bars * (up - down); \
	if (across < left || across > right || rise < down || rise > up) \
		return 0; \
	return rise > up - bar || rise < down + bar ? 2 : 1; \
}

HALO_STEREO_WINDOW_SOURCE

#define HALO_STEREO_WINDOW_TEXT(...) #__VA_ARGS__
#define HALO_STEREO_WINDOW_STRING(...) HALO_STEREO_WINDOW_TEXT(__VA_ARGS__)

/* the screen's window (left, right, down, up) as an eye at eye (the
screen's frame, the screen's center its origin; any unit) sees a screen of
half extents half_width by half_height; 0, and everything, if the eye isn't
in front of it */
static inline int halo_stereo_screen_window(const float eye[3], float half_width, float half_height, float window[4])
{
	if (!(eye[2] > 0.0f)) {
		window[0] = window[2] = -HALO_STEREO_WINDOW_PI;
		window[1] = window[3] = HALO_STEREO_WINDOW_PI;
		return 0;
	}
	window[0] = atan2f(-half_width - eye[0], eye[2]);
	window[1] = atan2f(half_width - eye[0], eye[2]);
	window[2] = atan2f(-half_height - eye[1], eye[2]);
	window[3] = atan2f(half_height - eye[1], eye[2]);
	return 1;
}

/* an empty window, for halo_stereo_window_include to grow */
static inline void halo_stereo_window_empty(float window[4])
{
	window[0] = window[2] = HALO_STEREO_WINDOW_PI;
	window[1] = window[3] = -HALO_STEREO_WINDOW_PI;
}

/* grows an end window to take in a direction (the screen's frame) and the
margin past it; a direction away from the screen makes it everything */
static inline void halo_stereo_window_include(float window[4], const float direction[3])
{
	float across, rise;

	if (!(-direction[2] > 0.0f)) {
		window[0] = window[2] = -HALO_STEREO_WINDOW_PI;
		window[1] = window[3] = HALO_STEREO_WINDOW_PI;
		return;
	}
	across = atan2f(direction[0], -direction[2]);
	rise = atan2f(direction[1], -direction[2]);
	window[0] = fminf(window[0], fmaxf(-HALO_STEREO_WINDOW_PI, across - HALO_STEREO_EXPANSION_MARGIN));
	window[1] = fmaxf(window[1], fminf(HALO_STEREO_WINDOW_PI, across + HALO_STEREO_EXPANSION_MARGIN));
	window[2] = fminf(window[2], fmaxf(-HALO_STEREO_WINDOW_PI, rise - HALO_STEREO_EXPANSION_MARGIN));
	window[3] = fmaxf(window[3], fminf(HALO_STEREO_WINDOW_PI, rise + HALO_STEREO_EXPANSION_MARGIN));
}

/* a view's frustum (positive tangents left, right, up, down) into an end
window: its corners and the middles of its edges, each turned into the
screen's frame by screen_from_view (a rotation, rows) */
static inline void halo_stereo_window_include_view(float window[4], const float tangents[4],
	const float screen_from_view[3][3])
{
	int i, j, k;

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			float view[3], screen[3];

			if (i == 1 && j == 1)
				continue;
			view[0] = i == 0 ? -tangents[0] : i == 2 ? tangents[1] : 0.5f * (tangents[1] - tangents[0]);
			view[1] = j == 0 ? tangents[2] : j == 2 ? -tangents[3] : 0.5f * (tangents[2] - tangents[3]);
			view[2] = -1.0f;
			for (k = 0; k < 3; k++)
				screen[k] = screen_from_view[k][0] * view[0] + screen_from_view[k][1] * view[1] +
					screen_from_view[k][2] * view[2];
			halo_stereo_window_include(window, screen);
		}
	}
}

/* the window at progress (0 to 1) from start to end, linear, and never
smaller than last (NULL: no last): the edges sweep outward at a steady
angular rate, and a head turning during it can't pull one back */
static inline void halo_stereo_window_at(const float start[4], const float end[4], float progress,
	const float *last, float window[4])
{
	float t = progress < 0.0f ? 0.0f : progress > 1.0f ? 1.0f : progress;
	int side;

	for (side = 0; side < 4; side++) {
		float target = (side & 1) ? fmaxf(start[side], end[side]) : fminf(start[side], end[side]);

		window[side] = start[side] + (target - start[side]) * t;
		if (last)
			window[side] = (side & 1) ? fmaxf(window[side], last[side]) : fminf(window[side], last[side]);
	}
}

/* the bars elapsed seconds after the expansion began with them: on out at
the letterbox's rate, one amount a second */
static inline float halo_stereo_expansion_bars(float bars, float elapsed)
{
	float now = bars - elapsed / HALO_STEREO_EXPANSION_SECONDS;

	return now > 0.0f ? now : 0.0f;
}

/* The presenter's memory of an expansion (host_stereo.m), which keeps each
view's window from shrinking: whether the last frame it drew expanded, that
frame's progress, and each view's window then. Frames that don't reach the
full view's presenter (the film, a menu, no space) never update it, so an
expansion begins afresh at progress 0 (the guest reports its first frame at
exactly 0) or when the progress goes back, whatever it last saw */
#define HALO_STEREO_EXPANSION_VIEWS 4
struct halo_stereo_expansion_memory
{
	int on;
	float progress;
	float window[HALO_STEREO_EXPANSION_VIEWS][4];
};

/* once each frame the full view's presenter draws: 1 if this frame begins
an expansion (there is no last window to keep), else 0 */
static inline int halo_stereo_expansion_fresh(struct halo_stereo_expansion_memory *memory, int expanding,
	float progress)
{
	int fresh = expanding && (!memory->on || progress <= 0.0f || progress < memory->progress);

	memory->on = expanding;
	memory->progress = expanding ? progress : 0.0f;
	return fresh;
}

#endif
