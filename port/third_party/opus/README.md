# Opus (voice chat's codec)

Opus, by Xiph.Org, Skype Limited, Octasic, Jean-Marc Valin, Timothy B.
Terriberry, CSIRO, Gregory Maxwell, Mark Borgerding, Erik de Castro Lopo
and the other contributors in `AUTHORS`, under the BSD license in `COPYING`.

Upstream: https://opus-codec.org, release `1.5.2`, `opus-1.5.2.tar.gz`,
SHA-256 `65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1`.
This is our own copy from Xiph's release, not anyone else's vendored one.

Only the floating-point library in plain C is kept, unchanged: the files
`sources.txt` lists (Opus's own `CELT_SOURCES`, `SILK_SOURCES`,
`SILK_SOURCES_FLOAT`, `OPUS_SOURCES` and `OPUS_SOURCES_FLOAT`) and the
headers of `include`, `celt`, `silk`, `silk/float` and `src`. The
processor-specific code (x86, ARM), the fixed-point code and the neural
network extensions (`dnn`) are left out. It is built with `OPUS_BUILD`,
`VAR_ARRAYS`, `HAVE_LRINTF` and `HAVE_LRINT` (tools/linux_build.py's
`OPUS_DEFINES`). To update, copy the same files from a newer release and
make `sources.txt` from its `*_sources.mk` files again.

The desktop and Android builds use it for voice chat
(`port/linux/src/voice.c`). The dedicated server and the consoles don't
build it: a server relays voice without decoding it.
