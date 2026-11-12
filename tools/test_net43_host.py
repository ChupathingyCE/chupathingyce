"""A host announcing OpenCE's 43 by a table row, run for real: its own
machine joins its game at home (the start path every host takes), a client
of the same row joins it over system link and plays, and a client whose
table gives 43 no version word in the join request is refused, silently.

It runs two or three copies of a built game on this machine's loopback
(port/linux/game/network_test.c's "host:" and "join"), each with its own
data root (maps linked, never copied) and save root, and reads their logs.
It needs a built game and maps: set NET43_BINARY to the game and
NET43_DATA_ROOT_SCRIPT to a script that makes a data root with links to the
maps (its one argument the folder); without them it is skipped.

    NET43_BINARY=build/macos/chupathingy NET43_DATA_ROOT_SCRIPT=new-data-root.sh \\
        python3 -m pytest -q tools/test_net43_host.py
"""
import json
import os
import signal
import subprocess
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BINARY = os.environ.get("NET43_BINARY", "")
DATA_ROOT_SCRIPT = os.environ.get("NET43_DATA_ROOT_SCRIPT", "")

pytestmark = pytest.mark.skipif(not BINARY or not DATA_ROOT_SCRIPT,
                                reason="needs NET43_BINARY and NET43_DATA_ROOT_SCRIPT (a built game and maps)")


def table(path, word=True):
    """a local table whose wire's row announces 43 (the row file's facts);
    without word, 43's join request has no version word (an older build's)"""
    row = json.loads((ROOT / "tools/legacy_rows/chupa-43a-hosting-43.json").read_text())["chupa-43a"]
    row["announce"] = 43
    row.pop("hosting", None)
    if not word:
        fact = row["layouts"]["43"]
        fact.pop("join_request_version_at")
        fact["join_request_extra"] = 234
    path.write_text(json.dumps({"delta_legacy": 1, "serial": 1, "wires": {"chupa-43a": row},
                                "disabled_capabilities": []}))
    return path


def copy(folder, legacy, test):
    folder.mkdir(parents=True)
    subprocess.run([DATA_ROOT_SCRIPT, str(folder / "data")], check=True, capture_output=True)
    for name in ("home", "saves"):
        (folder / name).mkdir()
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": str(folder / "home"),
        "HALO_DATA_ROOT": str(folder / "data"),
        "HALO_SAVE_ROOT": str(folder / "saves"),
        "HALO_LEGACY_TABLE": str(legacy),
        "HALO_NETWORK_TEST": test,
        "HALO_NETWORK_TEST_START": "20",
        "HALO_UPDATE_AUTO": "false",
        "HALO_DISCORD_APPLICATION": "",
        "HALO_NET_ONLINE": "false",
        "HALO_NET_LIST_GAMES": "false",
        "HALO_NET_REPORT_GAMES": "false",
        "HALO_NET_BROWSER": "http://127.0.0.1:9",
        "HALO_NET_JOIN_FROM_CLIPBOARD": "false",
        "HALO_NET_ALLOW_UPNP": "false",
        "HALO_DEDICATED_PUBLIC": "false",
        "HALO_LAUNCHER_SHOW": "never",
        "HALO_LAUNCHER_MODE_CHOICE": "default",
        "SDL_AUDIO_DRIVER": "dummy",
    }
    output = open(folder / "out.log", "wb")
    process = subprocess.Popen([os.path.abspath(BINARY)], cwd=folder / "data", env=env, stdout=output,
                               stderr=subprocess.STDOUT, start_new_session=True)
    return process, output


def stop(process, output):
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
    output.close()


def log(folder):
    path = folder / "data" / "logs" / "debug.txt"
    return path.read_text(errors="replace") if path.exists() else ""


def test_host_at_43(tmp_path):
    host = tmp_path / "host"
    good = tmp_path / "client"
    bad = tmp_path / "client_without_word"
    running = [copy(host, table(tmp_path / "t43.json"), "host:bloodgulch")]
    try:
        time.sleep(8)
        running.append(copy(good, tmp_path / "t43.json", "join"))
        running.append(copy(bad, table(tmp_path / "t43_no_word.json", word=False), "join"))
        time.sleep(60)
    finally:
        for process, output in running:
            stop(process, output)
    hosted, joined, refused = log(host), log(good), log(bad)
    # (the host's own machine: joined at home with 43's word, never refused)
    assert "joining a host of network version 43: the join request carries 236 more bytes" in hosted
    assert "successfully joined a net game; our machine is #0" in hosted
    assert "can't add players to a game until after a game is joined" not in hosted
    # (the client of the row: joined, with the record of 43, and played)
    assert "joining a host of network version 43: the join request carries 236 more bytes" in joined
    assert "successfully joined a net game" in joined
    assert "different game layout" not in joined and "different game layout" not in hosted
    assert "checkpoint boundary" in hosted and "the host's checkpoint epoch" in joined
    # (the client without the word: dropped, without a reply)
    assert "joining a host of network version 43: the join request carries 234 more bytes" in refused
    assert "successfully joined a net game" not in refused
    assert hosted.count("ignoring a join request of another network version than this host's") >= 1
