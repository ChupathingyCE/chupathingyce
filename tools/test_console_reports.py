"""Runs the C tests of a console's reports of the games it joins
(port/console/reports/console_reports.c: port/console/tests/
console_reports_test.c) with the build machine's C compiler, then reads the
bodies it sent as the site would: JSON, the report's members, the
identity's, and no address or line tag anywhere."""
import json
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_console_reports(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    prelude = tmp_path / "prelude.h"
    prelude.write_text("void posix_random_bytes(void *buffer, unsigned long size);\n"
                       "void platform_log(const char *format, ...);\n")
    binary = tmp_path / "console_reports_test"
    monocypher = ROOT / "port" / "third_party" / "monocypher"
    qrcodegen = ROOT / "port" / "third_party" / "qrcodegen"
    link = ROOT / "port" / "console" / "link"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         "-D__HALO_LINUX_PLATFORM_H", "-D__HALO_LINUX_POSIX_H", "-include", str(prelude),
         "-I", str(ROOT), "-I", str(link), "-I", str(ROOT / "port" / "linux" / "src"), "-I", str(monocypher),
         "-I", str(qrcodegen),
         str(ROOT / "port" / "console" / "tests" / "console_reports_test.c"),
         str(ROOT / "port" / "console" / "reports" / "console_reports.c"),
         str(ROOT / "port" / "linux" / "src" / "game_report.c"), str(link / "console_link.c"),
         str(ROOT / "port" / "linux" / "src" / "p2p_crypto.c"), str(qrcodegen / "qrcodegen.c"),
         str(monocypher / "monocypher.c"), str(monocypher / "monocypher-ed25519.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary), str(tmp_path)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout

    keyed = json.loads((tmp_path / "key_report.json").read_text())
    signed = json.loads((tmp_path / "signed_report.json").read_text())
    for body in (keyed, signed):
        assert body["platform"] == "xbox"
        assert re.fullmatch(r"[0-9a-f]{64}", body["invite"])
        assert body["map"] and body["reporter"] == "Chief"
        assert len(body["players"]) == 3 and body["duration"] == 600
        # (the reporter's line in full, the others the scoreboard's numbers)
        assert "medals" in body["players"][0] and "shots_fired" in body["players"][0]
        for line in body["players"][1:]:
            assert set(line) == {"name", "team", "place", "score", "kills", "deaths"}
        assert body["players"][1]["name"] == 'Some"one'
        text = json.dumps(body)
        assert '"tag"' not in text and "address" not in text
        assert not re.search(r"\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b", text)
    assert re.fullmatch(r"[0-9a-f]{64}", keyed["key"]) and "console_key" not in keyed
    assert "key" not in signed
    assert re.fullmatch(r"[0-9a-f]{64}", signed["console_key"])
    assert re.fullmatch(r"[0-9a-f]{32}", signed["challenge"])
    assert re.fullmatch(r"[0-9a-f]{128}", signed["signature"])
    # (the signature the last member: the site checks the bytes before it)
    assert (tmp_path / "signed_report.json").read_text().endswith('"signature": "%s"}' % signed["signature"])


def test_console_reports_gates():
    """The console's report follows the desktop's rules: the opt-out and the
    joined invite before anything else, and the game's units built for it
    behind HALO_PORT_GAME_REPORTS (a console's HALO_CONSOLE_GAME_REPORTS)."""
    reports = (ROOT / "port/console/reports/console_reports.c").read_text()
    body = re.search(r"^int console_reports_joined_game\(.*?^\}", reports, re.M | re.S).group(0)
    # (the opt-out, then the invite, each before anything else is asked)
    opt_out = body.index('!config_boolean("network.report_joined_games")')
    invite = body.index("!p2p_joined_invite(")
    assert opt_out < invite < body.index("identity()")
    # (no tag function: a client's report tags no line)
    assert re.search(r"game_report_json\(prefix,.*?NULL,\s*NULL\);", body, re.S)
    features = (ROOT / "port/linux/include/halo_port_features.h").read_text()
    assert "defined(HALO_GAME_BROWSER) || defined(HALO_CONSOLE_GAME_REPORTS)" in features
    stats = (ROOT / "port/linux/game/game_stats.c").read_text()
    assert "#ifdef HALO_PORT_GAME_REPORTS" in stats
    config = (ROOT / "port/linux/src/port_config.c").read_text()
    gate = config.index("#if defined(HALO_GAME_BROWSER) || defined(HALO_CONSOLE_GAME_REPORTS)")
    assert config.index('"network.report_joined_games"') > gate
