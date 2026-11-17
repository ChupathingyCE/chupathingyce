"""Checks how a decoded sound is brought to half or twice its rate
(port/linux/game/ce_vorbis.c, ce_vorbis_resample): a 44.1 kHz mono sound
played at 22.05 kHz, as the Xbox's channels play mono, or 22.05 kHz Ogg
Vorbis in a 44.1 kHz sound. ce_vorbis.c is compiled alone with the build
machine's C compiler, and test tones are put through it: what is under
about 9.5 kHz keeps its level, what is above 11.025 kHz (which halving would
fold back under it) is gone, and doubling adds no images. Needs clang (or
cc) and Python 3 alone.

    python3 -m pytest tools/test_ce_vorbis_resample.py
    python3 tools/test_ce_vorbis_resample.py          (prints the figures)
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent

# a tone in, its level and the level at another frequency out, in dB:
# halve|double <frequency> <frequency measured> on each line of stdin
HARNESS = r"""
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int ce_vorbis_resample(short **samples, int frames, int channels, int rate, int wanted);
#define FRAMES 44100
static double level(const short *samples, int frames, int channels, int channel, double rate, double frequency)
{
	/* the tone's amplitude, by one DFT bin over a Hann window, away from the ends */
	double re = 0.0, im = 0.0, total = 0.0;
	int start = frames / 8, count = frames - 2 * (frames / 8), i;
	for (i = 0; i < count; i++)
	{
		double window = 0.5 - 0.5 * cos(2.0 * M_PI * i / (count - 1));
		double phase = 2.0 * M_PI * frequency * (start + i) / rate;
		re += window * samples[(start + i) * channels + channel] * cos(phase);
		im += window * samples[(start + i) * channels + channel] * sin(phase);
		total += window;
	}
	return 20.0 * log10(2.0 * sqrt(re * re + im * im) / total / 16384.0 + 1e-12);
}
int main(void)
{
	char mode[16];
	double frequency, measured;
	while (scanf("%15s %lf %lf", mode, &frequency, &measured) == 3)
	{
		int halve = !strcmp(mode, "halve");
		if (!strcmp(mode, "steady") || !strcmp(mode, "steady2"))
		{
			/* a steady level of <frequency> frames, halved or doubled: the
			largest step away from it anywhere, the ends too */
			int frames = (int)frequency, i, out, worst = 0;
			short *samples = malloc((size_t)frames * sizeof(*samples));
			for (i = 0; i < frames; i++)
				samples[i] = 10000;
			out = ce_vorbis_resample(&samples, frames, 1, mode[6] ? 22050 : 44100, mode[6] ? 44100 : 22050);
			for (i = 0; i < out; i++)
				worst = abs(samples[i] - 10000) > worst ? abs(samples[i] - 10000) : worst;
			printf("%d %d 0\n", out, worst);
			free(samples);
			continue;
		}
		int channels = 2, rate = halve ? 44100 : 22050, wanted = halve ? 22050 : 44100;
		int frames = halve ? FRAMES : FRAMES / 2, i, out;
		short *samples = malloc((size_t)frames * channels * sizeof(*samples));
		for (i = 0; i < frames; i++)
		{
			/* (the second channel silent: the channels stay apart) */
			samples[i * channels] = (short)lrint(16384.0 * sin(2.0 * M_PI * frequency * i / rate));
			samples[i * channels + 1] = 0;
		}
		out = ce_vorbis_resample(&samples, frames, channels, rate, wanted);
		printf("%d %.3f %.3f\n", out, level(samples, out, channels, 0, wanted, measured),
			level(samples, out, channels, 1, wanted, measured));
		free(samples);
	}
	return 0;
}
"""


def compiler():
    for name in (os.environ.get("CC"), "clang", "cc", "gcc"):
        if name and shutil.which(name):
            return name
    return None


_BINARY = None


def binary():
    global _BINARY
    if _BINARY is None:
        cc = compiler()
        if not cc:
            pytest.skip("no C compiler")
        folder = Path(tempfile.mkdtemp(prefix="ce_vorbis_resample_"))
        (folder / "harness.c").write_text(HARNESS, encoding="utf-8")
        _BINARY = folder / "harness"
        subprocess.run([cc, "-O2", "-DHALO_CUSTOM_EDITION", "-I", str(ROOT / "port/third_party/stb"),
                        str(ROOT / "port/linux/game/ce_vorbis.c"), str(folder / "harness.c"),
                        "-o", str(_BINARY), "-lm"], check=True, capture_output=True)
    return _BINARY


def run(cases):
    text = "".join(f"{mode} {frequency} {measured}\n" for mode, frequency, measured in cases)
    out = subprocess.run([str(binary())], input=text, capture_output=True, text=True, check=True).stdout
    return [(int(a), float(b), float(c)) for a, b, c in (line.split() for line in out.splitlines())]


def halved(frequency, measured=None):
    return run([("halve", frequency, frequency if measured is None else measured)])[0]


def test_halving_keeps_the_frames():
    frames, _, _ = halved(1000)
    assert frames == 22050


@pytest.mark.parametrize("frequency", [100, 1000, 4000, 8000, 9000])
def test_halving_keeps_the_pass_band(frequency):
    _, db, silent = halved(frequency)
    assert abs(db) < 0.1, f"{frequency} Hz is {db:.2f} dB after halving"
    assert silent < -100


@pytest.mark.parametrize("frequency", [12500, 14000, 16000, 18000, 20000])
def test_halving_drops_what_would_fold_back(frequency):
    # (a tone above 11.025 kHz folds to 22.05 kHz less it)
    _, db, _ = halved(frequency, 22050 - frequency)
    assert db < -60, f"{frequency} Hz folds back to {22050 - frequency} Hz at {db:.1f} dB"


@pytest.mark.parametrize("frames", [2, 3, 10, 95, 96, 97, 1000])
def test_a_steady_level_stays_steady_to_the_ends(frames):
    (halved_frames, worst, _), = run([("steady", frames, 0)])
    assert halved_frames == frames // 2
    assert worst <= 1
    (doubled_frames, worst, _), = run([("steady2", frames, 0)])
    assert doubled_frames == frames * 2
    assert worst <= 1


@pytest.mark.parametrize("frequency", [1000, 5000, 9000])
def test_doubling_keeps_the_tone_and_adds_no_image(frequency):
    (frames, db, _), = run([("double", frequency, frequency)])
    (_, image, _), = run([("double", frequency, 22050 - frequency)])
    assert frames == 44100
    assert abs(db) < 0.1
    assert image < -60, f"{frequency} Hz doubled has an image at {22050 - frequency} Hz of {image:.1f} dB"


if __name__ == "__main__":
    for frequency in (1000, 4000, 8000, 9000, 9500, 10000, 10500, 11025, 11500, 12500, 14000, 16000, 18000, 20000):
        frames, db, _ = halved(frequency, frequency if frequency < 11025 else 22050 - frequency)
        print(f"halve {frequency:6d} Hz -> {frequency if frequency < 11025 else 22050 - frequency:6d} Hz: {db:7.2f} dB")
    sys.exit(0)
