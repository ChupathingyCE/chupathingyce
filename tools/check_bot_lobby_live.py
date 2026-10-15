"""Drive the real Xbox or PC/XML Bot Match menu and verify local gameplay.

Needs Xbox maps and a --bots build. Runs in a hidden window with isolated data
and saves, never on the user's desktop. Each run keeps a fresh artifact folder.
"""
import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run(binary: Path, maps: Path, output: Path, menus: str = "xbox", mode: str = "slayer",
        friendly: int = 1, enemies: int = 2, unarmed: bool = False, accelerated: bool = False) -> Path:
    binary = binary.resolve(strict=True)
    maps = maps.resolve(strict=True)
    for name in ("ui", "bloodgulch", "beavercreek"):
        if not (maps / f"{name}.map").is_file():
            raise ValueError(f"Missing Xbox map: {maps / (name + '.map')}")
    output.mkdir(parents=True, exist_ok=True)
    artifacts = Path(tempfile.mkdtemp(prefix="bot-lobby-", dir=output.resolve()))
    data, saves = artifacts / "data", artifacts / "saves"
    data.mkdir()
    saves.mkdir()
    (data / "maps").symlink_to(maps, target_is_directory=True)
    # Fresh saves start on Campaign. Select Multiplayer, then its fifth row.
    # Real keyboard arrows choose Battle Creek, 1 friendly + 2 enemies, ODST,
    # and a custom Assault Rifle / Magnum loadout; controller Start launches.
    if friendly + enemies < 1 or friendly + enemies > 15:
        raise ValueError("Choose between 1 and 15 bots")
    buttons = "wait wait wait wait wait down wait a wait down wait down wait down wait down wait a wait "
    if menus == "pc":
        # Start on Multiplayer: six selectable rows precede BOT MATCH.
        buttons = "wait wait wait wait wait down wait down wait down wait down wait down wait down wait a wait "
    buttons += "key:Right wait key:Down wait "  # map: Battle Creek, then mode
    if mode == "ctf":
        buttons += "key:Right wait "
    buttons += "key:Down wait " + "key:Right wait " * friendly
    buttons += "key:Down wait " + ("key:Left wait " if enemies == 0 else "key:Right wait " * (enemies - 1))
    buttons += "key:Down wait key:Right wait key:Down wait key:Right wait "
    if unarmed:
        # Custom slots start at Assault Rifle (2) and Magnum (3).
        buttons += "key:Down wait key:Left wait key:Left wait key:Down wait key:Left wait key:Left wait key:Left wait "
    buttons += "start"
    env = {key: value for key, value in os.environ.items() if not key.startswith("HALO_")}
    env.update(
        HALO_DATA_ROOT=str(data), HALO_SAVE_ROOT=str(saves), HALO_MENUS=menus,
        HALO_NET_ONLINE="false", HALO_NET_PUBLIC_LOBBY="false", HALO_NET_HOST_PUBLIC="false",
        HALO_NET_REPORT_EVENTS="false", HALO_NET_REPORT_GAMES="false",
        HALO_NET_LIST_GAMES="false", HALO_NET_ALLOW_UPNP="false", HALO_UPDATE_AUTO="false",
        HALO_HIDDEN_WINDOW="1", HALO_EXIT_AFTER="70" if accelerated else "180" if mode == "ctf" else "55",
        HALO_GAME_SPEED="20" if accelerated else "1", HALO_WINDOW_SCALE="1",
        HALO_BOTS="0", HALO_TEST_INPUT=f"menu:{buttons}",
    )
    if mode == "ctf":
        env["HALO_BOT_CTF"] = "true"
    if menus == "pc":
        env["HALO_MENU_OPEN"] = "main_menu/multiplayer_type_select/multiplayer_type_select_screen"
    print(f"Artifacts: {artifacts}", flush=True)
    with (artifacts / "process.log").open("w") as log:
        subprocess.run([str(binary)], cwd=ROOT, env=env, stdout=log,
                       stderr=subprocess.STDOUT, check=True, timeout=95 if accelerated else 210 if mode == "ctf" else 80)
    log = (data / "debug.txt").read_text(errors="replace")
    assert "starting precaching of map 'beavercreek'" in log, "Start did not load Battle Creek"
    assert log.count("joined (odst)") == friendly + enemies, "Bot count or difficulty was not applied"
    teams = re.findall(r"bots: bot (\d+) assigned team (\d+) lane", log)
    expected = [(str(i + 1), "0" if i < friendly else "1") for i in range(friendly + enemies)]
    assert teams == expected, f"Wrong friendly/enemy teams: {teams}"
    assert "Assertion failed" not in log and "### ASSERTION" not in log, "Engine assertion"
    if mode == "ctf":
        assert re.search(r"bots: CTF flag [01] state 3 carrier", log), "No actual flag acquisition"
        assert re.search(r"bots: CTF team [01] captures [1-9]", log), "No engine-awarded capture"
    print(f"PASS: real {mode} setup loaded Battle Creek with {friendly} friendly + {enemies} enemy ODST bots")
    return artifacts


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/macos/halo")
    parser.add_argument("--maps", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path(tempfile.gettempdir()) / "bot-lobby-checks")
    parser.add_argument("--menus", choices=("xbox", "pc"), default="xbox")
    parser.add_argument("--mode", choices=("slayer", "ctf"), default="slayer")
    parser.add_argument("--friendly", type=int, choices=range(16), default=1)
    parser.add_argument("--enemies", type=int, choices=range(16), default=2)
    parser.add_argument("--unarmed", action="store_true", help="Isolate objectives from firearm combat")
    parser.add_argument("--accelerated", action="store_true", help="Run game simulation at 20x after menu setup")
    args = parser.parse_args()
    run(args.binary, args.maps, args.output, args.menus, args.mode, args.friendly, args.enemies,
        args.unarmed, args.accelerated)
