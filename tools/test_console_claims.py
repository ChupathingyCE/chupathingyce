"""Runs the C tests of a console's claim of its own line in a game
(port/console/reports/console_claims.c: port/console/tests/
console_claims_test.c) with the build machine's C compiler, then reads the
claims it sent as the site would (puma's console_link.clean_claim): JSON,
the fields, one identity, no address. Also checks the game's side is built
for the consoles alone, after the joined game's report."""
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


def test_console_claims(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    prelude = tmp_path / "prelude.h"
    prelude.write_text("void posix_random_bytes(void *buffer, unsigned long size);\n"
                       "void platform_log(const char *format, ...);\n")
    binary = tmp_path / "console_claims_test"
    monocypher = ROOT / "port" / "third_party" / "monocypher"
    qrcodegen = ROOT / "port" / "third_party" / "qrcodegen"
    link = ROOT / "port" / "console" / "link"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         "-D__HALO_LINUX_PLATFORM_H", "-D__HALO_LINUX_POSIX_H", "-include", str(prelude),
         "-I", str(ROOT), "-I", str(link), "-I", str(ROOT / "port" / "linux" / "src"), "-I", str(monocypher),
         "-I", str(qrcodegen),
         str(ROOT / "port" / "console" / "tests" / "console_claims_test.c"),
         str(ROOT / "port" / "console" / "reports" / "console_claims.c"),
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

    keyed = json.loads((tmp_path / "claim_key.json").read_text())
    player = json.loads((tmp_path / "claim_player.json").read_text())
    for body in (keyed, player):
        assert body["platform"] == "xbox" and body["role"] == "host" and body["invite"] == ""
        assert body["map"] == "bloodgulch"
        assert bytes.fromhex(body["host"]).decode() == "Xbox"
        assert bytes.fromhex(body["name"]).decode() == "Chief"
        assert (body["team"], body["score"], body["kills"], body["deaths"], body["duration"]) == (0, 12, 12, 3, 600)
        assert isinstance(body["ago"], int) and 0 <= body["ago"] <= 86400
        assert re.fullmatch(r"[0-9a-f]{32}", body["challenge"])
        assert re.fullmatch(r"[0-9a-f]{128}", body["signature"])
        text = json.dumps(body)
        assert "address" not in text and not re.search(r"\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b", text)
    assert re.fullmatch(r"[0-9a-f]{64}", keyed["console_key"]) and "player" not in keyed
    assert re.fullmatch(r"[0-9a-f]{32}", player["player"]) and "console_key" not in player


def test_console_claims_gates():
    """The claim is the consoles' that build it (HALO_CONSOLE_GAME_CLAIMS, no
    game list of their own), made once a game is over, after the joined
    game's report, on the main thread"""
    stats = (ROOT / "port" / "linux" / "game" / "game_stats.c").read_text()
    claim = re.search(r"^static void stats_console_claim\(.*?^\}", stats, re.M | re.S).group(0)
    start = stats.index("static void stats_console_claim(")
    gate = "#if !defined(HALO_GAME_BROWSER) && defined(HALO_CONSOLE_GAME_CLAIMS)"
    assert stats.rfind(gate, 0, start) > stats.rfind("#endif", 0, start)
    assert "local_player_index == 0" in claim
    update = re.search(r"^void game_stats_update\(.*?^\}", stats, re.M | re.S).group(0)
    assert update.index("stats_client_report();") < update.index("stats_console_claim();")
    claims = (ROOT / "port" / "console" / "reports" / "console_claims.c").read_text()
    ended = re.search(r"^int console_claims_game_ended\(.*?^\}", claims, re.M | re.S).group(0)
    # (the player's choice before anything, and no request on the main thread)
    assert ended.index('config_boolean("network.report_joined_games")') < ended.index("console_claims_port_start")
    assert "request(" not in ended and "malloc" not in claims
