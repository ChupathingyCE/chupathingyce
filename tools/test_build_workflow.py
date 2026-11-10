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
  own head without a version.

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
    lines = jobs()["gate"]
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
