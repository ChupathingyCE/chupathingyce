# Loading picture

`chupathingy_loading.tga` is Chupathingy's loading picture
(`port/linux/game/loading_picture.c`). `display.xbox_loading_picture` is on
by default, so the loading screen shows the Xbox's own picture when the
maps folder has one (`xbox_loading.tga`, or a PAL disc's `loading.tga`),
and this one otherwise. With the setting off, it always shows this one.
The consoles leave it out and show the disc's picture
(`HALO_CONSOLE_NO_LOADING_PICTURE`). It imitates the Xbox's picture with
the helmet and tusks of Chupathingy's icon.

It is made by `tools/make_loading_picture.py` from `port/art/icon.svg` and
`port/assets/fonts/CyborgInterfaceGothic-Regular.ttf` (by fizzy, SIL OFL), and the builds embed it
(`tools/embed_assets.py`). The tool needs `rsvg-convert` (librsvg), Pillow
and NumPy:

    python3 tools/make_loading_picture.py [--preview preview.png]

`--preview` also writes the picture at 640 by 480 as a PNG. The picture
must stay an uncompressed 24-bit TGA of 320 by 240, the only kind the
loading screen reads.
