#!/usr/bin/env python3
"""The desktop mixer's levels and routing (port/linux/src/dsound_sdl.c)
against what goatbutt and the OpenH1 team measured on an Xbox
(docs/reading/xbox_audio.md, "Levels and routing"):

    - the reverb send of a 3D voice is 6.0 dB over its room level and its
      direct path's distance rolloff, from half a unit to 20 units, and the
      room rolloff factor changes nothing (voice_gains);
    - obstruction reaches the send: a voice obstructed at -1200 (LF ratio 0)
      sends as one occluded at -1200 does (i3dl2_source_levels);
    - the output is half of the sum (-6.02 dB), and the limiter acts only on
      a sum over twice full scale; audio.loudness = "louder" keeps the whole
      sum into the -1 dBFS limiter (output_scale, limit).

It builds those functions of dsound_sdl.c alone. Needs clang (or cc) and
Python 3 alone.

    python3 -m pytest tools/test_audio_levels.py
    python3 tools/test_audio_levels.py          (prints the figures)
"""
import cmath
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
#define MAXIMUM_OUTPUT_CHANNELS 8
#define DSBVOLUME_MIN (-10000)
#define DS3DMODE_NORMAL 0
#define DS3DMODE_HEADRELATIVE 1
#define DS3DMODE_DISABLE 2
typedef struct
{
	LONG lRoom, lRoomHF;
	float flRoomRolloffFactor, flDecayTime, flDecayHFRatio;
	LONG lReflections;
	float flReflectionsDelay;
	LONG lReverb;
	float flReverbDelay, flDiffusion, flDensity, flHFReference;
} DSI3DL2LISTENER;
struct i3dl2_part { LONG lHFLevel; float flLFRatio; };
typedef struct
{
	LONG lDirect, lDirectHF, lRoom, lRoomHF;
	float flRoomRolloffFactor;
	struct i3dl2_part Obstruction, Occlusion;
} DSI3DL2BUFFER;
struct sdl_stream
{
	unsigned long channels;
	float volume, mix_left, mix_right;
	BOOL has_3d;
	unsigned long mode;
	float position[3];
	float minimum_distance, maximum_distance;
	LONG direct, direct_hf, room, room_hf;
	unsigned long cone_inside, cone_outside;
	float cone_orientation[3];
	LONG cone_outside_volume;
};
static unsigned long output_channels = 2;
static float master_volume = 1.0f;
static BOOL reverb_enabled = TRUE;
static DSI3DL2LISTENER environment = { 0, 0, 0.0f, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100.0f, 100.0f, 5000.0f };
/* the listener at the origin, facing +z, +y up */
static struct
{
	float position[3], front[3], top[3];
	float rolloff_factor;
} listener = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, 1.0f };
static const char *loudness_setting = "xbox";
static const char *config_string(const char *name) { (void)name; return loudness_setting; }
static float clamp_real(float value, float minimum, float maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}
'''

FUNCTIONS = ["gain_from_millibels", "frequency_cosine", "lowpass_coefficient", "dot3", "distance_attenuation",
             "voice_direction", "voice_cone_gain", "spatialize", "voice_gains", "i3dl2_source_levels",
             "output_loudness_read", "output_scale"]

MAIN = r'''
static void voice_3d(struct sdl_stream *stream, float distance, float minimum)
{
	memset(stream, 0, sizeof(*stream));
	stream->channels = 1;
	stream->volume = 1.0f;
	stream->has_3d = TRUE;
	stream->mode = DS3DMODE_NORMAL;
	stream->position[2] = distance;
	stream->minimum_distance = minimum;
	stream->maximum_distance = 1000.0f;
}

int main(int argc, char **argv)
{
	struct sdl_stream stream;
	float left, right, room, direct_lowpass, room_lowpass, rolloff, ignored;

	if (argc > 1 && !strcmp(argv[1], "send"))
	{
		/* send <distance> <minimum> <voice room mB> <environment room rolloff>:
		the send's gain and the direct rolloff */
		voice_3d(&stream, (float)atof(argv[2]), (float)atof(argv[3]));
		stream.room = atol(argv[4]);
		stream.room_hf = stream.room;
		environment.flRoomRolloffFactor = (float)atof(argv[5]);
		voice_gains(&stream, &left, &right, &room, &direct_lowpass, &room_lowpass);
		spatialize(&stream, &ignored, &ignored, &rolloff);
		printf("%.9g %.9g\n", room, rolloff);
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "i3dl2"))
	{
		/* i3dl2 <obstruction HF> <obstruction LF ratio> <occlusion HF>
		<occlusion LF ratio>: a voice at 1 unit, inside its minimum distance,
		the source's room at the game's -1204: the direct gain (left), the
		send's gain and the two low passes' coefficients */
		DSI3DL2BUFFER source;

		memset(&source, 0, sizeof(source));
		source.lRoom = -1204;
		source.lRoomHF = -1204;
		source.Obstruction.lHFLevel = atol(argv[2]);
		source.Obstruction.flLFRatio = (float)atof(argv[3]);
		source.Occlusion.lHFLevel = atol(argv[4]);
		source.Occlusion.flLFRatio = (float)atof(argv[5]);
		voice_3d(&stream, 1.0f, 2.0f);
		i3dl2_source_levels(&source, &stream.direct, &stream.direct_hf, &stream.room, &stream.room_hf);
		voice_gains(&stream, &left, &right, &room, &direct_lowpass, &room_lowpass);
		printf("%.9g %.9g %.9g %.9g\n", left, room, direct_lowpass, room_lowpass);
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "voice2d"))
	{
		/* voice2d: a 2D voice at full volume, its left gain at the output */
		float output[2], send[1] = { 0.0f };

		memset(&stream, 0, sizeof(stream));
		stream.channels = 1;
		stream.volume = 1.0f;
		stream.mix_left = stream.mix_right = 1.0f;
		loudness_setting = argv[2];
		output_loudness_read();
		voice_gains(&stream, &left, &right, &room, &direct_lowpass, &room_lowpass);
		output[0] = left;
		output[1] = right;
		output_scale(output, send, 1, output_level);
		printf("%.9g\n", output[0]);
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "limit"))
	{
		/* limit <loudness> <amplitude>: a 1 kHz tone of that amplitude (the
		sum, full scale 1) for a second through output_scale and the
		limiter: the last half second's peak */
		static float output[OUTPUT_RATE * 2], send[OUTPUT_RATE];
		float amplitude = (float)atof(argv[3]), peak = 0.0f;
		unsigned long frame;

		loudness_setting = argv[2];
		output_loudness_read();
		for (frame = 0; frame < OUTPUT_RATE; frame++)
			output[2 * frame] = output[2 * frame + 1] = amplitude * sinf(2.0f * 3.14159265f * 1000.0f * frame / OUTPUT_RATE);
		for (frame = 0; frame < OUTPUT_RATE; frame += 256)
		{
			output_scale(output + 2 * frame, send + frame, 256, output_level);
			limit(output + 2 * frame, 256);
		}
		for (frame = OUTPUT_RATE / 2; frame < OUTPUT_RATE; frame++)
			if (fabsf(output[2 * frame]) > peak)
				peak = fabsf(output[2 * frame]);
		printf("%.9g\n", peak);
		return 0;
	}
	return 1;
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


def defines(source, names):
    text = ""
    for name in names:
        match = re.search(r"^#define " + name + r"\b.*$", source, re.M)
        if not match:
            raise ValueError(f"Define not found: {name}")
        text += match.group(0) + "\n"
    return text


def limiter(source):
    """the limiter's state and limit, as dsound_sdl.c has them"""
    start = source.index("#define LIMITER_LOOKAHEAD")
    end = source.index("} limiter;", start) + len("} limiter;")
    return source[start:end] + "\n" + function(source, "limit")


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
    program = PRELUDE + defines(mixer, ["OUTPUT_LEVEL_XBOX", "LIMITER_CEILING_XBOX", "OUTPUT_LEVEL_LOUDER",
                                        "LIMITER_CEILING_LOUDER"])
    program += "static float output_level = OUTPUT_LEVEL_XBOX;\nstatic float limiter_ceiling = LIMITER_CEILING_XBOX;\n"
    program += "".join(function(mixer, name) for name in FUNCTIONS) + limiter(mixer) + MAIN
    source = work / "levels.c"
    source.write_text(program)
    exe = work / "levels"
    subprocess.run([cc, "-O1", "-std=gnu11", str(source), "-o", str(exe), "-lm"], check=True)
    return exe


def run(exe, *arguments):
    out = subprocess.run([str(exe)] + [str(a) for a in arguments], check=True, capture_output=True, text=True)
    return [float(value) for value in out.stdout.split()]


def db(gain):
    return 20.0 * math.log10(gain) if gain > 0 else -math.inf


# (world units; the voice's minimum distance 1)
SEND_DISTANCES = [0.5, 1.0, 2.0, 4.0, 10.0, 20.0]


def send_figures(exe):
    rows = []
    for distance in SEND_DISTANCES:
        room, rolloff = run(exe, "send", distance, 1.0, -1204, 0.0)
        over = db(room) - (-12.04 + db(rolloff))
        rows.append((distance, over, db(room)))
    return rows


def check_send(exe):
    for distance, over, _ in send_figures(exe):
        assert abs(over - 6.02) < 0.05, (
            f"{distance} units: the send is {over:+.2f} dB over room level and direct rolloff, the Xbox's +6.0")
    # the room rolloff factor (the environment's) does nothing
    for distance in SEND_DISTANCES:
        plain = run(exe, "send", distance, 1.0, -1204, 0.0)[0]
        factored = run(exe, "send", distance, 1.0, -1204, 3.0)[0]
        assert abs(db(plain) - db(factored)) < 0.01, f"{distance} units: the room rolloff factor moved the send"


def lowpass_response(a, frequency):
    """the one-pole y = x + a (y' - x)'s gain at frequency, in dB"""
    z = cmath.exp(-2j * math.pi * frequency / 48000.0)
    return db(abs((1.0 - a) / (1.0 - a * z)))


FREQUENCIES = [125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0]


def send_spectrum(exe, *arguments):
    _, room, _, room_lowpass = run(exe, "i3dl2", *arguments)
    return [db(room) + lowpass_response(room_lowpass, f) for f in FREQUENCIES]


def check_obstruction(exe):
    obstructed = send_spectrum(exe, -1200, 0.0, 0, 0.0)
    occluded = send_spectrum(exe, 0, 0.0, -1200, 0.0)
    clear = send_spectrum(exe, 0, 0.0, 0, 0.0)
    for frequency, a, b in zip(FREQUENCIES, obstructed, occluded):
        assert abs(a - b) < 2.0, f"{frequency:.0f} Hz: obstructed send {a:.2f} dB, occluded {b:.2f} dB"
    # (and obstruction does muffle the send: 5 kHz, the HF reference, near
    # 12 dB down)
    assert clear[6] - obstructed[6] > 8.0, "obstruction does not reach the send"
    # its LF ratio part stays the direct path's: an obstruction at LF ratio
    # 0.5 turns the direct path down 6 dB, the send's low end not at all
    direct_clear = run(exe, "i3dl2", 0, 0.0, 0, 0.0)[0]
    direct_half = run(exe, "i3dl2", -1200, 0.5, 0, 0.0)[0]
    assert abs(db(direct_clear) - db(direct_half) - 6.0) < 0.05
    low_half = send_spectrum(exe, -1200, 0.5, 0, 0.0)[0]
    assert abs(low_half - clear[0]) < 1.0, f"the send's low end moved {low_half - clear[0]:+.2f} dB"


def check_level(exe):
    xbox = run(exe, "voice2d", "xbox")[0]
    assert abs(db(xbox) + 6.02) < 0.01, f"a 2D voice at full volume comes out at {db(xbox):+.2f} dB"
    louder = run(exe, "voice2d", "louder")[0]
    assert abs(db(louder)) < 0.01, f"louder: a 2D voice at full volume comes out at {db(louder):+.2f} dB"
    # a sum past full scale still comes out whole, up to twice it
    for amplitude in (1.0, 1.5, 1.95):
        peak = run(exe, "limit", "xbox", amplitude)[0]
        assert abs(peak - amplitude / 2.0) < 0.002, f"a sum of {amplitude}: {peak:.4f} out, not {amplitude / 2}"
    peak = run(exe, "limit", "xbox", 3.0)[0]
    assert peak <= 1.0001 and peak > 0.99, f"a sum of 3.0: {peak:.4f} out"
    peak = run(exe, "limit", "louder", 1.0)[0]
    assert abs(peak - 0.891) < 0.002, f"louder, a sum of 1.0: {peak:.4f} out"


def with_build(check):
    with tempfile.TemporaryDirectory() as work:
        exe = build(Path(work))
        if exe is None:
            import pytest
            pytest.skip("no C compiler")
        check(exe)


def test_reverb_send_follows_direct_rolloff():
    with_build(check_send)


def test_obstruction_reaches_send():
    with_build(check_obstruction)


def test_output_level():
    with_build(check_level)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as work:
        exe = build(Path(work))
        if exe is None:
            sys.exit("no C compiler")
        print("reverb send (minimum distance 1, room -1204 mB):")
        for distance, over, level in send_figures(exe):
            print(f"  {distance:5.1f} units: {level:+7.2f} dB, {over:+.2f} dB over room level and direct rolloff")
        print("send spectra (dB):  " + " ".join(f"{f:>7.0f}" for f in FREQUENCIES))
        for name, arguments in [("clear", (0, 0.0, 0, 0.0)), ("obstructed", (-1200, 0.0, 0, 0.0)),
                                ("occluded", (0, 0.0, -1200, 0.0))]:
            print(f"  {name:>16}: " + " ".join(f"{v:7.2f}" for v in send_spectrum(exe, *arguments)))
        print(f"2D voice at full volume: xbox {db(run(exe, 'voice2d', 'xbox')[0]):+.2f} dB, "
              f"louder {db(run(exe, 'voice2d', 'louder')[0]):+.2f} dB")
        for loudness, amplitude in [("xbox", 1.5), ("xbox", 1.95), ("xbox", 3.0), ("louder", 1.0)]:
            print(f"  {loudness}, a sum of {amplitude}: peak {run(exe, 'limit', loudness, amplitude)[0]:.4f}")
        check_send(exe)
        check_obstruction(exe)
        check_level(exe)
        print("ok")
