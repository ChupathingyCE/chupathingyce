"""Stopping a channel on the SDL backend: the flush's callbacks never refill it, and its packets are all released."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure

CASES = ["stop", "idle-backlog", "paused", "refill"]
NEGATIVE_CONTROLS = {
    "keeps-producer": (("channel->playing_permutation= NULL;", ""), "stop"),
    "skips-idle-backlog": (("if (channel->state!=_sound_channel_idle || channel->packet_count!=0)",
                            "if (channel->state!=_sound_channel_idle)"), "idle-backlog"),
    "never-refills": (("else if (status!=XMEDIAPACKET_STATUS_FLUSHED)", "else if (FALSE)"), "refill"),
}


def generated(fault=None):
    backend = read("source/sound/sound_dsound_xbox.c")
    statuses = re.findall(r"^#define XMEDIAPACKET_STATUS_\w+ .*$", read("port/include/xdk/xdk_dsound.h"), re.M)
    types = "\n".join([*statuses, enum_with(backend, "_sound_channel_idle"), structure(backend, "sound_channel")])
    code = function(backend, "dsound_channel_callback") + "\n" + function(backend, "channel_stop")
    if fault:
        code = mutated(code, *fault)
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("sound_channel_stop", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("sound_channel_stop", generated(fault)), case)
    assert status == CHECK_FAILED, f"{control} escaped {case}: {output}"
