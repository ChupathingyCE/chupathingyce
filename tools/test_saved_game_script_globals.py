"""Compile the saved game's script globals check (game_state.c,
game_state_image_hs_globals_valid) and check its count math: a saved game is
taken only when its "hs globals" array is laid out as this build lays it out
for the map (the game's own globals, then the map's). A saved game of a build
with another number of the game's own globals (the Xbox's 443, the desktop's
443 + 14, or a build that adds more) is refused, whichever way they differ."""
import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from test_death_timing import function

ROOT = Path(__file__).resolve().parent.parent

PRELUDE = r'''
#include <assert.h>
#include <stdio.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
'''

TESTS = r'''
/* the array's count after hs_runtime_initialize and
hs_runtime_initialize_for_new_map: one past the last of the map's globals */
static long laid_out(long external_count, long scenario_count) {
    return external_count + scenario_count;
}

int main(void) {
    long const desktop = 443 + 14, xbox = 443;
    /* a10, a30, a50 and a map whose scripts are off (no globals) */
    long const maps[] = {84, 108, 128, 0};
    int i;
    for (i = 0; i < 4; i++) {
        long map = maps[i];
        /* this build's own saved game is taken */
        assert(game_state_image_hs_globals_valid(laid_out(desktop, map), desktop, map));
        assert(game_state_image_hs_globals_valid(laid_out(xbox, map), xbox, map));
        /* another build's is not, more or fewer of the game's own globals */
        assert(!game_state_image_hs_globals_valid(laid_out(xbox, map), desktop, map));
        assert(!game_state_image_hs_globals_valid(laid_out(desktop, map), xbox, map));
        assert(!game_state_image_hs_globals_valid(laid_out(desktop + 1, map), desktop, map));
        /* nor one of another map's globals */
        if (map)
            assert(!game_state_image_hs_globals_valid(laid_out(desktop, map - 1), desktop, map));
    }
    /* the crash reports: a10's global 80 read at 457 + 80 = 537 in a saved
    game laid out for 443 (its last at 443 + 83 = 526), refused now */
    assert(laid_out(desktop, 80) == 537 && laid_out(xbox, 84) - 1 == 526);
    assert(!game_state_image_hs_globals_valid(laid_out(xbox, 84), desktop, 84));
    puts("PASS: a saved game's script globals must be laid out as this build lays them out");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    source = (ROOT / 'source/saved games/game_state.c').read_text()
    unit = PRELUDE + function(source, 'game_state_image_hs_globals_valid') + TESTS
    cc = shutil.which(args.cc) or args.cc
    with tempfile.TemporaryDirectory(prefix='halo-saved-game-globals-') as temp:
        c = Path(temp) / 'globals.c'
        exe = Path(temp) / ('globals.exe' if os.name == 'nt' else 'globals')
        c.write_text(unit)
        flags = ['--target=i686-pc-windows-msvc', '-fuse-ld=lld'] if os.name == 'nt' else []
        subprocess.run([cc, *flags, '-std=c11', '-O2', str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
