"""The bots' test levels: twenty small multiplayer maps, each made to test
one thing the bots do (tools/bot_levels/README.md). Each is a Grid
(geometry.py) and what is placed on it: where players start, the two flags,
teleporters, vehicles and weapons, and the tour the bots are timed round
(HALO_BOT_TOUR) for the test.

A player is about 0.7 world units tall, runs at 2.25 a second, jumps 0.76
up from a stand and about 2.9 across at a run (bot_aas.c's figures)."""

import math
import random

from geometry import Grid


class Level:
    def __init__(self, name, title, grid, about):
        self.name = name
        self.title = title
        self.grid = grid
        self.about = about
        self.spawns = []  # (x, y, z, yaw degrees, team)
        self.flags = {}  # team -> (x, y, z)
        self.teleporters = []  # ((x, y, z) from, (x, y, z) to, to yaw, channel)
        self.vehicles = []  # (kind, x, y, z, yaw)
        self.weapons = []  # (kind, x, y, z)
        self.tour = []  # (x, y, z): the places the test goes round
        self.game = "ctf"  # the game type its test plays
        self.drive = False  # a driving test (HALO_BOT_DRIVEBENCH) too

    def z(self, x, y, above=0.0):
        return round(self.grid.height_at(x, y) + above, 3)

    def spawn(self, x, y, yaw, team):
        self.spawns.append((x, y, self.z(x, y, 0.25), yaw, team))

    def spawn_ring(self, cx, cy, radius, count, team, facing=None):
        for k in range(count):
            a = 2 * math.pi * k / count
            x, y = cx + radius * math.cos(a), cy + radius * math.sin(a)
            yaw = facing if facing is not None else math.degrees(a)
            self.spawn(round(x, 2), round(y, 2), yaw, team)

    def flag(self, team, x, y):
        self.flags[team] = (x, y, self.z(x, y, 0.05))

    def teleporter(self, x0, y0, x1, y1, channel, yaw=0.0):
        self.teleporters.append(((x0, y0, self.z(x0, y0, 0.05)), (x1, y1, self.z(x1, y1, 0.05)), yaw, channel))

    def vehicle(self, kind, x, y, yaw):
        self.vehicles.append((kind, x, y, self.z(x, y, 0.4), yaw))

    def weapon(self, kind, x, y):
        self.weapons.append((kind, x, y, self.z(x, y, 0.1)))

    def stop(self, x, y):
        self.tour.append((x, y, self.z(x, y, 0.0)))


def saddle_free(heights, cell, floor=0.0):
    """whether a lattice of block heights ({(i, j): height}, None for a wall)
    has no saddle at the cell's corners: two blocks meeting only at a corner,
    higher than the other two there (the mesh's edge there would have four
    faces, and tool.exe will not take it)"""
    def h(c):
        v = heights.get(c, floor)
        return 1.0e9 if v is None else v
    i, j = cell
    for vi, vj in ((i, j), (i + 1, j), (i, j + 1), (i + 1, j + 1)):
        sw, se, ne, nw = h((vi - 1, vj - 1)), h((vi, vj - 1)), h((vi, vj)), h((vi - 1, vj))
        if min(sw, ne) > max(se, nw) or min(se, nw) > max(sw, ne):
            return False
    return True


def both_sides(level, x, yaw0, count=6, spread=6.0):
    """team 0 at -x, team 1 at +x, facing each other"""
    for k in range(count):
        y = -spread + 2 * spread * k / max(count - 1, 1)
        level.spawn(-x, round(y, 2), yaw0, 0)
        level.spawn(x, round(y, 2), (yaw0 + 180) % 360, 1)


LEVELS = []


def level(function):
    LEVELS.append(function)
    return function


# ---------- jumping

@level
def steps():
    """a staircase of blocks, each a jump higher than the last, to a flag on top"""
    g = Grid(-30, -20, 30, 20, ceiling=14, cell=4)
    L = Level("bot_steps", "Steps", g, "Jumping up again and again: blocks 0.4, 0.55 and 0.7 higher each, to a flag on top.")
    rises = [0.4, 0.55, 0.7, 0.4, 0.55, 0.7, 0.55, 0.7]
    h = 0.0
    for k, rise in enumerate(rises):
        h += rise
        x0 = -16 + k * 3.5
        g.box(x0, -4, x0 + 3.5, 4, round(h, 3))
    top = h
    g.box(12, -6, 20, 6, round(top, 3), material="block")
    # (the way back down: a ramp off the back)
    g.ramp(20, -4, 26, 4, top, 0.0, "x")
    L.flag(0, -24, 0)
    L.flag(1, 16, 0)
    for k in range(6):
        L.spawn(-24, -12 + k * 4.8, 0, 0)
        L.spawn(24, -12 + k * 4.8, 180, 1)
    L.stop(-24, 0)
    L.stop(16, 0)
    L.stop(24, 12)
    return L


@level
def jump_gaps():
    """platforms over a pit, the gaps between them wider each time"""
    g = Grid(-40, -16, 40, 16, ceiling=12, cell=4)
    L = Level("bot_jump_gaps", "Jump Gaps", g, "Running jumps over gaps of 1, 1.5, 2, 2.5 and 2.8 units; a fall is into a pit with a ramp back up.")
    g.floor(-3.0, "ground")  # the pit
    gaps = [1.0, 1.5, 2.0, 2.5, 2.8]
    x = -30.0
    g.box(-40, -6, x, 6, 0.0)
    for gap in gaps:
        x += gap
        g.box(x, -3, x + 4, 3, 0.0)
        x += 4
    g.box(x + 2.0, -6, 40, 6, 0.0)
    # (out of the pit at either end: up a ramp to a landing joined to the
    # start, the ramp's top at the landing; they were the wrong way round,
    # their tops out over the pit and their feet against the landings)
    g.ramp(-30, 8, -20, 12, 0.0, -3.0, "x")
    g.box(-34, 6, -30, 12, 0.0)
    g.ramp(20, 8, 30, 12, -3.0, 0.0, "x")
    g.box(30, 6, 36, 12, 0.0)
    L.flag(0, -35, 0)
    L.flag(1, 36, 0)
    for k in range(6):
        L.spawn(-37, -5 + k * 2, 0, 0)
        L.spawn(37, -5 + k * 2, 180, 1)
    L.stop(-34, 0)
    L.stop(36, 0)
    return L


@level
def climb():
    """a tower climbed by ledges round it, each a jump above the last"""
    g = Grid(-24, -24, 24, 24, ceiling=20, cell=4)
    L = Level("bot_climb", "Climb", g, "A tower in the middle, climbed by eleven ledges round it, each 0.6 above the last; a flag on the highest.")
    g.wall(-4, -4, 4, 4)  # (the tower's core)
    # eleven squares round it from the east side, each 0.6 above the last (a twelfth
    # would meet the first only at a corner of the tower: the mesh would not close)
    ring = [(4, -4), (4, 0), (4, 4), (0, 4), (-4, 4), (-8, 4), (-8, 0), (-8, -4), (-8, -8), (-4, -8), (0, -8)]
    h = 0.0
    for x, y in ring:
        h += 0.6
        g.box(x, y, x + 4, y + 4, round(h, 3))
    top = ring[-1]
    L.flag(0, -18, -18)
    L.flag(1, top[0] + 2, top[1] + 2)
    for k in range(6):
        L.spawn(-20, -18 + k * 3, 45, 0)
        L.spawn(20, 18 - k * 3, 225, 1)
    L.stop(-18, -18)
    L.stop(top[0] + 2, top[1] + 2)
    L.stop(18, 18)
    return L


@level
def jump_spiral():
    """five running jumps up round a pillar, each a gap and higher, turning as it goes"""
    g = Grid(-14, -14, 14, 14, ceiling=12, cell=4)
    L = Level("bot_jump_spiral", "Jump Spiral", g,
              "A ledge 0.7 up, then five running jumps round a pillar: gaps of about 1.5, each 0.45 higher, a turn before each; a flag on the last. A fall is to the floor, and the climb starts again.")
    g.wall(-2, -2, 2, 2)  # (the pillar: no jumping across the middle)
    h = 0.7
    platforms = []
    for k in range(6):
        angle = math.radians(-90 + 45 * k)
        cx, cy = round(5.5 * math.cos(angle), 2), round(5.5 * math.sin(angle), 2)
        g.box(cx - 1.2, cy - 1.2, cx + 1.2, cy + 1.2, round(h, 3))
        platforms.append((cx, cy))
        h += 0.45
    start = platforms[0]
    top = platforms[-1]
    L.flag(0, 0, -11)
    L.flag(1, top[0], top[1])
    for k in range(6):
        L.spawn(-10 + k * 4, -11.5, 90, 0)
        L.spawn(-10 + k * 4, 11.5, 270, 1)
    L.stop(0, -11)
    for x, y in platforms:
        L.stop(x, y)
    return L


@level
def jump_zigzag():
    """jumps up from side to side of a run, from one small block to the next"""
    g = Grid(-26, -10, 26, 10, ceiling=12, cell=4)
    L = Level("bot_jump_zigzag", "Jump Zigzag", g,
              "A ledge 0.7 up, then five jumps up from side to side, block to block (1.8 across, gaps of about 1.3), each 0.35 to 0.45 higher; a flag on a wider block at the top.")
    rises = [0.4, 0.45, 0.35, 0.45, 0.4]  # (no more than 0.45: a running jump lands 0.5 up at most, and at that it fails)
    h = 0.7
    blocks = []
    x = -12.0
    for k in range(6):
        # (1.4 to either side: from 1.6, the way between two blocks went
        # cornerwise, two units from corner to corner, and up 0.45 that is
        # past a running jump; at 1.4 it is 1.4)
        y = -1.4 if k % 2 == 0 else 1.4
        g.box(x - 0.9, y - 0.9, x + 0.9, y + 0.9, round(h, 3))
        blocks.append((x, y))
        if k < len(rises):
            h += rises[k]
            x += 2.6
    # (the top: a wider block past the last, a step down from it, with the flag)
    last_x, last_y = blocks[-1]
    g.box(last_x + 0.9, last_y - 0.9, last_x + 4.9, last_y + 0.9, round(h - 0.3, 3))
    L.flag(0, -21, 0)
    L.flag(1, last_x + 3.5, last_y)
    for k in range(6):
        L.spawn(-22, -6 + k * 2.4, 0, 0)
        L.spawn(22, -6 + k * 2.4, 180, 1)
    L.stop(-21, 0)
    for bx, by in blocks:
        L.stop(bx, by)
    L.stop(last_x + 3.5, last_y)
    return L


@level
def jump_mixed():
    """six jumps upward, each a different kind: a high ledge, a long gap, a gap up, a narrow beam, two gaps up with turns"""
    g = Grid(-24, -8, 4, 20, ceiling=12, cell=4)
    L = Level("bot_jump_mixed", "Jump Mix", g,
              "Six jumps up, each a different one: a ledge 0.74 high, a level gap of 2.3, a gap of 1.4 up 0.48 with a turn, a ledge 0.72 up onto a beam a unit wide, a gap of 1.7 up 0.45, and a gap of 1.5 up 0.45 with a turn; a flag on the last. All over a pit a unit deep, with a ramp out of it at each end.")
    # (a pit a unit down all round, so that each block is got to from the one
    # before it and not from the floor; the two ends where the sides start
    # are at 0, with a ramp up to each out of the pit)
    g.floor(-1.0, "ground")
    g.box(-23, -7, -17, 7, 0.0, material="floor")
    g.box(-2, -7, 3, 7, 0.0, material="floor")
    g.ramp(-17, -7, -13, -4, 0.0, -1.0, "x")
    g.ramp(-6, -7, -2, -4, -1.0, 0.0, "x")
    g.box(-17, -2, -13, 2, 0.74)          # 1: a ledge, from the start (against it)
    g.box(-10.7, -2, -8.3, 2, 0.74)       # 2: a level gap of 2.3
    g.box(-10.7, 3.4, -8.3, 5.8, 1.22)    # 3: north, a gap of 1.4 up 0.48 (a running jump lands 0.5 up at most)
    g.box(-10.0, 5.8, -9.0, 10.8, 1.94)   # 4: a beam a unit wide, 0.72 up from 3
    g.box(-11.2, 12.5, -7.8, 15.9, 2.39)  # 5: a gap of 1.7 off the beam's end, up 0.45
    g.box(-6.3, 12.5, -3.3, 15.9, 2.84)   # 6: east, a gap of 1.5 up 0.45
    L.flag(0, -20, 0)
    L.flag(1, -4.8, 14.2)
    for k in range(6):
        L.spawn(-21, -5 + k * 2, 0, 0)
        L.spawn(0, -5 + k * 2, 180, 1)
    for x, y in [(-20, 0), (-14.5, 0), (-9.5, 0), (-9.5, 4.6), (-9.5, 9.5), (-9.5, 14.2), (-4.8, 14.2)]:
        L.stop(x, y)
    return L


@level
def ledges():
    """blocks of every height across a field, to step onto, jump onto, or go round"""
    g = Grid(-30, -30, 30, 30, ceiling=10, cell=4)
    L = Level("bot_ledges", "Ledges", g, "Blocks 0.1 to 1.2 high scattered over a field: which are stepped up, which jumped, which gone round.")
    heights = [0.1, 0.2, 0.3, 0.45, 0.6, 0.75, 0.9, 1.2]
    rng = random.Random(7)
    placed = {}
    for k in range(60):
        i, j = rng.randint(-11, 10), rng.randint(-11, 10)
        if (abs(i * 2.4) > 20 and abs(j * 2.4) < 8) or (i, j) in placed:
            continue
        trial = dict(placed)
        trial[(i, j)] = heights[k % len(heights)]
        if not saddle_free(trial, (i, j)):
            continue
        placed = trial
        g.box(i * 2.4, j * 2.4, i * 2.4 + 2.4, j * 2.4 + 2.4, trial[(i, j)])
    L.flag(0, -26, 0)
    L.flag(1, 26, 0)
    for k in range(6):
        L.spawn(-27, -5 + k * 2, 0, 0)
        L.spawn(27, -5 + k * 2, 180, 1)
    L.stop(-26, 0)
    L.stop(0, 24)
    L.stop(26, 0)
    L.stop(0, -24)
    return L


# ---------- slopes and ramps

@level
def ramps():
    """ramps of 10 to 55 degrees up to one long platform"""
    g = Grid(-40, -30, 40, 30, ceiling=14, cell=4)
    L = Level("bot_ramps", "Ramps", g, "Ramps of 10, 20, 30, 40, 48 and 55 degrees up to a platform 3 high; the steepest are too steep to walk.")
    g.box(-30, 10, 30, 20, 3.0)
    angles = [10, 20, 30, 40, 48, 55]
    for k, angle in enumerate(angles):
        x0 = -28 + k * 10
        run = 3.0 / math.tan(math.radians(angle))
        g.ramp(x0, 10 - run, x0 + 6, 10, 0.0, 3.0, "y")
    L.flag(0, 0, -24)
    L.flag(1, 0, 15)
    for k in range(6):
        L.spawn(-30 + k * 12, -24, 90, 0)
        L.spawn(-30 + k * 12, 24, 270, 1)
    L.stop(0, -24)
    L.stop(-25, 15)
    L.stop(25, 15)
    return L


@level
def hills():
    """rolling ground, every slope smooth: walking and driving over it"""
    g = Grid(-60, -60, 60, 60, ceiling=30, cell=6)
    L = Level("bot_hills", "Hills", g, "Rolling hills up to 8 high, nothing flat: on foot and in a Warthog.")
    f = lambda x, y: round(4.0 + 3.0 * math.sin(x / 9.0) * math.cos(y / 11.0) + 1.5 * math.sin((x + y) / 17.0), 3)
    g.terrain(-59, -59, 59, 59, f, step=4)
    g.floor(f(-58, -58))
    L.flag(0, -45, -45)
    L.flag(1, 45, 45)
    for k in range(6):
        L.spawn(-48 + k * 2, -40, 45, 0)
        L.spawn(48 - k * 2, 40, 225, 1)
    L.vehicle("warthog", -40, -48, 45)
    L.vehicle("warthog", 40, 48, 225)
    L.vehicle("ghost", -48, -36, 45)
    L.vehicle("ghost", 48, 36, 225)
    L.stop(-45, -45)
    L.stop(45, -45)
    L.stop(45, 45)
    L.stop(-45, 45)
    L.drive = True
    return L


# ---------- teleporters

@level
def teleport():
    """rooms joined only by teleporters, one way each"""
    g = Grid(-40, -20, 40, 20, ceiling=10, cell=4)
    L = Level("bot_teleport", "Teleport", g, "Four walled rooms; the only ways between them are one-way teleporters, round in a ring.")
    g.wall(-40, -2, 40, 2)
    g.wall(-2, -20, 2, 20)
    # rooms: A (-x,-y), B (+x,-y), C (+x,+y), D (-x,+y); A->B->C->D->A
    L.teleporter(-10, -10, 10, -14, 0, 0)
    L.teleporter(30, -10, 30, 14, 1, 90)
    L.teleporter(10, 10, -10, 14, 2, 180)
    L.teleporter(-30, 10, -30, -14, 3, 270)
    L.flag(0, -30, -6)
    L.flag(1, 30, 6)
    for k in range(4):
        L.spawn(-20 + k * 3, -16, 0, 0)
        L.spawn(20 - k * 3, 16, 180, 1)
    L.stop(-30, -6)
    L.stop(30, -6)
    L.stop(30, 6)
    L.stop(-30, 6)
    return L


@level
def teleport_chain():
    """a line of rooms, each left by a teleporter that drops you in the next"""
    g = Grid(-50, -10, 50, 10, ceiling=10, cell=4)
    L = Level("bot_teleport_chain", "Teleport Chain", g, "Six rooms in a row with no doors: a teleporter in each to the next, and one from the last back to the first.")
    xs = [-50, -33, -16, 1, 18, 35, 50]
    for x in xs[1:-1]:
        g.wall(x - 1, -10, x + 1, 10)
    for k in range(6):
        cx = (xs[k] + xs[k + 1]) / 2.0
        nx = (xs[(k + 1) % 6] + xs[(k + 1) % 6 + 1]) / 2.0
        L.teleporter(cx + 4, 0, nx - 4, 0, k, 0)
    L.flag(0, -45, 6)
    L.flag(1, 45, -6)
    for k in range(4):
        L.spawn(-46, -6 + k * 4, 0, 0)
        L.spawn(46, 6 - k * 4, 180, 1)
    L.stop(-45, 6)
    L.stop(45, -6)
    return L


# ---------- obstacles

@level
def obstacles():
    """an open field of pillars and walls to go round"""
    g = Grid(-50, -40, 50, 40, ceiling=12, cell=4)
    L = Level("bot_obstacles", "Obstacles", g, "Pillars and wall pieces across an open field, nothing to climb: the way round them.")
    rng = random.Random(11)
    placed = {}
    for k in range(60):
        i, j = rng.randint(-20, 19), rng.randint(-17, 16)
        w = rng.choice([1, 1, 2, 4])
        d = 1 if w > 1 else rng.choice([1, 2])
        if rng.random() < 0.5:
            w, d = d, w
        if abs(i * 2.0) > 36 or abs((i + w) * 2.0) > 36:
            continue
        trial = dict(placed)
        cells = [(i + a, j + b) for a in range(w) for b in range(d)]
        for c in cells:
            trial[c] = None
        if not all(saddle_free(trial, c) for c in cells):
            continue
        placed = trial
        g.wall(i * 2.0, j * 2.0, (i + w) * 2.0, (j + d) * 2.0)
    L.flag(0, -45, 0)
    L.flag(1, 45, 0)
    for k in range(6):
        L.spawn(-46, -10 + k * 4, 0, 0)
        L.spawn(46, 10 - k * 4, 180, 1)
    L.vehicle("warthog", -44, -20, 0)
    L.vehicle("warthog", 44, 20, 180)
    L.stop(-45, 0)
    L.stop(0, 35)
    L.stop(45, 0)
    L.stop(0, -35)
    L.drive = True
    return L


@level
def forest():
    """a dense wood of thin trunks, and Ghosts to weave through it"""
    g = Grid(-60, -40, 60, 40, ceiling=16, cell=6)
    L = Level("bot_forest", "Forest", g, "Dense trunks a unit across over flat ground: weaving through on foot and in Ghosts and Warthogs.")
    # (flat: rolling ground cut up by every trunk's edges was more planes than
    # tool.exe's collision tree is allowed to be deep, and it stopped)
    rng = random.Random(5)
    taken = set()
    for k in range(70):
        x = rng.randint(-26, 25) * 2.0
        y = rng.randint(-17, 16) * 2.0
        if abs(x) > 46 or (x, y) in taken:
            continue
        # (no two trunks touching at a corner only: the mesh would not close)
        if any((x + dx, y + dy) in taken for dx in (-2, 0, 2) for dy in (-2, 0, 2)):
            continue
        taken.add((x, y))
        g.wall(x, y, x + 1.0, y + 1.0)
    L.flag(0, -52, 0)
    L.flag(1, 52, 0)
    for k in range(6):
        L.spawn(-54, -10 + k * 4, 0, 0)
        L.spawn(54, 10 - k * 4, 180, 1)
    L.vehicle("ghost", -50, -20, 0)
    L.vehicle("ghost", 50, 20, 180)
    L.vehicle("warthog", -50, 20, 0)
    L.vehicle("warthog", 50, -20, 180)
    L.stop(-52, 0)
    L.stop(52, 0)
    L.drive = True
    return L


# ---------- open ground, to drive

@level
def open_drive():
    """a long open valley, a base at each end, vehicles"""
    g = Grid(-120, -50, 120, 50, ceiling=40, cell=10)
    L = Level("bot_open_drive", "Open Drive", g, "A flat valley 240 long, a low base at each end and vehicles: driving for the flag.")
    for side in (-1, 1):
        x = side * 105
        g.box(x - 8, -8, x + 8, 8, 1.0, material="block")
        g.ramp(x - 8, 8, x + 8, 14, 1.0, 0.0, "y")
        g.ramp(x - 8, -14, x + 8, -8, 0.0, 1.0, "y")
        g.ramp(x - 14 if side > 0 else x + 8, -8, x - 8 if side > 0 else x + 14, 8, 0.0 if side > 0 else 1.0, 1.0 if side > 0 else 0.0, "x")
    L.flag(0, -105, 0)
    L.flag(1, 105, 0)
    for k in range(6):
        L.spawn(-110, -6 + k * 2.4, 0, 0)
        L.spawn(110, 6 - k * 2.4, 180, 1)
    for k, kind in enumerate(["warthog", "warthog", "ghost", "ghost", "scorpion"]):
        L.vehicle(kind, -90, -30 + k * 15, 0)
        L.vehicle(kind, 90, 30 - k * 15, 180)
    L.stop(-105, 0)
    L.stop(0, 40)
    L.stop(105, 0)
    L.stop(0, -40)
    L.drive = True
    return L


@level
def badlands():
    """wide hilly ground with rocks to drive round"""
    g = Grid(-120, -80, 120, 80, ceiling=50, cell=10)
    L = Level("bot_badlands", "Badlands", g, "Wide hilly ground with rock walls and mesas: driving the long way round and over.")
    f = lambda x, y: round(3.0 * math.sin(x / 23.0) * math.cos(y / 19.0) + 2.0 * math.cos(x / 41.0 + 1.0), 3)
    g.terrain(-119, -79, 119, 79, f, step=8)
    rng = random.Random(3)
    placed = {}
    for k in range(30):
        i, j = rng.randint(-9, 8), rng.randint(-6, 5)
        w, d = rng.choice([1, 1, 2]), rng.choice([1, 2])
        trial = dict(placed)
        cells = [(i + a, j + b) for a in range(w) for b in range(d)]
        for c in cells:
            trial[c] = None
        if len(placed) >= 18 or not all(saddle_free(trial, c) for c in cells):
            continue
        placed = trial
        g.wall(i * 8.0, j * 8.0, (i + w) * 8.0 - 2.0, (j + d) * 8.0 - 2.0)
    L.flag(0, -105, 0)
    L.flag(1, 105, 0)
    for k in range(6):
        L.spawn(-110, -8 + k * 3, 0, 0)
        L.spawn(110, 8 - k * 3, 180, 1)
    for k, kind in enumerate(["warthog", "warthog", "ghost", "ghost"]):
        L.vehicle(kind, -98, -24 + k * 16, 0)
        L.vehicle(kind, 98, 24 - k * 16, 180)
    L.stop(-105, 0)
    L.stop(0, 65)
    L.stop(105, 0)
    L.stop(0, -65)
    L.drive = True
    return L


# ---------- mazes

def maze_walls(g, x0, y0, columns, rows, cell, wall, seed, openings=()):
    """a maze of corridors `cell` wide (a perfect maze: one way between any
    two places), its walls `wall` thick; openings: (column, row, side) knocked
    through the outer wall"""
    rng = random.Random(seed)
    visited = [[False] * rows for _ in range(columns)]
    # every wall stands to begin with; carved away as the maze is made
    east = [[True] * rows for _ in range(columns)]
    north = [[True] * rows for _ in range(columns)]
    stack = [(0, 0)]
    visited[0][0] = True
    while stack:
        c, r = stack[-1]
        options = []
        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nc, nr = c + dc, r + dr
            if 0 <= nc < columns and 0 <= nr < rows and not visited[nc][nr]:
                options.append((dc, dr, nc, nr))
        if not options:
            stack.pop()
            continue
        dc, dr, nc, nr = rng.choice(options)
        if dc == 1:
            east[c][r] = False
        elif dc == -1:
            east[nc][nr] = False
        elif dr == 1:
            north[c][r] = False
        else:
            north[nc][nr] = False
        visited[nc][nr] = True
        stack.append((nc, nr))
    pitch = cell + wall
    # (walls as blocks on a lattice: every post, and the walls between posts
    # that stand; posts always, so no two blocks meet at a corner only)
    for c in range(columns + 1):
        for r in range(rows + 1):
            px, py = x0 + c * pitch, y0 + r * pitch
            g.wall(px, py, px + wall, py + wall)
    for c in range(columns):
        for r in range(rows):
            px, py = x0 + c * pitch, y0 + r * pitch
            if east[c][r] or c == columns - 1:
                if not (c == columns - 1 and (c, r, "east") in openings):
                    g.wall(px + pitch, py + wall, px + pitch + wall, py + pitch)
            if north[c][r] or r == rows - 1:
                if not (r == rows - 1 and (c, r, "north") in openings):
                    g.wall(px + wall, py + pitch, px + pitch, py + pitch + wall)
            if c == 0 and (c, r, "west") not in openings:
                g.wall(px, py + wall, px + wall, py + pitch)
            if r == 0 and (c, r, "south") not in openings:
                g.wall(px + wall, py, px + pitch, py + wall)
    return pitch


def maze_level(L, g, columns, rows, seed):
    """a maze filling the level from y edge to y edge, with a strip at each
    end for a team: in at the south-west, out at the north-east"""
    corridor, wall = 3.0, 0.5
    pitch = corridor + wall
    x0, y0, x1, y1 = g.bounds
    width, height = columns * pitch + wall, rows * pitch + wall
    mx = -width / 2.0
    my = y0 + Grid.RING
    assert abs(my + height - (y1 - Grid.RING)) < 1e-3, "the maze must fill the level's height"
    maze_walls(g, mx, my, columns, rows, corridor, wall, seed, openings={(0, 0, "west"), (columns - 1, rows - 1, "east")})
    west = (x0 + Grid.RING + mx) / 2.0
    east = (x1 - Grid.RING + mx + width) / 2.0
    L.flag(0, west, my + 1.75)
    L.flag(1, east, my + height - 1.75)
    for k in range(5):
        L.spawn(west, my + 6 + k * 3, 0, 0)
        L.spawn(east, my + height - 6 - k * 3, 180, 1)
    L.stop(west, my + 1.75)
    L.stop(east, my + height - 1.75)
    return pitch, mx, my


@level
def maze():
    columns, rows = 16, 16
    height = rows * 3.5 + 0.5 + 2 * Grid.RING
    g = Grid(-38, -height / 2, 38, height / 2, ceiling=8, cell=4)
    L = Level("bot_maze", "Maze", g, "A maze of 16 by 16 cells, corridors 3 wide, one way through, from a team's strip at one end to the other's.")
    maze_level(L, g, columns, rows, seed=21)
    return L


@level
def maze_big():
    columns, rows = 30, 30
    height = rows * 3.5 + 0.5 + 2 * Grid.RING
    g = Grid(-62, -height / 2, 62, height / 2, ceiling=8, cell=6)
    L = Level("bot_maze_big", "Big Maze", g, "A maze of 30 by 30 cells, corridors 3 wide: a long way through, one way only.")
    maze_level(L, g, columns, rows, seed=34)
    return L


@level
def maze_teleporters():
    """a maze with teleporters all through it: shortcuts, and some that lead astray"""
    columns, rows = 20, 20
    height = rows * 3.5 + 0.5 + 2 * Grid.RING
    g = Grid(-45, -height / 2, 45, height / 2, ceiling=8, cell=4)
    L = Level("bot_maze_teleporters", "Teleporter Maze", g, "A 20 by 20 maze with twelve one-way teleporters in its corridors: some a shortcut to the far end, some back to the start.")
    pitch, mx, my = maze_level(L, g, columns, rows, seed=8)
    rng = random.Random(9)
    def middle(c, r):
        return (round(mx + c * pitch + 0.5 + 1.5, 2), round(my + r * pitch + 0.5 + 1.5, 2))
    used = set()
    for k in range(12):
        while True:
            a = (rng.randrange(columns), rng.randrange(rows))
            b = (rng.randrange(columns), rng.randrange(rows))
            if a != b and a not in used and b not in used:
                break
        used.update((a, b))
        (ax, ay), (bx, by) = middle(*a), middle(*b)
        L.teleporter(ax, ay, bx, by, k, 0)
    return L


# ---------- round about

@level
def loop():
    """a canyon bent round a long ridge: the bases face each other across it
    and the way between goes the long way round (as Sidewinder)"""
    g = Grid(-100, -60, 100, 60, ceiling=40, cell=10)
    L = Level("bot_loop", "Loop", g, "A U-shaped canyon round a ridge that cannot be crossed: the bases are 40 apart across it and 250 apart round it.")
    g.wall(-100, -10, 70, 10)  # the ridge, open at the +x end
    f = lambda x, y: round(1.5 * math.sin(x / 15.0) + 0.5 * math.cos(y / 7.0), 3)
    g.terrain(-99, -59, 99, -10, f, step=8)
    g.terrain(-99, 10, 99, 59, f, step=8)
    g.terrain(70, -10, 99, 10, f, step=8)
    L.flag(0, -88, -35)
    L.flag(1, -88, 35)
    for k in range(6):
        L.spawn(-92, -50 + k * 4, 0, 0)
        L.spawn(-92, 50 - k * 4, 0, 1)
    for team, sy in ((0, -1), (1, 1)):
        L.vehicle("warthog", -75, sy * 30, 0)
        L.vehicle("ghost", -75, sy * 45, 0)
    # (weapons round the bend, as Sidewinder has: what is fought over there,
    # and so in the navigation, which goes only as far as the places that
    # are played in, and stopped 30 units past the bases without them)
    for kind, x, y in (("rocket", 85, 0), ("sniper", 10, -35), ("sniper", 10, 35), ("shotgun", 60, -30), ("shotgun", 60, 30)):
        L.weapon(kind, x, y)
    L.stop(-88, -35)
    L.stop(85, 0)
    L.stop(-88, 35)
    L.drive = True
    return L


@level
def loop_small():
    """a walled ring of corridor round a solid middle, and a ramp over it"""
    g = Grid(-40, -40, 40, 40, ceiling=12, cell=4)
    L = Level("bot_loop_small", "Ring", g, "A ring corridor 8 wide round a solid middle, with a ramp up and over the middle on one side: the way round against the way over.")
    g.wall(-24, -24, 24, 24)
    g.box(-40, -40, 40, -32, 0.0)
    # (over the middle: a ramp up from the south corridor to a bridge level with the middle's top would need an overhang; instead
    # a raised path round the north side, a short way at a jump)
    g.box(-24, 24, 24, 32, 1.5)
    g.ramp(-32, 24, -24, 32, 0.0, 1.5, "x")
    g.ramp(24, 24, 32, 32, 1.5, 0.0, "x")
    L.flag(0, -36, -36)
    L.flag(1, 36, 36)
    for k in range(5):
        L.spawn(-36, -36 + k * 3, 45, 0)
        L.spawn(36, 36 - k * 3, 225, 1)
    L.stop(-36, -36)
    L.stop(36, -36)
    L.stop(36, 36)
    L.stop(-36, 36)
    return L


# ---------- inside

@level
def corridors():
    """rooms joined by narrow corridors and doorways"""
    g = Grid(-40, -40, 40, 40, ceiling=6, cell=4)
    L = Level("bot_corridors", "Corridors", g, "Nine rooms joined by corridors a unit and a half wide and doorways one wide: tight turns indoors.")
    g.wall(-40, -40, 40, 40)
    rooms = [(-36 + c * 26, -36 + r * 26) for c in range(3) for r in range(3)]
    for x, y in rooms:
        g.box(x, y, x + 20, y + 20, 0.0)
    for c in range(3):
        for r in range(3):
            x, y = -36 + c * 26, -36 + r * 26
            if c < 2:  # a corridor east, its doorway narrower than it
                g.box(x + 20, y + 9.25, x + 26, y + 10.75, 0.0)
            if r < 2:
                g.box(x + 9.5, y + 20, x + 10.5, y + 26, 0.0)
    L.flag(0, -26, -26)
    L.flag(1, 26, 26)
    for k in range(5):
        L.spawn(-34 + k * 3, -34, 45, 0)
        L.spawn(34 - k * 3, 34, 225, 1)
    L.stop(-26, -26)
    L.stop(26, -26)
    L.stop(26, 26)
    L.stop(-26, 26)
    return L


@level
def arena():
    """a small symmetric arena: cover, ramps up to ledges, a middle platform"""
    g = Grid(-30, -30, 30, 30, ceiling=12, cell=4)
    L = Level("bot_arena", "Arena", g, "A small arena for fighting on the move: cover blocks, ledges round the walls reached by ramps, and a raised middle.")
    g.box(-26, 18, 26, 26, 2.0)
    g.box(-26, -26, 26, -18, 2.0)
    g.ramp(-14, 12, -6, 18, 0.0, 2.0, "y")
    g.ramp(6, -18, 14, -12, 2.0, 0.0, "y")
    g.box(-5, -5, 5, 5, 1.2)
    g.ramp(5, -2, 9, 2, 1.2, 0.0, "x")
    g.ramp(-9, -2, -5, 2, 0.0, 1.2, "x")
    for x, y in ((-16, -6), (14, 4), (-14, 6), (16, -6), (0, 12), (0, -12)):
        g.wall(x, y, x + 2, y + 2)
    g.box(-24, -4, -20, 4, 0.8)
    g.box(20, -4, 24, 4, 0.8)
    L.flag(0, -26, 0)
    L.flag(1, 26, 0)
    L.game = "slayer"
    for k in range(6):
        L.spawn(-27, -10 + k * 4, 0, 0)
        L.spawn(27, 10 - k * 4, 180, 1)
    for kind, x, y in (("rocket", 0, 0), ("sniper", -22, 22), ("sniper", 22, -22), ("shotgun", -12, 0), ("shotgun", 12, 0)):
        L.weapon(kind, x, y)
    L.stop(-26, 0)
    L.stop(0, 22)
    L.stop(26, 0)
    L.stop(0, -22)
    L.stop(0, 0)
    return L


@level
def course():
    """everything at once: ramps, steps, gaps, a corridor, a teleporter at the end"""
    g = Grid(-60, -20, 60, 20, ceiling=16, cell=4)
    L = Level("bot_course", "Course", g, "An obstacle course: a ramp, three steps up, two gaps, a narrow ledge, a drop, a short maze and a teleporter home.")
    g.floor(-2.0, "ground")
    g.box(-60, -6, -44, 6, 0.0)
    g.ramp(-44, -3, -36, 3, 0.0, 2.0, "x")
    g.box(-36, -3, -32, 3, 2.0)
    for k, h in enumerate((2.5, 3.1, 3.7)):
        g.box(-32 + k * 3, -3, -29 + k * 3, 3, h)
    g.box(-23, -3, -19, 3, 3.7)
    g.box(-17.5, -3, -13.5, 3, 3.7)  # (a 1.5 gap)
    g.box(-11.5, -3, -7.5, 3, 3.7)  # (a 2.0 gap)
    g.box(-7.5, -0.6, 4.5, 0.6, 3.7)  # (a narrow ledge)
    g.box(4.5, -3, 8.5, 3, 3.7)
    g.ramp(8.5, -3, 14.5, 3, 3.7, 0.0, "x")
    g.box(14.5, -10, 44, 10, 0.0)
    maze_walls(g, 16, -9.5, 6, 4, 3.0, 0.5, seed=4, openings={(0, 0, "west"), (5, 3, "east")})
    g.box(44, -6, 60, 6, 0.0)
    # (out of the ditch alongside, wherever it is fallen into)
    for x in (-40, -10, 30):
        g.ramp(x, 10, x + 8, 16, -2.0, 0.0, "x")
        g.box(x + 8, 10, x + 10, 16, 0.0)
    L.teleporter(54, 0, -52, 0, 0, 0)
    L.flag(0, -55, 0)
    L.flag(1, 50, 0)
    for k in range(5):
        L.spawn(-57, -5 + k * 2.5, 0, 0)
        L.spawn(57, 5 - k * 2.5, 180, 1)
    L.stop(-55, 0)
    L.stop(50, 0)
    return L


def all_levels():
    return [f() for f in LEVELS]
