/*
CONTENT_VORBIS.C

The game's one stb_vorbis (port/third_party/stb), and the two decoders over it:
Halo PC's Ogg Vorbis sounds decoded to 16-bit PCM for Custom Edition maps
(ce_resources.c, mod_sounds.c: ce_vorbis_*), and MCC's (mcc_audio.c:
mcc_vorbis_decode, mcc_vorbis.h), which checks every page first and lets an
empty residue book add nothing. Each keeps its own rules; only the decoder
under them is shared.
*/

#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
/* (Custom Edition maps' sounds, and Halo MCC maps' (halo_port_features.h,
above): a build with neither builds this to nothing) */
#if defined(HALO_CUSTOM_EDITION) || HALO_MCC

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "content_bytes.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
/* (port/third_party/stb, a game include directory: port/linux/port.json) */
#include "stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif /* HALO_CUSTOM_EDITION || HALO_MCC */

/* ---------- Custom Edition's (ce_vorbis_*) */

#ifdef HALO_CUSTOM_EDITION

/* the most samples per channel a sound's permutation is decoded to (as
ce_resources.c keeps no more), and the most channels (the game's sounds are
mono or stereo) */
#define CE_VORBIS_MAXIMUM_FRAMES 0x1000000
#define CE_VORBIS_MAXIMUM_CHANNELS 2

/* an Ogg Vorbis stream's samples, interleaved 16-bit (malloc'd: free them),
their channels and rate; the count of samples per channel, or -1. As
stb_vorbis_decode_memory, but a stream of more channels than a sound has, or
longer than one is kept, is refused as it is found to be, not decoded whole
first (a map's sounds are anyone's) */
int ce_vorbis_decode(const unsigned char *data, int size, int *channels, int *sample_rate, short **samples)
{
	int error = 0;
	stb_vorbis *vorbis = stb_vorbis_open_memory(data, size, &error, NULL);
	short *buffer;
	int frames = 0;
	int capacity, used = 0;

	*samples = NULL;
	if (!vorbis)
		return -1;
	*channels = vorbis->channels;
	*sample_rate = (int)vorbis->sample_rate;
	if (vorbis->channels < 1 || vorbis->channels > CE_VORBIS_MAXIMUM_CHANNELS)
	{
		stb_vorbis_close(vorbis);
		return -1;
	}
	capacity = vorbis->channels * 4096;
	buffer = malloc((size_t)capacity * sizeof(*buffer));
	while (buffer)
	{
		int count = stb_vorbis_get_frame_short_interleaved(vorbis, vorbis->channels, buffer + used, capacity - used);

		if (!count)
		{
			*samples = buffer;
			stb_vorbis_close(vorbis);
			return frames;
		}
		frames += count;
		used += count * vorbis->channels;
		if (frames > CE_VORBIS_MAXIMUM_FRAMES)
			break;
		if (used + vorbis->channels * 4096 > capacity)
		{
			short *larger = realloc(buffer, (size_t)capacity * 2 * sizeof(*buffer));

			if (!larger)
				break;
			buffer = larger;
			capacity *= 2;
		}
	}
	if (buffer)
		free(buffer);
	stb_vorbis_close(vorbis);
	return -1;
}

/* the first `count` frames (or all, if fewer) of what ce_vorbis_decode
gives of a stream, the same samples, and with `whole` its last `count`
too, without keeping the rest: decoded a frame at a time from the start, as
ce_vorbis_decode decodes it, and without whole only as far as the head.
(Not by a seek to the end: stb_vorbis's seek trusts the stream's pages,
which a map's sounds may be anything.) With whole, the count of frames
ce_vorbis_decode gives; else the head's; or -1 */
int ce_vorbis_edges(const unsigned char *data, int size, int count, int whole, int *channels, int *sample_rate,
	short *head, int *head_frames, short *tail, int *tail_frames)
{
	int error = 0;
	stb_vorbis *vorbis = stb_vorbis_open_memory(data, size, &error, NULL);
	short *buffer;
	int total = 0, kept = 0, channel_count;

	*head_frames = *tail_frames = 0;
	if (!vorbis)
		return -1;
	*channels = channel_count = vorbis->channels;
	*sample_rate = (int)vorbis->sample_rate;
	if (count <= 0 || channel_count < 1 || channel_count > CE_VORBIS_MAXIMUM_CHANNELS)
	{
		stb_vorbis_close(vorbis);
		return -1;
	}
	/* (a frame is at most 4096 samples a channel) */
	buffer = malloc(4096 * CE_VORBIS_MAXIMUM_CHANNELS * sizeof(*buffer));
	while (buffer)
	{
		int read = stb_vorbis_get_frame_short_interleaved(vorbis, channel_count, buffer,
			4096 * CE_VORBIS_MAXIMUM_CHANNELS);
		int keep, drop;

		if (!read)
			break;
		if (*head_frames < count)
		{
			int more = read < count - *head_frames ? read : count - *head_frames;

			memcpy(head + *head_frames * channel_count, buffer, (size_t)more * (size_t)channel_count * sizeof(*head));
			*head_frames += more;
			if (!whole && *head_frames == count)
			{
				total = count;
				break;
			}
		}
		total += read;
		if (total > CE_VORBIS_MAXIMUM_FRAMES)
		{
			total = -1;
			break;
		}
		/* (the last `count` frames of what was kept and this) */
		keep = read < count ? read : count;
		drop = kept + keep > count ? kept + keep - count : 0;
		if (drop)
		{
			memmove(tail, tail + drop * channel_count, (size_t)(kept - drop) * (size_t)channel_count * sizeof(*tail));
			kept -= drop;
		}
		memcpy(tail + kept * channel_count, buffer + (read - keep) * channel_count,
			(size_t)keep * (size_t)channel_count * sizeof(*tail));
		kept += keep;
	}
	if (!buffer)
		total = -1;
	free(buffer);
	stb_vorbis_close(vorbis);
	if (total < 0)
	{
		*head_frames = *tail_frames = 0;
		return -1;
	}
	if (!whole)
		return *head_frames;
	*tail_frames = kept;
	return total;
}

/* the half-band low pass a sound is brought to half or twice its rate
through: a Kaiser-windowed sinc cut off at a quarter of the higher rate
(11,025 Hz between 44.1 and 22.05 kHz), flat to about 10 kHz and more than
80 dB down from about 12.5 kHz. Every other tap but the middle one is zero,
so only the odd ones are kept: ce_vorbis_half_band[i] is the tap 2i+1 away
from the middle, on either side; the middle one is a half. (float: the
original Xbox does this at a map's load too, on its x87) */
#define CE_VORBIS_HALF_BAND_TAPS 24
#define CE_VORBIS_HALF_BAND_BETA 7.0

static float ce_vorbis_half_band[CE_VORBIS_HALF_BAND_TAPS];
/* the shorter filters for a sample near an end, where only its first k
taps a side reach real samples: the halfway point of the polynomial through
those 2k samples (Lagrange's), whose ce_vorbis_short_taps[k][i] are the
weights of the two samples 2i+1 away (halving: half of them, and the middle
sample a half). The shortest is the straight line between two samples, and
each is exact for a polynomial of its degree: a low tone keeps its shape */
static float ce_vorbis_short_taps[CE_VORBIS_HALF_BAND_TAPS][CE_VORBIS_HALF_BAND_TAPS];
static int ce_vorbis_half_band_ready;

/* the modified Bessel function I0, by its series */
static double ce_vorbis_bessel_i0(double x)
{
	double sum = 1.0, term = 1.0;
	int k;

	for (k = 1; k < 50; k++)
	{
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
		if (term < sum * 1e-12)
			break;
	}
	return sum;
}

static void ce_vorbis_half_band_make(void)
{
	double const pi = 3.14159265358979323846;
	int const half = 2 * CE_VORBIS_HALF_BAND_TAPS - 1;
	double taps[CE_VORBIS_HALF_BAND_TAPS];
	double total = 0.0;
	int i, k;

	if (ce_vorbis_half_band_ready)
		return;
	for (i = 0; i < CE_VORBIS_HALF_BAND_TAPS; i++)
	{
		int n = 2 * i + 1;
		double ratio = (double)n / (double)(half + 1);
		double window = ce_vorbis_bessel_i0(CE_VORBIS_HALF_BAND_BETA * sqrt(1.0 - ratio * ratio)) /
			ce_vorbis_bessel_i0(CE_VORBIS_HALF_BAND_BETA);

		taps[i] = sin(pi * n / 2.0) / (pi * n) * window;
		total += 2.0 * taps[i];
	}
	/* (a gain of exactly one at 0 Hz: the middle tap's half and the
	others' half) */
	for (i = 0; i < CE_VORBIS_HALF_BAND_TAPS; i++)
		ce_vorbis_half_band[i] = (float)(taps[i] * 0.5 / total);
	for (k = 1; k < CE_VORBIS_HALF_BAND_TAPS; k++)
	{
		for (i = 0; i < k; i++)
		{
			/* (the samples at +-(j + 1/2) from the halfway point) */
			double weight = 1.0, node = i + 0.5;
			int j;

			for (j = 0; j < k; j++)
			{
				double other = j + 0.5;

				if (j != i)
					weight *= (0.0 - other) / (node - other);
				weight *= (0.0 + other) / (node + other);
			}
			ce_vorbis_short_taps[k][i] = (float)weight;
		}
	}
	ce_vorbis_half_band_ready = 1;
}

/* a piece's samples of one channel, with its neighbors' beside it: frame -1
is the last of `before`, frame `frames` the first of `after` (the caller
reads only frames that are there) */
struct ce_vorbis_piece
{
	const short *in;
	int frames;
	const short *before;
	int before_frames;
	const short *after;
	int after_frames;
	int channels;
};

static float ce_vorbis_piece_sample(const struct ce_vorbis_piece *piece, int channel, int frame)
{
	if (frame < 0)
		return piece->before[(piece->before_frames + frame) * piece->channels + channel];
	if (frame >= piece->frames)
		return piece->after[(frame - piece->frames) * piece->channels + channel];
	return piece->in[frame * piece->channels + channel];
}

static short ce_vorbis_clamp(float value)
{
	value += value < 0.0f ? -0.5f : 0.5f;
	if (value > 32767.0f)
		return 32767;
	if (value < -32768.0f)
		return -32768;
	return (short)value;
}

static int ce_vorbis_minimum(int a, int b)
{
	return a < b ? a : b;
}

/* As ce_vorbis_resample, a piece of a longer sound: `before` the last
`before_frames` of the piece played before it, `after` the first
`after_frames` of the one played after it, at its rate and channels (NULL,
0 where there is none; a looping sound's own end and start, to loop
seamlessly; silence for a sound played from or into silence). The filter
reaches 47 frames either side when halving, 24 when doubling. Where it
would reach past what there is, a shorter one that reaches only real
samples is used (ce_vorbis_short_taps): no sample is made up, so pieces
brought to their rate alone still join without a step */
int ce_vorbis_resample_piece(short **samples, int frames, int channels, int rate, int wanted,
	const short *before, int before_frames, const short *after, int after_frames)
{
	struct ce_vorbis_piece piece;
	short *in = *samples;
	int frame, channel, tap;

	if (frames <= 0 || channels < 1 || channels > CE_VORBIS_MAXIMUM_CHANNELS)
		return -1;
	if (rate == wanted)
		return frames;
	ce_vorbis_half_band_make();
	piece.frames = frames;
	piece.channels = channels;
	piece.before = before;
	piece.before_frames = before ? before_frames : 0;
	piece.after = after;
	piece.after_frames = after ? after_frames : 0;
	if (rate == wanted * 2)
	{
		int count = frames / 2;
		short *copy;

		if (count <= 0)
			return -1;
		/* (written in place, from a copy) */
		copy = malloc((size_t)frames * (size_t)channels * sizeof(*copy));
		if (!copy)
			return -1;
		memcpy(copy, in, (size_t)frames * (size_t)channels * sizeof(*copy));
		piece.in = copy;
		for (frame = 0; frame < count; frame++)
		{
			int middle = 2 * frame;
			int reach = ce_vorbis_minimum(middle + piece.before_frames, frames - 1 - middle + piece.after_frames);
			int taps = ce_vorbis_minimum(CE_VORBIS_HALF_BAND_TAPS, (reach + 1) / 2);

			for (channel = 0; channel < channels; channel++)
			{
				float sum = 0.5f * copy[middle * channels + channel];

				if (taps == CE_VORBIS_HALF_BAND_TAPS && middle >= 2 * CE_VORBIS_HALF_BAND_TAPS &&
					middle + 2 * CE_VORBIS_HALF_BAND_TAPS < frames)
				{
					/* (away from the ends, read directly) */
					const short *at = copy + middle * channels + channel;

					for (tap = 0; tap < CE_VORBIS_HALF_BAND_TAPS; tap++)
					{
						int offset = (2 * tap + 1) * channels;

						sum += ce_vorbis_half_band[tap] * (float)(at[-offset] + at[offset]);
					}
				}
				else
				{
					float const *weights = taps == CE_VORBIS_HALF_BAND_TAPS ? NULL : ce_vorbis_short_taps[taps];

					for (tap = 0; tap < taps; tap++)
					{
						int offset = 2 * tap + 1;

						sum += (weights ? 0.5f * weights[tap] : ce_vorbis_half_band[tap]) *
							(ce_vorbis_piece_sample(&piece, channel, middle - offset) +
							ce_vorbis_piece_sample(&piece, channel, middle + offset));
					}
					if (!taps)
						sum *= 2.0f;
				}
				in[frame * channels + channel] = ce_vorbis_clamp(sum);
			}
		}
		free(copy);
		return count;
	}
	if (rate * 2 == wanted && frames <= CE_VORBIS_MAXIMUM_FRAMES / 2)
	{
		short *out = malloc((size_t)frames * 2 * (size_t)channels * sizeof(*out));

		if (!out)
			return -1;
		piece.in = in;
		for (frame = 0; frame < frames; frame++)
		{
			/* (between this sample and the next: the odd taps, doubled for
			the zeros put between the samples) */
			int reach = ce_vorbis_minimum(frame + piece.before_frames,
				frames - 2 - frame + piece.after_frames);
			int taps = ce_vorbis_minimum(CE_VORBIS_HALF_BAND_TAPS, reach + 1);

			for (channel = 0; channel < channels; channel++)
			{
				float sum = 0.0f;

				if (taps == CE_VORBIS_HALF_BAND_TAPS && frame >= CE_VORBIS_HALF_BAND_TAPS &&
					frame + CE_VORBIS_HALF_BAND_TAPS < frames)
				{
					const short *at = in + frame * channels + channel;

					for (tap = 0; tap < CE_VORBIS_HALF_BAND_TAPS; tap++)
						sum += 2.0f * ce_vorbis_half_band[tap] * (float)(at[-tap * channels] + at[(tap + 1) * channels]);
				}
				else if (taps <= 0)
				{
					/* (the last sample, with nothing after it) */
					sum = in[frame * channels + channel];
				}
				else
				{
					float const *weights = taps == CE_VORBIS_HALF_BAND_TAPS ? NULL : ce_vorbis_short_taps[taps];

					for (tap = 0; tap < taps; tap++)
					{
						sum += (weights ? weights[tap] : 2.0f * ce_vorbis_half_band[tap]) *
							(ce_vorbis_piece_sample(&piece, channel, frame - tap) +
							ce_vorbis_piece_sample(&piece, channel, frame + 1 + tap));
					}
				}
				out[(2 * frame) * channels + channel] = in[frame * channels + channel];
				out[(2 * frame + 1) * channels + channel] = ce_vorbis_clamp(sum);
			}
		}
		free(in);
		*samples = out;
		return frames * 2;
	}
	return -1;
}

/* samples ce_vorbis_decode gave at twice the rate wanted, or half it (a
44.1 kHz mono sound, which the game plays at 22.05 kHz as the Xbox's
channels do, or 44.1 kHz Ogg Vorbis in a 22 kHz sound, which some tools
built: battlecraft_hd's), brought to that rate so they play at their pitch,
through the half-band low pass: halved (each second sample of the filtered
sound kept), or doubled (a filtered sample between each two). A sound
alone: ce_vorbis_resample_piece has its neighbors. The new count of samples
per channel, or -1 (the samples are then as they were) */
int ce_vorbis_resample(short **samples, int frames, int channels, int rate, int wanted)
{
	return ce_vorbis_resample_piece(samples, frames, channels, rate, wanted, NULL, 0, NULL, 0);
}

/* an Ogg Vorbis stream opened to be decoded a piece at a time (a long one,
which ce_resources.c writes out as it goes rather than holding it whole):
its channels and rate; NULL if it cannot be, or has more channels than a
sound has. data stays the caller's until ce_vorbis_stream_close */
void *ce_vorbis_stream_open(const unsigned char *data, int size, int *channels, int *sample_rate)
{
	int error = 0;
	stb_vorbis *vorbis = stb_vorbis_open_memory(data, size, &error, NULL);

	*channels = *sample_rate = 0;
	if (!vorbis)
		return NULL;
	*channels = vorbis->channels;
	*sample_rate = (int)vorbis->sample_rate;
	if (vorbis->channels < 1 || vorbis->channels > CE_VORBIS_MAXIMUM_CHANNELS)
	{
		stb_vorbis_close(vorbis);
		return NULL;
	}
	return vorbis;
}

/* the next `frames` frames of a stream (fewer at its end), interleaved
16-bit, into buffer (room for frames of the stream's channels): how many,
0 at the end */
int ce_vorbis_stream_read(void *stream, short *buffer, int frames)
{
	stb_vorbis *vorbis = stream;

	if (!vorbis || frames <= 0)
		return 0;
	return stb_vorbis_get_samples_short_interleaved(vorbis, vorbis->channels, buffer, frames * vorbis->channels);
}

void ce_vorbis_stream_close(void *stream)
{
	if (stream)
		stb_vorbis_close(stream);
}

/* room for frames of channels' samples, as ce_vorbis_resample_piece takes
them (it may free them and give others): ce_vorbis_free them */
short *ce_vorbis_samples_new(int frames, int channels)
{
	if (frames <= 0 || channels < 1 || channels > CE_VORBIS_MAXIMUM_CHANNELS)
		return NULL;
	return malloc((size_t)frames * (size_t)channels * sizeof(short));
}

/* the samples ce_vorbis_decode gave (freed as stb_vorbis allocated them: the
game's units free through its own allocator, which takes no NULL: debug_free) */
void ce_vorbis_free(short *samples)
{
	if (samples)
		free(samples);
}

#endif /* HALO_CUSTOM_EDITION */

/* ---------- MCC's (mcc_vorbis.h): the pages checked first, and empty residue
books that add nothing. Empty residue vector books add nothing in Xiph's
reference decoder; stb treats an attempted read from one as an error and
abandons the remaining residue, so only those vector-book references are
normalized, before any audio packet is decoded. Reference:
https://github.com/xiph/vorbis/blob/v1.3.7/lib/codebook.c
(vorbis_book_decodevs_add, vorbis_book_decodev_add, vorbis_book_decodevv_add). */

#if HALO_MCC
#include "mcc_vorbis.h"
#include "mcc_loading.h"
#include "mcc_host_memory.h"

/* The pull decoder does not verify page checksums. Validate the complete bounded
 * permutation first, including EOS, so a truncated/damaged packet is not made
 * acceptable by the narrow empty-book compatibility rule. */
static int mcc_ogg_validate(unsigned char const *bytes, uint32_t size, uint32_t *frames,
    uint32_t *payload_end, uint32_t *final_page)
{
    uint32_t table[256], offset=0, serial=0, sequence=0, i, j;
    uint32_t last_page=0, last_end=0, previous_granule=0;
    int continued=0, ended=0;
    *payload_end=size;*final_page=0;
    for (i=0;i<256;i++) {
        uint32_t crc=i<<24;
        for (j=0;j<8;j++) crc=(crc<<1)^((crc&0x80000000u)?0x04C11DB7u:0);
        table[i]=crc;
    }
    while (offset<size) {
        unsigned char const *page=bytes+offset;
        uint32_t length=27, count, crc=0;
        mcc_loading_pulse();
        if (ended || size-offset<27 || memcmp(page,"OggS",4) || page[4] || (page[5]&~7)) return 0;
        count=page[26];
        if (count>size-offset-length) return 0;
        length+=count;
        for (i=0;i<count;i++) length+=page[27+i];
        if (length>size-offset || (count && !!(page[5]&1)!=continued)) return 0;
        if (!offset) {
            if (!(page[5]&2)) return 0;
            serial=content_u32le(page+14);sequence=content_u32le(page+18);
        } else if ((page[5]&2) || content_u32le(page+14)!=serial || content_u32le(page+18)!=sequence) return 0;
        for (i=0;i<length;i++) {
            unsigned char value=i>=22 && i<26 ? 0 : page[i];
            crc=(crc<<8)^table[(crc>>24)^value];
        }
        if (crc!=content_u32le(page+22)) return 0;
        /* A page without a completed packet has no granule. All other known
         * positions must progress; EOS cannot trim an earlier page's audio. */
        if (content_u32le(page+6)!=0xFFFFFFFFu || content_u32le(page+10)!=0xFFFFFFFFu) {
            if (content_u32le(page+10) || content_u32le(page+6)<previous_granule) return 0;
            previous_granule=content_u32le(page+6);
        }
        /* Ogg permits zero-segment pages (including a separate EOS page). With
         * no lacing entries no packet begins or ends, so retain pending state.
         * https://xiph.org/ogg/doc/framing.html#page_segments */
        if (count) {
            continued=page[26+count]==255;
            last_page=offset;last_end=offset+length;
        }
        ended=(page[5]&4)!=0;
        if (ended) {
            if (continued || content_u32le(page+10)) return 0;
            *frames=content_u32le(page+6);
            if (!count) {
                if (!last_end) return 0;
                *payload_end=last_end;*final_page=last_page;
            }
        }
        offset+=length;sequence++;
    }
    return ended;
}

/* stb's page reader uses a stale lacing entry for an empty terminal page. Give
 * only this case a private equivalent framing: finish the last complete packet
 * page with the already-validated EOS granule. This also preserves final-block
 * trimming. Source bytes and the shared Xbox/CE decoder remain untouched. */
static unsigned char *mcc_ogg_terminal_page(unsigned char const *bytes,
    uint32_t size, uint32_t offset, uint32_t frames)
{
    unsigned char *copy=malloc(size), *page;
    uint32_t i,j,crc=0;
    if (!copy) return NULL;
    memcpy(copy,bytes,size);page=copy+offset;
    page[5]|=4;
    for (i=0;i<4;i++) page[6+i]=(unsigned char)(frames>>(8*i));
    memset(page+10,0,4);memset(page+22,0,4);
    for (i=0;i<size-offset;i++) {
        crc^=(uint32_t)page[i]<<24;
        for (j=0;j<8;j++) crc=(crc<<1)^((crc&0x80000000u)?0x04C11DB7u:0);
    }
    for (i=0;i<4;i++) page[22+i]=(unsigned char)(crc>>(8*i));
    return copy;
}

static int mcc_vorbis_empty_book(Codebook const *book)
{
    int i;
    if (book->sparse) return !book->sorted_entries;
    /* stb expands very small sparse books, including an empty 1-3 entry book. */
    for (i=0;i<book->entries;i++) if (book->codeword_lengths[i]!=NO_CODE) return 0;
    return 1;
}

static int mcc_vorbis_residues(vorb *stream)
{
    int index, classification, pass;
    /* A floor book supplies scalar values, never an additive vector. Keep that
     * distinct from the zero-contribution residue case. */
    for (index=0;index<stream->floor_count;index++) {
        Floor1 const *floor=&stream->floor_config[index].floor1;
        int partition;
        if (stream->floor_types[index]!=1) return 0;
        for (partition=0;partition<floor->partitions;partition++) {
            int kind=floor->partition_class_list[partition];
            int subclasses=floor->class_subclasses[kind];
            if (subclasses && mcc_vorbis_empty_book(stream->codebooks+floor->class_masterbooks[kind])) return 0;
            for (classification=0;classification<(1<<subclasses);classification++) {
                int number=floor->subclass_books[kind][classification];
                if (number>=0 && mcc_vorbis_empty_book(stream->codebooks+number)) return 0;
            }
        }
    }
    for (index=0;index<stream->residue_count;index++) {
        Residue *residue=stream->residue_config+index;
        /* Empty scalar classbooks cannot supply classifications. This is not the
         * empty additive-vector case, and must not be silently accepted. */
        if (mcc_vorbis_empty_book(stream->codebooks+residue->classbook)) return 0;
        for (classification=0;classification<residue->classifications;classification++) {
            for (pass=0;pass<8;pass++) {
                int number=residue->residue_books[classification][pass];
                if (number>=0) {
                    Codebook const *book=stream->codebooks+number;
                    if (!book->lookup_type) return 0;
                    if (mcc_vorbis_empty_book(book)) residue->residue_books[classification][pass]=-1;
                }
            }
        }
    }
    return 1;
}

int mcc_vorbis_decode(unsigned char const *bytes, uint32_t size, uint32_t frame_limit,
    short **samples, uint32_t *frames, uint32_t *rate, int *channels)
{
    vorb stream;
    uint32_t expected=0, count=0, payload_end=0, final_page=0;
    unsigned char *normalized=NULL;
    short *pcm=NULL;
    short packet[8192 * 2];
    uint32_t decoded=0;
    int ok=0, boundary_known=0, boundary_bias=0;
    *samples=NULL;*frames=0;*rate=0;*channels=0;
    if (!bytes || !size || size>INT_MAX || !mcc_ogg_validate(bytes,size,&expected,&payload_end,&final_page) ||
        !expected || expected>frame_limit || expected>INT_MAX/(2*sizeof(short))) return 0;
    if (payload_end<size) {
        normalized=mcc_ogg_terminal_page(bytes,payload_end,final_page,expected);
        if (!normalized) return 0;
        bytes=normalized;size=payload_end;
    }
    vorbis_init(&stream,NULL);
    stream.stream=(uint8 *)bytes;
    stream.stream_start=stream.stream;
    stream.stream_end=stream.stream+size;
    stream.stream_len=size;
    if (!start_decoder(&stream) || !mcc_vorbis_residues(&stream) ||
        stream.channels<1 || stream.channels>2 || stream.sample_rate<8000 || stream.sample_rate>192000) goto done;
    pcm=malloc((size_t)expected*stream.channels*sizeof(short));
    if (!pcm) goto done;
    /* Normalization precedes the discarded overlap packet, which can itself use
     * an empty residue vector book. No Xbox/CE decoder state is accessible here. */
    if (!vorbis_pump_first_frame(&stream) || stream.error) goto done;
    for (;;) {
        int got=stb_vorbis_get_frame_short_interleaved(&stream,stream.channels,packet,8192*2);
        uint32_t retain;
        mcc_loading_pulse();
        if (stream.error) goto done;
        if (!got) break;
        decoded+=(uint32_t)got;
        if (!(stream.page_flag&PAGEFLAG_last_page) && stream.last_seg_which==stream.end_seg_with_known_loc) {
            int difference=(int)decoded-(int)stream.known_loc_for_packet;
            /* A long block followed by a short block emits up to its right
             * window's start, beyond the packet's centre/granule. This offset
             * disappears again after the short packet. Accept only that exact
             * window transition, not arbitrary drifting page positions. */
            if (difference!=0 && difference!=(stream.blocksize_1-stream.blocksize_0)/4) goto done;
            boundary_bias=difference;boundary_known=1;
        }
        retain=(uint32_t)got;
        if (retain>expected-count) {
            /* Vorbis I, appendix A.2. A final-packet trim plus the validated
             * window-boundary offset is bounded by one maximum block. Never
             * drain an unbounded remainder or suppress packet errors. */
            if (decoded-expected>(uint32_t)stream.blocksize_1 ||
                (!(stream.page_flag&PAGEFLAG_last_page) &&
                 (!boundary_known || decoded-expected>(uint32_t)boundary_bias))) goto done;
            retain=expected-count;
        }
        memcpy(pcm+count*stream.channels,packet,(size_t)retain*stream.channels*sizeof(short));
        count+=retain;
    }
    if (count!=expected) goto done;
    *samples=pcm;*frames=count;*rate=stream.sample_rate;*channels=stream.channels;
    pcm=NULL;ok=1;
done:
    free(pcm);
    vorbis_deinit(&stream);
    free(normalized);
    return ok;
}

void mcc_vorbis_free(short *samples)
{
    free(samples);
}
#endif /* HALO_MCC */
