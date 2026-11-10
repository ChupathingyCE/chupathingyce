"""ChupathingyCE's version, for the builds (tools/linux_build.py,
windows_build.py, macos_build.py; port/android/app/build.gradle reads the
same):

  - VERSION, in the repository's root, is the version being made: 0.5.0b.
  - A release is built by GitHub Actions from its tag, v<VERSION>
    (tools/ci_build.py gives HALO_VERSION, and HALO_RELEASE_BUILD=1, the
    only builds whose self-updater looks for newer releases).
  - Other builds of the workflow are nightlies, <VERSION>-nightly.<serial>,
    and test builds, <VERSION>-testing.<serial> (the serial the UTC time the
    build was made, 202611101530); a build anywhere else is <VERSION>-dev.
  - A nightly's and a test build's self-updater looks for updates on its
    channel (HALO_UPDATE_CHANNEL: update_channel), as a release's does on
    the release channel.
  - Each build also records its channel (release, nightly, dev or custom)
    and its commit (HALO_COMMIT, else git's; "unknown" without either), for
    its logs (port/linux/src/build_identity.c).
"""

import functools
import os
import re
import subprocess
from pathlib import Path
from typing import List, Tuple

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


# a nightly's or a test build's version: <VERSION>-nightly.<serial>,
# <VERSION>-testing.<serial>, in the one version grammar
# (port/linux/src/update_manifest.h; the command repository's signing.py),
# the serial the UTC time it was built, YYYYMMDDHHMM
PRERELEASE_VERSION = re.compile(
    r"^\d{1,9}\.\d{1,9}\.\d{1,9}[a-z]{0,15}-(testing|nightly)\.((\d{4})(\d{2})(\d{2})(\d{2})(\d{2}))$")


def serial_valid(match) -> bool:
    """the serial's time: 2026 to 2099, each field in its range"""
    year, month, day, hour, minute = (int(match.group(index)) for index in range(3, 8))
    return 2026 <= year <= 2099 and 1 <= month <= 12 and 1 <= day <= 31 and hour <= 23 and minute <= 59


def update_channel() -> str:
    """the update channel of a nightly ("nightly") or a test build
    ("testing"), whose self-updater looks for updates there; "" for any
    other build (a release's looks on the release channel, HALO_RELEASE_BUILD)"""
    if release_build():
        return ""
    match = PRERELEASE_VERSION.match(version())
    return match.group(1) if match and serial_valid(match) else ""


def channel() -> str:
    """this build's channel: a release's, a nightly, a test build, a -dev
    build (anywhere but a release workflow), or custom (another version
    given by hand)"""
    if release_build():
        return "release"
    if update_channel():
        return update_channel()
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
# self-updater, the build's identity for the logs, and the settings
# (update.channel's default is the build's update channel)
VERSION_SOURCES = ("updater.c", "build_identity.c", "port_config.c")


def identity_defines() -> str:
    """the defines of the build's identity (port/linux/src/build_identity.c),
    escaped as the builds' updater_defines are"""
    defines = f'-DHALO_CHANNEL=\\"{channel()}\\" -DHALO_COMMIT=\\"{commit()}\\"'
    if update_channel():
        defines += f' -DHALO_UPDATE_CHANNEL=\\"{update_channel()}\\"'
    return defines


def resource_version() -> Tuple[str, str]:
    """this build's version for a Windows executable's version information
    (port/windows/halo.rc): its four numbers (major, minor, patch, and the
    release's letter as a number, a being 1: 0.7.5c is 0,7,5,3) and its text"""
    match = re.match(r"(\d+)\.(\d+)\.(\d+)([a-z]?)", version())
    if not match:
        return "0,0,0,0", version()
    major, minor, patch, letter = match.groups()
    build = ord(letter) - ord("a") + 1 if letter else 0
    return f"{major},{minor},{patch},{build}", version()
