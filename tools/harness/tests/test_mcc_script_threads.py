"""Real MCC thread storage, frame validation and old checkpoint migration."""
import re
import subprocess
import sys
from pathlib import Path
import pytest
from tools.harness import build, read, run, structure, enum_with, function


@pytest.fixture(scope="module")
def thread_tool(tmp_path_factory):
    runtime = read("source/hs/hs_runtime.c")
    types = "\n".join(structure(runtime, name) for name in ("hs_stack_frame", "hs_thread_datum"))
    types += "\n" + structure(read("source/memory/data.h"), "data_array")
    types += "\n" + enum_with(runtime, "_hs_thread_type_script =")
    types += "\n" + enum_with(runtime, "HS_THREAD_STACK_SIZE =")
    types += "\n" + enum_with(runtime, "_hs_thread_in_function_call_bit =")
    checkpoint = read("port/linux/game/mcc_checkpoint.c")
    definitions = "\n".join(line for line in checkpoint.splitlines() if line.startswith("#define MCC_CHECKPOINT_"))
    definitions += "\n" + read("port/linux/game/mcc_campaign.h")
    definitions += "\n" + read("port/linux/game/mcc_object_names.h")
    # Headers only contribute the capacity constants; no private implementation is copied.
    definitions = "\n".join(line for line in definitions.splitlines() if line.startswith("#define MCC_"))
    implementation = re.sub(r'^#include .*$', '', read("port/linux/game/mcc_script_threads.inl"), flags=re.M)
    code = definitions + "\n" + function(checkpoint, "mcc_checkpoint_script_storage") + "\n" + implementation
    code += "\n" + function(runtime, "hs_stack_push") + "\n" + function(runtime, "hs_stack_allocate")
    generated = (("types.inc", types), ("under_test.inc", code))
    if sys.platform == "win32":
        work = tmp_path_factory.mktemp("mcc-threads")
        for name, text in generated:
            (work / name).write_text(text)
        binary = work / "threads.exe"
        result = subprocess.run(["clang", "-m32", "-std=gnu89", "-O2", "-Wall", "-Werror",
            "-Wno-unused-function", "-I" + str(work), str(Path(__file__).with_name("mcc_script_threads.c")),
            "-o", str(binary)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        return binary
    return build("mcc_script_threads", generated)


@pytest.mark.parametrize("case", ["lifecycle", "capacity", "roundtrip", "migration", "checksum",
    "stride", "count", "pointer", "stack", "cycle", "overlap", "result", "size", "root", "other_thread", "bounds",
    "unactivated", "missing", "flags", "type", "sleep", "unused_slot"])
def test_thread_storage(thread_tool, case):
    status, output = run(thread_tool, case)
    assert status == 0, output
