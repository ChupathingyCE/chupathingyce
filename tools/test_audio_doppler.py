"""Compile the real Doppler and resampler code of the desktop builds and check it.

- observer.c's observer_tick_velocity_add, driven the way the native builds
  run the observer: once a frame, a first-person camera snapping to where the
  player is after each 30 Hz tick. A player moving at a steady speed must give
  the listener that speed at 30 to 240 frames a second (and at 20, two ticks
  in some frames), as the Xbox did at 30. The negative control is the
  observer's own velocity, the last frame's, which jumps about at 144.
- dsound_sdl.c's doppler_glide: a new Doppler shift is reached over a tick,
  in even steps, a voice starting over taking it at once.
- dsound_sdl.c's resampler (its tables and its tap sum at the output rate or
  slower): a 22.05 kHz voice resampled to 48 kHz keeps a 10 kHz tone, and the
  images of a 10 kHz and a 5 kHz tone are far down.

No game assets are needed. Run with Python and a C compiler (cc) on PATH.
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def function(source, name):
    match = re.search(r"^(?:static )?[\w *]+\b" + name + r"\(\s*[^;{]*\)\s*\{", source, re.M)
    if not match:
        raise ValueError(f"Function not found: {name}")
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    # These functions have no braces in strings or comments.
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def structure(source, name):
    match = re.search(r"^struct " + name + r"\n\{.*?^\};\n", source, re.M | re.S)
    if not match:
        raise ValueError(f"Structure not found: {name}")
    return match.group(0)


def defines(source, prefix):
    lines = re.findall(r"^#define " + prefix + r"\w* .*$", source, re.M)
    if not lines:
        raise ValueError(f"No defines: {prefix}")
    return "\n".join(lines) + "\n"


PRELUDE = r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
typedef float real;
typedef int boolean;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define OUTPUT_RATE 48000
#define TICKS_PER_SECOND 30
typedef struct { real i, j, k; } real_vector3d;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
'''

TESTS = r'''
/* a player moving along i at speed units a second, the observer snapping a
first-person camera to them once a frame (observer.c's velocity:
displacement over the frame), at fps frames a second; the spread of the
velocities the listener takes after the first second, from the tick
average (ticked) or the last frame's (the old way) */
static void walk(int fps, real speed, int ticked, real *lowest, real *highest)
{
    struct observer_tick_velocity average = {0};
    real position = 0.f;
    long frame;

    *lowest = 1e30f;
    *highest = -1e30f;
    for (frame = 1; frame <= 3 * fps; frame++)
    {
        long tick = frame * TICKS_PER_SECOND / fps;
        real target = speed * (real)tick / TICKS_PER_SECOND;
        real dtime = 1.f / (real)fps;
        real_vector3d velocity = { (target - position) / dtime, 0.f, 0.f };
        real heard;

        position = target;
        observer_tick_velocity_add(&average, tick, dtime, FALSE, &velocity);
        heard = ticked ? average.velocity.i : velocity.i;
        if (frame > fps)
        {
            if (heard < *lowest) *lowest = heard;
            if (heard > *highest) *highest = heard;
        }
    }
}

static double tone_level(const float *samples, long count, double frequency, double rate)
{
    double re = 0.0, im = 0.0;
    long n;

    for (n = 0; n < count; n++)
    {
        /* (a Hann window, so a tone's leakage stays out of another's bin) */
        double window = 0.5 - 0.5 * cos(2.0 * M_PI * n / (count - 1));

        re += samples[n] * window * cos(2.0 * M_PI * frequency * n / rate);
        im += samples[n] * window * sin(2.0 * M_PI * frequency * n / rate);
    }
    return 20.0 * log10(sqrt(re * re + im * im) * 4.0 / count + 1e-20);
}

/* a tone at 22.05 kHz resampled to 48 kHz as mix_voice does at a step under
1, and its level at frequency (dB, 0 the tone's own) */
static double resampled(double tone, double frequency)
{
    enum { INPUT = 22050, OUTPUT = 48000 };
    static float input[INPUT], output[OUTPUT];
    double step = (double)INPUT / OUTPUT;
    long n;

    for (n = 0; n < INPUT; n++)
        input[n] = (float)sin(2.0 * M_PI * tone * n / INPUT);
    for (n = 0; n < OUTPUT; n++)
    {
        double moment = n * step;
        unsigned long center = (unsigned long)moment;
        double position = (moment - center) * RESAMPLER_TABLE_STEPS;
        unsigned long row = (unsigned long)position;
        float blend = (float)(position - (double)row);
        const float *weights = resampler_phases[row], *next_weights = resampler_phases[row + 1];
        long first = (long)center + 1 - RESAMPLER_ZERO_CROSSINGS, tap;
        float sample = 0.f;

        for (tap = 0; tap < 2 * RESAMPLER_ZERO_CROSSINGS; tap++)
        {
            long index = first + tap;
            float source = index >= 0 && index < INPUT ? input[index] : 0.f;

            sample += source * (weights[tap] + (next_weights[tap] - weights[tap]) * blend);
        }
        output[n] = sample;
    }
    /* (the middle half second, away from the ends) */
    return tone_level(output + OUTPUT / 4, OUTPUT / 2, frequency, OUTPUT);
}

int main(void)
{
    static const int rates[] = { 20, 30, 50, 60, 144, 165, 240 };
    real speed = 7.f, lowest, highest;
    unsigned int index;

    /* the listener's velocity: steady at every frame rate */
    for (index = 0; index < sizeof(rates) / sizeof(rates[0]); index++)
    {
        walk(rates[index], speed, TRUE, &lowest, &highest);
        printf("%3d fps: listener velocity %.3f to %.3f (moving at %.1f)\n", rates[index], lowest, highest, speed);
        CHECK(fabsf(lowest - speed) < 0.01f * speed && fabsf(highest - speed) < 0.01f * speed);
    }
    /* (the last frame's velocity, at 144: 0 between ticks, nearly 5 times
    the speed on the frame after one) */
    walk(144, speed, FALSE, &lowest, &highest);
    printf("144 fps, last frame's velocity: %.3f to %.3f\n", lowest, highest);
    CHECK(lowest < 0.1f * speed && highest > 4.f * speed);
    /* (a camera gliding to its target keeps its own velocity) */
    {
        struct observer_tick_velocity average = {0};
        real_vector3d glide = { 3.f, -2.f, 1.f };

        observer_tick_velocity_add(&average, 10, 1.f / 144.f, TRUE, &glide);
        CHECK(average.velocity.i == 3.f && average.velocity.j == -2.f && average.velocity.k == 1.f);
    }

    /* the Doppler shift's glide: a tick, in even steps; a start, at once */
    {
        float current = 1.f, target = 1.f, previous = 1.f, value = 1.f;
        unsigned long left = 0, frames = 0;
        int mixes;

        for (mixes = 0; value != 1.25f && mixes < 100; mixes++)
        {
            value = doppler_glide(&current, &target, &left, 1.25f, 256, FALSE);
            CHECK(value >= previous && value - previous <= 0.25f * 256 / DOPPLER_GLIDE_FRAMES + 1e-5f);
            previous = value;
            frames += 256;
        }
        printf("Doppler 1.00 to 1.25: %lu frames (a tick is %d)\n", frames, DOPPLER_GLIDE_FRAMES);
        CHECK(value == 1.25f && frames >= DOPPLER_GLIDE_FRAMES && frames < DOPPLER_GLIDE_FRAMES + 256);
        CHECK(doppler_glide(&current, &target, &left, 0.8f, 256, TRUE) == 0.8f);
    }

    /* the resampler */
    resampler_initialize();
    resampler_phases_initialize();
    {
        double pass5 = resampled(5000.0, 5000.0), pass10 = resampled(10000.0, 10000.0);
        double image10 = resampled(10000.0, 22050.0 - 10000.0) - pass10;
        double image5 = resampled(5000.0, 22050.0 - 5000.0) - pass5;

        printf("22.05 kHz to 48 kHz: 5 kHz %.2f dB, 10 kHz %.2f dB; images: of 10 kHz %.1f dB, of 5 kHz %.1f dB\n",
            pass5, pass10, image10, image5);
        CHECK(fabs(pass5) < 0.1);
        CHECK(pass10 > -2.0 && pass10 < 0.1);
        CHECK(image10 < -60.0 && image5 < -60.0);
    }
    puts("PASS");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=shutil.which("cc") or "cc")
    args = parser.parse_args()
    observer = (ROOT / "source/camera/observer.c").read_text()
    mixer = (ROOT / "port/linux/src/dsound_sdl.c").read_text()
    unit_source = (
        PRELUDE
        + structure(observer, "observer_tick_velocity")
        + function(observer, "observer_tick_velocity_add")
        + defines(mixer, "RESAMPLER_")
        + "#define DOPPLER_GLIDE_FRAMES (OUTPUT_RATE / 30)\n"
        + function(mixer, "doppler_glide")
        + "static float resampler_table[RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS + 2];\n"
        + "static float resampler_phases[RESAMPLER_TABLE_STEPS + 1][2 * RESAMPLER_ZERO_CROSSINGS];\n"
        + function(mixer, "bessel_i0")
        + function(mixer, "resampler_initialize")
        + function(mixer, "resampler_weight")
        + function(mixer, "resampler_phases_initialize")
        + TESTS
    )
    if "#define DOPPLER_GLIDE_FRAMES (OUTPUT_RATE / 30)" not in mixer:
        raise ValueError("DOPPLER_GLIDE_FRAMES changed: update this test")
    compiler = [args.cc, "-std=gnu11", "-O2"]
    if sys.platform != "win32":
        compiler.append("-lm")
    with tempfile.TemporaryDirectory(prefix="halo-doppler-test-") as directory:
        unit = Path(directory) / "doppler.c"
        exe = Path(directory) / "doppler.exe"
        unit.write_text(unit_source)
        subprocess.run([*compiler[:3], str(unit), "-o", str(exe), *compiler[3:]], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
