/*
VOICE.C

Voice chat's audio (voice.h): the microphone through SDL3's recording, our
own Opus (port/third_party/opus, Xiph's release), a jitter buffer for each
speaker (voice_jitter.c), and the voices played through the game's mixer
(dsound_sdl.c's dsound_voice_*), where a voice heard from where its speaker
stands takes the game's 3D path: its distance, its pan or Headphone 3D, and
the room's reverb.

Opus is set up as OpenCE's voice sets it up, so that a frame can pass
between the two unchanged later (docs/delta.md, "Voice bridge (planned)"):
48 kHz mono, 20 ms frames, VOIP with the voice signal, variable bitrate at
the host's quality (8 to 64 kbps, 24 unless the host says), in-band FEC for
10% loss, complexity 5, and each frame no bigger than the host's cap.

The microphone opens only when the player first talks (push to talk held,
or open mic picked), never when the game starts: that first use is when
macOS and Android ask for permission. In push to talk it is paused while
the key is up and closed after a while without talking. Nothing is ever
written down: frames go to the host and nowhere else.

Settings (config.toml): audio.voice_chat ("off", the default, as retail had
no voice; "push_to_talk"; "open_mic"), audio.voice_volume, the channels'
volumes audio.voice_volume_team, _proximity, _everyone and _party (0 turns a
channel off: the host then sends none of it), audio.voice_input_device,
audio.voice_open_mic_level, audio.voice_positional.

debug.voice_test = "tone" (a 440 Hz tone) or the path of a WAV file (16-bit
PCM, 48 kHz) said in place of the microphone, all the time, for the
automated tests; each voice heard is then logged once a second
("voice test: speaker M level L route R channel C bytes B"), with what the
codec costs. With it, HALO_VOICE_TEST_DUMP names a WAV file that what this
copy hears is written to, for the tests' own checks. A release's build
(channel "release") ignores both: only test builds feed the voice.
*/

#include "port_config.h"

#ifdef HALO_PORT_VOICE_AUDIO

#include "voice.h"
#include "voice_jitter.h"
#include "build_identity.h"
#include "delta_wire.h"

#include <SDL3/SDL.h>
#include <opus.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
/* dsound_sdl.c's */
void *dsound_voice_open(void);
void dsound_voice_close(void *voice);
int dsound_voice_submit(void *voice, const short *samples, unsigned long frames);
unsigned long dsound_voice_queued(void *voice);
void dsound_voice_place(void *voice, int mode, const float *position, float minimum_distance, float maximum_distance,
	float gain);

/* ---------- constants */

enum
{
	RATE = 48000,
	/* a frame: 20 ms */
	FRAME = 960,
	/* the microphone's sound kept to cut into frames (a second) */
	CAPTURE_SIZE = RATE,
	/* frames said and waiting to be sent */
	OUTGOING = 8,
	/* a voice is kept this far ahead of the mixer (60 ms): the jitter
	buffer's start, then this */
	PLAY_AHEAD = 3 * FRAME,
	/* push to talk: the microphone paused at once when the key comes up, and
	closed after this long without talking (milliseconds), so the system's
	microphone light goes out soon after */
	CLOSE_AFTER = 3000,
	/* open mic: the gate stays open this long after the level drops (ms) */
	OPEN_MIC_HANG = 400,
	/* a speaker not heard this long gives back its voice, and its decoder
	this long (ms) */
	SPEAKER_IDLE = 2000,
	SPEAKER_FORGET = 30000,
	/* debug.voice_test's log: once a second */
	TEST_LOG_INTERVAL = 1000,
	/* a proximity voice is at full volume this near (world units) */
	PROXIMITY_FULL = 3
};

/* ---------- globals */

struct speaker
{
	int used;
	int machine;
	int player;
	OpusDecoder *decoder;
	struct voice_jitter jitter;
	void *voice;
	unsigned long heard_time;
	int route;
	int channel;
	/* where it is (voice_place) */
	int placed;
	float position[3];
	float range;
	/* debug.voice_test's log */
	unsigned long bytes;
	float level;
	unsigned long frames_decoded;
};

static struct
{
	int ready;
	unsigned long settings_read;
	/* the settings */
	int mode;
	float volume;
	/* each channel's volume (enum delta_voice_channel) */
	float channel_volume[NUMBER_OF_DELTA_VOICE_CHANNELS];
	float open_mic_level;
	int positional;
	char input_device[256];
	/* the microphone */
	int microphone;
	SDL_AudioStream *capture;
	int capturing;
	unsigned long last_talk_time;
	float capture_buffer[CAPTURE_SIZE];
	int capture_count;
	float input_level;
	unsigned long gate_until;
#ifdef SDL_PLATFORM_ANDROID
	/* the system's answer (SDL_RequestAndroidPermission's: 0 waiting, 1 yes,
	-1 no) */
	volatile int permission;
#endif
	/* the encoder, at the host's quality */
	OpusEncoder *encoder;
	int kbps;
	unsigned short sequence;
	struct voice_frame outgoing[OUTGOING];
	int outgoing_head;
	int outgoing_count;
	unsigned long sent_time;
	int sent_any;
	/* the voices heard */
	struct speaker speakers[VOICE_SPEAKERS];
	/* debug.voice_test */
	int test;
	short *test_samples;
	unsigned long test_count;
	unsigned long test_position;
	double tone_phase;
	unsigned long test_log_time;
	FILE *dump;
	unsigned long dump_frames;
	/* what the codec costs (microseconds, for the log) */
	double encode_microseconds;
	unsigned long encodes;
	double decode_microseconds;
	unsigned long decodes;
} voice;

/* ---------- settings */

static int same_text(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
		char y = *b >= 'A' && *b <= 'Z' ? (char)(*b - 'A' + 'a') : *b;

		if (x != y)
			return 0;
	}
	return *a == *b;
}

static float clamped(double value, float minimum, float maximum)
{
	return value < minimum ? minimum : value > maximum ? maximum : (float)value;
}

static void test_load(const char *source);

static void read_settings(void)
{
	const char *mode = config_string("audio.voice_chat");
	/* (debug.voice_test is for test builds: a release's never feeds the
	voice from anything but the microphone) */
	const char *test = strcmp(build_identity_channel(), "release") ? config_string("debug.voice_test") : "";
	int was = voice.mode;

	voice.mode = same_text(mode, "push_to_talk") ? _voice_mode_push_to_talk :
		same_text(mode, "open_mic") ? _voice_mode_open_mic : _voice_mode_off;
	voice.volume = clamped(config_real("audio.voice_volume"), 0.0f, 2.0f);
	voice.channel_volume[_delta_voice_channel_team] = clamped(config_real("audio.voice_volume_team"), 0.0f, 2.0f);
	voice.channel_volume[_delta_voice_channel_proximity] =
		clamped(config_real("audio.voice_volume_proximity"), 0.0f, 2.0f);
	voice.channel_volume[_delta_voice_channel_everyone] =
		clamped(config_real("audio.voice_volume_everyone"), 0.0f, 2.0f);
	voice.channel_volume[_delta_voice_channel_party] = clamped(config_real("audio.voice_volume_party"), 0.0f, 2.0f);
	voice.open_mic_level = clamped(config_real("audio.voice_open_mic_level"), 0.0001f, 1.0f);
	voice.positional = config_boolean("audio.voice_positional");
	snprintf(voice.input_device, sizeof(voice.input_device), "%s", config_string("audio.voice_input_device"));
	/* (the test feed: talks all the time, push to talk or not) */
	if (test && test[0] && !voice.test)
	{
		test_load(test);
		if (voice.mode == _voice_mode_off)
			voice.mode = _voice_mode_open_mic;
	}
	if (voice.mode != was)
	{
		platform_log("voice: %s", voice.mode == _voice_mode_off ? "off" :
			voice.mode == _voice_mode_push_to_talk ? "push to talk" : "open mic");
	}
}

int voice_mode(void)
{
	return voice.mode;
}

int voice_microphone(void)
{
	return voice.microphone;
}

float voice_input_level(void)
{
	return voice.input_level;
}

int voice_hears(void)
{
	int hears = 0;

	if (voice.mode == _voice_mode_off || voice.volume <= 0.0f)
		return 0;
	if (voice.channel_volume[_delta_voice_channel_team] > 0.0f)
		hears |= DELTA_VOICE_TEAM;
	if (voice.channel_volume[_delta_voice_channel_proximity] > 0.0f)
		hears |= DELTA_VOICE_PROXIMITY;
	if (voice.channel_volume[_delta_voice_channel_everyone] > 0.0f)
		hears |= DELTA_VOICE_EVERYONE;
	if (voice.channel_volume[_delta_voice_channel_party] > 0.0f)
		hears |= DELTA_VOICE_PARTY;
	return hears;
}

/* ---------- debug.voice_test */

static unsigned long read_le(const unsigned char *data, int bytes)
{
	unsigned long value = 0;
	int index;

	for (index = bytes - 1; index >= 0; index--)
		value = value << 8 | data[index];
	return value;
}

/* the test's feed: "tone", or a WAV file of 16-bit PCM at 48 kHz (its first
channel) */
static void test_load(const char *source)
{
	voice.test = 1;
	if (same_text(source, "tone"))
	{
		platform_log("voice test: a 440 Hz tone in place of the microphone");
		return;
	}
	{
		size_t size = 0;
		unsigned char *data = (unsigned char *)SDL_LoadFile(source, &size);
		size_t offset = 12;
		int channels = 0, bits = 0;
		unsigned long rate = 0;

		if (!data || size < 44 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4))
		{
			platform_log("voice test: %s is not a WAV file: a tone instead", source);
			SDL_free(data);
			return;
		}
		while (offset + 8 <= size)
		{
			unsigned long chunk = read_le(data + offset + 4, 4);

			if (!memcmp(data + offset, "fmt ", 4) && chunk >= 16 && offset + 8 + chunk <= size)
			{
				channels = (int)read_le(data + offset + 10, 2);
				rate = read_le(data + offset + 12, 4);
				bits = (int)read_le(data + offset + 22, 2);
			}
			else if (!memcmp(data + offset, "data", 4) && channels > 0 && bits == 16 && rate == RATE)
			{
				unsigned long frames, index;

				if (chunk > size - offset - 8)
					chunk = (unsigned long)(size - offset - 8);
				frames = chunk / (unsigned long)(2 * channels);
				voice.test_samples = (short *)malloc(frames * sizeof(short) + 1);
				if (voice.test_samples)
				{
					for (index = 0; index < frames; index++)
						voice.test_samples[index] = (short)read_le(data + offset + 8 + index * 2 * (unsigned long)channels, 2);
					voice.test_count = frames;
				}
				break;
			}
			offset += 8 + chunk + (chunk & 1);
		}
		SDL_free(data);
		if (voice.test_count)
			platform_log("voice test: %s (%.1f s) in place of the microphone", source, voice.test_count / (double)RATE);
		else
			platform_log("voice test: %s is not 16-bit PCM at 48 kHz: a tone instead", source);
	}
}

/* the test feed's next frames into the capture buffer */
static void test_feed(int frames)
{
	while (frames-- > 0 && voice.capture_count < CAPTURE_SIZE)
	{
		float sample;

		if (voice.test_count)
		{
			sample = voice.test_samples[voice.test_position] * (1.0f / 32768.0f);
			voice.test_position = (voice.test_position + 1) % voice.test_count;
		}
		else
		{
			sample = 0.25f * (float)sin(voice.tone_phase);
			voice.tone_phase += 2.0 * 3.14159265358979 * 440.0 / RATE;
			if (voice.tone_phase > 2.0 * 3.14159265358979)
				voice.tone_phase -= 2.0 * 3.14159265358979;
		}
		voice.capture_buffer[voice.capture_count++] = sample;
	}
}

static void dump_header(unsigned long frames)
{
	unsigned char header[44];
	unsigned long bytes = frames * 2;

	memcpy(header, "RIFF", 4);
	header[4] = (unsigned char)(bytes + 36);
	header[5] = (unsigned char)((bytes + 36) >> 8);
	header[6] = (unsigned char)((bytes + 36) >> 16);
	header[7] = (unsigned char)((bytes + 36) >> 24);
	memcpy(header + 8, "WAVEfmt ", 8);
	memcpy(header + 16, "\x10\0\0\0\x01\0\x01\0\x80\xbb\0\0\0\x77\x01\0\x02\0\x10\0", 20);
	memcpy(header + 36, "data", 4);
	header[40] = (unsigned char)bytes;
	header[41] = (unsigned char)(bytes >> 8);
	header[42] = (unsigned char)(bytes >> 16);
	header[43] = (unsigned char)(bytes >> 24);
	fseek(voice.dump, 0, SEEK_SET);
	fwrite(header, 1, sizeof(header), voice.dump);
	fseek(voice.dump, 0, SEEK_END);
}

/* (the test's own check of what this copy heard: HALO_VOICE_TEST_DUMP) */
static void dump_frames(const short *samples, int frames)
{
	if (!voice.test)
		return;
	if (!voice.dump)
	{
		const char *path = getenv("HALO_VOICE_TEST_DUMP");

		if (!path || !path[0] || !(voice.dump = fopen(path, "wb")))
			return;
		dump_header(0);
	}
	fwrite(samples, sizeof(short), (size_t)frames, voice.dump);
	voice.dump_frames += (unsigned long)frames;
	dump_header(voice.dump_frames);
	fflush(voice.dump);
}

/* ---------- the microphone */

#ifdef SDL_PLATFORM_ANDROID
static void SDLCALL permission_answered(void *userdata, const char *permission, bool granted)
{
	(void)userdata;
	(void)permission;
	voice.permission = granted ? 1 : -1;
}
#endif

static SDL_AudioDeviceID input_device(void)
{
	SDL_AudioDeviceID chosen = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
	SDL_AudioDeviceID *devices;
	int count = 0, index;

	if (!voice.input_device[0] || same_text(voice.input_device, "default"))
		return chosen;
	devices = SDL_GetAudioRecordingDevices(&count);
	for (index = 0; devices && index < count; index++)
	{
		const char *name = SDL_GetAudioDeviceName(devices[index]);

		if (name && same_text(name, voice.input_device))
			chosen = devices[index];
	}
	if (chosen == SDL_AUDIO_DEVICE_DEFAULT_RECORDING)
		platform_log("voice: no microphone called \"%s\": the system's", voice.input_device);
	SDL_free(devices);
	return chosen;
}

static void microphone_close(void)
{
	if (voice.capture)
	{
		SDL_DestroyAudioStream(voice.capture);
		voice.capture = NULL;
		platform_log("voice: the microphone closed");
	}
	voice.capturing = 0;
	voice.capture_count = 0;
	if (voice.microphone == _voice_microphone_open)
		voice.microphone = _voice_microphone_closed;
}

/* the microphone opened (the first time asked for, on Android): 1 if open */
static int microphone_open(void)
{
	SDL_AudioSpec spec;

	if (voice.capture)
		return 1;
	if (voice.microphone == _voice_microphone_denied || voice.microphone == _voice_microphone_missing)
		return 0;
#ifdef SDL_PLATFORM_ANDROID
	if (voice.permission <= 0)
	{
		if (voice.permission < 0)
		{
			voice.microphone = _voice_microphone_denied;
			platform_log("voice: the microphone isn't allowed: listening only");
			return 0;
		}
		if (voice.microphone != _voice_microphone_asking)
		{
			voice.microphone = _voice_microphone_asking;
			if (!SDL_RequestAndroidPermission("android.permission.RECORD_AUDIO", permission_answered, NULL))
				voice.permission = -1;
		}
		return 0;
	}
#endif
	if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
	{
		voice.microphone = _voice_microphone_missing;
		return 0;
	}
	spec.format = SDL_AUDIO_F32;
	spec.channels = 1;
	spec.freq = RATE;
	voice.capture = SDL_OpenAudioDeviceStream(input_device(), &spec, NULL, NULL);
	if (!voice.capture)
	{
		voice.microphone = _voice_microphone_missing;
#ifdef SDL_PLATFORM_WINDOWS
		platform_log("voice: no microphone (blocked by Windows' privacy settings?): %s", SDL_GetError());
#else
		platform_log("voice: no microphone: %s", SDL_GetError());
#endif
		return 0;
	}
	voice.microphone = _voice_microphone_open;
	voice.capturing = 0;
	platform_log("voice: the microphone opened");
	return 1;
}

/* the microphone running (talking) or paused */
static void microphone_run(int run)
{
	if (!voice.capture || run == voice.capturing)
		return;
	if (run)
	{
		SDL_ClearAudioStream(voice.capture);
		SDL_ResumeAudioStreamDevice(voice.capture);
	}
	else
		SDL_PauseAudioStreamDevice(voice.capture);
	voice.capturing = run;
	voice.capture_count = 0;
}

/* what the microphone has, into the capture buffer */
static void microphone_read(void)
{
	int available;

	if (!voice.capture || !voice.capturing)
		return;
	available = SDL_GetAudioStreamAvailable(voice.capture) / (int)sizeof(float);
	if (available > CAPTURE_SIZE - voice.capture_count)
	{
		/* (behind by more than a second: the oldest dropped) */
		voice.capture_count = 0;
		if (available > CAPTURE_SIZE)
		{
			float scratch[FRAME];

			while (available > CAPTURE_SIZE)
			{
				int take = available - CAPTURE_SIZE < FRAME ? available - CAPTURE_SIZE : FRAME;

				SDL_GetAudioStreamData(voice.capture, scratch, take * (int)sizeof(float));
				available -= take;
			}
		}
	}
	if (available > 0)
	{
		int got = SDL_GetAudioStreamData(voice.capture, voice.capture_buffer + voice.capture_count,
			available * (int)sizeof(float));

		if (got > 0)
			voice.capture_count += got / (int)sizeof(float);
	}
}

/* ---------- saying */

static void encoder_set(int kbps)
{
	int error = 0;

	if (kbps < DELTA_WIRE_VOICE_MINIMUM_KBPS)
		kbps = 24;
	if (kbps > DELTA_WIRE_VOICE_MAXIMUM_KBPS)
		kbps = DELTA_WIRE_VOICE_MAXIMUM_KBPS;
	if (!voice.encoder)
	{
		voice.encoder = opus_encoder_create(RATE, 1, OPUS_APPLICATION_VOIP, &error);
		if (!voice.encoder || error != OPUS_OK)
		{
			platform_log("voice: Opus's encoder can't start (%d)", error);
			voice.encoder = NULL;
			return;
		}
		opus_encoder_ctl(voice.encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
		opus_encoder_ctl(voice.encoder, OPUS_SET_VBR(1));
		opus_encoder_ctl(voice.encoder, OPUS_SET_INBAND_FEC(1));
		opus_encoder_ctl(voice.encoder, OPUS_SET_PACKET_LOSS_PERC(10));
		opus_encoder_ctl(voice.encoder, OPUS_SET_COMPLEXITY(5));
		voice.kbps = 0;
	}
	if (kbps != voice.kbps)
	{
		opus_encoder_ctl(voice.encoder, OPUS_SET_BITRATE(kbps * 1000));
		voice.kbps = kbps;
	}
}

/* one frame of the capture buffer encoded and queued (or dropped by the
gate); the buffer moves on */
static void say_frame(unsigned long now, int gate_open)
{
	unsigned char packet[VOICE_PACKET_SIZE];
	float energy = 0.0f;
	int index, length;

	for (index = 0; index < FRAME; index++)
		energy += voice.capture_buffer[index] * voice.capture_buffer[index];
	voice.input_level = sqrtf(energy / FRAME);
	if (voice.mode == _voice_mode_open_mic && !voice.test)
	{
		if (voice.input_level >= voice.open_mic_level)
			voice.gate_until = now + OPEN_MIC_HANG;
		gate_open = gate_open && (long)(voice.gate_until - now) > 0;
	}
	if (gate_open && voice.encoder)
	{
		Uint64 start = SDL_GetPerformanceCounter();

		length = opus_encode_float(voice.encoder, voice.capture_buffer, FRAME, packet,
			delta_wire_voice_packet_cap(voice.kbps));
		voice.encode_microseconds += (double)(SDL_GetPerformanceCounter() - start) * 1.0e6 /
			(double)SDL_GetPerformanceFrequency();
		voice.encodes++;
		if (length > 1 || (length == 1 && packet[0]))
		{
			struct voice_frame *frame;

			if (voice.outgoing_count == OUTGOING)
			{
				voice.outgoing_head = (voice.outgoing_head + 1) % OUTGOING;
				voice.outgoing_count--;
			}
			frame = &voice.outgoing[(voice.outgoing_head + voice.outgoing_count) % OUTGOING];
			frame->sequence = voice.sequence++;
			frame->length = length;
			memcpy(frame->data, packet, (size_t)length);
			voice.outgoing_count++;
			voice.sent_time = now;
			voice.sent_any = 1;
		}
	}
	memmove(voice.capture_buffer, voice.capture_buffer + FRAME, (size_t)(voice.capture_count - FRAME) * sizeof(float));
	voice.capture_count -= FRAME;
}

int voice_take_frame(struct voice_frame *frame)
{
	if (!voice.outgoing_count)
		return 0;
	*frame = voice.outgoing[voice.outgoing_head];
	voice.outgoing_head = (voice.outgoing_head + 1) % OUTGOING;
	voice.outgoing_count--;
	return 1;
}

int voice_talking(unsigned long now)
{
	return voice.sent_any && now - voice.sent_time < VOICE_TALKING_TIME;
}

/* ---------- hearing */

static struct speaker *speaker_of(int machine, int make)
{
	struct speaker *free_one = NULL, *oldest = NULL;
	int index;

	for (index = 0; index < VOICE_SPEAKERS; index++)
	{
		struct speaker *speaker = &voice.speakers[index];

		if (speaker->used && speaker->machine == machine)
			return speaker;
		if (!speaker->used && !free_one)
			free_one = speaker;
		if (speaker->used && (!oldest || (long)(speaker->heard_time - oldest->heard_time) < 0))
			oldest = speaker;
	}
	if (!make)
		return NULL;
	if (!free_one)
	{
		/* (the one heard longest ago gives up its place) */
		free_one = oldest;
		dsound_voice_close(free_one->voice);
		if (free_one->decoder)
			opus_decoder_destroy(free_one->decoder);
	}
	memset(free_one, 0, sizeof(*free_one));
	free_one->used = 1;
	free_one->machine = machine;
	voice_jitter_reset(&free_one->jitter);
	return free_one;
}

static void speaker_forget(struct speaker *speaker)
{
	dsound_voice_close(speaker->voice);
	if (speaker->decoder)
		opus_decoder_destroy(speaker->decoder);
	memset(speaker, 0, sizeof(*speaker));
}

void voice_heard(unsigned long now, int speaker_machine, int speaker_player, int route, int channel,
	unsigned short sequence, const unsigned char *packet, int length)
{
	struct speaker *speaker;

	if (!voice.ready || voice.mode == _voice_mode_off || channel <= 0 || channel >= NUMBER_OF_DELTA_VOICE_CHANNELS ||
		voice.channel_volume[channel] <= 0.0f)
	{
		return;
	}
	speaker = speaker_of(speaker_machine, 1);
	if (!speaker->decoder)
	{
		int error = 0;

		speaker->decoder = opus_decoder_create(RATE, 1, &error);
		if (!speaker->decoder || error != OPUS_OK)
		{
			speaker->decoder = NULL;
			speaker->used = 0;
			return;
		}
	}
	speaker->player = speaker_player;
	speaker->route = route;
	speaker->channel = channel;
	speaker->heard_time = now;
	speaker->bytes += (unsigned long)length;
	voice_jitter_put(&speaker->jitter, now, sequence, packet, length);
}

void voice_place(int speaker_machine, const float *position, float range)
{
	struct speaker *speaker = speaker_of(speaker_machine, 0);

	if (!speaker)
		return;
	speaker->placed = position != NULL;
	if (position)
		memcpy(speaker->position, position, sizeof(speaker->position));
	speaker->range = range;
}

int voice_speaker_talking(unsigned long now, int speaker_machine)
{
	struct speaker *speaker = speaker_of(speaker_machine, 0);

	return speaker && now - speaker->heard_time < VOICE_TALKING_TIME;
}

int voice_speaker_channel(int speaker_machine)
{
	struct speaker *speaker = speaker_of(speaker_machine, 0);

	return speaker ? speaker->channel : 0;
}

void voice_reset(void)
{
	int index;

	for (index = 0; index < VOICE_SPEAKERS; index++)
	{
		if (voice.speakers[index].used)
			speaker_forget(&voice.speakers[index]);
	}
	voice.outgoing_count = 0;
}

/* a speaker's voice fed up to PLAY_AHEAD, and placed */
static void speaker_play(struct speaker *speaker, unsigned long now)
{
	unsigned long queued;
	float gain = voice.volume * voice.channel_volume[speaker->channel];

	if (!speaker->voice)
	{
		if (voice_jitter_count(&speaker->jitter) == 0)
			return;
		speaker->voice = dsound_voice_open();
		if (!speaker->voice)
			return;
	}
	if (speaker->route == _delta_voice_route_proximity && speaker->placed)
	{
		float range = speaker->range > PROXIMITY_FULL ? speaker->range : PROXIMITY_FULL + 1.0f;

		dsound_voice_place(speaker->voice, voice.positional ? 1 : 2, speaker->position, PROXIMITY_FULL, range, gain);
	}
	else
		dsound_voice_place(speaker->voice, 0, NULL, 0.0f, 0.0f, gain);
	queued = dsound_voice_queued(speaker->voice);
	while (queued < PLAY_AHEAD)
	{
		unsigned char packet[VOICE_JITTER_PACKET_SIZE];
		short pcm[FRAME];
		int length = 0, frames, result = voice_jitter_take(&speaker->jitter, now, packet, &length);
		Uint64 start;

		if (result == _voice_jitter_wait || result == _voice_jitter_end)
			break;
		start = SDL_GetPerformanceCounter();
		if (result == _voice_jitter_frame)
			frames = opus_decode(speaker->decoder, packet, length, pcm, FRAME, 0);
		else if (result == _voice_jitter_fec)
			frames = opus_decode(speaker->decoder, packet, length, pcm, FRAME, 1);
		else
			frames = opus_decode(speaker->decoder, NULL, 0, pcm, FRAME, 0);
		voice.decode_microseconds += (double)(SDL_GetPerformanceCounter() - start) * 1.0e6 /
			(double)SDL_GetPerformanceFrequency();
		voice.decodes++;
		if (frames <= 0)
			continue;
		if (voice.test)
		{
			float energy = 0.0f;
			int index;

			for (index = 0; index < frames; index++)
				energy += (float)pcm[index] * (float)pcm[index];
			speaker->level = sqrtf(energy / (float)frames) / 32768.0f;
			dump_frames(pcm, frames);
		}
		speaker->frames_decoded++;
		dsound_voice_submit(speaker->voice, pcm, (unsigned long)frames);
		queued += (unsigned long)frames;
	}
}

static void test_log(unsigned long now)
{
	int index;

	if (!voice.test || now - voice.test_log_time < TEST_LOG_INTERVAL)
		return;
	voice.test_log_time = now;
	for (index = 0; index < VOICE_SPEAKERS; index++)
	{
		struct speaker *speaker = &voice.speakers[index];

		if (!speaker->used || now - speaker->heard_time > TEST_LOG_INTERVAL)
			continue;
		platform_log("voice test: speaker %d level %.3f route %d channel %d bytes %lu lost %lu late %lu",
			speaker->machine, speaker->level, speaker->route, speaker->channel, speaker->bytes,
			speaker->jitter.lost, speaker->jitter.late);
		speaker->bytes = 0;
	}
	if (voice.encodes || voice.decodes)
	{
		platform_log("voice test: encode %.0f us a frame (%lu), decode %.0f us a frame (%lu), %d kbps",
			voice.encodes ? voice.encode_microseconds / (double)voice.encodes : 0.0, voice.encodes,
			voice.decodes ? voice.decode_microseconds / (double)voice.decodes : 0.0, voice.decodes, voice.kbps);
		voice.encode_microseconds = voice.decode_microseconds = 0.0;
		voice.encodes = voice.decodes = 0;
	}
}

/* ---------- each frame */

void voice_update(unsigned long now, int push_held, int may_talk, int kbps)
{
	int talking, index;

	if (!voice.ready || config_changes() != voice.settings_read)
	{
		voice.ready = 1;
		voice.settings_read = config_changes();
		read_settings();
	}
	/* (saying: push to talk held, or open mic, while the host lets this
	copy talk on its channel) */
	talking = may_talk && (voice.mode == _voice_mode_open_mic || voice.test ||
		(voice.mode == _voice_mode_push_to_talk && push_held));
	if (talking)
	{
		voice.last_talk_time = now;
		encoder_set(kbps);
	}
	if (voice.test)
	{
		/* (the test's feed in real time, as the microphone would) */
		static unsigned long fed_time;
		unsigned long elapsed = fed_time ? now - fed_time : 0;

		if (elapsed > 200)
			elapsed = 200;
		fed_time = now;
		if (talking)
			test_feed((int)(elapsed * RATE / 1000));
		else
			voice.capture_count = 0;
	}
	else if (talking)
	{
		if (microphone_open())
		{
			microphone_run(1);
			microphone_read();
		}
	}
	else
	{
		microphone_run(0);
		voice.input_level = 0.0f;
		/* (closed: voice off, or push to talk unused a while) */
		if (voice.capture && (voice.mode != _voice_mode_push_to_talk || now - voice.last_talk_time > CLOSE_AFTER))
			microphone_close();
	}
	while (voice.capture_count >= FRAME)
		say_frame(now, talking);

	/* (hearing) */
	for (index = 0; index < VOICE_SPEAKERS; index++)
	{
		struct speaker *speaker = &voice.speakers[index];

		if (!speaker->used)
			continue;
		if (voice.mode == _voice_mode_off || now - speaker->heard_time > SPEAKER_FORGET)
		{
			speaker_forget(speaker);
			continue;
		}
		speaker_play(speaker, now);
		if (speaker->voice && now - speaker->heard_time > SPEAKER_IDLE && !dsound_voice_queued(speaker->voice))
		{
			dsound_voice_close(speaker->voice);
			speaker->voice = NULL;
			voice_jitter_reset(&speaker->jitter);
		}
	}
	test_log(now);
}

#endif
