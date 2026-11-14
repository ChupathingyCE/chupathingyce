# HD controller buttons (DKrumpp)

DKrumpp's HD controller buttons: the original Xbox controller's (the
Duke's), and the PlayStation's and the Switch's face buttons, each in two
looks. He drew the gem look (white rim, gradient) for menus and the flat look
for in-game prompts, and that's how the game uses them
(`port/linux/src/ui_overlay.c`, `port/linux/game/hud_buttons.c`):

- `input.button_style`: AUTO (the controller you use), DUKE, XBOX,
  PLAYSTATION or SWITCH.
- `input.button_look`: GEM or FLAT, for the menus and the Delta Guide.
- `input.hud_buttons`: CLASSIC (the map's own pictures) or HD (the flat look).

**Used with permission, credit DKrumpp.** DKrumpp offered the art on the
Chupathingy Discord (2026-11-11) and asked only to be credited. The art is
not covered by Chupathingy's GPL-3.0 license: anyone reusing these files
should credit DKrumpp and ask him first. See
[docs/permissions.md](../../../docs/permissions.md).

`tools/hd_button_assets.py` renders the PNGs here (128x128, committed) from
his 4096-pixel PNGs (`hd_buttons_v1.zip`, unpacked):

    python3 tools/hd_button_assets.py .../hd_buttons_v1

The only change to his art: the black button gets a light grey rim, so it
shows on the menus' dark blues.

| Files | Buttons |
| --- | --- |
| `duke-{a,b,x,y}-gem`, `duke-{a,b,x,y}-flat` | The Duke's A, B, X, Y |
| `duke-white`, `duke-black`, `duke-start`, `duke-back`, `duke-trigger-{left,right}` | Its white and black buttons, Start, Back, the triggers |
| `ps-{cross,circle,square,triangle}-{gem,flat}` | PlayStation face buttons |
| `switch-{a,b,x,y}-{gem,flat}` | Switch face buttons |
