/*
DSOUND_SDL.C

Xbox DirectSound for the Linux build: a software mixer on an SDL3 audio
stream.

The game plays nearly everything through DirectSound streams: 16-bit stereo
PCM (music and other uncompressed sounds) and Xbox ADPCM, mono or stereo, at
22 or 44 kHz; the loading screen's tones are buffers, played as streams of
one looping packet (buffers). A packet is decoded to 16-bit PCM when the game submits it, since
the sound cache may reuse its memory once the packet completes. The mixer
runs on SDL's audio thread; for every voice it resamples to the output rate
with a windowed sinc low pass (which is how SetFrequency changes pitch;
resampling) and applies:
	- the stream volume (millibels),
	- the front left and right mix bin volumes of 2D voices,
	- for 3D voices, DirectSound's inverse distance rolloff between the
	  minimum and maximum distance, an equal power pan from the source's
	  direction in listener space, and the I3DL2 direct path, obstruction
	  and occlusion levels, their high frequency levels a low pass at the
	  environment's HF reference: a sound behind a wall is muffled.
3D voices also send to an I3DL2 reverb of the environment the listener is in
(reverb). On stereo output with audio.hrtf on, 3D voices are rendered for
headphones instead of panned (binaural rendering). A moving 3D voice is
Doppler shifted (doppler_ratio; audio.doppler = false leaves it), and a
voice whose tag gives it a sound cone is quieter facing away
(voice_cone_gain), as DirectSound did both. A look-ahead limiter keeps the
sum under full scale (limit).

The output is stereo, 5.1 or 7.1 (audio.channels; "auto" is 5.1 or 7.1 on
a device of 6 or 8 channels, else stereo). For surround the speaker config
reports AC-3, as an Xbox set to Dolby Digital does: the game then sends its
2D voices to the center and back mix bins too (dsound_initialize_channel).
3D voices are panned around the speakers the Xbox's DirectSound panned them
between, the front and back pairs (and on 7.1 the sides), not the center,
which is the 2D voices' (surround_pan). The reverb plays from those
speakers too, each its own mix of the late reverberation. The mix bins 0-5
are in SDL's channel order (FL, FR, FC, LFE, BL, BR; 7.1 adds SL, SR), so
bin n is output channel n; on 7.1 the back bins play from the side and back
pairs. The game sends nothing to the LFE bin, and neither does the mixer: a
receiver's bass management takes the low end of the other speakers to the
subwoofer, as it did for the Xbox. Stereo output mixes exactly as before.

Packets the mixer has finished are completed from DirectSoundDoWork, which
the game calls every frame, and from Flush, never from the audio thread:
the game's completion callback is not meant to run concurrently with it.

Without an audio device, a clock thread runs the same mixer into a scratch
buffer, so streams still drain at their real rate.

The audio probe (port/linux/game/audio_probe.c) records the mixer's output,
and the reverb's input and output apart (recording).

audio.volume sets the master volume (default 1.0); audio.reverb = false
turns the reverb off; audio.resampling = "linear" resamples by linear
interpolation instead (resampling); audio.enabled = false skips opening a
device; audio.buffer_frames sets the device's buffer; audio.channels
chooses stereo or 5.1 (port_config.c).
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "thread_stop.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define OUTPUT_RATE 48000
#define OUTPUT_CHANNELS 2 /* the stereo mix's, and the reverb's outputs */
#define MAXIMUM_OUTPUT_CHANNELS 8
#define MAXIMUM_STREAM_PACKETS 64
/* (the most mixed at once, under mixer_lock: the game's thread waits out a
chunk's mix to start, stop or change a sound, so a chunk is about 5 ms of
sound - at 1024, up to 2.7 ms of the Switch's game thread for each such call
in a busy firefight) */
#define MIX_CHUNK_FRAMES 256

#define XBOX_ADPCM_BLOCK_BYTES 36
#define XBOX_ADPCM_BLOCK_SAMPLES 64

/* the resampler (resampling) */
#define RESAMPLER_ZERO_CROSSINGS 16
#define RESAMPLER_TABLE_STEPS 256
#define RESAMPLER_CUTOFF 0.88
#define RESAMPLER_KAISER_BETA 6.5
#define RESAMPLER_MAXIMUM_STRETCH 2
#define RESAMPLER_HISTORY 128

/* the binaural rendering (hrtf) */
#define HRTF_MAXIMUM_TAPS 64
#define HRTF_MAXIMUM_DELAY 48
#define HRTF_INPUT 2048 /* a mix and the longest delay before it, a power of 2 */
#define HRTF_FADE_SECONDS 0.1f

/* ---------- voices */

struct voice_packet
{
	XMEDIAPACKET packet;
	short *samples;           /* interleaved, source channel count */
	unsigned long frames;
	BOOL finished;            /* played out by the mixer, not yet completed */
};

struct sdl_stream
{
	/* must be first: in C an IDirectSoundStream is just { lpVtbl } */
	IDirectSoundStream object;
	struct sdl_stream *next;
	ULONG reference_count;
	LPFNXMEDIAOBJECTCALLBACK callback;
	LPVOID context;

	/* format */
	BOOL adpcm;
	unsigned long channels;
	DWORD sample_rate;
	DWORD frequency;

	BOOL paused;
	/* a buffer played with DSBPLAY_LOOPING (buffers): its one packet starts
	over at its end, and never finishes */
	BOOL looping;

	/* 2D gains */
	float volume;             /* SetVolume */
	float mix_left, mix_right;
	float headroom;
	/* on surround output: the gains from each source channel to each
	speaker mix bin (SetMixBins, SetMixBinVolumes) */
	float surround_bins[2][MAXIMUM_OUTPUT_CHANNELS];

	/* 3D */
	BOOL has_3d;
	DWORD mode;
	float position[3];
	float minimum_distance, maximum_distance;
	/* the I3DL2 direct path's level and high frequency level, in millibels
	with obstruction and occlusion (SetI3DL2Source) */
	LONG direct, direct_hf;
	/* the I3DL2 room (reverb) send's level and high frequency level, in
	millibels with occlusion, and its rolloff factor (SetI3DL2Source) */
	LONG room, room_hf;
	float room_rolloff_factor;
	/* SetVelocity (units a second), and the Doppler shift the mixer is
	ramping from (doppler_ratio); SetConeAngles (degrees),
	SetConeOrientation and SetConeOutsideVolume (millibels): voice_cone_gain */
	float velocity[3];
	float current_doppler;
	DWORD cone_inside, cone_outside;
	float cone_orientation[3];
	LONG cone_outside_volume;

	struct voice_packet packets[MAXIMUM_STREAM_PACKETS];
	unsigned long packet_head;
	unsigned long packet_count;
	/* the next frame to take from the first packet not finished */
	unsigned long cursor;
	/* the resampler's (resampler_reset): the last RESAMPLER_HISTORY frames
	taken, how many were taken, the frame the output is at and how far past
	it, and the frames of silence taken since the packets ran out */
	float history[RESAMPLER_HISTORY][2];
	unsigned long history_count;
	unsigned long center;
	double phase;
	unsigned long silence;
	/* gains and low pass coefficients the mixer is ramping from, to avoid
	clicks */
	float current_left, current_right, current_room;
	float current_direct_lowpass, current_room_lowpass;
	float current_surround[2][MAXIMUM_OUTPUT_CHANNELS];
	BOOL gains_valid;
	/* the direct path's low pass, for each channel, and the room send's */
	float direct_lowpass[2];
	float room_lowpass;
	/* the binaural rendering's (hrtf): the voice's mono direct path, the
	last taps - 1 samples of each ear's copy of it delayed by its interaural
	delay, where the input is written next, and the filters, delays and
	gain it is moving from (none yet while hrtf_valid is FALSE) */
	float hrtf_input[HRTF_INPUT];
	float hrtf_tail[2][HRTF_MAXIMUM_TAPS];
	unsigned long hrtf_position;
	float hrtf_filters[2][HRTF_MAXIMUM_TAPS];
	float hrtf_delays[2];
	float current_hrtf;
	BOOL hrtf_valid;
};

static pthread_mutex_t mixer_lock = PTHREAD_MUTEX_INITIALIZER;
/* The mixer holds mixer_lock a voice at a time, not for a whole chunk's mix
(mix): the game's thread, which takes it to start, stop, feed or change a
sound, waited out every voice's mix, 7% of a Switch's game thread in a
30-player match. The game's every taking of the lock counts in
mixer_lock_wanted, and between voices the mixer lets it in (musl's mutex
hands the lock to no waiter: unlocked and taken again at once, the mixer
would most often have it back first). Between voices the next stream to mix
is mix_cursor, which a stream let go then moves past it (stream_release). */
static int mixer_lock_wanted;
static struct sdl_stream *mix_cursor;
#define mixer_lock_take() \
	do \
	{ \
		__atomic_add_fetch(&mixer_lock_wanted, 1, __ATOMIC_RELAXED); \
		pthread_mutex_lock(&mixer_lock); \
		__atomic_sub_fetch(&mixer_lock_wanted, 1, __ATOMIC_RELAXED); \
	} \
	while (0)
static struct sdl_stream *streams;
/* the DirectSound buffers' voices (buffers) */
static struct sdl_stream *buffers;

/* the listener, in DirectSound's left-handed +y up space */
static struct
{
	float position[3];
	float front[3];
	float top[3];
	float rolloff_factor;
	/* meters a unit (SetDistanceFactor): the game sets 3.048, a world unit
	being 10 feet. Only Doppler uses it (doppler_ratio) */
	float distance_factor;
	/* SetVelocity: units a second (the game leaves it 0, giving each
	voice's velocity relative to the listener) */
	float velocity[3];
} listener = { { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, 1.0f, 1.0f, { 0, 0, 0 } };

static float master_volume = 1.0f;
/* 2, 6 or 8 (audio.channels: stereo, 5.1, 7.1), chosen when the device
first opens; the game sets its mix bins by it, so it holds while the game
runs (another device is given the same channels, which SDL converts) */
static unsigned long output_channels = 2;

/* the I3DL2 listener properties (SetI3DL2Listener), which the reverb models:
DirectSound's defaults until the game sets an environment, their room level
leaving it silent; serial counts the changes */
static DSI3DL2LISTENER environment =
{
	DSBVOLUME_MIN, 0, 0.0f, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100.0f, 100.0f, 5000.0f
};
static unsigned long environment_serial;
/* audio.reverb */
static BOOL reverb_enabled = TRUE;
/* audio.resampling = "linear" */
static BOOL resampling_linear = FALSE;
/* audio.hrtf: whether 3D voices are rendered for headphones (hrtf_update),
and how far the mixer has faded to it (0 to 1) */
static volatile BOOL hrtf_wanted = FALSE;
/* audio.doppler */
static BOOL doppler_enabled = TRUE;
static float hrtf_level = 0.0f;

static float gain_from_millibels(LONG millibels)
{
	if (millibels <= DSBVOLUME_MIN)
		return 0.0f;
	return powf(10.0f, (float)millibels / 2000.0f);
}

/* value, or 0 once it is far too small to hear: the state of a filter or of
the reverb fed silence decays into the denormals, which many CPUs work out
slowly, and neither x86 nor ARM flushes them to zero by default */
static float flush_denormal(float value)
{
	return fabsf(value) < 1.0e-20f ? 0.0f : value;
}

/* cos(2 pi f / OUTPUT_RATE) of a filter's frequency f, within what the
output rate holds */
static float frequency_cosine(float frequency)
{
	if (frequency < 20.0f)
		frequency = 20.0f;
	if (frequency > 0.45f * OUTPUT_RATE)
		frequency = 0.45f * OUTPUT_RATE;
	return cosf(2.0f * 3.14159265f * frequency / OUTPUT_RATE);
}

/* the coefficient a of the one-pole low pass y = x + a (y' - x) whose gain is
gain (at most 1) at the frequency of cosine (frequency_cosine) */
static float lowpass_coefficient(float gain, float cosine)
{
	float a, b;

	if (gain >= 1.0f)
		return 0.0f;
	if (gain < 0.001f)
		gain = 0.001f;
	a = 1.0f - gain * gain;
	b = 1.0f - gain * gain * cosine;
	return (b - sqrtf(b * b - a * a)) / a;
}

/* ---------- decoding */

static const int ima_index_table[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

static const int ima_step_table[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
};

static int ima_expand(int nibble, int *predictor, int *index)
{
	int step = ima_step_table[*index];
	int difference = step >> 3;

	if (nibble & 1) difference += step >> 2;
	if (nibble & 2) difference += step >> 1;
	if (nibble & 4) difference += step;
	if (nibble & 8) difference = -difference;
	*predictor += difference;
	if (*predictor > 32767) *predictor = 32767;
	if (*predictor < -32768) *predictor = -32768;
	*index += ima_index_table[nibble];
	if (*index < 0) *index = 0;
	if (*index > 88) *index = 88;
	return *predictor;
}

/* Xbox ADPCM: per block, a 4-byte header per channel (first sample, step
index), then 4-byte groups of eight nibbles, low nibble first, alternating
between channels; 64 samples per channel. The header's sample is the block's
first, and the nibbles code the 63 after it: the 64th nibble only pads the
block (the maps' sounds always have 0 there), and decoding it in place of the
header's sample put a wrong sample in every 64, a buzz at 344 Hz in 22 kHz
sounds. */
static short *decode_adpcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long block_bytes = XBOX_ADPCM_BLOCK_BYTES * channels;
	unsigned long blocks = size / block_bytes;
	short *samples = malloc((blocks ? blocks : 1) * XBOX_ADPCM_BLOCK_SAMPLES * channels * sizeof(short));
	unsigned long block, channel;

	if (!samples)
	{
		*frame_count = 0;
		return NULL;
	}
	for (block = 0; block < blocks; block++)
	{
		const unsigned char *data = source + block * block_bytes;
		short *output = samples + block * XBOX_ADPCM_BLOCK_SAMPLES * channels;

		for (channel = 0; channel < channels; channel++)
		{
			const unsigned char *header = data + channel * 4;
			int predictor = (short)(header[0] | (header[1] << 8));
			int index = header[2] > 88 ? 88 : header[2];
			unsigned long group, byte;

			output[channel] = (short)predictor;
			for (group = 0; group < 8; group++)
			{
				const unsigned char *nibbles = data + 4 * channels + (group * channels + channel) * 4;

				for (byte = 0; byte < 4; byte++)
				{
					/* nibble n codes sample n + 1 */
					unsigned long sample = group * 8 + byte * 2 + 1;

					output[sample * channels + channel] = (short)ima_expand(nibbles[byte] & 0xf, &predictor, &index);
					if (sample + 1 < XBOX_ADPCM_BLOCK_SAMPLES)
						output[(sample + 1) * channels + channel] = (short)ima_expand(nibbles[byte] >> 4, &predictor, &index);
				}
			}
		}
	}
	*frame_count = blocks * XBOX_ADPCM_BLOCK_SAMPLES;
	return samples;
}

static short *decode_pcm(const unsigned char *source, unsigned long size, unsigned long channels,
	unsigned long *frame_count)
{
	unsigned long frames = size / (2 * channels);
	short *samples = malloc((frames ? frames : 1) * channels * sizeof(short));

	if (samples)
	{
#ifdef HALO_BIG_ENDIAN
		/* (the maps' samples are little-endian) */
		unsigned long index;

		for (index = 0; index < frames * channels; index++)
			samples[index] = (short)(source[index * 2] | (source[index * 2 + 1] << 8));
#else
		memcpy(samples, source, frames * channels * sizeof(short));
#endif
	}
	*frame_count = samples ? frames : 0;
	return samples;
}

/* ---------- 3D */

static float dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* DirectSound's inverse distance law, held beyond the maximum distance. The
game sets a voice's maximum distance before its minimum, so the maximum can
be under the minimum for a moment: it is held at the minimum */
static float distance_attenuation(const struct sdl_stream *stream, float distance, float rolloff_factor)
{
	float minimum = stream->minimum_distance;
	float maximum = stream->maximum_distance > minimum ? stream->maximum_distance : minimum;

	if (minimum <= 0.0f || distance <= minimum)
		return 1.0f;
	if (distance > maximum)
		distance = maximum;
	return minimum / (minimum + rolloff_factor * (distance - minimum));
}

static float clamp_real(float value, float minimum, float maximum);

static float clamp_unit(float value)
{
	return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

/* where a 3D voice is from the listener: to the right, up and ahead, in the
game's units */
static void voice_direction(const struct sdl_stream *stream, float *side, float *up, float *ahead)
{
	float offset[3], right_axis[3];
	int axis;

	if (stream->mode == DS3DMODE_HEADRELATIVE)
	{
		*side = stream->position[0];
		*up = stream->position[1];
		*ahead = stream->position[2];
		return;
	}
	for (axis = 0; axis < 3; axis++)
		offset[axis] = stream->position[axis] - listener.position[axis];
	/* left-handed: right = top x front */
	right_axis[0] = listener.top[1] * listener.front[2] - listener.top[2] * listener.front[1];
	right_axis[1] = listener.top[2] * listener.front[0] - listener.top[0] * listener.front[2];
	right_axis[2] = listener.top[0] * listener.front[1] - listener.top[1] * listener.front[0];
	*side = dot3(offset, right_axis);
	*up = dot3(offset, listener.top);
	*ahead = dot3(offset, listener.front);
}

/* DirectSound's sound cone: a voice is at its full level inside half its
inside angle of where it faces, at its outside volume beyond half its
outside angle, and between the two in between (in millibels). The game
sets each sound's cone from its tag; the retail maps' sounds leave the
angles 0 (and the channels start at 0), which the Xbox played as no cone,
as it does 360. The I3DL2 room send is not coned: the room still hears a
sound facing away */
static float voice_cone_gain(const struct sdl_stream *stream)
{
	float to_listener[3], length, facing, angle, inside, outside, fraction;
	int axis;

	if (!stream->cone_outside || (stream->cone_inside >= 360 && stream->cone_outside >= 360))
		return 1.0f;
	for (axis = 0; axis < 3; axis++)
	{
		to_listener[axis] = stream->mode == DS3DMODE_HEADRELATIVE ? -stream->position[axis] :
			listener.position[axis] - stream->position[axis];
	}
	length = sqrtf(dot3(to_listener, to_listener)) * sqrtf(dot3(stream->cone_orientation, stream->cone_orientation));
	if (length <= 1.0e-6f)
		return 1.0f;
	facing = dot3(to_listener, stream->cone_orientation) / length;
	angle = acosf(facing < -1.0f ? -1.0f : facing > 1.0f ? 1.0f : facing) * 57.29578f;
	inside = 0.5f * (float)stream->cone_inside;
	outside = 0.5f * (float)(stream->cone_outside > stream->cone_inside ? stream->cone_outside : stream->cone_inside);
	if (angle <= inside)
		return 1.0f;
	if (angle >= outside || outside <= inside)
		return gain_from_millibels(stream->cone_outside_volume);
	fraction = (angle - inside) / (outside - inside);
	return gain_from_millibels((LONG)(fraction * (float)stream->cone_outside_volume));
}

/* DirectSound's Doppler shift of a 3D voice, the factor its frequency is
played at: (c - vl) / (c - vs), vl and vs the listener's and the voice's
speeds towards each other along the line between them, c the speed of
sound, the velocities turned into meters a second by the distance factor
(the game: 3.048). The Xbox shifted the game's moving sounds so (a
Warthog, a Banshee, a projectile going past); the game gives each voice's
velocity relative to the listener. Held within a factor of 2 either way,
the speeds below c */
#define DOPPLER_SPEED_OF_SOUND 343.0f
#define DOPPLER_MAXIMUM 2.0f

static float doppler_ratio(const struct sdl_stream *stream)
{
	float to_listener[3], length, listener_speed = 0.0f, source_speed, ratio;
	int axis;

	if (!doppler_enabled || !stream->has_3d || stream->mode == DS3DMODE_DISABLE)
		return 1.0f;
	for (axis = 0; axis < 3; axis++)
	{
		to_listener[axis] = stream->mode == DS3DMODE_HEADRELATIVE ? -stream->position[axis] :
			listener.position[axis] - stream->position[axis];
	}
	length = sqrtf(dot3(to_listener, to_listener));
	if (length <= 1.0e-4f)
		return 1.0f;
	/* (towards the listener: the voice's velocity along the line, and the
	listener's along it the other way) */
	source_speed = dot3(stream->velocity, to_listener) / length * listener.distance_factor;
	if (stream->mode != DS3DMODE_HEADRELATIVE)
		listener_speed = -dot3(listener.velocity, to_listener) / length * listener.distance_factor;
	source_speed = clamp_real(source_speed, -0.9f * DOPPLER_SPEED_OF_SOUND, 0.9f * DOPPLER_SPEED_OF_SOUND);
	listener_speed = clamp_real(listener_speed, -0.9f * DOPPLER_SPEED_OF_SOUND, 0.9f * DOPPLER_SPEED_OF_SOUND);
	ratio = (DOPPLER_SPEED_OF_SOUND + listener_speed) / (DOPPLER_SPEED_OF_SOUND - source_speed);
	return clamp_real(ratio, 1.0f / DOPPLER_MAXIMUM, DOPPLER_MAXIMUM);
}

/* the gains of a 3D voice to the left and right, and the room rolloff's
attenuation of its room send */
static void spatialize(const struct sdl_stream *stream, float *left, float *right, float *room)
{
	float distance, attenuation, pan, side, up, ahead;

	voice_direction(stream, &side, &up, &ahead);
	/* in the game's units, those of the minimum and maximum distances: the
	distance factor turns units into meters for Doppler, and scaling by it
	here put every 3D sound 3 times as far away, up to 10 dB quieter */
	distance = sqrtf(side * side + up * up + ahead * ahead);
	attenuation = distance_attenuation(stream, distance, listener.rolloff_factor);
	*room = distance_attenuation(stream, distance, environment.flRoomRolloffFactor + stream->room_rolloff_factor);

	/* equal power pan; close sources and sources straight ahead or behind
	stay centred, and neither ear drops below a quarter */
	{
		float horizontal = sqrtf(side * side + ahead * ahead);
		float angle;

		pan = horizontal > 1.0e-4f ? side / horizontal : 0.0f;
		if (distance < stream->minimum_distance && stream->minimum_distance > 0.0f)
			pan *= distance / stream->minimum_distance;
		pan *= 0.75f;
		angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		*left = cosf(angle) * 1.41421356f * 0.70710678f;
		*right = sinf(angle) * 1.41421356f * 0.70710678f;
	}
	*left *= attenuation * gain_from_millibels(stream->direct) * voice_cone_gain(stream);
	*right *= attenuation * gain_from_millibels(stream->direct) * voice_cone_gain(stream);
}

/* the voice's gains to the left and right, and to the reverb (only 3D voices
send to it, as their I3DL2 mix bin did on the Xbox), with the coefficients of
the low passes of its direct path (3D voices: obstruction and occlusion
muffle it) and of the send */
static void voice_gains(const struct sdl_stream *stream, float *left, float *right, float *room,
	float *direct_lowpass, float *room_lowpass)
{
	*room = 0.0f;
	*direct_lowpass = 0.0f;
	*room_lowpass = 0.0f;
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		float rolloff, cosine = frequency_cosine(environment.flHFReference);

		spatialize(stream, left, right, &rolloff);
		if (stream->direct_hf < stream->direct)
			*direct_lowpass = lowpass_coefficient(gain_from_millibels(stream->direct_hf - stream->direct), cosine);
		if (reverb_enabled)
		{
			LONG level = environment.lRoom + stream->room;
			LONG high_level = environment.lRoom + environment.lRoomHF + stream->room_hf;

			*room = gain_from_millibels(level) * rolloff;
			if (*room > 0.0f && high_level < level)
				*room_lowpass = lowpass_coefficient(gain_from_millibels(high_level - level), cosine);
		}
	}
	else
	{
		*left = stream->mix_left;
		*right = stream->mix_right;
	}
	*left *= stream->volume * master_volume;
	*right *= stream->volume * master_volume;
	*room *= stream->volume * master_volume;
}

/* the speakers 3D voices are panned between, clockwise from the front right,
and their directions in radians clockwise from straight ahead. On 5.1 these
are the Xbox's four 3D speakers, the front and back pairs (ITU-R BS.775: the
fronts at 30 degrees, the backs at 110): its DirectSound panned 3D voices
around them and kept the center for 2D voices (the game's mono ones,
dsound_initialize_channel), so a sound straight ahead is between the front
pair. On 7.1 the sides are at 90 degrees and the backs at 150 */
struct surround_speaker
{
	unsigned long channel;
	float angle;
};

#define SURROUND_51_SPEAKERS 4
#define SURROUND_71_SPEAKERS 6
static const struct surround_speaker surround_51[SURROUND_51_SPEAKERS] =
{
	{ 1, 0.52359878f }, { 5, 1.91986218f }, { 4, 4.36332313f }, { 0, 5.75958653f },
};
static const struct surround_speaker surround_71[SURROUND_71_SPEAKERS] =
{
	{ 1, 0.52359878f }, { 7, 1.57079633f }, { 5, 2.61799388f }, { 4, 3.66519143f }, { 6, 4.71238898f },
	{ 0, 5.75958653f },
};

static const struct surround_speaker *surround_speakers(unsigned long *count)
{
	*count = output_channels == 8 ? SURROUND_71_SPEAKERS : SURROUND_51_SPEAKERS;
	return output_channels == 8 ? surround_71 : surround_51;
}

/* the speaker gains of a sound from a direction, side (right) and ahead: an
equal power pan between the two speakers either side of it, blended (in
power) towards all of them equally by spread, 0 to 1. Their power sums to 1,
as the stereo pan's does */
static void surround_pan(float side, float ahead, float spread, float *gains)
{
	unsigned long count, index, speaker;
	const struct surround_speaker *speakers = surround_speakers(&count);
	float angle = atan2f(side, ahead), from, width, position;

	for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
		gains[speaker] = 0.0f;
	if (angle < 0.0f)
		angle += 6.28318531f;
	/* the pair either side: index and the next, the last pair across the
	front */
	for (index = 0; index + 1 < count && angle >= speakers[index + 1].angle; index++)
		;
	if (angle < speakers[0].angle || index == count - 1)
	{
		index = count - 1;
		from = speakers[index].angle;
		width = speakers[0].angle + 6.28318531f - from;
		position = (angle < speakers[0].angle ? angle + 6.28318531f : angle) - from;
	}
	else
	{
		from = speakers[index].angle;
		width = speakers[index + 1].angle - from;
		position = angle - from;
	}
	position = clamp_unit(position / width);
	gains[speakers[index].channel] = cosf(position * 1.57079633f);
	gains[speakers[(index + 1) % count].channel] = sinf(position * 1.57079633f);
	for (index = 0; index < count; index++)
	{
		float *gain = &gains[speakers[index].channel];

		*gain = sqrtf((1.0f - spread) * *gain * *gain + spread / (float)count);
	}
}

/* on surround output, a voice's gains from each source channel to each
speaker (the direct path's; its room send and low passes are voice_gains'):
	- a 3D voice is panned around the 3D speakers from its direction, a
	  little spread so that it never vanishes between two, spread out
	  towards all of them inside its minimum distance and above or below, as
	  the stereo pan centres those;
	- a 2D voice goes by its mix bins: on 7.1 the Xbox's back bins play from
	  the side and back pairs. */
static void voice_surround_gains(const struct sdl_stream *stream, float gains[2][MAXIMUM_OUTPUT_CHANNELS])
{
	float scale = stream->volume * master_volume;
	unsigned long speaker;

	memset(gains, 0, 2 * sizeof(gains[0]));
	if (stream->has_3d && stream->mode != DS3DMODE_DISABLE)
	{
		float side, up, ahead, distance, horizontal, attenuation, spread = 0.1f;

		voice_direction(stream, &side, &up, &ahead);
		horizontal = sqrtf(side * side + ahead * ahead);
		distance = sqrtf(side * side + up * up + ahead * ahead);
		if (horizontal <= 1.0e-4f)
			spread = 1.0f;
		else if (distance > 1.0e-4f && 0.5f * (1.0f - horizontal / distance) > spread)
			spread = 0.5f * (1.0f - horizontal / distance);
		if (distance < stream->minimum_distance && stream->minimum_distance > 0.0f &&
			1.0f - distance / stream->minimum_distance > spread)
		{
			spread = 1.0f - distance / stream->minimum_distance;
		}
		surround_pan(side, ahead, spread, gains[0]);
		attenuation = distance_attenuation(stream, distance, listener.rolloff_factor) *
			gain_from_millibels(stream->direct) * voice_cone_gain(stream);
		for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
		{
			gains[0][speaker] *= attenuation;
			/* a stereo 3D voice: both channels from where it is */
			if (stream->channels == 2)
				gains[1][speaker] = gains[0][speaker] *= 0.5f;
		}
	}
	else
	{
		memcpy(gains, stream->surround_bins, 2 * sizeof(gains[0]));
		if (stream->channels == 1)
			memset(gains[1], 0, sizeof(gains[1]));
		if (output_channels == 8)
		{
			int row;

			for (row = 0; row < 2; row++)
			{
				gains[row][6] = gains[row][4] *= 0.70710678f;
				gains[row][7] = gains[row][5] *= 0.70710678f;
			}
		}
	}
	for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
	{
		gains[0][speaker] *= scale;
		gains[1][speaker] *= scale;
	}
}

/* ---------- binaural rendering

audio.hrtf = "on" (or "auto", on an output whose name says it is
headphones) renders 3D voices for headphones on stereo output: each one is
heard through the head-related impulse responses of its direction, the
filtering of the head and the outer ears, and the far ear hears it later,
by the interaural delay, so that a sound is placed all around the listener,
above and behind too, instead of between the ears. The Xbox's DirectSound
filtered 3D voices with HRTFs of its own (the game asks for its full
quality ones, DirectSoundUseFullHRTF); these are measured ones, the SADIE II
database's KEMAR, diffuse-field equalized, so that they place a sound
without changing its tone overall (port/assets/hrtf, tools/hrtf_assets.py).

The responses are a grid of directions, each ear's made minimum phase and
HRTF taps long, its delay apart: a voice's filter is the four directions
around it blended by how near each is, and its delays blended the same way,
which keeps the blend free of the comb filtering of mixing two delays. Each
mix the filter and the delays move to the voice's direction: the output
crossfades from the old filter's to the new one's over the mix, and each
ear's delay slides to its new length, so a moving sound moves smoothly,
without clicks. Inside its minimum distance a voice blends towards the
stereo pan's centered sound (the player's own footsteps and weapons stay in
the head, as the stereo pan keeps them), and turning audio.hrtf on or off
fades over HRTF_FADE_SECONDS. Its level is the stereo pan's: both ears
together have a power of 1 over all directions. 2D voices, the reverb and
surround output are as they were. */

extern const unsigned int dsound_hrtf_data[];
extern const unsigned long dsound_hrtf_data_size;

static struct
{
	BOOL loaded;
	unsigned long taps, elevations, azimuths;
	/* radians */
	float first_elevation, elevation_step, azimuth_step;
	/* for each direction (elevation by elevation, azimuth counterclockwise
	from ahead), each ear's taps, last first (hrtf_convolve), and its delay in
	samples; taps is the file's rounded up to a multiple of 4, the first
	ones 0 */
	float *filters;
	float *delays;
} hrtf;

static unsigned long hrtf_read_u32(const unsigned char *data)
{
	return (unsigned long)data[0] | ((unsigned long)data[1] << 8) | ((unsigned long)data[2] << 16) |
		((unsigned long)data[3] << 24);
}

static float hrtf_read_float(const unsigned char *data)
{
	unsigned int bits = (unsigned int)hrtf_read_u32(data);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int hrtf_read_s16(const unsigned char *data)
{
	return (short)(data[0] | (data[1] << 8));
}

/* the embedded responses (the file tools/hrtf_assets.py describes); FALSE
without them */
static BOOL hrtf_load(void)
{
	const unsigned char *data = (const unsigned char *)dsound_hrtf_data;
	unsigned long size = (unsigned long)dsound_hrtf_data_size, count, index, ear, tap, record, file_taps;
	float scale;

	if (hrtf.loaded)
		return TRUE;
	if (size < 40 || memcmp(data, "HRTF", 4) || hrtf_read_u32(data + 4) != 1 ||
		hrtf_read_u32(data + 8) != OUTPUT_RATE)
	{
		return FALSE;
	}
	file_taps = hrtf_read_u32(data + 12);
	hrtf.taps = (file_taps + 3) & ~3UL;
	hrtf.elevations = hrtf_read_u32(data + 16);
	hrtf.first_elevation = hrtf_read_float(data + 20) * 0.017453293f;
	hrtf.elevation_step = hrtf_read_float(data + 24) * 0.017453293f;
	hrtf.azimuths = hrtf_read_u32(data + 28);
	hrtf.azimuth_step = hrtf_read_float(data + 32) * 0.017453293f;
	scale = hrtf_read_float(data + 36);
	count = hrtf.elevations * hrtf.azimuths;
	record = 4 + 4 * file_taps;
	if (!file_taps || hrtf.taps > HRTF_MAXIMUM_TAPS || !count || hrtf.elevation_step <= 0.0f ||
		hrtf.azimuth_step <= 0.0f || size < 40 + count * record)
	{
		return FALSE;
	}
	hrtf.filters = calloc(count * 2 * hrtf.taps, sizeof(float));
	hrtf.delays = malloc(count * 2 * sizeof(float));
	if (!hrtf.filters || !hrtf.delays)
	{
		free(hrtf.filters);
		free(hrtf.delays);
		return FALSE;
	}
	for (index = 0; index < count; index++)
	{
		const unsigned char *entry = data + 40 + index * record;

		for (ear = 0; ear < 2; ear++)
		{
			float delay = (float)hrtf_read_s16(entry + 2 * ear) / 256.0f;

			hrtf.delays[index * 2 + ear] = delay < 0.0f ? 0.0f : delay > HRTF_MAXIMUM_DELAY ? HRTF_MAXIMUM_DELAY : delay;
			for (tap = 0; tap < file_taps; tap++)
			{
				hrtf.filters[(index * 2 + ear) * hrtf.taps + hrtf.taps - 1 - tap] =
					(float)hrtf_read_s16(entry + 4 + 2 * (ear * file_taps + tap)) * scale;
			}
		}
	}
	hrtf.loaded = TRUE;
	return TRUE;
}

/* the filters and delays of a voice from a direction (side to the right, up,
ahead): the four grid directions around it, blended */
static void hrtf_direction(float side, float up, float ahead, float filters[2][HRTF_MAXIMUM_TAPS], float *delays)
{
	float horizontal = sqrtf(side * side + ahead * ahead);
	float azimuth = atan2f(-side, ahead), elevation = atan2f(up, horizontal);
	float ring, step, ring_fraction, step_fraction;
	unsigned long rings[2], steps[2], ear, tap;
	int corner;

	if (azimuth < 0.0f)
		azimuth += 6.28318531f;
	ring = (elevation - hrtf.first_elevation) / hrtf.elevation_step;
	if (ring < 0.0f)
		ring = 0.0f;
	if (ring > (float)(hrtf.elevations - 1))
		ring = (float)(hrtf.elevations - 1);
	rings[0] = (unsigned long)ring;
	rings[1] = rings[0] + 1 < hrtf.elevations ? rings[0] + 1 : rings[0];
	ring_fraction = ring - (float)rings[0];
	step = azimuth / hrtf.azimuth_step;
	steps[0] = (unsigned long)step % hrtf.azimuths;
	steps[1] = (steps[0] + 1) % hrtf.azimuths;
	step_fraction = step - floorf(step);
	memset(filters, 0, 2 * sizeof(filters[0]));
	delays[0] = delays[1] = 0.0f;
	for (corner = 0; corner < 4; corner++)
	{
		unsigned long index = rings[corner >> 1] * hrtf.azimuths + steps[corner & 1];
		float weight = ((corner >> 1) ? ring_fraction : 1.0f - ring_fraction) *
			((corner & 1) ? step_fraction : 1.0f - step_fraction);

		if (weight <= 0.0f)
			continue;
		for (ear = 0; ear < 2; ear++)
		{
			const float *source = hrtf.filters + (index * 2 + ear) * hrtf.taps;

			for (tap = 0; tap < hrtf.taps; tap++)
				filters[ear][tap] += source[tap] * weight;
			delays[ear] += hrtf.delays[index * 2 + ear] * weight;
		}
	}
}

/* how much of a 3D voice the binaural rendering takes (0 to 1), the rest
the stereo pan's: none on surround output or with audio.hrtf off, and the
fade between while it is turned on or off */
static float hrtf_share(const struct sdl_stream *stream)
{
	if (hrtf_level <= 0.0f || output_channels != 2 || !stream->has_3d || stream->mode == DS3DMODE_DISABLE)
		return 0.0f;
	return hrtf_level;
}

/* a voice's target for this mix: its filters and delays, and its gain (the
direct path's, as spatialize's but for the pan). Inside its minimum
distance the filters blend towards the centered sound (each ear the voice
as it is, at the stereo pan's level) and the delays towards none: the
filters are minimum phase, their energy at their start, so the blend does
not cancel itself as mixing the two outputs would */
static float hrtf_target(const struct sdl_stream *stream, float share, float filters[2][HRTF_MAXIMUM_TAPS],
	float *delays)
{
	float side, up, ahead, distance;

	voice_direction(stream, &side, &up, &ahead);
	distance = sqrtf(side * side + up * up + ahead * ahead);
	hrtf_direction(side, up, ahead, filters, delays);
	if (stream->minimum_distance > 0.0f && distance < stream->minimum_distance)
	{
		float near = distance / stream->minimum_distance;
		unsigned long ear, tap;

		for (ear = 0; ear < 2; ear++)
		{
			for (tap = 0; tap < hrtf.taps; tap++)
				filters[ear][tap] *= near;
			filters[ear][hrtf.taps - 1] += (1.0f - near) * 0.70710678f;
			delays[ear] *= near;
		}
	}
	return share * distance_attenuation(stream, distance, listener.rolloff_factor) *
		gain_from_millibels(stream->direct) * voice_cone_gain(stream) * stream->volume * master_volume;
}

/* a voice starting (over), or taken up by the rendering again: silence
behind it */
static void hrtf_reset(struct sdl_stream *stream)
{
	memset(stream->hrtf_input, 0, sizeof(stream->hrtf_input));
	memset(stream->hrtf_tail, 0, sizeof(stream->hrtf_tail));
	stream->hrtf_position = 0;
	stream->current_hrtf = 0.0f;
	stream->hrtf_valid = FALSE;
}

/* the sample of input delay + 1 samples before position (a cubic Hermite
interpolation between the four around it). The delay is never negative, so
a conversion to int is its floor: floorf is a call into the C library on
32-bit x86, which made this five times slower there */
static float hrtf_delayed(const float *input, unsigned long position, float delay)
{
	/* (a sample more than the delay, so the newest of the four is one
	written already) */
	int whole = (int)delay;
	float fraction = delay - (float)whole;
	unsigned long newest = position - 1 - (unsigned long)whole;
	float y0 = input[(newest + 1) & (HRTF_INPUT - 1)];
	float y1 = input[newest & (HRTF_INPUT - 1)];
	float y2 = input[(newest - 1) & (HRTF_INPUT - 1)];
	float y3 = input[(newest - 2) & (HRTF_INPUT - 1)];
	float c1 = 0.5f * (y2 - y0);
	float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
	float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

	return ((c3 * fraction + c2) * fraction + c1) * fraction + y1;
}

/* adds to sums (frames of them) the filter (taps of it, last first) run
over samples (taps - 1 before the first frame's, then the frames'): tap by
tap over all the frames, which vectorizes, each tap a constant */
static void hrtf_convolve(const float *filter, const float *samples, float *sums, unsigned long frames,
	unsigned long taps)
{
	unsigned long tap, frame;

	for (tap = 0; tap < taps; tap++)
	{
		float weight = filter[tap];
		const float *source = samples + tap;

		for (frame = 0; frame < frames; frame++)
			sums[frame] += weight * source[frame];
	}
}

/* the binaural rendering of a voice's mono direct path, mono (frames of it,
at most MIX_CHUNK_FRAMES), added to output (frames of stereo); its targets
for the mix computed by hrtf_target. The mix's input goes in first; then
for each ear its delayed copy, the delay sliding to the new one, and that
filtered a block at a time */
static void hrtf_mix(struct sdl_stream *stream, const float *mono, float *output, unsigned long frames,
	float target_gain, float filters[2][HRTF_MAXIMUM_TAPS], const float *delays)
{
	unsigned long taps = hrtf.taps, position = stream->hrtf_position, frame, ear;
	BOOL fading;
	float gain, ramp_gain;
	/* an ear's samples: the taps - 1 before the mix, then the mix's; and
	the old filter's sums and the new one's */
	float samples[HRTF_MAXIMUM_TAPS + MIX_CHUNK_FRAMES], old_sums[MIX_CHUNK_FRAMES], new_sums[MIX_CHUNK_FRAMES];

	if (!stream->hrtf_valid)
	{
		memcpy(stream->hrtf_filters, filters, sizeof(stream->hrtf_filters));
		stream->hrtf_delays[0] = delays[0];
		stream->hrtf_delays[1] = delays[1];
		stream->hrtf_valid = TRUE;
	}
	fading = memcmp(stream->hrtf_filters, filters, sizeof(stream->hrtf_filters)) != 0;
	gain = stream->current_hrtf;
	ramp_gain = (target_gain - gain) / (float)frames;
	for (frame = 0; frame < frames; frame++)
	{
		stream->hrtf_input[(position + frame) & (HRTF_INPUT - 1)] = mono[frame] * gain;
		gain += ramp_gain;
	}
	for (ear = 0; ear < 2; ear++)
	{
		float delay = stream->hrtf_delays[ear], ramp_delay = (delays[ear] - delay) / (float)frames;
		float *delayed = samples + taps - 1;

		memcpy(samples, stream->hrtf_tail[ear], (taps - 1) * sizeof(float));
		for (frame = 0; frame < frames; frame++)
		{
			delayed[frame] = hrtf_delayed(stream->hrtf_input, position + frame, delay);
			delay += ramp_delay;
		}
		memset(old_sums, 0, frames * sizeof(float));
		hrtf_convolve(stream->hrtf_filters[ear], samples, old_sums, frames, taps);
		if (fading)
		{
			float fade = 0.0f, ramp_fade = 1.0f / (float)frames;

			memset(new_sums, 0, frames * sizeof(float));
			hrtf_convolve(filters[ear], samples, new_sums, frames, taps);
			for (frame = 0; frame < frames; frame++)
			{
				old_sums[frame] += (new_sums[frame] - old_sums[frame]) * fade;
				fade += ramp_fade;
			}
		}
		for (frame = 0; frame < frames; frame++)
			output[frame * 2 + ear] += old_sums[frame];
		memcpy(stream->hrtf_tail[ear], samples + frames, (taps - 1) * sizeof(float));
	}
	stream->hrtf_position = position + frames;
	memcpy(stream->hrtf_filters, filters, sizeof(stream->hrtf_filters));
	stream->hrtf_delays[0] = delays[0];
	stream->hrtf_delays[1] = delays[1];
	stream->current_hrtf = target_gain;
}

/* ---------- resampling

Each voice is resampled to the output rate by band-limited interpolation (J.
O. Smith's): an output sample is the source frames around its moment, each
weighted by a windowed sinc low pass centred there. The low pass keeps
RESAMPLER_CUTOFF of the source's band and takes the images of it out (65 dB
down): linear interpolation, which the mixer did before, left them only 8 to
20 dB down, a gritty haze above 11 kHz over every 22 kHz voice. A voice
played faster than the output rate takes its frames (a step over 1) gets the
low pass narrowed to match, up to RESAMPLER_MAXIMUM_STRETCH times, so it
does not alias. The frames come from the voice's packets in turn, so the low
pass reads straight across a packet's end into the next.

audio.resampling = "linear" interpolates between the two frames around
the moment instead, as the mixer did before: brighter to some ears, for the
images it leaves, which the windowed sinc takes out. It is a choice of its
own: turning the reverb off left the low pass on. */

/* the low pass's one side, RESAMPLER_TABLE_STEPS values a source frame */
static float resampler_table[RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS + 2];
/* the same low pass as the weights of a voice's taps 1 - RESAMPLER_ZERO_CROSSINGS
to RESAMPLER_ZERO_CROSSINGS, at each of RESAMPLER_TABLE_STEPS phases between two
source frames (and one row more, for the last's blend): a voice at the output
rate or slower blends two rows by its phase, which is what the table gave tap
by tap, a third of the work */
static float resampler_phases[RESAMPLER_TABLE_STEPS + 1][2 * RESAMPLER_ZERO_CROSSINGS];

static double bessel_i0(double x)
{
	double sum = 1.0, term = 1.0;
	int k;

	for (k = 1; k < 64 && term > 1.0e-12 * sum; k++)
	{
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
	}
	return sum;
}

static void resampler_initialize(void)
{
	unsigned long index;

	for (index = 0; index <= RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS; index++)
	{
		double distance = (double)index / RESAMPLER_TABLE_STEPS;
		double edge = distance / RESAMPLER_ZERO_CROSSINGS;
		double angle = 3.14159265358979 * RESAMPLER_CUTOFF * distance;
		double sinc = index ? sin(angle) / angle : 1.0;
		double window = bessel_i0(RESAMPLER_KAISER_BETA * sqrt(1.0 - edge * edge)) / bessel_i0(RESAMPLER_KAISER_BETA);

		resampler_table[index] = (float)(RESAMPLER_CUTOFF * sinc * window);
	}
	resampler_table[RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS + 1] = 0.0f;
}

/* the low pass, distance source frames from its centre times
RESAMPLER_TABLE_STEPS */
static float resampler_weight(float distance)
{
	unsigned long index = (unsigned long)distance;
	float fraction;

	if (index >= RESAMPLER_ZERO_CROSSINGS * RESAMPLER_TABLE_STEPS)
		return 0.0f;
	fraction = distance - (float)index;
	return resampler_table[index] + (resampler_table[index + 1] - resampler_table[index]) * fraction;
}

static void resampler_phases_initialize(void)
{
	unsigned long phase, tap;

	for (phase = 0; phase <= RESAMPLER_TABLE_STEPS; phase++)
	{
		for (tap = 0; tap < 2 * RESAMPLER_ZERO_CROSSINGS; tap++)
		{
			float distance = fabsf((float)((long)tap + 1 - RESAMPLER_ZERO_CROSSINGS) -
				(float)phase / RESAMPLER_TABLE_STEPS);

			resampler_phases[phase][tap] = resampler_weight(distance * RESAMPLER_TABLE_STEPS);
		}
	}
}

/* a voice starting (over): silence before its first frame, which the output
starts at */
static void resampler_reset(struct sdl_stream *stream)
{
	memset(stream->history, 0, sizeof(stream->history));
	stream->history_count = RESAMPLER_ZERO_CROSSINGS * RESAMPLER_MAXIMUM_STRETCH;
	stream->center = stream->history_count;
	stream->phase = 0.0;
	stream->silence = 0;
}

static float packet_sample(const struct voice_packet *packet, unsigned long frame, unsigned long channel,
	unsigned long channels)
{
	return packet->samples[frame * channels + channel] * (1.0f / 32768.0f);
}

/* the voice's next frame, finishing the packets it passes; FALSE once they
run out */
static BOOL take_frame(struct sdl_stream *stream, float *frame)
{
	for (;;)
	{
		struct voice_packet *packet = NULL;
		unsigned long position;

		for (position = 0; position < stream->packet_count; position++)
		{
			struct voice_packet *candidate = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

			if (!candidate->finished)
			{
				packet = candidate;
				break;
			}
		}
		if (!packet)
			return FALSE;
		if (stream->cursor < packet->frames)
		{
			frame[0] = packet_sample(packet, stream->cursor, 0, stream->channels);
			frame[1] = packet_sample(packet, stream->cursor, stream->channels - 1, stream->channels);
			stream->cursor++;
			return TRUE;
		}
		stream->cursor = 0;
		if (stream->looping && packet->frames)
			continue;
		packet->finished = TRUE;
	}
}

/* whether the voice has a frame to take; the packets with none left are
marked finished on the way, as take_frame marks them */
static BOOL voice_has_frames(struct sdl_stream *stream)
{
	unsigned long position;

	for (position = 0; position < stream->packet_count; position++)
	{
		struct voice_packet *packet = &stream->packets[(stream->packet_head + position) % MAXIMUM_STREAM_PACKETS];

		if (packet->finished)
			continue;
		if (stream->cursor < packet->frames)
			return TRUE;
		stream->cursor = 0;
		packet->finished = TRUE;
	}
	return FALSE;
}

/* ---------- mixing */

/* mixes one voice into output (frames of output_channels floats), and into
the reverb's send (frames of mono) */
static void mix_voice(struct sdl_stream *stream, float *output, float *send, unsigned long frames)
{
	double step, ramp_step, base_step;
	float doppler, target_doppler;
	float target_left, target_right, target_room, left, right, room, ramp_left, ramp_right, ramp_room, scale;
	float target_direct_lowpass, target_room_lowpass, direct_lowpass, room_lowpass;
	float ramp_direct_lowpass, ramp_room_lowpass;
	long width;
	unsigned long frame;
	/* on surround output, the speakers' gains (voice_surround_gains) */
	BOOL surround = output_channels > 2;
	float target_surround[2][MAXIMUM_OUTPUT_CHANNELS], surround_gains[2][MAXIMUM_OUTPUT_CHANNELS];
	float ramp_surround[2][MAXIMUM_OUTPUT_CHANNELS];
	unsigned long speaker;
	/* on stereo output with audio.hrtf, the binaural rendering's share of a
	3D voice, and its targets (hrtf) */
	float share, target_hrtf = 0.0f, hrtf_filters[2][HRTF_MAXIMUM_TAPS], hrtf_delays[2];
	float hrtf_mono[MIX_CHUNK_FRAMES];
	BOOL binaural;

	if (stream->paused || !stream->packet_count || !stream->sample_rate)
		return;
	base_step = (double)(stream->frequency ? stream->frequency : stream->sample_rate) / OUTPUT_RATE;
	/* the Doppler shift, moving from the last mix's to this one's over the
	mix, so that a voice going past glides */
	target_doppler = doppler_ratio(stream);
	doppler = stream->current_doppler > 0.0f ? stream->current_doppler : target_doppler;
	step = base_step * doppler;
	ramp_step = base_step * (target_doppler - doppler) / (double)frames;
	stream->current_doppler = target_doppler;
	/* the low pass narrowed for a voice taking frames faster than the output
	rate (at the fastest it is in this mix), and the frames it reaches on
	each side */
	{
		double fastest = step > base_step * target_doppler ? step : base_step * target_doppler;

		scale = fastest > 1.0 ? (float)(1.0 / (fastest < RESAMPLER_MAXIMUM_STRETCH ? fastest :
			RESAMPLER_MAXIMUM_STRETCH)) : 1.0f;
	}
	width = (long)ceilf(RESAMPLER_ZERO_CROSSINGS / scale);
	/* a voice that ran dry and stopped (below) starts over once it has
	frames again: stream_process starts over only a stream with no packets,
	and the next can come before the finished ones are completed */
	if (stream->silence > (unsigned long)(2 * width))
	{
		if (!voice_has_frames(stream))
			return;
		resampler_reset(stream);
		stream->gains_valid = FALSE;
	}
	voice_gains(stream, &target_left, &target_right, &target_room, &target_direct_lowpass, &target_room_lowpass);
	if (surround)
		voice_surround_gains(stream, target_surround);
	share = frames <= MIX_CHUNK_FRAMES ? hrtf_share(stream) : 0.0f;
	if (!stream->gains_valid)
		hrtf_reset(stream);
	if (share > 0.0f)
	{
		target_hrtf = hrtf_target(stream, share, hrtf_filters, hrtf_delays);
		target_left *= 1.0f - share;
		target_right *= 1.0f - share;
	}
	/* (a voice turned all the way down needs none of it) */
	binaural = target_hrtf > 0.0f || stream->current_hrtf > 0.0f;
	if (!stream->gains_valid)
	{
		if (surround)
			memcpy(stream->current_surround, target_surround, sizeof(target_surround));
		stream->current_left = target_left;
		stream->current_right = target_right;
		stream->current_room = target_room;
		stream->current_direct_lowpass = target_direct_lowpass;
		stream->current_room_lowpass = target_room_lowpass;
		stream->direct_lowpass[0] = stream->direct_lowpass[1] = 0.0f;
		stream->room_lowpass = 0.0f;
		stream->gains_valid = TRUE;
	}
	left = stream->current_left;
	right = stream->current_right;
	room = stream->current_room;
	direct_lowpass = stream->current_direct_lowpass;
	room_lowpass = stream->current_room_lowpass;
	ramp_left = (target_left - left) / (float)frames;
	ramp_right = (target_right - right) / (float)frames;
	ramp_room = (target_room - room) / (float)frames;
	ramp_direct_lowpass = (target_direct_lowpass - direct_lowpass) / (float)frames;
	ramp_room_lowpass = (target_room_lowpass - room_lowpass) / (float)frames;
	if (surround)
	{
		for (speaker = 0; speaker < MAXIMUM_OUTPUT_CHANNELS; speaker++)
		{
			surround_gains[0][speaker] = stream->current_surround[0][speaker];
			surround_gains[1][speaker] = stream->current_surround[1][speaker];
			ramp_surround[0][speaker] = (target_surround[0][speaker] - surround_gains[0][speaker]) / (float)frames;
			ramp_surround[1][speaker] = (target_surround[1][speaker] - surround_gains[1][speaker]) / (float)frames;
		}
	}

	for (frame = 0; frame < frames; frame++)
	{
		float sample_left = 0.0f, sample_right = 0.0f;
		long tap;

		/* the frames ahead the low pass reaches; after the last packet,
		silence, until what was taken has played out */
		while ((long)(stream->history_count - stream->center) <= width)
		{
			float *slot = stream->history[stream->history_count % RESAMPLER_HISTORY];

			if (take_frame(stream, slot))
			{
				stream->silence = 0;
			}
			else
			{
				slot[0] = slot[1] = 0.0f;
				stream->silence++;
			}
			stream->history_count++;
		}
		if (stream->silence > (unsigned long)(2 * width))
			break;

		if (!left && !right && !room && !ramp_left && !ramp_right && !ramp_room && !binaural)
		{
			/* a voice turned all the way down (out of earshot), and not
			sending to the reverb, only moves on */
		}
		else if (resampling_linear)
		{
			const float *a = stream->history[stream->center % RESAMPLER_HISTORY];
			const float *b = stream->history[(stream->center + 1) % RESAMPLER_HISTORY];
			float fraction = (float)stream->phase;

			sample_left = a[0] + (b[0] - a[0]) * fraction;
			sample_right = a[1] + (b[1] - a[1]) * fraction;
		}
		else if (scale == 1.0f)
		{
			double position = stream->phase * RESAMPLER_TABLE_STEPS;
			unsigned long row = (unsigned long)position;
			float blend = (float)(position - (double)row);
			const float *weights = resampler_phases[row], *next_weights = resampler_phases[row + 1];
			unsigned long first = stream->center + 1 - RESAMPLER_ZERO_CROSSINGS;

			for (tap = 0; tap < 2 * RESAMPLER_ZERO_CROSSINGS; tap++)
			{
				const float *source = stream->history[(first + (unsigned long)tap) % RESAMPLER_HISTORY];
				float weight = weights[tap] + (next_weights[tap] - weights[tap]) * blend;

				sample_left += source[0] * weight;
				sample_right += source[1] * weight;
			}
		}
		else
		{
			for (tap = 1 - width; tap <= width; tap++)
			{
				const float *source = stream->history[(stream->center + (unsigned long)tap) % RESAMPLER_HISTORY];
				float weight = scale * resampler_weight(fabsf((float)tap - (float)stream->phase) * scale * RESAMPLER_TABLE_STEPS);

				sample_left += source[0] * weight;
				sample_right += source[1] * weight;
			}
		}
		/* the room send, with its own low pass */
		if (room || ramp_room)
		{
			float mono = stream->channels == 1 ? sample_left : 0.5f * (sample_left + sample_right);

			stream->room_lowpass = flush_denormal(mono + room_lowpass * (stream->room_lowpass - mono));
			send[frame] += stream->room_lowpass * room;
		}
		/* the direct path, muffled, or as it is; a mono voice's mix bins or pan
		split it across the speakers */
		if (direct_lowpass || ramp_direct_lowpass)
		{
			sample_left = flush_denormal(sample_left + direct_lowpass * (stream->direct_lowpass[0] - sample_left));
			sample_right = flush_denormal(sample_right + direct_lowpass * (stream->direct_lowpass[1] - sample_right));
		}
		stream->direct_lowpass[0] = sample_left;
		stream->direct_lowpass[1] = sample_right;
		if (binaural)
			hrtf_mono[frame] = stream->channels == 1 ? sample_left : 0.5f * (sample_left + sample_right);
		if (surround)
		{
			/* the first row's gains take the left (a mono voice's only)
			channel, the second the right */
			float *speakers = output + frame * output_channels;
			float first = sample_left;
			float second = stream->channels == 2 ? sample_right : 0.0f;

			for (speaker = 0; speaker < output_channels; speaker++)
			{
				speakers[speaker] += first * surround_gains[0][speaker] + second * surround_gains[1][speaker];
				surround_gains[0][speaker] += ramp_surround[0][speaker];
				surround_gains[1][speaker] += ramp_surround[1][speaker];
			}
		}
		else if (stream->channels == 1)
		{
			output[frame * 2] += sample_left * left;
			output[frame * 2 + 1] += sample_left * right;
		}
		else
		{
			output[frame * 2] += sample_left * left;
			output[frame * 2 + 1] += sample_right * right;
		}
		left += ramp_left;
		right += ramp_right;
		room += ramp_room;
		direct_lowpass += ramp_direct_lowpass;
		room_lowpass += ramp_room_lowpass;
		stream->phase += step;
		step += ramp_step;
		while (stream->phase >= 1.0)
		{
			stream->phase -= 1.0;
			stream->center++;
		}
	}
	if (binaural && frame)
		hrtf_mix(stream, hrtf_mono, output, frame, target_hrtf, hrtf_filters, hrtf_delays);
	else if (!binaural && stream->hrtf_valid)
		hrtf_reset(stream);
	stream->current_left = target_left;
	stream->current_right = target_right;
	stream->current_room = target_room;
	stream->current_direct_lowpass = target_direct_lowpass;
	stream->current_room_lowpass = target_room_lowpass;
	if (surround)
		memcpy(stream->current_surround, target_surround, sizeof(target_surround));
}

/* ---------- reverb

The Xbox ran an I3DL2 reverb on its audio DSP (the effects image the game
downloads), fed by every 3D voice's I3DL2 mix bin. The game sets the listener
properties from the sound environment the camera is in (sound_dsound_xbox.c
dsound_set_listener_properties), and each 3D voice's room send from its
reverb attenuation and occlusion (dsound_channel_set_I3DL2_properties). This
is a reverb of the I3DL2 model on those properties, not the DSP's program:
	- each 3D voice sends to a mono bus at the listener's and its own room
	  levels, occlusion included, their high frequency levels a low pass at the
	  HF reference, attenuated with distance by the room rolloff factors
	  (voice_gains);
	- the early reflections are REVERB_TAPS taps of the bus, starting the
	  reflections delay later, at the reflections level;
	- the late reverberation starts the reverb delay after the reflections:
	  all-pass diffusers as strong as the diffusion, then a feedback delay
	  network of REVERB_LINES lines, as long as the density makes them, each
	  losing what decays it by 60 dB in the decay time, and its high
	  frequencies in the decay time times the HF ratio, at the reverb level.
Both are normalized to the energy sent, so their levels are relative to the
room level, as I3DL2 gives them. A change of environment crossfades each
delay's read from its old length to its new one, and each gain, over
REVERB_FADE_FRAMES: what is reverberating carries on, without the click of a
jump or the pitch shift of a sliding delay. audio.reverb = false fades the
reverb out over as long and stops it. Once nothing has been sent for
REVERB_QUIET_FRAMES and the reverberation is 120 dB down, the reverb stops
until something is sent again (outdoors, in the menus). */

#define REVERB_DELAY_SIZE 32768 /* the reflections and reverb delays, 0.3 + 0.1 s at most, and the taps */
#define REVERB_TAPS 6
#define REVERB_DIFFUSERS 4
#define REVERB_DIFFUSER_SIZE 1024
#define REVERB_LINES 8
#define REVERB_LINE_SIZE 4096
#define REVERB_FADE_FRAMES 4096 /* 85 ms */
/* the input delays, then the diffusers, played out */
#define REVERB_QUIET_FRAMES (REVERB_DELAY_SIZE + REVERB_DIFFUSERS * REVERB_DIFFUSER_SIZE)
#define REVERB_QUIET_PEAK 1.0e-6f

/* in milliseconds: the taps after the reflections delay, the diffusers, and
the lines at full density */
static const float reverb_tap_times[REVERB_TAPS] = { 0.0f, 3.1f, 5.3f, 7.9f, 11.2f, 14.9f };
static const float reverb_tap_signs[REVERB_TAPS] = { 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f };
static const float reverb_diffuser_times[REVERB_DIFFUSERS] = { 4.7f, 3.6f, 12.7f, 9.3f };
static const float reverb_line_times[REVERB_LINES] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 73.1f };
/* which lines go in negated, and the two outputs' signs of the lines */
static const float reverb_input_signs[REVERB_LINES] = { 1, -1, -1, 1, 1, -1, -1, 1 };
static const float reverb_left_signs[REVERB_LINES] = { 1, -1, 1, -1, 1, -1, 1, -1 };
static const float reverb_right_signs[REVERB_LINES] = { 1, 1, -1, -1, 1, 1, -1, -1 };
/* on surround output, the reverb plays from every speaker 3D voices pan
between (surround_speakers), as the Xbox's did from its four: each output
another row of the 8 x 8 Hadamard matrix, the stereo pair's first,
orthogonal to each other, so that each speaker's reverberation is unlike
its neighbors' and the room surrounds the listener. In the order of
reverb_outputs */
static const float reverb_surround_signs[SURROUND_71_SPEAKERS][REVERB_LINES] =
{
	{ 1, -1, 1, -1, 1, -1, 1, -1 },
	{ 1, 1, -1, -1, 1, 1, -1, -1 },
	{ 1, 1, 1, 1, -1, -1, -1, -1 },
	{ 1, -1, 1, -1, -1, 1, -1, 1 },
	{ 1, 1, -1, -1, -1, -1, 1, 1 },
	{ 1, -1, -1, 1, -1, 1, 1, -1 },
};
/* the output channels of those rows: the front pair, the back pair, the
sides (7.1) */
static const unsigned long reverb_outputs_51[SURROUND_51_SPEAKERS] = { 0, 1, 4, 5 };
static const unsigned long reverb_outputs_71[SURROUND_71_SPEAKERS] = { 0, 1, 4, 5, 6, 7 };

/* what an environment sets: delays in samples, and gains */
struct reverb_parameters
{
	unsigned long taps[REVERB_TAPS];
	unsigned long late_delay;
	unsigned long lengths[REVERB_LINES];
	/* each line's gain a pass, and its low pass's coefficient */
	float feedback[REVERB_LINES];
	float damping[REVERB_LINES];
	float reflections_gain, late_gain;
	float diffusion;
};

static struct
{
	/* the bus of the current mix */
	float send[MIX_CHUNK_FRAMES];
	/* the environment serial and properties of the parameters faded to */
	unsigned long serial;
	DSI3DL2LISTENER properties;
	/* the parameters fading from and to, and the frames of the fade left
	(none: from is to) */
	struct reverb_parameters from, to;
	unsigned long fade;
	/* the output's level, 1 while audio.reverb is on, moving to 0 when it
	is turned off; whether the buffers are silent; and the frames since the
	send or the output was last over REVERB_QUIET_PEAK */
	float level;
	BOOL silent;
	unsigned long quiet;

	/* the bus, delayed for the taps and the late reverberation */
	float delay[REVERB_DELAY_SIZE];
	unsigned long delay_position;

	float diffusers[REVERB_DIFFUSERS][REVERB_DIFFUSER_SIZE];
	unsigned long diffuser_lengths[REVERB_DIFFUSERS];
	unsigned long diffuser_position;

	float lines[REVERB_LINES][REVERB_LINE_SIZE];
	unsigned long line_position;
	/* each line's low pass */
	float lowpass[REVERB_LINES];
} reverb;

static float clamp_real(float value, float minimum, float maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

/* whole samples */
static unsigned long reverb_samples(float seconds)
{
	return (unsigned long)floorf(seconds * OUTPUT_RATE + 0.5f);
}

/* the sample buffer got delay samples before position, crossfaded from the
from delay to the to delay by fade (0 to 1) */
static float reverb_read(const float *buffer, unsigned long mask, unsigned long position,
	unsigned long from_delay, unsigned long to_delay, float fade)
{
	float from_sample = buffer[(position - from_delay) & mask];

	if (from_delay == to_delay)
		return from_sample;
	return from_sample + (buffer[(position - to_delay) & mask] - from_sample) * fade;
}

static float reverb_blend(float from, float to, float fade)
{
	return from + (to - from) * fade;
}

static void reverb_parameters_set(struct reverb_parameters *parameters, const DSI3DL2LISTENER *properties)
{
	float decay = clamp_real(properties->flDecayTime, 0.1f, 20.0f);
	/* a low pass loses high frequencies; a ratio over 1 would need them to
	last longer */
	float high_decay = decay * clamp_real(properties->flDecayHFRatio, 0.1f, 1.0f);
	float scale = 0.25f + 0.75f * clamp_real(properties->flDensity / 100.0f, 0.0f, 1.0f);
	float cosine = frequency_cosine(properties->flHFReference);
	float mean_length = 0.0f, mean_feedback;
	unsigned long reflections_delay;
	int index;

	for (index = 0; index < REVERB_LINES; index++)
	{
		float length = floorf(reverb_line_times[index] * 0.001f * OUTPUT_RATE * scale + 0.5f);
		float high_feedback;

		parameters->lengths[index] = (unsigned long)length;
		parameters->feedback[index] = powf(10.0f, -3.0f * length / (decay * OUTPUT_RATE));
		high_feedback = powf(10.0f, -3.0f * length / (high_decay * OUTPUT_RATE));
		parameters->damping[index] = lowpass_coefficient(high_feedback / parameters->feedback[index], cosine);
		mean_length += length / REVERB_LINES;
	}
	mean_feedback = powf(10.0f, -3.0f * mean_length / (decay * OUTPUT_RATE));
	reflections_delay = reverb_samples(clamp_real(properties->flReflectionsDelay, 0.0f, 0.3f));
	for (index = 0; index < REVERB_TAPS; index++)
		parameters->taps[index] = reflections_delay + reverb_samples(reverb_tap_times[index] * 0.001f);
	parameters->late_delay = reflections_delay + reverb_samples(clamp_real(properties->flReverbDelay, 0.0f, 0.1f));
	/* the taps' energy, half of them to each side; the network's, whose
	sound circulates until it decays */
	parameters->reflections_gain = gain_from_millibels(properties->lReflections) /
		sqrtf((float)REVERB_TAPS / OUTPUT_CHANNELS);
	parameters->late_gain = gain_from_millibels(properties->lReverb) * sqrtf(1.0f - mean_feedback * mean_feedback);
	parameters->diffusion = 0.7f * clamp_real(properties->flDiffusion / 100.0f, 0.0f, 1.0f);
}

/* a new environment, faded to unless the reverb is silent. The game sets the
same one again several times a second in some places (Derelict), which
changes nothing */
static void reverb_update(const DSI3DL2LISTENER *properties, unsigned long serial)
{
	reverb.serial = serial;
	if (!reverb.silent && !memcmp(properties, &reverb.properties, sizeof(*properties)))
		return;
	reverb.properties = *properties;
	reverb_parameters_set(&reverb.to, properties);
	if (reverb.silent)
		reverb.from = reverb.to;
	reverb.fade = reverb.silent ? 0 : REVERB_FADE_FRAMES;
}

static void reverb_clear(void)
{
	memset(reverb.delay, 0, sizeof(reverb.delay));
	memset(reverb.diffusers, 0, sizeof(reverb.diffusers));
	memset(reverb.lines, 0, sizeof(reverb.lines));
	memset(reverb.lowpass, 0, sizeof(reverb.lowpass));
	reverb.from = reverb.to;
	reverb.fade = 0;
	reverb.silent = TRUE;
	reverb.quiet = 0;
}

static void reverb_initialize(void)
{
	int index;

	for (index = 0; index < REVERB_DIFFUSERS; index++)
		reverb.diffuser_lengths[index] = reverb_samples(reverb_diffuser_times[index] * 0.001f);
	reverb.silent = TRUE;
	reverb.level = reverb_enabled ? 1.0f : 0.0f;
	reverb_update(&environment, environment_serial);
}

/* adds the reverberation of send to output, its level moving to
target_level */
static void reverb_process(const float *send, float *output, unsigned long frames, float target_level)
{
	const struct reverb_parameters *from = &reverb.from, *to = &reverb.to;
	float input_scale = 1.0f / sqrtf((float)REVERB_LINES), peak = 0.0f;
	/* the outputs (on stereo, the left and right channels); the taps are
	shared out between them, and each plays the late reverberation at its
	share of the stereo pair's power */
	static const unsigned long stereo_outputs[2] = { 0, 1 };
	unsigned long reverb_count = output_channels == 8 ? SURROUND_71_SPEAKERS :
		output_channels == 6 ? SURROUND_51_SPEAKERS : 2;
	const unsigned long *reverb_outputs = output_channels == 8 ? reverb_outputs_71 :
		output_channels == 6 ? reverb_outputs_51 : stereo_outputs;
	float output_scale = sqrtf(2.0f / (float)reverb_count);
	unsigned long frame;
	int index;

	reverb.silent = FALSE;
	for (frame = 0; frame < frames; frame++)
	{
		/* how far the fade from the old environment to the new is */
		float fade = 1.0f - (float)reverb.fade / REVERB_FADE_FRAMES;
		float early[SURROUND_71_SPEAKERS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
		float late[SURROUND_71_SPEAKERS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
		float outputs[REVERB_LINES], feedback_sum = 0.0f, value;
		float diffusion = reverb_blend(from->diffusion, to->diffusion, fade);

		reverb.delay[reverb.delay_position & (REVERB_DELAY_SIZE - 1)] = send[frame];
		for (index = 0; index < REVERB_TAPS; index++)
		{
			early[index % reverb_count] += reverb_tap_signs[index] * reverb_read(reverb.delay, REVERB_DELAY_SIZE - 1,
				reverb.delay_position, from->taps[index], to->taps[index], fade);
		}

		/* diffusers: w = x + g w', y = w' - g w */
		value = reverb_read(reverb.delay, REVERB_DELAY_SIZE - 1, reverb.delay_position,
			from->late_delay, to->late_delay, fade);
		reverb.delay_position++;
		for (index = 0; index < REVERB_DIFFUSERS; index++)
		{
			float *buffer = reverb.diffusers[index];
			float delayed = buffer[(reverb.diffuser_position - reverb.diffuser_lengths[index]) & (REVERB_DIFFUSER_SIZE - 1)];
			float written = flush_denormal(value + diffusion * delayed);

			buffer[reverb.diffuser_position & (REVERB_DIFFUSER_SIZE - 1)] = written;
			value = delayed - diffusion * written;
		}
		reverb.diffuser_position++;

		/* the network: each line's output is damped and heard, then decayed
		and mixed back into every line by a Householder reflection */
		for (index = 0; index < REVERB_LINES; index++)
		{
			float line_output = reverb_read(reverb.lines[index], REVERB_LINE_SIZE - 1, reverb.line_position,
				from->lengths[index], to->lengths[index], fade);
			float damping = reverb_blend(from->damping[index], to->damping[index], fade);

			reverb.lowpass[index] = flush_denormal(line_output + damping * (reverb.lowpass[index] - line_output));
			if (reverb_count == 2)
			{
				late[0] += reverb_left_signs[index] * reverb.lowpass[index];
				late[1] += reverb_right_signs[index] * reverb.lowpass[index];
			}
			else
			{
				unsigned long row;

				for (row = 0; row < reverb_count; row++)
					late[row] += reverb_surround_signs[row][index] * reverb.lowpass[index];
			}
			outputs[index] = reverb.lowpass[index] * reverb_blend(from->feedback[index], to->feedback[index], fade);
			feedback_sum += outputs[index];
		}
		feedback_sum *= 2.0f / REVERB_LINES;
		for (index = 0; index < REVERB_LINES; index++)
		{
			reverb.lines[index][reverb.line_position & (REVERB_LINE_SIZE - 1)] =
				flush_denormal(outputs[index] - feedback_sum + value * reverb_input_signs[index] * input_scale);
		}
		reverb.line_position++;

		{
			float reflections_gain = reverb_blend(from->reflections_gain, to->reflections_gain, fade);
			float late_gain = reverb_blend(from->late_gain, to->late_gain, fade);
			unsigned long row;

			for (row = 0; row < reverb_count; row++)
			{
				float sample = early[row] * reflections_gain + late[row] * late_gain * output_scale;

				output[frame * output_channels + reverb_outputs[row]] += sample * reverb.level;
				peak = fabsf(sample) > peak ? fabsf(sample) : peak;
			}
			peak = fabsf(send[frame]) > peak ? fabsf(send[frame]) : peak;
		}
		if (reverb.fade && !--reverb.fade)
			reverb.from = reverb.to;
		reverb.level = clamp_real(target_level, reverb.level - 1.0f / REVERB_FADE_FRAMES,
			reverb.level + 1.0f / REVERB_FADE_FRAMES);
	}
	reverb.quiet = peak > REVERB_QUIET_PEAK ? 0 : reverb.quiet + frames;
}

/* whether anything is sent to the reverb in send */
static BOOL reverb_sent(const float *send, unsigned long frames)
{
	unsigned long frame;

	for (frame = 0; frame < frames; frame++)
	{
		if (fabsf(send[frame]) > REVERB_QUIET_PEAK)
			return TRUE;
	}
	return FALSE;
}

/* ---------- limiter

The game sets its mix bins' headroom to 0 (sound_dsound_xbox.c), so the voices
sum at their full level, as on the Xbox, and a pile of loud ones goes over
full scale. Clipping each sample, or bending it near full scale, distorts the
sound: dialogue over gunfire crackled. Instead the whole mix is turned down
for as long as it would go over, both channels alike. The output is delayed
LIMITER_LOOKAHEAD - 1 frames (1.3 ms), so that the gain comes down smoothly
before each peak: the smallest gain the frames ahead need, averaged over the
last LIMITER_LOOKAHEAD frames, is never more than a peak needs when it plays.
The gain comes back up over LIMITER_RELEASE_SECONDS. */

#define LIMITER_CEILING 0.891f /* -1 dBFS */
#define LIMITER_LOOKAHEAD 64
#define LIMITER_RELEASE_SECONDS 0.1f

static struct
{
	/* the frames the output is delayed by, and the gain each needs */
	float delay[LIMITER_LOOKAHEAD][MAXIMUM_OUTPUT_CHANNELS];
	float needed[LIMITER_LOOKAHEAD];
	/* the gain held down to what the frames ahead need, coming back up */
	float held;
	/* its last LIMITER_LOOKAHEAD values, and their sum */
	float history[LIMITER_LOOKAHEAD];
	double history_sum;
	unsigned long position;
	BOOL initialized;
} limiter;

static void limit(float *output, unsigned long frames)
{
	float release = 1.0f - expf(-1.0f / (LIMITER_RELEASE_SECONDS * OUTPUT_RATE));
	unsigned long frame, index, channel;

	if (!limiter.initialized)
	{
		for (index = 0; index < LIMITER_LOOKAHEAD; index++)
		{
			limiter.needed[index] = 1.0f;
			limiter.history[index] = 1.0f;
		}
		limiter.held = 1.0f;
		limiter.history_sum = LIMITER_LOOKAHEAD;
		limiter.initialized = TRUE;
	}
	for (frame = 0; frame < frames; frame++)
	{
		float *sample = output + frame * output_channels;
		unsigned long position = limiter.position;
		unsigned long oldest = (position + 1) % LIMITER_LOOKAHEAD;
		float peak = 0.0f, lowest, gain;

		/* the new frame takes the slot of the oldest, which has played */
		for (channel = 0; channel < output_channels; channel++)
		{
			if (fabsf(sample[channel]) > peak)
				peak = fabsf(sample[channel]);
			limiter.delay[position][channel] = sample[channel];
		}
		limiter.needed[position] = peak > LIMITER_CEILING ? LIMITER_CEILING / peak : 1.0f;
		lowest = limiter.needed[0];
		for (index = 1; index < LIMITER_LOOKAHEAD; index++)
		{
			if (limiter.needed[index] < lowest)
				lowest = limiter.needed[index];
		}
		if (lowest < limiter.held)
			limiter.held = lowest;
		else
			limiter.held += (lowest - limiter.held) * release;
		limiter.history_sum += limiter.held - limiter.history[position];
		limiter.history[position] = limiter.held;
		gain = (float)(limiter.history_sum / LIMITER_LOOKAHEAD);
		/* the frame LIMITER_LOOKAHEAD - 1 frames old plays */
		for (channel = 0; channel < output_channels; channel++)
			sample[channel] = limiter.delay[oldest][channel] * gain;
		limiter.position = oldest;
	}
}

/* ---------- recording

What the mixer plays, for the audio probe (port/linux/game/audio_probe.c,
audio_probe.h): <prefix>-final.wav is the output as it goes to the device,
<prefix>-dry.wav the voices' mix before the reverb, <prefix>-reverb.wav the
reverb's output (the reflections and the late reverberation, at the reverb's
level), each 32-bit float at OUTPUT_RATE, as many channels as the output's (stereo or
5.1). The final mix comes through
the limiter, LIMITER_LOOKAHEAD - 1 frames behind the other two. The mixer
puts what it mixes in a ring; the game's thread writes it out
(audio_capture_update), so the audio thread never waits on a file. */

#define CAPTURE_BUSES 3
#define CAPTURE_RING_FRAMES (OUTPUT_RATE * 4)

static const char *const capture_bus_names[CAPTURE_BUSES] = { "final", "dry", "reverb" };

static pthread_mutex_t capture_lock = PTHREAD_MUTEX_INITIALIZER;
static struct
{
	volatile BOOL on;
	/* CAPTURE_BUSES rings of CAPTURE_RING_FRAMES frames */
	float *ring;
	/* frames put in the rings and taken out, from the start */
	unsigned long put, taken;
	/* frames mixed since the start (put, and any the full rings lost) */
	unsigned long frames;
	unsigned long lost;
	/* the output's channels when the recording started */
	unsigned long channels;
	FILE *files[CAPTURE_BUSES];
	unsigned long file_frames;
	/* the mixer's (mix) */
	float dry[MIX_CHUNK_FRAMES * MAXIMUM_OUTPUT_CHANNELS];
	float reverb[MIX_CHUNK_FRAMES * MAXIMUM_OUTPUT_CHANNELS];
} capture;

static void capture_push(const float *output, unsigned long frames)
{
	const float *buses[CAPTURE_BUSES];
	unsigned long bus, frame, channel;

	buses[0] = output;
	buses[1] = capture.dry;
	buses[2] = capture.reverb;
	pthread_mutex_lock(&capture_lock);
	/* (stopped since this mix began) */
	if (!capture.on)
	{
		pthread_mutex_unlock(&capture_lock);
		return;
	}
	if (capture.put - capture.taken + frames > CAPTURE_RING_FRAMES)
	{
		capture.lost += frames;
	}
	else
	{
		for (frame = 0; frame < frames; frame++)
		{
			unsigned long slot = (capture.put + frame) % CAPTURE_RING_FRAMES;

			for (bus = 0; bus < CAPTURE_BUSES; bus++)
			{
				float *ring = capture.ring + (bus * CAPTURE_RING_FRAMES + slot) * capture.channels;

				for (channel = 0; channel < capture.channels; channel++)
					ring[channel] = buses[bus][frame * capture.channels + channel];
			}
		}
		capture.put += frames;
	}
	capture.frames += frames;
	pthread_mutex_unlock(&capture_lock);
}

static void capture_write_u32(FILE *file, unsigned long value)
{
	unsigned char bytes[4];

	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
	fwrite(bytes, 1, 4, file);
}

static void capture_write_u16(FILE *file, unsigned int value)
{
	unsigned char bytes[2];

	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	fwrite(bytes, 1, 2, file);
}

/* a WAVE header of 32-bit float frames */
static void capture_write_header(FILE *file, unsigned long frames)
{
	unsigned long data_bytes = frames * capture.channels * sizeof(float);

	fwrite("RIFF", 1, 4, file);
	capture_write_u32(file, 4 + 8 + 18 + 8 + data_bytes);
	fwrite("WAVEfmt ", 1, 8, file);
	capture_write_u32(file, 18);
	capture_write_u16(file, 3); /* WAVE_FORMAT_IEEE_FLOAT */
	capture_write_u16(file, capture.channels);
	capture_write_u32(file, OUTPUT_RATE);
	capture_write_u32(file, OUTPUT_RATE * capture.channels * sizeof(float));
	capture_write_u16(file, capture.channels * sizeof(float));
	capture_write_u16(file, 32);
	capture_write_u16(file, 0);
	fwrite("data", 1, 4, file);
	capture_write_u32(file, data_bytes);
}

int audio_capture_start(char const *path_prefix)
{
	unsigned long bus;

	if (capture.on)
		return 0;
	capture.channels = output_channels;
	capture.ring = calloc((size_t)CAPTURE_BUSES * CAPTURE_RING_FRAMES * capture.channels, sizeof(float));
	if (!capture.ring)
		return 0;
	for (bus = 0; bus < CAPTURE_BUSES; bus++)
	{
		char name[512], path[1024];

		/* (the probe's path is the game's, an Xbox path) */
		snprintf(name, sizeof(name), "%s-%s.wav", path_prefix, capture_bus_names[bus]);
		platform_translate_path(name, path, sizeof(path));
		capture.files[bus] = fopen(path, "wb");
		if (!capture.files[bus])
		{
			platform_log("audio probe: cannot write %s", path);
			while (bus-- > 0)
				fclose(capture.files[bus]);
			free(capture.ring);
			capture.ring = NULL;
			return 0;
		}
		capture_write_header(capture.files[bus], 0);
	}
	pthread_mutex_lock(&capture_lock);
	capture.put = capture.taken = capture.frames = capture.lost = 0;
	capture.file_frames = 0;
	capture.on = TRUE;
	pthread_mutex_unlock(&capture_lock);
	platform_log("audio probe: recording the mixer (audio.volume %.2f, reverb %s, %s resampling, the final mix %d "
		"frames behind the buses)", master_volume, reverb_enabled ? "on" : "off",
		resampling_linear ? "linear" : "sinc", LIMITER_LOOKAHEAD - 1);
	return 1;
}

unsigned long audio_capture_frames(void)
{
	unsigned long frames;

	pthread_mutex_lock(&capture_lock);
	frames = capture.frames;
	pthread_mutex_unlock(&capture_lock);
	return frames;
}

unsigned long audio_capture_rate(void)
{
	return OUTPUT_RATE;
}

char const *audio_capture_platform(void)
{
	return "pc";
}

void audio_capture_update(void)
{
	unsigned long put, bus;

	if (!capture.ring)
		return;
	pthread_mutex_lock(&capture_lock);
	put = capture.put;
	pthread_mutex_unlock(&capture_lock);
	/* (the frames between taken and put are the mixer's no longer) */
	while (capture.taken != put)
	{
		unsigned long slot = capture.taken % CAPTURE_RING_FRAMES;
		unsigned long count = put - capture.taken;

		if (count > CAPTURE_RING_FRAMES - slot)
			count = CAPTURE_RING_FRAMES - slot;
		for (bus = 0; bus < CAPTURE_BUSES; bus++)
			fwrite(capture.ring + (bus * CAPTURE_RING_FRAMES + slot) * capture.channels, sizeof(float) * capture.channels,
				count, capture.files[bus]);
		capture.file_frames += count;
		pthread_mutex_lock(&capture_lock);
		capture.taken += count;
		pthread_mutex_unlock(&capture_lock);
	}
}

void audio_capture_stop(void)
{
	unsigned long bus;

	if (!capture.on)
		return;
	pthread_mutex_lock(&capture_lock);
	capture.on = FALSE;
	pthread_mutex_unlock(&capture_lock);
	audio_capture_update();
	for (bus = 0; bus < CAPTURE_BUSES; bus++)
	{
		fseek(capture.files[bus], 0, SEEK_SET);
		capture_write_header(capture.files[bus], capture.file_frames);
		fclose(capture.files[bus]);
		capture.files[bus] = NULL;
	}
	if (capture.lost)
		platform_log("audio probe: the recording lost %lu frames (the game's thread was held up); what follows is "
			"that much early", capture.lost);
	platform_log("audio probe: recorded %lu frames", capture.file_frames);
	free(capture.ring);
	capture.ring = NULL;
}

/* hrtf_level moved towards audio.hrtf's (hrtf_update) */
static void hrtf_fade(unsigned long frames)
{
	float target = hrtf_wanted && hrtf.loaded && output_channels == 2 ? 1.0f : 0.0f;
	float step = (float)frames / (HRTF_FADE_SECONDS * OUTPUT_RATE);

	hrtf_level = clamp_real(target, hrtf_level - step, hrtf_level + step);
}

static void mix(float *output, unsigned long frames)
{
	struct sdl_stream *stream;
	DSI3DL2LISTENER properties;
	BOOL enabled = reverb_enabled, changed = FALSE;
	BOOL capturing = capture.on && frames <= MIX_CHUNK_FRAMES;
	unsigned long serial = 0;

	memset(output, 0, frames * output_channels * sizeof(float));
	memset(reverb.send, 0, frames * sizeof(float));
	hrtf_fade(frames);
	pthread_mutex_lock(&mixer_lock);
	/* (a voice at a time, the game let in between them: mixer_lock_wanted) */
	for (stream = streams; stream; stream = mix_cursor)
	{
		mix_voice(stream, output, reverb.send, frames);
		mix_cursor = stream->next;
		if (mix_cursor && __atomic_load_n(&mixer_lock_wanted, __ATOMIC_RELAXED))
		{
			pthread_mutex_unlock(&mixer_lock);
			sched_yield();
			pthread_mutex_lock(&mixer_lock);
		}
	}
	mix_cursor = NULL;
	for (stream = buffers; stream; stream = stream->next)
		mix_voice(stream, output, reverb.send, frames);
	/* a new environment, once the fade to the last is over */
	if (reverb.serial != environment_serial && !reverb.fade)
	{
		properties = environment;
		serial = environment_serial;
		changed = TRUE;
	}
	pthread_mutex_unlock(&mixer_lock);
	if (changed)
		reverb_update(&properties, serial);
	if (capturing)
		memcpy(capture.dry, output, frames * capture.channels * sizeof(float));
	/* the reverb runs while something reverberates, and stops once it is
	turned off and faded out, or quiet */
	if (!reverb.silent || (enabled && reverb_sent(reverb.send, frames)))
	{
		reverb_process(reverb.send, output, frames, enabled ? 1.0f : 0.0f);
		if ((!enabled && reverb.level <= 0.0f) || reverb.quiet > REVERB_QUIET_FRAMES)
			reverb_clear();
	}
	if (capturing)
	{
		unsigned long index;

		for (index = 0; index < frames * capture.channels; index++)
			capture.reverb[index] = output[index] - capture.dry[index];
	}
	limit(output, frames);
	if (capturing)
		capture_push(output, frames);
}

/* ---------- output */

static SDL_AudioStream *audio_stream;
static BOOL audio_started = FALSE;
static BOOL audio_silent_clock = FALSE;
static SDL_AudioSpec audio_spec;
/* audio.device as the stream was opened for it ("" for the system's) */
static char audio_device_name[256];
/* the output it plays on (SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK for the system's) */
static SDL_AudioDeviceID audio_device = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
	float buffer[MIX_CHUNK_FRAMES * MAXIMUM_OUTPUT_CHANNELS];

	(void)userdata;
	(void)total_amount;
	while (additional_amount > 0)
	{
		unsigned long frames = (unsigned long)additional_amount / (output_channels * sizeof(float));

		if (frames > MIX_CHUNK_FRAMES)
			frames = MIX_CHUNK_FRAMES;
		if (!frames)
			frames = 1;
		mix(buffer, frames);
		SDL_PutAudioStreamData(stream, buffer, (int)(frames * output_channels * sizeof(float)));
		additional_amount -= (int)(frames * output_channels * sizeof(float));
	}
}

/* without a device, drain voices in real time */
static void *silent_clock_thread(void *parameter)
{
	float buffer[480 * MAXIMUM_OUTPUT_CHANNELS];
	struct timespec next;

	(void)parameter;
	clock_gettime(CLOCK_MONOTONIC, &next);
	/* (until the game's end asks the platform layer's threads to stop,
	thread_stop.h) */
	while (!thread_stop_requested())
	{
		/* (stopped while a stream opened later, on another audio.device,
		plays) */
		if (!audio_stream)
		{
			/* (10 ms, in chunks of MIX_CHUNK_FRAMES at most) */
			mix(buffer, 240);
			mix(buffer, 240);
		}
		next.tv_nsec += 10000000L;
		if (next.tv_nsec >= 1000000000L)
		{
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		platform_sleep_until(&next);
	}
	return NULL;
}

/* the playback device named so (SDL's name for it), else the system's
default output, which SDL moves to wherever the system's default goes.
audio.device chooses the game's; a voice output of its own, later, would
open its stream on audio.voice_device's the same way. */
static SDL_AudioDeviceID audio_device_named(char const *name)
{
	SDL_AudioDeviceID found = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
	SDL_AudioDeviceID *devices;
	int count = 0, index;

	if (!name || !name[0] || !strcmp(name, "default"))
		return found;
	devices = SDL_GetAudioPlaybackDevices(&count);
	for (index = 0; devices && index < count; index++)
	{
		char const *device_name = SDL_GetAudioDeviceName(devices[index]);

		if (device_name && !strcmp(device_name, name))
		{
			found = devices[index];
			break;
		}
	}
	SDL_free(devices);
	if (found == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
		platform_log("audio: there is no output named \"%s\"; the system's default plays", name);
	return found;
}

/* the playback devices' names (for Audio Setup's Output Device: the
menus' copies of them), at most maximum; 0 without SDL's audio */
int platform_audio_device_names(char const **names, int maximum)
{
	static char stored[32][256];
	SDL_AudioDeviceID *devices;
	int count = 0, index, written = 0;

	if (!config_boolean("audio.enabled") || !platform_sdl_initialize())
		return 0;
	devices = SDL_GetAudioPlaybackDevices(&count);
	for (index = 0; devices && index < count && written < maximum && written < 32; index++)
	{
		char const *name = SDL_GetAudioDeviceName(devices[index]);

		if (!name || !name[0])
			continue;
		snprintf(stored[written], sizeof(stored[written]), "%s", name);
		names[written] = stored[written];
		written++;
	}
	SDL_free(devices);
	return written;
}

static void audio_silent_clock_start(void)
{
	if (audio_silent_clock)
		return;
	audio_silent_clock = TRUE;
	thread_stop_create("silent audio clock", silent_clock_thread, NULL);
}

/* the stream opened on audio.device's output (else the default's); FALSE
when none opens */
static BOOL audio_stream_open(void)
{
	char const *name = config_string("audio.device");

	snprintf(audio_device_name, sizeof(audio_device_name), "%s", name);
	audio_device = audio_device_named(name);
	audio_stream = SDL_OpenAudioDeviceStream(audio_device, &audio_spec, audio_callback, NULL);
	if (!audio_stream && audio_device != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
	{
		platform_log("audio: cannot open \"%s\" (%s); the system's default plays", name, SDL_GetError());
		audio_device = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
		audio_stream = SDL_OpenAudioDeviceStream(audio_device, &audio_spec, audio_callback, NULL);
	}
	if (!audio_stream)
		return FALSE;
	SDL_ResumeAudioStreamDevice(audio_stream);
	return TRUE;
}

/* audio.device changed (Audio Setup's OK): the stream closed (which waits
out the mixer's callback) and opened on the new one, on the game's thread */
static void audio_device_update(void)
{
	char const *name = config_string("audio.device");

	if (!audio_stream || !strcmp(name, audio_device_name))
		return;
	if (audio_device_named(name) == audio_device)
	{
		/* the same output (an unknown name plays on the default) */
		snprintf(audio_device_name, sizeof(audio_device_name), "%s", name);
		return;
	}
	SDL_DestroyAudioStream(audio_stream);
	audio_stream = NULL;
	if (!audio_stream_open())
	{
		platform_log("cannot open an audio device (%s); sound is silent", SDL_GetError());
		audio_silent_clock_start();
	}
}

/* whether the output's name says it is headphones (a headset, earbuds),
for audio.hrtf = "auto": SDL does not say what is plugged in, but the
systems name a headphone output so (macOS "External Headphones", AirPods,
PipeWire's and Windows' "Headphones", a USB or Bluetooth headset) */
static BOOL audio_output_is_headphones(char *name, unsigned long name_size)
{
	static const char *const words[] = { "headphone", "headset", "airpods", "earbuds", "earphone", "buds", "beats" };
	char lowered[256];
	const char *device_name;
	unsigned long index;

	name[0] = '\0';
	if (!audio_stream)
		return FALSE;
	device_name = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(audio_stream));
	if (!device_name)
		return FALSE;
	snprintf(name, name_size, "%s", device_name);
	for (index = 0; device_name[index] && index + 1 < sizeof(lowered); index++)
		lowered[index] = (char)(device_name[index] >= 'A' && device_name[index] <= 'Z' ? device_name[index] + 32 :
			device_name[index]);
	lowered[index] = '\0';
	for (index = 0; index < sizeof(words) / sizeof(words[0]); index++)
	{
		if (strstr(lowered, words[index]))
			return TRUE;
	}
	return FALSE;
}

/* audio.hrtf read again (the game's thread): "on", "off", or "auto" for on
when the output is headphones, checked again as the output changes; the
responses loaded the first time they are wanted, here, not on the mixer's
thread */
static BOOL hrtf_automatic = FALSE;

static void hrtf_update(void)
{
	static int logged = -1;
	const char *setting = config_string("audio.hrtf");
	char name[256] = "";
	BOOL wanted;

	hrtf_automatic = !strcmp(setting, "auto");
	if (!strcmp(setting, "on"))
		wanted = TRUE;
	else if (!strcmp(setting, "auto"))
		wanted = audio_output_is_headphones(name, sizeof(name));
	else
		wanted = FALSE;
	if (wanted && !hrtf_load())
	{
		if (logged != 2)
			platform_log("audio: the HRTF data is missing; 3D sounds are panned");
		logged = 2;
		wanted = FALSE;
	}
	if (wanted && output_channels != 2)
		wanted = FALSE;
	if ((int)wanted != logged && logged != 2)
	{
		platform_log("audio: binaural 3D sound (audio.hrtf \"%s\") %s%s%s%s", setting, wanted ? "on" : "off",
			name[0] ? " (output \"" : "", name, name[0] ? "\")" : "");
		logged = (int)wanted;
	}
	hrtf_wanted = wanted;
}

/* audio.channels: "stereo", "5.1", "7.1", or "auto": 5.1 when the device
has six (or seven) channels, 7.1 when it has eight, else stereo. A device
of some other count is an interface's outputs more likely than speakers
around the room, so it plays stereo. Android plays stereo */
static unsigned long choose_output_channels(SDL_AudioDeviceID device)
{
#if defined(HALO_ANDROID) || defined(HALO_WEB) || defined(HALO_ARM64_GUEST)
	(void)device;
	return 2;
#else
	const char *setting = config_string("audio.channels");
	SDL_AudioSpec format;

	if (!strcmp(setting, "stereo"))
		return 2;
	if (!strcmp(setting, "5.1"))
		return 6;
	if (!strcmp(setting, "7.1"))
		return 8;
	if (strcmp(setting, "auto"))
		platform_log("audio.channels \"%s\" is not \"auto\", \"stereo\", \"5.1\" or \"7.1\"; using auto", setting);
	if (!SDL_GetAudioDeviceFormat(device, &format, NULL) &&
		!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format, NULL))
	{
		return 2;
	}
	platform_log("audio: the output has %d channels", format.channels);
	if (format.channels == 6 || format.channels == 7)
		return 6;
	if (format.channels == 8)
		return 8;
	return 2;
#endif
}

static void audio_start(void)
{
	long buffer_frames;
	char frames[16];

	if (audio_started)
		return;
	audio_started = TRUE;
	master_volume = (float)config_real("audio.volume");
	reverb_enabled = config_boolean("audio.reverb");
	resampling_linear = !strcmp(config_string("audio.resampling"), "linear");
	doppler_enabled = config_boolean("audio.doppler");
	resampler_initialize();
	resampler_phases_initialize();
	reverb_initialize();

	if (config_boolean("audio.enabled") && platform_sdl_initialize())
	{
		output_channels = choose_output_channels(audio_device_named(config_string("audio.device")));
		platform_log("sound: %s", output_channels == 8 ? "7.1 surround" : output_channels == 6 ? "5.1 surround" : "stereo");
		audio_spec.format = SDL_AUDIO_F32;
		audio_spec.channels = (int)output_channels;
		audio_spec.freq = OUTPUT_RATE;
		/* audio.buffer_frames: the device's buffer (port_config.c) */
		buffer_frames = config_integer("audio.buffer_frames");
		if (buffer_frames < 64)
			buffer_frames = 64;
		if (buffer_frames > 8192)
			buffer_frames = 8192;
		snprintf(frames, sizeof(frames), "%ld", buffer_frames);
		SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frames);
		if (audio_stream_open())
			return;
		platform_log("cannot open an audio device (%s); sound is silent", SDL_GetError());
		output_channels = 2;
	}
	audio_silent_clock_start();
}

/* ---------- completion */

static void packet_release(struct voice_packet *entry)
{
	free(entry->samples);
	entry->samples = NULL;
}

/* completes the head packet; called with the lock held, which the game's
callback runs without */
static void stream_complete_head(struct sdl_stream *stream, DWORD status, DWORD completed_size)
{
	struct voice_packet *entry = &stream->packets[stream->packet_head];
	XMEDIAPACKET packet = entry->packet;

	/* (one not played to its end, flushed: the cursor was its place, and the
	mixer may take the next while the lock is let go below) */
	if (!entry->finished)
		stream->cursor = 0;
	packet_release(entry);
	entry->finished = FALSE;
	stream->packet_head = (stream->packet_head + 1) % MAXIMUM_STREAM_PACKETS;
	stream->packet_count--;
	if (packet.pdwCompletedSize)
		*packet.pdwCompletedSize = completed_size;
	if (packet.pdwStatus)
		*packet.pdwStatus = status;
	if (stream->callback)
	{
		pthread_mutex_unlock(&mixer_lock);
		stream->callback(stream->context, packet.pContext, status);
		mixer_lock_take();
	}
	else if (packet.hCompletionEvent)
	{
		SetEvent(packet.hCompletionEvent);
	}
}

/* (on the game's thread each frame: with the mixer mixing, the lock is not
waited for but tried again the next frame, a stream's finished packets told a
frame later - waited for, it was up to an eighth of the Switch's game thread
in a busy match; a fifth frame in a row waits) */
static void streams_complete_finished(void)
{
	static int missed;
	struct sdl_stream *stream;

	if (pthread_mutex_trylock(&mixer_lock) != 0)
	{
		if (++missed < 5)
			return;
		mixer_lock_take();
	}
	missed = 0;
	for (stream = streams; stream; stream = stream->next)
	{
		while (stream->packet_count && stream->packets[stream->packet_head].finished)
			stream_complete_head(stream, XMEDIAPACKET_STATUS_SUCCESS, stream->packets[stream->packet_head].packet.dwMaxSize);
	}
	pthread_mutex_unlock(&mixer_lock);
}

/* ---------- stream interface */

static struct sdl_stream *stream_from_interface(void *stream)
{
	return (struct sdl_stream *)stream;
}

static ULONG STDMETHODCALLTYPE stream_add_reference(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);
	ULONG count;

	mixer_lock_take();
	count = ++stream->reference_count;
	pthread_mutex_unlock(&mixer_lock);
	return count;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object);

static ULONG STDMETHODCALLTYPE stream_release(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);
	struct sdl_stream **link;
	ULONG count;

	mixer_lock_take();
	count = --stream->reference_count;
	pthread_mutex_unlock(&mixer_lock);
	if (count)
		return count;

	stream_flush(object);
	mixer_lock_take();
	for (link = &streams; *link; link = &(*link)->next)
	{
		if (*link == stream)
		{
			*link = stream->next;
			/* (the mixer, between voices, goes on from the next) */
			if (mix_cursor == stream)
				mix_cursor = stream->next;
			break;
		}
	}
	pthread_mutex_unlock(&mixer_lock);
	free(stream);
	return 0;
}

static HRESULT STDMETHODCALLTYPE stream_get_info(IDirectSoundStream *object, LPXMEDIAINFO information)
{
	struct sdl_stream *stream = stream_from_interface(object);

	memset(information, 0, sizeof(*information));
	information->dwFlags = XMO_STREAMF_FIXED_SAMPLE_SIZE | XMO_STREAMF_INPUT_ASYNC;
	information->dwInputSize = stream->adpcm ? XBOX_ADPCM_BLOCK_BYTES * stream->channels : 2 * stream->channels;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get_status(IDirectSoundStream *object, LPDWORD status)
{
	struct sdl_stream *stream = stream_from_interface(object);

	mixer_lock_take();
	*status = stream->packet_count < MAXIMUM_STREAM_PACKETS ? XMO_STATUSF_ACCEPT_INPUT_DATA : 0;
	pthread_mutex_unlock(&mixer_lock);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_process(IDirectSoundStream *object, LPCXMEDIAPACKET input, LPCXMEDIAPACKET output)
{
	struct sdl_stream *stream = stream_from_interface(object);
	struct voice_packet *entry;
	unsigned long frames = 0;
	short *samples;

	(void)output;
	if (!input)
		return E_INVALIDARG;
	/* decode outside the lock */
	samples = stream->adpcm ?
		decode_adpcm(input->pvBuffer, input->dwMaxSize, stream->channels, &frames) :
		decode_pcm(input->pvBuffer, input->dwMaxSize, stream->channels, &frames);
	mixer_lock_take();
	if (stream->packet_count == MAXIMUM_STREAM_PACKETS)
	{
		pthread_mutex_unlock(&mixer_lock);
		free(samples);
		return E_OUTOFMEMORY;
	}
	entry = &stream->packets[(stream->packet_head + stream->packet_count) % MAXIMUM_STREAM_PACKETS];
	entry->packet = *input;
	entry->samples = samples;
	entry->frames = samples ? frames : 0;
	entry->finished = FALSE;
	if (input->pdwStatus)
		*input->pdwStatus = XMEDIAPACKET_STATUS_PENDING;
	if (input->pdwCompletedSize)
		*input->pdwCompletedSize = 0;
	if (!stream->packet_count)
	{
		/* a stream that ran dry starts over */
		stream->cursor = 0;
		resampler_reset(stream);
		stream->gains_valid = FALSE;
	}
	stream->packet_count++;
	pthread_mutex_unlock(&mixer_lock);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_discontinuity(IDirectSoundStream *object)
{
	(void)object;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_flush(IDirectSoundStream *object)
{
	struct sdl_stream *stream = stream_from_interface(object);

	mixer_lock_take();
	while (stream->packet_count)
	{
		struct voice_packet *head = &stream->packets[stream->packet_head];

		stream_complete_head(stream, head->finished ? XMEDIAPACKET_STATUS_SUCCESS : XMEDIAPACKET_STATUS_FLUSHED,
			head->finished ? head->packet.dwMaxSize : 0);
	}
	stream->cursor = 0;
	resampler_reset(stream);
	pthread_mutex_unlock(&mixer_lock);
	return S_OK;
}

static IDirectSoundStreamVtbl stream_vtable =
{
	stream_add_reference,
	stream_release,
	stream_get_info,
	stream_get_status,
	stream_process,
	stream_discontinuity,
	stream_flush,
};

/* ---------- the DirectSound object */

struct sdl_direct_sound
{
	ULONG reference_count;
};

static struct sdl_direct_sound direct_sound = { 0 };

HRESULT WINAPI DirectSoundCreate(LPGUID device_id, LPDIRECTSOUND *result, LPUNKNOWN outer)
{
	(void)device_id;
	(void)outer;
	audio_start();
	direct_sound.reference_count++;
	*result = (LPDIRECTSOUND)&direct_sound;
	return DS_OK;
}

ULONG WINAPI IDirectSound_Release(LPDIRECTSOUND sound)
{
	(void)sound;
	return direct_sound.reference_count ? --direct_sound.reference_count : 0;
}

VOID WINAPI DirectSoundDoWork(void)
{
	static unsigned long volume_read_at = (unsigned long)-1;

	/* (audio.volume, audio.reverb, audio.resampling and audio.device read
	again when the settings change: on the game's thread, not the mixer's,
	whose lock the config's file I/O would hold) */
	if (volume_read_at != config_changes())
	{
		volume_read_at = config_changes();
		master_volume = (float)config_real("audio.volume");
		reverb_enabled = config_boolean("audio.reverb");
		resampling_linear = !strcmp(config_string("audio.resampling"), "linear");
		doppler_enabled = config_boolean("audio.doppler");
		audio_device_update();
		hrtf_update();
	}
	else if (hrtf_automatic)
	{
		static unsigned long checked_at;
		unsigned long now = (unsigned long)SDL_GetTicks();

		/* (the system's output may have moved to headphones, or off them) */
		if (now - checked_at > 1000)
		{
			checked_at = now;
			hrtf_update();
		}
	}
	streams_complete_finished();
}

VOID WINAPI DirectSoundUseFullHRTF(void)
{
}

HRESULT WINAPI IDirectSound_GetCaps(LPDIRECTSOUND sound, LPDSCAPS caps)
{
	(void)sound;
	memset(caps, 0, sizeof(*caps));
	caps->dwFree2DBuffers = 64;
	caps->dwFree3DBuffers = 64;
	caps->dwFreeBufferSGEs = 2047;
	caps->dwMemoryAllocated = 0;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_GetSpeakerConfig(LPDIRECTSOUND sound, LPDWORD speaker_config)
{
	(void)sound;
	*speaker_config = output_channels > 2 ? DSSPEAKER_ENABLE_AC3 : DSSPEAKER_STEREO;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_DownloadEffectsImage(LPDIRECTSOUND sound, LPCVOID image, DWORD image_size,
	LPCDSEFFECTIMAGELOC image_location, LPDSEFFECTIMAGEDESC *image_description)
{
	(void)sound;
	(void)image;
	(void)image_size;
	(void)image_location;
	if (image_description)
		*image_description = NULL;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CommitDeferredSettings(LPDIRECTSOUND sound) { (void)sound; return DS_OK; }
HRESULT WINAPI IDirectSound_SetMixBinHeadroom(LPDIRECTSOUND sound, DWORD mix_bin_mask, DWORD headroom) { (void)sound; (void)mix_bin_mask; (void)headroom; return DS_OK; }
HRESULT WINAPI IDirectSound_SetI3DL2Listener(LPDIRECTSOUND sound, LPCDSI3DL2LISTENER listener_properties, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	environment = *listener_properties;
	environment_serial++;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetDistanceFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.distance_factor = factor > 0.0f ? factor : 1.0f;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetRolloffFactor(LPDIRECTSOUND sound, FLOAT factor, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.rolloff_factor = factor >= 0.0f ? factor : 1.0f;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetPosition(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.position[0] = x;
	listener.position[1] = y;
	listener.position[2] = z;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_SetVelocity(LPDIRECTSOUND sound, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.velocity[0] = x;
	listener.velocity[1] = y;
	listener.velocity[2] = z;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

static void normalize3(float *vector)
{
	float length = sqrtf(dot3(vector, vector));

	if (length > 1.0e-6f)
	{
		vector[0] /= length;
		vector[1] /= length;
		vector[2] /= length;
	}
}

HRESULT WINAPI IDirectSound_SetOrientation(LPDIRECTSOUND sound, FLOAT x_front, FLOAT y_front, FLOAT z_front,
	FLOAT x_top, FLOAT y_top, FLOAT z_top, DWORD apply)
{
	(void)sound;
	(void)apply;
	mixer_lock_take();
	listener.front[0] = x_front;
	listener.front[1] = y_front;
	listener.front[2] = z_front;
	listener.top[0] = x_top;
	listener.top[1] = y_top;
	listener.top[2] = z_top;
	normalize3(listener.front);
	normalize3(listener.top);
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundStream(LPDIRECTSOUND sound, LPCDSSTREAMDESC description,
	LPDIRECTSOUNDSTREAM *result, LPUNKNOWN outer)
{
	struct sdl_stream *stream = calloc(1, sizeof(*stream));
	const WAVEFORMATEX *format = description->lpwfxFormat;

	(void)sound;
	(void)outer;
	if (!stream)
		return E_OUTOFMEMORY;
	stream->object.lpVtbl = &stream_vtable;
	stream->reference_count = 1;
	stream->callback = description->lpfnCallback;
	stream->context = description->lpvContext;
	stream->adpcm = format && format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	stream->channels = format && format->nChannels == 2 ? 2 : 1;
	stream->sample_rate = format ? format->nSamplesPerSec : 0;
	stream->frequency = stream->sample_rate;
	stream->volume = 1.0f;
	/* DirectSound's default mix bins: a mono voice to both fronts, a
	stereo voice's channels to the front left and right */
	stream->mix_left = 1.0f;
	stream->mix_right = 1.0f;
	stream->surround_bins[0][0] = 1.0f;
	stream->surround_bins[stream->channels - 1][1] = 1.0f;
	stream->has_3d = (description->dwFlags & DSSTREAMCAPS_CTRL3D) != 0;
	stream->mode = DS3DMODE_NORMAL;
	stream->minimum_distance = DS3D_DEFAULTMINDISTANCE;
	stream->maximum_distance = DS3D_DEFAULTMAXDISTANCE;
	/* DirectSound's defaults: no cone, facing +z */
	stream->cone_inside = stream->cone_outside = 360;
	stream->cone_orientation[2] = 1.0f;
	stream->current_doppler = 1.0f;
	resampler_reset(stream);
	mixer_lock_take();
	stream->next = streams;
	streams = stream;
	pthread_mutex_unlock(&mixer_lock);
	*result = &stream->object;
	return DS_OK;
}

/* January-era DirectSound exports the game declares itself
(sound_dsound_xbox.c); the XDK 3911 headers no longer carry them */

void __stdcall DirectSoundStopStream(LPDIRECTSOUNDSTREAM stream)
{
	stream_flush(stream);
}

unsigned long __stdcall DirectSoundGetStreamVoiceStatus(LPDIRECTSOUNDSTREAM stream)
{
	struct sdl_stream *record = stream_from_interface(stream);
	unsigned long active;

	mixer_lock_take();
	active = record->packet_count != 0;
	pthread_mutex_unlock(&mixer_lock);
	return active;
}

#define STREAM_SETTER(body) \
	struct sdl_stream *record = stream_from_interface(stream); \
	mixer_lock_take(); \
	body; \
	pthread_mutex_unlock(&mixer_lock); \
	return DS_OK;

HRESULT WINAPI IDirectSoundStream_SetFrequency(LPDIRECTSOUNDSTREAM stream, DWORD frequency)
{
	STREAM_SETTER(record->frequency = frequency ? frequency : record->sample_rate)
}

HRESULT WINAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM stream, LONG volume)
{
	STREAM_SETTER(record->volume = gain_from_millibels(volume))
}

/* the speaker bins of a mask, for 5.1, in the order of its set bits: a stereo
voice sends its left channel to the first, third, ... and its right to the
second, fourth, ... (the game's stereo voices on AC-3 take front left, front
right, back left, back right). The crosstalk, reverb and effect sends are not
speakers. volumes is NULL for 0 dB. */
static void stream_set_surround_bins(struct sdl_stream *record, DWORD mix_bin_mask, const LONG *volumes, BOOL clear)
{
	unsigned long bit, index = 0;

	if (clear)
		memset(record->surround_bins, 0, sizeof(record->surround_bins));
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) & DSMIXBIN_SPEAKER_MASK)
		{
			record->surround_bins[record->channels == 2 ? index & 1 : 0][bit] =
				volumes ? gain_from_millibels(volumes[index]) : 1.0f;
		}
		index++;
	}
}

HRESULT WINAPI IDirectSoundStream_SetMixBins(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask)
{
	STREAM_SETTER(
		record->mix_left = (mix_bin_mask & DSMIXBIN_FRONT_LEFT) ? 1.0f : 0.0f;
		record->mix_right = (mix_bin_mask & DSMIXBIN_FRONT_RIGHT) ? 1.0f : 0.0f;
		stream_set_surround_bins(record, mix_bin_mask, NULL, TRUE))
}

/* volumes come in the order of the set bits of the mask */
HRESULT WINAPI IDirectSoundStream_SetMixBinVolumes(LPDIRECTSOUNDSTREAM stream, DWORD mix_bin_mask, const LONG *volumes)
{
	struct sdl_stream *record = stream_from_interface(stream);
	unsigned long bit, index = 0;

	mixer_lock_take();
	for (bit = 0; bit < 32; bit++)
	{
		if (!(mix_bin_mask & (1UL << bit)))
			continue;
		if ((1UL << bit) == DSMIXBIN_FRONT_LEFT)
			record->mix_left = gain_from_millibels(volumes[index]);
		else if ((1UL << bit) == DSMIXBIN_FRONT_RIGHT)
			record->mix_right = gain_from_millibels(volumes[index]);
		index++;
	}
	stream_set_surround_bins(record, mix_bin_mask, volumes, FALSE);
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundStream_SetMode(LPDIRECTSOUNDSTREAM stream, DWORD mode, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->mode = mode)
}

HRESULT WINAPI IDirectSoundStream_SetPosition(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->position[0] = x; record->position[1] = y; record->position[2] = z)
}

HRESULT WINAPI IDirectSoundStream_SetMinDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->minimum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetMaxDistance(LPDIRECTSOUNDSTREAM stream, FLOAT distance, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->maximum_distance = distance)
}

HRESULT WINAPI IDirectSoundStream_SetI3DL2Source(LPDIRECTSOUNDSTREAM stream, LPCDSI3DL2BUFFER source, DWORD apply)
{
	LONG direct, direct_hf, room, room_hf;

	(void)apply;
	/* the direct path: at low frequencies obstruction and occlusion take
	their LF ratio of their high frequency levels, which (with the source's
	own) muffle it above the HF reference */
	direct = source->lDirect +
		(LONG)(source->Obstruction.lHFLevel * source->Obstruction.flLFRatio) +
		(LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	if (direct > 0)
		direct = 0;
	direct_hf = source->lDirect + source->lDirectHF + source->Obstruction.lHFLevel + source->Occlusion.lHFLevel;
	/* the room send: occlusion muffles it as it does the direct path, and
	obstruction does not reach it */
	room = source->lRoom + (LONG)(source->Occlusion.lHFLevel * source->Occlusion.flLFRatio);
	room_hf = source->lRoom + source->lRoomHF + source->Occlusion.lHFLevel;
	{
		STREAM_SETTER(
			record->direct = direct;
			record->direct_hf = direct_hf;
			record->room = room;
			record->room_hf = room_hf;
			record->room_rolloff_factor = source->flRoomRolloffFactor)
	}
}

HRESULT WINAPI IDirectSoundStream_Pause(LPDIRECTSOUNDSTREAM stream, DWORD pause)
{
	STREAM_SETTER(record->paused = pause == DSSTREAMPAUSE_PAUSE)
}

HRESULT WINAPI IDirectSoundStream_SetVelocity(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->velocity[0] = x; record->velocity[1] = y; record->velocity[2] = z)
}
HRESULT WINAPI IDirectSoundStream_SetConeAngles(LPDIRECTSOUNDSTREAM stream, DWORD inside, DWORD outside, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->cone_inside = inside; record->cone_outside = outside)
}
HRESULT WINAPI IDirectSoundStream_SetConeOrientation(LPDIRECTSOUNDSTREAM stream, FLOAT x, FLOAT y, FLOAT z, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->cone_orientation[0] = x; record->cone_orientation[1] = y; record->cone_orientation[2] = z)
}
HRESULT WINAPI IDirectSoundStream_SetConeOutsideVolume(LPDIRECTSOUNDSTREAM stream, LONG volume, DWORD apply)
{
	(void)apply;
	STREAM_SETTER(record->cone_outside_volume = volume)
}

/* ---------- buffers

The game has two buffers: the silent looping one that keeps the voice
processor busy (on mix bins that are not the speakers), and the loading
screen's four tones (progress_bar.c), Xbox ADPCM looping at a pitch of their
own, their volumes following the progress. A buffer is a voice like a
stream's, its data decoded once into one packet, which loops while it is
played with DSBPLAY_LOOPING. Buffers are mixed from a list of their own, out
of the streams' completion: the packet is the buffer's, never the game's to
take back. Loop regions are not modelled: the game's are the whole buffer. */

static struct sdl_stream *buffer_from_interface(LPDIRECTSOUNDBUFFER buffer)
{
	return (struct sdl_stream *)buffer;
}

HRESULT WINAPI DirectSoundCreateBuffer(LPCDSBUFFERDESC description, LPDIRECTSOUNDBUFFER *result)
{
	struct sdl_stream *buffer = calloc(1, sizeof(*buffer));
	const WAVEFORMATEX *format = description->lpwfxFormat;

	if (!buffer)
		return E_OUTOFMEMORY;
	buffer->reference_count = 1;
	buffer->adpcm = format && format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM;
	buffer->channels = format && format->nChannels == 2 ? 2 : 1;
	buffer->sample_rate = format ? format->nSamplesPerSec : 0;
	buffer->frequency = buffer->sample_rate;
	buffer->volume = 1.0f;
	/* the mix bins asked for, else DirectSound's default, both fronts */
	buffer->mix_left = !description->dwMixBinMask || (description->dwMixBinMask & DSMIXBIN_FRONT_LEFT) ? 1.0f : 0.0f;
	buffer->mix_right = !description->dwMixBinMask || (description->dwMixBinMask & DSMIXBIN_FRONT_RIGHT) ? 1.0f : 0.0f;
	/* stopped until Play */
	buffer->paused = TRUE;
	resampler_reset(buffer);
	mixer_lock_take();
	buffer->next = buffers;
	buffers = buffer;
	pthread_mutex_unlock(&mixer_lock);
	*result = (LPDIRECTSOUNDBUFFER)buffer;
	return DS_OK;
}

HRESULT WINAPI IDirectSound_CreateSoundBuffer(LPDIRECTSOUND sound, LPCDSBUFFERDESC description,
	LPDIRECTSOUNDBUFFER *result, LPUNKNOWN outer)
{
	(void)sound;
	(void)outer;
	return DirectSoundCreateBuffer(description, result);
}

ULONG WINAPI IDirectSoundBuffer_Release(LPDIRECTSOUNDBUFFER buffer)
{
	struct sdl_stream *record = buffer_from_interface(buffer);
	struct sdl_stream **link;
	ULONG count;

	mixer_lock_take();
	count = --record->reference_count;
	if (!count)
	{
		for (link = &buffers; *link; link = &(*link)->next)
		{
			if (*link == record)
			{
				*link = record->next;
				break;
			}
		}
	}
	pthread_mutex_unlock(&mixer_lock);
	if (count)
		return count;
	packet_release(&record->packets[0]);
	free(record);
	return 0;
}

HRESULT WINAPI IDirectSoundBuffer_SetBufferData(LPDIRECTSOUNDBUFFER buffer, LPVOID data, DWORD size)
{
	struct sdl_stream *record = buffer_from_interface(buffer);
	struct voice_packet *entry = &record->packets[0];
	unsigned long frames = 0;
	short *samples, *old_samples;

	/* decode outside the lock */
	samples = !data || !size ? NULL : record->adpcm ?
		decode_adpcm(data, size, record->channels, &frames) :
		decode_pcm(data, size, record->channels, &frames);
	mixer_lock_take();
	old_samples = entry->samples;
	memset(entry, 0, sizeof(*entry));
	entry->samples = samples;
	entry->frames = samples ? frames : 0;
	record->packet_head = 0;
	record->packet_count = samples ? 1 : 0;
	record->cursor = 0;
	resampler_reset(record);
	record->gains_valid = FALSE;
	pthread_mutex_unlock(&mixer_lock);
	free(old_samples);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Play(LPDIRECTSOUNDBUFFER buffer, DWORD reserved1, DWORD reserved2, DWORD flags)
{
	struct sdl_stream *record = buffer_from_interface(buffer);

	(void)reserved1;
	(void)reserved2;
	mixer_lock_take();
	record->looping = (flags & DSBPLAY_LOOPING) != 0;
	/* (one played to its end plays again from the start) */
	if (record->packets[0].finished)
	{
		record->packets[0].finished = FALSE;
		record->cursor = 0;
		resampler_reset(record);
		record->gains_valid = FALSE;
	}
	record->paused = FALSE;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_Stop(LPDIRECTSOUNDBUFFER buffer)
{
	struct sdl_stream *record = buffer_from_interface(buffer);

	mixer_lock_take();
	record->paused = TRUE;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

/* in bytes of the buffer's data; in ADPCM, to the start of its block */
HRESULT WINAPI IDirectSoundBuffer_SetCurrentPosition(LPDIRECTSOUNDBUFFER buffer, DWORD play_cursor)
{
	struct sdl_stream *record = buffer_from_interface(buffer);
	unsigned long frame = record->adpcm ?
		play_cursor / (XBOX_ADPCM_BLOCK_BYTES * record->channels) * XBOX_ADPCM_BLOCK_SAMPLES :
		play_cursor / (2 * record->channels);

	mixer_lock_take();
	record->packets[0].finished = FALSE;
	record->cursor = frame < record->packets[0].frames ? frame : 0;
	resampler_reset(record);
	record->gains_valid = FALSE;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetLoopRegion(LPDIRECTSOUNDBUFFER buffer, DWORD loop_start, DWORD loop_length) { (void)buffer; (void)loop_start; (void)loop_length; return DS_OK; }

/* the Xbox's pitch: 1/4096 octaves from 48 kHz (XAudioCalculatePitch), the
rate the voice plays its frames at */
HRESULT WINAPI IDirectSoundBuffer_SetPitch(LPDIRECTSOUNDBUFFER buffer, LONG pitch)
{
	struct sdl_stream *record = buffer_from_interface(buffer);
	DWORD frequency = (DWORD)(48000.0 * pow(2.0, (double)pitch / 4096.0) + 0.5);

	mixer_lock_take();
	record->frequency = frequency ? frequency : 1;
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}

HRESULT WINAPI IDirectSoundBuffer_SetVolume(LPDIRECTSOUNDBUFFER buffer, LONG volume)
{
	struct sdl_stream *record = buffer_from_interface(buffer);

	mixer_lock_take();
	record->volume = gain_from_millibels(volume);
	pthread_mutex_unlock(&mixer_lock);
	return DS_OK;
}
