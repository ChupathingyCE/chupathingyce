"""The menus' footer prompts show the device in use (#1147).

  - The name entry's picture (ui\\bkd_virtual_keyboard, source/interface/
    virtual_keyboard.c) has the Xbox's B and A drawn into it ("=BACK
    =ENTER"), which showed a controller's buttons with the keyboard in use,
    in every menu set. Where the menus' prompts are drawn (the keyboard's
    key caps, or the Chupathingy menus' glyphs for any device: the same
    rule as the footers' pictures, port/linux/game/menu_tags.c), the
    picture is drawn without them and the prompts are drawn in their place.
  - A key cap wider than the picture it stands for (ENTER's, for A) moves
    every piece before it on its line left by as much (#1144), the pieces
    in a group of their own too: the Xbox's profile list keeps BACK and
    SELECT in button_key, after the screen's "=DELETE", which BKSP then
    touched.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEYBOARD = (ROOT / "source" / "interface" / "virtual_keyboard.c").read_text(encoding="utf-8")
WIDGET = (ROOT / "source" / "interface" / "ui_widget.c").read_text(encoding="utf-8")
WIDGET_H = (ROOT / "source" / "interface" / "ui_widget.h").read_text(encoding="utf-8")
TAGS = (ROOT / "port" / "linux" / "game" / "menu_tags.c").read_text(encoding="utf-8")


def function(source, name):
    """a function's body: its definition, not a declaration"""
    match = re.search(r"^(?:static )?[a-z][^;\n]*\b" + name + r"\((?:[^;{]*?)\)\s*\n\{", source, re.M | re.S)
    assert match, name
    body = match.end() - 1
    return source[body:source.index("\n}\n", body)]


def test_name_entry_picture_drawn_without_its_buttons():
    render = function(KEYBOARD, "virtual_keyboard_render_internal")
    # (the background drawn whole only when the prompts don't stand in)
    assert re.search(r"if \(!virtual_keyboard_background_prompts\(bitmap\)\)\n#endif\n\t\tdraw_bitmap_in_rect\(",
                     render)
    prompts = function(KEYBOARD, "virtual_keyboard_background_prompts")
    # (its name as the C source writes it)
    assert '"ui\\\\bkd_virtual_keyboard"' in prompts
    # (only where the menus' prompts are drawn; asked before any drawing)
    asked = prompts.index("pc_menu_keyboard_prompt(NONE")
    assert asked < prompts.index("virtual_keyboard_background_piece(")
    # (B and A, each over its picture's place)
    assert "pc_menu_keyboard_prompt(which," in prompts
    assert "NUMBER_OF_VIRTUAL_KEYBOARD_PROMPTS = 2" in KEYBOARD


def test_name_entry_pieces_cover_the_picture():
    """the pieces tile the 640x480 picture: above and below the band whole,
    the band in pieces between and over the two pictures"""
    columns = re.search(r"virtual_keyboard_prompt_columns\[NUMBER_OF_VIRTUAL_KEYBOARD_PROMPTS\]\[4\] =\n\{\n"
                        r"(.*?)\n\};", KEYBOARD, re.S).group(1)
    rows = [tuple(int(value) for value in re.findall(r"\d+", line)) for line in columns.splitlines()
            if re.search(r"\{ \d", line)]
    assert len(rows) == 2
    x = 0
    for x0, x1, plain0, plain1 in rows:
        assert x < x0 < x1 <= 640
        # (the plain band is beside the picture, not in it)
        assert plain1 <= x0 and plain0 < plain1
        x = x1
    # (the pictures, B at 371..391 and A at 472..491 in the picture, are inside the pieces)
    assert rows[0][0] <= 371 and rows[0][1] >= 391
    assert rows[1][0] <= 472 and rows[1][1] >= 491
    band = {name: int(value) for name, value in re.findall(r"_virtual_keyboard_(band_y[01]) = (\d+)", KEYBOARD)}
    assert band["band_y0"] <= 414 and band["band_y1"] >= 440


def test_pieces_drawn_from_their_texels():
    """draw_bitmap_in_rect takes the texels' size, from the column and row
    draw_bitmap_texel_x and draw_bitmap_texel_y give: both are reset"""
    piece = function(KEYBOARD, "virtual_keyboard_background_piece")
    assert "draw_bitmap_texel_x = texel_x0;" in piece and "draw_bitmap_texel_y = y0;" in piece
    assert piece.rstrip().endswith("draw_bitmap_texel_y = 0;")
    assert "extern short draw_bitmap_texel_y;" in WIDGET_H
    assert "\nshort draw_bitmap_texel_y = 0;" in WIDGET


def test_keyboard_prompt_follows_the_footers_rule():
    prompt = function(TAGS, "pc_menu_keyboard_prompt")
    cap = function(TAGS, "pc_menu_key_cap_button")
    # (the footers' pictures: the keyboard's caps, or every device's in the Chupathingy menus)
    assert "!halo_keyboard_prompts() && !modern" in cap
    assert "if (!halo_keyboard_prompts() && !modern)\n\t\treturn FALSE;" in prompt
    assert "menu_tags.loaded && menu_tags.chupathingy" in prompt
    assert "ui_overlay_button(button, size, (float)x1 - width," in prompt


def test_key_cap_room_for_every_piece_before_it():
    shift = function(WIDGET, "ui_widget_port_key_cap_shift")
    overflow = function(WIDGET, "ui_widget_port_key_cap_overflow")
    # (no distance limit: "=DELETE" is further than 120 from ENTER)
    assert "> 120" not in shift and "> 120" not in overflow
    # (siblings and their children: button_key's pictures count for "=DELETE")
    assert "ui_widget_port_key_cap_overflow(sibling," in shift
    assert re.search(r"for \(child = widget->child; child; child = child->next\)\n\t\{\n\t\toverflow = "
                     r"\(short\)\(overflow \+ ui_widget_port_key_cap_overflow\(child,", overflow)
    # (a piece sitting on the picture's line, the picture after its start)
    assert "bounds.x0 > x0 + 1" in overflow
    assert "y0 >= bounds.y0 - 8 && y1 <= bounds.y1 + 8" in overflow
    assert "pc_menu_button_key_cap_left(definition->background_bitmap.index" in overflow
