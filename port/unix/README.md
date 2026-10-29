# Other Unix hosts

`ninja freebsd64` builds the same game as the 64-bit Linux build
(`ninja linux64`, [port/linux/README.md](../linux/README.md#64-bit)) as native
x86-64 code on FreeBSD: `build/freebsd64/halo`. Releases and nightlies have
it as `chupathingyce-freebsd64-release.zip`, built by the Build workflow in a
FreeBSD 15.1 virtual machine, and a release's build updates itself from the
next release's FreeBSD zip (`updater.c`, "freebsd64"). A host whose builds the
project does not publish (`published` in `tools/unix64_build.py`) is built by
hand and never updates itself (`configure.py` refuses `HALO_RELEASE_BUILD=1`
there).

The Xbox address space and the `long` rewrite are the 64-bit Linux build's
(`tools/lp64_build.py`). What differs is the host, and all of it is here and in
`tools/unix64_build.py`; no file of `port/linux` is edited for it, but three
hooks that name no host (`memory_watch.c` and `posix_crash.c`: the registers
of a signal's context; `build_identity.c`: the platform's name).

| File | Says |
| --- | --- |
| `include/halo_unix_prefix.h` | Ahead of `halo_linux_prefix.h`, in the game's units and the platform units: a 16-bit `wchar_t`, the MSVC names libtiff uses (`_open`...) apart from libc's own, glibc's `__control_word` for the x87 environment, the host's `u_long`, ctype as functions. |
| `include/halo_unix_host.h` | In the platform units and `posix_*.c`: `/proc/self/exe`, a mapping hint that is kept, `MAP_NORESERVE` and `MAP_FIXED_NOREPLACE`, the registers of a signal's context. |
| `src/unix_host.c` | The functions it declares. |
| `tools/unix64_build.py` | `UNIX_HOSTS`: the C library's restriction to ISO C, FFmpeg's target, the libraries. |

Another host is an entry in `UNIX_HOSTS`, and a block in each of the two
headers (and in `unix_host.c`) for what its C library does differently.

## FreeBSD

### Requirements

- FreeBSD 14 or later, amd64. The base system's clang and lld.
- `pkg install python3 ninja pkgconf sdl3 gmake` (`gmake` builds FFmpeg).
- `llvm` for `llvm-nm` (also found as `llvm-nm19` and the like), which the
  link check needs: configure.py stops without it (`--lto=off` leaves objects
  any `nm` reads).
- OpenGL 4.5 (core) from the driver (Mesa or NVIDIA); the game needs it.

### Build

```sh
python3 configure.py --release
ninja freebsd64
```

`configure.py` on FreeBSD does not generate the Linux targets, and `ninja`
without a target builds `freebsd64`. `--portable` only means "any x86-64
processor" here (`-march=x86-64` instead of `-march=native`). `--lto=thin`
or `--lto=off` link faster. `--without-ffmpeg` leaves the movies out, and
FFmpeg's build with them.

### The released build

Unzip `chupathingyce-freebsd64-release.zip` and add the maps next to `halo`.
It is built on FreeBSD 15.1 and needs FreeBSD 15 or later (amd64), and the
system's SDL3: `pkg install sdl3`. On FreeBSD 14, build it yourself (below).

### Start the game

As on Linux (`build/freebsd64/halo`; the maps next to it, the saves in
`~/.local/share/chupathingyce`). The executable's own folder is found with
`sysctl kern.proc.pathname`, not `/proc`, which FreeBSD does not mount by
default.
