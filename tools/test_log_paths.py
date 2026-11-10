"""The log's folder paths (port/linux/src/log_paths.c): the player's home
folder is written as ~ (%USERPROFILE% for a Windows one), wherever it is in a
line, unless debug.log_full_paths is on; what goes to the site
(crash_report_redact, a crash report's minidump) never has it.

log_paths.c and crash_report.c are built with the build machine's C
compiler beside stand-ins for the platform layer (its config, log and build
identity). The home folders come from the environment, as in the game: HOME
(and USERPROFILE, HOMEDRIVE + HOMEPATH on Windows)."""
import base64
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"

STUBS = {
    "port_config.h": "int config_boolean(const char *name);\nint config_loaded_yet(void);\n",
    "build_identity.h": "const char *build_identity_channel(void);\n",
    "log_address.h": "int log_address_local(const unsigned char *bytes, int length);\n",
}

MAIN = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log_paths.h"
#include "crash_report.h"

static int loaded, whole, said;

void platform_log(const char *format, ...) { (void)format; said++; }
int config_loaded_yet(void) { return loaded; }
int config_boolean(const char *name) { return !strcmp(name, "debug.log_full_paths") && whole; }
const char *build_identity_channel(void) { return "release"; }
int log_address_local(const unsigned char *bytes, int length)
{
	return length == 4 && (bytes[0] == 10 || bytes[0] == 127 || (bytes[0] == 192 && bytes[1] == 168));
}

/* reads lines from standard input; the first argument says what to do with
each: "line" (log_paths_redact_line), "redact" (log_paths_redact), "crash"
(crash_report_redact), "mask16"/"mask8" (log_paths_mask of the line as UTF-16
or 8-bit, printed back as 8-bit), "body" (crash_report_body with the line as
the log and as a UTF-16 minidump) */
int main(int count, char **arguments)
{
	char line[4096];
	const char *mode = count > 1 ? arguments[1] : "line";

	loaded = count > 2 && strcmp(arguments[2], "unloaded");
	whole = count > 2 && !strcmp(arguments[2], "whole");
	while (fgets(line, sizeof(line), stdin))
	{
		char out[3 * sizeof(line) + 1];
		size_t length;

		line[strcspn(line, "\n")] = 0;
		length = strlen(line);
		if (!strcmp(mode, "line"))
		{
			char small[2048];

			snprintf(small, sizeof(small), "%s", line);
			log_paths_redact_line(small, sizeof(small));
			printf("%s\n", small);
		}
		else if (!strcmp(mode, "redact"))
		{
			log_paths_redact(line, length, out, 3 * length + 1);
			printf("%s\n", out);
		}
		else if (!strcmp(mode, "crash"))
		{
			crash_report_redact(line, length, out, 3 * length + 1);
			printf("%s\n", out);
		}
		else if (!strcmp(mode, "mask16"))
		{
			unsigned char wide[2 * sizeof(line)];
			size_t index;

			for (index = 0; index < length; index++)
			{
				wide[2 * index] = (unsigned char)line[index];
				wide[2 * index + 1] = 0;
			}
			log_paths_mask(wide, 2 * length);
			for (index = 0; index < length; index++)
				out[index] = wide[2 * index + 1] ? '!' : (char)wide[2 * index];
			out[length] = 0;
			printf("%s\n", out);
		}
		else if (!strcmp(mode, "mask8"))
		{
			log_paths_mask(line, length);
			printf("%s\n", line);
		}
		else if (!strcmp(mode, "body"))
		{
			unsigned char wide[2 * sizeof(line)];
			size_t index, body_length;
			char *body;

			for (index = 0; index < length; index++)
			{
				wide[2 * index] = (unsigned char)line[index];
				wide[2 * index + 1] = 0;
			}
			body = crash_report_body("version 0.9.0d\nframe chupathingy+0x10\n", line, length, wide, 2 * length,
				&body_length);
			printf("%s\n", body);
			free(body);
		}
	}
	printf("said %d\n", said);
	return 0;
}
"""


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.fixture(scope="module")
def program(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    folder = tmp_path_factory.mktemp("log_paths")
    for name in ("log_paths.c", "log_paths.h", "crash_report.c", "crash_report.h"):
        shutil.copy(SOURCE / name, folder / name)
    for name, text in STUBS.items():
        (folder / name).write_text(text)
    (folder / "main.c").write_text(MAIN)
    out = folder / "log_paths_test"
    result = subprocess.run([cc, "-std=gnu99", "-Wall", "-Werror", "-o", str(out), str(folder / "main.c"),
                             str(folder / "log_paths.c"), str(folder / "crash_report.c"), "-lpthread"],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return out


def run(program, lines, *arguments, home=None, extra=None):
    environment = {key: value for key, value in os.environ.items()
                   if key not in ("HOME", "USERPROFILE", "HOMEDRIVE", "HOMEPATH")}
    if home is not None:
        environment["HOME"] = home
    environment.update(extra or {})
    result = subprocess.run([str(program), *arguments], input="\n".join(lines) + "\n", capture_output=True,
                            text=True, env=environment)
    assert result.returncode == 0, result.stderr
    return result.stdout.splitlines()


def test_mac_home_in_paths(program):
    lines = run(program, [
        "settings: /Users/bob/Library/Application Support/ChupathingyCE/config.toml",
        "saves /Users/bob/Library/Application Support/ChupathingyCE, logs /Users/bob/Library/Logs/x",
        "data root: /Users/bob",
        "maps folder \"/Users/bob/halo maps\" (missing).",
        "home is /Users/bob.",
    ], "line", "loaded", home="/Users/bob")
    assert lines[:5] == [
        "settings: ~/Library/Application Support/ChupathingyCE/config.toml",
        "saves ~/Library/Application Support/ChupathingyCE, logs ~/Library/Logs/x",
        "data root: ~",
        "maps folder \"~/halo maps\" (missing).",
        "home is ~.",
    ]
    assert lines[5] == "said 0"


def test_only_whole_folder_names(program):
    lines = run(program, [
        "/Users/bobby/Library",
        "/Users/bob.old/Library",
        "/Volumes/x/Users/bob/Library",
        "name/Users/bob",
        "/Users/bob_2",
    ], "line", "loaded", home="/Users/bob")
    assert lines[:5] == [
        "/Users/bobby/Library",
        "/Users/bob.old/Library",
        # (a home under another folder is not the home)
        "/Volumes/x/Users/bob/Library",
        "name/Users/bob",
        "/Users/bob_2",
    ]


def test_linux_home_with_trailing_separator(program):
    lines = run(program, ["log /home/milo/.local/share/chupathingyce/logs/debug.txt"], "line", "loaded",
                home="/home/milo/")
    assert lines[0] == "log ~/.local/share/chupathingyce/logs/debug.txt"


def test_linux_home_case(program):
    lines = run(program, ["/home/Milo/x"], "line", "loaded", home="/home/milo")
    if sys.platform == "darwin":
        # (macOS's file system ignores case: so does the match)
        assert lines[0] == "~/x"
    else:
        assert lines[0] == "/home/Milo/x"


def test_freebsd_and_android_homes(program):
    assert run(program, ["/usr/home/kim/halo/logs"], "line", "loaded", home="/usr/home/kim")[0] == "~/halo/logs"
    lines = run(program, ["/data/user/0/dev.horrible.chupathingyce/files/maps"], "line", "loaded",
                home="/data/user/0/dev.horrible.chupathingyce")
    assert lines[0] == "~/files/maps"


def test_windows_home_any_case_and_slash(program):
    lines = run(program, [
        "settings: C:\\Users\\Bob\\AppData\\Roaming\\ChupathingyCE\\config.toml",
        "saves c:/users/bob/AppData/Roaming/ChupathingyCE",
        "maps C:\\USERS\\BOB\\Documents\\halo",
        "folder 'C:\\Users\\Bob'",
        "C:\\Users\\Bobby\\x",
        "\\\\?\\C:\\Users\\Bob\\x",
    ], "line", "loaded", home="C:\\Users\\Bob")
    assert lines[:6] == [
        "settings: %USERPROFILE%\\AppData\\Roaming\\ChupathingyCE\\config.toml",
        "saves %USERPROFILE%/AppData/Roaming/ChupathingyCE",
        "maps %USERPROFILE%\\Documents\\halo",
        "folder '%USERPROFILE%'",
        "C:\\Users\\Bobby\\x",
        "\\\\?\\%USERPROFILE%\\x",
    ]


def test_no_home_changes_nothing(program):
    text = ["/Users/bob/Library", "C:\\Users\\Bob\\x", "/home/x"]
    assert run(program, text, "line", "loaded", home="/")[:3] == text
    assert run(program, text, "line", "loaded")[:3] == text


def test_full_paths_only_when_asked(program):
    line = "settings: /Users/bob/Library/config.toml"
    lines = run(program, [line, line], "line", "whole", home="/Users/bob")
    assert lines[:2] == [line, line]
    # (the log says once why it has them)
    assert lines[2] == "said 1"
    # (before config.toml is read, the paths are taken out)
    lines = run(program, [line], "line", "unloaded", home="/Users/bob")
    assert lines == ["settings: ~/Library/config.toml", "said 0"]


def test_site_always_redacted(program):
    lines = run(program, ["log /Users/bob/x from 203.0.113.9 and 192.168.1.4"], "crash", "whole",
                home="/Users/bob")
    assert lines[0] == "log ~/x from [address] and 192.168.1.4"
    lines = run(program, ["C:\\Users\\Al\\x"], "redact", "whole", home="C:\\Users\\Al")
    assert lines[0] == "%USERPROFILE%\\x"


def test_minidump_mask(program):
    lines = run(program, ["module C:\\Users\\Bob\\Desktop\\chupathingy.exe; c:/users/bob/x"], "mask16", "loaded",
                home="C:\\Users\\Bob")
    assert lines[0] == "module C:\\Users\\___\\Desktop\\chupathingy.exe; c:/users/___/x"
    lines = run(program, ["/Users/bob/Applications/Chupathingy.app"], "mask8", "loaded", home="/Users/bob")
    assert lines[0] == "/Users/___/Applications/Chupathingy.app"


def test_crash_report_body(program):
    lines = run(program, ["opened C:\\Users\\Bob\\AppData\\Roaming\\ChupathingyCE\\x.map"], "body", "whole",
                home="C:\\Users\\Bob")
    body = json.loads(lines[0])
    assert body["log"] == "opened %USERPROFILE%\\AppData\\Roaming\\ChupathingyCE\\x.map"
    dump = base64.b64decode(body["minidump"]).decode("utf-16-le")
    assert dump == "opened C:\\Users\\___\\AppData\\Roaming\\ChupathingyCE\\x.map"



def test_crash_report_renderer_from_log(program):
    """the report's renderer: the log's last renderer= line (d3d8_gl.c)"""
    body = json.loads(run(program, ["chupathingyce: renderer=gl then renderer=bgfx-vulkan, more"], "body")[0])
    assert body["renderer"] == "bgfx-vulkan"
    body = json.loads(run(program, ["no renderer here; frame_renderer=x"], "body")[0])
    assert "renderer" not in body

def test_every_log_writer_redacts():
    """platform_log and the game's error log (debug.txt) both go through
    log_paths_redact_line; the site's uploads through crash_report_redact."""
    kernel = (SOURCE / "xbox_kernel.c").read_text()
    body = kernel[kernel.index("void platform_log(const char *format, ...)\n{"):]
    body = body[:body.index("\n}\n")]
    assert body.index("log_paths_redact_line(buffer") < body.index("fputs(buffer, stderr)")
    assert body.index("log_paths_redact_line(buffer") < body.index("server_control_log(buffer)")
    errors = (ROOT / "source" / "cseries" / "errors.c").read_text()
    writer = errors[errors.index("void write_to_error_file("):]
    writer = writer[:writer.index("\n}\n")]
    assert writer.index("log_paths_redact_line(redacted") < writer.index("write_to_debug_file(string")
    launcher = (SOURCE / "launcher.c").read_text()
    assert "crash_report_redact(from" in launcher
    config = (SOURCE / "port_config.c").read_text()
    assert '{ "debug.log_full_paths", _config_boolean, "false", "HALO_LOG_FULL_PATHS"' in config
