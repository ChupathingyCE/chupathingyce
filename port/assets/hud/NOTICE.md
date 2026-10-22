# High-res HUD art

## Aerocatia's and Jessie's art

The PNGs in `art/` are Aerocatia's and Jessie's high-resolution HUD art
from [Halo PC Restored](https://github.com/Aerocatia/halopc-restored),
release r220: the HUD bitmaps of its tags (`tags/ui/hud/bitmaps`, the
sniper rifle's scope ladder in `tags/weapons/sniper rifle/bitmaps`, and the
meters in the Xbox channel order in `extra/xbox_order_hud_meters`), decoded
as they are, unchanged. The HUD textures in this folder that
`tools/hud_assets.py` builds from them (every texture here except the five
listed below) are their art, cut and placed to fit the Xbox maps' HUD
sheets.

One of them is edited: `art/hud__bitmaps__sniper__hud_reticles_scope__10x.png`
is their sniper zoom labels' "x" beside a "10" set in Tahoma Bold, made by
`tools/hud_assets.py` (ten_times_label) because the Xbox's label says 10x
where theirs says 8x.

They are not covered by Chupathingy's license. They are used with
Aerocatia's permission, given on October 14, 2026 ("it was released to be
used"), on the condition that Aerocatia and Jessie are credited. Anyone
reusing them should follow the same terms, or ask Aerocatia.

## Older redraws

Where Halo PC Restored's art is no larger than the Xbox bitmap, the
textures are drawn from the SVG redraws in `svg/` instead: the health
meter's base (`hud_health_base__0.png`), the radar's blips
(`hud_sensor_blip__0.png`, `hud_sensor_blip_custom__0.png`) and its sweeper
(`hud_sweeper__0.png`, `hud_sweeper_mask__0.png`). Those redraws came with
the code this project was forked from. They are not Aerocatia's or
Jessie's work.
