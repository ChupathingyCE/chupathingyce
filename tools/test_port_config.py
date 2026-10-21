"""Check config.toml's handling of defaults (port/linux/src/port_config.c).

Builds port_config.c on its own with tools/tests/port_config_test.c (SDL's
few functions stood in for with the C library), once for each kind of build
whose defaults differ, and runs it on config.toml files of each history a
player's can have:

- a new file: every setting commented out at its default;
- a file from before config_version, every setting written as a value:
  values at this version's default are commented out, every other kept
  (an earlier default may be the player's choice);
- a file of this layout that an older version then ran on, which added each
  commented-out setting again as a value at its default: left alone until the
  player answers the question about the new defaults, then those values are
  taken out ("use the new settings") or stay ("keep mine");
- a file of the previous release, with the old defaults commented out: the
  settings keep their old values and the file stays as it is, the player is
  asked once, "keep mine" writes the old values and "use the new settings"
  lets them follow the new defaults; the question never comes again;
- the per-platform defaults of each build (DEFAULTS below).

Needs only a C compiler. Usage: test_port_config.py [--cc clang]
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# the defaults this version gives each kind of build, for the settings whose
# default is not the same everywhere or changed in this version
DEFAULTS = {
	"desktop": {
		"display.anti_aliasing": "msaa2x",
		"display.shadow_resolution": "256",
	},
	"desktop32": {
		"display.anti_aliasing": "off",
		"display.shadow_resolution": "256",
		"audio.buffer_frames": "512",
	},
	"android": {
		"display.anti_aliasing": "off",
		"display.shadow_resolution": "256",
		"audio.buffer_frames": "1024",
	},
	"server": {
		"display.anti_aliasing": "off",
		"display.shadow_resolution": "128",
	},
}

# the settings an existing file would see change, for each kind of build, with
# the value the previous release had (the dedicated server is not asked: its
# defaults are its own)
OLD = {
	"display.menus": "xbox",
	"display.anti_aliasing": "off",
	"display.shadow_resolution": "128",
	"audio.buffer_frames": "512",
}
CHANGED = {
	"desktop": ["display.menus", "display.anti_aliasing", "display.shadow_resolution"],
	"desktop32": ["display.menus", "display.shadow_resolution"],
	"android": ["display.menus", "display.shadow_resolution", "audio.buffer_frames"],
	"server": [],
}
NEW = {"display.menus": "chupathingy"}

# the macros each kind of build is compiled with; the others are not macOS
# (whose own defaults the native build checks on a Mac)
DEFINES = {
	"desktop": ["-DHALO_64BIT", "-DHALO_GAME_BROWSER"],
	"desktop32": ["-U__APPLE__", "-DHALO_GAME_BROWSER"],
	"android": ["-U__APPLE__", "-DHALO_ANDROID", "-DHALO_GAME_BROWSER"],
	"server": ["-U__APPLE__", "-DHALO_SERVER", "-DHALO_64BIT", "-DHALO_GAME_BROWSER"],
}

STUB_PLATFORM = """#include <stddef.h>
void platform_log(const char *format, ...);
"""

STUB_POSIX = """#include <sys/stat.h>
typedef unsigned long posix_ulong;
struct posix_file_information
{
	posix_ulong flags, size_low, size_high, modification_seconds, modification_nanoseconds,
		access_seconds, access_nanoseconds, creation_seconds, creation_nanoseconds;
};
static int posix_stat(const char *path, struct posix_file_information *information)
{
	struct stat st;

	if (stat(path, &st) != 0)
		return -1;
	information->flags = 0;
	information->size_low = (posix_ulong)st.st_size;
	information->size_high = 0;
	information->modification_seconds = (posix_ulong)st.st_mtime;
	information->modification_nanoseconds = 0;
	return 0;
}
"""

STUB_SDL = """#include <stdbool.h>
#include <stddef.h>
const char *SDL_GetBasePath(void);
bool SDL_CreateDirectory(const char *path);
void *SDL_LoadFile(const char *path, size_t *size);
bool SDL_SaveFile(const char *path, const void *data, size_t size);
void SDL_free(void *memory);
typedef struct { long long size; long long modify_time; } SDL_PathInfo;
bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info);
int SDL_strncasecmp(const char *first, const char *second, size_t length);
"""


def build(cc, kind, directory):
	"""The test program for a kind of build, in directory."""
	source = directory / "source"
	(source / "SDL3").mkdir(parents=True, exist_ok=True)
	shutil.copy(ROOT / "port/linux/src/port_config.c", source)
	shutil.copy(ROOT / "port/linux/src/port_config.h", source)
	(source / "platform.h").write_text(STUB_PLATFORM)
	(source / "posix.h").write_text(STUB_POSIX)
	(source / "SDL3/SDL.h").write_text(STUB_SDL)
	program = directory / f"port_config_test_{kind}"
	subprocess.run([cc, "-std=c11", "-D_DEFAULT_SOURCE", "-D_DARWIN_C_SOURCE", "-Wall", "-Werror", "-g",
		*DEFINES[kind], "-I", str(source), "-I", str(ROOT / "port/third_party/tomlc17"),
		str(source / "port_config.c"), str(ROOT / "tools/tests/port_config_test.c"),
		str(ROOT / "port/third_party/tomlc17/tomlc17.c"), "-lpthread", "-o", str(program)],
		check=True)
	return program


class Case:
	def __init__(self, program, directory):
		self.program = program
		self.folder = directory
		self.folder.mkdir(parents=True, exist_ok=True)
		self.path = self.folder / "config.toml"

	def run(self, *arguments):
		environment = {key: value for key, value in os.environ.items() if not key.startswith("HALO_")}
		environment["PORT_CONFIG_TEST_FOLDER"] = str(self.folder)
		environment["HALO_DATA_ROOT"] = str(self.folder)
		result = subprocess.run([str(self.program), *arguments], capture_output=True, text=True,
			env=environment, check=True)
		values = dict(re.findall(r"^([a-z_]+\.[a-z_0-9]+)=(.*)$", result.stdout, re.M))
		return values, result.stdout

	def get(self, *names):
		return self.run("get", *names)

	def text(self):
		return self.path.read_text()


def value_lines(text, key):
	"""The file's lines "key = ..." and "# key = ..." for a key."""
	values = re.findall(rf"^\s*{key}\s*=.*$", text, re.M)
	commented = re.findall(rf"^\s*#\s*{key}\s*=.*$", text, re.M)
	return values, commented


def check(condition, message):
	if not condition:
		raise AssertionError(message)


def pending(case):
	"""The count of changes the player would be asked about, and their words."""
	_, output = case.run("pending")
	return int(re.search(r"^pending=(\d+)$", output, re.M).group(1)), output


def new_value(name, defaults):
	return NEW.get(name) or defaults[name]


def old_value_text(name):
	return OLD[name] if OLD[name].isdigit() else f'"{OLD[name]}"'


def previous_release_file(case, kind):
	"""A file as the previous release wrote it: every setting commented out,
	at that release's defaults."""
	case.get("display.vsync")
	text = case.text()
	for name in CHANGED["android" if kind == "server" else kind]:
		key = name.split(".")[1]
		text = re.sub(rf"^# {key} = .*$", f"# {key} = {old_value_text(name)}", text, flags=re.M)
	case.path.write_text(text)
	return text


def test_new_file(program, directory, defaults, kind):
	case = Case(program, directory / "new")
	values, _ = case.get(*defaults)
	for name, expected in defaults.items():
		check(values[name] == expected, f"new file: {name} is {values[name]}, not {expected}")
	text = case.text()
	check("config_version = " in text, "new file: no config_version")
	for name, expected in defaults.items():
		key = name.split(".")[1]
		lines, commented = value_lines(text, key)
		check(not lines and len(commented) == 1, f"new file: {key} is not commented out once: {lines} {commented}")
	# (a new file is not asked about anything, then or on the next start)
	check(pending(case)[0] == 0, "new file: something to ask about")
	before = case.text()
	case.get("display.vsync")
	check(case.text() == before and pending(case)[0] == 0, "new file: changed or asked on the next start")


def test_previous_release_file(program, directory, defaults, kind):
	"""The question: the previous release's file keeps its old values and
	stays as it is until the player answers, then once for all."""
	changed = CHANGED[kind]
	names = list(changed) or ["display.vsync"]
	for answer in ("keep", "new"):
		case = Case(program, directory / f"previous_{answer}")
		written = previous_release_file(case, kind)
		values, _ = case.get(*names)
		for name in changed:
			check(values[name] == OLD[name], f"previous/{answer}: {name} is {values[name]} before the answer")
		check(case.text() == written, f"previous/{answer}: the file changed before the answer")
		count, output = pending(case)
		check(count == len(changed), f"previous/{answer}: {count} changes, not {len(changed)}: {output}")
		if "display.anti_aliasing" in changed:
			check("Smoother edges (2x multisampling)" in output, f"previous/{answer}: no words for anti-aliasing")
		if "display.shadow_resolution" in changed:
			check("Sharper shadows (256)" in output, f"previous/{answer}: no words for shadows")
		if not changed:
			continue
		case.run("answer", answer)
		text = case.text()
		check(re.search(r"^defaults_answered = 1$", text, re.M), f"previous/{answer}: the answer is not written")
		# (asked once: a restart finds nothing, and changes nothing)
		check(pending(case)[0] == 0, f"previous/{answer}: asked again")
		values, _ = case.get(*names)
		before = case.text()
		case.get("display.vsync")
		check(case.text() == before, f"previous/{answer}: the file changed on the next start")
		for name in changed:
			key = name.split(".")[1]
			lines, commented = value_lines(text, key)
			if answer == "keep":
				check(values[name] == OLD[name], f"previous/keep: {name} is {values[name]}, not {OLD[name]}")
				check(lines == [f"{key} = {old_value_text(name)}"], f"previous/keep: {key} not pinned: {lines}")
			else:
				check(values[name] == new_value(name, defaults), f"previous/new: {name} is {values[name]}")
				check(not lines and commented == [f"# {key} = " + (f'"{values[name]}"' if not values[name].isdigit() else values[name])],
					f"previous/new: {key}: {lines} {commented}")


def test_file_without_the_settings(program, directory, defaults, kind):
	"""A file from before the settings existed: they were at the old
	defaults; asked about, and the file is left alone until answered."""
	changed = CHANGED[kind]
	if not changed:
		return
	case = Case(program, directory / "without")
	case.path.write_text("config_version = 2\n\n[display]\nfullscreen = false\n")
	values, _ = case.get(*changed)
	for name in changed:
		check(values[name] == OLD[name], f"without: {name} is {values[name]}")
		lines, commented = value_lines(case.text(), name.split(".")[1])
		check(not lines and not commented, f"without: {name} was added before the answer: {lines} {commented}")
	check(pending(case)[0] == len(changed), "without: not asked about every change")
	case.run("answer", "keep")
	values, _ = case.get(*changed)
	check(all(values[name] == OLD[name] for name in changed), f"without: keep: {values}")
	check(pending(case)[0] == 0, "without: asked again")
	check("fullscreen = false" in case.text(), "without: the player's fullscreen was lost")


def test_readded_by_older_version(program, directory, defaults, kind):
	"""The owner's case: a config_version 2 file that a build from before
	config_version then ran on, adding every setting again as a value. It is
	left alone until the player answers: use the new settings takes the
	values out, keep mine leaves them."""
	changed = CHANGED[kind]
	for answer in ("new", "keep"):
		case = Case(program, directory / f"readded_{answer}")
		case.get("display.vsync")
		text = case.text()
		# what that older build appends to [display]: its default (the old one
		# for each setting this version changed) and one the player chose
		old = {"shadow_resolution": "128", "anti_aliasing": "\"off\"", "interpolation": "true", "vsync": "true",
			"high_res_hud": "false"}
		block = "".join(f"\n# An older version's comment for {key}.\n# (for one run: HALO_X=<value>)\n{key} = {value}\n"
			for key, value in old.items())
		audio = text.index("\n[audio]")
		text = text[:audio].rstrip("\n") + "\n" + block + text[audio:]
		case.path.write_text(text)
		values, _ = case.get("display.shadow_resolution", "display.anti_aliasing", "display.interpolation",
			"display.vsync", "display.high_res_hud")
		check(values["display.shadow_resolution"] == "128", f"readded: shadow_resolution is {values['display.shadow_resolution']}")
		check(values["display.anti_aliasing"] == "off", f"readded: anti_aliasing is {values['display.anti_aliasing']}")
		check(values["display.high_res_hud"] == "false", "readded: the player's high_res_hud = false was lost")
		check(case.text() == text, f"readded/{answer}: the file changed before the answer")
		count, _ = pending(case)
		wanted = len([name for name in changed if name in ("display.shadow_resolution", "display.anti_aliasing")])
		# (the menus and Android's buffer were not added as values here)
		check(count == wanted, f"readded/{answer}: {count} changes, not {wanted}")
		if not wanted:
			continue
		case.run("answer", answer)
		values, _ = case.get("display.shadow_resolution", "display.anti_aliasing", "display.high_res_hud")
		check(values["display.high_res_hud"] == "false", f"readded/{answer}: high_res_hud lost")
		text = case.text()
		for key in ("shadow_resolution", "anti_aliasing"):
			lines, commented = value_lines(text, key)
			if answer == "new":
				check(not lines and len(commented) == 1, f"readded/new: {key}: {lines} {commented}")
				check(f"An older version's comment for {key}." not in text, f"readded/new: {key}'s added comment stayed")
			else:
				check(len(lines) == 1, f"readded/keep: {key}: {lines}")
		if answer == "new":
			check(values["display.shadow_resolution"] == defaults["display.shadow_resolution"],
				f"readded/new: shadow_resolution stayed {values['display.shadow_resolution']}")
			check(values["display.anti_aliasing"] == defaults["display.anti_aliasing"],
				f"readded/new: anti_aliasing stayed {values['display.anti_aliasing']}")
			lines, commented = value_lines(text, "interpolation")
			check(not lines and len(commented) == 1, f"readded/new: interpolation: {lines} {commented}")
		else:
			check(values["display.shadow_resolution"] == "128" and values["display.anti_aliasing"] == "off",
				"readded/keep: the old values were lost")
		lines, _ = value_lines(text, "high_res_hud")
		check(lines == ["high_res_hud = false"], f"readded/{answer}: high_res_hud: {lines}")
		# (nothing more to do the second time)
		check(pending(case)[0] == 0, f"readded/{answer}: asked again")
		before = case.text()
		case.get("display.vsync")
		check(case.text() == before, f"readded/{answer}: the file changed again on the next start")


def test_before_config_version(program, directory, defaults, kind):
	"""A file from before config_version: every setting a value. Those at
	this version's default are commented out; an earlier default is kept,
	being possibly the player's choice (existing players keep their
	settings: the new defaults are for new files)."""
	case = Case(program, directory / "version1")
	case.get("display.vsync")
	text = case.text()
	# every commented-out setting as a value, the way version 1 wrote them,
	# at the old defaults for those this version changed
	old = {"shadow_resolution": "128", "anti_aliasing": "\"off\""}
	text = re.sub(r"^config_version = \d+\n", "", text, flags=re.M)
	text = text.replace("# The layout of this file, for the game: leave it.\n", "")

	def value(match):
		key = match.group(1)
		return f"{key} = {old.get(key, match.group(2))}"
	text = re.sub(r"^# ([a-z_0-9]+) = (.*)$", value, text, flags=re.M)
	text = text.replace("volume = 1.0", "volume = 0.5", 1)
	case.path.write_text(text)
	values, _ = case.get("display.shadow_resolution", "display.anti_aliasing", "audio.volume")
	check(values["display.shadow_resolution"] == "128", f"version 1: shadow_resolution {values['display.shadow_resolution']}")
	check(values["display.anti_aliasing"] == "off", f"version 1: anti_aliasing {values['display.anti_aliasing']}")
	check(values["audio.volume"] == "0.5", "version 1: the player's audio.volume was lost")
	text = case.text()
	check("config_version = 2" in text, "version 1: no config_version written")
	# (values the player may have chosen are theirs: nothing to ask about)
	check(pending(case)[0] == 0, "version 1: asked about values the file holds")
	lines, commented = value_lines(text, "vsync")
	check(not lines and len(commented) == 1, f"version 1: vsync, at its default, is not commented out: {lines} {commented}")


def test_old_browser_address(program, directory, defaults, kind):
	"""The community site's first address, written out as the default by an
	earlier version, follows to stats.chupathingy.org (the same server); any
	other address is the player's and is kept."""
	case = Case(program, directory / "browser")
	values, _ = case.get("network.browser_url")
	check(values["network.browser_url"] == "https://stats.chupathingy.org", f"browser_url default {values}")
	case.path.write_text('config_version = 2\n\n[network]\nbrowser_url = "https://halo.milenko.org"\n')
	values, _ = case.get("network.browser_url")
	check(values["network.browser_url"] == "https://stats.chupathingy.org", f"old browser_url not followed: {values}")
	check("halo.milenko.org" not in case.text(), "old browser_url still in the file")
	check("game list" not in pending(case)[1] and "milenko" not in pending(case)[1], pending(case)[1])
	case.path.write_text('config_version = 2\n\n[network]\nbrowser_url = "https://lists.example.org"\n')
	values, _ = case.get("network.browser_url")
	check(values["network.browser_url"] == "https://lists.example.org", "custom browser_url lost")
	check('browser_url = "https://lists.example.org"' in case.text(), "custom browser_url not kept in the file")


def main():
	parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
	arguments = parser.parse_args()
	failures = 0
	with tempfile.TemporaryDirectory() as directory:
		directory = Path(directory)
		for kind in DEFAULTS:
			program = build(arguments.cc, kind, directory / kind)
			for test in (test_new_file, test_previous_release_file, test_file_without_the_settings,
				test_readded_by_older_version, test_before_config_version, test_old_browser_address):
				try:
					test(program, directory / kind / "cases", DEFAULTS[kind], kind)
					print(f"ok   {kind}: {test.__name__}")
				except AssertionError as error:
					print(f"FAIL {kind}: {test.__name__}: {error}")
					failures += 1
	sys.exit(1 if failures else 0)


if __name__ == "__main__":
	main()
