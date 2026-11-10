/*
VOICE_TEST.C

Voice chat's tests that need no game or device (tools/test_voice.py): the
jitter buffer (port/linux/src/voice_jitter.c), and Opus set up as voice.c
sets it up, its frames within the host's size cap at every quality, lost
frames rebuilt from the next one's FEC, and what encoding and decoding cost
on this machine (printed, for docs/delta.md's figures).
*/

#include "../src/voice_jitter.h"
#include "../src/delta_wire.h"

#include <opus.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static unsigned char frame_byte(unsigned short sequence)
{
	return (unsigned char)(sequence * 7 + 1);
}

static void put(struct voice_jitter *jitter, unsigned long now, unsigned short sequence)
{
	unsigned char data[4];

	memset(data, frame_byte(sequence), sizeof(data));
	voice_jitter_put(jitter, now, sequence, data, (int)sizeof(data));
}

static void test_jitter(void)
{
	struct voice_jitter jitter;
	unsigned char data[VOICE_JITTER_PACKET_SIZE];
	int length;

	voice_jitter_reset(&jitter);
	/* nothing yet; one frame waits for the start's time or two more */
	CHECK(voice_jitter_take(&jitter, 0, data, &length) == _voice_jitter_wait);
	put(&jitter, 100, 10);
	CHECK(voice_jitter_take(&jitter, 120, data, &length) == _voice_jitter_wait);
	/* out of order: played in order */
	put(&jitter, 125, 12);
	put(&jitter, 130, 11);
	CHECK(voice_jitter_take(&jitter, 130, data, &length) == _voice_jitter_frame && data[0] == frame_byte(10));
	CHECK(voice_jitter_take(&jitter, 130, data, &length) == _voice_jitter_frame && data[0] == frame_byte(11));
	CHECK(voice_jitter_take(&jitter, 130, data, &length) == _voice_jitter_frame && data[0] == frame_byte(12));
	/* a late frame is dropped; a duplicate too */
	put(&jitter, 140, 11);
	CHECK(jitter.late == 1 && voice_jitter_count(&jitter) == 0);
	/* a lost frame with the next one here: its FEC; then the next itself */
	put(&jitter, 150, 14);
	CHECK(voice_jitter_take(&jitter, 150, data, &length) == _voice_jitter_fec && data[0] == frame_byte(14) &&
		length == 4);
	CHECK(voice_jitter_take(&jitter, 150, data, &length) == _voice_jitter_frame && data[0] == frame_byte(14));
	/* a lost frame with a later one here: concealed */
	put(&jitter, 160, 17);
	CHECK(voice_jitter_take(&jitter, 160, data, &length) == _voice_jitter_lost);
	CHECK(voice_jitter_take(&jitter, 160, data, &length) == _voice_jitter_fec);
	CHECK(voice_jitter_take(&jitter, 160, data, &length) == _voice_jitter_frame && data[0] == frame_byte(17));
	CHECK(jitter.lost == 3);
	/* the speaker stopped: the end, and the next talk starts over */
	CHECK(voice_jitter_take(&jitter, 200, data, &length) == _voice_jitter_end && !jitter.playing);
	put(&jitter, 300, 18);
	CHECK(voice_jitter_take(&jitter, 310, data, &length) == _voice_jitter_wait);
	CHECK(voice_jitter_take(&jitter, 400, data, &length) == _voice_jitter_frame && data[0] == frame_byte(18));
	/* a frame far ahead (the speaker started over) starts the buffer over */
	put(&jitter, 400, 30000);
	CHECK(!jitter.playing && voice_jitter_count(&jitter) == 1);
	/* sequences wrap */
	voice_jitter_reset(&jitter);
	put(&jitter, 0, 65534);
	put(&jitter, 0, 65535);
	put(&jitter, 0, 0);
	CHECK(voice_jitter_take(&jitter, 0, data, &length) == _voice_jitter_frame && data[0] == frame_byte(65534));
	CHECK(voice_jitter_take(&jitter, 0, data, &length) == _voice_jitter_frame && data[0] == frame_byte(65535));
	CHECK(voice_jitter_take(&jitter, 0, data, &length) == _voice_jitter_frame && data[0] == frame_byte(0));
	/* too many waiting: the oldest skipped, so delay can't build up */
	voice_jitter_reset(&jitter);
	{
		unsigned short sequence;

		for (sequence = 100; sequence < 100 + VOICE_JITTER_MOST_WAITING + 4; sequence++)
			put(&jitter, 0, sequence);
		CHECK(voice_jitter_take(&jitter, 0, data, &length) == _voice_jitter_frame &&
			data[0] == frame_byte(104) && jitter.skipped == 4);
	}
	/* bad input refused */
	CHECK(!voice_jitter_put(&jitter, 0, 5, data, 0) && !voice_jitter_put(&jitter, 0, 5, NULL, 4) &&
		!voice_jitter_put(&jitter, 0, 5, data, 256));
}

static double seconds_now(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1.0e-9;
}

/* speech-like test sound: a voice's buzz, its pitch moving, under a
syllable's swell */
static void speech(float *samples, int frames, long start)
{
	int index;

	for (index = 0; index < frames; index++)
	{
		double t = (double)(start + index) / 48000.0;
		double pitch = 140.0 + 40.0 * sin(t * 2.0);
		double swell = 0.5 + 0.5 * sin(t * 2.0 * 3.14159 * 3.0);
		double value = 0.0;
		int harmonic;

		for (harmonic = 1; harmonic <= 12; harmonic++)
			value += sin(2.0 * 3.14159 * pitch * harmonic * t) / harmonic;
		samples[index] = (float)(0.15 * swell * value);
	}
}

static void test_codec(void)
{
	static const int qualities[] = { 8, 16, 24, 32, 48, 64 };
	int quality;

	for (quality = 0; quality < (int)(sizeof(qualities) / sizeof(qualities[0])); quality++)
	{
		int kbps = qualities[quality], error = 0, frame, cap = delta_wire_voice_packet_cap(kbps), largest = 0;
		long bytes = 0;
		OpusEncoder *encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error);
		OpusDecoder *decoder = opus_decoder_create(48000, 1, &error);
		double encode_time = 0.0, decode_time = 0.0;
		unsigned char previous[255];
		int previous_length = 0;

		CHECK(encoder && decoder);
		if (!encoder || !decoder)
			return;
		opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
		opus_encoder_ctl(encoder, OPUS_SET_VBR(1));
		opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
		opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
		opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
		opus_encoder_ctl(encoder, OPUS_SET_BITRATE(kbps * 1000));
		for (frame = 0; frame < 500; frame++)
		{
			float samples[960];
			unsigned char packet[255];
			short pcm[960];
			int length, decoded;
			double start;

			speech(samples, 960, (long)frame * 960);
			start = seconds_now();
			length = opus_encode_float(encoder, samples, 960, packet, cap);
			encode_time += seconds_now() - start;
			CHECK(length > 0 && length <= cap);
			if (length <= 0)
				break;
			bytes += length;
			if (length > largest)
				largest = length;
			start = seconds_now();
			/* (every tenth frame lost: rebuilt from this one's FEC first) */
			if (frame % 10 == 5 && previous_length)
			{
				decoded = opus_decode(decoder, packet, length, pcm, 960, 1);
				CHECK(decoded == 960);
			}
			decoded = opus_decode(decoder, packet, length, pcm, 960, 0);
			decode_time += seconds_now() - start;
			CHECK(decoded == 960);
			memcpy(previous, packet, (size_t)length);
			previous_length = length;
		}
		printf("voice_test: %2d kbps: %3ld bytes a frame on average, %3d at most (cap %3d), encode %.0f us, decode "
			"%.0f us a frame, %.1f KB/s for a talker to a listener on the wire\n", kbps, bytes / 500, largest, cap,
			encode_time / 500 * 1.0e6, decode_time / 500 * 1.0e6,
			/* (Delta's header 12, VOICE_DOWN's 8, UDP and IPv4 28) */
			(bytes / 500.0 + 12 + 8 + 28) * 50.0 / 1000.0);
		opus_encoder_destroy(encoder);
		opus_decoder_destroy(decoder);
	}
}

int main(void)
{
	test_jitter();
	test_codec();
	if (failures)
	{
		fprintf(stderr, "%d checks failed\n", failures);
		return 1;
	}
	printf("voice_test: ok\n");
	return 0;
}
