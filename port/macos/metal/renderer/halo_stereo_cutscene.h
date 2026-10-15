/* The immersive cutscene's mask (Task 12k's spike, debug.cutscene_immersive;
the stereo spec's "Option: the immersive cutscene with the frame's outside
blurred"). A cutscene's third-person film frame renders as the full view:
the eyes are the cutscene camera turned by the head (stereo.c), and the
director's frame, the camera's own 16:9 frustum, sits anchored in the room.
The presenter (port/ios/host/host_stereo.m; the side-by-side view's debug
screenshot in port/linux/src/d3d8_device.c) keeps the picture inside that
frame sharp and blurs, darkens and desaturates it outside, with a soft edge.

HALO_STEREO_CUTSCENE_SOURCE is written in the subset of C and the Metal
Shading Language both take (as halo_stereo_window.h's window test): the eye
shader compiles it as text, and the probes and the debug screenshot run it
as C. Its math functions are called as (atan)(...) so that no function-like
macro rewrites them. */
#ifndef HALO_STEREO_CUTSCENE_H
#define HALO_STEREO_CUTSCENE_H

#include <math.h>

/* the soft edge's width outside the frame, in radians (3 degrees): the
frame itself stays sharp to its edge */
#define HALO_STEREO_CUTSCENE_SOFT_EDGE 0.05235988f
/* the director's frame: the 16:9 inside of the letterbox, whose width is
the camera's 4:3 frame's (cinematics.c's bars cover 12.5% of the height
top and bottom) */
#define HALO_STEREO_CUTSCENE_ASPECT (16.0f / 9.0f)
/* the blurred picture's size against the eye's, each axis */
#define HALO_STEREO_CUTSCENE_BLUR_SCALE 4

/* 0 inside the director's frame to 1 fully outside it, for a direction
(x right, y up, z forward) in the frame's own axes, given the frame's half
tangents across (tx) and up (ty) and the soft edge's width (soft, radians):
how far the direction lies outside the frame, in angle along each axis,
eased over the soft edge. A direction behind the frame is fully outside */
#define HALO_STEREO_CUTSCENE_SOURCE \
static inline float halo_stereo_cutscene_outside(float x, float y, float z, float tx, float ty, float soft) \
{ \
	float ax = x < 0.0f ? -x : x; \
	float ay = y < 0.0f ? -y : y; \
	float out_x, out_y, t; \
	if (!(z > 0.0f)) \
		return 1.0f; \
	out_x = (float)(atan)(ax / z) - (float)(atan)(tx); \
	out_y = (float)(atan)(ay / z) - (float)(atan)(ty); \
	t = (out_x > out_y ? out_x : out_y) / soft; \
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); \
	return t * t * (3.0f - 2.0f * t); \
}

HALO_STEREO_CUTSCENE_SOURCE

#define HALO_STEREO_CUTSCENE_TEXT(...) #__VA_ARGS__
#define HALO_STEREO_CUTSCENE_STRING(...) HALO_STEREO_CUTSCENE_TEXT(__VA_ARGS__)

/* the outside's color from the blurred picture's (display-encoded RGB):
desaturated toward its luma and darkened, both by dim (0: as it is; 1:
black), debug.cutscene_outside_dim */
static inline void halo_stereo_cutscene_dimmed(const float in[3], float dim, float out[3])
{
	float luma = 0.299f * in[0] + 0.587f * in[1] + 0.114f * in[2];
	float keep = 1.0f - dim;
	int i;

	for (i = 0; i < 3; i++)
		out[i] = (luma + (in[i] - luma) * keep) * keep;
}

/* the blur's weights: a separable Gaussian over HALO_STEREO_CUTSCENE_TAPS
taps each side of the center on the quarter-size picture, sigma
HALO_STEREO_CUTSCENE_SIGMA texels there (about 16 of the eye's pixels) */
#define HALO_STEREO_CUTSCENE_TAPS 8
#define HALO_STEREO_CUTSCENE_SIGMA 4.0f

#endif
