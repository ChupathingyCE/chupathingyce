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
- the plan job: which platform jobs a pull request gets, and the full
  matrix everywhere else; ci-ok's verdict; cache-prune never deleting
  main's newest cache of a kind. Their scripts are run here with a fake gh.

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


@pytest.fixture
def fake_gh(tmp_path):
    """a gh that answers from files: labels, files (a pull request's), caches
    (JSON lines), and logs each call"""
    folder = tmp_path / "bin"
    folder.mkdir()
    gh = folder / "gh"
    gh.write_text(f"""#!/bin/sh
echo "$*" >> {tmp_path}/calls
case "$*" in
  *"-X DELETE"*) exit 0 ;;
  *"/files"*) cat {tmp_path}/files ;;
  *"/actions/caches"*) cat {tmp_path}/caches ;;
  *) cat {tmp_path}/labels ;;
esac
""")
    gh.chmod(0o755)
    for name in ("labels", "files", "caches", "calls"):
        (tmp_path / name).write_text("")
    return tmp_path


def plan(fake_gh, event="pull_request", files=(), labels=(), head_ref="pr/change") -> dict:
    (fake_gh / "files").write_text("".join(f"{name}\n" for name in files))
    (fake_gh / "labels").write_text("".join(f"{name}\n" for name in labels))
    output = fake_gh / "output"
    output.write_text("")
    environment = {**os.environ, "PATH": f"{fake_gh / 'bin'}:{os.environ['PATH']}", "GITHUB_OUTPUT": str(output),
                   "EVENT": event, "HEAD_REF": head_ref, "NUMBER": "1", "REPOSITORY": "ChupathingyCE/chupathingyce"}
    result = subprocess.run(["bash", "-e", "-c", job_script("plan")], capture_output=True, text=True, env=environment)
    assert result.returncode == 0, result.stdout + result.stderr
    return dict(line.split("=", 1) for line in output.read_text().splitlines())


@pytest.mark.parametrize("event", ["push", "workflow_dispatch"])
def test_plan_builds_everything_off_pull_requests(fake_gh, event):
    outputs = plan(fake_gh, event=event)
    assert outputs["full"] == "true" and outputs["server_archs"] == '["x86","x64","arm64"]'


def test_plan_fast_check_for_shared_code(fake_gh):
    outputs = plan(fake_gh, files=["source/game/game.c", "port/linux/src/posix_net.c"])
    assert all(value == "false" for key, value in outputs.items() if key != "server_archs")


@pytest.mark.parametrize("files,area", [
    (["port/windows/src/win32_net.c"], "windows"),
    (["tools/windows_build.py"], "windows"),
    (["port/android/app/build.gradle"], "android"),
    (["port/macos/main.m"], "macos"),
    (["port/unix/unix_main.c"], "freebsd"),
    (["server/src/main.c"], "server"),
    (["port/linux/src/d3d8_bgfx.inl"], "linux64"),
])
def test_plan_adds_the_platform_changed(fake_gh, files, area):
    outputs = plan(fake_gh, files=files)
    assert outputs["full"] == "false"
    assert outputs[area] == "true"
    assert [key for key, value in outputs.items() if value == "true"] == [area]


@pytest.mark.parametrize("kind", ["label", "batch", "plumbing"])
def test_plan_full_matrix_on_a_pull_request(fake_gh, kind):
    outputs = plan(fake_gh, files=["tools/ci_build.py" if kind == "plumbing" else "source/game/game.c"],
                   labels=["network", "ci: full"] if kind == "label" else ["network"],
                   head_ref="batch/2026-11-13" if kind == "batch" else "pr/change")
    assert outputs["full"] == "true"
    assert outputs["server_archs"] == '["x86","x64","arm64"]'


def ci_ok(needs: dict, full: str, event: str) -> bool:
    import json
    environment = {**os.environ, "NEEDS": json.dumps({name: {"result": result} for name, result in needs.items()}),
                   "FULL": full, "EVENT": event}
    return subprocess.run(["bash", "-e", "-c", job_script("ci-ok")], capture_output=True, text=True,
                          env=environment).returncode == 0


def needs(platforms: str, **changes) -> dict:
    found = {"gate": "success", "plan": "success", "info": "skipped", "linux": "success", "linux-tests": "success",
             "linux-big-endian": "success", **{name: platforms for name in PLATFORMS}}
    found.update({name.replace("_", "-"): result for name, result in changes.items()})
    return found


@pytest.mark.skipif(subprocess.run(["sh", "-c", "command -v jq"], capture_output=True).returncode != 0,
                    reason="no jq")
def test_ci_ok():
    assert ci_ok(needs("skipped"), "false", "pull_request")
    assert ci_ok(needs("success"), "true", "push")
    assert ci_ok(needs("success", info="success", freebsd64="failure"), "true", "workflow_dispatch")
    # (a full run with a platform left out, a failed test, FreeBSD failing
    # off a dispatch, a dispatch without its build-info, a failed gate)
    assert not ci_ok(needs("skipped"), "true", "pull_request")
    assert not ci_ok(needs("skipped", linux_tests="failure"), "false", "pull_request")
    assert not ci_ok(needs("success", freebsd64="failure"), "true", "push")
    assert not ci_ok(needs("success"), "true", "workflow_dispatch")
    assert not ci_ok(needs("skipped", gate="failure", plan="skipped"), "", "pull_request")


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
