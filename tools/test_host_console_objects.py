"""The host's console objects in a network game (network.host_console_objects,
cheats.c): which creations are counted, where the quota is asked, the
original Xbox's tighter cap and index limit, the reset each map, and the
setting."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHEATS_C = ROOT / "source/game/cheats.c"
HS_EXTERNAL_C = ROOT / "source/hs/hs_library_external.c"
CONFIG_C = ROOT / "port/linux/src/port_config.c"
DELTA_PEER_GAME_C = ROOT / "port/linux/src/delta_peer_game.c"


def _body(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}\n", start)]


def test_counted_only_from_the_hosts_console():
    text = CHEATS_C.read_text(encoding="utf-8")
    counted = _body(text, "static boolean cheats_console_objects_counted(")
    assert "terminal_command_running" in counted
    assert "global_network_game_server_get() != NULL" in counted
    assert "!network_game_distributed_client()" in counted


def test_quota_and_xbox_limits():
    text = CHEATS_C.read_text(encoding="utf-8")
    assert re.search(r"CONSOLE_OBJECTS_DEFAULT = 32,", text)
    assert re.search(r"CONSOLE_OBJECTS_WITH_XBOX = 8,", text)
    assert re.search(r"CONSOLE_OBJECTS_XBOX_INDEX_LIMIT = 1024", text)
    allowed = _body(text, "boolean cheats_console_object_allowed(")
    assert 'config_integer("network.host_console_objects")' in allowed
    assert "cheats_console_objects_made >= limit" in allowed
    made = _body(text, "boolean cheats_console_object_made(")
    assert "DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index) >= CONSOLE_OBJECTS_XBOX_INDEX_LIMIT" in made
    assert "object_delete(object_index);" in made
    reset = _body(text, "void cheats_initialize_for_new_map(")
    assert "cheats_console_objects_made = 0;" in reset


def test_every_console_path_asks():
    cheats = CHEATS_C.read_text(encoding="utf-8")
    objects = _body(cheats, "static void cheat_objects(")
    assert "if (!cheats_console_object_allowed())\n\t\t\t\t\tbreak;" in objects
    assert "cheats_console_object_made(object_new(&data));" in objects
    assert "\t\t\t\tobject_new(&data);" not in objects
    create = _body(HS_EXTERNAL_C.read_text(encoding="utf-8"), "void hs_object_create(\n\tshort object_name_index)\n{")
    assert "else if (cheats_console_object_allowed())\n\t\t\tcheats_console_object_made(object_new_by_name(" in create


def test_setting_and_xbox_check():
    config = CONFIG_C.read_text(encoding="utf-8")
    assert re.search(r'\{ "network.host_console_objects", _config_integer, "32", "HALO_NET_HOST_CONSOLE_OBJECTS"',
                     config)
    xbox = _body(DELTA_PEER_GAME_C.read_text(encoding="utf-8"), "int delta_peer_game_hosting_xbox(void)")
    assert "summary.machines[_delta_platform_xbox] > 0" in xbox
