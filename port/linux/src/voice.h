/*
VOICE.H

Voice chat's audio (voice.c): the microphone, Opus, and the voices heard,
played through the game's mixer (dsound_sdl.c). What goes over the network
and who hears whom are Delta Peer's and the host's (docs/delta.md, Voice;
port/linux/game/voice_game.c); this side only turns sound into frames and
frames into sound.

Plain types only. Everything is called on the game's thread.
*/

#ifndef HALO_VOICE_H
#define HALO_VOICE_H

/* audio.voice_chat */
enum voice_mode
{
	_voice_mode_off = 0,
	_voice_mode_push_to_talk,
	_voice_mode_open_mic
};

/* the microphone, as the player's settings can show it */
enum voice_microphone
{
	/* not opened yet (it opens at the first push to talk, or with open mic) */
	_voice_microphone_closed = 0,
	_voice_microphone_open,
	/* asked, and the system said no (Android) */
	_voice_microphone_denied,
	/* none could be opened */
	_voice_microphone_missing,
	/* waiting for the system's answer */
	_voice_microphone_asking
};

enum
{
	/* the speakers heard at once, and a speaker's frames' bytes at most */
	VOICE_SPEAKERS = 16,
	VOICE_PACKET_SIZE = 255,
	/* a speaker counts as talking this long after its last frame (ms) */
	VOICE_TALKING_TIME = 300
};

/* a frame this copy's player said, to send */
struct voice_frame
{
	unsigned short sequence;
	int length;
	unsigned char data[VOICE_PACKET_SIZE];
};

/* each frame: the time (milliseconds), whether the player holds push to
talk, whether they may talk now (the host's rules and the channel allow
it), and the size the host's quality allows a frame (bytes; its kbps).
Reads the settings again when they change, takes the microphone's sound,
and decodes and plays what was heard */
void voice_update(unsigned long now, int push_held, int may_talk, int kbps);
/* the next frame said, oldest first: 1 if there was one */
int voice_take_frame(struct voice_frame *frame);
/* a frame heard: its speaker (machine; player for the log), how it is played
(enum delta_voice_route), the channel it was said on (enum
delta_voice_channel) and its bytes */
void voice_heard(unsigned long now, int speaker_machine, int speaker_player, int route, int channel,
	unsigned short sequence, const unsigned char *packet, int length);
/* where a speaker's player is (the game's world units; position NULL: not
known, played in the middle) and how far the host's proximity reaches */
void voice_place(int speaker_machine, const float *position, float range);
/* everything heard forgotten (the game left) */
void voice_reset(void);

/* audio.voice_chat as it is now (enum voice_mode) */
int voice_mode(void);
/* the microphone (enum voice_microphone) */
int voice_microphone(void);
/* whether this copy's player is being sent now (a frame within
VOICE_TALKING_TIME), and a speaker heard now */
int voice_talking(unsigned long now);
int voice_speaker_talking(unsigned long now, int speaker_machine);
/* the channel a speaker was last heard on (0: none) */
int voice_speaker_channel(int speaker_machine);
/* the input's level now, 0 to 1 (for Settings' meter) */
float voice_input_level(void);
/* the channels this copy hears (DELTA_VOICE_*: those whose volume isn't 0,
none with voice off) */
int voice_hears(void);

#endif
