"""ChupathingyCE's version, for the builds (tools/linux_build.py,
windows_build.py, macos_build.py; port/android/app/build.gradle reads the
same):

  - VERSION, in the repository's root, is the version being made: 0.5.0b.
  - A release is built by GitHub Actions from its tag, v<VERSION>
    (tools/ci_build.py gives HALO_VERSION, and HALO_RELEASE_BUILD=1, the
    only builds whose self-updater looks for newer releases).
  - Other builds of the workflow are nightlies, <VERSION>-nightly.<run>; a
    build anywhere else is <VERSION>-dev.
  - Each build also records its channel (release, nightly, dev or custom)
    and its commit (HALO_COMMIT, else git's; "unknown" without either), for
    its logs (port/linux/src/build_identity.c).
"""

import functools
import os
import subprocess
from pathlib import Path
from typing import List

ROOT = Path(__file__).resolve().parent.parent


def base_version() -> str:
    """VERSION's"""
    return (ROOT / "VERSION").read_text(encoding="utf-8").strip()


def version() -> str:
    """this build's"""
    return os.environ.get("HALO_VERSION") or f"{base_version()}-dev"


def release_build() -> bool:
    """whether this build is a release's (and so looks for newer ones)"""
    return os.environ.get("HALO_RELEASE_BUILD") == "1"


def channel() -> str:
    """this build's channel: a release's, a nightly, a -dev build (anywhere
    but a release workflow), or custom (another version given by hand)"""
    if release_build():
        return "release"
    if "-nightly" in version():
        return "nightly"
    if version().endswith("-dev"):
        return "dev"
    return "custom"


def _git(*arguments: str) -> str:
    try:
        return subprocess.run(["git", "-C", str(ROOT), *arguments], capture_output=True, text=True,
                              check=True, timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


@functools.lru_cache(maxsize=None)
def commit() -> str:
    """this build's commit, short (8 digits): HALO_COMMIT, else the
    checkout's; "unknown" without git"""
    return os.environ.get("HALO_COMMIT") or _git("rev-parse", "--short=8", "HEAD") or "unknown"


def commit_inputs() -> List[Path]:
    """what changes with the checkout's commit (its HEAD's log), for
    build.ninja to be configured again, and the commit recorded again"""
    if os.environ.get("HALO_COMMIT"):
        return []
    log = _git("rev-parse", "--git-path", "logs/HEAD")
    if not log:
        return []
    path = Path(log) if Path(log).is_absolute() else ROOT / log
    return [path] if path.is_file() else []


# the sources given the version's defines (identity_defines): the
# self-updater, and the build's identity for the logs
VERSION_SOURCES = ("updater.c", "build_identity.c")


def identity_defines() -> str:
    """the defines of the build's identity (port/linux/src/build_identity.c),
    escaped as the builds' updater_defines are"""
    return f'-DHALO_CHANNEL=\\"{channel()}\\" -DHALO_COMMIT=\\"{commit()}\\"'
