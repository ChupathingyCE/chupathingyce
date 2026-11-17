"""The campaign snapshot's place in an MCC checkpoint's fixed slot
(port/linux/game/mcc_checkpoint_footer.inl, mcc_checkpoint.c): the payload
keeps the snapshot's version-5 bytes and the footer the words it grew by,
so the whole current snapshot (version 7, with the skulls) is saved and
read back, and older footers still read. Runs the C test with the build
machine's compiler; nothing of the engine is needed."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def test_mcc_checkpoint_footer(tmp_path):
    cc = next((path for path in map(shutil.which, ("clang", "cc", "gcc")) if path), None)
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "mcc_checkpoint_footer_test"
    # (mcc_campaign.h, for the snapshot sizes, includes the engine's cseries.h:
    # its two type names here, nothing else of it is used)
    (tmp_path / "cseries.h").write_text("typedef unsigned char boolean;\ntypedef float real;\n")
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-I", str(tmp_path),
         str(ROOT / "port" / "linux" / "tests" / "mcc_checkpoint_footer_test.c"),
         "-o", str(binary)],
        capture_output=True, text=True,
    )
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_capture_packs_the_whole_snapshot():
    """mcc_checkpoint.c saves through the pack and names the footer version
    it writes, instead of a size check that a grown snapshot fails."""
    checkpoint = (ROOT / "port" / "linux" / "game" / "mcc_checkpoint.c").read_text()
    assert '#include "mcc_checkpoint_footer.inl"' in checkpoint
    assert "mcc_checkpoint_campaign_pack(campaign,sizeof(campaign)" in checkpoint
    assert "mcc_checkpoint_campaign_unpack(" in checkpoint
    assert "MCC_CHECKPOINT_FOOTER_VERSION" in checkpoint
    assert "sizeof(campaign)!=MCC_CHECKPOINT_CAMPAIGN_BYTES+4" not in checkpoint
    # (the slot's size is fixed: the saved thread pointers address it)
    assert "#define MCC_CHECKPOINT_CAMPAIGN_BYTES MCC_CAMPAIGN_SNAPSHOT_V5_BYTES" in checkpoint
    assert "#define MCC_CHECKPOINT_FOOTER_BYTES 96u" in checkpoint
