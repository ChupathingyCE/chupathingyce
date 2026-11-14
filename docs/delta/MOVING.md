# The spec on its own

This folder and the reference client are self-contained, so they can be
published or moved on their own. This note lists what belongs to the spec
and what belongs to the game.

## The spec

Everything in these two folders, which keep their places relative to each
other (`docs/delta/` and `tools/delta-ref/`):

| Folder | Files |
| --- | --- |
| `docs/delta/` | `README.md`, `peer.md`, `versioning.md`, `registry.md`, `registry.json`, `list.md`, `stats.md`, `control.md`, `link.md`, `clients.md`, `security.md`, `ports.md`, this note |
| `tools/delta-ref/` | `delta_ref.h`, `delta_ref.c`, `delta_ref_test.c`, `golden.h`, `Makefile`, `conformance.py`, `loopback_host.sh`, `README.md` |

The two folders link to each other with relative paths
(`../../tools/delta-ref/...` from the spec, `../../docs/delta/...` from the
client). Every other link out of them is a full GitHub URL into the game's
repository.

The reference client and its tests need only `golden.h` and a C99
compiler. `conformance.py` needs only Python 3. `loopback_host.sh` needs a
Chupathingy dedicated server program, which it is given by path.

## The game's side

| File | What it is |
| --- | --- |
| `docs/delta-implementation.md` | how Chupathingy's own game and server implement the spec |
| `docs/delta.md`, `docs/delta-compatibility.md` | short pages that send old links to the spec |
| `docs/online.md` | the players' page on playing online |
| `tools/delta_ref_capture/` | makes `golden.h` from the game's own `delta_wire.c`, `delta_compat.c` and `browser.c`, checks `peer.md`'s hex examples against it, and runs the reference client against the game's real host |
| `tools/test_delta_ref.py`, `tools/test_delta_registry.py`, `tools/test_delta_docs.py` | the game's checks of the kit, of `registry.json` against `delta.h`, and of these pages' links |

Wherever the spec lives, the game keeps checking `golden.h` and
`registry.json` against its own code, so a change to the game's wire shows
up as a change to the spec.
