"""Every question the Guide's Workshop asks leaves a way out, and a press by
mistake answers it with no (port/linux/game/guide_workshop.c).

UNLINK STEAM had one button, UNLINK, with the focus: a click on SETTINGS'
STEAM ACCOUNT row by mistake led to it, and a mouse had no way back but
the Escape key. REMOVE turned into CONFIRM where it stood, so a second
click deleted the item's files. Every question now opens in the Guide's
one question window, CANCEL on the left with the focus; B, CANCEL, a
right-click or a click beside it leave things as they were.

These read the source: guide_workshop.c draws with the overlay and asks the
Workshop list, the Steam bridge and the menus (cseries, input, the virtual
keyboard, ~50 functions), so it can't be built alone as guide_layout.c is;
the layout and text rules it uses are tested by running them
(port/linux/tests/guide_layout_test.c)."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKSHOP = (ROOT / "port" / "linux" / "game" / "guide_workshop.c").read_text()
GUIDE = (ROOT / "port" / "linux" / "game" / "guide.c").read_text()
BROWSER = (ROOT / "port" / "linux" / "src" / "workshop_browser.c").read_text()


def body(text, signature):
    """a C function's body, from its signature to its closing brace"""
    start = text.index(signature)
    match = re.search(r"^\}\n", text[start:], re.M)
    assert match, signature
    return text[start:start + match.end()]


def test_unlink_opens_on_cancel():
    press = body(WORKSHOP, "static void link_settings_press(void)")
    assert "steam_link_set_armed(1);" in press
    assert press.index("workshop_view.modal_option = 0;") > press.index("steam_link_set_armed(1);")
    answer = body(WORKSHOP, "void guide_workshop_link_modal_press(void)")
    # (CANCEL, on the left, focused: nothing unlinked)
    unlink = answer[answer.index("if (unlink_asked())"):]
    assert unlink.index("if (workshop_view.modal_option)") < unlink.index("steam_link_unlink();")


def test_questions_have_cancel_and_click_outside():
    """one window for the Guide's questions: CANCEL on the left with the
    focus, the yes on the right; a click beside it, B or a right-click are
    CANCEL"""
    render = body(WORKSHOP, "void guide_workshop_link_modal_render(")
    assert 'modal_button(layout.cancel, "CANCEL", !workshop_view.modal_option, 0, target);' in render
    assert "modal_button(layout.button, yes, workshop_view.modal_option != 0, 1, target);" in render
    assert '"Unlink Steam?"' in render and '"Remove %s?"' in render
    outside = body(WORKSHOP, "void guide_workshop_link_modal_click_outside(short x, short y)")
    assert "guide_workshop_link_modal_back();" in outside and "steam_link_unlink" not in outside
    back = body(WORKSHOP, "void guide_workshop_link_modal_back(void)")
    assert "confirm_close();" in back and "workshop_browser_remove" not in back
    assert "guide_workshop_link_modal_click_outside(pointer->click_x, pointer->click_y);" in GUIDE
    assert "guide_workshop_link_modal_click(target->index);" in GUIDE


def test_remove_asks_in_the_window():
    """REMOVE never deletes on its own press: it opens the question, CANCEL
    focused; only the window's REMOVE deletes"""
    run = body(WORKSHOP, "static void option_run(short option)")
    remove = run[run.index("case _option_remove:"):run.index("case _option_open_steam:")]
    assert "workshop_view.confirm = _confirm_remove;" in remove
    assert "workshop_view.modal_option = 0;" in remove
    assert "workshop_browser_remove" not in remove
    press = body(WORKSHOP, "void guide_workshop_link_modal_press(void)")
    assert "workshop_view.modal_option && view != NONE && workshop_browser_remove(view)" in press
    assert '"CONFIRM"' not in WORKSHOP and '"DELETE"' not in WORKSHOP


def test_remove_deletes_own_copy_before_steam():
    """REMOVE deletes the game's own copy first: a player also subscribed
    in a running Steam client keeps that subscription, and the game's copy
    never stays behind looking installed. The bridge unsubscribes only
    behind the old paths, as workshop_browser_removable says."""
    remove = body(BROWSER, "int workshop_browser_remove(int index)")
    own = remove.index("if (workshop_browser_in_own_library(index))")
    assert own < remove.index("steam_bridge_platform_remove_folder(path)") < remove.index("steam_bridge_remove(item->id)")
    bridge = remove[remove.index("else if", own):]
    assert bridge.index("WORKSHOP_OLD_INSTALL_PATHS_DEBUG_ONLY") < bridge.index("steam_bridge_remove(item->id)")
    removable = body(BROWSER, "int workshop_browser_removable(int index)")
    assert "!WORKSHOP_OLD_INSTALL_PATHS_DEBUG_ONLY && bridge_ready()" in removable


def test_buttons_follow_where_the_item_stands():
    """the page's fixed buttons (workshop-flow.md 1.4): GET, CANCEL, RETRY,
    STORE, PLAY with REMOVE (UPDATE between them)"""
    options = body(WORKSHOP, "static short options_of(")
    for state, option in (("_page_not_installed", "_option_get"), ("_page_getting", "_option_cancel"),
            ("_page_failed", "_option_retry"), ("_page_needs_mcc", "_option_store")):
        case = options[options.index("case " + state + ":"):]
        assert case.index("options[count++] = " + option + ";") < case.index("break;"), state
    assert "options[count++] = _option_remove;" in options
    assert "WORKSHOP_STEAM_ITEMS_OPEN_IN_STEAM" in options


def test_steam_maps_row_is_the_switch():
    """an item in Steam's own folders while STEAM'S MCC MAPS is off: the row,
    drawn as a setting, in PLAY's place, turns them on (and off)"""
    assert 'case _option_steam_maps: return "STEAM\'S MCC MAPS";' in WORKSHOP
    assert 'steam_maps_on() ? "ON" : "OFF", on);' in WORKSHOP
    run = body(WORKSHOP, "static void option_run(short option)")
    assert 'config_write_boolean("game.workshop_maps", !steam_maps_on());' in run
    assert '"TURN ON"' not in WORKSHOP


def test_list_failure_has_one_retry():
    heading = body(WORKSHOP, "static void heading_render(")
    assert '"RETRY"' not in heading
