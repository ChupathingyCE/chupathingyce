"""A bgfx patch (tools/bgfx-patches) that fails to apply fails the build
(#1080): tools/bgfx_build.py raises BgfxPatchError, which bgfx_available lets
through to configure.py, instead of quietly leaving the bgfx renderer out
the way a machine without git or the network does."""
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools import bgfx_build  # noqa: E402


class Done:
    def __init__(self, returncode=0, stdout=""):
        self.returncode = returncode
        self.stdout = stdout


def test_a_failed_patch_raises(monkeypatch, tmp_path):
    """sources at their pins, one patch refused by git apply: BgfxPatchError"""
    sources = tmp_path / "bgfx"
    for name, (url, commit) in bgfx_build.BGFX_REPOSITORIES.items():
        (sources / name).mkdir(parents=True)
        (sources / name / "LICENSE").write_text("x")
    monkeypatch.setattr(bgfx_build, "BGFX_SOURCES", sources)
    monkeypatch.setattr(bgfx_build, "_git_head", lambda directory: bgfx_build.BGFX_REPOSITORIES[directory.name][1])
    patch = tmp_path / "0001-nope.patch"
    patch.write_text("--- a/x\n+++ b/x\n@@ -1 +1 @@\n-a\n+b\n")
    monkeypatch.setattr(bgfx_build, "BGFX_PATCHES", [patch])
    calls = []

    def run(command, **arguments):
        calls.append(command)
        if command[:2] == ["git", "-C"] and "apply" in command:
            return Done(1)
        return Done(0)

    monkeypatch.setattr(subprocess, "run", run)
    with pytest.raises(bgfx_build.BgfxPatchError, match="0001-nope.patch does not apply"):
        bgfx_build.fetch_bgfx()
    assert any("apply" in command for command in calls)
    # (the stamp is not written: the next configure tries again)
    assert not (sources / "bgfx" / ".halo-patches").exists()
    # (and bgfx_available does not swallow it, as it does a fetch that fails)
    monkeypatch.setattr(bgfx_build, "bgfx_wanted", lambda sln: True)
    with pytest.raises(bgfx_build.BgfxPatchError):
        bgfx_build.bgfx_available(None)


def test_a_fetch_that_fails_leaves_bgfx_out(monkeypatch, capsys):
    """no sources and no network: as before, no bgfx and a line saying so"""
    monkeypatch.setattr(bgfx_build, "bgfx_wanted", lambda sln: True)
    monkeypatch.setattr(bgfx_build, "fetch_bgfx", lambda: False)
    assert bgfx_build.bgfx_available(None) is False
    assert "left out" in capsys.readouterr().out
    monkeypatch.setattr(bgfx_build, "fetch_bgfx", lambda: (_ for _ in ()).throw(OSError("no git")))
    assert bgfx_build.bgfx_available(None) is False
