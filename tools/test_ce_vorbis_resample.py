"""Checks how a decoded sound is brought to half or twice its rate
(port/linux/game/content_vorbis.c, ce_vorbis_resample and _piece): a 44.1 kHz
mono sound played at 22.05 kHz, as the Xbox's mono channels play mono, or
22.05 kHz Ogg Vorbis in a 44.1 kHz sound. content_vorbis.c is compiled alone with
the build machine's C compiler, and test signals are put through it:

- what is under about 10 kHz keeps its level, what is above 11.025 kHz
  (which halving would fold back under it) is gone, and doubling adds no
  images;
- a sound split into pieces (a long sound's permutations, played one after
  another) and brought to its rate piece by piece, each with its neighbors'
  edges (ce_resources.c), is the same as the whole brought at once, and a
  looping sound with its own end and start loops as seamlessly;
- a sound's start and end with silence as their neighbors (a sound that
  is not looped is played from and into silence) are as the sound played
  in silence;
- with no neighbor known the filter takes no made-up sample, so two pieces
  with none still join without a step, and a tone keeps its shape up to
  the ends;
- the mods folder's PCM sounds at 44.1 kHz mono go through the same filter
  (port/linux/game/mod_sounds.c, samples_make);
- a permutation's ends read alone (ce_vorbis_edges) are the full
  decode's, also for a stream whose last page gives another length.

Needs clang (or cc) and Python 3 alone.

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

# commands on stdin, one a line; each prints one line
HARNESS = r"""
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int ce_vorbis_resample(short **samples, int frames, int channels, int rate, int wanted);
int ce_vorbis_resample_piece(short **samples, int frames, int channels, int rate, int wanted,
	const short *before, int before_frames, const short *after, int after_frames);
#define CONTEXT 48
static unsigned long seed = 1;
static double noise(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return ((double)((seed >> 8) & 0xffff) / 32768.0) - 1.0;
}
/* a tone's amplitude in dB (16384 = 0 dB), by one DFT bin over a Hann window */
static double level(const short *samples, int frames, int channels, int channel, double rate, double frequency)
{
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
/* a test signal: a tone (frequency > 0, on the first channel), noise
through four one-pole low passes at -frequency (frequency < 0: mostly under
about twice that), or full-band noise (0) */
static short *signal_make(int frames, int channels, double rate, double frequency)
{
	short *samples = malloc((size_t)frames * channels * sizeof(*samples));
	double state[2][4] = { { 0.0 } };
	int i, c;
	seed = 1;
	for (i = 0; i < frames; i++)
		for (c = 0; c < channels; c++)
		{
			double value;
			if (frequency > 0.0)
				value = c ? 0.0 : 16384.0 * sin(2.0 * M_PI * frequency * i / rate);
			else if (frequency < 0.0)
			{
				double k = 1.0 - exp(-2.0 * M_PI * -frequency / rate), x = 30000.0 * noise();
				int pass;
				for (pass = 0; pass < 4; pass++)
					x = state[c][pass] = state[c][pass] + k * (x - state[c][pass]);
				value = 2.0 * x;
			}
			else
				value = 10000.0 * noise();
			samples[i * channels + c] = (short)lrint(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
		}
	return samples;
}
static short *copy_of(const short *samples, int frames, int channels)
{
	short *copy = malloc((size_t)(frames ? frames : 1) * channels * sizeof(*copy));
	memcpy(copy, samples, (size_t)frames * channels * sizeof(*copy));
	return copy;
}
/* the RMS of a and b's difference over [from, to), against b's RMS over
`frames`; and the largest difference */
static void compare(const short *a, const short *b, int frames, int channels, int from, int to,
	double *difference, int *largest)
{
	double sum = 0.0, power = 0.0;
	int i;
	*largest = 0;
	for (i = 0; i < frames * channels; i++)
		power += (double)b[i] * b[i];
	for (i = from * channels; i < to * channels; i++)
	{
		int step = abs(a[i] - b[i]);
		sum += (double)step * step;
		*largest = step > *largest ? step : *largest;
	}
	*difference = sqrt(sum / ((to - from) * channels)) / sqrt(power / (frames * channels) + 1e-12);
}
int main(void)
{
	char mode[16];
	double frequency, measured;
	while (scanf("%15s %lf %lf", mode, &frequency, &measured) == 3)
	{
		if (!strcmp(mode, "halve") || !strcmp(mode, "double"))
		{
			int halve = mode[0] == 'h', channels = 2, rate = halve ? 44100 : 22050, wanted = halve ? 22050 : 44100;
			int frames = halve ? 44100 : 22050, out;
			short *samples = signal_make(frames, channels, rate, frequency);
			out = ce_vorbis_resample(&samples, frames, channels, rate, wanted);
			/* (the second channel silent: the channels stay apart) */
			printf("%d %.3f %.3f\n", out, level(samples, out, channels, 0, wanted, measured),
				level(samples, out, channels, 1, wanted, measured));
			free(samples);
		}
		else if (!strncmp(mode, "join", 4))
		{
			/* a sound of <frequency>'s kind halved (join) or doubled (join2)
			whole, and in two pieces split at <measured> frames, each with
			its neighbor's edge (or alone): the difference near the join */
			int halve = mode[4] != '2', alone = strstr(mode, "alone") != NULL, channels = 1;
			int rate = halve ? 44100 : 22050, wanted = halve ? 22050 : 44100, frames = 8000, split = (int)measured;
			short *whole = signal_make(frames, channels, rate, frequency);
			short *first = copy_of(whole, split, channels), *second = copy_of(whole + split, frames - split, channels);
			short *joined;
			int whole_frames, first_frames, second_frames, largest;
			double difference;
			int before = split < CONTEXT ? split : CONTEXT, after = frames - split < CONTEXT ? frames - split : CONTEXT;
			first_frames = ce_vorbis_resample_piece(&first, split, channels, rate, wanted, NULL, 0,
				alone ? NULL : whole + split, alone ? 0 : after);
			second_frames = ce_vorbis_resample_piece(&second, frames - split, channels, rate, wanted,
				alone ? NULL : whole + split - before, alone ? 0 : before, NULL, 0);
			whole_frames = ce_vorbis_resample(&whole, frames, channels, rate, wanted);
			joined = malloc((size_t)(first_frames + second_frames) * sizeof(*joined));
			memcpy(joined, first, (size_t)first_frames * sizeof(*joined));
			memcpy(joined + first_frames, second, (size_t)second_frames * sizeof(*joined));
			compare(joined, whole, whole_frames, channels, first_frames - 30 < 0 ? 0 : first_frames - 30,
				first_frames + 30 > whole_frames ? whole_frames : first_frames + 30, &difference, &largest);
			printf("%d %.6f %d\n", first_frames + second_frames == whole_frames, difference, largest);
			free(whole); free(first); free(second); free(joined);
		}
		else if (!strncmp(mode, "loop", 4))
		{
			/* a loop of <measured> frames, of <frequency>'s kind, halved with
			its own end and start as its neighbors (or alone) and played
			five times over, against the five times over halved at once:
			the difference at the middle two wraps (the others' are near
			the five's own ends) */
			int alone = mode[4] != 0, frames = (int)measured, rate = 44100, i, largest, count, wrap;
			short *loop = signal_make(frames, 1, rate, frequency);
			short *three = malloc((size_t)frames * 5 * sizeof(*three)), *piece, *played;
			double difference = 0.0;
			int context = frames < CONTEXT ? frames : CONTEXT;
			for (i = 0; i < 5; i++)
				memcpy(three + i * frames, loop, (size_t)frames * sizeof(*three));
			piece = copy_of(loop, frames, 1);
			count = ce_vorbis_resample_piece(&piece, frames, 1, rate, rate / 2, alone ? NULL : loop + frames - context,
				alone ? 0 : context, alone ? NULL : loop, alone ? 0 : context);
			ce_vorbis_resample(&three, frames * 5, 1, rate, rate / 2);
			played = malloc((size_t)count * 5 * sizeof(*played));
			for (i = 0; i < 5; i++)
				memcpy(played + i * count, piece, (size_t)count * sizeof(*played));
			largest = 0;
			for (wrap = 2; wrap <= 3; wrap++)
			{
				int from = wrap * count - (count < 30 ? count : 30), to = wrap * count + (count < 30 ? count : 30);
				double this_difference;
				int this_largest;
				compare(played, three, count * 5, 1, from, to, &this_difference, &this_largest);
				difference = this_difference > difference ? this_difference : difference;
				largest = this_largest > largest ? this_largest : largest;
			}
			printf("%d %.6f %d\n", count, difference, largest);
			free(loop); free(three); free(piece); free(played);
		}
		else if (!strncmp(mode, "silence", 7))
		{
			/* a sound of <frequency>'s kind, played from and into silence:
			halved (silence) or doubled (silence2) with silence as its
			neighbors, against the silence and the sound halved at once: the
			difference over the sound */
			int halve = mode[7] != '2', rate = halve ? 44100 : 22050, wanted = halve ? 22050 : 44100;
			int frames = 4000, pad = 200, count, largest;
			short *sound = signal_make(frames, 1, rate, frequency);
			short *padded = calloc((size_t)(frames + 2 * pad), sizeof(*padded));
			short zeros[CONTEXT] = { 0 };
			double difference;
			memcpy(padded + pad, sound, (size_t)frames * sizeof(*padded));
			count = ce_vorbis_resample_piece(&sound, frames, 1, rate, wanted, zeros, CONTEXT, zeros, CONTEXT);
			ce_vorbis_resample(&padded, frames + 2 * pad, 1, rate, wanted);
			compare(sound, padded + (halve ? pad / 2 : pad * 2), count, 1, 0, count, &difference, &largest);
			printf("%d %.6f %d\n", count, difference, largest);
			free(sound); free(padded);
		}
		else if (!strncmp(mode, "edge", 4))
		{
			/* a tone of <frequency> Hz alone, halved (edge) or doubled
			(edge2): the largest step of the result from the tone at the new
			rate in its first and last <measured> frames, against the tone's
			amplitude */
			int halve = mode[4] != '2', rate = halve ? 44100 : 22050, wanted = halve ? 22050 : 44100;
			int frames = halve ? 4410 : 2205, out, i, edge = (int)measured;
			short *samples = signal_make(frames, 1, rate, frequency);
			double worst = 0.0;
			out = ce_vorbis_resample(&samples, frames, 1, rate, wanted);
			for (i = 0; i < out; i++)
			{
				double ideal = 16384.0 * sin(2.0 * M_PI * frequency * i / wanted);
				if ((i < edge || i >= out - edge) && (halve || i < out - 1))
					worst = fabs(samples[i] - ideal) > worst ? fabs(samples[i] - ideal) : worst;
			}
			printf("%d %.6f 0\n", out, worst / 16384.0);
			free(samples);
		}
		else if (!strncmp(mode, "steady", 6))
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
		}
	}
	return 0;
}
"""

# samples_make (port/linux/game/mod_sounds.c), compiled with what it uses:
# a 44.1 kHz mono PCM permutation (big-endian, as a tag file has it) of a
# 1 kHz and a 15 kHz tone, made the 22.05 kHz the sound plays at
MOD_HARNESS = r"""
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char byte;
#define SOUND_COMPRESSION_NONE 0
#define SOUND_COMPRESSION_XBOX_ADPCM 1
#define MAXIMUM_SOUND_DATA_SIZE 0x1000000
int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples);
int ce_vorbis_resample(short **samples, int frames, int channels, int rate, int wanted);
void ce_vorbis_free(short *samples);
@SAMPLES_MAKE@
static double level(const short *samples, int frames, double rate, double frequency)
{
	double re = 0.0, im = 0.0, total = 0.0;
	int i;
	for (i = 0; i < frames; i++)
	{
		double window = 0.5 - 0.5 * cos(2.0 * M_PI * i / (frames - 1));
		re += window * samples[i] * cos(2.0 * M_PI * frequency * i / rate);
		im += window * samples[i] * sin(2.0 * M_PI * frequency * i / rate);
		total += window;
	}
	return 20.0 * log10(2.0 * sqrt(re * re + im * im) / total / 8192.0 + 1e-12);
}
int main(void)
{
	int frames = 44100, i;
	byte *data = malloc(frames * 2);
	unsigned long made_bytes;
	short made_compression;
	char const *reason = "";
	short *made;
	for (i = 0; i < frames; i++)
	{
		short value = (short)lrint(8192.0 * sin(2.0 * M_PI * 1000.0 * i / 44100.0) +
			8192.0 * sin(2.0 * M_PI * 15000.0 * i / 44100.0));
		data[2 * i] = (byte)((unsigned short)value >> 8);
		data[2 * i + 1] = (byte)value;
	}
	made = (short *)samples_make(data, frames * 2, SOUND_COMPRESSION_NONE, 1, 44100, 22050, &made_bytes,
		&made_compression, &reason);
	if (!made)
	{
		printf("0 0 0 %s\n", reason);
		return 0;
	}
	/* (the 1 kHz tone, and the 15 kHz one folded to 7.05 kHz) */
	printf("%lu %.3f %.3f\n", made_bytes / 2, level(made, (int)(made_bytes / 2), 22050.0, 1000.0),
		level(made, (int)(made_bytes / 2), 22050.0, 7050.0));
	return 0;
}
"""


# ce_vorbis_edges against ce_vorbis_decode, on an Ogg Vorbis file named on
# the command line: the frames each gives, and whether the first and last 48
# frames are the same
EDGES_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples);
int ce_vorbis_edges(const unsigned char *data, int size, int count, int whole, int *channels, int *sample_rate,
	short *head, int *head_frames, short *tail, int *tail_frames);
void ce_vorbis_free(short *samples);
int main(int argc, char **argv)
{
	FILE *file = fopen(argv[1], "rb");
	unsigned char *data;
	long size;
	int channels = 0, rate = 0, edge_channels = 0, edge_rate = 0, frames, edge_frames, head_frames, tail_frames, count;
	short *samples = NULL, head[48 * 2], tail[48 * 2], head_alone[48 * 2], unused[48 * 2];
	int alone_frames, alone_head_frames, alone_tail_frames;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size);
	fread(data, 1, (size_t)size, file);
	fclose(file);
	frames = ce_vorbis_decode(data, (int)size, &channels, &rate, &samples);
	edge_frames = ce_vorbis_edges(data, (int)size, 48, 1, &edge_channels, &edge_rate, head, &head_frames, tail,
		&tail_frames);
	alone_frames = ce_vorbis_edges(data, (int)size, 48, 0, &edge_channels, &edge_rate, head_alone, &alone_head_frames,
		unused, &alone_tail_frames);
	count = frames < 48 ? frames : 48;
	printf("%d %d %d %d %d\n", frames, edge_frames,
		frames > 0 && alone_frames == count && alone_head_frames == count && alone_tail_frames == 0 &&
			!memcmp(head_alone, samples, (size_t)count * channels * sizeof(short)),
		frames > 0 && head_frames == count && !memcmp(head, samples, (size_t)count * channels * sizeof(short)),
		frames > 0 && tail_frames == count &&
			!memcmp(tail, samples + (size_t)(frames - count) * channels, (size_t)count * channels * sizeof(short)));
	ce_vorbis_free(samples);
	free(data);
	return 0;
}
"""

# synthetic test streams (tools/tests/vorbis: noise and a tone, encoded for
# these tests): 44.1 and 22.05 kHz, and one shorter than the edges
VORBIS_FILES = sorted((ROOT / "tools/tests/vorbis").glob("*.ogg"))


def ogg_crc(page):
    crc = 0
    for byte in page:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7 if crc & 0x80000000 else crc << 1) & 0xFFFFFFFF
    return crc


def last_granule_changed(data, delta):
    """the stream with its last page's granule position (its length) moved
    by delta, its checksum made again"""
    at = data.rindex(b"OggS")
    page = bytearray(data[at:])
    granule = max(0, int.from_bytes(page[6:14], "little") + delta)
    page[6:14] = granule.to_bytes(8, "little")
    page[22:26] = b"\0\0\0\0"
    page[22:26] = ogg_crc(page).to_bytes(4, "little")
    return data[:at] + bytes(page)


def compiler():
    for name in (os.environ.get("CC"), "clang", "cc", "gcc"):
        if name and shutil.which(name):
            return name
    return None


_BINARIES = {}


def build(name, source):
    if name not in _BINARIES:
        cc = compiler()
        if not cc:
            pytest.skip("no C compiler")
        folder = Path(tempfile.mkdtemp(prefix="ce_vorbis_resample_"))
        (folder / f"{name}.c").write_text(source, encoding="utf-8")
        binary = folder / name
        result = subprocess.run([cc, "-O2", "-DHALO_CUSTOM_EDITION", "-DHALO_MCC=0", "-I", str(ROOT / "port/third_party/stb"),
                                 str(ROOT / "port/linux/game/content_vorbis.c"), str(folder / f"{name}.c"),
                                 "-o", str(binary), "-lm"], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"{name} does not compile:\n{result.stderr}")
        _BINARIES[name] = binary
    return _BINARIES[name]


def run(cases):
    text = "".join(f"{mode} {a} {b}\n" for mode, a, b in cases)
    out = subprocess.run([str(build("harness", HARNESS))], input=text, capture_output=True, text=True,
                         check=True).stdout
    return [(int(a), float(b), float(c)) for a, b, c in (line.split() for line in out.splitlines())]


def one(mode, a, b=0):
    return run([(mode, a, b)])[0]


def samples_make_source():
    """samples_make's text from mod_sounds.c, by its braces"""
    text = (ROOT / "port/linux/game/mod_sounds.c").read_text(encoding="utf-8")
    start = text.index("static byte *samples_make(")
    depth, index = 0, text.index("{", start)
    while True:
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        index += 1
        if depth == 0:
            return text[start:index]


def test_halving_keeps_the_frames():
    assert one("halve", 1000, 1000)[0] == 22050


@pytest.mark.parametrize("frequency", [100, 1000, 4000, 8000, 9000, 10000])
def test_halving_keeps_the_pass_band(frequency):
    _, db, silent = one("halve", frequency, frequency)
    assert abs(db) < 0.1, f"{frequency} Hz is {db:.2f} dB after halving"
    assert silent < -100


@pytest.mark.parametrize("frequency", [12500, 14000, 16000, 18000, 20000])
def test_halving_drops_what_would_fold_back(frequency):
    # (a tone above 11.025 kHz folds to 22.05 kHz less it)
    _, db, _ = one("halve", frequency, 22050 - frequency)
    assert db < -60, f"{frequency} Hz folds back to {22050 - frequency} Hz at {db:.1f} dB"


@pytest.mark.parametrize("frequency", [1000, 5000, 9000])
def test_doubling_keeps_the_tone_and_adds_no_image(frequency):
    frames, db, _ = one("double", frequency, frequency)
    _, image, _ = one("double", frequency, 22050 - frequency)
    assert frames == 44100
    assert abs(db) < 0.1
    assert image < -60, f"{frequency} Hz doubled has an image at {22050 - frequency} Hz of {image:.1f} dB"


# full-band noise (0) and noise mostly under 8 kHz (-4000: four poles at
# 4 kHz); an even split, as a halved piece keeps the whole's phase only from
# an even frame (an odd piece loses its last frame, as it did before)
@pytest.mark.parametrize("kind", [0, -4000])
@pytest.mark.parametrize("split", [2000, 4000, 7990])
def test_pieces_halved_with_their_neighbors_join_as_the_whole(kind, split):
    same_length, _, largest = one("join", kind, split)
    assert same_length
    assert largest <= 1, f"halved in pieces, {largest} away from the whole at the join"


@pytest.mark.parametrize("kind", [0, -4000])
@pytest.mark.parametrize("split", [1000, 1001, 3999])
def test_pieces_doubled_with_their_neighbors_join_as_the_whole(kind, split):
    same_length, _, largest = one("join2", kind, split)
    assert same_length
    assert largest <= 1, f"doubled in pieces, {largest} away from the whole at the join"


# where the piece after is not known (a looped sound of several
# permutations picks it as it plays), the filter near the join is a shorter
# one that reaches only real samples: under 8 kHz the sound is the whole's
# within a few per cent; above it, what the long filter takes out (and
# would fold back) is left in for about a millisecond, but nothing that
# wasn't in the sound is put in
LIMITS_ALONE = {("join-alone", 0): 0.20, ("join-alone", -4000): 0.02,
                ("join2-alone", 0): 0.20, ("join2-alone", -4000): 0.04}


@pytest.mark.parametrize("mode", ["join-alone", "join2-alone"])
@pytest.mark.parametrize("kind", [0, -4000])
def test_pieces_alone_join_without_made_up_samples(mode, kind):
    _, difference, _ = one(mode, kind, 4000)
    assert difference < LIMITS_ALONE[mode, kind], f"{mode}: {difference:.1%} of the signal near the join"


@pytest.mark.parametrize("mode", ["silence", "silence2"])
@pytest.mark.parametrize("kind", [0, -4000, 1000])
def test_a_sound_from_and_into_silence_is_as_played_in_silence(mode, kind):
    count, _, largest = one(mode, kind)
    assert count == (2000 if mode == "silence" else 8000)
    assert largest <= 1, f"{mode}: {largest} away from the sound in silence"


@pytest.mark.parametrize("kind", [0, -4000, 441])
@pytest.mark.parametrize("frames", [4410, 300, 96])
def test_a_loop_with_its_own_ends_wraps_seamlessly(kind, frames):
    # (441 Hz: a whole number of cycles in 4410 and 300 frames; it need not
    # be, the loop is the loop)
    count, _, largest = one("loop", kind, frames)
    assert count == frames // 2
    assert largest <= 1, f"a loop of {frames} frames is {largest} away at its wraps"


@pytest.mark.parametrize("frames", [2, 3, 10, 95, 96, 97, 1000])
def test_a_steady_level_stays_steady_to_the_ends(frames):
    halved_frames, worst, _ = one("steady", frames)
    assert halved_frames == frames // 2
    assert worst <= 1
    doubled_frames, worst, _ = one("steady2", frames)
    assert doubled_frames == frames * 2
    assert worst <= 1


# a tone alone, with no neighbors at all (the mods folder's sounds): at its
# ends the shorter filters (down to the sample itself halving, and a
# straight line between two samples doubling, the last held) keep a tone's
# shape; a 5 kHz one is softened in the first and last frames, as the old
# straight line softened it everywhere
LIMITS_EDGE = {100: 0.01, 1000: 0.01, 5000: 0.25}


@pytest.mark.parametrize("mode", ["edge", "edge2"])
@pytest.mark.parametrize("frequency", [100, 1000, 5000])
def test_a_tone_keeps_its_shape_to_the_ends(mode, frequency):
    # (its first and last 30 frames, against the tone's amplitude)
    _, worst, _ = one(mode, frequency, 30)
    assert worst < LIMITS_EDGE[frequency], f"{mode} {frequency} Hz: {worst:.1%} off near the ends"


def test_mods_folder_pcm_is_halved_through_the_filter():
    binary = build("mod_sounds", MOD_HARNESS.replace("@SAMPLES_MAKE@", samples_make_source()))
    out = subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout.split()
    frames, tone, folded = int(out[0]), float(out[1]), float(out[2])
    assert frames == 22050, out
    assert abs(tone) < 0.1
    assert folded < -60, f"15 kHz folds back to 7.05 kHz at {folded:.1f} dB"


def edges_of(data, tmp_path, name):
    path = tmp_path / name
    path.write_bytes(data)
    binary = build("edges", EDGES_HARNESS)
    out = subprocess.run([str(binary), str(path)], capture_output=True, text=True, check=True).stdout.split()
    return [int(value) for value in out]


@pytest.mark.parametrize("path", VORBIS_FILES, ids=lambda path: path.name)
@pytest.mark.parametrize("delta", [0, 4096, -100, 1 << 30])
def test_edges_read_alone_are_the_full_decodes(path, delta, tmp_path):
    # (ce_resources.c reads a neighbor's head alone, its first frames, and
    # a tail by decoding a frame at a time, keeping only the last; a stream
    # whose last page gives another length is decoded the same)
    data = path.read_bytes()
    if delta:
        data = last_granule_changed(data, delta)
    frames, edge_frames, head_alone_same, head_same, tail_same = edges_of(data, tmp_path, path.name)
    assert edge_frames == frames
    if delta == 0:
        assert frames > 0
    if frames > 0:
        assert head_alone_same and head_same and tail_same


if __name__ == "__main__":
    for frequency in (1000, 4000, 8000, 9000, 9500, 10000, 10500, 11500, 12500, 14000, 16000, 18000, 20000):
        measured = frequency if frequency < 11025 else 22050 - frequency
        print(f"halve {frequency:6d} Hz -> {measured:6d} Hz: {one('halve', frequency, measured)[1]:7.2f} dB")
    for mode in ("join", "join-alone", "join2", "join2-alone"):
        for kind, name in ((0, "full band"), (-4000, "under 8 kHz")):
            _, difference, largest = one(mode, kind, 4000)
            print(f"{mode:12s} {name:12s}: {difference:7.3%} of the signal near the join, largest step {largest:.0f}")
    for mode in ("silence", "silence2"):
        _, difference, largest = one(mode, 0)
        print(f"{mode:12s} full band   : {difference:7.3%} from the sound in silence, largest step {largest:.0f}")
    for mode in ("loop", "loop-alone"):
        for kind, name in ((0, "full band"), (-4000, "under 8 kHz")):
            _, difference, largest = one(mode, kind, 4410)
            print(f"{mode:12s} {name:12s}: {difference:7.3%} at the wraps, largest step {largest:.0f}")
    sys.exit(0)


# a long permutation decoded a piece at a time (ce_vorbis_stream_*, which
# ce_resources.c writes out piece by piece): the same frames as the whole
# decode, whatever the piece's length (one shorter than a Vorbis packet,
# one longer than the stream)
STREAM_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples);
void *ce_vorbis_stream_open(const unsigned char *data, int size, int *channels, int *sample_rate);
int ce_vorbis_stream_read(void *stream, short *buffer, int frames);
void ce_vorbis_stream_close(void *stream);
void ce_vorbis_free(short *samples);
int main(int argc, char **argv)
{
	FILE *file = fopen(argv[1], "rb");
	int piece = atoi(argv[2]);
	unsigned char *data;
	long size;
	int channels = 0, rate = 0, stream_channels = 0, stream_rate = 0, frames, read, total = 0, same = 1;
	short *samples = NULL, *buffer;
	void *stream;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size);
	fread(data, 1, (size_t)size, file);
	fclose(file);
	frames = ce_vorbis_decode(data, (int)size, &channels, &rate, &samples);
	stream = ce_vorbis_stream_open(data, (int)size, &stream_channels, &stream_rate);
	buffer = malloc((size_t)piece * 2 * sizeof(short));
	while (stream && (read = ce_vorbis_stream_read(stream, buffer, piece)) > 0)
	{
		if (total + read > frames || memcmp(buffer, samples + (size_t)total * channels, (size_t)read * channels * sizeof(short)))
			same = 0;
		total += read;
	}
	printf("%d %d %d %d\n", frames, total, same, stream_channels == channels && stream_rate == rate);
	ce_vorbis_stream_close(stream);
	ce_vorbis_free(samples);
	free(buffer);
	free(data);
	return 0;
}
"""


@pytest.mark.parametrize("path", VORBIS_FILES, ids=lambda path: path.name)
@pytest.mark.parametrize("piece", [7, 4096, 65536])
def test_a_stream_decoded_in_pieces_is_the_whole(path, piece):
    out = subprocess.run([str(build("stream", STREAM_HARNESS)), str(path), str(piece)], capture_output=True,
                         text=True, check=True).stdout.split()
    frames, total, same, format_same = map(int, out)
    assert frames > 0
    assert total == frames
    assert same and format_same

