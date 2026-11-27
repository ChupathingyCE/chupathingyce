"""tools/symbolize_crash.py's --symbols mode: a crash report's calls named
from a build's kept symbols (a linker's map here, which needs no LLVM), and
the build IDs it checks a report's build_id (crash_report.h) against."""
import importlib.util
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools" / "symbolize_crash.py"

MAP = """ chupathingy

 Timestamp is 00000000 (Thu Jan  1 00:00:00 1970)

 Preferred load address is 0000000140000000

 Start         Length     Name                   Class
 0001:00000000 00256d01H .text                   CODE

  Address         Publics by Value              Rva+Base               Lib:Object

 0000:00000000       __except_list              0000000000000000     <absolute>
 0001:00000000       render_window              0000000140001000 f   chupathingy.exe.lto.obj
 0001:00000400       render_model               0000000140001400 f   chupathingy.exe.lto.obj
 0001:00000900       rasterizer_model_draw      0000000140001900 f   chupathingy.exe.lto.obj
"""


def module():
    spec = importlib.util.spec_from_file_location("symbolize_crash", TOOL)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def symbolize(tmp_path, *arguments):
    (tmp_path / "chupathingy.map").write_text(MAP)
    return subprocess.run([sys.executable, str(TOOL), "--symbols", str(tmp_path / "chupathingy.map"), *arguments],
                          capture_output=True, text=True, check=True)


def test_calls_named_from_a_map(tmp_path):
    output = symbolize(tmp_path, "chupathingy.exe+0x1910", "+0x1405", "+0x1001").stdout
    # (the first call where it crashed; the others return addresses, one past their calls)
    assert "rasterizer_model_draw+0x10" in output
    assert "render_model+0x4" in output
    assert "render_window+0x0" in output


def test_a_report_file(tmp_path):
    report = tmp_path / "report.json"
    report.write_text(json.dumps({"format": 1, "build_id": "ABC1", "frames": ["chupathingy.exe+0x1910", "?+0x10"]}))
    result = symbolize(tmp_path, str(report))
    assert "rasterizer_model_draw+0x10" in result.stdout
    # (a map has no ID of its own to check the report's against)
    assert "ABC1" in result.stderr
    lines = tmp_path / "report.txt"
    lines.write_text("version 0.9.2\nbuild_id ABC1\nframe chupathingy.exe+0x1401\n")
    assert "render_model+0x1" in symbolize(tmp_path, str(lines)).stdout


def test_absolute_addresses(tmp_path):
    # (the original Xbox's crash lines: addresses, not offsets)
    output = symbolize(tmp_path, "--absolute", "--exact", "0x140001905").stdout
    assert "rasterizer_model_draw+0x5" in output


def test_codeview_id():
    # (a PDB's GUID and age, as a symbol server names them and win32_crash.c sends them)
    record = struct.pack("<IHH", 0xDB9B4BE7, 0x2A08, 0x19C4) + bytes.fromhex("4C4C44205044422E") + struct.pack("<I", 1)
    assert module().codeview_id(record) == "DB9B4BE72A0819C44C4C44205044422E1"


def test_elf_build_id():
    # a 64-bit ELF of one section header, a note: NT_GNU_BUILD_ID
    descriptor = bytes.fromhex("4bd9cef9069f96b8741b174e15b9f559eaa70159")
    note = struct.pack("<III", 4, len(descriptor), 3) + b"GNU\0" + descriptor
    header = bytearray(64)
    header[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<Q", header, 0x28, 64 + len(note))
    struct.pack_into("<HH", header, 0x3A, 64, 1)
    section = bytearray(64)
    struct.pack_into("<I", section, 4, 7)
    struct.pack_into("<QQ", section, 24, 64, len(note))
    assert module().elf_build_id(bytes(header) + note + bytes(section)) == descriptor.hex()
    assert module().elf_build_id(b"MZ") is None


def test_other_modules_left_unnamed(tmp_path):
    # (a system library's call, or "?" for none, is not named from the game's symbols)
    output = symbolize(tmp_path, "chupathingy.exe+0x1910", "ntdll.dll+0x1405", "?+0x1001").stdout
    assert "rasterizer_model_draw+0x10" in output
    assert "render_model" not in output and "render_window" not in output
    assert "ntdll.dll+0x1405" in output and "?+0x1001" in output
