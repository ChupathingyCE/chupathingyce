"""SDL's headers, fetched once from SDL's release (never its library), for
the builds that compile against them without linking SDL: the dedicated
server (tools/server_build.py) and the console ports (warthog-360's and
warthog-wiiu's builds). tools/server_build.py re-exports these names, so the
older imports keep working."""

import hashlib
import sys
import tarfile
import urllib.request
from pathlib import Path

# SDL's headers (its release's source, the same version as the other ports'
# SDL: android_build.SDL_TAG); only include/ and the license are unpacked
SDL_VERSION = "3.4.16"
SDL_TARBALL_URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/SDL3-{SDL_VERSION}.tar.gz"
SDL_TARBALL_SHA256 = "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68"
THIRD_PARTY = Path("build/server/third_party")
SDL_HEADERS_DIR = THIRD_PARTY / f"SDL3-{SDL_VERSION}"
SDL_INCLUDE = SDL_HEADERS_DIR / "include"


def fetch_sdl_headers() -> bool:
    """SDL's headers (once): whether they are there"""
    if (SDL_INCLUDE / "SDL3" / "SDL.h").is_file():
        return True
    THIRD_PARTY.mkdir(parents=True, exist_ok=True)
    archive = THIRD_PARTY / f"SDL3-{SDL_VERSION}.tar.gz"
    print(f"Downloading {SDL_TARBALL_URL} (its headers, for the server)")
    try:
        with urllib.request.urlopen(SDL_TARBALL_URL) as response, open(archive, "wb") as f:
            f.write(response.read())
    except OSError as error:
        print(f"Server build disabled: cannot fetch SDL's headers ({error})", file=sys.stderr)
        return False
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != SDL_TARBALL_SHA256:
        archive.unlink()
        raise SystemExit(f"{SDL_TARBALL_URL}: SHA-256 {digest}, not the pinned {SDL_TARBALL_SHA256}")
    prefix = f"SDL3-{SDL_VERSION}/"
    with tarfile.open(archive) as tar:
        members = [m for m in tar.getmembers()
                   if m.isfile() and (m.name.startswith(prefix + "include/") or m.name == prefix + "LICENSE.txt")]
        # (only regular files under the prefix; and the extraction filter
        # where this Python has it)
        if hasattr(tarfile, "data_filter"):
            tar.extractall(THIRD_PARTY, members=members, filter="data")
        else:
            tar.extractall(THIRD_PARTY, members=members)
    archive.unlink()
    return (SDL_INCLUDE / "SDL3" / "SDL.h").is_file()
