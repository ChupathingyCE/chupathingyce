#!/usr/bin/env python3
"""Which tests a Build run runs (docs/ci.md, "Which tests run"), and how
linux-tests splits them over its runners.

Every test has a bucket in tools/ci_test_buckets.txt:
- core: every run;
- area: when the pull request changes a file the test reads, builds or
  imports (its paths, below), or the test itself;
- security: as area on a pull request; every run of the merge queue, main
  and the nightly runs them anyway, and so do they all of these;
- maps: only on the self-hosted maps runner's nightly (.github/workflows/maps-tests.yml);
- local: never in CI.
A test with no line runs on every pull request, as core does (and
tools/test_ci_select.py fails until it has one).

A test's paths are found, not kept by hand: the repository paths written in
the test (strings like "source/game/players.c", or ROOT / "port" / "linux"
chains), its harness or fuzz files, the tools modules it imports, and the
paths after its name in the buckets file. A changed header counts as a
change to every file that includes it, however deep (by #include; a name
that resolves to no one file matches every file of that name, to be safe),
and a changed tools module as one to every module that imports it. A test
whose paths come out empty runs every time, and a changed file that no
test's paths name, directly or through the files that include or import
it (documentation aside), runs every test.

Only a pull request is filtered (tier A). The merge queue (B), pushes to
main and release branches (C) and runs started by hand (D) run every test
but the maps and local ones, so a path a test missed shows up before a
merge, never after a release. Some files change how every test runs (this
script, the workflow, the harness's shared parts, the prefix header): a
pull request that changes one runs them all too.

    python3 tools/ci_select_tests.py --event pull_request --changed changed.json
    python3 tools/ci_select_tests.py --event push

--changed is GitHub's list of a pull request's files (filename,
previous_filename, patch). The output, on stdout, is JSON: {"parts": [...]},
one entry a linux-tests runner, each with its "pytest", "harness" and
"scripts" files and "update_install", and "why"; with --explain, the reason
each test was chosen goes to stderr.
"""

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BUCKETS_FILE = "tools/ci_test_buckets.txt"
PYTEST_LIST = "tools/ci_tests.txt"
SCRIPT_LIST = "tools/ci_test_scripts.txt"
HARNESS_TESTS = "tools/harness/tests"
UPDATE_INSTALL = "tools/test_update_install.py"
BUCKETS = ("core", "area", "security", "maps", "local")

# (a change to one of these changes how every test runs: all of them run.
# The build's plumbing, the workflows, the shared headers every unit sees)
EVERYTHING = (
    ".github/workflows/*",
    "configure.py",
    "VERSION",
    "tools/ninja_syntax.py",
    "tools/*_build.py",
    "tools/*_sysroot.py",
    "tools/*_sysroot.cmake",
    "tools/version.py",
    "tools/download.py",
    "tools/third_party_notices.py",
    "source/cseries/*.h",
    "tools/ci_select_tests.py",
    BUCKETS_FILE,
    "tools/fuzz_support.py",
    "tools/harness/__init__.py",
    "tools/harness/audio.py",
    "tools/harness/render_types.py",
    "tools/harness/include/*",
    "port/linux/include/halo_linux_prefix.h",
    "conftest.py",
    "tools/conftest.py",
)

# (folders too broad to say anything: a test that names one of these and
# nothing narrower depends on it, else only on the narrower ones)
BROAD = {"source", "port", "server", "tools", "docs", "port/linux", "port/linux/src", "port/linux/game",
         "port/linux/include", "port/linux/tests", "port/assets", "port/assets/menus", "port/console",
         "port/windows/src"}

# (the pytest shards' weights: the fuzzers and a few long ones by their
# seconds on a runner, every other file as 2)
WEIGHTS = {
    "tools/test_map_fuzz.py": 200, "tools/test_host_fuzz.py": 180, "tools/test_steam_content.py": 160,
    "tools/test_server_control.py": 90, "tools/test_delta_peer.py": 70, "tools/test_workshop_list.py": 25,
    "tools/test_steam_bridge.py": 20,
}
SHARD_WEIGHT = 300
MAX_SHARDS = 3

TOP = r"(?:source|port|server|tools|third_party|docs|shaders|data|assets|include|\.github)"
STRING_PATH = re.compile(r"""["'/ (](""" + TOP + r"""/[A-Za-z0-9_./*-]+)""")
CHAIN = re.compile(r"""ROOT((?:\s*/\s*["'][A-Za-z0-9_.*-]+["'])+)""")
CHAIN_PART = re.compile(r"""["']([A-Za-z0-9_.*-]+)["']""")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
IMPORT = re.compile(r"^\s*(?:from\s+(tools\.)?([A-Za-z_0-9]+)(?:\.[A-Za-z_0-9.]+)?\s+import\s+([A-Za-z_0-9, ()]+)"
                    r"|import\s+(tools\.)?([A-Za-z_0-9]+))", re.M)
C_SUFFIXES = (".c", ".h", ".inl", ".inc", ".cpp", ".hpp", ".cc", ".m", ".mm")


def read_list(path):
    """a test list's files, comments and blank lines left out"""
    lines = (ROOT / path).read_text(encoding="utf-8").splitlines()
    return [line.strip() for line in lines if line.strip() and not line.lstrip().startswith("#")]


def read_buckets(path=BUCKETS_FILE):
    """{test: (bucket, [extra paths])} from the buckets file"""
    buckets = {}
    for number, line in enumerate((ROOT / path).read_text(encoding="utf-8").splitlines(), 1):
        words = line.split("#", 1)[0].split()
        if not words:
            continue
        if len(words) < 2 or words[0] not in BUCKETS:
            raise ValueError(f"{path}:{number}: '<bucket> <test> [paths...]', bucket one of {', '.join(BUCKETS)}")
        if words[1] in buckets:
            raise ValueError(f"{path}:{number}: {words[1]} twice")
        buckets[words[1]] = (words[0], words[2:])
    return buckets


def tracked():
    """the repository's files (git's index; the tree's files without git)"""
    try:
        out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout
        return sorted(name for name in out.decode("utf-8", "replace").split("\0") if name)
    except (OSError, subprocess.CalledProcessError):
        return sorted(str(path.relative_to(ROOT)) for path in ROOT.rglob("*") if path.is_file() and ".git" not in path.parts)


def every_test():
    """each test file CI knows of: the two lists, the harness's, the updater's"""
    tests = read_list(PYTEST_LIST) + read_list(SCRIPT_LIST)
    tests += sorted(str(path.relative_to(ROOT)) for path in (ROOT / HARNESS_TESTS).glob("test_*.py"))
    tests.append(UPDATE_INSTALL)
    seen = set()
    return [test for test in tests if not (test in seen or seen.add(test))]


class Repository:
    """the files, and who includes or imports whom"""

    def __init__(self, files=None, text=None):
        self.files = sorted(files if files is not None else tracked())
        self.set = set(self.files)
        self._text = text
        self._by_suffix = {}
        for name in self.files:
            parts = name.split("/")
            for i in range(len(parts)):
                self._by_suffix.setdefault("/".join(parts[i:]), []).append(name)
        self._includers = None
        self._importers = None
        self._matches = {}

    def text(self, name):
        if self._text is not None:
            return self._text.get(name, "")
        try:
            return (ROOT / name).read_text(encoding="utf-8", errors="replace")
        except OSError:
            return ""

    def resolve_include(self, including, name):
        """the files an #include names: beside the file that includes it, else
        every file whose path ends with the name"""
        name = name.replace("\\", "/")
        beside = os.path.normpath(os.path.join(os.path.dirname(including), name)).replace("\\", "/")
        if beside in self.set:
            return [beside]
        return self._by_suffix.get(name.lstrip("./"), [])

    def resolve_import(self, name):
        for candidate in (f"tools/{name}.py", f"tools/{name}/__init__.py", f"tools/harness/{name}.py"):
            if candidate in self.set:
                return [candidate]
        return []

    def imports(self, name):
        found = []
        for match in IMPORT.finditer(self.text(name)):
            module = match.group(2) or match.group(5)
            if module == "tools" and match.group(3):
                for item in re.split(r"[\s,()]+", match.group(3)):
                    if item and item != "as":
                        found += self.resolve_import(item)
            elif module:
                found += self.resolve_import(module)
        return found

    def _graphs(self):
        if self._includers is not None:
            return
        self._includers, self._importers = {}, {}
        for name in self.files:
            if name.endswith(C_SUFFIXES):
                for include in INCLUDE.findall(self.text(name)):
                    for target in self.resolve_include(name, include):
                        if target != name:
                            self._includers.setdefault(target, set()).add(name)
            elif name.endswith(".py") and name.startswith("tools/"):
                for target in self.imports(name):
                    if target != name:
                        self._importers.setdefault(target, set()).add(name)

    def affected(self, changed):
        """changed, and every file that includes or imports one of them, however deep"""
        self._graphs()
        result = set(changed)
        pending = list(changed)
        while pending:
            name = pending.pop()
            for user in self._includers.get(name, set()) | self._importers.get(name, set()):
                if user not in result:
                    result.add(user)
                    pending.append(user)
        return result

    def matches(self, pattern):
        """the files a path names: itself, a folder's files, or a glob's"""
        if pattern not in self._matches:
            self._matches[pattern] = self._match(pattern)
        return self._matches[pattern]

    def _match(self, pattern):
        pattern = pattern.rstrip("/")
        if pattern.endswith("/**"):
            pattern = pattern[:-3]
        if any(character in pattern for character in "*?["):
            return {name for name in self.files if fnmatch.fnmatch(name, pattern)}
        if pattern in self.set:
            return {pattern}
        return {name for name in self.files if name.startswith(pattern + "/")}


def written_paths(text):
    """the repository paths a test names"""
    paths = set(STRING_PATH.findall(text))
    for chain in CHAIN.findall(text):
        paths.add("/".join(CHAIN_PART.findall(chain)))
    return {path.rstrip("/.").replace("//", "/") for path in paths if path}


def test_paths(repo, test, extra=()):
    """the paths a test depends on (patterns), its own file first"""
    text = repo.text(test)
    named = {path for path in written_paths(text) if path != test and repo.matches(path)}
    narrow = {path for path in named if path not in BROAD}
    paths = [test, *sorted(narrow or named)]
    if test.startswith(HARNESS_TESTS + "/test_"):
        stem = Path(test).stem[len("test_"):]
        paths += [name for name in (f"{HARNESS_TESTS}/{stem}.c",) if name in repo.set]
    # (a fuzzer's targets: each folder's target.json names its harness and units)
    if test == "tools/test_host_fuzz.py":
        for target in repo.matches("tools/tests/host_fuzz/*/target.json"):
            paths.append(str(Path(target).parent))
            try:
                spec = json.loads(repo.text(target) or "{}")
            except ValueError:
                continue
            for key in ("harness", "sources", "game_sources", "includes"):
                values = spec.get(key, [])
                for value in [values] if isinstance(values, str) else values:
                    value = value if isinstance(value, str) else value.get("source", "")
                    if value:
                        paths.append(value)
    paths += repo.imports(test)
    paths += list(extra)
    return list(dict.fromkeys(paths))


def changed_files(entries):
    """the files of GitHub's pull request file list (names and old names)"""
    names = []
    for entry in entries:
        names.append(entry["filename"])
        if entry.get("previous_filename"):
            names.append(entry["previous_filename"])
    return names


def added_to_lists(entries):
    """the tests a pull request's patch adds to the two lists"""
    added = []
    for entry in entries:
        if entry["filename"] in (PYTEST_LIST, SCRIPT_LIST):
            for line in (entry.get("patch") or "").splitlines():
                if line.startswith("+") and not line.startswith("+++"):
                    name = line[1:].strip()
                    if name and not name.startswith("#"):
                        added.append(name)
    return added


def select(event, entries=None, repo=None, buckets=None, tests=None, explain=None):
    """{test: why} for the run: every core, area and security test unless a
    pull request's files say fewer"""
    repo = repo or Repository()
    buckets = read_buckets() if buckets is None else buckets
    tests = every_test() if tests is None else tests
    note = explain or (lambda message: None)
    runnable = [test for test in tests if buckets.get(test, ("core",))[0] not in ("maps", "local")]
    if event != "pull_request" or entries is None:
        return {test: f"every test ({event})" for test in runnable}
    changed = changed_files(entries)
    if not changed:
        return {test: "no files listed" for test in runnable}
    for name in changed:
        if any(fnmatch.fnmatch(name, pattern) for pattern in EVERYTHING):
            return {test: f"{name} changed" for test in runnable}
    affected = repo.affected(changed)
    added = set(added_to_lists(entries))
    chosen = {}
    # (the lists and the tests themselves, the maps and local ones among them)
    claimed = {PYTEST_LIST, SCRIPT_LIST, *tests, *buckets}
    for test in runnable:
        bucket, extra = buckets.get(test, (None, []))
        if bucket is None:
            chosen[test] = "no bucket (tools/ci_test_buckets.txt)"
            continue
        paths = test_paths(repo, test, extra)
        if bucket == "core":
            chosen[test] = "core"
            for pattern in paths:
                claimed |= repo.matches(pattern) or {pattern}
            continue
        if test in added:
            chosen[test] = "added to a list"
            continue
        if len(paths) == 1:
            chosen[test] = "no paths found: every time"
            continue
        hit = None
        for pattern in paths:
            files = repo.matches(pattern) or {pattern}
            claimed |= files
            if hit is None:
                hit = next((name for name in sorted(affected & files)), None)
        if hit:
            chosen[test] = f"{hit} changed"
    # (a file no test names: no one can say which tests it reaches, so all of
    # them run; documentation that no test reads reaches none)
    # (named: itself, or a file that includes or imports it, however deep)
    unclaimed = [name for name in changed
                 if not name.endswith(".md") and not name.startswith("docs/") and not (repo.affected([name]) & claimed)]
    if unclaimed:
        note(f"::notice::no test's paths name {', '.join(unclaimed[:5])}"
             f"{' and more' if len(unclaimed) > 5 else ''}: every test runs")
        return {test: f"{unclaimed[0]}: no test names it" for test in runnable}
    return chosen


def parts(chosen, tests=None, update_install=UPDATE_INSTALL):
    """the linux-tests runners: pytest shards dealt longest first to the
    lightest, then one runner for the harness, the scripts and the updater"""
    pytest_list = read_list(PYTEST_LIST)
    script_list = read_list(SCRIPT_LIST)
    pytest_files = [test for test in pytest_list if test in chosen]
    scripts = [test for test in script_list if test in chosen]
    harness = sorted(test for test in chosen if test.startswith(HARNESS_TESTS + "/"))
    total = sum(WEIGHTS.get(test, 2) for test in pytest_files)
    count = max(1, min(MAX_SHARDS, -(-total // SHARD_WEIGHT))) if pytest_files else 0
    shards = [[] for _ in range(count)]
    load = [0] * count
    for test in sorted(pytest_files, key=lambda test: -WEIGHTS.get(test, 2)):
        best = load.index(min(load))
        shards[best].append(test)
        load[best] += WEIGHTS.get(test, 2)
    result = [{"name": f"pytest {i + 1} of {count}", "pytest": " ".join(sorted(shard)), "harness": "",
               "scripts": "", "update_install": False} for i, shard in enumerate(shards)]
    if harness or scripts or update_install in chosen:
        result.append({"name": "other", "pytest": "", "harness": " ".join(harness), "scripts": " ".join(scripts),
                       "update_install": update_install in chosen})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--event", required=True, help="github.event_name")
    parser.add_argument("--changed", help="a pull request's files, as GitHub lists them (JSON)")
    parser.add_argument("--explain", action="store_true", help="each test chosen, and why, on stderr")
    options = parser.parse_args()
    entries = None
    if options.changed:
        text = Path(options.changed).read_text(encoding="utf-8").strip()
        # (a JSON list, or one object a line, as gh api --paginate --jq '.[]' writes them)
        entries = json.loads(text) if text.startswith("[") else [json.loads(line) for line in text.splitlines() if line]
    chosen = select(options.event, entries, explain=lambda message: print(message, file=sys.stderr))
    if options.explain:
        for test, why in sorted(chosen.items()):
            print(f"{test}: {why}", file=sys.stderr)
    every = select("push")
    why = "every test" if set(chosen) >= set(every) else f"{len(chosen)} of {len(every)} tests"
    print(json.dumps({"why": why, "count": len(chosen), "parts": parts(chosen)}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
