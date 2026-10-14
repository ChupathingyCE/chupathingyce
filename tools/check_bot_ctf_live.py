"""Require engine-awarded CTF captures in an isolated, accelerated hidden match."""
import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run(binary: Path, maps: Path, output: Path, map_name: str, seconds: int,
        both_captures: bool = False) -> Path:
    binary = binary.resolve(strict=True)
    maps = maps.resolve(strict=True)
    for name in ("ui", map_name):
        if not (maps / f"{name}.map").is_file():
            raise ValueError(f"Missing Xbox map: {name}")
    output.mkdir(parents=True, exist_ok=True)
    artifacts = Path(tempfile.mkdtemp(prefix=f"ctf-{map_name}-", dir=output.resolve()))
    data, saves = artifacts / "data", artifacts / "saves"
    data.mkdir()
    saves.mkdir()
    (data / "maps").symlink_to(maps, target_is_directory=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith("HALO_")}
    env.update(
        HALO_DATA_ROOT=str(data), HALO_SAVE_ROOT=str(saves),
        HALO_NET_ONLINE="false", HALO_NET_PUBLIC_LOBBY="false", HALO_NET_HOST_PUBLIC="false",
        HALO_NET_REPORT_EVENTS="false", HALO_NET_REPORT_GAMES="false", HALO_NET_LIST_GAMES="false",
        HALO_NET_ALLOW_UPNP="false", HALO_UPDATE_AUTO="false",
        HALO_HIDDEN_WINDOW="1", HALO_GAME_SPEED="20",
        HALO_MAX_FPS="-1", HALO_EXIT_AFTER=str(seconds), HALO_BOTS="0", HALO_BOT_CTF="true",
        HALO_BOT_HUMAN_CALLOUTS="false", HALO_MENUS="pc",
        HALO_MENU_OPEN="main_menu/multiplayer_type_select/multiplayer_type_select_screen",
        HALO_TEST_INPUT="menu:wait wait wait down wait down wait down wait down wait down wait down wait a wait " + ("key:Right wait " if map_name == "beavercreek" else "") +
        "key:Down wait key:Right wait key:Down wait key:Right wait key:Right wait "
        "key:Down wait key:Right wait key:Down wait key:Right wait start",
    )
    print(f"Artifacts: {artifacts}", flush=True)
    with (artifacts / "process.log").open("w") as log:
        subprocess.run([str(binary)], cwd=ROOT, env=env, stdout=log,
                       stderr=subprocess.STDOUT, check=True, timeout=seconds + 20)
    log = (data / "debug.txt").read_text(errors="replace")
    assert f"starting precaching of map '{map_name}'" in log, "Map did not load"
    assert "Assertion failed" not in log and "### ASSERTION" not in log, "Engine assertion"
    assert "no such setting debug.game_speed" not in log, "Acceleration setting unavailable"
    for team in (0, 1):
        assert re.search(rf"bots: CTF flag {team} state 3 carrier", log), f"Flag {team} was never acquired"
    assert re.search(r"bots: CTF team [01] captures [1-9]", log), "No engine-awarded capture"
    if both_captures:
        # A stricter release sample, not a guaranteed result of every combat round.
        for team in (0, 1):
            assert re.search(rf"bots: CTF team {team} captures [1-9]", log), f"No engine capture by team {team}"
    print("PASS: both flags acquired; engine-awarded captures verified" +
          (" for both teams" if both_captures else ""))
    return artifacts


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/macos/halo")
    parser.add_argument("--maps", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/bot-ctf-checks")
    parser.add_argument("--map", choices=("beavercreek", "bloodgulch"), default="beavercreek")
    parser.add_argument("--seconds", type=int, choices=range(50, 121), default=60)
    parser.add_argument("--both-captures", action="store_true",
                        help="Require a capture by each team; a one-sided round can legitimately fail")
    args = parser.parse_args()
    run(args.binary, args.maps, args.output, args.map, args.seconds, args.both_captures)
