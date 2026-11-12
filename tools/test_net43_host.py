"""Hosts at OpenCE's 43 and 44, run for real: the host's own machine joins
its game at home (the start path every host takes), a client of this build
with no table (43 built in) joins and plays, and a client whose table makes
the join request without the version word is dropped, silently.

It runs copies of a built game on this machine's loopback
(port/linux/game/network_test.c's "host:" and "join"), each with its own
data root (maps linked, never copied) and save root, and reads their logs.
Each copy has an address of its own (127.0.0.40 to .42: Linux has them,
macOS needs them added), the host's search answers sent to its clients' (no
broadcast), and no window (SDL's dummy video driver; NET43_VIDEO_DRIVER
another). The host announces its number by a local table. It needs a built game and
maps: set NET43_BINARY to the game and NET43_DATA_ROOT_SCRIPT to a script
that makes a data root with links to the maps (its one argument the
folder); without them it is skipped.

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


def table(path, announce, without_word=None):
    """a local table whose wire's row announces a number (43 is built in;
    44 has 43's layout); without_word: a version whose join request the
    table makes 234 bytes, without the word (an older build's)"""
    row = {"announce": announce, "minimum": 11, "maximum": 45, "layouts": {}}
    if without_word:
        row["layouts"][str(without_word)] = {"join_request_extra": 234, "join_request_fill": "zeros"}
    path.write_text(json.dumps({"delta_legacy": 1, "serial": 1, "wires": {"chupa-43a": row},
                                "disabled_capabilities": []}))
    return path


# (each copy at an address of its own, the host's search answers sent to its
# clients' addresses: no broadcast; Linux has all of 127/8, macOS needs
# them added, as tools/crossplay_test.py says)
HOST_ADDRESS = os.environ.get("NET43_HOST_ADDRESS", "127.0.0.40")
CLIENT_ADDRESSES = os.environ.get("NET43_CLIENT_ADDRESSES", "127.0.0.41,127.0.0.42").split(",")


def copy(folder, legacy, test, address, broadcast):
    folder.mkdir(parents=True)
    subprocess.run([DATA_ROOT_SCRIPT, str(folder / "data")], check=True, capture_output=True)
    for name in ("home", "saves"):
        (folder / name).mkdir()
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": str(folder / "home"),
        "HALO_DATA_ROOT": str(folder / "data"),
        "HALO_SAVE_ROOT": str(folder / "saves"),
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
        "SDL_VIDEO_DRIVER": os.environ.get("NET43_VIDEO_DRIVER", "dummy"),
        "HALO_NET_ADDRESS": address,
        "HALO_NET_BROADCAST": broadcast,
    }
    if legacy:
        env["HALO_LEGACY_TABLE"] = str(legacy)
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


def play(tmp_path, announce, clients):
    host = tmp_path / "host"
    addresses = CLIENT_ADDRESSES[:len(clients)]
    running = [copy(host, table(tmp_path / "host.json", announce), "host:bloodgulch", HOST_ADDRESS,
                    ",".join(addresses))]
    try:
        time.sleep(8)
        for (name, legacy), address in zip(clients, addresses):
            running.append(copy(tmp_path / name, legacy, "join", address, HOST_ADDRESS))
        time.sleep(60)
    finally:
        for process, output in running:
            stop(process, output)
    return log(host), {name: log(tmp_path / name) for name, _ in clients}


def test_host_at_43(tmp_path):
    """a host at 43, its own machine's join at home, and a client of this
    build with no table at all (43 built in) joining and playing"""
    hosted, clients = play(tmp_path, 43, [("client", None)])
    joined = clients["client"]
    # (the host's own machine: joined at home with 43's word, never refused)
    assert "joining a host of network version 43: the join request carries 236 more bytes" in hosted
    assert "successfully joined a net game; our machine is #0" in hosted
    assert "can't add players to a game until after a game is joined" not in hosted
    assert "ignoring a join request" not in hosted
    # (the client: 43 built in, joined with the word and 43's record, played)
    assert "joining a host of network version 43: the join request carries 236 more bytes" in joined
    assert "successfully joined a net game" in joined
    assert "different game layout" not in joined and "different game layout" not in hosted
    assert "checkpoint boundary" in hosted and "the host's checkpoint epoch" in joined


def test_host_refuses_a_join_without_the_word(tmp_path):
    """a host at 44 (43's layout, the word needed): a client of this build
    joins; one whose table makes 44's request without the word is dropped"""
    hosted, clients = play(tmp_path, 44, [("client", None),
                                          ("without_word", table(tmp_path / "old.json", 32, without_word=44))])
    assert "successfully joined a net game; our machine is #0" in hosted
    assert "successfully joined a net game" in clients["client"]
    assert "joining a host of network version 44: the join request carries 234 more bytes" in clients["without_word"]
    assert "successfully joined a net game" not in clients["without_word"]
    assert "ignoring a join request of another network version than this host's" in hosted
