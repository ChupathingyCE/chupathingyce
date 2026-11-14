#!/usr/bin/env python3
"""The Xbox's HRTF and reverb models (port/linux/src/dsound_xbox_hrtf.inl,
dsound_xbox_reverb.inl) built alone, against the figures OpenH1 measured on
an Xbox (docs/reading/xbox_audio.md): each sound environment's decay times
and first reflection, and the HRTF's time between the ears and its front,
back and below cues. Needs clang (or cc) and Python 3 alone.

    python3 -m pytest tools/test_xbox_audio.py
    python3 tools/test_xbox_audio.py          (prints the figures)
"""
import math
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port/linux/src"

# what the mixer gives the models (dsound_sdl.c), the least of it
PRELUDE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
typedef long LONG;
typedef struct
{
	LONG lRoom, lRoomHF;
	float flRoomRolloffFactor, flDecayTime, flDecayHFRatio;
	LONG lReflections;
	float flReflectionsDelay;
	LONG lReverb;
	float flReverbDelay, flDiffusion, flDensity, flHFReference;
} DSI3DL2LISTENER;
#define DSBVOLUME_MIN (-10000)
#define OUTPUT_RATE 48000
#define MIX_CHUNK_FRAMES 256
static unsigned long output_channels = 2;
static DSI3DL2LISTENER environment;
static unsigned long environment_serial;
static BOOL reverb_enabled = TRUE;
static float flush_denormal(float value) { return fabsf(value) < 1.0e-20f ? 0.0f : value; }
static float clamp_real(float value, float minimum, float maximum);
#include "dsound_xbox_types.h"
struct sdl_stream
{
	struct hrtf_parameters hrtf_current;
	float hrtf_frames[2][HRTF_FRAMES];
	float hrtf_bands[HRTF_BANDS - 1][2];
};
#include "dsound_xbox_hrtf.inl"
#include "dsound_xbox_reverb.inl"
'''

# the maps' sound environments (their tags' values) and the Xbox's figures
# of each: the first reflection (frames at 48 kHz) and the decay times at
# 500 Hz and at the HF reference (seconds), measured by running the retail
# DSP program with the parameters DirectSound works out
ENVIRONMENTS = [
    # name, room, room_hf, decay, hf_ratio, reflections, refl_delay, reverb, reverb_delay, diffusion, density; first, rt_low, rt_high
    ("alley", 0.316, 0.72, 1.49, 0.86, 0.079, 0.007, 0.0995, 0.011, 1, 1, 336, 1.528, 1.304),
    ("arena", 0.316, 0.447, 7.24, 0.33, 0.0825, 0.02, 0.102, 0.03, 1, 1, 952, 7.032, 2.900),
    ("cave", 0.316, 1.0, 2.91, 1.3, 0.158, 0.015, 0.0707, 0.022, 1, 1, 712, 2.941, 2.961),
    ("concert_hall", 0.316, 0.5623, 3.92, 0.7, 0.0767, 0.02, 0.0997, 0.029, 1, 1, 952, 3.922, 2.847),
    ("forest", 0.316, 0.022, 1.49, 0.54, 0.0165, 0.162, 0.0492, 0.088, 0.79, 1, 7776, 1.480, 1.052),
    ("hallway", 0.316, 0.71, 1.49, 0.59, 0.077, 0.007, 0.166, 0.011, 1, 1, 336, 1.511, 0.942),
    ("hangar", 0.316, 0.316, 10.05, 0.23, 0.158, 0.02, 0.125, 0.03, 1, 1, 952, 9.660, 3.056),
    ("mountains", 0.316, 0.0562, 1.49, 0.21, 0.0128, 0.3, 0.009, 0.1, 0.27, 1, 2400, 1.116, 0.509),
    ("stone_room", 0.316, 0.71, 2.31, 0.64, 0.139, 0.012, 0.11, 0.017, 1, 1, 576, 2.323, 1.555),
    ("underwater", 0.316, 0.01, 1.49, 0.1, 0.188, 0.007, 0.71, 0.011, 1, 1, 336, 0.481, 0.059),
]

PROGRAM = PRELUDE + r'''
static LONG volume_from_gain(double gain, LONG maximum)
{
	LONG volume;
	if (gain == 0.0) return DSBVOLUME_MIN;
	volume = (LONG)(2000.0 * log10(gain) + maximum);
	return volume < DSBVOLUME_MIN ? DSBVOLUME_MIN : volume > maximum ? maximum : volume;
}
static double *band_energy(const float *x, unsigned long frames, double hertz)
{
	double *energy = calloc(frames, sizeof(double));
	double w = 2 * M_PI * hertz / OUTPUT_RATE, alpha = sin(w) / (2 * 1.414), a0 = 1 + alpha;
	double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2 * cos(w) / a0, a2 = (1 - alpha) / a0;
	double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
	unsigned long i;
	for (i = 0; i < frames; i++)
	{
		double xi = x[i * 2], y = b0 * xi + b2 * x2 - a1 * y1 - a2 * y2;
		x2 = x1; x1 = xi; y2 = y1; y1 = y; energy[i] = y * y;
	}
	return energy;
}
static double decay_time(const double *energy, unsigned long frames)
{
	double *s = malloc(frames * sizeof(double)), total = 0; long i, a = -1, b = -1;
	for (i = (long)frames - 1; i >= 0; i--) { total += energy[i]; s[i] = total; }
	if (total <= 0) { free(s); return 0; }
	for (i = 0; i < (long)frames; i++)
	{
		double db = 10 * log10(s[i] / total + 1e-300);
		if (a < 0 && db <= -5) a = i;
		if (b < 0 && db <= -25) { b = i; break; }
	}
	free(s);
	return a < 0 || b < 0 || b <= a ? 0 : 3.0 * (b - a) / OUTPUT_RATE;
}
static void response(DSI3DL2LISTENER p, unsigned long serial, float *out, unsigned long frames)
{
	float send[MIX_CHUNK_FRAMES];
	unsigned long at = 0;
	memset(&reverb, 0, sizeof(reverb));
	environment = p; environment_serial = serial;
	reverb_initialize();
	memset(out, 0, frames * 2 * sizeof(float));
	while (at < frames)
	{
		unsigned long n = frames - at < MIX_CHUNK_FRAMES ? frames - at : MIX_CHUNK_FRAMES;
		memset(send, 0, sizeof(send));
		if (at == 0) send[0] = 1.0f;
		reverb_process(send, out + at * 2, n, 1.0f);
		at += n;
	}
}
/* reverb <room> <room_hf> <decay> <hf_ratio> <reflections> <refl_delay> <reverb> <reverb_delay> <diffusion> <density>:
first reflection frame, RT60 at 500 Hz, RT60 at the HF reference */
static void reverb_figures(char **argv)
{
	DSI3DL2LISTENER p, q;
	unsigned long frames = OUTPUT_RATE * 12, early = OUTPUT_RATE / 2, f;
	float *out = calloc(frames * 2, sizeof(float));
	double *e, low, high; long first = -1;
	p.lRoom = 0; p.lRoomHF = 0; p.flRoomRolloffFactor = 0;
	p.flDecayTime = (float)atof(argv[2]); p.flDecayHFRatio = (float)atof(argv[3]);
	p.lReflections = volume_from_gain(atof(argv[4]), 1000); p.flReflectionsDelay = (float)atof(argv[5]);
	p.lReverb = volume_from_gain(atof(argv[6]), 2000); p.flReverbDelay = (float)atof(argv[7]);
	p.flDiffusion = (float)(atof(argv[8]) * 100); p.flDensity = (float)(atof(argv[9]) * 100); p.flHFReference = 5000.0f;
	q = p; q.lReverb = DSBVOLUME_MIN; response(q, 1, out, early);
	for (f = 0; f < early && first < 0; f++) if (fabs(out[f * 2]) > 1e-3) first = (long)f;
	q = p; q.lReflections = DSBVOLUME_MIN; response(q, 2, out, frames);
	e = band_energy(out, frames, 500.0); low = decay_time(e, frames); free(e);
	e = band_energy(out, frames, p.flHFReference); high = decay_time(e, frames); free(e);
	printf("first=%ld low=%.3f high=%.3f\n", first, low, high);
}
#define FRAMES 1024
static void hrtf_response(double azimuth, double elevation, float *out)
{
	struct hrtf_parameters p;
	struct sdl_stream *stream = calloc(1, sizeof(*stream));
	float mono[MIX_CHUNK_FRAMES];
	double az = azimuth * M_PI / 180, el = elevation * M_PI / 180;
	unsigned long at;
	hrtf_place((float)(cos(el) * sin(az)), (float)sin(el), (float)(cos(el) * cos(az)), 3.0f, 0.0f, FALSE, &p);
	p.gain = 1.0f; p.pair[0] = 1.0f; p.pair[1] = 0.0f;
	stream->hrtf_current = p;
	memset(out, 0, FRAMES * 2 * sizeof(float));
	for (at = 0; at < FRAMES; at += MIX_CHUNK_FRAMES)
	{
		memset(mono, 0, sizeof(mono));
		if (at == 0) mono[0] = 1.0f;
		hrtf_process(stream, mono, out + at * 2, MIX_CHUNK_FRAMES, MIX_CHUNK_FRAMES, &p, 1.0f, 0.0f, 1.0f);
	}
	free(stream);
}
/* an ear's level in an octave (dB re a unit impulse) */
static double level(const float *out, int ear, double centre)
{
	double lo = centre / sqrt(2.0), hi = centre * sqrt(2.0), energy = 0; int k, bins = 0;
	for (k = 1; k < FRAMES / 2; k++)
	{
		double f = (double)k * OUTPUT_RATE / FRAMES, re = 0, im = 0; int n;
		if (f < lo || f > hi) continue;
		for (n = 0; n < FRAMES; n++) { double w = 2 * M_PI * k * n / FRAMES; re += out[n * 2 + ear] * cos(w); im -= out[n * 2 + ear] * sin(w); }
		energy += re * re + im * im; bins++;
	}
	return 10 * log10(energy / (bins ? bins : 1) + 1e-30);
}
/* the time between the ears (ms): the lag of the greatest cross-correlation below 1.5 kHz */
static double itd(const float *out)
{
	static double l[FRAMES], r[FRAMES];
	double a = exp(-2 * M_PI * 1500.0 / OUTPUT_RATE), yl = 0, yr = 0, best = -1e30; int n, lag, best_lag = 0;
	for (n = 0; n < FRAMES; n++) { yl = a * yl + (1 - a) * out[n * 2]; yr = a * yr + (1 - a) * out[n * 2 + 1]; l[n] = yl; r[n] = yr; }
	for (lag = -60; lag <= 60; lag++)
	{
		double c = 0;
		for (n = 0; n < FRAMES; n++) { int m = n - lag; if (m >= 0 && m < FRAMES) c += l[n] * r[m]; }
		if (c > best) { best = c; best_lag = lag; }
	}
	return best_lag * 1000.0 / OUTPUT_RATE;
}
/* hrtf <azimuth> <elevation>: the time between the ears, and each ear's level at 8 and 16 kHz */
static void hrtf_figures(char **argv)
{
	float out[FRAMES * 2];
	hrtf_initialize();
	hrtf_response(atof(argv[2]), atof(argv[3]), out);
	printf("itd=%.3f l8k=%.2f l16k=%.2f r8k=%.2f r16k=%.2f\n", itd(out), level(out, 0, 8000), level(out, 0, 16000),
		level(out, 1, 8000), level(out, 1, 16000));
}
/* taps: reverb_tap's index against the DSP's rule written out with
divisions (a delay under a block read as a block, one past the line wrapped
round it), on every line's size, at every cursor near the ends, for every
delay up to past twice the size */
static void tap_figures(void)
{
	static const long sizes[] = { 612, 640, 680, 720, 1392, 1684, 2044, 2476, REVERB_INPUT_SIZE, REVERB_LOOP_SIZE };
	static float line[REVERB_LOOP_SIZE];
	long bad = 0, checked = 0;
	unsigned i;
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
	{
		long size = sizes[i], at, delay, k;
		for (k = 0; k < size; k++) line[k] = (float)k;
		for (at = 0; at < size; at += at < 40 || at > size - 40 ? 1 : 97)
			for (delay = 0; delay < 2 * size + 40; delay += delay < 64 || (delay > size - 40 && delay < size + 40) ? 1 : 13)
			{
				long d = delay < 16 ? 16 : (delay - 16) % size + 16;
				long expected = (at - d % size + size) % size;
				checked++;
				if (reverb_tap(line, size, at, delay) != (float)expected) bad++;
			}
	}
	printf("checked=%ld bad=%ld\n", checked, bad);
}
/* script: the reverb through rooms changing, a step a tick, turned off and
on, with bursts, clicks and silences, on stereo and 5.1; the output's bits
hashed (FNV-1a) */
static void script_figures(void)
{
	static const float rooms[][11] =
	{
		{ 1.49f, 0.59f, 0.077f, 0.007f, 0.166f, 0.011f, 1, 1 }, { 2.91f, 1.3f, 0.158f, 0.015f, 0.0707f, 0.022f, 1, 1 },
		{ 10.05f, 0.23f, 0.158f, 0.02f, 0.125f, 0.03f, 1, 1 }, { 1.49f, 0.21f, 0.0128f, 0.3f, 0.009f, 0.1f, 0.27f, 1 },
		{ 1.49f, 0.54f, 0.0165f, 0.162f, 0.0492f, 0.088f, 0.79f, 1 }, { 1.49f, 0.1f, 0.188f, 0.007f, 0.71f, 0.011f, 1, 1 },
		{ 0, 0, 0, 0, 0, 0, 1, 1 },
	};
	float send[MIX_CHUNK_FRAMES], out[MIX_CHUNK_FRAMES * 6];
	unsigned long hash = 2166136261UL, seed = 7, chunk, k, channels;
	for (channels = 2; channels <= 6; channels += 4)
	{
		memset(&reverb, 0, sizeof(reverb));
		output_channels = channels;
		for (chunk = 0; chunk < 40 * OUTPUT_RATE / MIX_CHUNK_FRAMES; chunk++)
		{
			unsigned long n = chunk % 37 == 5 ? 97 : MIX_CHUNK_FRAMES, room = chunk / 120 % 7;
			double second = (double)chunk * MIX_CHUNK_FRAMES / OUTPUT_RATE;
			BOOL enabled = !(second > 30.0 && second < 32.0);
			if (chunk % 120 == 0 || (second > 25.0 && second < 26.0 && chunk % 6 == 0))
			{
				const float *r = rooms[second > 25.0 && second < 26.0 ? chunk / 6 % 3 : room];
				DSI3DL2LISTENER p;
				p.lRoom = 0; p.lRoomHF = room == 1 ? 0 : -300; p.flRoomRolloffFactor = 0;
				p.flDecayTime = r[0] ? r[0] : 1.0f; p.flDecayHFRatio = r[1] ? r[1] : 1.0f;
				p.lReflections = r[0] ? volume_from_gain(r[2], 1000) : DSBVOLUME_MIN; p.flReflectionsDelay = r[3];
				p.lReverb = r[0] ? volume_from_gain(r[4], 2000) : DSBVOLUME_MIN; p.flReverbDelay = r[5];
				p.flDiffusion = r[6] * 100; p.flDensity = r[7] * 100; p.flHFReference = 5000.0f;
				environment = p; environment_serial++;
				if (chunk == 0) reverb_initialize();
			}
			if (reverb.serial != environment_serial && !reverb.fade)
				reverb_update(&environment, environment_serial);
			for (k = 0; k < n; k++)
			{
				double s = second + (double)k / OUTPUT_RATE;
				seed = (seed * 1103515245UL + 12345UL) & 0xffffffffUL;
				send[k] = fmod(s, 4.0) < 1.5 ? 0.3f * ((float)((seed >> 8) & 0xffff) / 32768.0f - 1.0f) : 0.0f;
				if ((chunk * MIX_CHUNK_FRAMES + k) % 9600 == 0) send[k] += 0.9f;
				if (s > 33.0 && s < 38.0) send[k] = 0.0f;
			}
			memset(out, 0, sizeof(out));
			if (!reverb.silent || (enabled && reverb_sent(send, n)))
			{
				reverb_process(send, out, n, enabled ? 1.0f : 0.0f);
				if ((!enabled && reverb.level <= 0.0f) || reverb.quiet > REVERB_QUIET_FRAMES)
					reverb_clear();
			}
			for (k = 0; k < n * channels; k++)
			{
				/* (0 and -0 alike: the steady frames skip the fade's x + 0) */
				float value = out[k] == 0.0f ? 0.0f : out[k];
				unsigned char bytes[sizeof(float)];
				unsigned b;
				memcpy(bytes, &value, sizeof(bytes));
				for (b = 0; b < sizeof(bytes); b++)
					hash = ((hash ^ bytes[b]) * 16777619UL) & 0xffffffffUL;
			}
		}
	}
	printf("hash=%08lx\n", hash);
}
int main(int argc, char **argv)
{
	if (argc >= 10 && !strcmp(argv[1], "reverb")) reverb_figures(argv);
	else if (argc >= 4 && !strcmp(argv[1], "hrtf")) hrtf_figures(argv);
	else if (argc >= 2 && !strcmp(argv[1], "taps")) tap_figures();
	else if (argc >= 2 && !strcmp(argv[1], "script")) script_figures();
	else return 2;
	return 0;
}
'''


def build(work, *defines):
    compiler = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not compiler:
        return None
    source = work / "xbox_audio.c"
    source.write_text(PROGRAM)
    exe = work / ("xbox_audio" + "".join("-" + define.lower() for define in defines))
    # (no fused multiply-adds, as the game's builds: the steady frames and
    # the fade's must round alike)
    result = subprocess.run([compiler, "-O2", "-std=gnu11", "-ffp-contract=off", "-Wall", "-Wextra",
                             "-Wno-unused-function", "-Wno-unused-parameter", *("-D" + define for define in defines),
                             "-I", str(SRC), str(source), "-o", str(exe), "-lm"],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return exe


def run(exe, *args):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stderr
    return dict(field.split("=") for field in result.stdout.split())


def reverb_figures(exe, environment):
    name, room, room_hf, decay, hf_ratio, reflections, refl_delay, reverb, reverb_delay, diffusion, density = environment[:11]
    fields = run(exe, "reverb", decay, hf_ratio, reflections, refl_delay, reverb, reverb_delay, diffusion, density)
    return int(fields["first"]), float(fields["low"]), float(fields["high"])


def check_reverb(exe):
    for environment in ENVIRONMENTS:
        name, first_xbox, low_xbox, high_xbox = environment[0], environment[11], environment[12], environment[13]
        first, low, high = reverb_figures(exe, environment)
        # (the Xbox's first reflection to the frame; its decay times within a tenth, the HF ones of the
        # long rooms within a half: the reverb's low pass is a fit, not the DSP's word for word)
        assert first == first_xbox, f"{name}: the first reflection at frame {first}, the Xbox's at {first_xbox}"
        assert abs(low - low_xbox) <= 0.1 + 0.03 * low_xbox, f"{name}: RT60 at 500 Hz {low:.2f} s, the Xbox's {low_xbox:.2f}"
        assert abs(high - high_xbox) <= 0.15 + 0.45 * high_xbox, f"{name}: RT60 at 5 kHz {high:.2f} s, the Xbox's {high_xbox:.2f}"


def check_hrtf(exe):
    side = run(exe, "hrtf", 90, 0)
    ahead = run(exe, "hrtf", 0, 0)
    behind = run(exe, "hrtf", 180, 0)
    below = run(exe, "hrtf", 0, -90)
    # the time between the ears to the side: the Xbox's 0.723 ms (recorded)
    assert abs(float(side["itd"]) - 0.723) < 0.06, f"the time between the ears to the side {side['itd']} ms, the Xbox's 0.723"
    # the far ear's shadow to the side: the Xbox's 20.6 dB at 8 kHz
    shadow = float(side["r8k"]) - float(side["l8k"])
    assert abs(shadow - 20.6) < 3.0, f"the shadow of the far ear at 8 kHz {shadow:.1f} dB, the Xbox's 20.6"
    # a sound behind: the Xbox's loses 7.7 dB at 8 kHz and 19.8 at 16 kHz against one ahead
    loss = float(ahead["l16k"]) - float(behind["l16k"])
    assert abs(loss - 19.8) < 4.0, f"behind, the loss at 16 kHz {loss:.1f} dB, the Xbox's 19.8"
    # straight below: 20 dB down at 8 kHz
    loss = float(ahead["l8k"]) - float(below["l8k"])
    assert abs(loss - 20.2) < 4.0, f"below, the loss at 8 kHz {loss:.1f} dB, the Xbox's 20.2"
    # the ears alike ahead
    assert abs(float(ahead["l8k"]) - float(ahead["r8k"])) < 0.01 and abs(float(ahead["itd"])) < 0.03


def test_reverb_against_the_xbox():
    with tempfile.TemporaryDirectory() as work:
        exe = build(Path(work))
        if exe is None:
            import pytest
            pytest.skip("no C compiler")
        check_reverb(exe)


def test_hrtf_against_the_xbox():
    with tempfile.TemporaryDirectory() as work:
        exe = build(Path(work))
        if exe is None:
            import pytest
            pytest.skip("no C compiler")
        check_hrtf(exe)


def test_reverb_taps_match_the_dsp_rule():
    with tempfile.TemporaryDirectory() as work:
        exe = build(Path(work))
        if exe is None:
            import pytest
            pytest.skip("no C compiler")
        fields = run(exe, "taps")
        assert int(fields["checked"]) > 100000 and fields["bad"] == "0", fields


def test_reverb_steady_frames_match_the_fade():
    # the reverb's frames with no fade running take a shorter way (the to
    # environment alone); its output must be the fade's arithmetic's, bit for bit
    with tempfile.TemporaryDirectory() as work:
        exe = build(Path(work))
        if exe is None:
            import pytest
            pytest.skip("no C compiler")
        fading = build(Path(work), "REVERB_ALWAYS_FADE")
        assert run(exe, "script")["hash"] == run(fading, "script")["hash"]


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as work:
        exe = build(Path(work))
        if exe is None:
            sys.exit("no C compiler")
        print(f"{'environment':13s} {'first':>6s} {'(Xbox)':>6s} {'RT60 500':>9s} {'(Xbox)':>7s} {'RT60 5k':>8s} {'(Xbox)':>7s}")
        for environment in ENVIRONMENTS:
            first, low, high = reverb_figures(exe, environment)
            print(f"{environment[0]:13s} {first:6d} {environment[11]:6d} {low:9.2f} {environment[12]:7.2f} {high:8.2f} {environment[13]:7.2f}")
        for azimuth, elevation in ((0, 0), (90, 0), (180, 0), (0, -90), (0, 90)):
            fields = run(exe, "hrtf", azimuth, elevation)
            print(f"hrtf {azimuth:3d} round, {elevation:3d} up: " + " ".join(f"{k}={v}" for k, v in fields.items()))
