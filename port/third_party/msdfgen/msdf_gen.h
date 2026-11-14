/*
MSDF_GEN.H

A glyph's multi-channel signed distance field (MSDF) from its outline: what
the high-res text is drawn from (port/linux/src/text_hires.c), each glyph's
field made once, as it is first drawn, and good for every size of the
display. Included, its functions static, by text_hires.c and by the test
(port/linux/tests/text_fields_test.c), so that both make the same fields.

Translated to C from Viktor Chlumsky's msdfgen (MIT; this folder's LICENSE
and README.md say which parts, and from which commit), the technique of his
thesis, "Shape Decomposition for Multi-channel Distance Fields" (Czech
Technical University in Prague, 2015). The outline's edges are coloured so
that each corner sits between edges of two different colours, each of red,
green and blue holds the pseudo-distance to the nearest edge of its colour,
and the median of the three is the distance to the outline, with its
corners kept sharp. The fourth channel (alpha) is the true signed distance,
for effects whose reach is wider than the corners' (an outline, a glow, a
shadow). Each texel's inside or outside is the outline's own (its non-zero
winding), the channels turned about where their median says otherwise (the
fonts' overlapping contours), and the texels whose channels would clash
when filtered with a neighbour's are made plain distance (msdfgen's error
correction).

Copyright (c) 2014 - 2025 Viktor Chlumsky (msdfgen, MIT License: the parts
translated from it). The translation and the rest are by goatbutt and the
OpenH1 team (OpenH1 commits dc9cb43b and bf24d22c), who relicensed their
work to this project under the GPL-3.0-or-later with credit; this copy is
theirs, its header rewritten for this tree.

The outline is stb_truetype's (stbtt_vertex: lines, quadratic and cubic
curves, in font units, y up). Values are stored as 0 to 255: 128 on the
outline, inside above, range texels of distance from one end to the other.

SPDX-FileCopyrightText: 2014-2025 Viktor Chlumsky
SPDX-FileCopyrightText: 2026 goatbutt and the OpenH1 team
SPDX-License-Identifier: MIT AND GPL-3.0-or-later
*/

#ifndef MSDFGEN_MSDF_GEN_H
#define MSDFGEN_MSDF_GEN_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* the edges a glyph may have (more: no field, the glyph drawn without) */
#define MSDF_MAXIMUM_EDGES 1024
#define MSDF_MAXIMUM_CONTOURS 128

enum
{
	_msdf_red = 1,
	_msdf_green = 2,
	_msdf_blue = 4,
	_msdf_yellow = 3,
	_msdf_magenta = 5,
	_msdf_cyan = 6,
	_msdf_white = 7,
};

struct msdf_edge
{
	/* its points: a line's first two, a quadratic curve's three */
	double p[3][2];
	int quadratic;
	int color;
};

struct msdf_shape
{
	struct msdf_edge edges[MSDF_MAXIMUM_EDGES];
	int edge_count;
	/* each contour's first edge; the last's end is edge_count */
	int contours[MSDF_MAXIMUM_CONTOURS + 1];
	int contour_count;
};

static double msdf_cross(const double a[2], const double b[2])
{
	return a[0] * b[1] - a[1] * b[0];
}

static double msdf_dot(const double a[2], const double b[2])
{
	return a[0] * b[0] + a[1] * b[1];
}

static void msdf_normalize(double v[2])
{
	double length = sqrt(v[0] * v[0] + v[1] * v[1]);

	if (length > 0.0)
	{
		v[0] /= length;
		v[1] /= length;
	}
}

static double msdf_sign(double value)
{
	return value > 0.0 ? 1.0 : -1.0;
}

static void msdf_point(const struct msdf_edge *edge, double t, double out[2])
{
	int axis;

	for (axis = 0; axis < 2; axis++)
	{
		if (edge->quadratic)
		{
			out[axis] = (1.0 - t) * (1.0 - t) * edge->p[0][axis] + 2.0 * (1.0 - t) * t * edge->p[1][axis] +
				t * t * edge->p[2][axis];
		}
		else
		{
			out[axis] = edge->p[0][axis] + t * (edge->p[1][axis] - edge->p[0][axis]);
		}
	}
}

/* the edge's direction at t (0 or 1 at its ends) */
static void msdf_direction(const struct msdf_edge *edge, double t, double out[2])
{
	int axis;

	if (!edge->quadratic)
	{
		out[0] = edge->p[1][0] - edge->p[0][0];
		out[1] = edge->p[1][1] - edge->p[0][1];
		return;
	}
	for (axis = 0; axis < 2; axis++)
		out[axis] = (1.0 - t) * (edge->p[1][axis] - edge->p[0][axis]) + t * (edge->p[2][axis] - edge->p[1][axis]);
	/* (a control point on an end) */
	if (out[0] == 0.0 && out[1] == 0.0)
	{
		out[0] = edge->p[2][0] - edge->p[0][0];
		out[1] = edge->p[2][1] - edge->p[0][1];
	}
}

/* the real roots of a x^2 + b x + c (msdfgen's equation-solver) */
static int msdf_solve_quadratic(double x[2], double a, double b, double c)
{
	double discriminant;

	if (a == 0.0 || fabs(b) > 1e12 * fabs(a))
	{
		if (b == 0.0)
			return 0;
		x[0] = -c / b;
		return 1;
	}
	discriminant = b * b - 4.0 * a * c;
	if (discriminant > 0.0)
	{
		discriminant = sqrt(discriminant);
		x[0] = (-b + discriminant) / (2.0 * a);
		x[1] = (-b - discriminant) / (2.0 * a);
		return 2;
	}
	if (discriminant == 0.0)
	{
		x[0] = -b / (2.0 * a);
		return 1;
	}
	return 0;
}

static int msdf_solve_cubic_normed(double x[3], double a, double b, double c)
{
	double a2 = a * a, q = (a2 - 3.0 * b) / 9.0, r = (a * (2.0 * a2 - 9.0 * b) + 27.0 * c) / 54.0;
	double r2 = r * r, q3 = q * q * q;

	a /= 3.0;
	if (r2 < q3)
	{
		double t = r / sqrt(q3);

		if (t < -1.0)
			t = -1.0;
		if (t > 1.0)
			t = 1.0;
		t = acos(t);
		q = -2.0 * sqrt(q);
		x[0] = q * cos(t / 3.0) - a;
		x[1] = q * cos((t + 2.0 * 3.14159265358979323846) / 3.0) - a;
		x[2] = q * cos((t - 2.0 * 3.14159265358979323846) / 3.0) - a;
		return 3;
	}
	else
	{
		double u = (r < 0.0 ? 1.0 : -1.0) * pow(fabs(r) + sqrt(r2 - q3), 1.0 / 3.0);
		double v = u == 0.0 ? 0.0 : q / u;

		x[0] = (u + v) - a;
		if (u == v || fabs(u - v) < 1e-12 * fabs(u + v))
		{
			x[1] = -0.5 * (u + v) - a;
			return 2;
		}
		return 1;
	}
}

static int msdf_solve_cubic(double x[3], double a, double b, double c, double d)
{
	if (a != 0.0)
	{
		double bn = b / a;

		/* (a close to zero: a quadratic, more exactly) */
		if (fabs(bn) < 1e6)
			return msdf_solve_cubic_normed(x, bn, c / a, d / a);
	}
	return msdf_solve_quadratic(x, b, c, d);
}

/* the signed distance from origin to the edge (negative on its right, as
its direction goes), and the end's dot (how far off along the edge's own
line an end that is nearest is: ties go to the edge most across), and
where on the edge (param, outside 0 to 1 past an end) */
static void msdf_edge_distance(const struct msdf_edge *edge, const double origin[2], double *distance, double *dot,
	double *param)
{
	if (!edge->quadratic)
	{
		double aq[2], ab[2], eq[2], length, endpoint;
		int end;

		aq[0] = origin[0] - edge->p[0][0];
		aq[1] = origin[1] - edge->p[0][1];
		ab[0] = edge->p[1][0] - edge->p[0][0];
		ab[1] = edge->p[1][1] - edge->p[0][1];
		*param = msdf_dot(aq, ab) / msdf_dot(ab, ab);
		end = *param > 0.5;
		eq[0] = edge->p[end][0] - origin[0];
		eq[1] = edge->p[end][1] - origin[1];
		endpoint = sqrt(msdf_dot(eq, eq));
		if (*param > 0.0 && *param < 1.0)
		{
			length = sqrt(msdf_dot(ab, ab));
			if (fabs(msdf_cross(aq, ab) / length) < endpoint)
			{
				*distance = msdf_cross(aq, ab) / length;
				*dot = 0.0;
				return;
			}
		}
		*distance = msdf_sign(msdf_cross(aq, ab)) * endpoint;
		msdf_normalize(ab);
		msdf_normalize(eq);
		*dot = fabs(msdf_dot(ab, eq));
		return;
	}
	else
	{
		double qa[2], ab[2], br[2], direction[2], to_end[2], t[3], minimum, length;
		int count, index;

		qa[0] = edge->p[0][0] - origin[0];
		qa[1] = edge->p[0][1] - origin[1];
		ab[0] = edge->p[1][0] - edge->p[0][0];
		ab[1] = edge->p[1][1] - edge->p[0][1];
		br[0] = edge->p[2][0] - edge->p[1][0] - ab[0];
		br[1] = edge->p[2][1] - edge->p[1][1] - ab[1];
		count = msdf_solve_cubic(t, msdf_dot(br, br), 3.0 * msdf_dot(ab, br), 2.0 * msdf_dot(ab, ab) + msdf_dot(qa, br),
			msdf_dot(qa, ab));
		msdf_direction(edge, 0.0, direction);
		minimum = msdf_sign(msdf_cross(direction, qa)) * sqrt(msdf_dot(qa, qa));
		*param = -msdf_dot(qa, direction) / msdf_dot(direction, direction);
		msdf_direction(edge, 1.0, direction);
		to_end[0] = edge->p[2][0] - origin[0];
		to_end[1] = edge->p[2][1] - origin[1];
		length = sqrt(msdf_dot(to_end, to_end));
		if (length < fabs(minimum))
		{
			double from_control[2];

			minimum = msdf_sign(msdf_cross(direction, to_end)) * length;
			from_control[0] = origin[0] - edge->p[1][0];
			from_control[1] = origin[1] - edge->p[1][1];
			*param = msdf_dot(from_control, direction) / msdf_dot(direction, direction);
		}
		for (index = 0; index < count; index++)
		{
			if (t[index] > 0.0 && t[index] < 1.0)
			{
				double qe[2], tangent[2];

				qe[0] = qa[0] + 2.0 * t[index] * ab[0] + t[index] * t[index] * br[0];
				qe[1] = qa[1] + 2.0 * t[index] * ab[1] + t[index] * t[index] * br[1];
				length = sqrt(msdf_dot(qe, qe));
				if (length <= fabs(minimum))
				{
					tangent[0] = ab[0] + t[index] * br[0];
					tangent[1] = ab[1] + t[index] * br[1];
					minimum = msdf_sign(msdf_cross(tangent, qe)) * length;
					*param = t[index];
				}
			}
		}
		*distance = minimum;
		if (*param >= 0.0 && *param <= 1.0)
		{
			*dot = 0.0;
			return;
		}
		if (*param < 0.5)
		{
			msdf_direction(edge, 0.0, direction);
			msdf_normalize(direction);
			msdf_normalize(qa);
			*dot = fabs(msdf_dot(direction, qa));
		}
		else
		{
			msdf_direction(edge, 1.0, direction);
			msdf_normalize(direction);
			msdf_normalize(to_end);
			*dot = fabs(msdf_dot(direction, to_end));
		}
	}
}

/* a distance past an end made the distance to the edge's tangent there,
where that is nearer (the pseudo-distance: msdfgen's
distanceToPseudoDistance) */
static double msdf_pseudo_distance(const struct msdf_edge *edge, const double origin[2], double distance, double param)
{
	double direction[2], end[2], aq[2], ts, pseudo;

	if (param >= 0.0 && param <= 1.0)
		return distance;
	msdf_direction(edge, param < 0.0 ? 0.0 : 1.0, direction);
	msdf_normalize(direction);
	msdf_point(edge, param < 0.0 ? 0.0 : 1.0, end);
	aq[0] = origin[0] - end[0];
	aq[1] = origin[1] - end[1];
	ts = msdf_dot(aq, direction);
	if (param < 0.0 ? ts < 0.0 : ts > 0.0)
	{
		pseudo = msdf_cross(aq, direction);
		if (fabs(pseudo) <= fabs(distance))
			return pseudo;
	}
	return distance;
}

/* ---------- the shape */

static int msdf_add(struct msdf_shape *shape, const double *p0, const double *p1, const double *p2)
{
	struct msdf_edge *edge;

	if (shape->edge_count >= MSDF_MAXIMUM_EDGES)
		return 0;
	edge = &shape->edges[shape->edge_count];
	memset(edge, 0, sizeof(*edge));
	edge->p[0][0] = p0[0];
	edge->p[0][1] = p0[1];
	if (p2 && !((p1[0] == p0[0] && p1[1] == p0[1]) || (p1[0] == p2[0] && p1[1] == p2[1])))
	{
		edge->quadratic = 1;
		edge->p[1][0] = p1[0];
		edge->p[1][1] = p1[1];
		edge->p[2][0] = p2[0];
		edge->p[2][1] = p2[1];
	}
	else
	{
		const double *end = p2 ? p2 : p1;

		/* (no length) */
		if (end[0] == p0[0] && end[1] == p0[1])
			return 1;
		edge->p[1][0] = end[0];
		edge->p[1][1] = end[1];
	}
	shape->edge_count++;
	return 1;
}

static void msdf_end_point(const struct msdf_edge *edge, double out[2])
{
	msdf_point(edge, 1.0, out);
}

/* a contour ended (closed with a line where it is not), empty ones dropped */
static void msdf_contour_end(struct msdf_shape *shape, const double start[2], const double pen[2])
{
	int first = shape->contours[shape->contour_count];

	if (shape->edge_count > first && (pen[0] != start[0] || pen[1] != start[1]))
		msdf_add(shape, pen, start, NULL);
	if (shape->edge_count > first && shape->contour_count < MSDF_MAXIMUM_CONTOURS)
		shape->contours[++shape->contour_count] = shape->edge_count;
	else
		shape->edge_count = first;
}

/* stb_truetype's outline (types 1 move, 2 line, 3 quadratic, 4 cubic) made
the shape; 0 if it has too many edges */
struct msdf_vertex
{
	int type;
	double x, y, cx, cy, cx1, cy1;
};

static int msdf_shape_make(struct msdf_shape *shape, const struct msdf_vertex *vertices, int count)
{
	double start[2] = { 0.0, 0.0 }, pen[2] = { 0.0, 0.0 };
	int index, open = 0;

	shape->edge_count = 0;
	shape->contour_count = 0;
	shape->contours[0] = 0;
	for (index = 0; index < count; index++)
	{
		const struct msdf_vertex *vertex = &vertices[index];
		double to[2], control[2];

		to[0] = vertex->x;
		to[1] = vertex->y;
		switch (vertex->type)
		{
		case 1:
			if (open)
				msdf_contour_end(shape, start, pen);
			if (shape->contour_count >= MSDF_MAXIMUM_CONTOURS)
				return 0;
			start[0] = pen[0] = to[0];
			start[1] = pen[1] = to[1];
			open = 1;
			continue;
		case 2:
			if (!msdf_add(shape, pen, to, NULL))
				return 0;
			break;
		case 3:
			control[0] = vertex->cx;
			control[1] = vertex->cy;
			if (!msdf_add(shape, pen, control, to))
				return 0;
			break;
		case 4:
		{
			/* (a cubic curve as four quadratic ones, each over a quarter
			of it: the fonts the game ships have none) */
			int part;
			double a[2], b[2], c[2], d[2];

			a[0] = pen[0];
			a[1] = pen[1];
			b[0] = vertex->cx;
			b[1] = vertex->cy;
			c[0] = vertex->cx1;
			c[1] = vertex->cy1;
			d[0] = to[0];
			d[1] = to[1];
			for (part = 0; part < 4; part++)
			{
				double t0 = part / 4.0, t1 = (part + 1) / 4.0, from[2], end[2], quad[2], tangent0[2], tangent1[2];
				int axis;

				for (axis = 0; axis < 2; axis++)
				{
					double u0 = 1.0 - t0, u1 = 1.0 - t1;

					from[axis] = u0 * u0 * u0 * a[axis] + 3 * u0 * u0 * t0 * b[axis] + 3 * u0 * t0 * t0 * c[axis] +
						t0 * t0 * t0 * d[axis];
					end[axis] = u1 * u1 * u1 * a[axis] + 3 * u1 * u1 * t1 * b[axis] + 3 * u1 * t1 * t1 * c[axis] +
						t1 * t1 * t1 * d[axis];
					tangent0[axis] = 3 * (u0 * u0 * (b[axis] - a[axis]) + 2 * u0 * t0 * (c[axis] - b[axis]) +
						t0 * t0 * (d[axis] - c[axis]));
					tangent1[axis] = 3 * (u1 * u1 * (b[axis] - a[axis]) + 2 * u1 * t1 * (c[axis] - b[axis]) +
						t1 * t1 * (d[axis] - c[axis]));
					/* (the control point between the ends' tangents,
					averaged) */
					quad[axis] = 0.5 * ((from[axis] + tangent0[axis] * (t1 - t0) * 0.5) +
						(end[axis] - tangent1[axis] * (t1 - t0) * 0.5));
				}
				if (!msdf_add(shape, from, quad, end))
					return 0;
			}
			break;
		}
		default:
			continue;
		}
		pen[0] = to[0];
		pen[1] = to[1];
	}
	if (open)
		msdf_contour_end(shape, start, pen);
	return 1;
}

/* the edge split in thirds, in place of it and the two after it */
static void msdf_split_thirds(const struct msdf_edge *edge, struct msdf_edge parts[3])
{
	int part;

	for (part = 0; part < 3; part++)
	{
		double t0 = part / 3.0, t1 = (part + 1) / 3.0;

		memset(&parts[part], 0, sizeof(parts[part]));
		parts[part].quadratic = edge->quadratic;
		msdf_point(edge, t0, parts[part].p[0]);
		if (edge->quadratic)
		{
			int axis;

			msdf_point(edge, t1, parts[part].p[2]);
			/* (the part's control point: along its start's tangent) */
			for (axis = 0; axis < 2; axis++)
			{
				parts[part].p[1][axis] = parts[part].p[0][axis] + (t1 - t0) *
					((1.0 - t0) * (edge->p[1][axis] - edge->p[0][axis]) + t0 * (edge->p[2][axis] - edge->p[1][axis]));
			}
		}
		else
		{
			msdf_point(edge, t1, parts[part].p[1]);
		}
	}
}

/* the next colour after a corner (msdfgen's switchColor), not banned's */
static int msdf_switch_color(int color, unsigned long *seed, int banned)
{
	int combined = color & banned;

	if (combined == _msdf_red || combined == _msdf_green || combined == _msdf_blue)
		return combined ^ _msdf_white;
	if (color == 0 || color == _msdf_white)
	{
		static const int start[3] = { _msdf_cyan, _msdf_magenta, _msdf_yellow };
		int chosen = start[*seed % 3];

		*seed /= 3;
		return chosen;
	}
	{
		int shifted = color << (1 + (*seed & 1));

		*seed >>= 1;
		return (shifted | shifted >> 3) & _msdf_white;
	}
}

static int msdf_is_corner(const double a[2], const double b[2])
{
	/* (sin 3, msdfgen's angle threshold) */
	return msdf_dot(a, b) <= 0.0 || fabs(msdf_cross(a, b)) > 0.14112000805986721;
}

/* the edges coloured (msdfgen's edgeColoringSimple); 0 if a contour's edges
cannot be split for room */
static int msdf_color_edges(struct msdf_shape *shape)
{
	unsigned long seed = 0;
	int contour;

	for (contour = 0; contour < shape->contour_count; contour++)
	{
		int first = shape->contours[contour], count = shape->contours[contour + 1] - first;
		int corners[MSDF_MAXIMUM_EDGES];
		int corner_count = 0, index;
		double previous[2];

		msdf_direction(&shape->edges[first + count - 1], 1.0, previous);
		msdf_normalize(previous);
		for (index = 0; index < count; index++)
		{
			double direction[2];

			msdf_direction(&shape->edges[first + index], 0.0, direction);
			msdf_normalize(direction);
			if (msdf_is_corner(previous, direction))
				corners[corner_count++] = index;
			msdf_direction(&shape->edges[first + index], 1.0, previous);
			msdf_normalize(previous);
		}
		if (corner_count == 0)
		{
			for (index = 0; index < count; index++)
				shape->edges[first + index].color = _msdf_white;
		}
		else if (corner_count == 1)
		{
			/* (a teardrop: three colours along it, its edges split where it
			has fewer than three) */
			int colors[3], corner = corners[0];

			colors[0] = msdf_switch_color(_msdf_white, &seed, 0);
			colors[1] = _msdf_white;
			colors[2] = msdf_switch_color(colors[0], &seed, 0);
			if (count < 3)
			{
				struct msdf_edge parts[6];
				int part, made = count * 3, shift;

				if (shape->edge_count + made - count > MSDF_MAXIMUM_EDGES)
					return 0;
				for (index = 0; index < count; index++)
					msdf_split_thirds(&shape->edges[first + index], &parts[index * 3]);
				/* (the parts in place of the edges, those after moved on) */
				shift = made - count;
				memmove(&shape->edges[first + made], &shape->edges[first + count],
					sizeof(shape->edges[0]) * (size_t)(shape->edge_count - first - count));
				for (index = contour + 1; index <= shape->contour_count; index++)
					shape->contours[index] += shift;
				shape->edge_count += shift;
				/* (from the corner on) */
				for (part = 0; part < made; part++)
				{
					int at = (part + corner * 3) % made;

					shape->edges[first + part] = parts[at];
					shape->edges[first + part].color = colors[made == 3 ? part : part / 2];
				}
			}
			else
			{
				for (index = 0; index < count; index++)
				{
					/* (msdfgen's symmetricalTrichotomy: -1, 0, 1) */
					int third = (int)(3.0 + 2.875 * index / (count - 1) - 1.4375 + 0.5) - 3;

					shape->edges[first + (corner + index) % count].color = colors[1 + third];
				}
			}
		}
		else
		{
			int spline = 0, start = corners[0], color = _msdf_white, initial;

			color = msdf_switch_color(color, &seed, 0);
			initial = color;
			for (index = 0; index < count; index++)
			{
				int at = (start + index) % count;

				if (spline + 1 < corner_count && corners[spline + 1] == at)
				{
					spline++;
					color = msdf_switch_color(color, &seed, spline == corner_count - 1 ? initial : 0);
				}
				shape->edges[first + at].color = color;
			}
		}
	}
	return 1;
}

/* the outline's non-zero winding about a point (curves cut in lines: the
fill is only the sign, near the outline its distance is nearly nothing) */
static int msdf_winding(const struct msdf_shape *shape, const double origin[2])
{
	int winding = 0, index;

	for (index = 0; index < shape->edge_count; index++)
	{
		const struct msdf_edge *edge = &shape->edges[index];
		int pieces = edge->quadratic ? 8 : 1, piece;
		double from[2], to[2];

		msdf_point(edge, 0.0, from);
		for (piece = 1; piece <= pieces; piece++)
		{
			msdf_point(edge, (double)piece / pieces, to);
			if ((from[1] <= origin[1]) != (to[1] <= origin[1]))
			{
				double x = from[0] + (origin[1] - from[1]) * (to[0] - from[0]) / (to[1] - from[1]);

				if (x > origin[0])
					winding += to[1] > from[1] ? 1 : -1;
			}
			from[0] = to[0];
			from[1] = to[1];
		}
	}
	return winding;
}

static float msdf_median(float a, float b, float c)
{
	float low = a < b ? a : b, high = a < b ? b : a;

	return c < low ? low : c > high ? high : c;
}

/* whether a texel's channels clash with a neighbour's when filtered, and it
is the one of the two further from the outline (msdfgen's detectClash) */
static int msdf_clash(const float *a, const float *b, float threshold)
{
	float a0 = a[0], a1 = a[1], a2 = a[2], b0 = b[0], b1 = b[1], b2 = b[2], swap;

	if (fabsf(b0 - a0) < fabsf(b1 - a1))
	{
		swap = a0; a0 = a1; a1 = swap;
		swap = b0; b0 = b1; b1 = swap;
	}
	if (fabsf(b1 - a1) < fabsf(b2 - a2))
	{
		swap = a1; a1 = a2; a2 = swap;
		swap = b1; b1 = b2; b2 = swap;
		if (fabsf(b0 - a0) < fabsf(b1 - a1))
		{
			swap = a0; a0 = a1; a1 = swap;
			swap = b0; b0 = b1; b1 = swap;
		}
	}
	return fabsf(b1 - a1) >= threshold && !(b0 == b1 && b0 == b2) && fabsf(a2 - 0.5f) >= fabsf(b2 - 0.5f);
}

/* The field of a glyph's outline into rgba (width by height texels, 4 bytes
each, rows from the top): texel (x, y)'s centre at font units
(left + (x + 0.5) / scale, top - (y + 0.5) / scale), scale texels to the
font unit, range texels from the field's lowest to its highest. 0 (rgba
untouched) if the outline is more than it holds. */
static int msdf_generate(const struct msdf_vertex *vertices, int count, double scale, double left, double top,
	double range, int width, int height, unsigned char *rgba)
{
	struct msdf_shape *shape;
	float *field;
	unsigned char *clashes;
	int x, y, flipped = 0, mismatched = 0;
	float threshold;

	if (width <= 0 || height <= 0 || scale <= 0.0 || range <= 0.0)
		return 0;
	shape = (struct msdf_shape *)malloc(sizeof(*shape));
	field = (float *)malloc(sizeof(float) * 4 * (size_t)width * (size_t)height);
	clashes = (unsigned char *)calloc((size_t)width * (size_t)height, 1);
	if (!shape || !field || !clashes || !msdf_shape_make(shape, vertices, count) || !msdf_color_edges(shape))
	{
		free(shape);
		free(field);
		free(clashes);
		return 0;
	}
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			double origin[2], best[3] = { 1e30, 1e30, 1e30 }, best_dot[3] = { 1e30, 1e30, 1e30 };
			double best_param[3] = { 0.0, 0.0, 0.0 }, nearest = 1e30;
			int best_edge[3] = { -1, -1, -1 }, channel, index;
			float *texel = &field[((size_t)y * width + x) * 4];

			origin[0] = left + (x + 0.5) / scale;
			origin[1] = top - (y + 0.5) / scale;
			for (index = 0; index < shape->edge_count; index++)
			{
				double distance, dot, param;

				msdf_edge_distance(&shape->edges[index], origin, &distance, &dot, &param);
				if (fabs(distance) < fabs(nearest))
					nearest = distance;
				for (channel = 0; channel < 3; channel++)
				{
					if (!(shape->edges[index].color & (1 << channel)))
						continue;
					if (fabs(distance) < fabs(best[channel]) ||
						(fabs(distance) == fabs(best[channel]) && dot < best_dot[channel]))
					{
						best[channel] = distance;
						best_dot[channel] = dot;
						best_param[channel] = param;
						best_edge[channel] = index;
					}
				}
			}
			for (channel = 0; channel < 3; channel++)
			{
				double distance = best_edge[channel] < 0 ? -1e30 :
					msdf_pseudo_distance(&shape->edges[best_edge[channel]], origin, best[channel], best_param[channel]);

				texel[channel] = (float)(distance * scale / range + 0.5);
			}
			texel[3] = (float)(fabs(nearest) * scale / range);
			/* (the inside, by the outline's winding: the alpha's sign) */
			if (msdf_winding(shape, origin) != 0)
			{
				texel[3] = 0.5f + texel[3];
			}
			else
			{
				texel[3] = 0.5f - texel[3];
			}
		}
	}
	/* (an outline wound the other way round: the edges' sides turned over;
	the fonts' outer contours go clockwise, as TrueType's do) */
	for (y = 0; y < height * width; y++)
	{
		float *texel = &field[(size_t)y * 4];
		float median = msdf_median(texel[0], texel[1], texel[2]);

		if (median != 0.5f && (median > 0.5f) != (texel[3] > 0.5f))
			mismatched++;
	}
	if (mismatched * 2 > height * width)
		flipped = 1;
	for (y = 0; y < height * width; y++)
	{
		float *texel = &field[(size_t)y * 4];
		float median;
		int channel;

		if (flipped)
		{
			for (channel = 0; channel < 3; channel++)
				texel[channel] = 1.0f - texel[channel];
		}
		/* (each texel on the side of the outline its winding puts it: the
		channels turned about the outline where their median is not) */
		median = msdf_median(texel[0], texel[1], texel[2]);
		if (median != 0.5f && (median > 0.5f) != (texel[3] > 0.5f))
		{
			for (channel = 0; channel < 3; channel++)
				texel[channel] = 1.0f - texel[channel];
		}
	}
	/* (msdfgen's error correction: a texel whose channels clash with a
	neighbour's when filtered made its median, the distance alone; a
	difference of more than one texel of distance) */
	threshold = (float)(1.001 / range);
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			const float *texel = &field[((size_t)y * width + x) * 4];

			if ((x > 0 && msdf_clash(texel, texel - 4, threshold)) ||
				(x < width - 1 && msdf_clash(texel, texel + 4, threshold)) ||
				(y > 0 && msdf_clash(texel, texel - 4 * width, threshold)) ||
				(y < height - 1 && msdf_clash(texel, texel + 4 * width, threshold)) ||
				(x > 0 && y > 0 && msdf_clash(texel, texel - 4 * width - 4, 2.0f * threshold)) ||
				(x < width - 1 && y > 0 && msdf_clash(texel, texel - 4 * width + 4, 2.0f * threshold)) ||
				(x > 0 && y < height - 1 && msdf_clash(texel, texel + 4 * width - 4, 2.0f * threshold)) ||
				(x < width - 1 && y < height - 1 && msdf_clash(texel, texel + 4 * width + 4, 2.0f * threshold)))
			{
				clashes[(size_t)y * width + x] = 1;
			}
		}
	}
	for (y = 0; y < height * width; y++)
	{
		float *texel = &field[(size_t)y * 4];
		int channel;

		if (clashes[y])
			texel[0] = texel[1] = texel[2] = msdf_median(texel[0], texel[1], texel[2]);
		for (channel = 0; channel < 4; channel++)
		{
			float value = texel[channel] * 255.0f + 0.5f;

			rgba[(size_t)y * 4 + channel] = (unsigned char)(value < 0.0f ? 0.0f : value > 255.0f ? 255.0f : value);
		}
	}
	free(shape);
	free(field);
	free(clashes);
	return 1;
}

#endif
