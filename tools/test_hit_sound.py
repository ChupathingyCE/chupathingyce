"""The hit tone where a map has no ting (port/linux/game/hit_sound.c, #2038).

- An Ogg Vorbis file is decoded only as far as the tone's first seconds
  (hit_sound_ogg_mono over ce_vorbis_edges), so a long file, or the start of
  one, gives the tone: built here with content_vorbis.c and run on the test
  streams in tools/tests/vorbis.
- The tone is read as the map loads and after a settings change, never at a
  hit, and goes with its map.
- The ting is the resource sound\\sfx\\impulse\\ting\\ting (the path itself:
  port/linux/tests/hit_sound_test.c, in tools/test_c_units.py).
"""

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "port" / "linux" / "game"
VORBIS = ROOT / "tools" / "tests" / "vorbis"

HARNESS = r"""
#define HIT_SOUND_VORBIS
#include "hit_sound_decode.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    static unsigned char data[1 << 20];
    static short out[HIT_SOUND_MAXIMUM_SOURCE_FRAMES];
    FILE *file = fopen(argv[1], "rb");
    long size = file ? (long)fread(data, 1, sizeof(data), file) : 0;
    long keep = argc > 2 ? atol(argv[2]) : size;
    int rate = 0;
    long frames;

    if (file)
        fclose(file);
    if (keep > size)
        keep = size;
    frames = hit_sound_ogg_mono(data, (unsigned long)keep, out, HIT_SOUND_MAXIMUM_SOURCE_FRAMES, &rate);
    printf("%ld %d\n", frames, rate);
    return 0;
}
"""


def compiler():
    for name in ("clang", "cc", "gcc"):
        if shutil.which(name):
            return shutil.which(name)
    return None


@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    folder = tmp_path_factory.mktemp("hit_sound")
    (folder / "harness.c").write_text(HARNESS)
    binary = folder / "harness"
    build = subprocess.run([cc, "-O1", "-DHALO_CUSTOM_EDITION", "-DHALO_MCC=0", "-I", str(GAME),
                            "-I", str(ROOT / "port" / "third_party" / "stb"), str(GAME / "content_vorbis.c"),
                            str(GAME / "content_audio.c"), str(folder / "harness.c"), "-o", str(binary), "-lm"],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    return binary


def decode(harness, name, keep=None):
    arguments = [str(harness), str(VORBIS / name)] + ([str(keep)] if keep is not None else [])
    frames, rate = subprocess.run(arguments, capture_output=True, text=True, check=True).stdout.split()
    return int(frames), int(rate)


@pytest.mark.parametrize("name", ["noise-22k.ogg", "tone-noise-44k.ogg"])
def test_ogg_tone_decoded(harness, name):
    frames, rate = decode(harness, name)
    assert rate in (22050, 44100)
    assert 0 < frames <= rate * 2


def test_ogg_start_of_a_long_file(harness):
    # (what was read of a long file: its start, cut anywhere past its headers)
    whole, rate = decode(harness, "tone-noise-44k.ogg")
    size = (VORBIS / "tone-noise-44k.ogg").stat().st_size
    part, part_rate = decode(harness, "tone-noise-44k.ogg", size * 3 // 4)
    assert part_rate == rate and 0 < part <= whole


def test_not_ogg_is_refused(harness, tmp_path):
    (tmp_path / "x.ogg").write_bytes(b"OggS" + b"\0" * 200)
    frames, rate = subprocess.run([str(harness), str(tmp_path / "x.ogg")], capture_output=True, text=True,
                                  check=True).stdout.split()
    assert (int(frames), int(rate)) == (0, 0)


def body(text, start):
    """a function's body, from its definition (a declaration ends with ";")"""
    at = next(index for index in range(len(text)) if text.startswith(start, index)
              and text[index:text.index("\n", index)].rstrip()[-1:] != ";")
    return text[at:text.index("\n}\n", at)]


def test_ogg_decodes_only_the_start():
    source = (GAME / "hit_sound.c").read_text()
    assert "hit_sound_ogg_mono(" in body(source, "static boolean tone_make(")
    assert "ce_vorbis_decode" not in source
    # (no more of the player's file read than the tone needs; no size refused)
    loader = body(source, "static boolean file_load(")
    assert "size = HIT_SOUND_FILE_READ_BYTES;" in loader
    assert "HIT_SOUND_MAXIMUM_BYTES" not in loader


def test_read_at_map_load_never_at_a_hit():
    source = (GAME / "hit_sound.c").read_text()
    play = body(source, "void hit_sound_play(void)")
    for reader in ("tone_read(", "ting_load(", "file_load(", "config_changes("):
        assert reader not in play, reader
    assert "tone_read();" in body(source, "void hit_sound_map_loaded(")
    assert "tone_forget();" in body(source, "void hit_sound_map_unloaded(")
    sounds = (ROOT / "source" / "game" / "game_engine_multiplayer_sounds.c").read_text()
    assert ("hit_sound_map_loaded(game_engine_multiplayer_sound_exists(_multiplayer_sound_ting));"
            in body(sounds, "void game_engine_intialize_queued_sounds("))
    assert "hit_sound_update();" in body(sounds, "void game_engine_update_multiplayer_sound(")
    engine = (ROOT / "source" / "game" / "game_engine.c").read_text()
    assert "hit_sound_map_unloaded();" in body(engine, "void game_engine_dispose_from_old_map(")
