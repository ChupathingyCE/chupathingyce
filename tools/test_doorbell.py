"""Tests of Delta's doorbell in internet play's signalling
(port/linux/src/p2p_signal.c; docs/delta.md, "The doorbell"), against a
local mosquitto set up as our brokers are: the anonymous listener carries
signalling (hceu/, for clients with or without a user name) and lets a
client read only doorbell/<its user name>; the site rings through its own
listener with a password. Skipped without mosquitto or a C compiler."""
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
ID = "0123456789abcdef0123456789abcdef"
OTHER = "fedcba9876543210fedcba9876543210"
# (the brokers' ACL on the signalling listener: the doorbell-setup.sh model)
SIGNALLING_ACL = ("# anonymous clients, and clients with a user name (a doorbell)\n"
                  "topic readwrite hceu/#\n"
                  "pattern readwrite hceu/#\n"
                  "pattern read doorbell/%u\n")
SITE_ACL = "user doorbell-site\ntopic write doorbell/+\n"


def compiler():
    """Finds a C compiler: clang, cc or gcc (the first found), or None."""
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def mosquitto_tool(name):
    """Finds one of mosquitto's programs (PATH, then Homebrew's). Its path, or None."""
    found = shutil.which(name)
    if found:
        return found
    for folder in ("/opt/homebrew/opt/mosquitto/sbin", "/opt/homebrew/opt/mosquitto/bin",
                   "/usr/local/opt/mosquitto/sbin", "/usr/local/opt/mosquitto/bin", "/usr/sbin"):
        path = Path(folder) / name
        if path.exists():
            return str(path)
    return None


def free_port():
    """A TCP port free on 127.0.0.1."""
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


@pytest.fixture(scope="module")
def binary(tmp_path_factory):
    """The test program (p2p_doorbell_test.c with p2p_signal.c), warnings as errors."""
    cc = compiler()
    if not cc or sys.platform == "win32":
        pytest.skip("no C compiler, or not POSIX")
    output = tmp_path_factory.mktemp("doorbell") / "p2p_doorbell_test"
    build = subprocess.run(
        [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-D__HALO_LINUX_PLATFORM_H", "-D__HALO_LINUX_POSIX_H",
         "-include", "port/linux/tests/p2p_fuzz_prelude.h", "-Iport/third_party/monocypher", "-Iport/linux/src",
         "port/linux/tests/p2p_doorbell_test.c", "port/linux/src/p2p_crypto.c", "port/linux/src/p2p_probe.c",
         "port/third_party/monocypher/monocypher.c", "port/third_party/monocypher/monocypher-ed25519.c",
         "-o", str(output)],
        capture_output=True, text=True, cwd=ROOT,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    return output


@pytest.fixture(scope="module")
def broker(tmp_path_factory):
    """A local mosquitto: the signalling listener (anonymous) and the site's
    (a password), and a second mosquitto standing for a broker that isn't
    ours. Returns (signalling port, site port, password, folder,
    the other's port)."""
    mosquitto, passwd = mosquitto_tool("mosquitto"), mosquitto_tool("mosquitto_passwd")
    if not mosquitto or not passwd or not mosquitto_tool("mosquitto_pub") or not mosquitto_tool("mosquitto_sub"):
        pytest.skip("mosquitto is not installed")
    folder = tmp_path_factory.mktemp("broker")
    password = "site-test-password"
    subprocess.run([passwd, "-c", "-b", str(folder / "passwords"), "doorbell-site", password], check=True,
                   capture_output=True)
    (folder / "signalling-acl").write_text(SIGNALLING_ACL)
    (folder / "site-acl").write_text(SITE_ACL)
    for name in ("passwords", "signalling-acl", "site-acl"):
        os.chmod(folder / name, 0o600)
    signalling, site, other = free_port(), free_port(), free_port()
    (folder / "mosquitto.conf").write_text(
        "per_listener_settings true\n"
        "persistence false\n"
        "log_dest stderr\nlog_type error\n"
        f"listener {signalling} 127.0.0.1\nallow_anonymous true\nacl_file {folder / 'signalling-acl'}\n"
        f"listener {site} 127.0.0.1\nallow_anonymous false\npassword_file {folder / 'passwords'}\n"
        f"acl_file {folder / 'site-acl'}\n")
    # (a broker of its own: signalling uses one client ID on every broker)
    (folder / "other.conf").write_text(
        f"persistence false\nlog_dest stderr\nlog_type error\nlistener {other} 127.0.0.1\nallow_anonymous true\n")
    process = subprocess.Popen([mosquitto, "-c", str(folder / "mosquitto.conf")], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    other_process = subprocess.Popen([mosquitto, "-c", str(folder / "other.conf")], stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
    for _ in range(50):
        try:
            socket.create_connection(("127.0.0.1", signalling), timeout=0.2).close()
            socket.create_connection(("127.0.0.1", site), timeout=0.2).close()
            socket.create_connection(("127.0.0.1", other), timeout=0.2).close()
            break
        except OSError:
            time.sleep(0.1)
    yield signalling, site, password, folder, other
    for running in (process, other_process):
        running.terminate()
        running.wait(timeout=5)


def publish(port, topic, payload, user=None, password=None):
    command = [mosquitto_tool("mosquitto_pub"), "-h", "127.0.0.1", "-p", str(port), "-V", "mqttv311",
               "-i", "pub-" + os.urandom(4).hex(), "-t", topic, "-m", payload]
    if user:
        command += ["-u", user] + (["-P", password] if password else [])
    subprocess.run(command, check=True, capture_output=True, timeout=10)


def test_the_game_hears_its_doorbell(binary, broker):
    """Signalling, started for the doorbell alone, connects to our broker
    only (with the doorbell as its user name), hears the site's ring, then
    connects to the others without it."""
    signalling, site, password, folder, other = broker
    (folder / "brokers.txt").write_text(f"broker.example.org:{other}\ndoorbell.chupathingy.org:{signalling}\n")
    process = subprocess.Popen([str(binary), str(folder / "brokers.txt"), ID, str(signalling), str(other)],
                               stdout=subprocess.PIPE, text=True, cwd=ROOT)
    line = process.stdout.readline().strip()
    assert line == "listening", line + process.stdout.read()
    # (rings from anyone else go nowhere; the site's arrives)
    publish(signalling, "doorbell/" + ID, "x")
    publish(signalling, "doorbell/" + ID, "x", user=ID)
    publish(site, "doorbell/" + ID, "\x02", "doorbell-site", password)
    output, _ = process.communicate(timeout=30)
    assert process.returncode == 0, line + "\n" + output
    assert output.split() == ["ring", "ok"], output


def test_the_acl_keeps_signalling_and_doorbells_apart(broker):
    """Signalling works for clients with and without a user name; nobody
    reads a doorbell that isn't their user name, and only the site rings."""
    signalling, site, password, _, _ = broker
    sub = mosquitto_tool("mosquitto_sub")

    def listen(topic, user=None, count=1):
        command = [sub, "-h", "127.0.0.1", "-p", str(signalling), "-V", "mqttv311", "-i", "sub-" + os.urandom(4).hex(),
                   "-t", topic, "-C", str(count), "-W", "3"]
        if user:
            command += ["-u", user]
        return subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)

    owner = listen("doorbell/" + ID, ID, count=2)
    snoopers = [listen("doorbell/" + ID, OTHER), listen("doorbell/+", OTHER), listen("doorbell/#"),
                listen("doorbell/#", "#"), listen("#", "+")]
    signalling_named, signalling_anonymous = listen("hceu/3/test", ID), listen("hceu/3/test")
    time.sleep(1)
    publish(signalling, "doorbell/" + ID, "fake", user=ID)
    publish(signalling, "doorbell/" + ID, "fake")
    publish(site, "doorbell/" + ID, "ring", "doorbell-site", password)
    publish(signalling, "hceu/3/test", "listing", user=OTHER)
    assert owner.communicate(timeout=10)[0].split() == ["ring"]
    for snooper in snoopers:
        assert snooper.communicate(timeout=10)[0] == ""
    assert signalling_named.communicate(timeout=10)[0].strip() == "listing"
    assert signalling_anonymous.communicate(timeout=10)[0].strip() == "listing"
