"""Check that the HUD and menu button prompts name the right game control.

hud_messaging.c and ui_widget.c map each game-control icon (action .. zoom)
to an index in input_abstraction.c's game control enumeration. The keyboard
prompt table in input_abstraction.c lists the same icons in the same order,
so each entry must land on the control whose keyboard action that table names.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# keyboard action (port_icon_keyboard_actions) -> game control enumerator
KEYBOARD_TO_CONTROL = {
	"HALO_KEYBOARD_ACTION": "_game_control_action",
	"HALO_KEYBOARD_THROW_GRENADE": "_game_control_grenade",
	"HALO_KEYBOARD_FIRE": "_game_control_primary_trigger",
	"HALO_KEYBOARD_FLASHLIGHT": "_game_control_flashlight",
	"HALO_KEYBOARD_JUMP": "_game_control_jump",
	"HALO_KEYBOARD_MELEE": "_game_control_melee",
	"HALO_KEYBOARD_SWITCH_WEAPON": "_game_control_switch_weapons",
	"HALO_KEYBOARD_SWITCH_GRENADE": "_game_control_switch_grenades",
	"HALO_KEYBOARD_CROUCH": "_game_control_crouch",
	"HALO_KEYBOARD_ZOOM": "_game_control_zoom",
}


def braces(source, marker):
	start = source.index(marker)
	open_index = source.index("{", start)
	return source[open_index + 1:source.index("}", open_index)]


def strip_comments(text):
	return re.sub(r"/\*.*?\*/", "", text, flags=re.S)


def entries(text):
	return [entry.strip() for entry in strip_comments(text).split(",") if entry.strip()]


def main():
	abstraction = (ROOT / "source/input/input_abstraction.c").read_text()
	enum_start = abstraction.index("_game_control_jump,")
	enum_text = abstraction[abstraction.rindex("{", 0, enum_start) + 1:abstraction.index("}", enum_start)]
	controls = [entry.split("=")[0].strip() for entry in entries(enum_text)]
	keyboard = entries(braces(abstraction, "port_icon_keyboard_actions["))
	expected = [controls.index(KEYBOARD_TO_CONTROL[action]) for action in keyboard]

	failures = 0
	for path in ("source/interface/hud_messaging.c", "source/interface/ui_widget.c"):
		source = (ROOT / path).read_text()
		table = braces(source, "static char button_mappings[")
		actual = [int(entry) for entry in entries(table)[:len(expected)]]
		if actual != expected:
			print(f"FAIL {path}: button_mappings {actual} != {expected}")
			failures += 1
		else:
			print(f"ok   {path}")
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
