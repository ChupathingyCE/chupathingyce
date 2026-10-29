"""Downloads for the build tools that survive a bad network day.

Each address is tried in turn, a few times each, waiting longer after every
failure, and the file is kept only when its SHA-256 is the expected one. A
tarball that is already in the download directory (the CI cache restores it
there) is checked and used without any network at all.
"""

import hashlib
import sys
import time
import urllib.request
from pathlib import Path

ATTEMPTS = 5
TIMEOUT = 60
# where the tarballs are kept between runs (the CI caches this directory)
DOWNLOADS = Path(__file__).resolve().parent.parent / "build" / "downloads"


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch_file(urls, destination, sha256, attempts=ATTEMPTS, timeout=TIMEOUT, delay=2):
    """the file at any of urls, at destination, with the expected SHA-256"""
    destination = Path(destination)
    if destination.is_file():
        if sha256_of(destination) == sha256:
            return destination
        destination.unlink()
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_name(destination.name + ".part")
    last = None
    for url in urls:
        for attempt in range(attempts):
            if attempt:
                time.sleep(delay * 2 ** (attempt - 1))
            try:
                with urllib.request.urlopen(url, timeout=timeout) as response, open(partial, "wb") as f:
                    while True:
                        block = response.read(1 << 20)
                        if not block:
                            break
                        f.write(block)
                digest = sha256_of(partial)
                if digest != sha256:
                    last = f"SHA-256 {digest}, expected {sha256}"
                    print(f"download: {url}: {last}", file=sys.stderr)
                    break  # (another attempt of this address gives the same file)
                partial.replace(destination)
                return destination
            except OSError as error:
                last = str(error)
                print(f"download: {url}: {error} (attempt {attempt + 1} of {attempts})", file=sys.stderr)
    partial.unlink(missing_ok=True)
    raise SystemExit(f"download: {destination.name}: no address worked ({last})")
