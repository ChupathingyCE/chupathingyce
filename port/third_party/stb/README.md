# stb

Sean Barrett's single-file libraries (RAD Game Tools), public domain or MIT
licensed, as the user chooses (see `LICENSE`), vendored unchanged from
<https://github.com/nothings/stb>:

- `stb_truetype.h` v1.26 (commit 2c980bb59875b0d32144a71867fbdebb2f77cd20):
  TrueType glyphs. The game's text is drawn with fonts from
  `port/assets/fonts`, rasterized with it at the display's resolution
  (`port/linux/src/text_hires.c`); and the overlay's text with the fonts
  of `port/linux/ui/fonts` (`port/linux/src/ui_overlay.c`, the game browser's).
- `stb_image.h` v2.30: PNG and JPEG decoding, for the overlay's pictures.
- `stb_vorbis.c` v1.22 (same commit): Ogg Vorbis decoding, for Halo PC's
  sounds in Custom Edition maps (`port/linux/game/ce_vorbis.c`).
