"""ONLINE CO-OP's Select Map lists the campaign's levels alone, and only while
it is up (#1736): once that list closes, the lobby's and the menus' map
lookups have the maps again, with no scan of the map folders each call and
no map refused."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure  # noqa: E402

CASES = ["closed", "closed-choose", "list-alone", "back-from-game-setup", "other-select-map"]

# a fault and the cases that must catch it
NEGATIVE_CONTROLS = {
    # (the co-op view left on once its list is deleted, as #1742 first had it)
    "delete-keeps-view": (("ui_widget_delete", "ui_widget_port_level_list_deleted(widget);", ";"),
                          ("closed", "closed-choose")),
    "deleted-keeps-view": (("ui_widget_port_level_list_deleted", "ui_map_list_coop_only(FALSE);", ";"),
                           ("list-alone",)),
    # (the co-op list only the first time it opens: GAME SETUP's B had the maps again)
    "first-open-only": (("multiplayer_level_list_initialize", "coop_level_list = coop ? widget : NULL;",
                         "coop_level_list = coop ? widget : NULL; online_games.coop = FALSE;"),
                        ("back-from-game-setup",)),
    # (every Select Map over the Multiplayer menu a co-op one)
    "any-item": (("ui_widget_port_map_select_over_coop",
                  "&&\n\t\tunder->data.focused_child_index == COOP_ITEM_POSITION", ""), ("other-select-map",)),
}

MAP_LIST = ("view_lower", "view_contains", "view_item_entry", "view_matches", "view_compare", "view_campaign_row",
            "view_apply", "view_refresh", "ui_map_list_coop_only", "ui_map_list_count",
            "ui_map_list_count_with_campaign", "ui_map_list_row_is_campaign", "ui_map_list_count_all",
            "ui_map_list_names", "ui_map_list_find")
HANDLERS = ("ui_map_list_lookup", "ui_widget_port_multiplayer_maps", "ui_widget_port_multiplayer_map_choose",
            "ui_widget_port_level_list_deleted", "multiplayer_level_list_initialize")
WIDGETS = ("ui_widget_port_map_select_over_coop", "ui_widget_delete", "ui_widget_delete_children_recursive")


def generated(fault=None):
    map_list = read("port/linux/game/ui_map_list.c")
    handlers = read("source/interface/ui_widget_event_handler_functions.c")
    widgets = read("source/interface/ui_widget.c")
    functions = {name: function(map_list, name) for name in MAP_LIST}
    functions.update({name: function(handlers, name) for name in HANDLERS})
    functions.update({name: function(widgets, name) for name in WIDGETS})
    if fault:
        name, before, after = NEGATIVE_CONTROLS[fault][0]
        functions[name] = mutated(functions[name], before, after)
    coop_item = re.search(r"\bCOOP_ITEM_POSITION = ([^,\n]+),", widgets).group(1)
    code = [
        f"#define ONLINE_GAMES_POSITION {constant(widgets, 'ONLINE_GAMES_POSITION')}",
        f"#define COOP_ITEM_POSITION ({coop_item})",
        re.search(r"^static struct widget_instance \*coop_level_list;$", handlers, re.M).group(0),
    ]
    code += [functions[name] for name in MAP_LIST + HANDLERS + WIDGETS]
    structures = "\n\n".join(structure(widgets, name) for name in ("widget_stack_data", "widget_stack_node"))
    return (("under_test_structures.inc", structures), ("under_test.inc", "\n\n".join(code)))


@pytest.mark.parametrize("case", CASES)
def test_coop_map_list(case):
    status, output = run(build("coop_map_list", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault, case", [(fault, case) for fault in sorted(NEGATIVE_CONTROLS)
                                         for case in NEGATIVE_CONTROLS[fault][1]])
def test_negative_control(fault, case):
    status, output = run(build("coop_map_list", generated(fault)), case)
    assert status == CHECK_FAILED, output
