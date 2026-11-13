"""The split-screen late-join wait is a host setting (network.late_join_wait),
5 seconds by default, 0 to 30, 0 for no wait."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANAGER = ROOT / "source/networking/network_server_manager.c"


def test_setting_registered_and_documented():
    config = (ROOT / "port/linux/src/port_config.c").read_text(encoding="utf-8")
    assert re.search(r'\{ "network\.late_join_wait", _config_integer, "5", "HALO_NET_LATE_JOIN_WAIT"', config)
    assert "`network.late_join_wait` | `5` | `HALO_NET_LATE_JOIN_WAIT` |" in \
        (ROOT / "port/linux/README.md").read_text(encoding="utf-8")
    assert "`network.late_join_wait`" in (ROOT / "docs/settings.md").read_text(encoding="utf-8")
    assert "`HALO_NET_LATE_JOIN_WAIT`" in (ROOT / "server/docs/settings.md").read_text(encoding="utf-8")


def test_host_reads_it_cheaply_and_clamps():
    source = MANAGER.read_text(encoding="utf-8")
    assert "LATE_JOINER_ADD_PLAYERS_MILLISECONDS" not in source
    assert 'config_integer("network.late_join_wait")' in source
    assert "read_at != config_changes()" in source
    assert "seconds < 0 ? 0 : seconds > 30 ? 30 : seconds" in source
    # (0 starts the machine at once)
    assert "wait_milliseconds > 0 &&" in source


def test_waiting_late_joiner_sees_its_players_and_picks_a_team():
    """X#83: a machine waiting to join the game in progress is sent its players,
    and its players' team picks reach the host's team rule."""
    source = MANAGER.read_text(encoding="utf-8")
    handler = (ROOT / "source/networking/network_server_message_handler.c").read_text(encoding="utf-8")
    team_change = (ROOT / "port/linux/game/team_change.c").read_text(encoding="utf-8")
    assert "network_game_server_show_late_joiner_players(server, client_machine);" in source
    assert "boolean network_game_server_late_joiner_player_settings(" in source
    assert "network_game_server_late_joiner_player_settings(server, client_machine, &player)" in handler
    assert "long team_change_late_joiner(long list_index, short team)" in team_change


def test_waiting_late_joiner_picks_are_rate_limited():
    """A waiting machine gets one team pick every few seconds and at most one
    answer (its players again) a second, however often it asks; its start
    clears the queued answer, and a new machine in its slot starts fresh."""
    source = MANAGER.read_text(encoding="utf-8")
    assert "LATE_JOINER_PICK_INTERVAL = 3 * MILLISECONDS_PER_SECOND," in source
    assert "LATE_JOINER_REPLY_INTERVAL = MILLISECONDS_PER_SECOND," in source
    body = source[source.index("boolean network_game_server_late_joiner_player_settings("):]
    body = body[:body.index("\n}\n")]
    # (the cooldown is checked before the team rule, and only a taken pick starts it)
    assert body.index("LATE_JOINER_PICK_INTERVAL") < body.index("team_change_late_joiner(")
    assert "network_game_server_late_joiner_pick_times[slot] = now ? now : 1;" in body
    # (the answer goes through the limiter, never straight out)
    assert "network_game_server_show_late_joiner_players" not in body
    assert "network_game_server_late_joiner_reply_due[slot] = TRUE;" in body
    replies = source[source.index("static void network_game_server_late_joiner_replies(\n\tstruct network_game_server *server)\n{"):]
    replies = replies[:replies.index("\n}\n")]
    assert "LATE_JOINER_REPLY_INTERVAL" in replies
    assert "network_game_server_late_joiner_not_started(server, machine)" in replies
    # (called each frame, and reset when a machine joins)
    assert "\tnetwork_game_server_late_joiner_replies(server);\n" in source
    assert "network_game_server_late_joiner_pick_times[machine_index] = 0;" in source


def test_late_join_uses_only_existing_messages():
    """OpenCE compatibility: the waiting late joiner's picks ride the existing
    player settings request, and the answer is the existing settings record."""
    handler = (ROOT / "source/networking/network_server_message_handler.c").read_text(encoding="utf-8")
    start = handler.index("network_game_server_late_joiner_player_settings(server, client_machine, &player)")
    block = handler[handler.rindex("port: a machine joining the game in progress, in its pregame", 0, start):start]
    assert "short packet_type = _message_client_player_settings_request;" in block
    assert "_network_game_packet_class_client_pregame" in block


def test_hold_is_its_own_setting():
    """the host's option to hold late joiners until the next game is its own
    setting (network.late_join_hold, a boolean), never a second
    network.late_join_wait: config_setting_index finds the first entry of a
    name, so a second one would read the split-screen wait as a boolean (0)
    and every host would lose it"""
    config = (ROOT / "port/linux/src/port_config.c").read_text(encoding="utf-8")
    assert len(re.findall(r'\{ "network\.late_join_wait",', config)) == 1
    assert re.search(r'\{ "network\.late_join_hold", _config_boolean, "false", "HALO_NET_LATE_JOIN_HOLD"', config)
    rules = (ROOT / "port/linux/game/host_rules.c").read_text(encoding="utf-8")
    assert 'config_boolean("network.late_join_hold")' in rules
    assert "network.late_join_wait" not in rules
    assert "`network.late_join_hold`" in (ROOT / "docs/settings.md").read_text(encoding="utf-8")
    assert "`network.late_join_hold` | `false` | `HALO_NET_LATE_JOIN_HOLD` |" in \
        (ROOT / "port/linux/README.md").read_text(encoding="utf-8")
    server = (ROOT / "server/src/server_config.c").read_text(encoding="utf-8")
    assert '{ "late_join_hold", SERVER_FIELD_BOOLEAN, 0, 1, "HALO_DEDICATED_LATE_JOIN_HOLD",' in server
    assert '"late_join_wait"' not in server
