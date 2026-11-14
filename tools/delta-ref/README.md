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
| `conformance.py` | checks a port's packets, or stands in for a host on 127.0.0.1 |
| `loopback_host.sh` | our dedicated server on 127.0.0.1 alone, to join from your port |
| (game side) | `tools/delta_ref_capture/` in the game's repository makes `golden.h` from the game's own code and tests this client against its real host |

GPL-3.0-or-later, like the rest of the repository.
