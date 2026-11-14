# Moving the Delta spec

The Delta spec and its reference client are to move to a repository of
their own, ChupathingyCE/delta. This note says what moves and what stays,
so the move is a plain copy.

## What moves

Everything in these two folders, keeping their places relative to each
other (`docs/delta/` and `tools/delta-ref/`):

| Folder | Files |
| --- | --- |
| `docs/delta/` | `README.md`, `peer.md`, `versioning.md`, `registry.md`, `registry.json`, `list.md`, `stats.md`, `control.md`, `link.md`, `clients.md`, `security.md`, `ports.md`, this note |
| `tools/delta-ref/` | `delta_ref.h`, `delta_ref.c`, `delta_ref_test.c`, `golden.h`, `Makefile`, `conformance.py`, `loopback_host.sh`, `README.md` |

The two folders link to each other with relative paths
(`../../tools/delta-ref/...` from the spec, `../../docs/delta/...` from the
client). Keep the layout, or change those links in one pass. Every other
link out of them is a full GitHub URL into the game's repository.

The reference client and its tests need only `golden.h` and a C99
compiler. `conformance.py` needs only Python 3. `loopback_host.sh` needs a
Chupathingy dedicated server program, which it is given by path.

## What stays in the game's repository

| File | Why |
| --- | --- |
| `docs/delta-implementation.md` | how Chupathingy's own game and server implement the spec |
| `docs/delta.md`, `docs/delta-compatibility.md` | short pages that send old links to the spec |
| `docs/online.md` | the players' page on playing online |
| `tools/delta_ref_capture/` | makes `golden.h` from the game's own `delta_wire.c`, `delta_compat.c` and `browser.c`, checks `peer.md`'s hex examples against it, and runs the reference client against the game's real host |
| `tools/test_delta_ref.py`, `tools/test_delta_registry.py` | the game's CI checks of the kit and of `registry.json` against `delta.h` |

## After the move

- The game's repository keeps a copy of `golden.h` and `registry.json` (or
  fetches them), and its tests keep checking them against its own code: a
  change to the game's wire then shows up as a change to the spec's golden
  packets.
- `docs/delta.md` and the game's code comments point at the new
  repository's pages instead of `docs/delta/`.
- The spec repository's CI runs `make -C tools/delta-ref` (little- and
  big-endian) and `conformance.py`'s self-checks.
