"""ChupathingyCE's version, for the builds (tools/linux_build.py,
windows_build.py, macos_build.py; port/android/app/build.gradle reads the
same):

  - VERSION, in the repository's root, is the version being made: 0.5.0b.
  - A release is built by GitHub Actions from its tag, v<VERSION>
    (tools/ci_build.py gives HALO_VERSION, and HALO_RELEASE_BUILD=1, the
    only builds whose self-updater looks for newer releases).
  - Other builds of the workflow are nightlies, <VERSION>-nightly.<run>; a
    build anywhere else is <VERSION>-dev.
  - A test build (configure.py --no-updater, with no self-updater) is
    <VERSION>-test, or with --test-name NAME, <VERSION>-NAME-test.
"""

import os
from pathlib import Path
from typing import Any

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


def test_version(name: str = "") -> str:
    """a test build's (configure.py --no-updater): VERSION's, marked as a
    test's, 0.6.0b-test, or with a name, 0.6.0b-halopc-maps-test"""
    return f"{base_version()}-{name}-test" if name else f"{base_version()}-test"


def has_updater(sln: Any) -> bool:
    """whether the build has the self-updater (all but configure.py
    --no-updater's test builds)"""
    return getattr(sln, "updater", True)


def build_version(sln: Any) -> str:
    """the version the build is given: version()'s, or a test build's"""
    return version() if has_updater(sln) else test_version(getattr(sln, "test_name", None) or "")
