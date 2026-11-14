"""Compile the real Doppler and resampler code of the desktop builds and check it.

- observer.c's observer_tick_velocity_add, driven the way the native builds
  run the observer: once a frame, a first-person camera snapping to where the
  player is after each 30 Hz tick. A player moving at a steady speed must give
  the listener that speed at 30 to 240 frames a second (and at 20, two ticks
  in some frames), as the Xbox did at 30. The negative control is the
  observer's own velocity, the last frame's, which jumps about at 144.
- dsound_sdl.c's doppler_ratio: the first order shift an Xbox gave, as the
  OpenH1 team recorded it (a source passing at 20 units a second 1.178
  coming and 0.822 going, at 40 1.356 and 0.644); the old law's figures
  (1.216 and 0.849) are the negative control.
- dsound_sdl.c's resampler (its tables and its tap sum at the output rate or
  slower): a 22.05 kHz voice resampled to 48 kHz keeps a 10 kHz tone, and the
  images of a 10 kHz and a 5 kHz tone are far down; the Xbox's own low pass
  (xbox_resampler_initialize) samples a click at half the output's rate as
  the Xbox did (0.598, -0.120, 0.024, -0.003).
- dsound_sdl.c's subsonic filter: 3 dB down at 20 Hz, under 0.2 dB from 30 Hz
  up, 24 dB down at 10 Hz, and a steady offset gone.

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
#include <string.h>
typedef float real;
typedef int boolean;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define OUTPUT_RATE 48000
#define TICKS_PER_SECOND 30
typedef struct { real i, j, k; } real_vector3d;
typedef unsigned long DWORD;
#define DS3DMODE_NORMAL 0
#define DS3DMODE_HEADRELATIVE 1
#define DS3DMODE_DISABLE 2
#define MAXIMUM_OUTPUT_CHANNELS 8
static unsigned long output_channels = 2;
static BOOL doppler_enabled = TRUE;
struct sdl_stream { BOOL has_3d; DWORD mode; float position[3]; float velocity[3]; };
static struct { float position[3]; float velocity[3]; float distance_factor; } listener = { {0, 0, 0}, {0, 0, 0}, 3.048f };
static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float clamp_real(float value, float low, float high) { return value < low ? low : value > high ? high : value; }
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

    /* the Doppler shift: a source far down the line coming at, then going
    away at 20 and 40 units a second, as recorded on an Xbox */
    {
        static const float speeds[] = { 20.f, 40.f };
        static const float coming[] = { 1.178f, 1.356f }, going[] = { 0.822f, 0.644f };
        struct sdl_stream voice = { TRUE, DS3DMODE_NORMAL, { -1000.f, 0.f, 0.f }, { 0.f, 0.f, 0.f } };

        for (index = 0; index < 2; index++)
        {
            float toward, away;

            voice.position[0] = -1000.f;
            voice.velocity[0] = speeds[index];
            toward = doppler_ratio(&voice);
            voice.position[0] = 1000.f;
            away = doppler_ratio(&voice);
            printf("Doppler at %.0f units a second: %.3f coming (Xbox %.3f), %.3f going (Xbox %.3f)\n", speeds[index],
                toward, coming[index], away, going[index]);
            CHECK(fabsf(toward - coming[index]) < 0.002f && fabsf(away - going[index]) < 0.002f);
            /* (the old law's 1.216, 1.551: a fast pass too high) */
            CHECK(fabsf(toward - 1.216f) > 0.02f);
        }
        /* (an octave either way at most) */
        voice.position[0] = -1000.f;
        voice.velocity[0] = 1000.f;
        CHECK(doppler_ratio(&voice) == 2.f);
        doppler_enabled = FALSE;
        CHECK(doppler_ratio(&voice) == 1.f);
        doppler_enabled = TRUE;
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

    /* the Xbox's low pass: a click in a 24 kHz voice, sampled halfway
    between its frames (recorded: 0.598, -0.120, 0.024, -0.003), and on a
    frame exactly the frame */
    xbox_resampler_initialize();
    {
        static const float recorded[] = { 0.598f, -0.120f, 0.024f, -0.003f };
        int tap;

        printf("the Xbox's low pass halfway between frames:");
        for (tap = 0; tap < XBOX_RESAMPLER_ZERO_CROSSINGS; tap++)
        {
            float weight = xbox_phases[RESAMPLER_TABLE_STEPS / 2][XBOX_RESAMPLER_ZERO_CROSSINGS - 1 - tap];

            printf(" %.3f", weight);
            CHECK(fabsf(weight - recorded[tap]) < 0.0025f);
        }
        printf("\n");
        for (tap = 0; tap < XBOX_RESAMPLER_TAPS; tap++)
            CHECK(xbox_phases[0][tap] == (tap == XBOX_RESAMPLER_ZERO_CROSSINGS - 1 ? 1.f : 0.f));
    }

    /* the subsonic filter: tones' levels once settled, and an offset */
    {
        static const double frequencies[] = { 10.0, 15.0, 20.0, 30.0, 40.0, 1000.0 };
        static const double low[] = { -26.0, -12.0, -3.5, -0.3, -0.1, -0.05 }, high[] = { -22.0, -8.0, -2.5, 0.05, 0.05, 0.05 };
        enum { FRAMES = 4 * OUTPUT_RATE };
        static float block[FRAMES * 2];
        unsigned long frame;

        for (index = 0; index < sizeof(frequencies) / sizeof(frequencies[0]); index++)
        {
            double level;

            memset(subsonic_state, 0, sizeof(subsonic_state));
            for (frame = 0; frame < FRAMES; frame++)
                block[frame * 2] = block[frame * 2 + 1] = (float)sin(2.0 * M_PI * frequencies[index] * frame / OUTPUT_RATE);
            subsonic(block, FRAMES);
            {
                static float left[FRAMES / 2];

                for (frame = 0; frame < FRAMES / 2; frame++)
                    left[frame] = block[(FRAMES / 2 + frame) * 2];
                level = tone_level(left, FRAMES / 2, frequencies[index], OUTPUT_RATE);
            }
            printf("subsonic filter at %.0f Hz: %.2f dB\n", frequencies[index], level);
            CHECK(level > low[index] && level < high[index]);
        }
        memset(subsonic_state, 0, sizeof(subsonic_state));
        for (frame = 0; frame < FRAMES; frame++)
            block[frame * 2] = block[frame * 2 + 1] = 0.25f;
        subsonic(block, FRAMES);
        printf("subsonic filter: an offset of 0.25 is %.6f after 4 s\n", block[(FRAMES - 1) * 2]);
        CHECK(fabsf(block[(FRAMES - 1) * 2]) < 1e-4f);
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
        + defines(mixer, "XBOX_RESAMPLER_")
        + defines(mixer, "DOPPLER_")
        + function(mixer, "doppler_ratio")
        + "static float resampler_table[RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS + 2];\n"
        + "static float resampler_phases[RESAMPLER_TABLE_STEPS + 1][2 * RESAMPLER_ZERO_CROSSINGS];\n"
        + function(mixer, "bessel_i0")
        + function(mixer, "resampler_initialize")
        + function(mixer, "resampler_weight")
        + function(mixer, "resampler_phases_initialize")
        + "static float xbox_phases[RESAMPLER_TABLE_STEPS + 1][XBOX_RESAMPLER_TAPS];\n"
        + "static float xbox_deltas[RESAMPLER_TABLE_STEPS][XBOX_RESAMPLER_TAPS];\n"
        + function(mixer, "xbox_resampler_initialize")
        + re.search(r"^static const double subsonic_sections.*?^\};\n", mixer, re.M | re.S).group(0)
        + "static double subsonic_state[MAXIMUM_OUTPUT_CHANNELS][2][2];\n"
        + function(mixer, "subsonic")
        + TESTS
    )
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
