"""Runs the C tests of network.public_address, the address a host advertises
in place of the STUN servers' (port/linux/src/p2p_public_address.h), with
the build machine's C compiler. The unit has no SDL or game dependency, so it
compiles anywhere."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    """Finds a C compiler on the build machine.

    Returns the path of clang, cc or gcc (the first found), or None.
    """
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_p2p_public_address(tmp_path):
    """Builds the public address unit with its tests, warnings as errors, and runs them."""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "p2p_public_address_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port" / "linux" / "tests" / "p2p_public_address_test.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_public_address_applies_only_while_hosting():
    """network.public_address is a host setting: every place p2p.c acts on it
    goes through public_active() (set and hosting), so a machine joining
    someone else's game keeps STUN, UPnP and hole punching as usual."""
    source = (ROOT / "port" / "linux" / "src" / "p2p.c").read_text(encoding="utf-8")
    active = source[source.index("static int public_active(void)"):]
    active = active[:active.index("\n}\n")]
    assert "p2p.public_set && p2p.hosting" in active
    uses = [line.strip() for line in source.splitlines() if "p2p.public_set" in line]
    assert uses == ["return p2p.public_set && p2p.hosting;", "p2p.public_set = 1;"], uses
    # (STUN servers are still set up when the address is set, for joining)
    setup = source[source.index("static void stun_setup(void)"):]
    setup = setup[:setup.index("\n}\n")]
    parsed = setup[setup.index("p2p_parse_public_address("):]
    assert "return;" not in parsed[:parsed.index("while (*text")]
    for gate in ("else if (public_active() && !peer->is_host)",
                 "if (!allowed || public_active() || p2p.upnp_working",
                 "for (index = 0; index < count && !public_active(); index++)",
                 "if (!public_active() || !p2p.stun_started)"):
        assert gate in source, gate
