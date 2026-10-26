# FreeBSD

`ninja freebsd64` builds the same game as the 64-bit Linux build
(`ninja linux64`, [port/linux/README.md](../linux/README.md#64-bit)) as native
x86-64 code on FreeBSD: `build/freebsd64/halo`. The project publishes no
FreeBSD build and does not test this one: it is built by hand, and it never
updates itself.

The Xbox address space and the `long` rewrite are the 64-bit Linux build's
(`tools/lp64_build.py`); `tools/freebsd64_build.py` is only the host: FreeBSD's
libc restricted to ISO C (`_C99_SOURCE`), SDL3 from `/usr/local`, no libdl,
libexecinfo for `backtrace()`.

## Requirements

- FreeBSD 14 or later, amd64. The base system's clang and lld.
- `pkg install python3 ninja pkgconf sdl3 gmake` (`gmake` builds FFmpeg).
- `llvm` for `llvm-nm`, which the link check needs when link-time
  optimisation is on (`--lto=off` leaves objects `nm` can read). Without it
  the link goes unchecked, and configure.py says so.
- OpenGL 4.5 (core) from the driver (Mesa or NVIDIA); the game needs it.

## Build

```sh
python3 configure.py --release
ninja freebsd64
```

`configure.py` on FreeBSD does not generate the Linux targets, and `ninja`
without a target builds `freebsd64`. `--portable` only means "any x86-64
processor" here (`-march=x86-64` instead of `-march=native`). `--lto=thin`
or `--lto=off` link faster. `--without-ffmpeg` leaves the movies out, and
FFmpeg's build with them.

## Start the game

As on Linux (`build/freebsd64/halo`; the maps next to it, the saves in
`~/.local/share/chupathingyce`). The executable's own folder is found with
`sysctl kern.proc.pathname`, not `/proc`, which FreeBSD does not mount by
default.
