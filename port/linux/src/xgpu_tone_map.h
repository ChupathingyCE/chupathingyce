/*
XGPU_TONE_MAP.H

display.hdr's shaders, as GLSL both renderers compile (xgpu_post.c and
nv2a_psh.c for OpenGL, d3d8_bgfx.inl for bgfx), and the numbers behind
them. port/linux/README.md's "HDR" tells the whole of it.

The game draws its 3D view into a back buffer with room above white
(GL_RGBA16F in place of GL_RGBA8). Every pixel shader's result is clamped
to [0, 1], as the NV2A's was, so the light past white comes of blending:
where effects add up (plasma, explosions, the sun, lens flares, the dynamic
lights) the color goes on past 1 instead of clipping, and a frame whose
draws add no light past white is the SDR frame, value for value. A few
draws are let past white by themselves (the key's hdr, below): the glows
the game draws in [0, 1] though they stand for light.

Before the HUD and the menus are drawn over the view (halo_screen_tone_map),
the tone-mapping maps it down. The game's values are gamma 2.2 (its
textures and lightmaps were drawn for a TV, which the Xbox fed as they
were): the curve works in linear light and encodes its result the same way,
so the HUD, drawn after it in the game's own values, lands where it always
did. The curves are display.hdr_tone_mapping's: "neutral" leaves the picture
as drawn up to its knee and rolls off only what is brighter, by the largest
channel, so that a color keeps its hue; "aces" is Krzysztof Narkowicz's fit
of the ACES filmic curve; "reinhard" the extended Reinhard curve on
luminance. The peak is the display's over white (1 on an SDR display, and
always on OpenGL's 8-bit window): with a peak of 1 the neutral curve's knee
is 0.8 of white, the top of the range given up to the roll-off; on an HDR
display the knee is white itself, nothing below white changes, and the
highlights go up into the display's headroom.

The present then sends the picture out. An 8-bit window clamps; a
floating-point window (display.hdr_output on OpenGL: scRGB) takes linear
light scaled to the desktop's white; bgfx's HDR10 swap chain (RGB10A2, SMPTE
ST 2084) takes nits, white at display.hdr_paper_white, Rec. 709 primaries
turned to Rec. 2020 and PQ-encoded (pq_encode). The overlay (ui_overlay.c)
drawn over it in the window is encoded the same way.

The OpenGL path and the curves are hkmodd's (OpenCE pull request 280, before
its license changed); the glows past white and the paper white in nits
follow erin.cena's and g0atbutt's HDR in OpenH1, with the OpenH1 team's
permission. The expansion here is theirs without the pixel underneath, which
OpenGL and bgfx cannot read as they draw.
*/

#ifndef __HALO_LINUX_XGPU_TONE_MAP_H
#define __HALO_LINUX_XGPU_TONE_MAP_H

/* display.hdr_paper_white's and display.hdr_peak's range, in nits (0: the
display's own) */
#define XGPU_HDR_PAPER_WHITE_MINIMUM 80
#define XGPU_HDR_PAPER_WHITE_MAXIMUM 500
#define XGPU_HDR_PEAK_MINIMUM 400
#define XGPU_HDR_PEAK_MAXIMUM 10000
/* a display's SDR white where the system says none: BT.2408's reference
white */
#define XGPU_HDR_REFERENCE_WHITE_NITS 203.0f
/* scRGB's 1.0, in nits */
#define XGPU_HDR_SCRGB_WHITE_NITS 80.0f

/* display.hdr_exposure's range */
#define XGPU_HDR_EXPOSURE_MINIMUM 0.25f
#define XGPU_HDR_EXPOSURE_MAXIMUM 4.0f

/* what the game's code says of a draw (halo_hdr_draw_kind, declared with
these in port/linux/include/halo_linux_source_fixups.h for the game's
sources) */
#ifndef HALO_HDR_KIND_NONE
#define HALO_HDR_KIND_NONE 0
#define HALO_HDR_KIND_FIRST_PERSON 1
#define HALO_HDR_KIND_NUMERIC 2
#define HALO_HDR_KIND_METER 4
#define HALO_HDR_KIND_HUD_GLOW 16
#define HALO_HDR_KIND_LIGHT 32
#endif

/* ---------- the draws' tags (nv2a_pixel_shader_key.hdr)

A glow the game draws in [0, 1] though it stands for light (halo_hdr_draw_kind,
d3d8_gl.c's hdr_draw_tag): brightened toward its ceiling where it is bright,
by more of its own light (hdr_glow). The ceilings are in the frame's values
(gamma 2.2): the first-person weapon's displays and glows to 1.75 times
white's light, the HUD's flashing meters and motion tracker blips to 1.5
times, the light of the world (a teleporter's field, a light fixture, an
energy shield, a plasma bolt's volume) to 3 times, from lower, as the game
draws them dimmer. */
enum
{
	_xgpu_hdr_tag_none,
	_xgpu_hdr_tag_weapon_glow,
	_xgpu_hdr_tag_hud_glow,
	_xgpu_hdr_tag_light,
	NUMBER_OF_XGPU_HDR_TAGS
};

/* the ceilings (1.75, 1.5 and 3 to the 1 / 2.2) and knees, as the shaders'
text has them */
#define XGPU_HDR_WEAPON_CEILING "1.2898"
#define XGPU_HDR_HUD_CEILING "1.2024"
#define XGPU_HDR_LIGHT_CEILING "1.6474"
#define XGPU_HDR_GLOW_KNEE "0.6"
#define XGPU_HDR_LIGHT_KNEE "0.4"

/* hdr_glow(c, knee, ceiling): the draw's color, in [0, 1], brightened
toward the ceiling from the knee of its brightest channel up, smoothly */
#define XGPU_HDR_GLOW_GLSL \
	"vec3 hdr_glow(vec3 color, float knee, float ceiling)\n" \
	"{\n" \
	"\tfloat most = max(color.r, max(color.g, color.b));\n" \
	"\tfloat t = clamp((most - knee) / (1.0 - knee), 0.0, 1.0);\n" \
	"\treturn color * (1.0 + (ceiling - 1.0) * t * t * (3.0 - 2.0 * t));\n" \
	"}\n"

/* ---------- the tone-mapping (xgpu_post.c, d3d8_bgfx.inl)

tone_map(c, tone): the game's gamma-2.2 color mapped as above; tone.x is the
curve (xgpu.h's _xgpu_tone_mapping_*), tone.y the exposure, tone.z the peak
in whites (1 for SDR), tone.w the UI's white over paper white
(display.hdr_ui_nits; 1 for the same), which the view is drawn under so
that the HUD, at the frame's 1, is brighter: xgpu_tone_map_set gives them.
The neutral curve's roll-off is the extended Reinhard curve meeting the
line at its slope, reaching the peak at WHITE times it. */
#define XGPU_TONE_MAP_GLSL \
	"const float GAMMA = 2.2;\n" \
	"const float KNEE = 0.8;\n" \
	"const float WHITE = 8.0;\n" \
	"\n" \
	"float extended_reinhard(float value, float white)\n" \
	"{\n" \
	"\treturn value * (1.0 + value / (white * white)) / (1.0 + value);\n" \
	"}\n" \
	"\n" \
	"vec3 tone_map(vec3 encoded, vec4 tone)\n" \
	"{\n" \
	"\tvec3 color = pow(max(encoded, vec3(0.0)), vec3(GAMMA)) * tone.y;\n" \
	"\tfloat peak = tone.z;\n" \
	"\n" \
	"\tif (tone.x < 0.5)\n" \
	"\t{\n" \
	"\t\tfloat knee = min(KNEE * peak, 1.0);\n" \
	"\t\tfloat largest = max(color.r, max(color.g, color.b));\n" \
	"\n" \
	"\t\tif (largest > knee)\n" \
	"\t\t{\n" \
	"\t\t\tfloat range = peak - knee;\n" \
	"\t\t\tfloat rolled = knee + range * extended_reinhard((largest - knee) / range, (WHITE * peak - knee) / range);\n" \
	"\n" \
	"\t\t\tcolor *= rolled / largest;\n" \
	"\t\t}\n" \
	"\t}\n" \
	"\telse if (tone.x < 1.5)\n" \
	"\t{\n" \
	"\t\tvec3 x = color / peak;\n" \
	"\n" \
	"\t\tcolor = peak * clamp(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);\n" \
	"\t}\n" \
	"\telse\n" \
	"\t{\n" \
	"\t\tfloat luminance = dot(color, vec3(0.2126, 0.7152, 0.0722)) / peak;\n" \
	"\n" \
	"\t\tif (luminance > 0.0)\n" \
	"\t\t\tcolor *= extended_reinhard(luminance, WHITE) / luminance;\n" \
	"\t}\n" \
	"\treturn pow(min(color, vec3(peak)) / tone.w, vec3(1.0 / GAMMA));\n" \
	"}\n"

/* ---------- the present's encoding (d3d8_bgfx.inl)

pq_encode(c, encoding): the frame's gamma-2.2 color, as an HDR10 swap chain
takes it: linear, times the UI's white (encoding.z), at most the peak
(encoding.w, in whites), times the paper white in units of 10 000 nits
(encoding.y), Rec. 709 to Rec. 2020 (the same colors: nothing is made more
saturated), then SMPTE ST 2084's curve. encoding.x is 1 for HDR10 and 0 for
the window as it is; the shaders test it (xgpu_tone_map_encoding). */
#define XGPU_PQ_ENCODE_GLSL \
	"vec3 pq_encode(vec3 encoded, vec4 encoding)\n" \
	"{\n" \
	"\tvec3 light = min(pow(max(encoded, vec3(0.0)), vec3(2.2)) * encoding.z, vec3(encoding.w)) * encoding.y;\n" \
	"\tvec3 wide = vec3(dot(light, vec3(0.6274, 0.3293, 0.0433)), dot(light, vec3(0.0691, 0.9195, 0.0114)),\n" \
	"\t\tdot(light, vec3(0.0164, 0.0880, 0.8956)));\n" \
	"\tvec3 y = pow(clamp(wide, vec3(0.0), vec3(1.0)), vec3(0.1593017578125));\n" \
	"\treturn pow((vec3(0.8359375) + 18.8515625 * y) / (vec3(1.0) + 18.6875 * y), vec3(78.84375));\n" \
	"}\n"

#endif
