/*
H2_SOUND_CODEC.H

Halo 2 (MCC)'s sound effects made Xbox ADPCM, the format the game's sound
cache, its channels and the mixer play (h2_sound_codec.c): the chunks of a
sound permutation in sounds_neutral.dat - Opus packets (libopus, BSD
3-Clause: port/third_party/opus), WMA (FFmpeg's decoder, LGPL-2.1-or-later:
port/third_party/ffmpeg-wma) or the original Xbox ADPCM - decoded,
mixed to mono (or kept stereo), taken to the rate asked for with a
windowed-sinc converter and encoded (xbox_adpcm_encoder.h). An Xbox ADPCM
stream already at the channels and rate asked for is copied as it is.
*/

#ifndef __HALO_LINUX_H2_SOUND_CODEC_H
#define __HALO_LINUX_H2_SOUND_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "xbox_adpcm_encoder.h"

/* ---------- constants */

enum
{
	H2_SOUND_CODEC_KIND_OPUS = 1,
	H2_SOUND_CODEC_KIND_XBOX_ADPCM = 2,
	/* (the main menu music: each chunk an AM_MEDIA_TYPE, its WAVEFORMATEX
	(WMA version 1 or 2) and the packets - port/third_party/ffmpeg-wma) */
	H2_SOUND_CODEC_KIND_WMA = 3,
	/* (MCC's Halo CE banks' dialogue, music and streams: each chunk FMOD's
	CELT frames, 44100 Hz - fsb_celt.h; a link of a chain from the stream's
	first_frame, as WMA's) */
	H2_SOUND_CODEC_KIND_FSB_CELT = 4,

	/* the chunks a stream may have */
	H2_SOUND_CODEC_MAXIMUM_CHUNKS = 256,

	/* the working memory the transcoding needs (h2_sound_codec_work_bytes
	is this at most), aligned to 16 bytes */
	H2_SOUND_CODEC_WORK_BYTES = 160 * 1024
};

enum h2_sound_codec_status
{
	_h2_sound_codec_ok,
	/* a chunk could not be read */
	_h2_sound_codec_read_failed,
	/* not what the tag says, or damaged */
	_h2_sound_codec_bad_stream,
	/* a format or rate this port does not take */
	_h2_sound_codec_unsupported,
	/* the arguments are not usable (the working memory too small) */
	_h2_sound_codec_bad_arguments,
	NUMBER_OF_H2_SOUND_CODEC_STATUSES
};

/* ---------- structures */

struct h2_sound_codec_chunk
{
	uint32_t offset;
	uint32_t bytes;
};

/* a permutation's stream: its chunks, which the caller reads (a window of
sounds_neutral.dat) */
struct h2_sound_codec_stream
{
	int kind;
	/* the stream's channels (1 or 2) and rate (Hz), and its length in frames
	at that rate */
	int channels;
	long rate;
	uint32_t frames;
	uint32_t chunk_count;
	struct h2_sound_codec_chunk const *chunks;
	/* (a link of a long WMA or CELT stream's chain, h2_sound.c, custom_edition_sounds.c: the stream's frame
	the output starts at - `frames` then the link's own; 0 the stream's
	start; another kind takes 0 only) */
	uint32_t first_frame;
};

/* reads `size` bytes at `offset` of the dat into `buffer`; nonzero on
success */
typedef int (*h2_sound_codec_read_proc)(void *context, uint32_t offset, uint32_t size, void *buffer);

struct h2_sound_codec_result
{
	enum h2_sound_codec_status status;
	/* the frames made (at the output rate) and the silence after them */
	uint32_t frames_decoded;
	uint32_t frames_padded;
	/* the frames the stream gave (at its rate) */
	uint32_t source_frames;
	/* the output's sum of squares and largest magnitude (16-bit PCM) */
	double sum_of_squares;
	int peak;
	/* the sink stopped the output */
	int stopped;
	/* the stream was copied as it is */
	int copied;
};

/* ---------- prototypes */

size_t h2_sound_codec_work_bytes(void);

/* the mono frames made of `source_frames` frames at `source_rate`, taken at
`output_rate` (as many as lie before the input's end); 0 for rates out of
bounds */
uint32_t h2_sound_codec_output_frames(
	uint32_t source_frames,
	long source_rate,
	long output_rate);

/* whether a stream of this kind, channels and rate is copied as it is when
the output is mono at `output_rate` */
int h2_sound_codec_is_copy(
	int kind,
	int channels,
	long rate,
	long output_rate,
	int output_channels);

/* The stream made mono Xbox ADPCM of `output_rate`, `output_bytes` of whole
blocks (silence after what the stream fills; what does not fit is left out),
given to `sink` through `staging` (`staging_bytes`, at least two blocks'
worth) in order, as xbox_adpcm_encoder_begin_sink's. `work` is
H2_SOUND_CODEC_WORK_BYTES at least, aligned to 16 bytes. One call at a time
per working memory. Any input is safe: reads stay within the chunks, every
write within the output and the working memory. */
enum h2_sound_codec_status h2_sound_codec_transcode(
	struct h2_sound_codec_stream const *stream,
	h2_sound_codec_read_proc read,
	void *read_context,
	void *work,
	size_t work_bytes,
	long output_rate,
	int output_channels,
	uint8_t *staging,
	uint32_t staging_bytes,
	uint32_t output_bytes,
	xbox_adpcm_sink_proc sink,
	void *sink_context,
	struct h2_sound_codec_result *result);

/* As h2_sound_codec_transcode, from output frame `first_frame` on (a link of
a long sound's chain, custom_edition_sounds.c): a stream copied as it is
starts at the whole block holding it; any other is decoded from its start
and the frames before it left out */
enum h2_sound_codec_status h2_sound_codec_transcode_from(
	struct h2_sound_codec_stream const *stream,
	h2_sound_codec_read_proc read,
	void *read_context,
	void *work,
	size_t work_bytes,
	long output_rate,
	int output_channels,
	uint8_t *staging,
	uint32_t staging_bytes,
	uint32_t output_bytes,
	uint32_t first_frame,
	xbox_adpcm_sink_proc sink,
	void *sink_context,
	struct h2_sound_codec_result *result);

/* a mono frame of the stream, as the game's mixer would have it before any
rate converting */
typedef void (*h2_sound_codec_frame_proc)(void *context, int value);

/* The stream decoded and mixed to mono, at its own rate, each frame given
to `give` (at most the stream's `frames`); `count` the frames given. For
tests: the transcoding above is this, converted and encoded. */
enum h2_sound_codec_status h2_sound_codec_decode(
	struct h2_sound_codec_stream const *stream,
	h2_sound_codec_read_proc read,
	void *read_context,
	void *work,
	size_t work_bytes,
	h2_sound_codec_frame_proc give,
	void *give_context,
	uint32_t *count);

char const *h2_sound_codec_status_describe(
	enum h2_sound_codec_status status);

#endif
