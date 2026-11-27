"""Check the SPV3 installer's writing (port/linux/src/spv3_installer.c).

Builds the installer on its own with its test release (the host fuzz
target's, HALO_SPV3_INSTALLER_STANDALONE) and tools/tests/
spv3_installer_test.c, and installs that release into a scratch data folder
set up for each case:

- a fresh install writes the two files and leaves no .part;
- a .part that is a hard link, or a symbolic link, to a file outside is
  removed, never written through: the outside file stays as it was;
- Cancel while the files already there are checked stops the run ("Stopped"),
  not "SPV3 is in";
- a folder below the top one that is a link is refused, nothing written
  through it;
- what SPV3 mode runs without lua.scripts (spv3_installer_known_lua): an
  installed Lua file as written, but not one edited afterwards, and not an
  extra .lua dropped beside the installed ones.

POSIX only (the folder calls it checks are openat's). Needs only a C compiler.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
RELEASE = ROOT / "tools/tests/host_fuzz/spv3_release/seeds/test_release.zip"
SOURCES = [
	"port/linux/src/spv3_installer.c",
	"port/linux/src/posix_files.c",
	"port/third_party/zlib/adler32.c",
	"port/third_party/zlib/crc32.c",
	"port/third_party/zlib/inffast.c",
	"port/third_party/zlib/inflate.c",
	"port/third_party/zlib/inftrees.c",
	"port/third_party/zlib/zutil.c",
]
# the test release's files, where they go
TARGETS = ("maps_spv3/test.yelo", "lua/spv3/modules/test.lua")

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="POSIX folder calls")


@pytest.fixture(scope="module")
def program(tmp_path_factory):
	cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
	if not cc:
		pytest.skip("no C compiler")
	output = tmp_path_factory.mktemp("build") / "spv3_installer_test"
	subprocess.run([cc, "-std=gnu11", "-w", "-D_GNU_SOURCE", "-D_FILE_OFFSET_BITS=64", "-DZ_PREFIX",
		"-Dz_errmsg=z_port_errmsg", "-DHALO_SPV3_INSTALLER_STANDALONE", "-I", str(ROOT / "port/linux/src"),
		"-I", str(ROOT / "port/third_party/zlib"), *[str(ROOT / source) for source in SOURCES],
		str(ROOT / "tools/tests/spv3_installer_test.c"), "-o", str(output)], check=True)
	return output


def install(program, data, *mode):
	result = subprocess.run([str(program), str(RELEASE), str(data), *mode], capture_output=True, text=True,
		check=True)
	return result.stdout.strip()


def installed(data):
	"""the targets' CRC-32s, None for one not there"""
	return {target: (zlib.crc32((data / target).read_bytes()) if (data / target).is_file() else None)
		for target in TARGETS}


@pytest.fixture
def data(tmp_path):
	folder = tmp_path / "data"
	folder.mkdir()
	return folder


def test_fresh_install(program, data):
	assert install(program, data) == "ok"
	assert all(installed(data).values())
	assert not list(data.rglob("*.part"))


@pytest.mark.parametrize("kind", ["hard", "symbolic"])
def test_part_link_not_written_through(program, data, tmp_path, kind):
	assert install(program, data) == "ok"
	right = installed(data)
	outside = tmp_path / "outside.txt"
	outside.write_bytes(b"not SPV3's\n")
	target = data / TARGETS[0]
	target.unlink()
	part = data / (TARGETS[0] + ".part")
	if kind == "hard":
		os.link(outside, part)
	else:
		os.symlink(outside, part)
	assert install(program, data) == "ok"
	assert outside.read_bytes() == b"not SPV3's\n"
	assert installed(data) == right
	assert not part.exists() and not part.is_symlink()


def test_cancel_while_checking(program, data):
	assert install(program, data) == "ok"
	assert install(program, data, "cancel-first").startswith("error: Stopped.")


def test_folder_link_refused(program, data, tmp_path):
	outside = tmp_path / "outside"
	outside.mkdir()
	(data / "lua").mkdir()
	os.symlink(outside, data / "lua/spv3")
	result = install(program, data)
	assert result.startswith("error: lua/spv3 is a link")
	assert not any(outside.iterdir())


def known(program, relative, path):
	return subprocess.run([str(program), "known-lua", relative, str(path)], capture_output=True, text=True,
		check=True).stdout.strip()


def test_known_lua_files(program, data):
	assert install(program, data) == "ok"
	script = data / TARGETS[1]
	assert known(program, TARGETS[1], script) == "known"
	assert known(program, TARGETS[1].upper().replace("/", "\\"), script) == "known"
	extra = data / "lua/spv3/scripts/global/extra.lua"
	extra.parent.mkdir(parents=True, exist_ok=True)
	extra.write_bytes(script.read_bytes())
	assert known(program, "lua/spv3/scripts/global/extra.lua", extra) == "unknown"
	edited = bytearray(script.read_bytes())
	edited[0] ^= 1
	script.write_bytes(bytes(edited))
	assert known(program, TARGETS[1], script) == "unknown"
	assert known(program, TARGETS[0], data / TARGETS[0]) == "unknown"
