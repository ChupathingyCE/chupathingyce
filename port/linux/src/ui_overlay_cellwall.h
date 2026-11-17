/*
UI_OVERLAY_CELLWALL.H

The cellwall (ui_overlay_cellwall): the Xbox dashboard's background as the
TeamUIX site (teamuix.net) draws it, a sphere seen from inside with a mesh
pattern on it, turning slowly, fogged to black with distance. Here the
sphere is not a mesh but worked out for each pixel: the ray from the eye
through it meets the sphere, the meeting point's place on the sphere is
the pattern's (50 repeats around, 25 from pole to pole, as the site's
texture), and the pattern itself (the site's mesh_pattern.svg: a square's
edges, its diagonal and two three-way joins) is drawn from its lines'
distances rather than read from a texture. No texture, nothing stored.

The function, in GLSL, for the overlay's fragment shaders (ui_overlay.c's
OpenGL one and d3d8_bgfx.inl's), which call it as

	cellwall(ray, motion, side, front, fill)

ray: the eye's ray through the pixel, at a distance of 1 (x right, y up; the
eye looks down -z), interpolated from the quad's corners; motion: the
sphere's turn (radians), the eye's place (x, y) and the pattern's
brightness; side and front: the tint where the wall is seen edge on and
face on (the dashboard's falloff colors, Theseus's per theme); fill: the
color between the lines and its alpha. Needs fwidth (not OpenGL ES 2).

From Milenko's cellwall on teamuix.net (its camera, fog, sphere, pattern
and movement), with the dashboard's colors from his Theseus.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __UI_OVERLAY_CELLWALL_H
#define __UI_OVERLAY_CELLWALL_H

#define UI_OVERLAY_CELLWALL_GLSL \
	"float cellwall_segment(vec2 p, vec2 a, vec2 b)\n" \
	"{\n" \
	"\tvec2 pa = p - a, ba = b - a;\n" \
	"\treturn length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0));\n" \
	"}\n" \
	/* a line half_width wide at distance from it, w the pixel's width: \
	smooth, and one thinner than a pixel faded as a texture's smaller levels \
	would */ \
	"float cellwall_line(float distance, float half_width, float w)\n" \
	"{\n" \
	"\tfloat e = max(half_width, 0.5 * w);\n" \
	"\treturn clamp((e - distance) / max(w, 0.0001) + 0.5, 0.0, 1.0) * (half_width / e);\n" \
	"}\n" \
	"vec4 cellwall(vec2 ray, vec4 motion, vec4 side, vec4 front, vec4 fill)\n" \
	"{\n" \
	/* the eye inside the sphere (radius 1), as the site's camera */ \
	"\tvec3 o = vec3(motion.y, motion.z, 0.65);\n" \
	"\tvec3 d = vec3(ray, -1.0);\n" \
	"\tfloat a = dot(d, d), b = dot(o, d), c = dot(o, o) - 1.0;\n" \
	"\tfloat t = (-b + sqrt(max(b * b - a * c, 0.0))) / a;\n" \
	"\tvec3 p = o + t * d;\n" \
	/* the sphere's turn about its upright axis */ \
	"\tfloat cs = cos(motion.x), sn = sin(motion.x);\n" \
	"\tvec3 q = vec3(cs * p.x - sn * p.z, p.y, sn * p.x + cs * p.z);\n" \
	/* its place in the pattern: three.js's sphere's (turned inside out), \
	repeated 50 by -25; the pixel's size in it measured on both sides of \
	the seam, so the seam has none */ \
	"\tfloat u = atan(q.z, q.x) * 0.15915494;\n" \
	"\tfloat v = 1.0 - acos(clamp(q.y, -1.0, 1.0)) * 0.31830989;\n" \
	"\tvec2 tc = vec2(u * 50.0, v * -25.0);\n" \
	"\tfloat seam = fract(u + 0.5) * 50.0;\n" \
	"\tfloat w = max(min(fwidth(tc.x), fwidth(seam)), fwidth(tc.y)) * 33.866667;\n" \
	"\tvec2 f = fract(tc);\n" \
	"\tvec2 s = vec2(f.x, 1.0 - f.y) * 33.866667;\n" \
	/* the pattern's lines (mesh_pattern.svg, its units) */ \
	"\tfloat edge = min(min(s.x, 33.866667 - s.x), min(s.y, 33.866667 - s.y));\n" \
	"\tfloat lines = cellwall_line(edge, 0.529167, w);\n" \
	"\tfloat dl = cellwall_segment(s, vec2(0.0), vec2(33.866667));\n" \
	"\tdl = min(dl, cellwall_segment(s, vec2(22.053307, 12.062727), vec2(16.890978, 16.975687)));\n" \
	"\tdl = min(dl, cellwall_segment(s, vec2(22.053307, 12.062727), vec2(16.933333, 0.0)));\n" \
	"\tdl = min(dl, cellwall_segment(s, vec2(22.053307, 12.062727), vec2(33.866667, 16.933333)));\n" \
	"\tdl = min(dl, cellwall_segment(s, vec2(11.728649, 21.888647), vec2(16.890978, 16.975687)));\n" \
	"\tdl = min(dl, cellwall_segment(s, vec2(11.728649, 21.888647), vec2(16.933333, 33.866667)));\n" \
	"\tdl = min(dl, cellwall_segment(s, vec2(11.728649, 21.888647), vec2(0.0, 16.933333)));\n" \
	"\tlines = max(lines, cellwall_line(dl, 0.396875, w));\n" \
	/* the wall's tint: face on, front's; edge on, side's */ \
	"\tfloat facing = clamp(dot(p, normalize(d)), 0.0, 1.0);\n" \
	"\tvec3 tint = mix(side.rgb, front.rgb, facing);\n" \
	"\tvec3 color = mix(fill.rgb, tint * motion.w, lines);\n" \
	/* the site's fog: black from 0.2 to 2.45 deep */ \
	"\tcolor *= 1.0 - smoothstep(0.2, 2.45, t);\n" \
	"\treturn vec4(color, fill.a);\n" \
	"}\n"

#endif
