"""Where the Metal build's files are, for its tests.

The layout is port/macos/metal/manifest.json's: four source folders whose
file names are unique (the manifest's check enforces it), so a test names a
file, not its folder, and moving a file between folders does not break it.

Run the tests from the repository root:
    python3 -m unittest discover -s port/macos/metal/tests -p 'test_*.py'
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
METAL = ROOT / "port/macos/metal"
FOLDERS = ("xbox_compat", "shaders", "platform", "renderer")


def source(name: str) -> Path:
    """the Mac file called name, in whichever folder it is"""
    found = [METAL / folder / name for folder in FOLDERS if (METAL / folder / name).is_file()]
    if len(found) != 1:
        raise FileNotFoundError(f"{name}: {len(found)} matches under {METAL}")
    return found[0]


def read(name: str) -> str:
    return source(name).read_text()


def include_flags() -> list[str]:
    """the compiler's -I options for every Mac folder (and the Metal folder)"""
    flags = []
    for folder in FOLDERS:
        flags += ["-I", str(METAL / folder)]
    return flags + ["-I", str(METAL)]
