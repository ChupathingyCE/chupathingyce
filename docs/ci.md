# How CI works

The Build workflow (`.github/workflows/build.yml`) checks every change in tiers. The `plan` job picks the tier, and `ci-ok` is green when every job that tier needs passed.

| Tier | When | Jobs |
|---|---|---|
| A | every push to a pull request | `linux`, `linux-tests` (the tests the change needs, on 2 to 4 runners), `linux-big-endian`, `server` (x64), plus each platform whose own files changed |
| B | the merge queue, on `main` with the pull request merged | A plus `linux64`, `windows`, `windows64`, `macos`, `android64`, `freebsd64` |
| C | every push to `main` or a release branch, `batch/*` pull requests, build plumbing changes | every job: also the x86 and arm64 servers, `server-windows`, `server-container` |
| D | runs started by hand or by the release workflows (nightly, test builds, releases) | every job, plus debug builds and symbols |

`main` requires `ci-ok`. A pull request that passes tier A goes into the merge queue (`gh pr merge --auto --merge`). The queue runs tier B on the merge result, and merges only if `ci-ok` passes again. A pull request that changes only documentation still runs tier A, because the tests read `docs/`.

In tier A, the platform jobs for changed files use the same areas as the PR labels (`.github/labeler.yml`):

| Changed files | Extra jobs |
|---|---|
| `port/windows/`, `server/windows/`, `tools/windows_build.py` | `windows`, `windows64`, `server-windows` |
| `port/android/`, `tools/android*` | `android64` |
| `port/macos/`, `port/ios/`, `tools/macos_build.py` | `macos` |
| `port/unix/`, `tools/unix64_build.py` | `freebsd64` |
| `tools/linux64_build.py`, `tools/linux_sysroot.py`, `tools/bgfx-patches/`, the bgfx renderer files | `linux64` |

Build plumbing means `configure.py`, `VERSION`, or `tools/ci_build.py`, `version.py`, `download.py`, `ffmpeg_build.py`, `bgfx_build.py` or `third_party_notices.py`. To build every platform on a branch before then, start Build by hand on it (`gh workflow run build.yml --ref <branch>`). The old `ci: full` label does nothing now.

## Which tests run

`tools/ci_test_buckets.txt` gives every test a bucket, and the plan job (`tools/ci_select_tests.py`) picks the tests for the run:

| Bucket | Tier A (pull request) | Tiers B, C, D |
|---|---|---|
| `core` | always | always |
| `area` | when the pull request changes the test's paths | always |
| `security` | when the pull request changes the test's paths | always |
| `maps` | never (they need maps: the maps runner's nightly) | never |
| `local` | never | never |

A test's paths are found from the test itself: the repository paths it names, its harness or fuzz files, and the tools modules it imports, plus any paths after it in the buckets file. A changed header counts as a change to every file that includes it, however deep, and a changed tools module as one to every module that imports it. A test without a bucket, or whose paths come out empty, runs on every pull request. A pull request that changes the build's plumbing (`configure.py`, `VERSION`, the `tools/*_build.py` and sysroot files, `ninja_syntax.py`), a workflow, the selector, the buckets file, the harness's shared parts, `tools/fuzz_support.py`, the prefix header or a `source/cseries/` header runs every test, and so does one whose files GitHub can't list. So does a changed file that no test's paths name (the core tests' paths and the test lists count; documentation doesn't), with a notice in the plan job's log naming it. The merge queue runs every test, so a path a test missed shows up there, before the merge.

linux-tests runs on one to three pytest runners (by the tests' weight) plus `other`, which has the harness tests, the script tests and the updater's. `python3 tools/ci_select_tests.py --event pull_request --changed files.json --explain` shows what a set of files would run, and why.

A new test goes in `area` unless it guards something every change can break (`core`) or it is a security regression (`security`). `tools/test_ci_select.py` fails until it has a line.

A nightly, test build or release is published only from a tier D run of that exact commit where every platform built and passed.

## Runs and caches

- A newer push to a pull request or a release branch cancels that branch's run still going. `main` is a queue of one: the run in progress always finishes, and only the newest push waits behind it. Runs started by hand are never canceled.
- Tags don't build: releases and nightlies are built before they're tagged.
- Compile caches (ccache) and downloads are saved only by `main`'s runs (tier C). Pull requests and the merge queue only restore them, so every run starts from `main`'s newest. After each push to `main`, the `cache-prune` job deletes `main`'s older cache generations (never the newest) and anything pull requests left behind, which keeps the repository under GitHub's 10 GB cache limit.
- The Linux jobs' Ubuntu packages (clang 22 from apt.llvm.org, the cross compilers, the test tools) are cached the same way, per runner image. With `main`'s cache a job installs them without downloading anything. Without it, apt downloads first, each try with a time limit, and a try that fails starts the next from another Ubuntu mirror. apt.llvm.org's key is checked against the one pinned in the workflow.
- When GitHub gives a job no runner (macOS's Apple silicon runners run short at busy hours, and such a job is canceled after 15 to 20 minutes in the queue), `.github/workflows/rerun-unacquired.yml` runs that Build's failed jobs again, once.
- Artifacts are kept for three days. A pull request's builds can be downloaded from its run to test.

## Nightly

Three scheduled workflows run what a pull request shouldn't wait on or can't run. Each can also be started by hand from the Actions tab.

| Workflow | Runs on | What it catches |
|---|---|---|
| `fuzz.yml` (Fuzz) | GitHub's Linux runner | crashes that random fuzzing finds in the host's parsers and the Steam readers (pull requests only replay saved inputs) |
| `online-tests.yml` (Online tests) | GitHub's Linux runner | the community map repository changing what it sends |
| `maps-tests.yml` (Maps tests) | the self-hosted `maps` runner | a change that plays differently on a real map (determinism, tricks), AI navigation, Forge mouse look, map names |

### The maps runner

The `maps` bucket's tests need the stock Xbox maps and a built game, so they run on a machine of ours that keeps the maps on its own disk. The maps never leave it: the job uploads no artifact and saves no cache, and it runs only `main`'s code. Until a runner has the `maps` label and the repository variable `MAPS_RUNNER` is `true`, the job is skipped and the run passes.

To set one up (it needs the repository's admin rights):

1. On the machine: the Linux build's tools (clang, lld, ninja, Python 3 with pytest, SDL 3's build dependencies), and a folder of the stock Xbox maps (NTSC) that the runner's user can read.
2. Settings, Actions, Runners, New self-hosted runner (Linux x64), and follow its steps as the runner's own user. When `config.sh` asks for labels, give `maps` (or later: Settings, Actions, Runners, the runner, edit its labels, add `maps`).
3. Put the runner in a runner group of its own that allows only this repository and only the `maps-tests.yml` workflow (Organization settings, Actions, Runner groups), so no other workflow and no pull request can run on it.
4. In the runner's folder, a `.env` file with `HALO_MAPS=/path/to/the/maps` (and, for the Custom Edition comparison, `HALO_MAPS_DATA_ROOT=/path/to/a/data/root` with `maps_ce/deathisland.map`), then restart the runner service.
5. Settings, Secrets and variables, Actions, Variables: `MAPS_RUNNER` = `true`.
6. Start Maps tests by hand once and check its log. A full run takes about 20 minutes (the 64-bit build about 7, the tests about 10); determinism's golden replay skips unless kept games are given, and the Custom Edition comparison skips without `HALO_MAPS_DATA_ROOT`.

`join_password_pair` and `net43_host` host games, so they aren't in the job: run them by hand, loopback only.
