"""tools/ci_select_tests.py: every test CI runs has a bucket, and a pull
request's files choose the tests they should (a header's includers, a
module's importers, the files that change every test)."""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools import ci_select_tests as ci  # noqa: E402


def test_every_test_has_a_bucket():
    buckets = ci.read_buckets()
    missing = [test for test in ci.every_test() if test not in buckets]
    assert not missing, f"no line in {ci.BUCKETS_FILE} (core, area, security, maps or local): {missing}"


def test_buckets_file_is_sorted():
    names = [line.split()[1] for line in (ROOT / ci.BUCKETS_FILE).read_text(encoding="utf-8").splitlines()
             if line.strip() and not line.startswith("#")]
    assert names == sorted(names), f"{ci.BUCKETS_FILE}: keep the lines sorted by test"


def test_every_run_but_a_pull_request_runs_everything():
    tests = ci.every_test()
    buckets = ci.read_buckets()
    expected = {test for test in tests if buckets.get(test, ("core",))[0] not in ("maps", "local")}
    for event in ("merge_group", "push", "workflow_dispatch"):
        assert set(ci.select(event, [{"filename": "source/game/players.c"}])) == expected, event


def fake(files):
    """a repository of these files {name: text}"""
    return ci.Repository(files=list(files), text=files)


FILES = {
    "source/a/thing.h": "",
    "source/a/thing_extra.h": '#include "thing.h"\n',
    "source/a/thing.c": '#include "thing_extra.h"\n',
    "source/b/other.c": "",
    "tools/helper.py": "",
    "tools/test_thing.py": 'SOURCE = "source/a/thing.c"\n',
    "tools/test_other.py": 'from tools import helper\nSOURCE = ROOT / "source" / "b" / "other.c"\n',
    "tools/test_core.py": "",
    "tools/test_unknown.py": 'SOURCE = "source/b/other.c"\n',
    "tools/test_nothing.py": "",
    "tools/test_maps.py": 'SOURCE = "source/a/thing.c"\n',
    "docs/thing.md": "",
}
BUCKETS = {"tools/test_thing.py": ("area", []), "tools/test_other.py": ("security", []),
           "tools/test_core.py": ("core", []), "tools/test_nothing.py": ("area", []),
           "tools/test_maps.py": ("maps", [])}
TESTS = ["tools/test_thing.py", "tools/test_other.py", "tools/test_core.py", "tools/test_unknown.py",
         "tools/test_nothing.py", "tools/test_maps.py"]


def chosen(*names, patch=None):
    entries = [{"filename": name} for name in names]
    if patch:
        entries.append(patch)
    return set(ci.select("pull_request", entries, repo=fake(FILES), buckets=BUCKETS, tests=TESTS))


# (every pull request: core, a test without a bucket, one whose paths came out empty)
ALWAYS = {"tools/test_core.py", "tools/test_unknown.py", "tools/test_nothing.py"}


def test_a_header_reaches_the_tests_of_its_includers():
    assert chosen("source/a/thing.h") == ALWAYS | {"tools/test_thing.py"}


def test_an_imported_module_reaches_its_importers():
    assert chosen("tools/helper.py") == ALWAYS | {"tools/test_other.py"}


def test_a_path_written_as_a_chain():
    assert chosen("source/b/other.c") == ALWAYS | {"tools/test_other.py"}


def test_a_changed_test_runs():
    assert chosen("tools/test_thing.py") == ALWAYS | {"tools/test_thing.py"}


def test_documentation_alone_runs_the_always_set():
    assert chosen("docs/thing.md") == ALWAYS


def test_maps_tests_never_run_in_build():
    assert "tools/test_maps.py" not in chosen("source/a/thing.c")
    assert "tools/test_maps.py" not in set(ci.select("push", None, repo=fake(FILES), buckets=BUCKETS, tests=TESTS))


def test_a_test_added_to_a_list_runs():
    patch = {"filename": ci.PYTEST_LIST, "patch": "@@ -1 +1,2 @@\n tools/test_core.py\n+tools/test_thing.py\n"}
    assert "tools/test_thing.py" in chosen(patch=patch)


def test_the_workflow_or_the_selector_runs_everything():
    for name in (".github/workflows/build.yml", "tools/ci_select_tests.py", "tools/harness/include/harness.h",
                 "port/linux/include/halo_linux_prefix.h"):
        assert chosen(name) == set(TESTS) - {"tools/test_maps.py"}, name


def test_a_file_no_test_names_runs_everything():
    files = dict(FILES, **{"source/c/lonely.c": ""})
    picked = set(ci.select("pull_request", [{"filename": "source/c/lonely.c"}], repo=fake(files), buckets=BUCKETS,
                           tests=TESTS))
    assert picked == set(TESTS) - {"tools/test_maps.py"}


def test_a_core_tests_paths_count_as_named():
    files = dict(FILES, **{"tools/test_core.py": 'SOURCE = "source/c/core_only.c"\n', "source/c/core_only.c": ""})
    picked = set(ci.select("pull_request", [{"filename": "source/c/core_only.c"}], repo=fake(files),
                           buckets=BUCKETS, tests=TESTS))
    assert picked == ALWAYS


def test_build_plumbing_and_shared_headers_run_everything():
    for name in ("configure.py", "tools/linux_build.py", "tools/linux_sysroot.cmake", "source/cseries/cseries.h",
                 ".github/workflows/fuzz.yml"):
        assert set(ci.select("pull_request", [{"filename": name}])) == set(ci.select("push")), name


def test_parts_split_long_files_apart():
    selected = {"tools/test_map_fuzz.py": "", "tools/test_host_fuzz.py": "", "tools/test_steam_content.py": "",
                "tools/test_port_config.py": ""}
    parts = ci.parts(selected)
    shards = [part for part in parts if part["pytest"]]
    assert len(shards) == 2
    assert not any("test_map_fuzz" in part["pytest"] and "test_host_fuzz" in part["pytest"] for part in shards)
    assert parts[-1]["name"] == "other" and "tools/test_port_config.py" in parts[-1]["scripts"]


def test_real_selection_is_narrow_for_one_unit():
    """one source file's change runs far fewer than every test, and the core ones"""
    entries = [{"filename": "port/linux/src/workshop_list.c"}]
    picked = ci.select("pull_request", entries)
    every = ci.select("push")
    assert "tools/test_workshop_list.py" in picked
    assert len(picked) < len(every) // 3
    core = {test for test, (bucket, _) in ci.read_buckets().items() if bucket == "core" and test in every}
    assert core <= set(picked)
