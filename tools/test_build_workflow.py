#!/usr/bin/env python3
"""The Build workflow's guards for what ChupathingyCE signs
(.github/workflows/build.yml):

- the gate job: a run started with a commit builds it only when it is a
  full SHA on main or on a release-* branch, never another commit of the
  repository's network (a fork's pull request). Its script is run here
  against a repository made in the test;
- every other job waits for the gate;
- no cache step saves by itself (actions/cache), every restore is skipped
  in a run with a version, and every save happens only in a run of main's
  own head without a version;
- the plan job: each run's tier (A on a pull request, B in the merge
  queue, C on main, batches and plumbing changes, D by hand) and the
  platform jobs it gets; ci-ok's verdict; cache-prune never deleting
  main's newest cache of a kind. Their scripts are run here with a fake gh;
- the Linux jobs' Ubuntu packages: every job installs them through
  CI_APT_INSTALL, with its cache key, restore and save; the script is run
  here with a fake sudo, apt-get and curl (from the cache with nothing
  downloaded, without one, with a mirror that fails, with a changed key);
- rerun-unacquired.yml: a failed Build's jobs run again, once, only when
  one of them never had a runner.

Text only (no YAML library), no network: python3 -m pytest tools/test_build_workflow.py"""

import os
import re
import subprocess
from pathlib import Path

import pytest

WORKFLOW = Path(__file__).resolve().parent.parent / ".github" / "workflows" / "build.yml"
TEXT = WORKFLOW.read_text()
LINES = TEXT.splitlines()


def jobs() -> dict:
    """each job's lines, by name"""
    found, name = {}, None
    inside = False
    for line in LINES:
        if line == "jobs:":
            inside = True
            continue
        if not inside:
            continue
        match = re.match(r"^  ([a-z0-9_-]+):$", line)
        if match:
            name = match.group(1)
            found[name] = []
        elif name:
            found[name].append(line)
    return found


def steps(lines: list) -> list:
    """a job's steps, each its lines"""
    found = []
    for line in lines:
        if line.startswith("      - "):
            found.append([line])
        elif found and (line.startswith("        ") or not line.strip()):
            found[-1].append(line)
    return found


def condition(step: list) -> str:
    for line in step:
        if line.startswith("        if: "):
            return line[len("        if: "):]
    return ""


def test_every_job_waits_for_the_gate():
    every = jobs()
    assert "gate" in every
    for name, lines in every.items():
        if name == "gate":
            continue
        needs = [line for line in lines if line.startswith("    needs: ")]
        assert needs, f"{name} does not wait for the gate"
        assert re.search(r"\bgate\b", needs[0]), f"{name}: {needs[0].strip()}"


def test_no_cache_saves_by_itself():
    assert "uses: actions/cache@" not in TEXT


def test_restores_and_saves_are_guarded():
    restores = saves = 0
    for name, lines in jobs().items():
        for step in steps(lines):
            if "uses: actions/cache/restore@" in step[0]:
                restores += 1
                assert condition(step).startswith("env.CACHE_RESTORE == 'true'"), f"{name}: {step[0].strip()}"
            if "uses: actions/cache/save@" in step[0]:
                saves += 1
                assert "env.CACHE_SAVE == 'true'" in condition(step), f"{name}: {step[0].strip()}"
                assert "github.ref == 'refs/heads/main'" not in condition(step)
    assert restores and saves


def test_the_flags():
    env = {line.split(":", 1)[0].strip(): line.split(":", 1)[1].strip() for line in LINES
           if re.match(r"^  CACHE_(RESTORE|SAVE): ", line)}
    assert env["CACHE_RESTORE"] == "${{ inputs.version == '' && 'true' || 'false' }}"
    save = env["CACHE_SAVE"]
    for part in ("github.ref == 'refs/heads/main'", "inputs.version == ''",
                 "(inputs.commit == '' || inputs.commit == github.sha)"):
        assert part in save


def gate_script() -> str:
    """the gate's run: script, dedented"""
    return job_script("gate")


def job_script(name: str) -> str:
    """the job's (first) run: script, dedented"""
    lines = jobs()[name]
    start = next(index for index, line in enumerate(lines) if line.strip() == "run: |")
    script = []
    for line in lines[start + 1:]:
        if line and not line.startswith("          "):
            break
        script.append(line[10:])
    return "\n".join(script) + "\n"


def git(folder: Path, *arguments: str) -> str:
    environment = {**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.invalid",
                   "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.invalid",
                   "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}
    return subprocess.run(["git", "-C", str(folder), *arguments], check=True, capture_output=True, text=True,
                          env=environment).stdout.strip()


@pytest.fixture(scope="module")
def repository(tmp_path_factory):
    """main (two commits), a release branch off it with one of its own, and a
    branch nobody merged"""
    folder = tmp_path_factory.mktemp("origin")
    git(folder, "init", "-q", "-b", "main")
    git(folder, "commit", "-q", "--allow-empty", "-m", "one")
    old = git(folder, "rev-parse", "HEAD")
    git(folder, "commit", "-q", "--allow-empty", "-m", "two")
    head = git(folder, "rev-parse", "HEAD")
    git(folder, "checkout", "-q", "-b", "release-0.9.0", old)
    git(folder, "commit", "-q", "--allow-empty", "-m", "fix")
    release = git(folder, "rev-parse", "HEAD")
    git(folder, "checkout", "-q", "-b", "fork-pr", head)
    git(folder, "commit", "-q", "--allow-empty", "-m", "not merged")
    other = git(folder, "rev-parse", "HEAD")
    git(folder, "checkout", "-q", "-b", "release/old", head)
    git(folder, "commit", "-q", "--allow-empty", "-m", "outside the ruleset")
    slashed = git(folder, "rev-parse", "HEAD")
    git(folder, "checkout", "-q", "main")
    return {"folder": folder, "old": old, "head": head, "release": release, "other": other, "slashed": slashed}


def gate(repository, commit: str, work: Path) -> subprocess.CompletedProcess:
    environment = {**os.environ, "COMMIT": commit, "REPOSITORY": f"file://{repository['folder']}",
                   "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}
    return subprocess.run(["bash", "-e", "-c", gate_script()], cwd=work, capture_output=True, text=True,
                          env=environment)


@pytest.mark.parametrize("which", ["old", "head", "release"])
def test_gate_takes_mains_and_release_branches(repository, tmp_path, which):
    result = gate(repository, repository[which], tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("which", ["other", "slashed"])
def test_gate_refuses_a_commit_on_no_such_branch(repository, tmp_path, which):
    """an unmerged branch, and release/... (the ruleset covers release-* only)"""
    result = gate(repository, repository[which], tmp_path)
    assert result.returncode != 0
    assert "not on main or a release branch" in result.stdout


@pytest.mark.parametrize("commit", ["main", "release-0.9.0", "HEAD", "0" * 39, "0" * 41, "A" * 40, "short"])
def test_gate_refuses_anything_but_a_full_sha(repository, tmp_path, commit):
    result = gate(repository, commit, tmp_path)
    assert result.returncode != 0
    assert "not a full SHA" in result.stdout


def test_gate_refuses_an_abbreviated_main_commit(repository, tmp_path):
    result = gate(repository, repository["head"][:12], tmp_path)
    assert result.returncode != 0


PLATFORMS = ["linux64", "freebsd64", "server", "server-container", "server-windows", "android64", "windows",
             "windows64", "macos"]
# (the jobs that only some runs have, as the plan job's run output names them)
TIER_B = ["linux64", "freebsd64", "windows", "windows64", "macos", "android64"]
TIER_C = TIER_B + ["server_windows", "server_container"]


@pytest.fixture
def fake_gh(tmp_path):
    """a gh that answers from files: files (a pull request's or a merge
    group's comparison), caches (JSON lines), and logs each call"""
    folder = tmp_path / "bin"
    folder.mkdir()
    gh = folder / "gh"
    gh.write_text(f"""#!/bin/sh
echo "$*" >> {tmp_path}/calls
case "$*" in
  *"-X DELETE"*) exit 0 ;;
  *"/files"*|*"/compare/"*) cat {tmp_path}/files ;;
  *"/actions/caches"*) cat {tmp_path}/caches ;;
  *) exit 1 ;;
esac
""")
    gh.chmod(0o755)
    for name in ("files", "caches", "calls"):
        (tmp_path / name).write_text("")
    return tmp_path


def plan(fake_gh, event="pull_request", files=(), head_ref="pr/change") -> dict:
    import json
    (fake_gh / "files").write_text("".join(f"{name}\n" for name in files))
    output = fake_gh / "output"
    output.write_text("")
    environment = {**os.environ, "PATH": f"{fake_gh / 'bin'}:{os.environ['PATH']}", "GITHUB_OUTPUT": str(output),
                   "EVENT": event, "HEAD_REF": head_ref if event == "pull_request" else "", "NUMBER": "1",
                   "BASE_SHA": "a" * 40, "HEAD_SHA": "b" * 40, "REPOSITORY": "ChupathingyCE/chupathingyce"}
    result = subprocess.run(["bash", "-e", "-c", job_script("plan")], capture_output=True, text=True, env=environment)
    assert result.returncode == 0, result.stdout + result.stderr
    outputs = dict(line.split("=", 1) for line in output.read_text().splitlines())
    outputs["run"] = json.loads(outputs["run"])
    assert sorted(outputs["run"]) == sorted(TIER_C)
    return outputs


def running(outputs: dict) -> list:
    return sorted(job for job, runs in outputs["run"].items() if runs)


@pytest.mark.parametrize("event,tier", [("push", "c"), ("workflow_dispatch", "d")])
def test_plan_builds_everything_off_pull_requests(fake_gh, event, tier):
    outputs = plan(fake_gh, event=event)
    assert outputs["tier"] == tier
    assert running(outputs) == sorted(TIER_C)
    assert outputs["server_archs"] == '["x86","x64","arm64"]'


def test_plan_tier_a_for_shared_code(fake_gh):
    outputs = plan(fake_gh, files=["source/game/game.c", "port/linux/src/posix_net.c"])
    assert outputs["tier"] == "a" and running(outputs) == [] and outputs["server_archs"] == '["x64"]'


def test_plan_documentation_alone(fake_gh):
    for event in ("pull_request", "merge_group"):
        outputs = plan(fake_gh, event=event, files=["docs/delta/registry.md", "README.md"])
        assert running(outputs) == [] and outputs["server_archs"] == '["x64"]'


@pytest.mark.parametrize("files,jobs", [
    (["port/windows/src/win32_net.c"], ["server_windows", "windows", "windows64"]),
    (["tools/windows_build.py"], ["server_windows", "windows", "windows64"]),
    (["port/android/app/build.gradle"], ["android64"]),
    (["port/macos/main.m"], ["macos"]),
    (["port/unix/unix_main.c"], ["freebsd64"]),
    (["server/src/main.c"], []),
    (["port/linux/src/d3d8_bgfx.inl"], ["linux64"]),
])
def test_plan_adds_the_platform_changed(fake_gh, files, jobs):
    outputs = plan(fake_gh, files=files)
    assert outputs["tier"] == "a"
    assert running(outputs) == jobs
    assert outputs["server_archs"] == '["x64"]'


@pytest.mark.parametrize("files,extra", [
    (["source/game/game.c"], []),
    (["server/src/main.c"], ["server_windows"]),
    (["port/windows/src/win32_net.c"], ["server_windows"]),
])
def test_plan_merge_queue_is_tier_b(fake_gh, files, extra):
    outputs = plan(fake_gh, event="merge_group", files=files)
    assert outputs["tier"] == "b"
    assert running(outputs) == sorted(TIER_B + extra)
    assert outputs["server_archs"] == '["x64"]'
    assert "compare/" + "a" * 40 + "..." + "b" * 40 in (fake_gh / "calls").read_text()


@pytest.mark.parametrize("kind", ["batch", "plumbing"])
def test_plan_full_matrix_on_a_pull_request(fake_gh, kind):
    outputs = plan(fake_gh, files=["tools/ci_build.py" if kind == "plumbing" else "source/game/game.c"],
                   head_ref="batch/2026-11-13" if kind == "batch" else "pr/change")
    assert outputs["tier"] == "c" and running(outputs) == sorted(TIER_C)
    assert outputs["server_archs"] == '["x86","x64","arm64"]'


def test_plan_ignores_the_old_full_label():
    assert "ci: full" not in job_script("plan")
    assert not (WORKFLOW.parent / "full-build-label.yml").exists()


def test_plan_falls_back_to_tier_b_when_github_does_not_answer(fake_gh):
    (fake_gh / "bin" / "gh").write_text("#!/bin/sh\nexit 1\n")
    outputs = plan(fake_gh)
    assert running(outputs) == sorted(TIER_B)


def ci_ok(needs: dict, run, event: str) -> bool:
    import json
    environment = {**os.environ, "NEEDS": json.dumps({name: {"result": result} for name, result in needs.items()}),
                   "RUN": run if isinstance(run, str) else json.dumps({job: job in run for job in TIER_C}),
                   "TIER": "", "EVENT": event}
    return subprocess.run(["bash", "-e", "-c", job_script("ci-ok")], capture_output=True, text=True,
                          env=environment).returncode == 0


def needs(platforms: str, **changes) -> dict:
    found = {"gate": "success", "plan": "success", "info": "skipped", "linux": "success", "linux-tests": "success",
             "linux-big-endian": "success", **{name: platforms for name in PLATFORMS}, "server": "success"}
    found.update({name.replace("_", "-"): result for name, result in changes.items()})
    return found


@pytest.mark.skipif(subprocess.run(["sh", "-c", "command -v jq"], capture_output=True).returncode != 0,
                    reason="no jq")
def test_ci_ok():
    # (tier A: the platforms skipped; tier B: its six passed, the rest skipped)
    assert ci_ok(needs("skipped"), [], "pull_request")
    assert ci_ok(needs("skipped", **{job: "success" for job in TIER_B}), TIER_B, "merge_group")
    assert ci_ok(needs("success"), TIER_C, "push")
    assert ci_ok(needs("success", info="success", freebsd64="failure"), TIER_C, "workflow_dispatch")
    # (a platform the plan gave the run skipped, one it left out run anyway
    # counts too; a failed test, the server failing, FreeBSD failing off a
    # dispatch, a dispatch without its build-info, a failed gate)
    assert not ci_ok(needs("skipped"), TIER_B, "merge_group")
    assert not ci_ok(needs("skipped", macos="failure"), [], "pull_request")
    assert not ci_ok(needs("skipped", linux_tests="failure"), [], "pull_request")
    assert not ci_ok(needs("skipped", server="failure"), [], "pull_request")
    assert not ci_ok(needs("success", freebsd64="failure"), TIER_C, "push")
    assert not ci_ok(needs("success"), TIER_C, "workflow_dispatch")
    assert not ci_ok(needs("skipped", gate="failure", plan="skipped"), "{}", "pull_request")
    # (a canceled run: ci-ok still runs, and fails)
    assert not ci_ok(needs("skipped", linux_tests="cancelled"), [], "merge_group")
    assert "    if: always()" in jobs()["ci-ok"]


def test_cache_prune_keeps_mains_newest(fake_gh):
    import json
    commit = lambda digit: digit * 40  # noqa: E731
    sha256 = lambda digit: digit * 64  # noqa: E731
    caches = [
        (1, f"ccache-linux-ubuntu-clang22-{commit('1')}", "refs/heads/main", "2026-11-10T00:00:00Z"),
        (2, f"ccache-linux-ubuntu-clang22-{commit('2')}", "refs/heads/main", "2026-11-12T00:00:00Z"),
        (3, f"ccache-linux64-ubuntu-clang22-{commit('1')}", "refs/heads/main", "2026-11-10T00:00:00Z"),
        (4, f"bgfx-source-Linux-{sha256('a')}", "refs/heads/main", "2026-11-01T00:00:00Z"),
        (5, f"bgfx-source-Linux-{sha256('b')}", "refs/heads/main", "2026-11-11T00:00:00Z"),
        (6, f"bgfx-source-Windows-{sha256('a')}", "refs/heads/main", "2026-11-01T00:00:00Z"),
        (7, "freebsd-vm-image-15.1", "refs/pull/1019/merge", "2026-11-12T00:00:00Z"),
        (8, f"ccache-linux-ubuntu-clang22-{commit('3')}", "refs/heads/release-0.9.0", "2026-11-09T00:00:00Z"),
        (9, "no-hash-here", "refs/heads/main", "2026-11-01T00:00:00Z"),
    ]
    (fake_gh / "caches").write_text("".join(json.dumps({"id": number, "key": key, "ref": ref, "created_at": created,
                                                        "size_in_bytes": 2**20}) + "\n"
                                            for number, key, ref, created in caches))
    environment = {**os.environ, "PATH": f"{fake_gh / 'bin'}:{os.environ['PATH']}",
                   "REPOSITORY": "ChupathingyCE/chupathingyce"}
    result = subprocess.run(["bash", "-e", "-c", job_script("cache-prune")], capture_output=True, text=True,
                            env=environment)
    assert result.returncode == 0, result.stdout + result.stderr
    deleted = sorted(int(line.rsplit("/", 1)[1]) for line in (fake_gh / "calls").read_text().splitlines()
                     if "-X DELETE" in line)
    # (older main generations of a kind, and the pull request's; never 2, 3,
    # 5 or 6, main's newest of each, nor a release branch's or a key without
    # a hash)
    assert deleted == [1, 4, 7]


APT_JOBS = ["linux", "linux-tests", "linux-big-endian", "linux64"]


def apt_script() -> str:
    """CI_APT_INSTALL: the block the linux job writes once (&ci-apt-install), dedented"""
    start = next(index for index, line in enumerate(LINES) if line.endswith("CI_APT_INSTALL: &ci-apt-install |"))
    indent = len(LINES[start]) - len(LINES[start].lstrip()) + 2
    found = []
    for line in LINES[start + 1:]:
        if line and not line.startswith(" " * indent):
            break
        found.append(line[indent:])
    return "\n".join(found) + "\n"


def test_apt_only_through_the_shared_script():
    every = jobs()
    assert TEXT.count("&ci-apt-install") == 2  # (the anchor, and the comment naming it)
    for name, lines in every.items():
        text = "\n".join(lines)
        if name in APT_JOBS:
            assert 'bash -c "$CI_APT_INSTALL"' in text, name
            assert "APT_PACKAGES: >-" in text, name
            # (the key step and the install step, each with the script)
            uses = text.count("CI_APT_INSTALL: *ci-apt-install") + text.count("CI_APT_INSTALL: &ci-apt-install |")
            assert uses == 2, name
        else:
            assert "CI_APT_INSTALL" not in text, name
        # (apt-get nowhere but in the script)
        outside, inside = [], False
        for line in lines:
            if line.endswith("CI_APT_INSTALL: &ci-apt-install |"):
                inside = True
                continue
            if inside and line and not line.startswith(" " * 12):
                inside = False
            if not inside:
                outside.append(line)
        assert "apt-get" not in "\n".join(outside), f"{name} runs apt-get itself"
    # (only the steps that use it print it)
    assert not re.search(r"^  CI_APT_INSTALL:", TEXT, re.M)


@pytest.mark.parametrize("name", APT_JOBS)
def test_apt_cache_steps(name):
    job = steps(jobs()[name])
    names = [next((line.split("name: ", 1)[1] for line in step if "name: " in line), step[0].strip()) for step in job]
    key = names.index("The packages' cache key")
    restore = next(index for index, step in enumerate(job) if "id: apt-cache" in "\n".join(step))
    install = names.index("Install the toolchain")
    save = [index for index, step in enumerate(job)
            if "uses: actions/cache/save@" in step[0] and "steps.apt.outputs.key" in "\n".join(step)]
    assert key < restore < install and len(save) == 1 and save[0] > install
    restore_text, install_text = "\n".join(job[restore]), "\n".join(job[install])
    assert "restore-keys: " in restore_text
    assert "APT_CACHE_HIT: ${{ steps.apt-cache.outputs.cache-hit }}" in install_text
    assert "timeout-minutes: " in install_text
    assert "steps.apt-cache.outputs.cache-hit != 'true'" in condition(job[save[0]])
    # (a key that no other job's starts with: restore-keys is a prefix)
    prefix = re.search(r'echo "key=(apt-[a-z0-9-]+-)\$\(', "\n".join(job[key])).group(1)
    for other in APT_JOBS:
        if other != name:
            other_key = "\n".join(next(step for step in steps(jobs()[other]) if "The packages' cache key" in step[0]))
            assert not re.search(r'echo "key=' + re.escape(prefix), other_key), (name, other)


@pytest.fixture
def fake_apt(tmp_path):
    """sudo (with /etc under the test's folder), apt-get, curl, timeout and
    sleep that log their calls; apt-get's download fails as many times as
    the file download-failures says"""
    root, folder = tmp_path / "root", tmp_path / "bin"
    (root / "etc" / "apt" / "apt.conf.d").mkdir(parents=True)
    (root / "etc" / "apt" / "sources.list.d").mkdir(parents=True)
    folder.mkdir()
    (tmp_path / "download-failures").write_text("0")
    (tmp_path / "key").write_text("the key\n")
    scripts = {
        "sudo": f"""#!/usr/bin/env bash
args=()
for a in "$@"; do case "$a" in /etc/*) a="{root}$a" ;; esac; args+=("$a"); done
exec env "${{args[@]}}"
""",
        "timeout": '#!/usr/bin/env bash\nshift\nexec "$@"\n',
        "sleep": "#!/bin/sh\nexit 0\n",
        "curl": f"""#!/usr/bin/env bash
echo "curl $*" >> {tmp_path}/calls
while [ $# -gt 0 ]; do [ "$1" = -o ] && cp {tmp_path}/key "$2"; shift; done
""",
        "apt-get": f"""#!/usr/bin/env bash
echo "apt-get $*" >> {tmp_path}/calls
debs=$(sed -n 's/^Dir::Cache::archives "\\(.*\\)";$/\\1/p' {root}/etc/apt/apt.conf.d/80-ci)
lists=$(sed -n 's/^Dir::State::lists "\\(.*\\)";$/\\1/p' {root}/etc/apt/apt.conf.d/80-ci)
case "$*" in
  update*) touch "$lists/mirror_dists_noble_main_binary-amd64_Packages" ;;
  *--download-only*)
    left=$(cat {tmp_path}/download-failures)
    if [ "$left" -gt 0 ]; then echo $((left - 1)) > {tmp_path}/download-failures; exit 100; fi
    touch "$debs/clang-22.deb" ;;
  *--no-download*) [ -f "$debs/clang-22.deb" ] || exit 100 ;;
esac
""",
    }
    for name, text in scripts.items():
        (folder / name).write_text(text)
        (folder / name).chmod(0o755)
    (tmp_path / "calls").write_text("")
    return tmp_path


def run_apt(fake_apt, hit="", llvm="22", key_sha=None, mirrors=None):
    root = fake_apt / "root"
    mirror_file = root / "etc" / "apt" / "apt-mirrors.txt"
    if mirrors is not None:
        mirror_file.write_text(mirrors)
    import hashlib
    environment = {**os.environ, "PATH": f"{fake_apt / 'bin'}:{os.environ['PATH']}",
                   "APT_CACHE": str(fake_apt / "cache"), "APT_PACKAGES": "clang-22 lld-22\n zstd",
                   "APT_CACHE_HIT": hit, "LLVM_MAJOR": llvm, "APT_MIRRORS": str(mirror_file),
                   "LLVM_KEY_SHA256": key_sha or hashlib.sha256(b"the key\n").hexdigest()}
    result = subprocess.run(["bash", "-c", apt_script()], capture_output=True, text=True,
                            env=environment)
    calls = (fake_apt / "calls").read_text().splitlines()
    (fake_apt / "calls").write_text("")
    return result, calls


def test_apt_without_a_cache(fake_apt):
    result, calls = run_apt(fake_apt)
    assert result.returncode == 0, result.stdout + result.stderr
    apt = [call for call in calls if call.startswith("apt-get")]
    assert apt[0] == "apt-get update -q"
    assert "--download-only clang-22 lld-22 zstd" in apt[1]
    assert apt[2] == "apt-get autoclean -q"
    assert apt[3].endswith("--no-install-recommends --no-download clang-22 lld-22 zstd")
    assert any(call.startswith("curl ") for call in calls)
    root = fake_apt / "root" / "etc" / "apt"
    assert (root / "keyrings" / "apt.llvm.org.asc").read_text() == "the key\n"
    assert "llvm-toolchain-noble-22 main" in (root / "sources.list.d" / "llvm.list").read_text()
    # (kept for the cache: no lock or partial folder)
    cache = fake_apt / "cache"
    assert (cache / "debs" / "clang-22.deb").exists() and (cache / "apt.llvm.org.asc").exists()
    assert not (cache / "debs" / "partial").exists() and not (cache / "lists" / "partial").exists()


def test_apt_from_the_cache_downloads_nothing(fake_apt):
    assert run_apt(fake_apt)[0].returncode == 0
    result, calls = run_apt(fake_apt, hit="true")
    assert result.returncode == 0, result.stdout + result.stderr
    assert calls == ["apt-get install -y -q --no-install-recommends --no-download clang-22 lld-22 zstd"]
    assert "installed from the cache" in result.stdout


def test_apt_another_images_cache_is_brought_up_to_date(fake_apt):
    assert run_apt(fake_apt)[0].returncode == 0
    # (a cache restored by restore-keys: the lists are fetched again)
    result, calls = run_apt(fake_apt, hit="false")
    assert result.returncode == 0, result.stdout + result.stderr
    assert calls[0] == "apt-get update -q" and "apt-get autoclean -q" in calls


def test_apt_tries_the_next_mirror(fake_apt):
    (fake_apt / "download-failures").write_text("2")
    result, calls = run_apt(fake_apt, mirrors="http://slow.example/ubuntu/\tpriority:1\n"
                                              "http://archive.ubuntu.com/ubuntu/\tpriority:2\n")
    assert result.returncode == 0, result.stdout + result.stderr
    downloads = [call for call in calls if "--download-only" in call]
    assert len(downloads) == 3 and calls.count("apt-get update -q") == 1
    # (try 3 put the third mirror first: the runner's two, then azure's)
    assert (fake_apt / "root" / "etc" / "apt" / "apt-mirrors.txt").read_text().splitlines() == [
        "http://azure.archive.ubuntu.com/ubuntu/\tpriority:1", "http://slow.example/ubuntu/\tpriority:2",
        "http://archive.ubuntu.com/ubuntu/\tpriority:3"]


def test_apt_gives_up_after_four_tries(fake_apt):
    (fake_apt / "download-failures").write_text("9")
    result, calls = run_apt(fake_apt, mirrors="http://slow.example/ubuntu/\n")
    assert result.returncode != 0
    assert len([call for call in calls if "--download-only" in call]) == 4
    assert not any("--no-download" in call for call in calls)


def test_apt_refuses_a_changed_llvm_key(fake_apt):
    result, calls = run_apt(fake_apt, key_sha="0" * 64)
    assert result.returncode != 0
    assert not any(call.startswith("apt-get") for call in calls)


def test_apt_without_llvm(fake_apt):
    result, calls = run_apt(fake_apt, llvm="")
    assert result.returncode == 0, result.stdout + result.stderr
    assert not any(call.startswith("curl ") for call in calls)
    assert not (fake_apt / "root" / "etc" / "apt" / "sources.list.d" / "llvm.list").exists()


RERUN = WORKFLOW.parent / "rerun-unacquired.yml"


def rerun_script() -> str:
    lines = RERUN.read_text().splitlines()
    start = next(index for index, line in enumerate(lines) if line.strip() == "run: |")
    script = []
    for line in lines[start + 1:]:
        if line and not line.startswith("          "):
            break
        script.append(line[10:])
    return "\n".join(script) + "\n"


def test_rerun_only_a_failed_first_attempt():
    text = RERUN.read_text()
    assert "workflows: [Build]" in text and "types: [completed]" in text
    assert ("if: github.event.workflow_run.conclusion == 'failure' && github.event.workflow_run.run_attempt == 1"
            " && github.event.workflow_run.event != 'merge_group'" in text)
    assert "actions/checkout" not in text


@pytest.mark.parametrize("annotation, newest, rerun", [
    ("The job was not acquired by Runner of type hosted even after multiple attempts", "42", True),
    ("Canceling since a higher priority waiting request for build-refs/heads/main exists", "42", False),
    # (a newer run of the branch, which running this one again would cancel)
    ("The job was not acquired by Runner of type hosted even after multiple attempts", "43", False),
])
def test_rerun_when_no_runner_took_a_job(tmp_path, annotation, newest, rerun):
    folder = tmp_path / "bin"
    folder.mkdir()
    (tmp_path / "annotation").write_text(annotation + "\n")
    gh = folder / "gh"
    gh.write_text(f"""#!/bin/sh
echo "$*" >> {tmp_path}/calls
case "$*" in
  *"/jobs?"*) printf '%s\\n' "7 macos" ;;
  *"/check-runs/7/annotations"*) cat {tmp_path}/annotation ;;
  *"/workflows/9/runs"*) echo {newest} ;;
esac
""")
    gh.chmod(0o755)
    environment = {**os.environ, "PATH": f"{folder}:{os.environ['PATH']}", "REPOSITORY": "ChupathingyCE/chupathingyce",
                   "RUN": "42", "WORKFLOW": "9", "BRANCH": "main", "EVENT": "push"}
    result = subprocess.run(["bash", "-e", "-c", rerun_script()], capture_output=True, text=True, env=environment)
    assert result.returncode == 0, result.stdout + result.stderr
    calls = (tmp_path / "calls").read_text().splitlines()
    assert ("run rerun 42 --failed -R ChupathingyCE/chupathingyce" in calls) == rerun
