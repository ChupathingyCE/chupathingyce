#!/usr/bin/env python3
"""What the map loaders' shared pieces make, held to fixed figures
(tools/tests/content_golden.c), so a cleanup that moves them (#1771) can show
it changed nothing:

- every SHA-256 copy (p2p_crypto.c, identity.c, playlist_manifest.c,
  update_manifest.c, mcc_sha256.c, both its portable code and the
  processor's instructions) gives Python's hashlib digests, whole and in
  pieces, and p2p_hmac_sha256 gives Python's HMAC: map identity is on the
  wire, so not one digest may move;
- the Ogg Vorbis decoders (ce_vorbis.c's decode, edges, stream and
  resampler, mcc_vorbis.c's decode) give the same samples as before on
  tools/tests/vorbis's files, whole, cut short and with bytes changed;
- the desktop mixer's Xbox ADPCM decoder (dsound_sdl.c) and MCC's IMA coder
  (mcc_audio.c) give the same samples and codes as before, and their step
  tables are the retail game's (source/sound/ima_adpcm.c).

Where the cleanup gives a piece one home (content_sha256.c, content_vorbis.c,
content_audio.c in port/linux/game: #1763, #1766, #1767), the builds take it
when it is there, and the figures stay as they are.

Needs a C compiler (clang or cc). The figures were taken from the code as it
was before the cleanup; a change to them is a change in what players hear or
in what hashes match, and belongs in its own pull request.

    python3 -m pytest tools/test_content_golden.py
    python3 tools/test_content_golden.py --print     (the figures as they are now)
"""

import hashlib
import hmac
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    import pytest
except ImportError:
    pytest = None

ROOT = Path(__file__).resolve().parent.parent
DRIVER = ROOT / "tools" / "tests" / "content_golden.c"
SRC = ROOT / "port" / "linux" / "src"
GAME = ROOT / "port" / "linux" / "game"
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"
STB = ROOT / "port" / "third_party" / "stb"
VORBIS_FILES = sorted((ROOT / "tools" / "tests" / "vorbis").glob("*.ogg"))


def compiler():
    for name in (os.environ.get("CC"), "clang", "cc", "gcc"):
        if name and shutil.which(name):
            return name
    return None


def skip(reason):
    if pytest:
        pytest.skip(reason)
    raise SystemExit(reason)


_FOLDER = None


def folder():
    global _FOLDER
    if _FOLDER is None:
        _FOLDER = Path(tempfile.mkdtemp(prefix="content_golden_"))
    return _FOLDER


# (what p2p_crypto.c calls, declared ahead of it: content_golden.c has them)
PRELUDE = "void posix_random_bytes(void *buffer, unsigned long size);\nvoid platform_log(const char *format, ...);\n"


def build(name, arguments, source=DRIVER):
    """the driver built with arguments (defines and units): its path"""
    (folder() / "prelude.h").write_text(PRELUDE)
    arguments = [str(folder() / "prelude.h") if word == "@PRELUDE@" else word for word in arguments]
    cc = compiler()
    if not cc:
        skip("no C compiler")
    binary = folder() / name
    if not binary.exists():
        result = subprocess.run([cc, "-std=gnu11", "-O1", "-ffp-contract=off", "-o", str(binary), str(source), *arguments, "-lm",
                                 "-lpthread"], capture_output=True, text=True, cwd=ROOT)
        if result.returncode:
            raise RuntimeError(f"{name} does not build:\n{result.stderr[-4000:]}")
    return binary


def run(binary, *arguments, env=None):
    result = subprocess.run([str(binary), *map(str, arguments)], capture_output=True, text=True, timeout=300,
                            env={**os.environ, **(env or {})})
    assert result.returncode == 0, result.stderr
    return result.stdout


# ---------- SHA-256

# each copy's build: its define and units
SHA256_BUILDS = {
    "p2p": ["-DGOLDEN_SHA256_P2P", "-D__HALO_LINUX_PLATFORM_H", "-D__HALO_LINUX_POSIX_H", "-include", "@PRELUDE@", f"-I{SRC}",
            f"-I{MONOCYPHER}", str(SRC / "p2p_crypto.c"), str(MONOCYPHER / "monocypher.c"),
            str(MONOCYPHER / "monocypher-ed25519.c")],
    "identity": ["-DGOLDEN_SHA256_IDENTITY", "-iquote", str(ROOT / "port" / "linux" / "include"),
                 f"-I{MONOCYPHER}", str(SRC / "identity.c"), str(MONOCYPHER / "monocypher.c"),
                 str(MONOCYPHER / "monocypher-ed25519.c")],
    "playlist": ["-DGOLDEN_SHA256_PLAYLIST", f"-I{SRC}", f"-I{MONOCYPHER}", str(SRC / "playlist_manifest.c"),
                 str(MONOCYPHER / "monocypher.c"), str(MONOCYPHER / "monocypher-ed25519.c")],
    "update": ["-DGOLDEN_SHA256_UPDATE", f"-I{SRC}", str(SRC / "update_manifest.c")],
    # (mcc_sha256.h's names; their own unit until the one SHA-256 is there)
    "mcc": ["-DGOLDEN_SHA256_MCC", "-DHALO_MCC=1", f"-I{GAME}",
            *([str(GAME / "mcc_sha256.c")] if (GAME / "mcc_sha256.c").exists() else [])],
}


# the one SHA-256 every copy becomes a wrapper of, once it is there (#1763)
CONTENT_SHA256 = GAME / "content_sha256.c"
if CONTENT_SHA256.exists():
    for _units in SHA256_BUILDS.values():
        _units += [f"-I{GAME}", str(CONTENT_SHA256)]


def golden_bytes(size, seed):
    """content_golden.c's golden_bytes"""
    value = seed or 1
    out = bytearray(size)
    for index in range(size):
        value ^= (value << 13) & 0xFFFFFFFF
        value ^= value >> 17
        value ^= (value << 5) & 0xFFFFFFFF
        out[index] = value >> 24
    return bytes(out)


def expected_digest(case):
    if case == "text-abc":
        return hashlib.sha256(b"abc").hexdigest()
    if case == "text-448":
        return hashlib.sha256(b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").hexdigest()
    words = case.split("-")
    if words[0] == "bytes":
        return hashlib.sha256(golden_bytes(int(words[1]), int(words[2]))).hexdigest()
    if words[0] == "hmac":
        key = golden_bytes(int(words[1]), int(words[2]))
        return hmac.new(key, golden_bytes(int(words[3]), int(words[4])), hashlib.sha256).hexdigest()
    raise ValueError(case)


def sha256_lines(name, env=None):
    return [line.split() for line in run(build(f"sha256_{name}", SHA256_BUILDS[name]), env=env).splitlines()]


def check_sha256(name, env=None):
    lines = sha256_lines(name, env)
    assert len(lines) > 300, f"{name}: {len(lines)} digests"
    wrong = [case for case, digest in lines if digest != expected_digest(case)]
    assert not wrong, f"{name}: {len(wrong)} digests are not SHA-256's, the first {wrong[0]}"
    return lines


def sha256_names():
    return list(SHA256_BUILDS)


if pytest:
    @pytest.mark.parametrize("name", sha256_names())
    def test_sha256_copies_give_hashlib_digests(name):
        check_sha256(name)

    def test_mcc_sha256_portable_code_gives_the_same():
        """MCC_SHA256_PORTABLE: the portable code, where the processor's
        instructions would be used"""
        check_sha256("mcc", env={"MCC_SHA256_PORTABLE": "1"})


# ---------- Ogg Vorbis

# (ce_vorbis.c's and mcc_vorbis.c's, or their one home once it is there: #1766)
VORBIS_BUILD = ["-DGOLDEN_VORBIS", "-DHALO_CUSTOM_EDITION", "-DHALO_MCC=1", f"-I{STB}", f"-I{GAME}",
                *(str(GAME / name) for name in ("ce_vorbis.c", "mcc_vorbis.c", "content_vorbis.c")
                  if (GAME / name).exists())]

# what each decoder gave on the code before the cleanup (--print)
VORBIS_GOLDEN = """
noise-22k.ogg ce-decode 13248 2 22050 6ede2689864b4ee9
noise-22k.ogg ce-resample-11025 6624 5f051516c4593595
noise-22k.ogg ce-resample-22050 13248 6ede2689864b4ee9
noise-22k.ogg ce-resample-44100 26496 045aac0901ab5749
noise-22k.ogg ce-edges-0 48 2 22050 48 0 5f3c45421402f619 ab0c262759a1d225
noise-22k.ogg ce-edges-1 13248 2 22050 48 48 5f3c45421402f619 e3be54f837c86995
noise-22k.ogg ce-stream 1 13248 c7cf7136bf250001
noise-22k.ogg mcc-decode 1 13248 22050 2 6ede2689864b4ee9
noise-22k.ogg/half ce-decode 2048 2 22050 7e7dc1115e2a38f9
noise-22k.ogg/half ce-resample-11025 1024 36a5d41f3714ffe5
noise-22k.ogg/half ce-resample-22050 2048 7e7dc1115e2a38f9
noise-22k.ogg/half ce-resample-44100 4096 a8468697272daf21
noise-22k.ogg/half ce-edges-0 48 2 22050 48 0 5f3c45421402f619 ab0c262759a1d225
noise-22k.ogg/half ce-edges-1 2048 2 22050 48 48 5f3c45421402f619 83fa645267a0c15d
noise-22k.ogg/half ce-stream 1 2048 b6ed1585e93e470e
noise-22k.ogg/half mcc-decode 0 0 0 0 0000000000000000
noise-22k.ogg/flip1 ce-decode -1 0 0 0000000000000000
noise-22k.ogg/flip1 ce-edges-0 -1 0 0 0 0 ab0c262759a1d225 ab0c262759a1d225
noise-22k.ogg/flip1 ce-edges-1 -1 0 0 0 0 ab0c262759a1d225 ab0c262759a1d225
noise-22k.ogg/flip1 ce-stream 0 0 cbf29ce484222325
noise-22k.ogg/flip1 mcc-decode 0 0 0 0 0000000000000000
noise-22k.ogg/flip2 ce-decode 13248 2 22050 e352000863c410b1
noise-22k.ogg/flip2 ce-resample-11025 6624 33590ada7c71f3ad
noise-22k.ogg/flip2 ce-resample-22050 13248 e352000863c410b1
noise-22k.ogg/flip2 ce-resample-44100 26496 3e8988e33d61a535
noise-22k.ogg/flip2 ce-edges-0 48 2 22050 48 0 5f3c45421402f619 ab0c262759a1d225
noise-22k.ogg/flip2 ce-edges-1 13248 2 22050 48 48 5f3c45421402f619 e3be54f837c86995
noise-22k.ogg/flip2 ce-stream 1 13248 ca3ff4fb5ddea4a1
noise-22k.ogg/flip2 mcc-decode 0 0 0 0 0000000000000000
noise-22k.ogg/flip3 ce-decode 13248 2 22050 e95f5fcf01c1e351
noise-22k.ogg/flip3 ce-resample-11025 6624 810ffc014a62f0e2
noise-22k.ogg/flip3 ce-resample-22050 13248 e95f5fcf01c1e351
noise-22k.ogg/flip3 ce-resample-44100 26496 d05c881c71d45792
noise-22k.ogg/flip3 ce-edges-0 48 2 22050 48 0 5f3c45421402f619 ab0c262759a1d225
noise-22k.ogg/flip3 ce-edges-1 13248 2 22050 48 48 5f3c45421402f619 e3be54f837c86995
noise-22k.ogg/flip3 ce-stream 1 13248 7c43ec61b173d80f
noise-22k.ogg/flip3 mcc-decode 0 0 0 0 0000000000000000
one-ms.ogg ce-decode 64 2 44100 f628934f6554d3fd
one-ms.ogg ce-resample-11025 -1 0000000000000000
one-ms.ogg ce-resample-22050 32 35db9a915c9e1675
one-ms.ogg ce-resample-44100 64 f628934f6554d3fd
one-ms.ogg ce-edges-0 48 2 44100 48 0 610f796e4a9fd4dd ab0c262759a1d225
one-ms.ogg ce-edges-1 64 2 44100 48 48 610f796e4a9fd4dd 505cb2ad2760cab9
one-ms.ogg ce-stream 1 64 907979101d1b3f08
one-ms.ogg mcc-decode 1 64 44100 2 f628934f6554d3fd
one-ms.ogg/half ce-decode -1 0 0 0000000000000000
one-ms.ogg/half ce-edges-0 -1 0 0 0 0 ab0c262759a1d225 ab0c262759a1d225
one-ms.ogg/half ce-edges-1 -1 0 0 0 0 ab0c262759a1d225 ab0c262759a1d225
one-ms.ogg/half ce-stream 0 0 cbf29ce484222325
one-ms.ogg/half mcc-decode 0 0 0 0 0000000000000000
one-ms.ogg/flip1 ce-decode -1 0 0 0000000000000000
one-ms.ogg/flip1 ce-edges-0 -1 0 0 0 0 ab0c262759a1d225 ab0c262759a1d225
one-ms.ogg/flip1 ce-edges-1 -1 0 0 0 0 ab0c262759a1d225 ab0c262759a1d225
one-ms.ogg/flip1 ce-stream 0 0 cbf29ce484222325
one-ms.ogg/flip1 mcc-decode 0 0 0 0 0000000000000000
one-ms.ogg/flip2 ce-decode -1 0 0 0000000000000000
one-ms.ogg/flip2 ce-edges-0 -1 0 0 0 0 ab0c262759a1d225 ab0c262759a1d225
one-ms.ogg/flip2 ce-edges-1 -1 0 0 0 0 ab0c262759a1d225 ab0c262759a1d225
one-ms.ogg/flip2 ce-stream 0 0 cbf29ce484222325
one-ms.ogg/flip2 mcc-decode 0 0 0 0 0000000000000000
one-ms.ogg/flip3 ce-decode 64 2 44100 f628934f6554d3fd
one-ms.ogg/flip3 ce-resample-11025 -1 0000000000000000
one-ms.ogg/flip3 ce-resample-22050 32 35db9a915c9e1675
one-ms.ogg/flip3 ce-resample-44100 64 f628934f6554d3fd
one-ms.ogg/flip3 ce-edges-0 48 2 44100 48 0 610f796e4a9fd4dd ab0c262759a1d225
one-ms.ogg/flip3 ce-edges-1 64 2 44100 48 48 610f796e4a9fd4dd 505cb2ad2760cab9
one-ms.ogg/flip3 ce-stream 1 64 907979101d1b3f08
one-ms.ogg/flip3 mcc-decode 0 0 0 0 0000000000000000
tone-noise-44k.ogg ce-decode 52928 2 44100 063191a31a8bf6e1
tone-noise-44k.ogg ce-resample-11025 -1 0000000000000000
tone-noise-44k.ogg ce-resample-22050 26464 785fa4cac4507065
tone-noise-44k.ogg ce-resample-44100 52928 063191a31a8bf6e1
tone-noise-44k.ogg ce-edges-0 48 2 44100 48 0 f31c127b2cdf9959 ab0c262759a1d225
tone-noise-44k.ogg ce-edges-1 52928 2 44100 48 48 f31c127b2cdf9959 fb446f85f58b1061
tone-noise-44k.ogg ce-stream 1 52928 482c99e6d2595fde
tone-noise-44k.ogg mcc-decode 1 52928 44100 2 063191a31a8bf6e1
tone-noise-44k.ogg/half ce-decode 12288 2 44100 95448a544ce1f855
tone-noise-44k.ogg/half ce-resample-11025 -1 0000000000000000
tone-noise-44k.ogg/half ce-resample-22050 6144 e43131f989e1cb0d
tone-noise-44k.ogg/half ce-resample-44100 12288 95448a544ce1f855
tone-noise-44k.ogg/half ce-edges-0 48 2 44100 48 0 f31c127b2cdf9959 ab0c262759a1d225
tone-noise-44k.ogg/half ce-edges-1 12288 2 44100 48 48 f31c127b2cdf9959 10f9c622a6a8a76d
tone-noise-44k.ogg/half ce-stream 1 12288 864a4953f1b7ed94
tone-noise-44k.ogg/half mcc-decode 0 0 0 0 0000000000000000
tone-noise-44k.ogg/flip1 ce-decode 52928 2 44100 063191a31a8bf6e1
tone-noise-44k.ogg/flip1 ce-resample-11025 -1 0000000000000000
tone-noise-44k.ogg/flip1 ce-resample-22050 26464 785fa4cac4507065
tone-noise-44k.ogg/flip1 ce-resample-44100 52928 063191a31a8bf6e1
tone-noise-44k.ogg/flip1 ce-edges-0 48 2 44100 48 0 f31c127b2cdf9959 ab0c262759a1d225
tone-noise-44k.ogg/flip1 ce-edges-1 52928 2 44100 48 48 f31c127b2cdf9959 fb446f85f58b1061
tone-noise-44k.ogg/flip1 ce-stream 1 52928 482c99e6d2595fde
tone-noise-44k.ogg/flip1 mcc-decode 0 0 0 0 0000000000000000
tone-noise-44k.ogg/flip2 ce-decode 52928 2 44100 d0ff5f71d88a092b
tone-noise-44k.ogg/flip2 ce-resample-11025 -1 0000000000000000
tone-noise-44k.ogg/flip2 ce-resample-22050 26464 82b4bcfe71d3f0b0
tone-noise-44k.ogg/flip2 ce-resample-44100 52928 d0ff5f71d88a092b
tone-noise-44k.ogg/flip2 ce-edges-0 48 2 44100 48 0 f31c127b2cdf9959 ab0c262759a1d225
tone-noise-44k.ogg/flip2 ce-edges-1 52928 2 44100 48 48 f31c127b2cdf9959 fb446f85f58b1061
tone-noise-44k.ogg/flip2 ce-stream 1 52928 1643104dbe528768
tone-noise-44k.ogg/flip2 mcc-decode 0 0 0 0 0000000000000000
tone-noise-44k.ogg/flip3 ce-decode 52928 2 44100 edc1c5a3f2a89fae
tone-noise-44k.ogg/flip3 ce-resample-11025 -1 0000000000000000
tone-noise-44k.ogg/flip3 ce-resample-22050 26464 2da4fc83f0fe51db
tone-noise-44k.ogg/flip3 ce-resample-44100 52928 edc1c5a3f2a89fae
tone-noise-44k.ogg/flip3 ce-edges-0 48 2 44100 48 0 f31c127b2cdf9959 ab0c262759a1d225
tone-noise-44k.ogg/flip3 ce-edges-1 52928 2 44100 48 48 f31c127b2cdf9959 fb446f85f58b1061
tone-noise-44k.ogg/flip3 ce-stream 1 52928 2c64366897df3467
tone-noise-44k.ogg/flip3 mcc-decode 0 0 0 0 0000000000000000
""".strip()


def vorbis_lines():
    if not VORBIS_FILES:
        skip("no Ogg files in tools/tests/vorbis")
    return run(build("vorbis", VORBIS_BUILD), *VORBIS_FILES).strip()


def test_vorbis_decoders_give_the_same_samples():
    got = vorbis_lines().splitlines()
    want = VORBIS_GOLDEN.splitlines()
    differ = [f"{a}  (was {b})" for a, b in zip(got, want) if a != b]
    assert not differ and len(got) == len(want), "\n".join(differ[:20]) or f"{len(got)} lines, {len(want)} kept"


# ---------- IMA ADPCM

def function(source, name):
    """a static function's text, by name"""
    match = re.search(r"^static [\w *]+\b" + name + r"\(", source, re.M)
    if not match:
        raise ValueError(f"function not found: {name}")
    start = source.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def adpcm_code():
    """the mixer's decoder (from its tables to decode_adpcm) and MCC's IMA
    coder (from struct mcc_ima to mcc_ima_quantize), cut out of their files"""
    mixer = (SRC / "dsound_sdl.c").read_text()
    mcc = (GAME / "mcc_audio.c").read_text()
    code = ["#define XBOX_ADPCM_BLOCK_BYTES 36", "#define XBOX_ADPCM_BLOCK_SAMPLES 64",
            "#define CEILING(n, ceiling) ((n) > (ceiling) ? (ceiling) : (n))",
            "#define PIN(n, floor, ceiling) ((n) < (floor) ? (floor) : CEILING((n), (ceiling)))"]
    start = mixer.index("/* ---------- decoding")
    code.append(mixer[start:mixer.index("/* Xbox ADPCM: per block", start)])
    code.append(function(mixer, "decode_adpcm"))
    start = mcc.index("struct mcc_ima {")
    code.append(mcc[start:mcc.index("static int mcc_ima_advance", start)])
    code.append(function(mcc, "mcc_ima_advance"))
    code.append(function(mcc, "mcc_ima_quantize"))
    if CONTENT_AUDIO.exists():
        code.insert(0, '#include "content_audio.h"')
    return "\n".join(code)


# the one IMA ADPCM table and step, once they are there (#1767): the mixer's
# step is then content_audio.c's
CONTENT_AUDIO = GAME / "content_audio.c"
ADPCM_EXPAND = "content_ima_expand" if CONTENT_AUDIO.exists() else "ima_expand"
ADPCM_UNITS = [f"-I{GAME}", str(CONTENT_AUDIO)] if CONTENT_AUDIO.exists() else []

ADPCM_GOLDEN = """
mixer-1-0 0 cbf29ce484222325
mixer-1-36 64 b862aa1a46fb6053
mixer-1-72 128 e1a2d3c1e14813a0
mixer-1-3600 6400 4ff81721647ff14e
mixer-1-2664 4736 83cf6eece652c55d
mixer-2-0 0 cbf29ce484222325
mixer-2-36 0 cbf29ce484222325
mixer-2-72 64 96ad6e7052b69bd6
mixer-2-3600 3200 5663652b588bc88a
mixer-2-2664 2368 18eabda362e97915
mcc-advance e24191fa78142d45
mcc-quantize 15760e158ee4a629 13353 55
mixer-expand e24191fa78142d45
""".strip()


def adpcm_lines():
    source = folder() / "content_golden_adpcm.c"
    source.write_text(DRIVER.read_text().replace("@GOLDEN_ADPCM_CODE@", adpcm_code()))
    return run(build("adpcm", ["-DGOLDEN_ADPCM", f"-DGOLDEN_MIXER_EXPAND={ADPCM_EXPAND}", *ADPCM_UNITS],
                     source)).strip()


def test_adpcm_coders_give_the_same_samples():
    got = adpcm_lines().splitlines()
    want = ADPCM_GOLDEN.splitlines()
    differ = [f"{a}  (was {b})" for a, b in zip(got, want) if a != b]
    assert not differ and len(got) == len(want), "\n".join(differ) or f"{len(got)} lines, {len(want)} kept"


def numbers(text, name):
    """a C table's numbers, by its name"""
    match = re.search(name + r"\s*\[[^\]]*\]\s*=\s*\{([^}]*)\}", text)
    assert match, f"no table {name}"
    return [int(word) for word in re.findall(r"-?\d+", match.group(1))]


def test_adpcm_step_tables_are_the_retail_games():
    retail = (ROOT / "source" / "sound" / "ima_adpcm.c").read_text()
    steps = numbers(retail, "step_size_table")
    changes = numbers(retail, "step_size_adjustment_table")
    assert len(steps) == 89 and steps[0] == 7 and steps[-1] == 32767
    assert len(changes) == 16
    for path, step_name, change_name in ADPCM_TABLES:
        text = path.read_text()
        assert numbers(text, step_name) == steps, f"{path.name}'s {step_name}"
        change = numbers(text, change_name)
        assert change == changes[:len(change)], f"{path.name}'s {change_name}"


# the step tables outside the retail game's, and their adjustment tables
ADPCM_TABLES = [(CONTENT_AUDIO, "content_ima_steps", "content_ima_changes")] if CONTENT_AUDIO.exists() else [
    (SRC / "dsound_sdl.c", "ima_step_table", "ima_index_table"),
    (GAME / "mcc_audio.c", "mcc_ima_steps", "mcc_ima_changes"),
]


def main():
    if "--print" in sys.argv:
        print("VORBIS")
        print(vorbis_lines())
        print("ADPCM")
        print(adpcm_lines())
        for name in sha256_names():
            check_sha256(name)
            print(f"sha256 {name}: hashlib's")
        return 0
    return subprocess.call([sys.executable, "-m", "pytest", "-q", __file__])


if __name__ == "__main__":
    sys.exit(main())
