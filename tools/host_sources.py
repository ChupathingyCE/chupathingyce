"""The game's hosting code (port/linux/host): the dedicated server's
director, which every build with the game browser compiles (a listen host
runs it too), and beside it the units only the dedicated server links (its
headless platform, Custom Edition 1.0.10's join wire and the GameSpy-style
listing; tools/server_build.py)."""

from pathlib import Path
from typing import List

HOST_DIR = Path("port/linux/host")

# the units of HOST_DIR only the dedicated server links (tools/server_build.py,
# tools/windows_build.py's server-windows64)
SERVER_ONLY_SOURCES = frozenset({
    "server_platform.c", "server_input.c", "sdl_headless.c", "backtrace.c", "server_windows.c",
    "ce_messages.c", "ce_transport.c", "gamespy_listing.c",
})


def director_sources(root: Path = Path(".")) -> List[Path]:
    """the director's units (HOST_DIR's, without the server's own), under root"""
    return [source for source in sorted((root / HOST_DIR).glob("*.c")) if source.name not in SERVER_ONLY_SOURCES]
