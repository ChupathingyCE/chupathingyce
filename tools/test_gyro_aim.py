"""Check gyro aiming (port/linux/src/gyro_aim.c):

- the unit's own tests (port/linux/tests/gyro_aim_test.c), on made-up
  sensor samples: the bias learnt while the controller lies still, the
  smoothing of small motion, the deadzone, the axes, and the settings read
  from config.toml's values with each out of range clamped;
- the settings in config.toml (port_config.c, built as
  tools/test_port_config.py builds it): off by default, the Switch's
  defaults as they were, on the desktop and Android and on no other
  console; a value written out of range is read back and clamped;
- the menus' GYRO page (tools/port_settings.py) names only settings
  port_config.c has, and the Switch's own page (its tools/switch_menus.py,
  which adds GYRO to CONTROLLER_PAGES) leaves this one out.

Needs only a C compiler. Usage: test_gyro_aim.py [--cc clang]
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

import port_settings  # noqa: E402
import test_port_config  # noqa: E402

# the settings and their defaults (as config_text gives them): the Switch's
# five as they were, and the two SDL's builds add (HALO_PORT_GYRO_SDL)
SHARED = {
	"input.gyro_aim": "false",
	"input.gyro_sensitivity_horizontal": "1",
	"input.gyro_sensitivity_vertical": "1",
	"input.gyro_invert_horizontal": "false",
	"input.gyro_invert_vertical": "false",
}
SDL_ONLY = {
	"input.gyro_horizontal_axis": "yaw",
	"input.gyro_deadzone": "0.75",
}


def compiler(preferred=None):
	for name in ([preferred] if preferred else []) + ["clang", "cc", "gcc"]:
		path = shutil.which(name)
		if path:
			return path
	return None


def build_unit(cc, directory):
	program = directory / "gyro_aim_test"
	subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-I", str(ROOT / "port/linux/src"),
		str(ROOT / "port/linux/tests/gyro_aim_test.c"), str(ROOT / "port/linux/src/gyro_aim.c"), "-lm",
		"-o", str(program)], check=True)
	return program


def parsed(unit, values):
	output = subprocess.run([str(unit), "settings", *[values[name] for name in (
		"input.gyro_aim", "input.gyro_horizontal_axis", "input.gyro_sensitivity_horizontal",
		"input.gyro_sensitivity_vertical", "input.gyro_invert_horizontal", "input.gyro_invert_vertical",
		"input.gyro_deadzone")]], capture_output=True, text=True, check=True).stdout
	return dict(re.findall(r"^([a-z_]+)=(.*)$", output, re.M))


def check_config(cc, unit, directory):
	failures = []
	everything = [*SHARED, *SDL_ONLY]
	for kind, expected in (("desktop", {**SHARED, **SDL_ONLY}), ("android", SHARED), ("console", {}),
	                       ("server", {})):
		program = test_port_config.build(cc, kind, directory / f"config_{kind}")
		case = test_port_config.Case(program, directory / f"case_{kind}")
		values, output = case.get(*everything)
		for name in everything:
			if name in expected and values.get(name) != expected[name]:
				failures.append(f"{kind}: {name} is {values.get(name)!r}, not {expected[name]!r}")
			if name not in expected and f"{name}: no such setting" not in output:
				failures.append(f"{kind}: has {name}")
	# off by default, as the unit reads the defaults
	program = test_port_config.build(cc, "desktop", directory / "config_values")
	case = test_port_config.Case(program, directory / "case_values")
	values, _ = case.get(*everything)
	settings = parsed(unit, values)
	if settings.get("enabled") != "0" or settings.get("roll") != "0":
		failures.append(f"the defaults read as {settings}")
	if abs(float(settings.get("deadzone_degrees", "nan")) - 0.75) > 1e-3:
		failures.append(f"the default deadzone reads as {settings.get('deadzone_degrees')}")
	# written out of range: kept in config.toml as written, read clamped
	for name, value in (("input.gyro_aim", "zoom"), ("input.gyro_horizontal_axis", "roll"),
	                    ("input.gyro_sensitivity_horizontal", "50"), ("input.gyro_sensitivity_vertical", "0.1"),
	                    ("input.gyro_invert_vertical", "true"), ("input.gyro_deadzone", "-4")):
		case.run("write", name, value)
	values, _ = case.get(*everything)
	settings = parsed(unit, values)
	wanted = {"enabled": "1", "zoom_only": "1", "roll": "1", "invert_horizontal": "0", "invert_vertical": "1",
		"sensitivity_horizontal": "10", "sensitivity_vertical": "0.25", "deadzone_degrees": "0.0000"}
	for key, value in wanted.items():
		if settings.get(key) != value:
			failures.append(f"written values: {key} reads as {settings.get(key)!r}, not {value!r}")
	return failures


def check_menus():
	failures = []
	config = (ROOT / "port/linux/src/port_config.c").read_text()
	for row in port_settings.GYRO_ROWS:
		if f'{{ "{row[1]}",' not in config:
			failures.append(f"the GYRO page's {row[1]} is not a setting")
		for _, value in row[2]:
			if row[1] == "input.gyro_horizontal_axis" and value not in ("yaw", "roll"):
				failures.append(f"{row[1]}: {value!r}")
	# (this page with the desktop's pages; none when the Switch's is added)
	rows, _ = port_settings._controls_rows()
	if not any(widget.endswith("/view_gyro") for widget, _, _ in rows):
		failures.append("Controls Setup has no GYRO page")
	pages = port_settings.CONTROLLER_PAGES
	try:
		port_settings.CONTROLLER_PAGES = [*pages, "GYRO"]
		rows, extra = port_settings._controls_rows()
		if any(widget.endswith("/view_gyro") or "op_gyro_" in widget for widget, _, _ in rows):
			failures.append("with the Switch's GYRO page, this one is there too")
		if any("view_gamepad_buttons_gyro" in line for line in extra):
			failures.append("with the Switch's GYRO page, the SHOW row is marked for this one")
	finally:
		port_settings.CONTROLLER_PAGES = pages
	return failures


def run_all(cc):
	"""Every check; the failures, in words."""
	failures = check_menus()
	with tempfile.TemporaryDirectory(prefix="gyro-") as folder:
		directory = Path(folder)
		unit = build_unit(cc, directory)
		result = subprocess.run([str(unit)], capture_output=True, text=True)
		if result.returncode:
			failures.append(result.stdout + result.stderr)
		failures += check_config(cc, unit, directory)
	return failures


def test_gyro_aim():
	cc = compiler()
	assert cc, "no C compiler"
	failures = run_all(cc)
	assert not failures, "\n".join(failures)


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
		print("FAIL", failure)
	if not failures:
		print("gyro aim: all checks passed")
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
