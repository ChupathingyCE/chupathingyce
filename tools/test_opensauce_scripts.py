"""OpenSauce maps' script functions and globals (port/linux/game/opensauce_scripts.c):
there only on a map with an OpenSauce header, and host-authoritative, so no
client's simulation runs one of their changes on its own. CI has no
OpenSauce map, so these read the sources."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "port/linux/game/opensauce_scripts.c"


def source():
    return SCRIPTS.read_text(encoding="utf-8")


def table(text):
    return re.findall(r'OPENSAUCE_SCRIPT_FUNCTION\("(\w+)", (_hs_type_\w+), (\d+),\s*\{([^}]*)\}', text)


def test_only_on_opensauce_maps():
    text = source()
    assert "return opensauce_map_active() ? (long)NUMBEROF(opensauce_script_functions) : 0;" in text
    assert "return opensauce_map_active() ? (short)NUMBEROF(opensauce_script_globals) : 0;" in text


def test_table_is_well_formed():
    functions = table(source())
    names = [name for name, _, _, _ in functions]
    assert len(names) == len(set(names)) and len(names) >= 30
    for name, _, count, types in functions:
        given = [t for t in types.split(",") if t.strip() and t.strip() != "0"]
        assert int(count) == len(given) <= 7, name


def test_sets_are_host_authoritative():
    text = source()
    body = text[text.index("static boolean opensauce_script_may_set("):]
    body = body[:body.index("\n}\n")]
    # (a local game sets; a network client never; a co-op host does)
    assert "game_connection() == _game_connection_local" in body
    assert "if (network_game_distributed_client())\n\t\treturn FALSE;" in body
    assert "network_coop_active()" in body
    # every store into a field the scripts name is under it (within the
    # lines of its if)
    lines = text.splitlines()
    stores = [index for index, line in enumerate(lines)
              if re.search(r"^\s*\*(field|byte_field|short_field) = |speed_multiplier = ", line)]
    assert len(stores) >= 6
    for index in stores:
        guard = "\n".join(lines[max(0, index - 20):index])
        assert "opensauce_script_may_set" in guard, lines[index]
    assert "opensauce_script_may_set(FALSE))\n\t\t\topensauce_map_set_gravity_scale" in text


def test_clients_never_run_map_scripts():
    game = (ROOT / "source/game/game.c").read_text(encoding="latin-1")
    assert "if (!(network_game_distributed_client() && network_coop_active()))\n\t\ths_update();" in game
