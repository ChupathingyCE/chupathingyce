"""Every question the Guide's Workshop asks leaves a way out, and a press by
mistake answers it with no (port/linux/game/guide_workshop.c).

UNLINK STEAM had one button, UNLINK, with the focus: a click on SETTINGS'
STEAM ACCOUNT row by mistake led to it, and a mouse had no way back but
the Escape key. REMOVE turned into CONFIRM where it stood, so a second
click deleted the item's files. Each question now has its yes and CANCEL,
the focus on CANCEL as it opens; B, CANCEL, another item or (UNLINK's) a
click beside its window leave things as they were.

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


def body(text, signature):
    """a C function's body, from its signature to its closing brace"""
    start = text.index(signature)
    match = re.search(r"^\}\n", text[start:], re.M)
    assert match, signature
    return text[start:start + match.end()]


def test_unlink_opens_on_cancel():
    press = body(WORKSHOP, "static void link_settings_press(void)")
    assert "steam_link_set_armed(1);" in press
    assert press.index("workshop_view.modal_option = 1;") > press.index("steam_link_set_armed(1);")
    answer = body(WORKSHOP, "void guide_workshop_link_modal_press(void)")
    # (CANCEL focused: nothing unlinked)
    assert answer.index("if (workshop_view.modal_option)") < answer.index("steam_link_unlink();")


def test_unlink_has_cancel_and_click_outside():
    render = body(WORKSHOP, "void guide_workshop_link_modal_render(")
    assert '"CANCEL"' in render and "GUIDE_WORKSHOP_TARGET_MODAL, 1" in render
    assert "Workshop downloads will need you to scan again." in render
    outside = body(WORKSHOP, "void guide_workshop_link_modal_click_outside(short x, short y)")
    assert "steam_link_set_armed(0);" in outside and "steam_link_unlink" not in outside
    assert "guide_workshop_link_modal_click_outside(pointer->click_x, pointer->click_y);" in GUIDE
    assert "guide_workshop_link_modal_click(target->index);" in GUIDE


def test_page_questions_put_cancel_where_the_press_was():
    """REMOVE (second of PLAY, REMOVE) and CONNECT STEAM (often the only
    button): CANCEL takes the asking button's place and the focus, so a
    second press or click there answers no"""
    options = body(WORKSHOP, "static short options_of(")
    assert "workshop_view.confirm_slot ? yes : _option_cancel" in options
    assert "workshop_view.confirm_slot ? _option_cancel : yes" in options
    run = body(WORKSHOP, "static void option_run(short option)")
    for arm in ("workshop_view.confirm_remove = TRUE;", "workshop_view.confirm = TRUE;"):
        start = run.index(arm)
        after = run[start:start + 200]
        assert "workshop_view.confirm_slot = workshop_view.option ? 1 : 0;" in after, arm
        assert "workshop_view.option = workshop_view.confirm_slot;" in after, arm
    assert "case _option_cancel:" in run
    assert '"CONNECT" : "CONNECT STEAM"' in WORKSHOP and '"CONFIRM"' not in WORKSHOP


def test_workshop_maps_row_is_the_switch():
    """the page's row, drawn as a setting, turns them on; SETTINGS' row turns
    them off again (port/linux/src/settings_rows.c)"""
    assert 'case _option_show_maps: return "WORKSHOP MAPS";' in WORKSHOP
    assert 'ui_style_setting(button.x, button.y, button.width, button.height, 0.9f, "WORKSHOP MAPS", NULL, "OFF", on);' \
        in WORKSHOP
    rows = (ROOT / "port" / "linux" / "src" / "settings_rows.c").read_text()
    assert '{ _in_guide, "WORKSHOP MAPS", "game.workshop_maps", _kind_toggle,' in rows
    assert '"TURN ON"' not in WORKSHOP
    assert 'return "Workshop maps are off"' not in body(WORKSHOP, "static char const *page_note(")


def test_list_failure_has_one_retry():
    heading = body(WORKSHOP, "static void heading_render(")
    assert '"RETRY"' not in heading

