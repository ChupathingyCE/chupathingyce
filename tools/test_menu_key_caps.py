"""Check that the menus' keyboard key caps press what they say.

ui_overlay.c draws each pad button's prompt, after keyboard use, as the key
cap of the key that presses it in the menus; xinput_sdl.c's keyboard_gamepad
is what the keys press there. Each cap must name a key that keyboard_gamepad
maps to that button (Online Games showed ESC for Start, and X for the right
trigger, which no key pressed), and escape is always B.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ui_overlay.h's UI_BUTTON_* order, and what each presses in an XINPUT_GAMEPAD
# (the left trigger's cap is Q, the left bumper's key: the screens that show
# it, Online Games' sorts, take the white button for it too)
BUTTONS = [
	("A", ["bAnalogButtons[XINPUT_GAMEPAD_A]"]),
	("B", ["bAnalogButtons[XINPUT_GAMEPAD_B]"]),
	("X", ["bAnalogButtons[XINPUT_GAMEPAD_X]"]),
	("Y", ["bAnalogButtons[XINPUT_GAMEPAD_Y]"]),
	("START", ["XINPUT_GAMEPAD_START"]),
	("LEFT_TRIGGER", ["bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER]", "bAnalogButtons[XINPUT_GAMEPAD_WHITE]"]),
	("RIGHT_TRIGGER", ["bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER]"]),
	("LEFT_SHOULDER", ["bAnalogButtons[XINPUT_GAMEPAD_WHITE]"]),
	("RIGHT_SHOULDER", ["bAnalogButtons[XINPUT_GAMEPAD_BLACK]"]),
	("DPAD_LEFT", ["XINPUT_GAMEPAD_DPAD_LEFT"]),
	("DPAD_RIGHT", ["XINPUT_GAMEPAD_DPAD_RIGHT"]),
	("BACK", ["XINPUT_GAMEPAD_BACK"]),
]

# a cap's label -> SDL's scancode name
SCANCODES = {"ENTER": "RETURN", "BKSP": "BACKSPACE", "ESC": "ESCAPE", "←": "LEFT", "→": "RIGHT"}


def function_body(source, name):
	start = source.index("static void " + name + "(")
	body = source.index("\n{", start)
	return source[body:source.index("\n}\n", body)]


def keyboard_caps():
	overlay = (ROOT / "port/linux/src/ui_overlay.c").read_text(encoding="utf-8")
	table = overlay[overlay.index("pill_labels[NUMBER_OF_PROMPT_SCHEMES][NUMBER_OF_UI_BUTTONS]"):]
	row = re.search(r"\{\s*(\"ENTER\"[^}]*)\}", table).group(1)
	labels = re.findall(r'"((?:[^"\\]|\\.)*)"', row)
	return [label.encode("latin-1").decode("unicode_escape").encode("latin-1").decode("utf-8") for label in labels]


def mapped_keys():
	"""each of keyboard_gamepad's statements: the pad fields it sets and the
	keys it reads"""
	source = (ROOT / "port/linux/src/xinput_sdl.c").read_text(encoding="utf-8")
	body = re.sub(r"/\*.*?\*/", "", function_body(source, "keyboard_gamepad"), flags=re.S)
	body = re.sub(r"#ifdef.*?#endif", "", body, flags=re.S)
	# (its "enter": Return or the keypad's Enter)
	body = re.sub(r"\benter\b", "SDL_SCANCODE_RETURN", body)
	return [(statement, set(re.findall(r"SDL_SCANCODE_(\w+)", statement))) for statement in body.split(";")]


def main():
	caps = keyboard_caps()
	statements = mapped_keys()
	failures = 0
	assert len(caps) == len(BUTTONS), caps
	for (button, targets), label in zip(BUTTONS, caps):
		scancode = SCANCODES.get(label, label)
		pressed = any(scancode in keys and any(target in statement for target in targets)
			for statement, keys in statements)
		if not pressed:
			print(f"FAIL {button}: its key cap {label!r} presses nothing of {targets}")
			failures += 1
		else:
			print(f"ok   {button}: {label}")
	if "ESC" in caps and BUTTONS[caps.index("ESC")][0] != "B":
		print("FAIL escape is B's, not " + BUTTONS[caps.index("ESC")][0] + "'s")
		failures += 1
	return 1 if failures else 0


def test_menu_key_caps():
	assert main() == 0


if __name__ == "__main__":
	sys.exit(main())
