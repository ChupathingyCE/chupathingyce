#!/usr/bin/env python3
"""The [tuning] knobs' table, made from the code so it can't drift.

Reads port/linux/src/port_config.c: the knobs (config_tuning_knobs), the
presets' values, each knob's setting (its builds and default) and the
original Xbox's defaults and ranges (the build's own macros, through the C
preprocessor), and applies config_tuning_apply's rule (a preset never
raises a build's own limit, and a value that is the default sets nothing).
What each knob costs is written here, in COSTS: a knob added without a line
there fails (tools/test_tuning_table.py).

    python3 tools/tuning_table.py                  the table for docs/settings.md
    python3 tools/tuning_table.py --xbox           the original Xbox's, for warthog-xbox's README
    python3 tools/tuning_table.py --check FILE     FILE's table is this one (--xbox: the Xbox's)
    python3 tools/tuning_table.py --write FILE     FILE's table made this one

The table sits in the file between BEGIN and END below.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT_CONFIG = os.path.join(ROOT, "port", "linux", "src", "port_config.c")
INCLUDE = os.path.join(ROOT, "port", "linux", "include")

BEGIN = "<!-- tuning table: made by tools/tuning_table.py, don't edit by hand -->"
END = "<!-- end of tuning table -->"
PRESETS = ("retail", "lan-party", "potato")

# What each knob costs or saves, and where: the simulation (the CPU's game
# tick: never changed by a knob), the drawing (the CPU's render and the
# GPU), or memory. One line each, in plain words.
COSTS = {
    "display.dynamic_light_limit": "Drawing. Each dynamic light redraws the level it reaches twice; in a big firefight most of the GPU's time.",
    "display.specular_light_limit": "Drawing. The shine pass of near lights, the costliest pass in a firefight.",
    "display.particle_limit": "Drawing, and a little CPU a frame for each particle alive. Never under 1024 in a network game.",
    "display.particle_ticks": "CPU a frame: moving loose particles once a tick saves two in three of their collision tests at 60 fps.",
    "display.shadow_resolution": "Drawing and video memory: bigger shadow maps.",
    "display.anisotropic_filtering": "Drawing: texture sampling on the GPU.",
    "display.anti_aliasing": "Drawing and video memory: extra samples or a filter pass.",
    "display.per_pixel_lighting": "Drawing: lighting work for each pixel instead of each vertex.",
    "display.light_effects": "Drawing: the projectile, muzzle and emitter lights, and their wall tests.",
    "display.resolution_scaling": "Drawing: \"original\" fills 640x480 instead of the window.",
    "tuning.present_mode": "Waiting, not work: \"adaptive\" shows a late frame at once, with a tear, instead of a refresh later.",
    "tuning.texture_cache": "Memory: a bigger cache loads fewer textures from the disc, and leaves less free.",
    "tuning.particle_thinning": "Drawing: fewer sprites of loose particles; each is still moved and counted as before. In a network game only past the Xbox's 1024.",
    "tuning.effects": "Drawing: \"reduced\" skips the shine, reflection and grass passes; \"minimal\" also decals, weather, contrails and lens flares. A network game keeps the grass, weather, contrails and flares.",
    "tuning.shadow_pixels": "Drawing: each object's shadow redraws the level under it; an object too small for one is not lit for it either.",
    "tuning.object_cull_pixels": "Drawing, and CPU a frame: small objects are neither lit nor drawn. In a network game only items lying around.",
}

PLATFORM_FLAGS = {
    "_platform_desktop": 1, "_platform_android": 2, "_platform_all": 3, "_platform_windows": 4,
    "_platform_xbox": 8, "_platform_console_gl": 16, "_platform_server": 32,
}


def read(path):
    with open(path, encoding="latin-1") as file:
        return file.read()


def strip_quotes(text):
    return text[1:-1] if len(text) >= 2 and text[0] == '"' and text[-1] == '"' else text


def knobs(source):
    """config_tuning_knobs' rows: dicts of name, short, flags, minimum, maximum, presets."""
    table = source[source.index("} config_tuning_knobs[] =") :]
    table = table[: table.index("};")]
    rows = []
    for match in re.finditer(r'\{\s*"([^"]+)",\s*"([^"]+)",\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*\{([^}]*)\}\s*\}', table):
        presets = [None if value.strip() == "NULL" else strip_quotes(value.strip()) for value in match.group(6).split(",")]
        if len(presets) != len(PRESETS):
            raise ValueError(f"{match.group(1)}: {len(presets)} preset values")
        rows.append({
            "name": match.group(1), "short": match.group(2), "flags": match.group(3).strip(),
            "minimum": match.group(4).strip(), "maximum": match.group(5).strip(), "presets": presets,
        })
    if not rows:
        raise ValueError("no knobs found in port_config.c")
    return rows


def setting(source, name):
    """A setting's row in config_settings: (type, default token, platforms)."""
    match = re.search(r'\{\s*"' + re.escape(name) + r'",\s*(_config_\w+),\s*((?:"(?:[^"\\]|\\.)*")|\w+),\s*'
                      r'(?:"[^"]*"|NULL),\s*\w+,\s*([^,]+?),\s*\n?\s*"', source)
    if not match:
        raise LookupError(f"no setting row for {name}")
    default = match.group(2)
    if default.startswith('"'):
        default = default[1:-1].replace('\\"', '"')
    return match.group(1), default, match.group(3).strip()


def platforms(expression):
    value = 0
    for name in re.findall(r"_platform_\w+", expression):
        value |= PLATFORM_FLAGS.get(name, 0)
    return value


def xbox_macros(source, names):
    """The original Xbox build's values of port_config.c's macros (its
    defaults and the knobs' ranges), through the preprocessor."""
    start = source.index("/* The particles, and the dynamic lights")
    defaults = source[start : source.index("#endif", source.index("DEFAULT_SPECULAR_LIGHT_LIMIT", start)) + len("#endif")]
    start = source.index("#ifdef HALO_PORT_MAXIMUM_RENDERED_LIGHTS")
    maxima = source[start : source.index("#endif", source.index("TUNING_MAXIMUM_PARTICLES", start) + 40) + len("#endif")]
    text = "\n".join([
        '#include "halo_console_features.h"',
        '#include "halo_port_capacity.h"',
        defaults,
        maxima,
    ] + [f'@ "{name}" {name}' for name in names])
    compiler = os.environ.get("CC", "cc")
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as file:
        file.write(text)
    try:
        result = subprocess.run([compiler, "-E", "-P", "-DHALO_CONSOLE", "-DHALO_XBOX_CONSOLE", "-DHALO_CONSOLE_PLAYERS=16",
                                 "-I", INCLUDE, file.name], capture_output=True, text=True)
    finally:
        os.unlink(file.name)
    if result.returncode:
        raise RuntimeError(f"the preprocessor failed:\n{result.stderr}")
    values = {}
    for line in result.stdout.splitlines():
        match = re.match(r'@ "(\w+)" (.*)$', line.strip())
        if match:
            values[match.group(1)] = strip_quotes(match.group(2).strip())
    return values


def rows():
    """Each knob: its name, builds, the Xbox's default and range, and what each preset sets there."""
    source = read(PORT_CONFIG)
    table = knobs(source)
    missing = [knob["name"] for knob in table if knob["name"] not in COSTS]
    if missing:
        raise KeyError(f"no line in tools/tuning_table.py's COSTS for: {', '.join(missing)}")
    settings = {knob["name"]: setting(source, knob["name"]) for knob in table}
    names = sorted({value for _, value, _ in settings.values() if re.fullmatch(r"[A-Z_]+", value)} |
                   {knob["maximum"] for knob in table if re.fullmatch(r"[A-Z_]+", knob["maximum"])})
    macros = xbox_macros(source, names)
    result = []
    for knob in table:
        kind, default, where = settings[knob["name"]]
        on_xbox = bool(platforms(where) & PLATFORM_FLAGS["_platform_xbox"])
        xbox_default = strip_quotes(macros.get(default, default))
        maximum = macros.get(knob["maximum"], knob["maximum"])
        if not on_xbox:
            xbox_range = ""
        elif "_tuning_megabytes" in knob["flags"]:
            xbox_range = f'"auto", or {knob["minimum"]} to {maximum} MB (and what memory has room for)'
        elif kind == "_config_integer" and maximum not in ("0", ""):
            xbox_range = f"{knob['minimum']} to {maximum} (0: no limit)" if "_tuning_limit" in knob["flags"] else \
                f"{knob['minimum']} to {maximum}"
        else:
            words = re.search(r'\{\s*"' + re.escape(knob["name"]) + r'",\s*\{([^}]*)\}', source)
            xbox_range = " or ".join(f"`{strip_quotes(word.strip())}`" for word in words.group(1).split(",")
                                     if word.strip() != "NULL") if words else "its words"
        on_xbox_presets = []
        for value in knob["presets"]:
            if value is None:
                on_xbox_presets.append(None)
            elif not on_xbox:
                on_xbox_presets.append("-")
            elif "_tuning_limit" in knob["flags"] and 0 < int(xbox_default or 0) <= int(value):
                on_xbox_presets.append(None)
            elif value == xbox_default:
                on_xbox_presets.append(None)
            else:
                on_xbox_presets.append(value)
        result.append({
            "name": knob["name"], "on_xbox": on_xbox, "xbox_default": xbox_default if on_xbox else "",
            "xbox_range": xbox_range, "presets": knob["presets"], "xbox_presets": on_xbox_presets,
            "cost": COSTS[knob["name"]],
        })
    return result


def preset_cell(value):
    return "nothing" if value is None else ("not a setting here" if value == "-" else f"`{value}`")


def table(xbox=False):
    lines = [BEGIN, ""]
    if xbox:
        lines += [
            "| Setting | On the original Xbox | Default and range here | lan-party sets | potato sets | What it costs or saves |",
            "| --- | --- | --- | --- | --- | --- |",
        ]
        for row in rows():
            if row["on_xbox"]:
                lines.append(f"| `{row['name']}` | yes | `{row['xbox_default']}`; {row['xbox_range']} | "
                             f"{preset_cell(row['xbox_presets'][1])} | {preset_cell(row['xbox_presets'][2])} | {row['cost']} |")
            else:
                lines.append(f"| `{row['name']}` | no: not a console setting | | | | {row['cost']} |")
    else:
        lines += [
            "| Setting | lan-party sets | potato sets | On the original Xbox | Xbox default and range | lan-party on the Xbox | potato on the Xbox | What it costs or saves |",
            "| --- | --- | --- | --- | --- | --- | --- | --- |",
        ]
        for row in rows():
            xbox = (f"yes | `{row['xbox_default']}`; {row['xbox_range']} | {preset_cell(row['xbox_presets'][1])} | "
                    f"{preset_cell(row['xbox_presets'][2])}") if row["on_xbox"] else "no | | |"
            lines.append(f"| `{row['name']}` | {preset_cell(row['presets'][1])} | {preset_cell(row['presets'][2])} | "
                         f"{xbox} | {row['cost']} |")
    lines += ["", END]
    return "\n".join(lines)


def in_file(path):
    text = read(path)
    if BEGIN not in text or END not in text:
        raise LookupError(f"{path} has no tuning table (the lines {BEGIN!r} and {END!r})")
    start = text.index(BEGIN)
    return text, start, text.index(END, start) + len(END)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--xbox", action="store_true", help="the original Xbox's table")
    parser.add_argument("--check", metavar="FILE")
    parser.add_argument("--write", metavar="FILE")
    arguments = parser.parse_args()
    made = table(arguments.xbox)
    if arguments.check:
        text, start, end = in_file(arguments.check)
        if text[start:end] != made:
            print(f"{arguments.check}: the tuning table is not the code's; run "
                  f"python3 tools/tuning_table.py{' --xbox' if arguments.xbox else ''} --write {arguments.check}",
                  file=sys.stderr)
            return 1
        print(f"{arguments.check}: the tuning table is the code's")
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
