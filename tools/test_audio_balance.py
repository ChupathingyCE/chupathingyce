#!/usr/bin/env python3
"""Headphone 3D (audio.hrtf) keeps the sounds close to the head in the middle.

The desktop mixer (port/linux/src/dsound_sdl.c) pans a 3D voice inside its
minimum distance towards the middle (spatialize), so the player's own
weapon, a little right of the eye, and footsteps sound centered. With
audio.hrtf on, hrtf_target places the voice through the Xbox's HRTF
(dsound_xbox_hrtf.inl, hrtf_place) and has to center those voices the same
way. 0.9.1 centered them only within HRTF_NEAR (0.25 world units), whatever
the voice's minimum distance: a weapon 0.15 units right and 0.3 ahead of the
eye played 4.5 dB louder on the right (chupathingyce 0.9.1, Headphone 3D on).

This builds dsound_sdl.c's hrtf_target with the HRTF model, and checks:
    - a voice straight ahead: both ears alike;
    - voices close to the head on the right, inside their minimum distance:
      the ears no more than 1 dB further apart than the pan has them there
      (in the middle, about as with Headphone 3D off);
    - a voice 3 units to the right, outside its minimum distance: the right
      ear well up (the HRTF still works);
    - left and right mirror each other.
Needs clang (or cc) and Python 3 alone.

    python3 -m pytest tools/test_audio_balance.py
    python3 tools/test_audio_balance.py          (prints the figures)
"""
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port/linux/src"

PRELUDE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
typedef long LONG;
#define OUTPUT_RATE 48000
#define MIX_CHUNK_FRAMES 256
static unsigned long output_channels = 2;
static float flush_denormal(float value) { return fabsf(value) < 1.0e-20f ? 0.0f : value; }
static float clamp_real(float value, float minimum, float maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}
#include "dsound_xbox_types.h"
struct sdl_stream
{
	float position[3];
	float minimum_distance;
	LONG direct;
	float volume;
	struct hrtf_parameters hrtf_current;
	float hrtf_frames[2][HRTF_FRAMES];
	float hrtf_bands[HRTF_BANDS - 1][2];
};
static struct { float rolloff_factor; } listener = { 1.0f };
static float master_volume = 1.0f;
/* the listener at the origin, facing +z, +x to its right */
static void voice_direction(const struct sdl_stream *stream, float *side, float *up, float *ahead)
{
	*side = stream->position[0];
	*up = stream->position[1];
	*ahead = stream->position[2];
}
static float distance_attenuation(const struct sdl_stream *stream, float distance, float rolloff_factor)
{
	(void)stream; (void)distance; (void)rolloff_factor;
	return 1.0f;
}
static float gain_from_millibels(LONG millibels) { (void)millibels; return 1.0f; }
static float voice_cone_gain(const struct sdl_stream *stream) { (void)stream; return 1.0f; }
#include "dsound_xbox_hrtf.inl"
'''

MAIN = r'''
/* the ears' difference (left less right, dB), each ear's level the mean of
its bands' (power) */
int main(int argc, char **argv)
{
	struct sdl_stream stream;
	struct hrtf_parameters target;
	double power[2] = { 0, 0 };
	int ear, band;

	memset(&stream, 0, sizeof(stream));
	stream.position[0] = (float)atof(argv[1]);
	stream.position[1] = (float)atof(argv[2]);
	stream.position[2] = (float)atof(argv[3]);
	stream.minimum_distance = (float)atof(argv[4]);
	stream.volume = 1.0f;
	(void)argc;
	hrtf_target(&stream, 1.0f, &target);
	for (ear = 0; ear < 2; ear++)
		for (band = 0; band < HRTF_BANDS; band++)
			power[ear] += pow(10.0, target.decibels[ear][band] / 10.0) / HRTF_BANDS;
	printf("%.4f\n", 10.0 * log10(power[0] / power[1]));
	return 0;
}
'''


def function(source, name):
    match = re.search(r"^static [\w *]+\b" + name + r"\(", source, re.M)
    if not match:
        raise ValueError(f"Function not found: {name}")
    start = source.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def compiler():
    for name in (os.environ.get("CC"), "clang", "cc", "gcc"):
        if name and shutil.which(name):
            return name
    return None


def build(work):
    cc = compiler()
    if cc is None:
        return None
    mixer = (SRC / "dsound_sdl.c").read_text()
    program = PRELUDE + function(mixer, "hrtf_target") + MAIN
    source = work / "balance.c"
    source.write_text(program)
    exe = work / "balance"
    subprocess.run([cc, "-O1", "-std=gnu11", "-I", str(SRC), str(source), "-o", str(exe), "-lm"], check=True)
    return exe


def difference(exe, x, y, z, minimum):
    out = subprocess.run([str(exe), str(x), str(y), str(z), str(minimum)], check=True, capture_output=True, text=True)
    return float(out.stdout)


def pan_difference(x, y, z, minimum):
    """spatialize's equal power pan (dsound_sdl.c), left less right in dB"""
    distance = math.sqrt(x * x + y * y + z * z)
    horizontal = math.sqrt(x * x + z * z)
    pan = x / horizontal if horizontal > 1e-4 else 0.0
    if distance < minimum:
        pan *= distance / minimum
    angle = (pan * 0.75 + 1.0) * 0.25 * math.pi
    return 20.0 * math.log10(math.cos(angle) / math.sin(angle))


# close to the head on the right: (right, up, ahead, minimum distance), world
# units; a weapon in the right hand is about 0.1 to 0.2 right of the eye
NEAR = [
    (0.05, -0.05, 0.2, 1.0),
    (0.1, -0.05, 0.2, 1.0),
    (0.15, -0.1, 0.3, 1.0),
    (0.15, -0.1, 0.3, 3.0),
    (0.2, 0.0, 0.0, 1.0),
    (0.0, -0.4, 0.1, 1.0),
]


def check(exe):
    ahead = difference(exe, 0.0, 0.0, 3.0, 1.0)
    assert abs(ahead) < 0.01, f"straight ahead: the ears {ahead:+.2f} dB apart"
    for x, y, z, minimum in NEAR:
        hrtf = difference(exe, x, y, z, minimum)
        pan = pan_difference(x, y, z, minimum)
        assert abs(hrtf) <= abs(pan) + 1.0, (
            f"({x}, {y}, {z}) inside a minimum distance of {minimum}: Headphone 3D {hrtf:+.2f} dB, "
            f"the pan {pan:+.2f} dB; the sound is not kept in the middle")
        mirrored = difference(exe, -x, y, z, minimum)
        assert abs(hrtf + mirrored) < 0.01, f"({x}, {y}, {z}): right {hrtf:+.2f} dB, left {mirrored:+.2f} dB"
    side = difference(exe, 3.0, 0.0, 0.0, 1.0)
    assert side < -5.0, f"3 units to the right: the ears only {side:+.2f} dB apart"
    assert abs(side + difference(exe, -3.0, 0.0, 0.0, 1.0)) < 0.01


def test_headphone_3d_keeps_close_sounds_centered():
    with tempfile.TemporaryDirectory() as work:
        exe = build(Path(work))
        if exe is None:
            import pytest
            pytest.skip("no C compiler")
        check(exe)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as work:
        exe = build(Path(work))
        if exe is None:
            sys.exit("no C compiler")
        for x, y, z, minimum in NEAR + [(0.0, 0.0, 3.0, 1.0), (3.0, 0.0, 0.0, 1.0)]:
            print(f"({x:5.2f}, {y:5.2f}, {z:5.2f}) minimum {minimum}: Headphone 3D L-R "
                  f"{difference(exe, x, y, z, minimum):+6.2f} dB, pan {pan_difference(x, y, z, minimum):+6.2f} dB")
        check(exe)
        print("ok")
