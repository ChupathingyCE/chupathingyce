"""Runs the C tests of Delta's doorbell (port/linux/src/doorbell.c: its
packets, the subscription, pings, failover and backoff) with the build
machine's C compiler, and, when mosquitto is installed, listens on a local
mosquitto set up as the doorbell's brokers are (docs/delta.md, "The
doorbell": anonymous clients read only the topic of their own client ID; the
site alone writes), and checks that a ring from the site arrives and that
nobody else's does."""
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


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def mosquitto_tool(name):
    """Finds one of mosquitto's programs (PATH, then Homebrew's sbin and bin).

    Returns its path, or None.
    """
    found = shutil.which(name)
    if found:
        return found
    for folder in ("/opt/homebrew/opt/mosquitto/sbin", "/opt/homebrew/opt/mosquitto/bin",
                   "/usr/local/opt/mosquitto/sbin", "/usr/local/opt/mosquitto/bin", "/usr/sbin"):
        path = Path(folder) / name
        if path.exists():
            return str(path)
    return None


@pytest.fixture(scope="module")
def binary(tmp_path_factory):
    """Builds the doorbell unit with its tests, warnings as errors.

    Returns the test program's path.
    """
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    if sys.platform == "win32":
        pytest.skip("POSIX only")
    output = tmp_path_factory.mktemp("doorbell") / "doorbell_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "doorbell_test.c"),
         str(ROOT / "port" / "linux" / "src" / "doorbell.c"),
         "-o", str(output)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    return output


def test_doorbell(binary):
    """The unit's own tests pass."""
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def free_port():
    """Returns a TCP port free on 127.0.0.1."""
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


@pytest.fixture(scope="module")
def broker(tmp_path_factory):
    """A local mosquitto with the doorbell's settings (a plain listener: the
    real ones are TLS, which the game's transport adds).

    Returns (host:port, the site's password).
    """
    mosquitto = mosquitto_tool("mosquitto")
    passwd = mosquitto_tool("mosquitto_passwd")
    if not mosquitto or not passwd or not mosquitto_tool("mosquitto_pub"):
        pytest.skip("mosquitto is not installed")
    folder = tmp_path_factory.mktemp("broker")
    password = "site-test-password"
    subprocess.run([passwd, "-c", "-b", str(folder / "passwords"), "doorbell-site", password], check=True,
                   capture_output=True)
    os.chmod(folder / "passwords", 0o600)
    (folder / "acl").write_text(
        "# the site: rings to any doorbell, nothing else\n"
        "user doorbell-site\n"
        "topic write doorbell/+\n"
        "\n"
        "# every other client: its own doorbell, read only (its client ID)\n"
        "pattern read doorbell/%c\n")
    os.chmod(folder / "acl", 0o600)
    port = free_port()
    (folder / "mosquitto.conf").write_text(
        "per_listener_settings true\n"
        f"listener {port} 127.0.0.1\n"
        "allow_anonymous true\n"
        f"password_file {folder / 'passwords'}\n"
        f"acl_file {folder / 'acl'}\n"
        "max_packet_size 512\n"
        "max_keepalive 300\n"
        "persistence false\n"
        "retain_available false\n"
        "max_queued_messages 10\n"
        "log_dest stderr\n"
        "log_type error\n")
    process = subprocess.Popen([mosquitto, "-c", str(folder / "mosquitto.conf")], stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE)
    for _ in range(50):
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
            break
        except OSError:
            time.sleep(0.1)
    else:
        process.kill()
        pytest.fail("mosquitto did not start: " + process.stderr.read().decode(errors="replace"))
    yield f"127.0.0.1:{port}", password
    process.terminate()
    process.wait(timeout=5)


def publish(address, topic, user=None, password=None, payload="\x02"):
    """One ring published to topic as user (none: anonymous)."""
    host, port = address.split(":")
    command = [mosquitto_tool("mosquitto_pub"), "-h", host, "-p", port, "-V", "mqttv311", "-t", topic, "-m", payload]
    if user:
        command += ["-u", user, "-P", password]
    subprocess.run(command, check=True, capture_output=True, timeout=10)


def listener(binary, address, doorbell):
    """The test program listening for doorbell on address, once it says it
    listens. Returns the process."""
    process = subprocess.Popen([str(binary), "--broker", address, "--id", doorbell], stdout=subprocess.PIPE, text=True)
    line = process.stdout.readline().strip()
    assert line == "listening", line
    return process


def test_ring_from_the_site(binary, broker):
    """The site's ring reaches the copy whose doorbell it is."""
    address, password = broker
    process = listener(binary, address, ID)
    publish(address, "doorbell/" + ID, "doorbell-site", password)
    output, _ = process.communicate(timeout=20)
    assert process.returncode == 0 and output.strip() == "ring 2", output


def test_only_the_site_rings(binary, broker):
    """Rings from anyone but the site, and rings to another doorbell, never
    arrive; the site's still does."""
    address, password = broker
    process = listener(binary, address, ID)
    publish(address, "doorbell/" + ID, payload="\x01")
    publish(address, "doorbell/" + OTHER, "doorbell-site", password, payload="\x01")
    with pytest.raises(subprocess.CalledProcessError):
        publish(address, "doorbell/" + ID, "doorbell-site", "wrong password", payload="\x01")
    time.sleep(0.5)
    publish(address, "doorbell/" + ID, "doorbell-site", password, payload="\x04")
    output, _ = process.communicate(timeout=20)
    assert process.returncode == 0 and output.strip() == "ring 4", output


def test_no_snooping(broker):
    """A client can't read another doorbell, or every doorbell, whatever its
    client ID says."""
    address, password = broker
    host, port = address.split(":")
    sub = mosquitto_tool("mosquitto_sub")
    if not sub:
        pytest.skip("no mosquitto_sub")
    snoopers = [subprocess.Popen([sub, "-h", host, "-p", port, "-V", "mqttv311", "-i", client, "-t", topic, "-C", "1",
                                  "-W", "3"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                for client, topic in ((OTHER, "doorbell/" + ID), (OTHER, "doorbell/+"), (OTHER, "doorbell/#"),
                                      ("#", "doorbell/#"), ("+", "doorbell/+"), (OTHER, "#"))]
    owner = subprocess.Popen([sub, "-h", host, "-p", port, "-V", "mqttv311", "-i", ID, "-t", "doorbell/" + ID, "-C", "1",
                              "-W", "3"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1)
    publish(address, "doorbell/" + ID, "doorbell-site", password, payload="ring")
    for snooper in snoopers:
        output, _ = snooper.communicate(timeout=10)
        assert output == "", output
    # (the doorbell's own client heard it: the snoopers were really listening)
    output, _ = owner.communicate(timeout=10)
    assert output.strip() == "ring", output
