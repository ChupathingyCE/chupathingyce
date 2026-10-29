"""A playlist's map set (docs/playlist-maps.md): the tool that checks and
signs manifests (tools/playlist_maps.py) and the game's reader
(port/linux/src/playlist_manifest.c), built by itself with a test key, held
to the same manifests; and the setting it goes by."""
import copy
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import delta_table  # noqa: E402
import playlist_maps  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port" / "linux" / "src"
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"

DRIVER = r"""
#include "playlist_manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length + 1);
	*size = fread(data, 1, (size_t)length, file);
	fclose(file);
	return data;
}

int main(int count, char **arguments)
{
	static struct playlist_manifest manifest;
	char why[160];
	size_t size = 0, signature_size = 0;
	char *document, *signature;
	int index;

	if (count == 3 && !strcmp(arguments[1], "sha"))
	{
		/* (in uneven pieces: the pieces' joins must not matter) */
		struct playlist_sha256 context;
		unsigned char digest[32];
		size_t at = 0, step = 1;

		document = slurp(arguments[2], &size);
		playlist_sha256_begin(&context);
		while (at < size)
		{
			size_t piece = step < size - at ? step : size - at;

			playlist_sha256_add(&context, document + at, piece);
			at += piece;
			step = step * 3 % 199 + 1;
		}
		playlist_sha256_end(&context, digest);
		for (index = 0; index < 32; index++)
			printf("%02x", digest[index]);
		printf("\n");
		return 0;
	}
	document = slurp(arguments[1], &size);
	if (count == 2)
	{
		if (!playlist_manifest_parse(document, size, &manifest, why, sizeof(why)))
		{
			printf("refused: %s\n", why);
			return 0;
		}
	}
	else
	{
		signature = slurp(arguments[2], &signature_size);
		if (!playlist_manifest_check(document, size, signature, signature_size, count > 3 ? arguments[3] : NULL,
			&manifest, why, sizeof(why)))
		{
			printf("refused: %s\n", why);
			return 0;
		}
	}
	printf("ok %s %u %d %d\n", manifest.playlist, manifest.serial, manifest.file_count, manifest.pack_count);
	for (index = 0; index < manifest.file_count; index++)
	{
		const struct playlist_file *file = &manifest.files[index];

		printf("%s %s %s %u %d %d\n", file->folder, file->file, file->map[0] ? file->map : "-", file->size, file->source,
			file->pack);
	}
	return 0;
}
"""


def compiler():
    for name in ("clang", "cc", "gcc"):
        if shutil.which(name):
            return name
    return None


def openssl_or_skip():
    try:
        delta_table.openssl()
    except SystemExit:
        pytest.skip("needs OpenSSL 3")


@pytest.fixture(scope="module")
def keys(tmp_path_factory):
    openssl_or_skip()
    folder = tmp_path_factory.mktemp("keys")
    primary, recovery = folder / "primary.pem", folder / "recovery.pem"
    return (primary, delta_table.keygen(primary)), (recovery, delta_table.keygen(recovery))


@pytest.fixture(scope="module")
def reader(keys, tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    folder = tmp_path_factory.mktemp("reader")
    driver = folder / "driver.c"
    driver.write_text(DRIVER)
    primary = ",".join(f"0x{byte:02x}" for byte in keys[0][1])
    recovery = ",".join(f"0x{byte:02x}" for byte in keys[1][1])
    program = folder / "reader"
    built = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                            f"-DHALO_DELTA_TEST_KEY={primary}", f"-DHALO_DELTA_TEST_RECOVERY_KEY={recovery}",
                            "-I", str(SRC), "-I", str(MONOCYPHER), str(driver), str(SRC / "playlist_manifest.c"),
                            str(MONOCYPHER / "monocypher.c"), str(MONOCYPHER / "monocypher-ed25519.c"),
                            "-o", str(program)], capture_output=True, text=True)
    assert built.returncode == 0, built.stderr[-4000:]
    return program


def run(program, *arguments):
    result = subprocess.run([str(program), *map(str, arguments)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    return result.stdout


SHA = "7d6edf513925439c74f78e86670d6287cdc03417cd83225f774e7ecc41935d72"

MANIFEST = {
    "playlist_maps": 1,
    "playlist": "c_cex",
    "name": "[C] CE+ X",
    "serial": 1,
    "issued": 1793145600,
    "packs": {
        "cex-1.01": {"name": "Halo CE+ X 1.01", "author": "VKMT", "credits": "VKMT: Sean T, Ludus, Elote and others.",
                     "permission": "Mirrored with VKMT's permission"},
        "xbox": {"name": "Halo: Combat Evolved (Xbox)", "author": "Bungie"},
    },
    "files": [
        {"map": "bloodgulch@cex", "title": "Abyss", "folder": "maps_cex", "file": "bloodgulch.map",
         "size": 24887296, "sha256": SHA, "source": "chupathingy", "path": "packs/cex/1.01/bloodgulch.map",
         "pack": "cex-1.01"},
        {"folder": "maps_cex", "file": "ui.map", "size": 12288000, "sha256": SHA, "source": "chupathingy",
         "path": "packs/cex/1.01/ui.map", "pack": "cex-1.01"},
        {"map": "sanctuary@ce", "folder": "maps_ce", "file": "sanctuary.map", "size": 19000000, "sha256": SHA,
         "source": "halonet"},
        {"map": "prisoner", "folder": "maps", "file": "prisoner.map", "source": "own", "pack": "xbox"},
    ],
}


def write(tmp_path, manifest, name="maps.json"):
    path = tmp_path / name
    path.write_bytes(manifest if isinstance(manifest, bytes) else json.dumps(manifest, indent=1).encode())
    return path


def sign(path, key):
    signature = delta_table.sign_bytes(key, playlist_maps.CONTEXT + path.read_bytes())
    signature_path = Path(str(path) + ".sig")
    signature_path.write_text(signature.hex() + "\n")
    return signature_path


def both(reader, tmp_path, manifest):
    """the game's reader's answer and the tool's (each "ok" or why not)"""
    path = write(tmp_path, manifest)
    game = run(reader, path)
    try:
        playlist_maps.parse(path.read_bytes())
        tool = "ok"
    except playlist_maps.ManifestError as problem:
        tool = f"refused: {problem}"
    return game, tool


def test_the_example_is_read(reader, tmp_path):
    game, tool = both(reader, tmp_path, MANIFEST)
    assert tool == "ok"
    lines = game.splitlines()
    assert lines[0] == "ok c_cex 1 4 2"
    assert lines[1] == "maps_cex bloodgulch.map bloodgulch@cex 24887296 1 0"
    assert lines[2] == "maps_cex ui.map - 12288000 1 0"
    assert lines[3] == "maps_ce sanctuary.map sanctuary@ce 19000000 0 -1"
    assert lines[4] == "maps prisoner.map prisoner 0 2 1"


def test_unknown_fields_are_passed_over(reader, tmp_path):
    manifest = copy.deepcopy(MANIFEST)
    manifest["later"] = {"nested": [1, 2.5, -3e4, True, None, "x"]}
    manifest["files"][0]["later"] = [{"a": False}]
    manifest["packs"]["xbox"]["later"] = 1
    game, tool = both(reader, tmp_path, manifest)
    assert game.startswith("ok c_cex"), game
    assert tool == "ok"


def broken(change):
    manifest = copy.deepcopy(MANIFEST)
    change(manifest)
    return manifest


REFUSED = {
    "format 2": lambda m: m.update(playlist_maps=2),
    "no files": lambda m: m.update(files=[]),
    "playlist with a capital": lambda m: m.update(playlist="C_cex"),
    "serial 0": lambda m: m.update(serial=0),
    "serial a string": lambda m: m.update(serial="1"),
    "a path out of its folder": lambda m: m["files"][0].update(file="../bloodgulch.map"),
    "a file with a slash": lambda m: m["files"][0].update(file="a/b.map"),
    "an executable": lambda m: m["files"][0].update(file="default.xbe"),
    "a hidden file": lambda m: m["files"][0].update(file=".hidden.map"),
    "a folder not ours": lambda m: m["files"][0].update(folder="saves"),
    "a folder with dots": lambda m: m["files"][0].update(folder="maps_.."),
    "a download into maps": lambda m: m["files"][0].update(folder="maps"),
    "halonet outside maps_ce": lambda m: m["files"][2].update(folder="maps_cex"),
    "an unknown source": lambda m: m["files"][0].update(source="peer"),
    "no hash to download": lambda m: m["files"][0].pop("sha256"),
    "no size to download": lambda m: m["files"][0].pop("size"),
    "an uppercase hash": lambda m: m["files"][0].update(sha256=SHA.upper()),
    "a short hash": lambda m: m["files"][0].update(sha256=SHA[:-2]),
    "a size over 1 GB": lambda m: m["files"][0].update(size=1024 * 1024 * 1024 + 1),
    "a path with ..": lambda m: m["files"][0].update(path="packs/../secret.map"),
    "an absolute path": lambda m: m["files"][0].update(path="/packs/cex.map"),
    "a URL for a path": lambda m: m["files"][0].update(path="https://example.com/x.map"),
    "no path for the mirror": lambda m: m["files"][0].pop("path"),
    "a path for halonet": lambda m: m["files"][2].update(path="maps/sanctuary.map"),
    "an own file without a pack": lambda m: m["files"][3].pop("pack"),
    "a pack it does not have": lambda m: m["files"][0].update(pack="cex-2.0"),
    "a file twice": lambda m: m["files"].append(dict(m["files"][1])),
    "a pack without a name": lambda m: m["packs"]["xbox"].pop("name"),
    "a name with a control character": lambda m: m.update(name="a\u0001b"),
}


@pytest.mark.parametrize("case", sorted(REFUSED))
def test_refused_whole(reader, tmp_path, case):
    game, tool = both(reader, tmp_path, broken(REFUSED[case]))
    assert game.startswith("refused"), (case, game)
    assert tool.startswith("refused"), (case, tool)


def test_a_field_twice_is_refused(reader, tmp_path):
    text = json.dumps(MANIFEST)
    document = ('{"serial": 2, ' + text[1:]).encode()
    game, tool = both(reader, tmp_path, document)
    assert game.startswith("refused") and tool.startswith("refused")


def test_trailing_bytes_are_refused(reader, tmp_path):
    game, tool = both(reader, tmp_path, json.dumps(MANIFEST).encode() + b" {}")
    assert game.startswith("refused") and tool.startswith("refused")


def test_signed_by_the_primary_key(reader, keys, tmp_path):
    path = write(tmp_path, MANIFEST)
    signature = sign(path, keys[0][0])
    assert run(reader, path, signature, "c_cex").startswith("ok c_cex")
    assert playlist_maps.check(path.read_bytes(), signature.read_text(), [keys[0][1]])["playlist"] == "c_cex"


def test_another_playlist_is_refused(reader, keys, tmp_path):
    path = write(tmp_path, MANIFEST)
    signature = sign(path, keys[0][0])
    assert "another playlist" in run(reader, path, signature, "c_slayer")


def test_a_changed_byte_is_refused(reader, keys, tmp_path):
    path = write(tmp_path, MANIFEST)
    signature = sign(path, keys[0][0])
    path.write_bytes(path.read_bytes().replace(b"24887296", b"24887297"))
    assert "does not match" in run(reader, path, signature)


def test_a_signature_without_the_context_is_refused(reader, keys, tmp_path):
    """a manifest signed as a legacy table is (its bytes alone) does not pass"""
    path = write(tmp_path, MANIFEST)
    raw = delta_table.sign_bytes(keys[0][0], path.read_bytes())
    signature = tmp_path / "raw.sig"
    signature.write_text(raw.hex() + "\n")
    assert "does not match" in run(reader, path, signature)
    with pytest.raises(playlist_maps.ManifestError):
        playlist_maps.check(path.read_bytes(), signature.read_text(), [keys[0][1]])


def test_epochs(reader, keys, tmp_path):
    """the primary key signs epoch 0 alone; the recovery key any epoch"""
    later = broken(lambda m: m.update(serial=(1 << 24) | 1))
    path = write(tmp_path, later)
    assert "epoch" in run(reader, path, sign(path, keys[0][0]))
    assert run(reader, path, sign(path, keys[1][0])).startswith("ok")


def test_sha256_in_pieces(reader, tmp_path):
    for size in (0, 1, 55, 56, 63, 64, 65, 1000, 70001):
        data = bytes((index * 7 + size) & 0xFF for index in range(size))
        path = tmp_path / f"data{size}"
        path.write_bytes(data)
        assert run(reader, "sha", path).strip() == hashlib.sha256(data).hexdigest(), size


def test_the_tool_signs_and_verifies(keys, tmp_path):
    path = write(tmp_path, MANIFEST)
    tool = Path(__file__).resolve().parent / "playlist_maps.py"
    result = subprocess.run([sys.executable, str(tool), "sign", "--key", str(keys[0][0]), str(path)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([sys.executable, str(tool), "verify", "--public-key", keys[0][1].hex(),
                             "--last-epoch", "0", str(path)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    path.write_bytes(path.read_bytes() + b"\n")
    result = subprocess.run([sys.executable, str(tool), "verify", "--public-key", keys[0][1].hex(), str(path)],
                            capture_output=True, text=True)
    assert result.returncode == 1


def test_the_setting_is_documented_and_defaults_to_ask():
    config = (SRC / "port_config.c").read_text()
    assert '{ "network.playlist_downloads", _config_string, "\\"ask\\""' in config
    assert '{ "network.playlist_downloads", { "ask", "always", "never", NULL }, "\\"ask\\"" }' in config
    readme = (ROOT / "port" / "linux" / "README.md").read_text()
    assert "| `network.playlist_downloads` | `\"ask\"` |" in readme


def test_the_staged_cex_pack_reads_as_a_manifest():
    """the CE+ X 1.01 entries (the mirror's files.json, when it is here)"""
    staged = Path.home() / "chupathingyce-work" / "mirror-staging" / "packs" / "cex" / "1.01" / "files.json"
    if not staged.is_file():
        pytest.skip("no staged pack here")
    pack = json.loads(staged.read_text())
    manifest = {"playlist_maps": 1, "playlist": "c_cex", "name": "[C] CE+ X", "serial": 1, "issued": 0,
                "packs": pack["packs"], "files": pack["files"]}
    assert len(playlist_maps.parse(json.dumps(manifest).encode())["files"]) == 16
