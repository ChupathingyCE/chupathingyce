"""A Windows crash report of a call through a null function pointer (#1161):
the report's walk goes on from the caller's return address on top of the
stack, so the report has the caller's frame after ?+0x0, and
tools/symbolize_crash.py reads that caller's frame as a return address (one
past its call), as it reads every frame after the first."""
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "windows" / "src" / "win32_crash.c"


def symbolize():
    spec = importlib.util.spec_from_file_location("symbolize_crash", ROOT / "tools" / "symbolize_crash.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_report_walks_on_from_the_caller():
    text = SOURCE.read_text()
    walk = text[text.index("static void crash_write_report("):text.index("if (symbols)\n\t\tSymCleanup(process);")]
    # (the null frame first, then the walk from the return address the call pushed)
    null_frame = walk.index("if (!frame.AddrPC.Offset)")
    assert null_frame < walk.index("for (depth = 0; depth < CRASH_REPORT_FRAMES; depth++)")
    seeded = walk[null_frame:]
    assert "ReadProcessMemory(process, (void *)(ULONG_PTR)frame.AddrStack.Offset, &slot, sizeof(slot), NULL)" in seeded
    assert "crash_frame(process, 0, line, sizeof(line));" in seeded
    for register in ("context.Rip = return_address;", "context.Rsp += sizeof(slot);",
                     "context.Eip = (DWORD)return_address;", "context.Esp += sizeof(slot);",
                     "frame.AddrPC.Offset = return_address;", "frame.AddrStack.Offset += sizeof(slot);"):
        assert register in seeded, register


def test_symbolize_reads_the_caller_as_a_return_address():
    module = symbolize()
    # (the null frame is a frame, so the caller's after it is a return address)
    assert module.ANY_FRAME.search("frame ?+0x0")
    assert not module.FRAME.search("frame ?+0x0")
    assert module.FRAME.search("frame chupathingy.exe+0x1a2b3c")


@pytest.mark.parametrize("arch,defines", [("x86_64", ["-DHALO_64BIT"]), ("i686", [])])
def test_compiles_for_windows(arch, defines):
    cc = shutil.which(f"{arch}-w64-mingw32-gcc") or shutil.which(f"{arch}-w64-mingw32-gcc",
                                                                   path="/opt/homebrew/opt/mingw-w64/bin")
    if not cc:
        pytest.skip("no mingw-w64 compiler")
    generated = ROOT / "build" / "macos" / "generated"
    if not generated.is_dir():
        generated = next((d for d in (ROOT / "build").glob("*/generated") if d.is_dir()), None) if (ROOT / "build").is_dir() else None
    if not generated:
        pytest.skip("no configured build (build/<target>/generated)")
    result = subprocess.run([cc, "-fsyntax-only", "-Wall", "-Wextra", "-Werror=implicit-function-declaration",
                             *defines, "-I", str(ROOT / "port" / "windows" / "src"), "-I", str(ROOT / "port" / "linux" / "src"),
                             "-I", str(generated), str(SOURCE)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
