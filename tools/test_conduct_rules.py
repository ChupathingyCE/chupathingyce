"""Tests of a dedicated server's betrayal and AFK limits: the C tests of
their rules (port/linux/game/conduct_rules.c, port/linux/tests/
conduct_rules_test.c), built with the build machine's C compiler, and what
the sources must keep that the C tests cannot reach: both limits off unless
set, settings with their environment variables, a game in progress alone,
moderators never kicked for AFK, every warning, kick and ban in the audit
file, nobody's address logged, and nothing of the game's wire."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "port" / "linux" / "game"
RULES_C = GAME / "conduct_rules.c"
CONDUCT_C = ROOT / "server" / "src" / "server_conduct.c"
CONFIG_C = ROOT / "server" / "src" / "server_config.c"
DEDICATED_C = ROOT / "server" / "src" / "dedicated.c"
DISTRIBUTED_C = GAME / "network_distributed.c"
SETTINGS_MD = ROOT / "server" / "docs" / "settings.md"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def function(source, name):
    """a function's text, from its name to the next function's"""
    start = source.index("\n" + name + "(\n")
    end = re.search(r"\n\}\n", source[start:])
    return source[start:start + end.end()]


def test_rules(tmp_path):
    """the rules' C tests, warnings as errors"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "conduct_rules_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/conduct_rules_test.c"), str(RULES_C), "-o", str(binary), "-lm"],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "conduct_rules_test: ok" in result.stdout


def test_settings_off_by_default():
    """betrayal_kick and afk_kick_minutes are settings with their
    environment variables, 0 (off) unless set, and documented"""
    config = CONFIG_C.read_text(encoding="utf-8")
    assert '{ "betrayal_kick", SERVER_FIELD_INTEGER, 0, SERVER_SETTINGS_MAXIMUM_BETRAYAL_KICK, ' \
        '"HALO_DEDICATED_BETRAYAL_KICK",' in config
    assert '{ "afk_kick_minutes", SERVER_FIELD_INTEGER, 0, SERVER_SETTINGS_MAXIMUM_AFK_KICK_MINUTES,' in config
    dedicated = DEDICATED_C.read_text(encoding="utf-8")
    assert 'setting_number("HALO_DEDICATED_BETRAYAL_KICK", &settings, SERVER_SETTING_BETRAYAL_KICK, 0)' in dedicated
    assert 'setting_number("HALO_DEDICATED_AFK_KICK_MINUTES", &settings, SERVER_SETTING_AFK_KICK_MINUTES, 0)' \
        in dedicated
    settings = SETTINGS_MD.read_text(encoding="utf-8")
    assert "| `HALO_DEDICATED_BETRAYAL_KICK` | `0` |" in settings
    assert "| `HALO_DEDICATED_AFK_KICK_MINUTES` | `0` |" in settings


def test_game_in_progress_alone():
    """nothing is looked at, and every count starts over, outside a game in
    progress (the lobby, loading, the carnage report)"""
    update = function(CONDUCT_C.read_text(encoding="utf-8"), "void server_conduct_update")
    assert "status.state != _dedicated_state_in_game" in update
    assert "!game_in_progress()" in update
    assert update.count("forget_players();") >= 3


def test_moderators_and_input():
    """AFK spares moderators and players whose input never reached the host
    this game; the host notes each player's input as it changes"""
    source = CONDUCT_C.read_text(encoding="utf-8")
    update = function(source, "void server_conduct_update")
    assert "if (!input_time || (!ce_player && machine_moderator(machine_index)))" in update
    distributed = DISTRIBUTED_C.read_text(encoding="utf-8")
    assert "distributed_note_input_activity(input->player_index, &action);" in \
        function(distributed, "static void distributed_handle_inputs")
    assert "csmemset(distributed_input_activity, 0, sizeof(distributed_input_activity));" in \
        function(distributed, "void network_distributed_new_game")


def test_custom_edition_players():
    """a Custom Edition player (one of the server machine's own players) is
    warned in their chat, timed by their client's actions and kicked or
    banned alone: never the server's machine"""
    source = CONDUCT_C.read_text(encoding="utf-8")
    kick = function(source, "static boolean kick")
    assert kick.count("if (ce_player)\n") == 3
    assert kick.count("(void)server_ce_kick(machine_index, controller_index, NULL);") == 3
    assert "network_distributed_ban_until(ce_player ? NONE : machine_index, address," in kick
    assert "server_ce_tell(" in function(source, "static boolean conduct_tell")
    assert "server_ce_input_time(" in function(source, "void server_conduct_update")


def test_audited_and_no_address():
    """every warning, kick and ban is in the audit file; no address is
    logged, told or audited"""
    source = CONDUCT_C.read_text(encoding="utf-8")
    for action in ('"sv_warn"', '"sv_kick"', '"sv_ban"'):
        assert re.search(r'server_audit\("server", (BETRAYAL|AFK)_ACTOR, SERVER_ROLE_OWNER, ' + action, source), action
    assert source.count('"sv_warn"') == 2 and source.count('"sv_kick"') == 2
    for call in re.findall(r"\n\t+(?:shown = )?(?:error|server_audit|tell)\(.*?\);\n", source, re.S):
        assert "address" not in call and "key" not in call, call
    # (a second automatic kick decided once, when the kick is: a retried drop
    # is never taken for a second)
    assert source.count("conduct_note_automatic_kick(") == 1
    assert "CONDUCT_BAN_SECONDS" in function(source, "static boolean kick")


def test_no_wire_change():
    """the limits use the host's messages and notices as they are"""
    for path in (RULES_C, CONDUCT_C, GAME / "conduct_rules.h", ROOT / "server" / "src" / "server_conduct.h"):
        source = path.read_text(encoding="utf-8")
        assert "_distributed_message_" not in source
        assert "distributed_send" not in source
