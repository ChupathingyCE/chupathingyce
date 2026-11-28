#!/usr/bin/env python3
"""A list of what the map loaders' shared pieces make of real maps, to keep
from one build of the code and hold against another (#1771's cleanup: the
pieces moved must give the same):

- each file's SHA-256 from every copy of it (tools/test_content_golden.py's
  builds), which must all be Python's hashlib digest;
- each Ogg Vorbis stream in the files (Custom Edition, HaloMD, MCC and SPV3
  maps and their sounds.map), through ce_vorbis.c's decoders and resampler
  and mcc_vorbis.c's decoder: their samples' check values;
- each file's bytes (an Xbox map's inflated as the game reads it) as Xbox
  ADPCM blocks, through the desktop mixer's decoder and MCC's.

A tool, never part of a game build. Nothing is copied out of the maps but
into a scratch folder that is deleted (the Ogg streams, the inflated maps:
a few GB for the Xbox maps, in TMPDIR, so point that at a roomy disk).

    python3 tools/content_hashes.py --out before.txt ~/halo-maps/maps ~/halo-maps/maps_ce
    python3 tools/content_hashes.py --compare before.txt ~/halo-maps/maps ~/halo-maps/maps_ce
"""

import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools import test_content_golden as golden  # noqa: E402

BATCH = 200


def map_files(paths):
    files = []
    for path in map(Path, paths):
        if path.is_dir():
            files += sorted(p for p in path.rglob("*.map") if p.is_file())
        elif path.is_file():
            files.append(path)
    return files


def xbox_inflated(path, scratch):
    """an Xbox map as the game reads it (its tags inflated after its header),
    in scratch; other maps as they are"""
    data = path.read_bytes()
    if len(data) < 0x800 or data[:4] != b"daeh" or struct.unpack_from("<i", data, 4)[0] != 5:
        return path
    file_length = struct.unpack_from("<i", data, 8)[0]
    if len(data) == file_length:
        return path
    try:
        inflated = data[:0x800] + zlib.decompress(data[0x800:])
    except zlib.error:
        return path
    out = scratch / f"inflated-{hashlib.sha256(str(path).encode()).hexdigest()[:12]}.map"
    out.write_bytes(inflated)
    return out


def ogg_streams(data):
    """each whole Ogg stream in data, from its first page to its last: (offset,
    bytes)"""
    streams = []
    at = data.find(b"OggS")
    while at >= 0:
        start, serial, ok, end = at, None, False, at
        while data[end:end + 4] == b"OggS" and end + 27 <= len(data):
            flags = data[end + 5]
            segments = data[end + 26]
            if end + 27 + segments > len(data):
                break
            length = 27 + segments + sum(data[end + 27:end + 27 + segments])
            page_serial = data[end + 14:end + 18]
            if end == start:
                if not flags & 2:
                    break
                serial = page_serial
            elif page_serial != serial:
                break
            end += length
            if flags & 4:
                ok = True
                break
        if ok and end <= len(data):
            streams.append((start, data[start:end]))
            at = data.find(b"OggS", end)
        else:
            at = data.find(b"OggS", at + 4)
    return streams


def run_batches(binary, paths, env=None):
    lines = []
    for index in range(0, len(paths), BATCH):
        lines += golden.run(binary, *paths[index:index + BATCH], env=env).splitlines()
    return lines


def content_hashes(paths, scratch):
    files = map_files(paths)
    names = {str(path): str(path) for path in files}
    lines = []
    # SHA-256: every copy, every file
    for name in golden.sha256_names():
        builds = [({}, name)] + ([({"MCC_SHA256_PORTABLE": "1"}, "mcc-portable")] if name == "mcc" else [])
        for env, label in builds:
            binary = golden.build(f"sha256_{name}", golden.SHA256_BUILDS[name])
            for line in run_batches(binary, [str(path) for path in files], env=env):
                path, digest = line.rsplit(" ", 1)
                want = hashlib.sha256(Path(path).read_bytes()).hexdigest()
                if digest != want:
                    raise SystemExit(f"{label}: {path}: {digest}, not SHA-256's {want}")
    lines += [f"sha256 {path} {hashlib.sha256(path.read_bytes()).hexdigest()}" for path in files]
    # Ogg Vorbis streams
    oggs = []
    for path in files:
        for offset, stream in ogg_streams(path.read_bytes()):
            ogg = scratch / f"ogg-{len(oggs)}.ogg"
            ogg.write_bytes(stream)
            names[ogg.name] = f"{path}@{offset:x}"
            oggs.append(str(ogg))
    binary = golden.build("vorbis", golden.VORBIS_BUILD)
    for line in run_batches(binary, oggs, env={"GOLDEN_VORBIS_WHOLE": "1"}):
        name, rest = line.split(" ", 1)
        lines.append(f"vorbis {names[name]} {rest}")
    # Xbox ADPCM
    source = golden.folder() / "content_golden_adpcm.c"
    source.write_text(golden.DRIVER.read_text().replace("@GOLDEN_ADPCM_CODE@", golden.adpcm_code()))
    binary = golden.build("adpcm", ["-DGOLDEN_ADPCM", f"-DGOLDEN_MIXER_EXPAND={golden.ADPCM_EXPAND}",
                                    *golden.ADPCM_UNITS], source)
    inflated = {str(xbox_inflated(path, scratch)): str(path) for path in files}
    for line in run_batches(binary, list(inflated)):
        path, rest = line.split(" ", 1)
        lines.append(f"adpcm {inflated[path]} {rest}")
    return lines, len(files), len(oggs)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", help="map folders or files")
    parser.add_argument("--out", help="write the list here")
    parser.add_argument("--compare", help="a list written before: every line must be the same")
    args = parser.parse_args()
    scratch = Path(tempfile.mkdtemp(prefix="content_hashes_"))
    try:
        lines, file_count, ogg_count = content_hashes(args.paths, scratch)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    print(f"content hashes: {file_count} files, {ogg_count} Ogg streams, {len(lines)} lines")
    if args.out:
        Path(args.out).write_text("\n".join(lines) + "\n")
    if args.compare:
        kept = Path(args.compare).read_text().splitlines()
        if kept != lines:
            differ = [f"- {a}\n+ {b}" for a, b in zip(kept, lines) if a != b]
            print("\n".join(differ[:40]) or f"{len(kept)} lines kept, {len(lines)} now")
            print("content hashes: DIFFERENT")
            return 1
        print("content hashes: the same")
    return 0


if __name__ == "__main__":
    sys.exit(main())
