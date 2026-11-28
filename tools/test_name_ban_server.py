"""bans.txt's name rules on a dedicated server, run for real: the server
(HALO_DEDICATED, a playlist of one Slayer game) with a name rule in its
bans.txt, and copies of the game that join it by system link (port/linux/
game/network_test.c's "join", each player named by debug.network_test_name),
each with its own data root (maps linked, never copied), save root and HOME,
offline (no internet play, no game list), with no window.

  - a player whose name only looks like another ("nifty") joins and stays;
  - a player the rule names, written another way ("^1I f T y": a color code,
    spaces, other case), is banned: their machine dropped with the game's
    own ban refusal, and its line (hwid= and ip=) added to bans.txt under a
    comment naming the rule;
  - the same machine, back under another name, is refused at its join;
  - the server's log (debug.txt) never has a joining machine's address
    whole, only bans.txt does.

It needs built copies and maps: NAME_BAN_BINARY, the game; NAME_BAN_SERVER,
the dedicated server; NAME_BAN_DATA_ROOT_SCRIPT, a script that makes a data
root with links to the maps (its one argument the folder). Without them it
is skipped. The addresses are NAME_BAN_ADDRESSES (four, comma-separated:
the server's, then the three players'), loopback ones by default (Linux has
them, macOS needs them added). The log's check needs addresses outside every
private range, which the log would show whole; on Linux a network namespace
of its own gives them, with no way out to any real network:

    unshare -rn sh -c 'ip link set lo up && ip link add d0 type dummy &&
        for a in 2 3 4 5; do ip addr add 203.0.113.$a/24 dev d0; done &&
        ip link set d0 up &&
        NAME_BAN_ADDRESSES=203.0.113.2,203.0.113.3,203.0.113.4,203.0.113.5 \\
        NAME_BAN_BINARY=build/linux64/chupathingy NAME_BAN_SERVER=build/server-x64/chupathingyce-server \\
        NAME_BAN_DATA_ROOT_SCRIPT=new-data-root.sh python3 -m pytest -q tools/test_name_ban_server.py'
"""
import glob
import ipaddress
import os
import re
import shutil
import signal
import subprocess
import time

import pytest

from run_logs import read_debug_log

BINARY = os.environ.get("NAME_BAN_BINARY", "")
SERVER = os.environ.get("NAME_BAN_SERVER", "")
DATA_ROOT_SCRIPT = os.environ.get("NAME_BAN_DATA_ROOT_SCRIPT", "")
ADDRESSES = os.environ.get("NAME_BAN_ADDRESSES", "127.0.0.60,127.0.0.61,127.0.0.62,127.0.0.63").split(",")

pytestmark = pytest.mark.skipif(not BINARY or not SERVER or not DATA_ROOT_SCRIPT or len(ADDRESSES) != 4,
                                reason="needs NAME_BAN_BINARY, NAME_BAN_SERVER and NAME_BAN_DATA_ROOT_SCRIPT "
                                       "(built copies and maps)")

RULE = "ifty"
JOINED = "successfully joined a net game"
REFUSED_ON_SERVER = "banned (bans.txt)"
BANNED = "_rejection_code_blacklisted_machine"
# an IPv4 address with its port, as a log line has one
ADDRESS = re.compile(r"(?<![\w.])(\d{1,3}(?:\.\d{1,3}){3})(?::\d+)?(?![\w.])")

COMMON = {
    "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
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
    "SDL_VIDEO_DRIVER": "dummy",
}


def copy(folder, binary, env, setup=None):
    folder.mkdir(parents=True)
    subprocess.run([DATA_ROOT_SCRIPT, str(folder / "data")], check=True, capture_output=True)
    for name in ("home", "saves", "bin"):
        (folder / name).mkdir()
    # (a copy of the binary, and a portable build's own libraries beside it:
    # what it writes beside itself stays in the folder)
    for library in [binary, *glob.glob(os.path.join(os.path.dirname(os.path.abspath(binary)), "lib*.so*"))]:
        shutil.copy2(library, folder / "bin")
    binary = folder / "bin" / os.path.basename(binary)
    if setup:
        setup(folder / "data")
    environment = dict(COMMON, HOME=str(folder / "home"), HALO_DATA_ROOT=str(folder / "data"),
                       HALO_SAVE_ROOT=str(folder / "saves"), **env)
    if "LD_LIBRARY_PATH" in os.environ:
        environment["LD_LIBRARY_PATH"] = os.environ["LD_LIBRARY_PATH"]
    output = open(folder / "out.log", "wb")
    process = subprocess.Popen([str(binary)], cwd=folder / "data", env=environment, stdout=output,
                               stderr=subprocess.STDOUT, start_new_session=True)
    return process, output


def stop(process, output):
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
    output.close()


def log(folder):
    return read_debug_log(folder / "saves", folder / "data")


def player(folder, name, address, server_address):
    return copy(folder, BINARY, {"HALO_NETWORK_TEST": "join", "HALO_NETWORK_TEST_NAME": name,
                                 "HALO_NET_ADDRESS": address, "HALO_NET_BROADCAST": server_address})


def wait_for(path_text, needle, seconds):
    """waits until a log has the needle (path_text: a function giving the
    log's text): TRUE if it came in time"""
    deadline = time.time() + seconds
    while time.time() < deadline:
        if needle in path_text():
            return True
        time.sleep(1)
    return False


def public(address):
    """whether the log shows an address as a tag (port/linux/src/
    log_address.c's: not loopback, unspecified, a private range, link-local
    or 100.64/10); FALSE for what is no address"""
    try:
        parsed = ipaddress.IPv4Address(address)
    except ValueError:
        return False
    return not any(parsed in ipaddress.IPv4Network(network) for network in (
        "0.0.0.0/8", "10.0.0.0/8", "127.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "169.254.0.0/16",
        "100.64.0.0/10"))


def test_name_rule_bans_the_machine(tmp_path):
    server_address, other_address, banned_address, back_address = ADDRESSES

    def server_setup(data):
        (data / "name-ban-playlist.txt").write_text("bloodgulch slayer\n")
        (data / "bans.txt").write_text(f"# the test's rule\nname={RULE}\treason=test\n")

    running = []
    try:
        running.append(copy(tmp_path / "server", SERVER, {
            "HALO_DEDICATED": "name-ban-playlist.txt", "HALO_DEDICATED_MINIMUM_PLAYERS": "4",
            "HALO_DEDICATED_CONSOLE": "false", "HALO_NET_ADDRESS": server_address,
            "HALO_NET_BROADCAST": ",".join(ADDRESSES[1:])}, server_setup))
        time.sleep(8)
        # (a name that only looks like the rule's: it plays)
        running.append(player(tmp_path / "nifty", "nifty", other_address, server_address))
        assert wait_for(lambda: log(tmp_path / "nifty"), JOINED, 60), "nifty never joined"
        # (the rule's name, written another way: banned)
        running.append(player(tmp_path / "banned", "^1I f T y", banned_address, server_address))
        bans = tmp_path / "server" / "data" / "bans.txt"
        assert wait_for(lambda: bans.read_text(errors="replace"), "\thwid=", 60), "no machine line in bans.txt"
        time.sleep(3)
        # (the same machine under another name: refused at its join)
        running.append(player(tmp_path / "back", "Chupa", back_address, server_address))
        assert wait_for(lambda: log(tmp_path / "server"), REFUSED_ON_SERVER, 60), "the machine was let back in"
        time.sleep(3)
    finally:
        for process, output in running:
            stop(process, output)

    served = log(tmp_path / "server")
    lines = bans.read_text(errors="replace").splitlines()
    # (the rule kept, a comment naming it, then the machine's line)
    assert lines[1] == f"name={RULE}\treason=test"
    comment = next(index for index, line in enumerate(lines) if line.startswith("#") and f"name={RULE}" in line)
    machine = lines[comment + 1]
    assert f"\tip={banned_address}\t" in machine
    hardware_id = re.search(r"\thwid=([0-9a-f]+)\t", machine)
    assert hardware_id, machine
    assert "players=^1I f T y" in machine and f"name rule {RULE}" in machine
    assert "\tname=" not in machine and not machine.startswith("name=")
    # (one line for the banned machine, none for the others)
    assert sum("\thwid=" in line for line in lines) == 1
    assert f"ip={other_address}" not in "\n".join(lines)

    # nifty played on; the banned machine was dropped with the game's own ban
    # refusal; back, under another name, it was refused at its join
    assert JOINED in log(tmp_path / "nifty")
    assert BANNED not in log(tmp_path / "nifty")
    banned = log(tmp_path / "banned")
    assert JOINED in banned
    assert BANNED in banned or "dropped" in banned
    back = log(tmp_path / "back")
    assert JOINED not in back
    assert BANNED in back
    # (the server's own line: its log, or its terminal where that is all a
    # build's platform log goes to)
    terminal = (tmp_path / "server" / "out.log").read_text(errors="replace")
    assert "banned ^1I f T y (a bans.txt name rule)" in served + terminal

    # the server's log: no joining machine's address whole, where it is one
    # the log would hide (a public one); bans.txt alone has them
    for address in (other_address, banned_address, back_address):
        if public(address):
            assert address not in served
            assert address not in terminal
    if public(banned_address):
        found = {match.group(1) for match in ADDRESS.finditer(served)}
        assert not [address for address in found if public(address)], found
        assert "addr#" in served
