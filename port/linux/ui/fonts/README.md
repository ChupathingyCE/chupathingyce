# The overlay's fonts

Built into the game (port/linux/src/ui_overlay.c), for the screens it draws
over the game's own (the game list's Online Games, its filters, the
profile):

| Font | License |
| --- | --- |
| `NotoSans-Regular.ttf`, `NotoSans-Bold.ttf` (Google) | SIL Open Font License 1.1, `OFL.txt` |
| `input_xbox.ttf`, `input_playstation.ttf`, `input_nintendo.ttf`, `input_keyboard.ttf`: Kenney's Input Prompts | CC0, `KENNEY-CC0.txt` |
| `platform_icons.ttf`: seven icons of Font Awesome Free 6.7.2 (Fonticons, Inc.) | SIL Open Font License 1.1, `FONTAWESOME-LICENSE.txt` |

The Input Prompts fonts draw a controller's (or the keyboard's) buttons as
glyphs from U+E000, so a prompt names the button of what the player holds.

`platform_icons.ttf` draws the scoreboard's platform logos: Apple U+F179,
Windows U+F17A, Android U+F17B, Linux U+F17C, Steam U+F1B6 (the Steam Deck)
and Xbox U+F412 from Font Awesome's brands, and a gamepad U+F11B from its
solid set for the consoles it has no logo for. A player whose platform isn't
known gets no logo. `tools/platform_icons_font.py` cuts it from Font
Awesome's fonts; as the OFL asks of a modified font, it does not carry the
name "Font Awesome".
