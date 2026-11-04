# Loading picture

`chupathingy_loading.tga` is Chupathingy's loading picture, shown on the
loading screen unless `display.xbox_loading_picture` is on
(`port/linux/game/loading_picture.c`). It imitates the Xbox's picture with
the helmet and tusks of Chupathingy's icon.

It is made by `tools/make_loading_picture.py` from `port/art/icon.svg` and
`port/linux/ui/fonts/NotoSans-Bold.ttf` (SIL OFL), and the builds embed it
(`tools/embed_assets.py`). It must stay an uncompressed 24-bit TGA of 320
by 240, the only kind the loading screen reads.
