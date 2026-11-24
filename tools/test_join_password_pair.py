"""A game with a password, run for real: a host (port/linux/game/
network_test.c's "host:", with debug.network_test_password) and a client of
system link's "join", each with its own data root (maps linked, never
copied), save root and HOME, offline (no internet play, no game list), with
no window. A client on the host's local network joins as ever; a client at
an internet address is refused with the password's reason; a game without a
password takes both.

It needs a built game and maps: JOIN_PASSWORD_BINARY, the game, and
JOIN_PASSWORD_DATA_ROOT_SCRIPT, a script that makes a data root with links
to the maps (its one argument the folder); without them it is skipped.
The local network's test runs on loopback addresses (127.0.0.50 and .51:
Linux has them, macOS needs them added). The internet's needs two addresses
outside every private range on this machine, JOIN_PASSWORD_INTERNET (two,
comma-separated); without them it is skipped. On Linux, a network namespace
of its own gives them, with no way out to any real network:

    unshare -rn sh -c 'ip link set lo up && ip link add d0 type dummy &&
        ip addr add 203.0.113.2/24 dev d0 && ip addr add 203.0.113.3/24 dev d0 &&
        ip link set d0 up &&
        JOIN_PASSWORD_INTERNET=203.0.113.2,203.0.113.3 \\
        JOIN_PASSWORD_BINARY=build/linux64/chupathingy JOIN_PASSWORD_DATA_ROOT_SCRIPT=new-data-root.sh \\
        python3 -m pytest -q tools/test_join_password_pair.py'
"""
import os
import signal
import subprocess
import time

import pytest

from run_logs import read_debug_log

BINARY = os.environ.get("JOIN_PASSWORD_BINARY", "")
DATA_ROOT_SCRIPT = os.environ.get("JOIN_PASSWORD_DATA_ROOT_SCRIPT", "")
LAN = os.environ.get("JOIN_PASSWORD_LAN", "127.0.0.50,127.0.0.51").split(",")
INTERNET = [address for address in os.environ.get("JOIN_PASSWORD_INTERNET", "").split(",") if address]

pytestmark = pytest.mark.skipif(not BINARY or not DATA_ROOT_SCRIPT,
                                reason="needs JOIN_PASSWORD_BINARY and JOIN_PASSWORD_DATA_ROOT_SCRIPT "
                                       "(a built game and maps)")
needs_internet = pytest.mark.skipif(len(INTERNET) != 2,
                                    reason="needs JOIN_PASSWORD_INTERNET (two addresses outside the private ranges)")

PASSWORD = "gulch-night"
REFUSED_ON_HOST = "the game has a password, and the machine did not come through its invite"
REFUSED_ON_CLIENT = "unable to join game: reason= #3/_rejection_code_bad_password"
JOINED = "successfully joined a net game"


def copy(folder, test, address, broadcast, password):
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
        "HALO_NETWORK_TEST_PASSWORD": password,
        "HALO_UPDATE_AUTO": "false",
        "HALO_DISCORD_APPLICATION": "",
        "HALO_NET_ONLINE": "false",
        "HALO_NET_LIST_GAMES": "false",
        "HALO_NET_REPORT_GAMES": "false",
        "HALO_NET_REPORT_HOSTED": "false",
        "HALO_NET_BROWSER": "http://127.0.0.1:9",
        "HALO_NET_JOIN_FROM_CLIPBOARD": "false",
        "HALO_NET_ALLOW_UPNP": "false",
        "HALO_DEDICATED_PUBLIC": "false",
        "HALO_LAUNCHER_SHOW": "never",
        "HALO_LAUNCHER_MODE_CHOICE": "default",
        "SDL_AUDIO_DRIVER": "dummy",
        "SDL_VIDEO_DRIVER": os.environ.get("JOIN_PASSWORD_VIDEO_DRIVER", "dummy"),
        "HALO_NET_ADDRESS": address,
        "HALO_NET_BROADCAST": broadcast,
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
    return read_debug_log(folder / "saves", folder / "data")


def play(tmp_path, addresses, password):
    """the host at the first address, the client at the second: their logs"""
    host_address, client_address = addresses
    running = [copy(tmp_path / "host", "host:bloodgulch", host_address, client_address, password)]
    try:
        time.sleep(8)
        running.append(copy(tmp_path / "client", "join", client_address, host_address, ""))
        time.sleep(40)
    finally:
        for process, output in running:
            stop(process, output)
    return log(tmp_path / "host"), log(tmp_path / "client")


def test_local_network_joins_a_game_with_a_password(tmp_path):
    hosted, joined = play(tmp_path, LAN, PASSWORD)
    assert "network test: the game has a password" in hosted
    assert JOINED in joined
    assert REFUSED_ON_HOST not in hosted


@needs_internet
def test_internet_join_to_a_game_with_a_password_is_refused(tmp_path):
    hosted, joined = play(tmp_path, INTERNET, PASSWORD)
    assert "network test: the game has a password" in hosted
    # (the host's own machine joined at home)
    assert "successfully joined a net game; our machine is #0" in hosted
    assert REFUSED_ON_HOST in hosted
    assert REFUSED_ON_CLIENT in joined
    assert JOINED not in joined


@needs_internet
def test_internet_join_to_a_game_without_a_password(tmp_path):
    hosted, joined = play(tmp_path, INTERNET, "")
    assert "network test: the game has a password" not in hosted
    assert JOINED in joined
    assert REFUSED_ON_HOST not in hosted
