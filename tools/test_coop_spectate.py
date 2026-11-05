"""Network co-op spectating (port/linux/game/coop_spectate.c): a dead player
sees their own body for the first 3 seconds of each death, as the retail
dead camera shows it, before watching a teammate. The timing helper is
compiled from the source with the build machine's C compiler."""
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SPECTATE_C = ROOT / "port/linux/game/coop_spectate.c"


def _block(text, start_marker):
    start = text.index(start_marker)
    end = text.index("\n}", start) + 3
    return text[start:end]


PRELUDE = """
#include <assert.h>
#include <stdio.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define TICKS_PER_SECOND 30
"""

MAIN = r"""
int main(void)
{
	struct coop_spectate_death death = { NONE, NONE };
	long tick;

	/* no body yet (still joining): spectate at once */
	assert(!showing_own_body(&death, NONE, 0));
	/* a death at tick 100: the body for 90 ticks, then a teammate */
	for (tick = 100; tick < 190; tick++)
		assert(showing_own_body(&death, 5, tick));
	assert(!showing_own_body(&death, 5, 190));
	assert(!showing_own_body(&death, 5, 5000));
	/* the next death has a new body: the body again */
	assert(showing_own_body(&death, 9, 6000));
	assert(showing_own_body(&death, 9, 6089));
	assert(!showing_own_body(&death, 9, 6090));
	/* a new game's clock (behind the start) is a new death */
	assert(showing_own_body(&death, 9, 10));
	assert(!showing_own_body(&death, 9, 100));
	puts("PASS");
	return 0;
}
"""


def _unit():
    text = SPECTATE_C.read_text()
    ticks = re.search(r"#define SPECTATE_DEATH_TICKS .*", text).group(0)
    return "\n".join([
        PRELUDE,
        ticks,
        _block(text, "struct coop_spectate_death\n{"),
        _block(text, "static boolean showing_own_body("),
        MAIN,
    ])


def test_own_body_first():
    cc = shutil.which(os.environ.get("CC", "cc")) or shutil.which("clang") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    with tempfile.TemporaryDirectory(prefix="halo-coop-spectate-") as temp:
        source = Path(temp) / "spectate.c"
        exe = Path(temp) / "spectate"
        source.write_text(_unit())
        subprocess.run([cc, "-std=c11", "-Wall", "-Werror", str(source), "-o", str(exe)], check=True)
        result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
        assert "PASS" in result.stdout


if __name__ == "__main__":
    test_own_body_first()
    print("PASS")
