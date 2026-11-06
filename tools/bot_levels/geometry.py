"""Block geometry for the bots' test levels (tools/bot_levels): a level is a
floor of columns over a grid, closed by a sky ceiling, written as a JMS file
that the editing kit's tool.exe compiles into a structure BSP.

The level is the space between a floor and a ceiling. Over a rectilinear grid
(lines where any feature has an edge), each cell has a floor: flat at a
height, sloped (a height at each corner), or full (solid up to the ceiling: a
wall). The surface between solid and open space is written out: the cells'
tops, the vertical faces where neighbouring floors differ, and the ceiling
over every open cell (the sky). Every face is split where another face meets
it, so the mesh is closed and has no T-junctions, which tool.exe needs.

World units throughout (a player is about 0.7 tall); the JMS is in hundredths.
"""

import math
from collections import defaultdict


class Grid:
    """The level's floor: features painted onto a grid, last one wins."""

    RING = 1.0  # (the solid band round the edge of every level)

    def __init__(self, x0, y0, x1, y1, ceiling, cell=4.0):
        self.bounds = (x0, y0, x1, y1)
        self.ceiling = ceiling
        self.cell = cell  # the largest a cell is let be (open ground is split this fine)
        self.features = []  # (kind, rect, data, material)
        self.xs = {x0, x1, x0 + self.RING, x1 - self.RING}
        self.ys = {y0, y1, y0 + self.RING, y1 - self.RING}
        self.default_floor = 0.0
        self.default_material = "floor"

    # -- painting

    def _rect(self, x0, y0, x1, y1):
        x0, x1 = round(min(x0, x1), 4), round(max(x0, x1), 4)
        y0, y1 = round(min(y0, y1), 4), round(max(y0, y1), 4)
        self.xs.update((x0, x1))
        self.ys.update((y0, y1))
        return (x0, y0, x1, y1)

    def floor(self, height, material="floor"):
        self.default_floor = height
        self.default_material = material

    def box(self, x0, y0, x1, y1, height, material="block"):
        """a flat floor at a height over a rectangle (a platform, a pit)"""
        self.features.append(("flat", self._rect(x0, y0, x1, y1), height, material))

    def wall(self, x0, y0, x1, y1, material="wall"):
        """solid up to the ceiling"""
        self.features.append(("full", self._rect(x0, y0, x1, y1), None, material))

    def ramp(self, x0, y0, x1, y1, h0, h1, axis, material="ramp"):
        """a slope from h0 to h1 along an axis ('x' or 'y': h0 at the low
        coordinate's side)"""
        rect = self._rect(x0, y0, x1, y1)
        self.features.append(("ramp", rect, (h0, h1, axis), material))

    def terrain(self, x0, y0, x1, y1, function, material="ground", step=None):
        """rolling ground: the floor's height at each grid corner is
        function(x, y); split at most `step` apart"""
        rect = self._rect(x0, y0, x1, y1)
        step = step or self.cell
        nx = max(1, int(math.ceil((rect[2] - rect[0]) / step)))
        ny = max(1, int(math.ceil((rect[3] - rect[1]) / step)))
        for i in range(nx + 1):
            self.xs.add(rect[0] + (rect[2] - rect[0]) * i / nx)
        for j in range(ny + 1):
            self.ys.add(rect[1] + (rect[3] - rect[1]) * j / ny)
        self.features.append(("terrain", rect, function, material))

    def height_at(self, x, y):
        """the floor's height at a point, as the painting has it (the top of a
        wall there: the ceiling)"""
        h = self.default_floor
        bx0, by0, bx1, by1 = self.bounds
        if not (bx0 + self.RING < x < bx1 - self.RING and by0 + self.RING < y < by1 - self.RING):
            return self.ceiling
        for kind, (rx0, ry0, rx1, ry1), data, material in self.features:
            if not (rx0 <= x <= rx1 and ry0 <= y <= ry1):
                continue
            if kind == "flat":
                h = data
            elif kind == "full":
                h = self.ceiling
            elif kind == "ramp":
                h0, h1, axis = data
                h = h0 + (h1 - h0) * ((x - rx0) / (rx1 - rx0) if axis == "x" else (y - ry0) / (ry1 - ry0))
            elif kind == "terrain":
                h = data(x, y)
        return h

    # -- building

    @staticmethod
    def _split(values, largest):
        """the lines, sorted, none closer than a thousandth, with lines added
        so that no gap is more than `largest`"""
        values = sorted(round(v, 4) for v in values)
        lines = [values[0]]
        for v in values[1:]:
            if v - lines[-1] < 1e-3:
                continue
            gap = v - lines[-1]
            parts = int(math.ceil(gap / largest - 1e-9))
            start = lines[-1]
            for k in range(1, parts):
                lines.append(round(start + gap * k / parts, 4))
            lines.append(v)
        return lines

    def build(self):
        """the grid lines and every cell's floor: (xs, ys, cells), cells[i][j] =
        None (full) or (corner heights h00, h10, h11, h01, material)"""
        xs = self._split(self.xs, self.cell)
        ys = self._split(self.ys, self.cell)
        cells = []
        for i in range(len(xs) - 1):
            column = []
            for j in range(len(ys) - 1):
                cx0, cx1, cy0, cy1 = xs[i], xs[i + 1], ys[j], ys[j + 1]
                mx, my = (cx0 + cx1) / 2, (cy0 + cy1) / 2
                h = self.default_floor
                value = (h, h, h, h, self.default_material)
                for kind, (rx0, ry0, rx1, ry1), data, material in self.features:
                    if not (rx0 <= mx <= rx1 and ry0 <= my <= ry1):
                        continue
                    if kind == "flat":
                        value = (data, data, data, data, material)
                    elif kind == "full":
                        value = None
                    elif kind == "ramp":
                        h0, h1, axis = data
                        if axis == "x":
                            f = lambda x, y: h0 + (h1 - h0) * (x - rx0) / (rx1 - rx0)
                        else:
                            f = lambda x, y: h0 + (h1 - h0) * (y - ry0) / (ry1 - ry0)
                        value = (f(cx0, cy0), f(cx1, cy0), f(cx1, cy1), f(cx0, cy1), material)
                    elif kind == "terrain":
                        f = data
                        value = (f(cx0, cy0), f(cx1, cy0), f(cx1, cy1), f(cx0, cy1), material)
                if value is not None:
                    value = tuple(round(v, 4) for v in value[:4]) + (value[4],)
                column.append(value)
            cells.append(column)
        # the band round the edge is solid: the level is closed at its edges
        x0, y0, x1, y1 = self.bounds
        for i in range(len(xs) - 1):
            for j in range(len(ys) - 1):
                if xs[i + 1] <= x0 + self.RING + 1e-4 or xs[i] >= x1 - self.RING - 1e-4 or \
                        ys[j + 1] <= y0 + self.RING + 1e-4 or ys[j] >= y1 - self.RING - 1e-4:
                    cells[i][j] = None
        return xs, ys, cells


def mesh(grid):
    """the closed surface of a grid: (vertices, triangles, materials) with
    triangles as (a, b, c, material); faces facing into the open space"""
    xs, ys, cells = grid.build()
    top = grid.ceiling
    nx, ny = len(xs) - 1, len(ys) - 1

    def corner_heights(i, j):
        """the floor heights at grid vertex (i, j) of each cell round it, and
        the ceiling: every height a vertical face along that line has a vertex at"""
        heights = {round(top, 4)}
        for di, dj, k in ((-1, -1, 2), (0, -1, 3), (0, 0, 0), (-1, 0, 1)):
            ci, cj = i + di, j + dj
            if 0 <= ci < nx and 0 <= cj < ny and cells[ci][cj] is not None:
                heights.add(round(cells[ci][cj][k], 4))
        return sorted(heights)

    line_heights = {}

    def heights_at(i, j):
        if (i, j) not in line_heights:
            line_heights[(i, j)] = corner_heights(i, j)
        return line_heights[(i, j)]

    polygons = []  # (points, material, the direction it faces)

    for i in range(nx):
        for j in range(ny):
            c = cells[i][j]
            if c is None:
                continue
            h00, h10, h11, h01, material = c
            if max(h00, h10, h11, h01) > top - 0.05:
                raise ValueError("floor at %.2f %.2f reaches the ceiling" % (xs[i], ys[j]))
            p00 = (xs[i], ys[j], h00)
            p10 = (xs[i + 1], ys[j], h10)
            p11 = (xs[i + 1], ys[j + 1], h11)
            p01 = (xs[i], ys[j + 1], h01)
            polygons.append(([p00, p10, p11, p01], material, (0, 0, 1)))
            polygons.append(([(xs[i], ys[j], top), (xs[i], ys[j + 1], top), (xs[i + 1], ys[j + 1], top), (xs[i + 1], ys[j], top)], "+sky", (0, 0, -1)))

    def side(edge, ia, ja, ib, jb, facing):
        """the vertical face along a grid edge where open cell a's floor is
        below its neighbour b's (or b is solid): from a's floor up to b's,
        split at every height another face has on the edge's two ends, facing
        into a (facing: the direction into a)"""
        (x0, y0, l0, ka0, kb0), (x1, y1, l1, ka1, kb1) = edge
        ca = cells[ia][ja]
        cb = cells[ib][jb] if 0 <= ib < nx and 0 <= jb < ny else None
        a0, a1 = ca[ka0], ca[ka1]
        b0, b1 = (top, top) if cb is None else (cb[kb0], cb[kb1])
        if (a0 - b0) * (a1 - b1) < 0 and min(abs(a0 - b0), abs(a1 - b1)) > 1e-3:
            raise ValueError("floors cross at %.2f %.2f" % (x0, y0))
        if b0 <= a0 + 1e-4 and b1 <= a1 + 1e-4:
            return  # b's floor is no higher: b's own face, or none
        hs0 = sorted({round(h, 4) for h in heights_at(*l0) if a0 - 1e-4 <= h <= b0 + 1e-4} | {round(a0, 4), round(b0, 4)})
        hs1 = sorted({round(h, 4) for h in heights_at(*l1) if a1 - 1e-4 <= h <= b1 + 1e-4} | {round(a1, 4), round(b1, 4)})
        points = [(x0, y0, h) for h in hs0] + [(x1, y1, h) for h in hs1[::-1]]
        material = "wall" if cb is None or cb[4] in ("floor", "ground") else cb[4]
        polygons.append((points, material, facing))

    for i in range(nx):
        for j in range(ny):
            if cells[i][j] is None:
                continue
            # (corner indices: 0 (x0,y0), 1 (x1,y0), 2 (x1,y1), 3 (x0,y1))
            # -x side: the edge from (x0,y1) to (x0,y0), neighbour (i-1, j)
            side(((xs[i], ys[j + 1], (i, j + 1), 3, 2), (xs[i], ys[j], (i, j), 0, 1)), i, j, i - 1, j, (1, 0, 0))
            # +x side: (x1,y0) to (x1,y1), neighbour (i+1, j)
            side(((xs[i + 1], ys[j], (i + 1, j), 1, 0), (xs[i + 1], ys[j + 1], (i + 1, j + 1), 2, 3)), i, j, i + 1, j, (-1, 0, 0))
            # -y side: (x0,y0) to (x1,y0), neighbour (i, j-1)
            side(((xs[i], ys[j], (i, j), 0, 3), (xs[i + 1], ys[j], (i + 1, j), 1, 2)), i, j, i, j - 1, (0, 1, 0))
            # +y side: (x1,y1) to (x0,y1), neighbour (i, j+1)
            side(((xs[i + 1], ys[j + 1], (i + 1, j + 1), 2, 1), (xs[i], ys[j + 1], (i, j + 1), 3, 0)), i, j, i, j + 1, (0, -1, 0))

    # triangulate: a fan from a corner that is not on a straight run, skipping
    # degenerate triangles
    vertices = []
    index = {}
    triangles = []
    for points, material, facing in polygons:
        # (wound to face the open side)
        nrm = (0.0, 0.0, 0.0)
        for k in range(len(points)):
            p, q = points[k], points[(k + 1) % len(points)]
            nrm = (nrm[0] + (p[1] - q[1]) * (p[2] + q[2]), nrm[1] + (p[2] - q[2]) * (p[0] + q[0]), nrm[2] + (p[0] - q[0]) * (p[1] + q[1]))
        if nrm[0] * facing[0] + nrm[1] * facing[1] + nrm[2] * facing[2] < 0:
            points = points[::-1]
        clean = []
        for p in points:
            p = (round(p[0], 4), round(p[1], 4), round(p[2], 4))
            if not clean or clean[-1] != p:
                clean.append(p)
        if len(clean) > 1 and clean[0] == clean[-1]:
            clean.pop()
        if len(clean) < 3:
            continue
        ids = []
        for p in clean:
            if p not in index:
                index[p] = len(vertices)
                vertices.append(p)
            ids.append(index[p])
        def add_vertex(p):
            if p not in index:
                index[p] = len(vertices)
                vertices.append(p)
            return index[p]

        triangles.extend(_triangulate(ids, vertices, material, add_vertex))
    return vertices, triangles


def _area_normal(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    return (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)


def _triangulate(ids, vertices, material, add_vertex=None):
    """a convex polygon as triangles: a fan from its first corner when no
    three of its points are in a line; else from a point added in its middle
    (a fan from a corner along a straight run of points would make triangles
    of no area, and leave the run's edges unmatched)"""
    n = len(ids)
    straight = False
    for k in range(n):
        a, b, c = vertices[ids[k - 1]], vertices[ids[k]], vertices[ids[(k + 1) % n]]
        nrm = _area_normal(a, b, c)
        if sum(x * x for x in nrm) < 1e-10:
            straight = True
            break
    if not straight:
        return [(ids[0], ids[k], ids[k + 1], material) for k in range(1, n - 1)]
    middle = tuple(round(sum(vertices[v][axis] for v in ids) / n, 4) for axis in range(3))
    m = add_vertex(middle)
    return [(m, ids[k], ids[(k + 1) % n], material) for k in range(n)]


def check_closed(vertices, triangles):
    """every edge used by exactly two triangles, once each way: the problems"""
    edges = defaultdict(int)
    for a, b, c, _ in triangles:
        for u, v in ((a, b), (b, c), (c, a)):
            edges[(u, v)] += 1
    problems = []
    for (u, v), count in edges.items():
        if count != 1 or edges.get((v, u), 0) != 1:
            problems.append((vertices[u], vertices[v], count, edges.get((v, u), 0)))
    return problems


def write_jms(path, vertices, triangles):
    """the mesh as a JMS (version 8200) file, in hundredths of a world unit"""
    materials = []
    for _, _, _, m in triangles:
        if m not in materials:
            materials.append(m)
    lines = ["8200", "3251", "1", "frame", "-1", "-1", "0.0\t0.0\t0.0\t1.0", "0.0\t0.0\t0.0"]
    lines.append(str(len(materials)))
    for m in materials:
        lines += [m, "<none>"]
    lines += ["0", "1", "unnamed"]
    # each triangle its own three vertices, with its own normal
    out_vertices = []
    out_triangles = []
    for a, b, c, m in triangles:
        pa, pb, pc = vertices[a], vertices[b], vertices[c]
        n = _area_normal(pa, pb, pc)
        length = math.sqrt(sum(x * x for x in n)) or 1.0
        n = tuple(x / length for x in n)
        base = len(out_vertices)
        for p in (pa, pb, pc):
            # (texture coordinates by the plane the face is nearest)
            if abs(n[2]) > 0.7:
                u, v = p[0] / 4.0, p[1] / 4.0
            elif abs(n[0]) > abs(n[1]):
                u, v = p[1] / 4.0, p[2] / 4.0
            else:
                u, v = p[0] / 4.0, p[2] / 4.0
            out_vertices.append((p, n, u, v))
        out_triangles.append((materials.index(m), base, base + 1, base + 2))
    lines.append(str(len(out_vertices)))
    for p, n, u, v in out_vertices:
        lines += ["0", "%.4f\t%.4f\t%.4f" % (p[0] * 100, p[1] * 100, p[2] * 100), "%.6f\t%.6f\t%.6f" % n, "-1", "0.0", "%.4f" % u, "%.4f" % v, "0"]
    lines.append(str(len(out_triangles)))
    for m, a, b, c in out_triangles:
        lines += ["0", str(m), "%d\t%d\t%d" % (a, b, c)]
    with open(path, "w", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    return materials
