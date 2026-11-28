"""The vehicle prompts' name (source/interface/hud.c, #1376): each of the
enter, exit and flip prompts names its vehicle through
hud_vehicle_name_index, which gives "vehicle" (HUD_STATE_TEXT_GENERIC_VEHICLE)
in place of "<missing string>" when the map's hud icon text has no string at
the vehicle's index, and the state message draws that index as "vehicle"."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HUD = (ROOT / "source" / "interface" / "hud.c").read_text()
MESSAGING = (ROOT / "source" / "interface" / "hud_messaging.c").read_text()


def case(name):
    body = HUD[HUD.index(f"case {name}:"):]
    return body[:body.index("return;")]


def test_vehicle_prompts_use_the_checked_name():
    for name in ("_player_action_result_exit_vehicle", "_player_action_result_enter_vehicle",
                 "_player_action_result_flip_vehicle"):
        assert "hud_vehicle_name_index(" in case(name), name
    assert "get_object_icon_text_index(unit->object.parent_object_index)" not in case(
        "_player_action_result_exit_vehicle")


def test_the_name_is_checked_against_the_icon_text():
    helper = HUD[HUD.index("static short hud_vehicle_name_index("):]
    helper = helper[:helper.index("\n}\n")]
    assert "alternate_icon_text.index" in helper
    assert re.search(r"VALID_INDEX\(text_index, list->strings\.count\)", helper)
    assert "return HUD_STATE_TEXT_GENERIC_VEHICLE;" in helper


def test_the_generic_name_is_drawn_as_vehicle():
    at = MESSAGING.index("string_index == HUD_STATE_TEXT_GENERIC_VEHICLE")
    assert 'L"vehicle"' in MESSAGING[at:at + 300]
    # (checked before NONE and the string lists)
    assert at < MESSAGING.index("else if (string_index != NONE)")
