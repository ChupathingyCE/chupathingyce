# Fonts

The fonts the game's text is drawn with, at the display's resolution, in
place of the bitmap fonts of the maps (`port/linux/src/text_hires.c`).
`fonts.json` says which font draws each of the maps' font tags, its role
(`large` or `small`: which of the player's fonts stands in for it, and
which of Online Games' menu faces it is) and how wide it is drawn (`width`,
1 for the font's own widths). To change a built-in font, change its entry
there: the builds embed what it names. A player can draw their own font
instead (`display.high_res_font`, from `data/fonts/` beside the maps).

| File | Draws | From |
| --- | --- | --- |
| `CyborgInterfaceGothic-Regular.ttf` | `ui\large_ui`, `ui\interstate`, `ui\small_ui`, the menus' titles (as pictures, `port/assets/titles`), our own screens' text and titles, and Chupathingy's loading picture | Cyborg Interface Gothic by fizzy (below) |
| `Overpass-900.ttf` | (drew the large tags until 0.8.0) | Overpass, weight 900 (Black) |
| `Overpass-750.ttf` | (drew `ui\small_ui` until 0.8.0) | Overpass, weight 750 |

The maps' fonts are Interstate (Tobias Frere-Jones, The Font Bureau), a
commercial typeface that cannot be shipped.

Cyborg Interface Gothic was made by fizzy (Discord: ._o.) for this game's
text, to match Halo's interface font: many of its glyphs are derived from
Overpass, the others drawn by them. It is under the SIL Open Font License
1.1 (`CyborgInterfaceGothic-OFL.txt`, with both copyright lines), without a
Reserved Font Name. Its names (family "Cyborg Interface Gothic", PostScript
`CyborgInterfaceGothic-Regular`, version 1.000, the copyright and license
fields) were set with fontTools from fizzy's FontForge file, as agreed with
them; its outlines are theirs, unchanged. Matched against the Xbox maps'
font tags (each glyph's coverage overlapping the tag's, at the tag's
capital height, as `text_hires.c` sizes it), it overlaps `ui\large_ui`
and `ui\interstate` better than Overpass 900 does (0.82 to 0.79 on
average) and `ui\small_ui` as well as Overpass 750 does (0.74), at its own
widths (`width` 1 in `fonts.json`). It lacks the soft hyphen, the low
double quote and the euro sign; text with one of those is drawn from the
maps' bitmap fonts, as for any character the font lacks. Overpass (Red Hat, Delve
Withrington) follows the same Highway Gothic road-sign alphabet and is
under the SIL Open Font License 1.1 (`Overpass-OFL.txt`), without a
Reserved Font Name. Matched against the maps' glyphs, its weights 900 and 750 overlap
them as much as Interstate Bold does, at the same letter widths.

The menus' titles are pictures of text in the maps, set in another
commercial typeface. They are set again in Cyborg Interface Gothic, so the
titles and the text are one face: `tools/title_assets.py` draws the Xbox
menus' titles over their own plates, at the old text's place, width and
capital height, and `tools/ce_menus.py --titles` draws the titles our menus
add (the Gearbox and Chupathingy menus' own screens). The overlay's screens
(Online Games, the Delta Profile, the setup screen) draw their titles in it
too. To draw them again:

    python3 tools/title_assets.py --map assets/maps/ui.map
    python3 tools/ce_menus.py --titles

The Overpass files are static instances of Google Fonts' variable Overpass
(`ofl/overpass/Overpass[wght].ttf`, google/fonts commit
9710da1eacb3be272583c3224dcb70f9da6eadbb), made with fontTools:

    python3 -m fontTools.varLib.instancer 'Overpass[wght].ttf' wght=900 -o Overpass-900.ttf
    python3 -m fontTools.varLib.instancer 'Overpass[wght].ttf' wght=750 -o Overpass-750.ttf

The builds no longer embed the Overpass files: they draw nothing since
0.8.0, and the builds embed only the fonts `fonts.json` names
(`tools/embed_assets.py`). The release packages still carry
`Overpass-OFL.txt`, for the glyphs Cyborg Interface Gothic takes from it.
