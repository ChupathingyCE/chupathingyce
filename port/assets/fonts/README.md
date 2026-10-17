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
| `CyborgInterfaceGothic-Regular.ttf` | `ui\large_ui`, `ui\interstate`, `ui\small_ui`, and Online Games' menu text | Cyborg Interface Gothic by fizzy (below) |
| `Overpass-900.ttf` | (drew the large tags until 0.8.0) | Overpass, weight 900 (Black) |
| `Overpass-750.ttf` | (drew `ui\small_ui` until 0.8.0) | Overpass, weight 750 |
| `OpenCE-Regular.ttf` | the menus' titles, as pictures (`port/assets/titles`) | OpenCE: Newtown, respaced for this project (`tools/title_font.py`) |
| `Newtown-Regular.ttf` | (OpenCE's source) | Newtown by Roger White, unmodified |

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
commercial typeface. OpenCE (Open Community Edition) stands in for it:
Newtown, a typeface in the same style by Roger White (1994), which its author
released into the public domain (`Newtown-LICENSE.txt` quotes his statement
and where it is archived), respaced for this project. Only its spacing is
changed, its outlines are Newtown's: each letter's side spaces and the
kerning pairs are measured from where the letters sit in the maps' titles.
OpenCE is under the SIL Open Font License 1.1 (`OpenCE-OFL.txt`), without a
Reserved Font Name. `tools/title_assets.py` draws the menus' titles with it,
and the builds embed those pictures; the overlay's screens (Online Games)
draw their titles with it too, so the builds embed it, and the release
packages carry `OpenCE-OFL.txt`. To measure its spacing
again and rebuild it:

    python3 tools/title_font.py --measure    # paste the values into tools/title_font.py
    python3 tools/title_font.py
    python3 tools/title_assets.py --map assets/maps/ui.map

The Overpass files are static instances of Google Fonts' variable Overpass
(`ofl/overpass/Overpass[wght].ttf`, google/fonts commit
9710da1eacb3be272583c3224dcb70f9da6eadbb), made with fontTools:

    python3 -m fontTools.varLib.instancer 'Overpass[wght].ttf' wght=900 -o Overpass-900.ttf
    python3 -m fontTools.varLib.instancer 'Overpass[wght].ttf' wght=750 -o Overpass-750.ttf

The builds embed the Overpass files (`tools/embed_assets.py`), and the
release packages carry `Overpass-OFL.txt`, as the license asks.
