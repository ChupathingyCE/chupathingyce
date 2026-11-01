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
to Legacy PC U+E112). One logo can serve several platforms: the PlayStation
logo is the PSP's, the Vita's, the PS3's, the PS4's and the PS5's. A player
whose platform isn't known, or has no logo yet (the Nintendo 64, the Legacy
PC), gets none. The sources are the SVGs in `platform_icons/`, and
`tools/platform_icons_font.py` builds the font from them (its `PLATFORMS`
says which numbers each SVG draws):

- Windows, Apple (Mac), Linux, FreeBSD, Android, Steam Deck, Nintendo
  Switch, PlayStation (`playstation.svg`) and Xbox (the Xbox Series' sphere,
  `xbox_uwp.svg`) are from [Simple Icons](https://simpleicons.org), CC0 1.0.
- The original Xbox's X mark is Wikimedia Commons' "Xbox Logo 2001.svg"
  (public domain, PD-textlogo), the X symbol only, flattened to one shape.
- The Xbox 360's ring and the Wii U GamePad are our own drawings (CC0).

The names and logos are trademarks of their owners, used only to identify
platforms; Chupathingy is not affiliated with or endorsed by them.
