# The overlay's fonts

Built into the game (port/linux/src/ui_overlay.c), for the screens it draws
over the game's own (the game list's Online Games, its filters, the
profile):

| Font | License |
| --- | --- |
| `NotoSans-Regular.ttf`, `NotoSans-Bold.ttf` (Google) | SIL Open Font License 1.1, `OFL.txt` |
| `input_xbox.ttf`, `input_playstation.ttf`, `input_nintendo.ttf`, `input_keyboard.ttf`: Kenney's Input Prompts | CC0, `KENNEY-CC0.txt` |
| `platform_icons.ttf`: one-color platform logos from Simple Icons and Wikimedia Commons, `platform_icons/` | CC0 and public domain (see below) |

The Input Prompts fonts draw a controller's (or the keyboard's) buttons as
glyphs from U+E000, so a prompt names the button of what the player holds.

`platform_icons.ttf` draws the scoreboard's platform logos, one glyph per
platform at U+E100 plus its number in delta.h's registry (Windows U+E101 up
to Switch U+E109). A player whose platform isn't known, or has no logo yet,
gets none. The sources are the SVGs in `platform_icons/`, and
`tools/platform_icons_font.py` builds the font from them:

- Windows, Apple (Mac), Linux, FreeBSD, Android, Steam Deck, Xbox (the Xbox
  360's and Xbox Series' sphere) and Nintendo Switch are from
  [Simple Icons](https://simpleicons.org), CC0 1.0.
- The original Xbox's X mark is Wikimedia Commons' "Xbox Logo 2001.svg"
  (public domain, PD-textlogo), the X symbol only, flattened to one shape.
- The Xbox 360's ring and the Wii U GamePad are our own drawings (CC0).

The names and logos are trademarks of their owners, used only to identify
platforms; Chupathingy is not affiliated with or endorsed by them.
