"""Why a game can't be joined (port/linux/include/halo_join_reasons.h): one
set of sentences for the Online Games list, the PC Server Browser and a
failed join, the same as the site's game list says them (puma web/site.js
JOIN_REASONS). The header builds alone as plain C89, and the lists use it
rather than wording of their own."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "port" / "linux" / "include" / "halo_join_reasons.h"
USERS = [
    ROOT / "port" / "linux" / "game" / "browser_screen.c",
    ROOT / "port" / "linux" / "game" / "menu_functions.c",
    ROOT / "port" / "linux" / "game" / "server_browser.c",
    ROOT / "port" / "linux" / "game" / "map_families.c",
    ROOT / "source" / "networking" / "network_client_manager.c",
]

# the sentences, as the site has them too (keep both in step)
REASONS = {
    "NEEDS_UPDATE": "Needs a newer Chupathingy.",
    "HOST_OLDER": "The host is on an older version.",
    "OTHER_VERSION": "The host is on another version.",
    "MAP_MISSING": "Map not installed: %s.",
    "MAP_DOWNLOAD": "Map not installed: %s. Join to download it.",
    "MAP_FROM_HALO_PC": "Map not installed: %s. Copy it from Halo PC into maps_pc or maps_ce.",
    "MAPS_UNSUPPORTED": "%s maps don't work on this build.",
    "OPENCE_GAME_TYPE": "Uses an OpenCE game type Chupathingy doesn't have yet: %s.",
    "NEEDS_BOMB": "Needs a Chupathingy build with Bomb.",
    "NOT_OPEN": "Not taking players right now.",
    "FULL": "Game is full.",
    "PASSWORD": "Password needed.",
    "NO_ANSWER": "The host didn't answer.",
    # (a failed join's alone: the site's list never says it)
    "NO_CONNECTION": "Couldn't connect to the host.",
}

# the wording the lists had before, each its own way
OLD_WORDING = [
    "can't be joined.",
    "not accepting players",
    "not taking players",
    "did not answer.\"",
    "aren't supported in this build",
    "Needs %s/%s.map",
    "Needs maps_",
    "newer than yours",
    "older than yours",
]


def test_header_sentences():
    text = HEADER.read_text()
    found = dict(re.findall(r'#define JOIN_REASON_([A-Z_]+) "([^"]*)"', text))
    assert found == REASONS


# the neutral label of an OpenCE dedicated server, as the site says it too
# (puma web/site.js HOST_LABEL_OPENCE_SERVER)
OPENCE_SERVER_LABEL = "OpenCE dedicated"


def test_opence_server_label():
    text = HEADER.read_text()
    assert f'#define HOST_LABEL_OPENCE_SERVER "{OPENCE_SERVER_LABEL}"' in text
    # (both PC lists show it from the header, never words of their own)
    for path in (ROOT / "port" / "linux" / "game" / "browser_screen.c",
                 ROOT / "port" / "linux" / "game" / "menu_functions.c"):
        source = path.read_text()
        assert "HOST_LABEL_OPENCE_SERVER" in source, path
        assert f'"{OPENCE_SERVER_LABEL}"' not in source, path
        assert "OPENCE SERVER" not in source, path


def test_lists_use_the_header():
    for path in USERS:
        text = path.read_text()
        assert '#include "halo_join_reasons.h"' in text, path
        for old in OLD_WORDING:
            assert old not in text, (path, old)


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_header_builds_alone(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "join_reasons.c"
    source.write_text(
        '#include <stdio.h>\n#include <string.h>\n#include <wchar.h>\n#include "halo_join_reasons.h"\n'
        "int main(void)\n{\n"
        "\tint reason;\n\tconst wchar_t *wide = JOIN_WIDE(JOIN_REASON_FULL);\n"
        "\tfor (reason = _join_reason_none; reason < NUMBER_OF_JOIN_REASONS; reason++)\n"
        "\t\tprintf(\"%d=%s\\n\", reason, JOIN_REASON_TEXT(reason));\n"
        "\tprintf(\"older=%s\\n\", JOIN_REASON_TEXT(JOIN_REASON_OF_VERSION_STATE(-1)));\n"
        "\tprintf(\"newer=%s\\n\", JOIN_REASON_TEXT(JOIN_REASON_OF_VERSION_STATE(1)));\n"
        "\tprintf(\"wide=%d\\n\", (int)wcslen(wide));\n"
        "\treturn 0;\n}\n")
    binary = tmp_path / "join_reasons"
    # (the header alone: port/linux/include has its own stdio.h and others)
    shutil.copy(HEADER, tmp_path / HEADER.name)
    subprocess.run([cc, "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-I", str(tmp_path),
                    str(source), "-o", str(binary)], check=True)
    output = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
    lines = dict(line.split("=", 1) for line in output.splitlines())
    assert lines["older"] == REASONS["HOST_OLDER"]
    assert lines["newer"] == REASONS["NEEDS_UPDATE"]
    assert lines["wide"] == str(len(REASONS["FULL"]))
    # (every reason without a detail has its sentence)
    said = {value for key, value in lines.items() if key.isdigit() and value}
    assert said == {REASONS[name] for name in ("NEEDS_UPDATE", "HOST_OLDER", "OTHER_VERSION", "NEEDS_BOMB",
                                                "NOT_OPEN", "FULL", "PASSWORD", "NO_ANSWER",
                                                "NO_CONNECTION")}
