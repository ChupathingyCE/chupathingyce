# The overlay's fonts

Built into the game (port/linux/src/ui_overlay.c), for the screens it draws
over the game's own (the game list's Online Games, its filters, the
profile). Their text is Cyborg Interface Gothic (`port/assets/fonts`);
Noto Sans Bold draws only the letters Cyborg lacks (other scripts, in
players' names). Noto Sans Regular draws nothing now.

| Font | License |
| --- | --- |
| `NotoSans-Regular.ttf`, `NotoSans-Bold.ttf` (Google) | SIL Open Font License 1.1, `OFL.txt` |
| `input_xbox.ttf`, `input_playstation.ttf`, `input_nintendo.ttf` (the Switch's), `input_steam_deck.ttf`, `input_steam_controller.ttf`, `input_wiiu.ttf`, `input_keyboard.ttf`: Kenney's Input Prompts 1.5a | CC0, `KENNEY-CC0.txt` |
| `platform_icons.ttf`: one-color platform logos from Simple Icons and Wikimedia Commons, `platform_icons/` | CC0 and public domain (see below) |

The Input Prompts fonts draw a controller's (or the keyboard's) buttons as
glyphs from U+E000, so a prompt names the button of what the player holds.
Each is the font of its device's folder in Kenney's pack, renamed (its
glyphs' names are in the pack's `*_map.txt`). The PlayStation's face
buttons are drawn with them, and every button of the Steam Deck, the Steam
Controller and the Wii U pads (ui_overlay.c). The Steam and Wii U fonts are
for a few devices, so a build embeds only the ones it can meet
(`tools/embed_assets.py --prompt-fonts`): Android the Steam Controller's,
the dedicated server none.

`platform_icons.ttf` draws the scoreboard's platform logos, one glyph per
platform at U+E100 plus its number in delta.h's registry (Windows U+E101 up
to the Apple TV U+E115). One logo can serve several platforms: the PlayStation
logo is the PSP's, the Vita's, the PS3's, the PS4's and the PS5's. A player
whose platform isn't known, or has no logo yet (the Nintendo 64, the Legacy
PC, the Steam Link, iOS and the Apple TV), gets none. The sources are the SVGs in `platform_icons/`, and
`tools/platform_icons_font.py` builds the font from them (its `PLATFORMS`
says which numbers each SVG draws). [BSD Daemon used with permission.](http://www.mckusick.com/copyright.html)

- Windows, Apple (Mac), Linux, FreeBSD, Android, Steam Deck, Nintendo
  Switch, PlayStation (`playstation.svg`) and Xbox (the Xbox Series' sphere,
  `xbox_uwp.svg`) are from [Simple Icons](https://simpleicons.org), CC0 1.0.
- The original Xbox's X mark is Wikimedia Commons' "Xbox Logo 2001.svg"
  (public domain, PD-textlogo), the X symbol only, flattened to one shape.
- The Xbox 360's ring and the Wii U GamePad are our own drawings (CC0).

The names and logos are trademarks of their owners, used only to identify
platforms; Chupathingy is not affiliated with or endorsed by them.
