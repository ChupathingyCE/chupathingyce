#!/usr/bin/env python3
"""Writes port/linux/src/clan_emblem_table.h, the clan emblem's pieces the
game draws (port/linux/src/clan_emblem.c), from the site's: the answer of
GET /v1/clans/emblem (the game list's clans.py), saved as JSON.

    python3 tools/clan_emblem_table.py pieces.json > port/linux/src/clan_emblem_table.h

A piece is a list of parts, each a layer (1 primary, 2 secondary, 3
background color) and polygons on a 100 by 100 field, filled even-odd."""

import json
import sys


def main():
    pieces = json.load(open(sys.argv[1]))
    points, polygons, parts = [], [], []
    lines = ["/* CLAN_EMBLEM_TABLE.H: made by tools/clan_emblem_table.py from the site's", "emblem pieces (clans.py); not to be edited by hand */", ""]

    def add(kind):
        out = []
        for piece in pieces[kind]:
            first_part = len(parts)
            for part in piece["parts"]:
                first_polygon = len(polygons)
                for polygon in part["polygons"]:
                    polygons.append((len(points), len(polygon)))
                    points.extend(polygon)
                parts.append((part["layer"], first_polygon, len(part["polygons"])))
            out.append((piece["name"], first_part, len(piece["parts"])))
        return out

    backgrounds = add("backgrounds")
    foregrounds = add("foregrounds")
    lines.append(f"#define CLAN_EMBLEM_COLORS {len(pieces['colors'])}")
    lines.append(f"#define CLAN_EMBLEM_BACKGROUNDS {len(backgrounds)}")
    lines.append(f"#define CLAN_EMBLEM_FOREGROUNDS {len(foregrounds)}")
    lines.append("")
    lines.append("static unsigned int const clan_emblem_colors[CLAN_EMBLEM_COLORS] =\n{")
    lines.append("\t" + ", ".join("0x" + c["hex"].lstrip("#").upper() for c in pieces["colors"]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("static float const clan_emblem_points[][2] =\n{")
    for at in range(0, len(points), 6):
        lines.append("\t" + " ".join(f"{{ {float(x):.1f}f, {float(y):.1f}f }}," for x, y in points[at:at + 6]))
    lines.append("};")
    lines.append("")
    lines.append("static struct clan_emblem_polygon const clan_emblem_polygons[] =\n{")
    for at in range(0, len(polygons), 6):
        lines.append("\t" + " ".join(f"{{ {first}, {count} }}," for first, count in polygons[at:at + 6]))
    lines.append("};")
    lines.append("")
    lines.append("static struct clan_emblem_part const clan_emblem_parts[] =\n{")
    for at in range(0, len(parts), 4):
        lines.append("\t" + " ".join(f"{{ {layer}, {first}, {count} }}," for layer, first, count in parts[at:at + 4]))
    lines.append("};")
    for kind, table in (("backgrounds", backgrounds), ("foregrounds", foregrounds)):
        lines.append("")
        lines.append(f"static struct clan_emblem_piece const clan_emblem_{kind}[CLAN_EMBLEM_{kind.upper()}] =\n{{")
        for name, first, count in table:
            lines.append(f'\t{{ "{name}", {first}, {count} }},')
        lines.append("};")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
