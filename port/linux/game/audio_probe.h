/*
AUDIO_PROBE.H

The audio probe (audio_probe.c): a scripted recording of the game's sound,
to compare one build's mix with another's or with the Xbox's.
*/

#ifndef __AUDIO_PROBE_H
#define __AUDIO_PROBE_H

/* the I3DL2 listener properties the game gave the platform's DirectSound
(sound_dsound_xbox.c's dsound_set_listener_properties), in DSI3DL2LISTENER's
order and units */
struct audio_probe_environment
{
	long room, room_hf;
	float room_rolloff_factor, decay_time, decay_hf_ratio;
	long reflections;
	float reflections_delay;
	long reverb;
	float reverb_delay, diffusion, density, hf_reference;
};

/* called from the main loop every frame (main.c) */
void audio_probe_update(unsigned char main_menu_loaded, float seconds);
/* a new listener environment (sound_dsound_xbox.c) */
void audio_probe_environment_changed(struct audio_probe_environment const *environment);

/* a channel started a sound (sound_dsound_xbox.c), for debug.audio_capture_seconds's
log */
void audio_probe_sound_started(short channel, short type, char const *name, long bytes, float gain,
	float obstruction, float occlusion, int spatialized);

/* ---------- the platform's recording of its final mix

Each platform's DirectSound records what it plays from audio_capture_start
to audio_capture_stop: the desktop builds' mixer (port/linux/src/dsound_sdl.c)
its output, and the reverb's input and output apart; the Xbox the console's
own output (port/xbox/src/xbox_audio_capture.c). */

/* starts recording to files named path_prefix and a suffix of the
platform's; false if it cannot */
int audio_capture_start(char const *path_prefix);
/* the output frames recorded since the start (a frame is a stereo sample
pair at audio_capture_rate) */
unsigned long audio_capture_frames(void);
unsigned long audio_capture_rate(void);
/* what is recorded written out (from the main loop) */
void audio_capture_update(void);
/* the recording ended, its files finished */
void audio_capture_stop(void);
/* "pc", "xbox": which build recorded, for the files' names */
char const *audio_capture_platform(void);

#endif
