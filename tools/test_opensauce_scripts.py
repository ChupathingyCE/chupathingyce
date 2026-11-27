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


def test_weapon_index_set_is_bounded():
    # (a crafted map's script must not set an index the game reads its
    # weapons array by past that array: units.c's unit_get_weapon)
    text = source()
    assert ("if (short_field == &unit->unit.current_weapon_index &&\n"
            "\t\t\t(*value < 0 || *value >= MAXIMUM_WEAPONS_PER_UNIT))") in text


def test_clients_never_run_map_scripts():
    game = (ROOT / "source/game/game.c").read_text(encoding="latin-1")
    assert "if (!(network_game_distributed_client() && network_coop_active()))\n\t\ths_update();" in game


def test_sets_take_only_numbers_and_writable_fields():
    """a script's real is set only when it is a number, and never into a field
    OpenSauce keeps read-only (function outputs, radius, scale, center, the
    weapon index); a gravity scale only from 0 to 10"""
    text = source()
    assert "return value == value && value > -1.0e30f && value < 1.0e30f;" in text
    for store in ("*field = opensauce_real_argument(arguments[3]);", "*field = opensauce_real_argument(arguments[4]);",
                  "player->speed_multiplier = opensauce_real_argument(arguments[2]);"):
        before = text[:text.index(store)]
        assert "opensauce_real_settable" in before[-260:], store
    assert ('*readonly = !strcmp(name, "function_out") || !strcmp(name, "radius") || !strcmp(name, "scale") ||\n'
            '\t\t!strcmp(name, "center");') in text
    assert "short_field = &unit->unit.current_weapon_index;\n" in text
    assert "scale < 0.f || scale > OPENSAUCE_MAXIMUM_GRAVITY_SCALE" in text


def test_scripted_widgets_open_only_listed_definitions():
    """display_scripted_ui_widget opens only a ui_widget_definition the map's
    project_yellow (then project_yellow_globals) lists under the name, its
    list read within the map's tags; none on the dedicated server"""
    text = source()
    call = text[text.index('else if (!strcmp(name, "display_scripted_ui_widget"))'):]
    call = call[:call.index('else if (!strcmp(name, "physics_set_gravity"))')]
    assert "opensauce_map_scripted_widget(widget_name)" in call
    assert "ui_widget_load_by_name_or_tag(NULL, tag_index, NULL, NONE, NONE, NONE, NONE)" in call
    assert "if (tag_index != NONE)" in call
    assert text[:text.index(call)].rstrip().endswith("#ifndef HALO_SERVER")
    lookup = (ROOT / "port/linux/game/opensauce_map.c").read_text(encoding="utf-8")
    body = lookup[lookup.index("static long opensauce_scripted_widget_in("):]
    body = body[:body.index("\n}\n")]
    assert "ce_loaded_pointer(content_u32(root + offset + 4), (unsigned long)count * SCRIPTED_WIDGET_SIZE)" in body
    assert "'DeLa'" in body and "UI_WIDGET_DEFINITION_SIZE" in body
    find = lookup[lookup.index("long opensauce_map_scripted_widget("):]
    find = find[:find.index("\n}\n")]
    assert find.index("YELO_SCRIPTED_WIDGETS_OFFSET") < find.index("GELO_SCRIPTED_WIDGETS_OFFSET")
    assert "opensauce_globals.tag_instances = NULL;" in lookup
