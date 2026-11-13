# How CI works

The Build workflow (`.github/workflows/build.yml`) checks every change. Pull requests get a quick check, and every platform is built before anything ships.

## Pull requests

A pull request gets the **fast check**:

- `linux`: the 32-bit Linux build.
- `linux-tests`: the tests against that build, split over four runners. The console gate tests are in `tools/ci_tests.txt`, so they run on every pull request.
- `linux-big-endian`: Delta's wire and the map byte-order checks on a big-endian machine (QEMU).

It also builds each platform whose own files the pull request changes, using the same areas as the PR labels (`.github/labeler.yml`):

| Changed files | Extra jobs |
|---|---|
| `port/windows/`, `server/windows/`, `tools/windows_build.py` | `windows`, `windows64`, `server-windows` |
| `port/android/`, `tools/android*` | `android64` |
| `port/macos/`, `port/ios/`, `tools/macos_build.py` | `macos` |
| `port/unix/`, `tools/unix64_build.py` | `freebsd64` |
| `server/`, `tools/server_build.py` | `server` (x64 only) |
| `tools/linux64_build.py`, `tools/linux_sysroot.py`, `tools/bgfx-patches/`, the bgfx renderer files | `linux64` |

Changes to documentation alone (`*.md`, `docs/`) don't build.

## The full matrix

Every platform (Linux 32/64, FreeBSD, Windows 32/64, macOS, Android, the dedicated servers and their container) builds when:

- the pull request has the **`ci: full`** label. Adding the label runs the pull request's latest Build again (`.github/workflows/full-build-label.yml`), and later pushes keep building in full while the label is there.
- the pull request comes from a `batch/*` branch (several pull requests landed together).
- the pull request changes the build's plumbing: `configure.py`, `VERSION`, or `tools/ci_build.py`, `version.py`, `download.py`, `ffmpeg_build.py`, `bgfx_build.py` or `third_party_notices.py`.
- anything is pushed to `main` or a release branch.
- a run is started by hand or by the release workflows (nightlies, test builds and releases). Only these runs make debug builds and upload symbols.

If you change the workflow itself or a platform's build, add `ci: full` to check every platform before merging.

## ci-ok

`ci-ok` sums each run up in one check: it's green when every job that had to run passed. A platform job the fast check skips counts as passed. On a full run, every job must pass.

`ci-ok` isn't required to merge. Work in progress may land with a red job, which gets fixed right after in its own pull request. The strict gate is shipping: a nightly, test build or release is published only from a run of that exact commit where every platform built and passed.

## Runs and caches

- A newer push to a pull request, `main` or a release branch cancels that branch's run still going. Runs started by hand are never canceled.
- Tags don't build: releases and nightlies are built before they're tagged.
- Compile caches (ccache) and downloads are saved only by `main`'s runs, so every pull request starts from `main`'s newest. After each push to `main`, the `cache-prune` job deletes `main`'s older cache generations (never the newest) and anything pull requests left behind, which keeps the repository under GitHub's 10 GB cache limit.
- Artifacts are kept for three days. A pull request's builds can be downloaded from its run to test.
