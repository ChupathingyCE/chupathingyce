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
| `CyborgInterfaceGothic-Regular.ttf` | `ui\large_ui`, `ui\interstate`, `ui\small_ui`, our own screens' text, Chupathingy's loading picture, and the titles when `display.title_font` is `"cyborg"` (`port/assets/titles_cyborg`) | Cyborg Interface Gothic by fizzy (below) |
| `ChupathingyTitles-Regular.ttf` | the menus' titles, as pictures (`port/assets/titles`), and our own screens' titles: `display.title_font = "classic"`, the default | Chupathingy Titles: Newtown, respaced for this project (`tools/title_font.py`) |
| `Newtown-Regular.ttf` | (Chupathingy Titles' source) | Newtown by Roger White, unmodified |
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
commercial typeface. Chupathingy Titles stands in for it: Newtown, a
typeface in the same style by Roger White (1994), which its author released
into the public domain (`Newtown-LICENSE.txt` quotes his statement and where
it is archived), respaced for this project. Only its spacing is changed,
its outlines are Newtown's: each letter's side spaces and the kerning pairs
are measured from where the letters sit in the maps' titles. Chupathingy
Titles is under the SIL Open Font License 1.1 (`ChupathingyTitles-OFL.txt`,
which keeps its copyright line), without a Reserved Font Name.
`tools/title_assets.py` draws the menus' titles with it (the classic set,
`port/assets/titles`), each letter where the map's letter is, and
`tools/ce_menus.py --titles` the titles our menus add (`ce/port`). The
overlay's screens (Online Games, the Delta Profile, the setup screen) draw
their titles with it too, so the builds embed it, and the release packages
carry `ChupathingyTitles-OFL.txt`.

`display.title_font = "cyborg"` draws the titles in Cyborg Interface Gothic
instead: a second set of pictures (`port/assets/titles_cyborg`, the menus'
`ce/port_cyborg`), set at the font's own even spacing over the same plates,
at the old text's place, width and capital height, and the overlay's titles
at the classic ones' capital height. The consoles embed the classic set
only (`HALO_CONSOLE_NO_TITLE_CHOICE`). To measure Chupathingy Titles'
spacing again and draw the titles again:

    python3 tools/title_font.py --measure    # paste the values into tools/title_font.py
    python3 tools/title_font.py
    python3 tools/title_assets.py --map assets/maps/ui.map
    python3 tools/title_assets.py --map assets/maps/ui.map --set cyborg
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
