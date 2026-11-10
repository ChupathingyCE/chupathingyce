/*
VOICE_JITTER.H

A speaker's jitter buffer for voice chat (voice.c): the Opus frames heard
from one speaker, kept by their sequence and given back in order, one 20 ms
frame at a time. Nothing here decodes; the caller does, as take says:

- a frame: decode it;
- a lost frame whose next one is here: decode the next one's forward error
  correction (Opus's in-band FEC) in its place;
- a lost frame with nothing after it here yet: Opus's loss concealment;
- the end: the speaker stopped (nothing buffered), and playing starts over
  once enough frames come again.

Playing starts once VOICE_JITTER_START_FRAMES frames are in, or the first
has waited VOICE_JITTER_START_TIME. A frame older than the one playing is
late and dropped; one too far ahead starts the buffer over. No platform,
no allocation: the tests drive it directly (port/linux/tests/voice_test.c).
*/

#ifndef HALO_VOICE_JITTER_H
#define HALO_VOICE_JITTER_H

enum
{
	/* frames kept (320 ms) */
	VOICE_JITTER_SLOTS = 16,
	/* the most bytes a frame is (the wire's: a byte's length) */
	VOICE_JITTER_PACKET_SIZE = 255,
	/* playing starts with this many frames in (60 ms), or once the first
	has waited this long (milliseconds) */
	VOICE_JITTER_START_FRAMES = 3,
	VOICE_JITTER_START_TIME = 60,
	/* more than this many frames waiting: the oldest is skipped, so that a
	speaker's clock running fast doesn't build up delay */
	VOICE_JITTER_MOST_WAITING = 8
};

enum voice_jitter_result
{
	/* nothing to play yet */
	_voice_jitter_wait = 0,
	/* the frame to play: decode it */
	_voice_jitter_frame,
	/* the frame was lost: decode the next one's FEC (the bytes given) */
	_voice_jitter_fec,
	/* the frame was lost: conceal it */
	_voice_jitter_lost,
	/* the speaker stopped: nothing buffered */
	_voice_jitter_end
};

struct voice_jitter_slot
{
	unsigned char used;
	unsigned char length;
	unsigned short sequence;
	unsigned char data[VOICE_JITTER_PACKET_SIZE];
};

struct voice_jitter
{
	int playing;
	/* the next sequence to play (playing) */
	unsigned short next;
	/* when the first frame of a start came (milliseconds), and whether one
	has */
	int has_first;
	unsigned long first_time;
	struct voice_jitter_slot slots[VOICE_JITTER_SLOTS];
	/* counted for the log and the tests */
	unsigned long late;
	unsigned long lost;
	unsigned long skipped;
};

void voice_jitter_reset(struct voice_jitter *jitter);
/* a frame heard (length 1 to VOICE_JITTER_PACKET_SIZE): 1 if kept */
int voice_jitter_put(struct voice_jitter *jitter, unsigned long now, unsigned short sequence,
	const unsigned char *data, int length);
/* the next frame to play (enum voice_jitter_result); with a frame or FEC,
its bytes into data (VOICE_JITTER_PACKET_SIZE) and length */
int voice_jitter_take(struct voice_jitter *jitter, unsigned long now, unsigned char *data, int *length);
/* the frames kept now */
int voice_jitter_count(const struct voice_jitter *jitter);

#endif
