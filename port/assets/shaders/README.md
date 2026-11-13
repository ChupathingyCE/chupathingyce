# Shader key lists

`stock.keys` lists what the stock maps' shaders are made from, so the game can
make them while a map's loading screen is up (`display.prepare_shaders`,
`port/linux/src/d3d8_shader_prep.inl`) instead of stopping for a moment the
first time something new is drawn. The game embeds it
(`tools/embed_assets.py`). Every map played adds its own list to the cache
folder (`cache/shader_keys/<map>.keys`, the same format), so a custom map's
shaders are ready from its second load on.

A key is what the shader translators (`nv2a_vsh.c`, `nv2a_psh.c`) are given,
not what they make: the game's vertex program, the way it is drawn, and the
pixel shader's state. Each machine makes the GLSL, and its driver or bgfx
backend makes the rest, so one list serves OpenGL (and its 3.3 path), Vulkan
and Metal, on every platform. Compiled shaders can't be shipped: each driver
makes its own, and keeps them in its cache.

## Making it

```
python3 tools/shader_keys.py record --build build/macos/chupathingy
```

runs the build on each stock map with `debug.draw_every_shader`: the map's
first frame is drawn first, unseen, through every shader its tags hold
(`port/linux/game/render_enumerate.c`, based on work by goatbutt and the
OpenH1 team). Each draw's key goes into the map's own list, and the maps'
lists are merged into `stock.keys`. Make it again when the translators or the
pixel shader key change. `python3 tools/shader_keys.py check stock.keys`
reads a list as the game does.

## The format

Text, one record a line. `#` starts a comment.

```
chupathingy shader keys 1
p <number> <slot>:<value> ...
map <name>
k <vertex code> <flags> <packed mask> <pixel key number>
```

- `p` numbers a pixel shader key for the `k` lines after it, until another
  `p` line gives that number. It lists the key's slots that are not 0: the
  slot in decimal, the value in hexadecimal. Slots 0 to 56 are the combiner
  state (`combiner_state`), 100 the texture modes, 101-104 the samplers'
  types, 105-108 alpha kill, 109-112 color sign, 113 the alpha test's
  function, 114 fog, 115 the fog table mode, 116 sample counting, 117
  coverage alpha, 118 per-pixel lighting, 119 the alpha test's samples, 120
  point thresholds, 121 the Light effects, 122 an MCC map's bump
  attenuation. A new field takes the next number, and a number is never
  used again, so an older list still reads: a slot a build doesn't know is
  skipped, and a field a list doesn't give is 0.
- `map` starts one map's keys, by its file's name. A map's own list has no
  `map` line.
- `k` is a key: the vertex program's code hash (FNV-1a, 64 bits, of its
  words as little-endian bytes), its flags (1 immediate mode, 2 lit for each
  pixel, 4 drawn with a depth bias, 8 the Light effects' form) and its packed attributes, in
  hexadecimal, and the number of its pixel key.

Every value is written out as a number, so a list is the same on every
machine, whatever its byte order or word size.
