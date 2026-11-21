#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
"""The look presets' table (display.look), made from the code so it can't drift.

Reads port/linux/src/port_config.c: the settings each look sets
(config_look_knobs), each setting's builds and default (the 64-bit
desktop's, through the C preprocessor where it is a macro), and applies
config_look_apply's rule (a look's value that is the default sets nothing).
What each setting shows is written here, in SHOWS: a setting added without
a line there fails (tools/test_look_table.py).

    python3 tools/look_table.py                  the table for docs/settings.md
    python3 tools/look_table.py --check FILE     FILE's table is this one
    python3 tools/look_table.py --write FILE     FILE's table made this one

The table sits in the file between BEGIN and END below.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tuning_table  # noqa: E402

ROOT = tuning_table.ROOT
PORT_CONFIG = tuning_table.PORT_CONFIG
INCLUDE = tuning_table.INCLUDE

BEGIN = "<!-- look table: made by tools/look_table.py, don't edit by hand -->"
END = "<!-- end of look table -->"
LOOKS = ("chupathingy", "xbox")

# What the Xbox look shows of each setting, in plain words.
SHOWS = {
    "display.shadow_resolution": "Objects' shadows drawn into the Xbox's 128-pixel maps, with their soft, stepped edges.",
    "display.light_effects": "No light from shots, muzzle flashes and glowing things: the Xbox's own lighting.",
    "display.doors_block_light": "A light in the next room shines through a closed door, as on the Xbox.",
    "display.scoreboard_background": "No panel behind the scoreboard's text, as on the Xbox.",
    "display.high_res_hud": "The HUD drawn from the maps' own pictures.",
    "display.high_res_text": "The menus' and the HUD's text in the maps' own bitmap fonts.",
    "display.hud_safe_zone": "The HUD 48 and 36 in from the screen's edges in split screen too.",
}


def knobs(source):
    """config_look_knobs' rows: dicts of name, short and looks."""
    table = source[source.index("} config_look_knobs[] =") :]
    table = table[: table.index("};")]
    rows = []
    for match in re.finditer(r'\{\s*"([^"]+)",\s*"([^"]+)",\s*\{([^}]*)\}\s*\}', table):
        looks = [None if value.strip() == "NULL" else tuning_table.strip_quotes(value.strip())
                 for value in match.group(3).split(",")]
        if len(looks) != len(LOOKS):
            raise ValueError(f"{match.group(1)}: {len(looks)} look values")
        rows.append({"name": match.group(1), "short": match.group(2), "looks": looks})
    if not rows:
        raise ValueError("no look settings found in port_config.c")
    return rows


def desktop_macros(source, names):
    """The 64-bit desktop build's values of port_config.c's default macros:
    each one's #if block, through the preprocessor."""
    lines = source.splitlines()
    blocks = []
    for name in names:
        at = next(index for index, line in enumerate(lines) if re.match(rf"#define {name}\b", line))
        start, depth = at, 0
        while start > 0:
            start -= 1
            if lines[start].startswith("#endif"):
                depth += 1
            elif lines[start].startswith("#if"):
                if not depth:
                    break
                depth -= 1
        end, depth = at, 0
        while end < len(lines) - 1:
            end += 1
            if lines[end].startswith("#if"):
                depth += 1
            elif lines[end].startswith("#endif"):
                if not depth:
                    break
                depth -= 1
        block = "\n".join(lines[start:end + 1])
        if block not in blocks:
            blocks.append(block)
    text = "\n".join(blocks + [f'@ "{name}" {name}' for name in names])
    compiler = os.environ.get("CC", "cc")
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as file:
        file.write(text)
    try:
        result = subprocess.run([compiler, "-E", "-P", "-U__APPLE__", "-DHALO_64BIT", "-I", INCLUDE, file.name],
                                capture_output=True, text=True)
    finally:
        os.unlink(file.name)
    if result.returncode:
        raise RuntimeError(f"the preprocessor failed:\n{result.stderr}")
    values = {}
    for line in result.stdout.splitlines():
        match = re.match(r'@ "(\w+)" (.*)$', line.strip())
        if match:
            values[match.group(1)] = tuning_table.strip_quotes(match.group(2).strip())
    return values


def builds(expression):
    flags = tuning_table.platforms(expression)
    names = []
    if flags & tuning_table.PLATFORM_FLAGS["_platform_desktop"]:
        names.append("desktop")
    if flags & tuning_table.PLATFORM_FLAGS["_platform_android"]:
        names.append("Android")
    if flags & tuning_table.PLATFORM_FLAGS["_platform_xbox"]:
        names.append("original Xbox")
    return ", ".join(names)


def rows():
    """Each setting: its name, builds, default and what each look sets."""
    source = tuning_table.read(PORT_CONFIG)
    table = knobs(source)
    missing = [knob["name"] for knob in table if knob["name"] not in SHOWS]
    if missing:
        raise KeyError(f"no line in tools/look_table.py's SHOWS for: {', '.join(missing)}")
    settings = {knob["name"]: tuning_table.setting(source, knob["name"]) for knob in table}
    macros = desktop_macros(source, sorted({value for _, value, _ in settings.values()
                                            if re.fullmatch(r"[A-Z_]+", value)}))
    result = []
    for knob in table:
        _, default, where = settings[knob["name"]]
        default = tuning_table.strip_quotes(macros.get(default, default).replace('\\"', '"'))
        result.append({
            "name": knob["name"], "builds": builds(where), "default": default,
            "looks": [None if value is None or value == default else value for value in knob["looks"]],
            "shows": SHOWS[knob["name"]],
        })
    return result


def look_cell(value):
    return "the default" if value is None else f"`{value}`"


def table():
    lines = [BEGIN, "",
             "| Setting | Builds | Default | Chupathingy sets | Xbox sets | What the Xbox look shows |",
             "| --- | --- | --- | --- | --- | --- |"]
    for row in rows():
        lines.append(f"| `{row['name']}` | {row['builds']} | `{row['default']}` | {look_cell(row['looks'][0])} | "
                     f"{look_cell(row['looks'][1])} | {row['shows']} |")
    lines += ["", END]
    return "\n".join(lines)


def in_file(path):
    text = tuning_table.read(path)
    if BEGIN not in text or END not in text:
        raise LookupError(f"{path} has no look table (the lines {BEGIN!r} and {END!r})")
    start = text.index(BEGIN)
    return text, start, text.index(END, start) + len(END)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", metavar="FILE")
    parser.add_argument("--write", metavar="FILE")
    arguments = parser.parse_args()
    made = table()
    if arguments.check:
        text, start, end = in_file(arguments.check)
        if text[start:end] != made:
            print(f"{arguments.check}: the look table is not the code's; run "
                  f"python3 tools/look_table.py --write {arguments.check}", file=sys.stderr)
            return 1
        print(f"{arguments.check}: the look table is the code's")
        return 0
    if arguments.write:
        text, start, end = in_file(arguments.write)
        with open(arguments.write, "w", encoding="latin-1") as file:
            file.write(text[:start] + made + text[end:])
        return 0
    print(made)
    return 0


if __name__ == "__main__":
    sys.exit(main())
