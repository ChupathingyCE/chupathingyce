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
