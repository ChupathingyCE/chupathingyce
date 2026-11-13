"""A co-op client with nobody to spectate watches the host from behind (network_coop.c's client_watch_host_from_behind):
a wall or ceiling between the host's eyes and that spot keeps the camera in front of it, not inside or past it."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

SOURCE = "port/linux/game/network_coop.c"
CASES = ["open", "wall", "wall-at-eyes", "eased"]


def generated(fault=None):
    source = read(SOURCE)
    config = ""
    for name in ("WATCH_HOST_DISTANCE", "WATCH_HOST_HEIGHT", "WATCH_HOST_FOLLOW", "WATCH_HOST_WALL_GAP"):
        value = re.search(rf"^#define {name}\s+(\S+)", source, re.M)
        assert value, f"{name} not found in {SOURCE}"
        config += f"#define {name} {value.group(1)}\n"
    code = function(source, "client_watch_host_from_behind")
    if fault == "no-wall":
        code = mutated(code, "point_from_line3d(position, &reach, MAX(t, 0.0f), &coop_presentation.watching_host_position);",
                       "(void)t;")
    if fault == "no-gap":
        code = mutated(code, "collision.t - WATCH_HOST_WALL_GAP / length", "collision.t")
    return (("under_test.inc", config + code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("coop_watch_host", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [("no-wall", "wall"), ("no-gap", "wall")])
def test_negative_control(fault, case):
    status, output = run(build("coop_watch_host", generated(fault)), case)
    assert status == CHECK_FAILED, output
