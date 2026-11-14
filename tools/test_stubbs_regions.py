"""Compile Stubbs mode's region rows (port/linux/game/stubbs_regions.c) and check them.

An object's sixteen region permutations live beside the datum in Stubbs mode,
and the datum's eight are a copy of the first eight. A checkpoint revert or a
saved game load puts the datum back under the same datum index, so the row
must start again from the restored eight:

- save: a cop with permutations 1..8 in its datum is saved (the datum copied);
- change: after the checkpoint a region of the first eight and one past them
  change through the row (a limb lost, a torso swapped);
- revert: the saved datum comes back and the game state's after-load procs
  run; the row must equal the saved eight with regions 8-15 at 0, and the
  next change must not write the post-checkpoint state back into the datum.

The negative control skips stubbs_regions_forget and must see the stale row.
Also checked: game_state.c's after-load procs call stubbs_regions_forget, and
Halo mode never hands out a row.

No game assets are needed. Run with Python and a C compiler (cc) on PATH.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FAKES = {
    "cseries.h": r'''
#ifndef FAKE_CSERIES_H
#define FAKE_CSERIES_H
typedef unsigned char byte;
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define DATUM_INDEX_TO_ABSOLUTE_INDEX(i) ((unsigned long)(i) & 0xFFFF)
#endif
''',
    "halo_console_features.h": "",
    "halo_port_capacity.h": "#define HALO_PORT_MAXIMUM_OBJECTS_PER_MAP 64\n",
    "stubbs_mode.h": "boolean stubbs_mode_active(void);\n",
    "objects/objects.h": r'''
#define MAXIMUM_REGIONS_PER_OBJECT 8
struct object_datum { struct { byte region_permutations[MAXIMUM_REGIONS_PER_OBJECT]; } object; };
struct object_datum *object_try_and_get(long object_index);
''',
}

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include "cseries.h"
#include "objects/objects.h"
#include "stubbs_regions.h"

static boolean stubbs_on = TRUE;
static struct object_datum cop;
static const long cop_index = 0x12340005;

boolean stubbs_mode_active(void) { return stubbs_on; }
struct object_datum *object_try_and_get(long object_index)
{
	return object_index == cop_index ? &cop : NULL;
}

static int failures = 0;
static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	failures += !ok;
}

int main(int argc, char **argv)
{
	boolean forget = argc < 2 || strcmp(argv[1], "no-forget");
	struct object_datum saved;
	byte *row;
	byte expected[STUBBS_MAXIMUM_REGIONS_PER_OBJECT] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	int i;

	for (i = 0; i < 8; i++)
		cop.object.region_permutations[i] = (byte)(i + 1);
	stubbs_regions_object_new(cop_index);
	row = stubbs_regions_permutations(cop_index);
	check(row && !memcmp(row, expected, sizeof(expected)), "a new row starts from the datum's eight");

	/* save */
	saved = cop;
	/* change: a limb lost (region 3) and the torso (region 11) swapped */
	row[3] = 0;
	row[11] = 2;
	stubbs_regions_changed(cop_index);
	check(cop.object.region_permutations[3] == 0, "a change reaches the datum's eight");

	/* revert: the datum comes back, then the after-load procs */
	cop = saved;
	if (forget)
		stubbs_regions_forget();
	row = stubbs_regions_permutations(cop_index);
	check(row && !memcmp(row, expected, sizeof(expected)), "after a revert the row equals the saved state");
	stubbs_regions_changed(cop_index);
	check(!memcmp(cop.object.region_permutations, saved.object.region_permutations, 8),
		"the next change keeps the restored datum");

	stubbs_on = FALSE;
	check(stubbs_regions_permutations(cop_index) == NULL, "Halo mode hands out no row");
	return failures ? 1 : 0;
}
'''


def main():
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not cc:
        print("no C compiler")
        return 1
    game_state = (ROOT / "source/saved games/game_state.c").read_text(encoding="latin-1")
    procs = game_state[game_state.index("after_load_procs[] ="):]
    procs = procs[:procs.index("};")]
    ok = "stubbs_regions_forget" in procs
    print(("ok  " if ok else "FAIL") + " game_state.c's after-load procs forget the region rows")
    failures = 0 if ok else 1
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        for name, text in FAKES.items():
            (work / name).parent.mkdir(parents=True, exist_ok=True)
            (work / name).write_text(text)
        (work / "driver.c").write_text(DRIVER)
        # (only this header: port/linux/include has its own stdio.h and more)
        shutil.copy(ROOT / "port/linux/include/stubbs_regions.h", work / "stubbs_regions.h")
        exe = work / "regions"
        subprocess.run(
            [cc, "-std=gnu99", "-w", "-I", str(work),
             str(work / "driver.c"), str(ROOT / "port/linux/game/stubbs_regions.c"), "-o", str(exe)],
            check=True)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        print(run.stdout, end="")
        failures += run.returncode != 0
        control = subprocess.run([str(exe), "no-forget"], capture_output=True, text=True)
        stale = control.returncode != 0
        print(("ok  " if stale else "FAIL") + " negative control: without the after-load proc the row is stale")
        failures += not stale
    print("passed" if not failures else f"{failures} failed")
    return 1 if failures else 0


def test_stubbs_regions_revert():
    assert main() == 0


if __name__ == "__main__":
    sys.exit(main())
