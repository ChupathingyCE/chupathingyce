"""Check the controller's layouts (port/linux/src/controller_layout.c) and
where they are kept.

- the unit's own tests (port/linux/tests/controller_layout_test.c): the
  presets, swaps, the text config.toml keeps, the controllers' names;
- its tables against the game's: the presets as player_ui.c's
  set_local_player_controls_from_player_profile has them, the buttons as
  input.h numbers them, the actions as input_abstraction.c does, the
  presets as player_profile.h does (the custom one last, so that a build
  without it reads it as it reads any preset it does not know: Default);
- a custom layout written into config.toml (port_config.c, built as
  tools/test_port_config.py builds it) and read back, in a new process, as
  the same layout.

Needs only a C compiler. Usage: test_controller_layout.py [--cc clang]
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

import test_port_config  # noqa: E402

UNIT = ROOT / "port/linux/src/controller_layout.c"


def compiler(preferred=None):
	for name in ([preferred] if preferred else []) + ["clang", "cc", "gcc"]:
		path = shutil.which(name)
		if path:
			return path
	return None


def build_unit(cc, directory):
	program = directory / "controller_layout_test"
	subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-I", str(ROOT / "port/linux/src"),
		str(ROOT / "port/linux/tests/controller_layout_test.c"), str(UNIT), "-o", str(program)], check=True)
	return program


def strip_comments(text):
	return re.sub(r"/\*.*?\*/", "", text, flags=re.S)


def enum_names(text, first):
	"""The names of the enumeration whose first name is first, in order."""
	start = text.index(first)
	body = text[text.rindex("{", 0, start) + 1:text.index("}", start)]
	return [entry.split("=")[0].strip() for entry in strip_comments(body).split(",") if entry.strip()]


def check_tables():
	failures = []
	unit = UNIT.read_text()
	header = (ROOT / "port/linux/src/controller_layout.h").read_text()
	input_h = (ROOT / "source/input/input.h").read_text()
	abstraction = (ROOT / "source/input/input_abstraction.c").read_text()
	profile = (ROOT / "source/saved games/player_profile.h").read_text()
	player_ui = (ROOT / "source/interface/player_ui.c").read_text()

	# the buttons: the game's analog and binary ones, in its order
	game_buttons = [name for name in enum_names(input_h, "_gamepad_analog_button_a")
		if name.startswith("_gamepad_") and not name.startswith("NUMBER")]
	ours = [name for name in enum_names(header, "CONTROLLER_BUTTON_A") if name != "CONTROLLER_BUTTON_COUNT"]
	if len(game_buttons) != len(ours):
		failures.append(f"buttons: {len(ours)} here, {len(game_buttons)} in input.h")
	# the actions, in the game's order
	game_controls = [name for name in enum_names(abstraction, "_game_control_jump") if name.startswith("_game_control")]
	actions = [name for name in enum_names(header, "CONTROLLER_ACTION_JUMP") if name != "CONTROLLER_ACTION_COUNT"]
	expected_actions = ["jump", "switch_grenades", "action", "switch_weapons", "melee", "flashlight", "grenade",
		"primary_trigger", "start", "back", "crouch", "zoom"]
	if [name.replace("_game_control_", "") for name in game_controls] != expected_actions or len(actions) != 12:
		failures.append(f"actions: input_abstraction.c has {game_controls}")
	# the presets: the custom one after the game's
	presets = [name for name in enum_names(profile, "_button_preset_standard") if name.startswith("_button")]
	if presets[-1] != "_button_preset_custom" or len(presets) != 7:
		failures.append(f"presets: player_profile.h has {presets}")

	# each preset's table in player_ui.c, as button names
	short = {"a": "A", "b": "B", "x": "X", "y": "Y", "black": "BLACK", "white": "WHITE", "left_trigger": "LT",
		"right_trigger": "RT", "start": "START", "back": "BACK", "left_thumb": "LS", "right_thumb": "RS"}
	switch = player_ui[player_ui.index("switch (controls->button_preset)"):]
	switch = switch[:switch.index("default:")]
	cases = re.split(r"case (_button_preset_\w+):", switch)[1:]
	tables = {}
	for name, body in zip(cases[0::2], cases[1::2]):
		buttons = re.findall(r"game_control_to_xbox_buttons\[(\d+)\] = _gamepad_(?:analog|binary)_button_(\w+);", body)
		tables[name] = [short[button] for _, button in sorted(buttons, key=lambda pair: int(pair[0]))]
	rows = re.findall(r"^\t\{ ([A-Z, ]+) \},", unit[unit.index("preset_layouts["):unit.index("preset_titles")], re.M)
	ours = [[entry.strip() for entry in row.split(",")] for row in rows]
	for index, name in enumerate(presets[:-1]):
		if tables.get(name) != (ours[index] if index < len(ours) else None):
			failures.append(f"{name}: player_ui.c {tables.get(name)}, controller_layout.c "
				f"{ours[index] if index < len(ours) else None}")
	# the screens' rows: Controller Buttons' labels (tools/port_settings.py) and
	# the actions menu_functions.c and controls_screen.c put on them
	titles = dict(zip(actions, re.findall(r'"([A-Z /]+)"', unit[unit.index("action_titles["):
		unit.index("};", unit.index("action_titles["))])))
	sys.path.insert(0, str(ROOT / "tools"))
	import port_settings
	for path in ("port/linux/game/menu_functions.c", "port/linux/game/controls_screen.c"):
		source = (ROOT / path).read_text()
		start = source.index("button_row_actions[BUTTON_ROWS] =")
		rows = re.findall(r"CONTROLLER_ACTION_\w+", source[start:source.index("};", start)])
		if [f"{titles[row]}:" for row in rows] != port_settings.CONTROLLER_ACTIONS:
			failures.append(f"{path}'s rows {rows} are not port_settings.py's {port_settings.CONTROLLER_ACTIONS}")
	if port_settings.CONTROLLER_PRESETS != re.findall(r'"([A-Z, ]+)"', unit[unit.index("preset_titles["):
		unit.index("};", unit.index("preset_titles["))]):
		failures.append("port_settings.py's CONTROLLER_PRESETS are not controller_layout.c's preset_titles")
	# the custom preset read from config.toml, else the default (any newer one)
	if "case _button_preset_custom:" not in player_ui:
		failures.append("player_ui.c does not read the custom layout")
	return failures


def check_config(cc, unit_program, directory):
	failures = []
	program = test_port_config.build(cc, "desktop", directory / "config_build")
	case = test_port_config.Case(program, directory / "config_case")
	values, _ = case.get("controls.controller_layout")
	if values.get("controls.controller_layout") != "":
		failures.append(f"controls.controller_layout's default: {values.get('controls.controller_layout')!r}")
	layout = subprocess.run([str(unit_program), "format", "melee=LT, jump=LB, zoom=B"], capture_output=True,
		text=True, check=True).stdout.strip()
	case.run("write", "controls.controller_layout", layout)
	values, _ = case.get("controls.controller_layout")
	read = values.get("controls.controller_layout", "")
	if read != layout:
		failures.append(f"config.toml gave back {read!r} for {layout!r}")
	again = subprocess.run([str(unit_program), "format", read], capture_output=True, text=True,
		check=True).stdout.strip()
	if again != layout:
		failures.append(f"the layout read back is {again!r}, not {layout!r}")
	for part in ("melee=LT", "jump=LB", "zoom=B"):
		if part not in again:
			failures.append(f"{part} not in {again!r}")
	return failures


def run_all(cc):
	"""Every check; the failures, in words."""
	failures = check_tables()
	with tempfile.TemporaryDirectory(prefix="controls-") as folder:
		directory = Path(folder)
		program = build_unit(cc, directory)
		result = subprocess.run([str(program)], capture_output=True, text=True)
		if result.returncode:
			failures.append(result.stdout + result.stderr)
		failures += check_config(cc, program, directory)
	return failures


def test_controller_layout():
	cc = compiler()
	if not cc:
		import pytest
		pytest.skip("no C compiler")
	assert run_all(cc) == []


def main():
	parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
	parser.add_argument("--cc")
	arguments = parser.parse_args()
	cc = compiler(arguments.cc)
	if not cc:
		print("no C compiler")
		return 1
	failures = run_all(cc)
	for failure in failures:
		print(f"FAIL {failure}")
	if not failures:
		print("ok")
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
