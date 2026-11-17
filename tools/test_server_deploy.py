"""The dedicated server's deploy scripts (server/deploy/deploy.sh,
deploy-instance.sh, deploy-probe.sh), run against stand-ins for ssh and scp
that only write down what they were asked: what goes to the host passes
through a folder mktemp made there for the run, never a fixed name in the
host's shared /tmp (which another user of the host could make first, and
change before it is installed as root's)."""
import os
import re
import shutil
import stat
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
DEPLOY = ROOT / "server" / "deploy"
# (what the stand-in ssh answers for mktemp -d: a folder of the run's own)
REMOTE_FOLDER = "/tmp/tmp.Xy7Qp2LmNa"

SSH = f"""#!/bin/sh
printf 'ssh' >> "$DEPLOY_LOG"
for word in "$@"; do printf ' <%s>' "$word" >> "$DEPLOY_LOG"; done
printf '\\n' >> "$DEPLOY_LOG"
case "$*" in
*"mktemp -d"*) echo {REMOTE_FOLDER} ;;
esac
exit 0
"""

SCP = """#!/bin/sh
printf 'scp' >> "$DEPLOY_LOG"
for word in "$@"; do printf ' <%s>' "$word" >> "$DEPLOY_LOG"; done
printf '\\n' >> "$DEPLOY_LOG"
exit 0
"""


def run(script, arguments, tmp_path):
    """script run with the stand-ins first on PATH: what they were asked,
    a line each"""
    if not shutil.which("sh") or not shutil.which("od"):
        pytest.skip("no POSIX shell")
    bin_folder = tmp_path / "bin"
    bin_folder.mkdir()
    for name, text in (("ssh", SSH), ("scp", SCP)):
        path = bin_folder / name
        path.write_text(text)
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
    log = tmp_path / "log"
    log.write_text("")
    environment = dict(os.environ, PATH=f"{bin_folder}{os.pathsep}{os.environ['PATH']}", DEPLOY_LOG=str(log),
                       TMPDIR=str(tmp_path))
    result = subprocess.run(["sh", str(DEPLOY / script), *arguments], capture_output=True, text=True,
                            env=environment, cwd=ROOT, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return log.read_text().splitlines()


def fake_server(tmp_path):
    """a file whose ELF header says x86-64 (all server-arch.sh reads)"""
    binary = tmp_path / "chupathingyce-server"
    header = bytearray(64)
    header[0:4] = b"\x7fELF"
    header[18:20] = (62).to_bytes(2, "little")
    binary.write_bytes(bytes(header))
    return binary


def check_no_shared_tmp(lines):
    """no scp into the host's /tmp itself, and nothing taken from a fixed
    name there"""
    for line in lines:
        words = re.findall(r"<([^>]*)>", line)
        if line.startswith("scp"):
            destination = words[-1]
            host_path = destination.split(":", 1)[1] if ":" in destination else destination
            if "/tmp" in host_path:
                assert host_path.startswith(REMOTE_FOLDER), line
        else:
            for word in words:
                for path in re.findall(r"/tmp/[A-Za-z0-9._@-]+", word):
                    assert path == REMOTE_FOLDER, line


def test_deploy_uses_its_own_folder(tmp_path):
    lines = run("deploy.sh", ["--no-restart", "user@host", str(fake_server(tmp_path))], tmp_path)
    check_no_shared_tmp(lines)
    assert any("mktemp -d" in line for line in lines)
    assert any(f"{REMOTE_FOLDER}/halo-dedicated.service" in line for line in lines)


def test_deploy_instance_uses_its_own_folder(tmp_path):
    lines = run("deploy-instance.sh", ["user@host", "team"], tmp_path)
    check_no_shared_tmp(lines)
    assert any(f"{REMOTE_FOLDER}/halo-dedicated@.service" in line for line in lines)


def test_deploy_probe_uses_its_own_folder(tmp_path):
    lines = run("deploy-probe.sh", ["user@host", str(fake_server(tmp_path)), "ssh-ed25519 AAAAC3Nza site"],
                tmp_path)
    check_no_shared_tmp(lines)
    assert any(f"{REMOTE_FOLDER}/probe_authorized_keys" in line for line in lines)
