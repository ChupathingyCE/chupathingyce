# Controller prompts

The controller's button icons the overlay's prompts draw for an Xbox-like
pad (`port/linux/src/ui_overlay.c`: Online Games), the Xbox One and Series
look. The keyboard's key caps, and the PlayStation's, Nintendo's, Steam
Deck's, Steam Controller's and Wii U's buttons, are drawn otherwise
(Kenney's Input Prompts fonts, `port/linux/ui/fonts`).

Controller button icons by snapchipper, CC0. They keep that license: they
are not covered by Chupathingy's (snapchipper offered them as CC BY-SA or
CC0; Chupathingy took them as CC0, with credit all the same).

`svg/` holds the drawings, renamed in lowercase. `tools/prompt_assets.py`
renders each one into a PNG of 128x128 pixels here (committed), and the
builds embed the ones the prompts use (`tools/embed_assets.py --fonts`):

    python3 tools/prompt_assets.py

| File | Button |
| --- | --- |
| `button-a`, `button-b`, `button-x`, `button-y` | A, B, X, Y |
| `button-back`, `button-start` | Back, Start |
| `bumper-left`, `bumper-right` | LB, RB |
| `trigger-left`, `trigger-right` | LT, RT |
| `stick-left`, `stick-right` | the sticks (not used yet) |
| `stick-left-click`, `stick-right-click` | the sticks pressed (not used yet) |
