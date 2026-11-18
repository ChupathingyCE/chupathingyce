#!/usr/bin/env python3
"""Puts function names and source lines on the crash lines of a Windows
build's debug.txt or halo.log (port/windows/src/win32_crash.c), or on the
calls of a crash report as the site keeps it (chupathingy.exe+0x1a2b3c, one
a line: port/linux/src/crash_report.h):

    python tools/symbolize_crash.py logs/debug.txt path/to/chupathingy.exe

(Both logs are in logs/ beside maps/ since 0.8.0, or in the save folder's
logs/ when a custom save folder is set (paths.saves, HALO_SAVE_ROOT), or in
paths.logs when that is set. Before 0.8.0, debug.txt was beside maps/ and
halo.log beside the program.)

The program must be the build that crashed (32- or 64-bit), with its .pdb
beside it: chupathingy.exe and chupathingy.pdb from 0.9.0, halo.exe and
halo.pdb before (a release's symbols, chupathingyce-<platform>-<config>-symbols,
have the PDB and the linker's map; the release's zip has the program). The
lines come from llvm-symbolizer, which reads the PDB. Crash lines name the
game by either name, halo.exe or chupathingy.exe, so reports from before and
after the rename both work, whichever program is given.

A crash report's calls, on any build, against the build's kept symbols (a
release's or a nightly's symbols-<version> in the private repository):

    python tools/symbolize_crash.py --symbols chupathingy.map +0x7e8697 +0x777960
    python tools/symbolize_crash.py --symbols chupathingy.pdb report.json
    python tools/symbolize_crash.py --symbols chupathingy +0x1a2b3c       (Linux)
    python tools/symbolize_crash.py --symbols chupathingy.dSYM --arch arm64 +0x1a2b3c
    python tools/symbolize_crash.py --symbols halo.map --absolute 0x17E13F (original Xbox)

The calls are offsets in the game's program (module+0x..., +0x..., or a
report: the site's JSON, or the report file's "frame" lines). The symbols
are the linker's map (read here, without LLVM: function names only), or a
PDB (with its program beside it), a Linux program or library, or a .dSYM
(llvm-symbolizer: names and lines). A report's build_id (crash_report.h) is
checked against the symbols' own: a PDB's GUID and age, an ELF's GNU build
ID, a Mach-O's UUID; a map has none, so check its version and commit.
"""

import argparse
import bisect
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

# the game's executable, by its names before and after the rename
GAME_MODULE = r"(?:halo|chupathingy)\.exe"
# the crash lines with an address in them (win32_crash.c's crash_filter)
BASE = re.compile(r"crash: " + GAME_MODULE + r" at (?:0x)?([0-9A-Fa-f]+)")
EXCEPTION = re.compile(r"crash: exception \w+ at (?:0x)?([0-9A-Fa-f]+)")
CALLER = re.compile(r"crash: called from (?:0x)?([0-9A-Fa-f]+)")
# a crash report's call: an offset in the game's executable already
FRAME = re.compile(r"\b" + GAME_MODULE + r"\+0x([0-9A-Fa-f]+)", re.IGNORECASE)
ANY_FRAME = re.compile(r"[\w.?-]\+0x[0-9A-Fa-f]+")


def image_layout(executable: Path) -> tuple:
    """the address the game's program is linked at and its size in memory (its PE
    optional header's ImageBase and SizeOfImage)"""
    data = executable.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    magic = struct.unpack_from("<H", data, pe + 24)[0] if data[pe:pe + 4] == b"PE\0\0" else 0
    if magic == 0x10B:
        base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    elif magic == 0x20B:
        # (PE32+, the 64-bit build: an 8-byte ImageBase)
        base = struct.unpack_from("<Q", data, pe + 24 + 24)[0]
    else:
        sys.exit(f"{executable} is not a Windows executable")
    return base, struct.unpack_from("<I", data, pe + 24 + 56)[0]


def symbolize(symbolizer: str, executable: Path, addresses: list) -> dict:
    """{address: [lines of llvm-symbolizer's answer]}"""
    if not addresses:
        return {}
    output = subprocess.run(
        [symbolizer, f"--obj={executable}", "--inlines", "--functions=linkage", "--relative-address"]
        + [hex(address) for address in addresses],
        capture_output=True, text=True, check=True).stdout
    answers = output.strip("\n").split("\n\n")
    return {address: answer.splitlines() for address, answer in zip(addresses, answers)}


# ---------- a crash report's calls, against a build's kept symbols

# a call: "chupathingy.exe+0x1a2b3c", "+0x1a2b3c" or (--absolute) "0x17E13F"
CALL = re.compile(r"^(?:(?P<module>[^\s+]*)\+)?(?:0x)?(?P<offset>[0-9A-Fa-f]+)$")
# a map's symbol: "0001:00012345  _name  0000000140013345  f  object.obj"
MAP_SYMBOL = re.compile(r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})\s+(\S+)\s+([0-9A-Fa-f]{8,16})\s+(?:f\s+)?(?:i\s+)?(.*)$")
MAP_BASE = re.compile(r"Preferred load address is\s+([0-9A-Fa-f]+)")


def report_calls(texts: list) -> tuple:
    """the calls named (module, offset) and the report's build_id (or None),
    from the arguments: calls, or a report's file (the site's JSON, or the
    report file's key-value lines)"""
    calls, build_id = [], None
    for text in texts:
        path = Path(text)
        if path.is_file():
            content = path.read_text(encoding="utf-8", errors="replace")
            try:
                report = json.loads(content)
            except ValueError:
                report = None
            if isinstance(report, dict):
                build_id = report.get("build_id") or build_id
                frames = report.get("frames", [])
            else:
                frames = []
                for line in content.splitlines():
                    if line.startswith("frame "):
                        frames.append(line[6:].strip())
                    elif line.startswith("build_id "):
                        build_id = line[9:].strip()
            for frame in frames:
                match = CALL.match(frame.strip())
                calls.append((match.group("module") or "", int(match.group("offset"), 16)) if match else (frame, None))
            continue
        for word in text.replace(",", " ").split():
            match = CALL.match(word)
            if not match:
                sys.exit(f"{word}: not a call (module+0xOFFSET, +0xOFFSET) or a report's file")
            calls.append((match.group("module") or "", int(match.group("offset"), 16)))
    return calls, build_id


def map_symbols(path: Path) -> tuple:
    """a linker's map (lld-link's /MAP, MSVC's; the original Xbox's
    halo.map): its preferred base and its symbols, by address"""
    base, symbols = None, []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if base is None:
            match = MAP_BASE.search(line)
            if match:
                base = int(match.group(1), 16)
                continue
        match = MAP_SYMBOL.match(line)
        # (section 0000: absolute symbols, not code)
        if match and match.group(1) != "0000":
            symbols.append((int(match.group(4), 16), match.group(3), match.group(5).strip()))
    symbols.sort()
    return base or 0, symbols


def map_lookup(symbols: list, address: int) -> str:
    """the symbol an address is in, as name+0xoffset (object)"""
    index = bisect.bisect_right([symbol[0] for symbol in symbols], address) - 1
    if index < 0:
        return "?"
    start, name, owner = symbols[index]
    return f"{name}+0x{address - start:x}" + (f"  ({owner})" if owner else "")


def pe_codeview_id(data: bytes):
    """a PE program's PDB id (its CodeView record's GUID and age, as a
    symbol server names them, and as the Windows build's crash reports send
    it), or None"""
    try:
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            return None
        sections = struct.unpack_from("<H", data, pe + 6)[0]
        optional_size = struct.unpack_from("<H", data, pe + 20)[0]
        optional = pe + 24
        magic = struct.unpack_from("<H", data, optional)[0]
        directories = optional + (96 if magic == 0x10B else 112)
        debug_rva, debug_size = struct.unpack_from("<II", data, directories + 6 * 8)
        # (the RVA as a file offset, by the section headers)
        table = optional + optional_size
        for index in range(sections):
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, table + index * 40 + 8)
            for entry in range(debug_size // 28):
                rva = debug_rva + entry * 28
                if not virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
                    continue
                at = rva - virtual_address + raw_pointer
                kind, size, _, pointer = struct.unpack_from("<IIII", data, at + 12)
                if kind == 2 and data[pointer:pointer + 4] == b"RSDS":
                    return codeview_id(data[pointer + 4:pointer + 24])
    except struct.error:
        return None
    return None


def codeview_id(record: bytes) -> str:
    """a GUID (16 bytes) and age (4) as one id: DB9B4BE72A0819C44C4C44205044422E1"""
    data1, data2, data3 = struct.unpack_from("<IHH", record, 0)
    age = struct.unpack_from("<I", record, 16)[0]
    return f"{data1:08X}{data2:04X}{data3:04X}{record[8:16].hex().upper()}{age:X}"


def pdb_id(path: Path, tool: str):
    """a PDB's GUID and age (llvm-pdbutil), or None"""
    if not tool:
        return None
    output = subprocess.run([tool, "dump", "--summary", str(path)], capture_output=True, text=True).stdout
    guid = re.search(r"GUID:\s*\{([0-9A-Fa-f-]+)\}", output)
    age = re.search(r"Age:\s*(\d+)", output)
    if not guid or not age:
        return None
    parts = guid.group(1).split("-")
    return f"{parts[0]}{parts[1]}{parts[2]}{parts[3]}{parts[4]}".upper() + f"{int(age.group(1)):X}"


def elf_build_id(data: bytes):
    """an ELF file's GNU build ID (its NT_GNU_BUILD_ID note), hex, or None"""
    if data[:4] != b"\x7fELF":
        return None
    wide, little = data[4] == 2, data[5] == 1
    order = "<" if little else ">"
    try:
        if wide:
            section_offset = struct.unpack_from(order + "Q", data, 0x28)[0]
            entry_size, count = struct.unpack_from(order + "HH", data, 0x3A)
        else:
            section_offset = struct.unpack_from(order + "I", data, 0x20)[0]
            entry_size, count = struct.unpack_from(order + "HH", data, 0x2E)
        for index in range(count):
            at = section_offset + index * entry_size
            if wide:
                kind = struct.unpack_from(order + "I", data, at + 4)[0]
                offset, size = struct.unpack_from(order + "QQ", data, at + 24)
            else:
                kind = struct.unpack_from(order + "I", data, at + 4)[0]
                offset, size = struct.unpack_from(order + "II", data, at + 16)
            if kind != 7:
                continue
            end, note = offset + size, offset
            while note + 12 <= end:
                name_size, descriptor_size, note_type = struct.unpack_from(order + "III", data, note)
                descriptor = note + 12 + (name_size + 3) // 4 * 4
                if note_type == 3 and data[note + 12:note + 16] == b"GNU\0":
                    return data[descriptor:descriptor + descriptor_size].hex()
                note = descriptor + (descriptor_size + 3) // 4 * 4
    except struct.error:
        return None
    return None


def elf_load_base(data: bytes) -> int:
    """the address an ELF program's offsets count from: 0 for a
    position-independent one, else its first loaded segment's page"""
    wide, order = data[4] == 2, "<" if data[5] == 1 else ">"
    kind = struct.unpack_from(order + "H", data, 16)[0]
    if kind != 2:
        return 0
    if wide:
        header, entry_size, count = struct.unpack_from(order + "Q", data, 0x20)[0], *struct.unpack_from(order + "HH", data, 0x36)
    else:
        header, entry_size, count = struct.unpack_from(order + "I", data, 0x1C)[0], *struct.unpack_from(order + "HH", data, 0x2A)
    for index in range(count):
        at = header + index * entry_size
        if struct.unpack_from(order + "I", data, at)[0] == 1:
            address = struct.unpack_from(order + ("Q" if wide else "I"), data, at + (16 if wide else 8))[0]
            return address & ~0xFFF
    return 0


def macho_uuids(path: Path) -> dict:
    """a Mach-O file's (or a .dSYM's) UUIDs by architecture, as the macOS
    build's crash reports send them (dwarfdump --uuid's form)"""
    if path.is_dir():
        found = sorted((path / "Contents" / "Resources" / "DWARF").glob("*"))
        if not found:
            return {}
        path = found[0]
    data = path.read_bytes()
    uuids = {}

    def thin(at: int):
        magic = struct.unpack_from("<I", data, at)[0]
        if magic not in (0xFEEDFACF, 0xFEEDFACE):
            return
        cpu, _, _, count, _ = struct.unpack_from("<iiiII", data, at + 4)
        command = at + (32 if magic == 0xFEEDFACF else 28)
        for _ in range(count):
            kind, size = struct.unpack_from("<II", data, command)
            if kind == 0x1B:
                raw = data[command + 8:command + 24].hex().upper()
                arch = {0x01000007: "x86_64", 0x0100000C: "arm64"}.get(cpu, str(cpu))
                uuids[arch] = f"{raw[:8]}-{raw[8:12]}-{raw[12:16]}-{raw[16:20]}-{raw[20:]}"
            command += size

    if struct.unpack_from(">I", data, 0)[0] == 0xCAFEBABE:
        for index in range(struct.unpack_from(">I", data, 4)[0]):
            thin(struct.unpack_from(">IIIII", data, 8 + index * 20)[2])
    else:
        thin(0)
    return uuids


def symbolize_report(args) -> int:
    """--symbols: a crash report's calls named from a build's kept symbols"""
    calls, build_id = report_calls(args.calls)
    if not calls:
        sys.exit("no calls given")
    symbols_path = args.symbols
    symbolizer = shutil.which("llvm-symbolizer") or shutil.which("llvm-addr2line")
    llvm_bin = Path(symbolizer).parent if symbolizer else None
    own_id, lines = None, {}
    pdbutil = shutil.which("llvm-pdbutil") or (str(llvm_bin / "llvm-pdbutil")
                                               if llvm_bin and (llvm_bin / "llvm-pdbutil").exists() else None)
    if symbols_path.suffix.lower() == ".pdb":
        own_id = pdb_id(symbols_path, pdbutil)

    # (a return address is one past its call: every call after the first)
    def wanted(index: int, offset: int) -> int:
        return offset - (0 if index == 0 or args.exact else 1)

    # (a PDB without its program: the linker's map beside it, which the kept
    # symbols have, gives the names; llvm-symbolizer reads a PDB only
    # through its program)
    if symbols_path.suffix.lower() == ".pdb" and not any(
            candidate.is_file() for candidate in (symbols_path.with_suffix(".exe"), symbols_path.parent / "chupathingy.exe")):
        beside = symbols_path.with_suffix(".map")
        if not beside.is_file():
            sys.exit(f"{symbols_path.name}: put the build's program (from its download) beside it, or give its .map")
        print(f"note: no program beside {symbols_path.name}: names from {beside.name} (put the program beside the "
              "PDB for source lines)", file=sys.stderr)
        symbols_path = beside
    if symbols_path.suffix.lower() == ".map":
        base, table = map_symbols(symbols_path)
        for index, (module, offset) in enumerate(calls):
            if offset is not None:
                address = wanted(index, offset) + (0 if args.absolute else base)
                lines[index] = [map_lookup(table, address)]
    else:
        if not symbolizer:
            sys.exit("llvm-symbolizer is not in the PATH (it comes with LLVM); a .map file needs none")
        obj, extra, add = symbols_path, ["--relative-address"], 0
        if symbols_path.suffix.lower() == ".pdb":
            program = next((candidate for candidate in (symbols_path.with_suffix(".exe"),
                                                        symbols_path.parent / "chupathingy.exe")
                            if candidate.is_file()), None)
            obj = program
        elif symbols_path.suffix.lower() == ".exe":
            own_id = pe_codeview_id(symbols_path.read_bytes())
        elif symbols_path.suffix.lower() == ".dsym" or symbols_path.is_dir():
            uuids = macho_uuids(symbols_path)
            own_id = uuids.get(args.arch) if args.arch else (next(iter(uuids.values())) if len(uuids) == 1 else None)
            if build_id and build_id in uuids.values():
                own_id = build_id
                args.arch = args.arch or next(arch for arch, uuid in uuids.items() if uuid == build_id)
            extra = [f"--default-arch={args.arch}"] if args.arch else []
            add = 0x100000000
            # (the DWARF in the bundle)
            if symbols_path.is_dir():
                obj = next(iter(sorted((symbols_path / "Contents" / "Resources" / "DWARF").glob("*"))), symbols_path)
        else:
            data = symbols_path.read_bytes()
            own_id = elf_build_id(data)
            extra, add = [], elf_load_base(data)
        addresses = [(index, wanted(index, offset) + add) for index, (module, offset) in enumerate(calls)
                     if offset is not None]
        if addresses:
            output = subprocess.run([symbolizer, f"--obj={obj}", "--inlines", "--functions=linkage", *extra]
                                    + [hex(address) for _, address in addresses],
                                    capture_output=True, text=True, check=True).stdout
            for (index, _), answer in zip(addresses, output.strip("\n").split("\n\n")):
                answer = answer.splitlines()
                lines[index] = [f"{function}  {location}" for function, location in zip(answer[0::2], answer[1::2])]

    if build_id and own_id and build_id.lower() != own_id.lower():
        print(f"warning: the report's build_id {build_id} is not these symbols' ({own_id}): another build's",
              file=sys.stderr)
    elif build_id and not own_id:
        print(f"note: the report's build_id is {build_id}; {symbols_path.name} has none to check it against "
              "(check its version and commit)", file=sys.stderr)
    for index, (module, offset) in enumerate(calls):
        name = f"{module}+0x{offset:x}" if offset is not None else module
        answer = lines.get(index) or ["?"]
        print(f"#{index:<2} {name}")
        for line in answer:
            print(f"        {line}")
    return 0


def main() -> int:
    if "--symbols" in sys.argv[1:]:
        parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
        parser.add_argument("--symbols", type=Path, required=True,
                            help="the build's symbols: its linker's .map, its .pdb (the program beside it), "
                            "its Linux program or library, or its .dSYM")
        parser.add_argument("--absolute", action="store_true",
                            help="the calls are addresses, not offsets (the original Xbox's crash lines)")
        parser.add_argument("--exact", action="store_true",
                            help="every call is where it was (not a return address one past its call)")
        parser.add_argument("--arch", help="a universal .dSYM's architecture (arm64, x86_64)")
        parser.add_argument("calls", nargs="+", help="module+0xOFFSET or +0xOFFSET calls, or a report's file")
        return symbolize_report(parser.parse_args())

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log", type=Path, help="debug.txt or halo.log (chupathingy.log)")
    parser.add_argument("executable", type=Path, help="the chupathingy.exe (or, before 0.9.0, halo.exe) that crashed, its .pdb beside it")
    args = parser.parse_args()

    symbolizer = shutil.which("llvm-symbolizer") or shutil.which("llvm-addr2line")
    if not symbolizer:
        sys.exit("llvm-symbolizer is not in the PATH (it comes with LLVM)")
    if not args.executable.with_suffix(".pdb").is_file():
        print(f"warning: no {args.executable.with_suffix('.pdb').name} beside {args.executable}", file=sys.stderr)
    linked, size = image_layout(args.executable)

    lines = args.log.read_text(encoding="utf-8", errors="replace").splitlines()
    # each crash's addresses, relative to where the program was in that run (a
    # line before "<program> at" is from a build too old to say: linked base);
    # a caller's is its return address, one past the call
    base = linked
    wanted = {}
    first_frame = True
    for index, line in enumerate(lines):
        match = FRAME.search(line)
        if match:
            # (a report's first call is where it crashed; the rest are return
            # addresses, one past their calls)
            wanted[index] = int(match.group(1), 16) - (0 if first_frame else 1)
            first_frame = False
            continue
        # (another module's call continues the report's calls)
        first_frame = not ANY_FRAME.search(line)
        match = BASE.search(line)
        if match:
            base = int(match.group(1), 16)
            continue
        match = EXCEPTION.search(line)
        if match:
            wanted[index] = int(match.group(1), 16) - base
            continue
        match = CALLER.search(line)
        if match:
            wanted[index] = int(match.group(1), 16) - base - 1
    if not wanted:
        print(f"{args.log} has no crash lines")
        return 1
    answers = symbolize(symbolizer, args.executable, sorted({offset for offset in wanted.values() if 0 <= offset < size}))

    for index, line in enumerate(lines):
        print(line)
        if index not in wanted:
            continue
        answer = answers.get(wanted[index])
        if not answer or answer[0] == "??":
            print(f"        (not in {args.executable.name})")
            continue
        for function, location in zip(answer[0::2], answer[1::2]):
            print(f"        {function}  {location}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
