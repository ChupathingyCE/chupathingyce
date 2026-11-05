"""The Windows half of the bots' test levels: a batch file for the editing
kit's folder (the MCC Halo CE Editing Kit, with Invader's programs in an
invader folder in it) that makes each level into a Custom Edition map:

  1. its shaders: copies of Blood Gulch's, by the names the JMS asks for
  2. tool.exe structure: the JMS (data\\levels\\test\\<name>\\models) into a
     structure BSP, and the scenario
  3. invader-edit: the scenario made a multiplayer one, its sky, where
     players start, the flags, teleporters, vehicles and weapons
  4. tool.exe lightmaps: its lighting (quick, at low quality)
  5. invader-build: the map, gbx-custom (Custom Edition), into bot_levels\\maps

Everything a step prints goes to bot_levels\\logs\\<name>.txt."""

SHADERS = {
    "floor": r"levels\test\bloodgulch\shaders\blood ground.shader_environment",
    "ground": r"levels\test\bloodgulch\shaders\blood ground.shader_environment",
    "wall": r"levels\test\bloodgulch\shaders\cap_cliff01b.shader_environment",
    "ramp": r"levels\test\bloodgulch\shaders\cap_ramp.shader_environment",
    "block": r"levels\test\bloodgulch\shaders\cap_metal.shader_environment",
}
SKY = r"levels\test\bloodgulch\bloodgulch.sky"
VEHICLES = {
    "warthog": r"vehicles\warthog\mp_warthog.vehicle",
    "ghost": r"vehicles\ghost\ghost_mp.vehicle",
    "scorpion": r"vehicles\scorpion\scorpion_mp.vehicle",
}
WEAPONS = {
    "rocket": r"item collections\single weapons\rocket launcher.item_collection",
    "sniper": r"item collections\single weapons\sniper rifle.item_collection",
    "shotgun": r"item collections\single weapons\shotgun.item_collection",
    "assault": r"item collections\single weapons\assault rifle.item_collection",
}


def q(text):
    return '"%s"' % text


def edits(level):
    """invader-edit's actions for a level's scenario, a list of argument lists
    (each one call: a long line is split before Windows' limit)"""
    calls = []
    sets = [["-S", "type", "multiplayer"]]
    sets += [["-I", "skies", "1", "0"], ["-S", "skies[0].sky", SKY]]
    calls.append(sets)

    spawns = [["-I", "player_starting_locations", str(len(level["spawns"])), "0"]]
    for k, (x, y, z, yaw, team) in enumerate(level["spawns"]):
        key = "player_starting_locations[%d]" % k
        spawns += [["-S", key + ".position", "%.3f,%.3f,%.3f" % (x, y, z)], ["-S", key + ".facing", "%.2f" % yaw],
                   ["-S", key + ".team_index", str(team)], ["-S", key + ".type_0", "all_games"]]
    calls.append(spawns)

    flags = []
    for team, (x, y, z) in sorted(level["flags"].items()):
        flags.append(("ctf_flag", int(team), (x, y, z), 0.0))
    for (a, b, yaw, channel) in level["teleporters"]:
        flags.append(("teleport_from", channel, a, 0.0))
        flags.append(("teleport_to", channel, b, yaw))
    netgame = [["-I", "netgame_flags", str(len(flags)), "0"]]
    for k, (kind, usage, (x, y, z), yaw) in enumerate(flags):
        key = "netgame_flags[%d]" % k
        netgame += [["-S", key + ".position", "%.3f,%.3f,%.3f" % (x, y, z)], ["-S", key + ".facing", "%.2f" % yaw],
                    ["-S", key + ".type", kind], ["-S", key + ".usage_id", str(usage)]]
    calls.append(netgame)

    if level["vehicles"]:
        kinds = sorted({v[0] for v in level["vehicles"]})
        vehicles = [["-I", "vehicle_palette", str(len(kinds)), "0"]]
        for k, kind in enumerate(kinds):
            vehicles.append(["-S", "vehicle_palette[%d].name" % k, VEHICLES[kind]])
        vehicles.append(["-I", "vehicles", str(len(level["vehicles"])), "0"])
        for k, (kind, x, y, z, yaw) in enumerate(level["vehicles"]):
            key = "vehicles[%d]" % k
            vehicles += [["-S", key + ".type", str(kinds.index(kind))], ["-S", key + ".position", "%.3f,%.3f,%.3f" % (x, y, z)],
                         ["-S", key + ".rotation", "%.2f,0,0" % yaw]]
            for flag in ("slayer_default", "ctf_default", "king_default", "oddball_default",
                         "slayer_allowed", "ctf_allowed", "king_allowed", "oddball_allowed"):
                vehicles.append(["-S", key + ".multiplayer_spawn_flags." + flag, "1"])
        calls.append(vehicles)

    if level["weapons"]:
        weapons = [["-I", "netgame_equipment", str(len(level["weapons"])), "0"]]
        for k, (kind, x, y, z) in enumerate(level["weapons"]):
            key = "netgame_equipment[%d]" % k
            weapons += [["-S", key + ".type_0", "all_games"], ["-S", key + ".position", "%.3f,%.3f,%.3f" % (x, y, z)],
                        ["-S", key + ".item_collection", WEAPONS[kind]]]
        calls.append(weapons)

    # (split each call's actions so that no line is too long for cmd)
    out = []
    for actions in calls:
        line = []
        length = 0
        for action in actions:
            size = sum(len(a) + 3 for a in action)
            if line and length + size > 6000:
                out.append(line)
                line, length = [], 0
            line.append(action)
            length += size
        if line:
            out.append(line)
    return out


def batch(levels):
    lines = [
        "@echo off",
        "rem The bots' test levels: made into Custom Edition maps (tools/bot_levels in the",
        "rem ChupathingyCE repository wrote this). Run it from the editing kit's folder.",
        "rem   build_bot_levels.bat           every level",
        "rem   build_bot_levels.bat bot_maze  one of them",
        "setlocal",
        'cd /d "%~dp0"',
        'if not exist tool.exe (echo tool.exe is not here: put this in the editing kit\'s folder & goto :end)',
        'if not exist invader\\invader-build.exe (echo invader\\invader-build.exe is missing & goto :end)',
        'if not exist tags\\levels\\test\\bloodgulch (echo the tags are not unpacked: run "HCEEK (Extract).bat" first & goto :end)',
        "if not exist bot_levels\\logs mkdir bot_levels\\logs",
        "if not exist bot_levels\\maps mkdir bot_levels\\maps",
        'set ONLY=%~1',
        "set FAILED=",
        "rem (once: the kit's tags refer to models as model tags; Custom Edition's are gbxmodel)",
        'if not exist bot_levels\\refactored.txt (',
        '  echo Pointing the tags at gbxmodel tags, once...',
        '  invader\\invader-refactor.exe -t tags -M no-move -g model gbxmodel > bot_levels\\logs\\refactor.txt 2>&1',
        '  if not errorlevel 1 echo done> bot_levels\\refactored.txt',
        ')',
        "rem (once: the kit's tags have data Invader will not build with: an index out of",
        "rem  bounds in the Scorpion's lens flare, enums out of range; Invader's README fix)",
        'if not exist bot_levels\\bludgeoned.txt (',
        '  echo Fixing the tags Invader will not build with, once...',
        '  invader\\invader-bludgeon.exe -t tags -b "*" -T everything > bot_levels\\logs\\bludgeon.txt 2>&1',
        '  if not errorlevel 1 echo done> bot_levels\\bludgeoned.txt',
        ')',
    ]
    for level in levels:
        lines.append('if "%%ONLY%%"=="" call :%s' % level["name"])
        lines.append('if "%%ONLY%%"=="%s" call :%s' % (level["name"], level["name"]))
    lines += [
        "echo.",
        "if defined FAILED (echo Not made:%FAILED% - see bot_levels\\logs) else (echo Done: the maps are in bot_levels\\maps)",
        "echo done> bot_levels\\logs\\finished.txt",
        "goto :end",
        "",
    ]
    for level in levels:
        name = level["name"]
        folder = r"levels\test\%s" % name
        log = r"bot_levels\logs\%s.txt" % name
        scenario = r"%s\%s.scenario" % (folder, name)
        lines += [
            ":%s" % name,
            "echo %s..." % name,
            "echo %s> %s" % (name, log),
            'if not exist "tags\\%s\\shaders" mkdir "tags\\%s\\shaders"' % (folder, folder),
        ]
        for material in level["materials"]:
            if material.startswith("+"):
                continue
            lines.append('copy /y "tags\\%s" "tags\\%s\\shaders\\%s.shader_environment" >> %s 2>&1' % (SHADERS[material], folder, material, log))
        lines += [
            'del /q "tags\\%s\\%s.scenario_structure_bsp" "tags\\%s" 2>nul' % (folder, name, scenario),
            "echo ---- structure >> %s" % log,
            "tool.exe structure %s %s >> %s 2>&1" % (folder, name, log),
            'if not exist "tags\\%s\\%s.scenario_structure_bsp" (set FAILED=%%FAILED%% %s& echo   the structure did not compile & goto :eof)' % (folder, name, name),
            'if not exist "tags\\%s" (' % scenario,
            "  invader\\invader-edit.exe -t tags -N %s >> %s 2>&1" % (q(scenario), log),
            "  invader\\invader-edit.exe -t tags -I structure_bsps 1 0 -S structure_bsps[0].structure_bsp %s %s >> %s 2>&1" % (q(folder + "\\" + name + ".scenario_structure_bsp"), q(scenario), log),
            ")",
            "echo ---- scenario >> %s" % log,
        ]
        for call in edits(level):
            args = " ".join(" ".join(q(a) if (" " in a or "\\" in a or "," in a) else a for a in action) for action in call)
            lines.append("invader\\invader-edit.exe -t tags %s %s >> %s 2>&1" % (args, q(scenario), log))
        lines.append("invader\\invader-edit.exe -t tags -G type -G skies[0].sky -C player_starting_locations -C netgame_flags -C vehicles %s >> %s 2>&1" % (q(scenario), log))
        lines += [
            "echo ---- lightmaps >> %s" % log,
            "tool.exe lightmaps %s %s 0 0.8 >> %s 2>&1" % (scenario[:-len(".scenario")], name, log),
            "echo ---- build >> %s" % log,
            'invader\\invader-build.exe -t tags -m bot_levels\\maps -g gbx-custom -q %s >> %s 2>&1' % (q(scenario[:-len(".scenario")]), log),
            'if not exist "bot_levels\\maps\\%s.map" (set FAILED=%%FAILED%% %s& echo   the map was not built) else (echo   made bot_levels\\maps\\%s.map)' % (name, name, name),
            "goto :eof",
            "",
        ]
    lines += [":end", "endlocal", "pause"]
    return "\r\n".join(lines) + "\r\n"
