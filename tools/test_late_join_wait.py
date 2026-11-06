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
