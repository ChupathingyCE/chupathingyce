<!--
SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Delta reference client

A small C99 client for Delta Peer, for other people's ports of Halo: the
handshake, the capability bits and the game's advertisement. The guide is
[docs/delta/ports.md](../../docs/delta/ports.md).

```sh
make            # builds delta_ref.o and runs the tests (BUILD=dir for elsewhere)
python3 conformance.py --help
./loopback_host.sh --help
```

| File | What it is |
| --- | --- |
| `delta_ref.h`, `delta_ref.c` | the client: no allocation, no threads, no OS calls but your send function |
| `delta_ref_test.c`, `golden.h` | its tests, against packets our own code wrote |
| `delta_layout.h`, `delta_layout.c` | layout facts by network version (11 to 45, and a signed table's facts past 44) and the game settings record's converter between versions' layouts ([versioning.md](../../docs/delta/versioning.md#layout-facts-by-network-version)) |
| `delta_layout_test.c` | its tests: 32's, 41's and 44's records each way, Content Evolved's plain record at 44, a table's facts (wrapping values too), the fuzz target on its regression inputs |
| `fuzz_layout.c` | a fuzz target for the layouts and the converter (libFuzzer with `-DDELTA_LAYOUT_LIBFUZZER`) |
| `conformance.py` | checks a port's packets, or stands in for a host on 127.0.0.1; `layout`, `record` and `convert` check a port's game settings records for a network version against `docs/delta/layouts.json` |
| `loopback_host.sh` | our dedicated server on 127.0.0.1 alone, to join from your port |
| (game side) | `tools/delta_ref_capture/` in the game's repository makes `golden.h` from the game's own code and tests this client against its real host |

## On bare metal

The client calls nothing, but a compiler may still emit calls to `memset`
and `memcpy` for struct copies and clears, so a port without a C library
must supply those two. It may also add a stack protector that wants
`__stack_chk_guard` and `__stack_chk_fail`; build with
`-fno-stack-protector` to leave it out.

GPL-3.0-or-later, like the rest of the repository.
