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
  Two changes, marked `ChupathingyCE`: `stbi__getn` copies nothing for 0
  bytes, and an empty IDAT chunk is not read at all (an empty first IDAT
  chunk had no buffer to copy to or offset).
- `stb_image_write.h` v1.16 (same commit as stb_truetype): PNG encoding,
  for the player's screenshots (`port/linux/src/capture.c`).
- `stb_vorbis.c` v1.22 (same commit): Ogg Vorbis decoding, compiled once in
  `port/linux/game/content_vorbis.c` for Halo PC's sounds in Custom Edition
  maps and for MCC maps' sounds.
  Not unchanged: a map's sounds come from anyone, so its setup parsing is
  bounded where upstream's v1.22 is not (each change marked `ChupathingyCE`
  in the file): allocation sizes that would overflow refused, the vendor and
  comment lengths and the comment count kept within the stream, the comment
  list zeroed and its length reset when it cannot be read, room for all 16
  submaps, a failed sparse codebook decode not used as an index, a
  codebook's entries times dimensions bounded, the flat run after a
  floor's last point masked to the table as `draw_line` masks its own, and
  no temporary allocation of 0 bytes (as upstream's v1.24: a sparse
  codebook of no entries leaked its lengths).
