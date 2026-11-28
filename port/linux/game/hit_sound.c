/*
HIT_SOUND.C

audio.hit_sounds' tone where the map's globals have no "ting" (the Xbox's
maps' end before it: game_engine_multiplayer_sounds.c). In order:
1. the ting of the player's own Custom Edition sounds.map (where this map's
   family finds it: map_family_resource), its resource
   sound\sfx\impulse\ting\ting;
2. the sound file audio.hit_sound_file names (a WAV of 16-bit PCM, or Ogg
   Vorbis; a name alone is a file in data/sounds beside the maps folder) of
   any length: its first 2 seconds, and no more of the file read than they
   need (4 MB at most);
3. none. Never the countdown's beep, and no sound of Halo's is shipped.
It is read as a multiplayer map without a ting loads, under its loading
screen (hit_sound_map_loaded, from game_engine_initialize_for_new_map), and
again when config.toml changes, on the next tick (hit_sound_update), never
when a hit wants it; it goes when the map does (hit_sound_map_unloaded), so
the next map's resource maps are looked at afresh. It plays through the
mixer as a voice of its own (dsound_sdl.c's dsound_voice_*), in the middle,
at audio.effects_volume. Desktop builds only: the consoles and the dedicated
server have no hit sounds.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "halo_port_features.h"

#if defined(HALO_PORT_VOICE_AUDIO) && defined(HALO_CUSTOM_EDITION)

#include "halo_map_families.h"
#include "content_resource_map.h"
#define HIT_SOUND_VORBIS
#include "hit_sound_decode.h"
#include "../src/port_config.h"
#include "../src/port_folders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *platform_folder(enum port_folder folder);
void platform_log(const char *format, ...);
int posix_make_directory(const char *path);
/* dsound_sdl.c's */
void *dsound_voice_open(void);
void dsound_voice_close(void *voice);
int dsound_voice_submit(void *voice, const short *samples, unsigned long frames);
unsigned long dsound_voice_queued(void *voice);
void dsound_voice_place(void *voice, int mode, const float *position, float minimum_distance, float maximum_distance,
	float gain);
void hit_sound_map_loaded(boolean map_has_ting);
void hit_sound_map_unloaded(void);
void hit_sound_update(void);
void hit_sound_play(void);

enum
{
	HIT_SOUND_MAXIMUM_FRAMES = HIT_SOUND_OUTPUT_RATE * HIT_SOUND_MAXIMUM_SECONDS,
};

static struct
{
	/* a multiplayer map without a ting is loaded: the tone is wanted */
	boolean wanted;
	unsigned long read_at;
	short *samples;
	long frames;
	void *voice;
} hit_sound = { FALSE, (unsigned long)-1 };

static boolean file_read(HANDLE file, unsigned long offset, void *buffer, unsigned long size)
{
	unsigned long bytes_read = 0;

	return SetFilePointer(file, (long)offset, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
		ReadFile(file, buffer, size, &bytes_read, NULL) && bytes_read == size;
}

/* samples (as hit_sound_samples says) made the tone: one channel at the
mixer's rate, at most HIT_SOUND_MAXIMUM_SECONDS */
static boolean tone_make(const unsigned char *data, struct hit_sound_samples const *samples)
{
	short *mono = malloc((size_t)HIT_SOUND_MAXIMUM_SOURCE_FRAMES * sizeof(short));
	long frames = 0;
	int rate = samples->rate;

	/* (Ogg Vorbis: only its first seconds decoded) */
	if (mono && samples->compression == HIT_SOUND_COMPRESSION_OGG)
		frames = hit_sound_ogg_mono(data, samples->size, mono, HIT_SOUND_MAXIMUM_SOURCE_FRAMES, &rate);
	else if (mono)
		frames = hit_sound_mono(data, samples->size, samples->compression, samples->channels, mono,
			(long)rate * HIT_SOUND_MAXIMUM_SECONDS);
	if (!mono || frames <= 0)
	{
		free(mono);
		return FALSE;
	}
	hit_sound.samples = malloc(HIT_SOUND_MAXIMUM_FRAMES * sizeof(short));
	if (hit_sound.samples)
		hit_sound.frames = hit_sound_resample(mono, frames, rate, hit_sound.samples, HIT_SOUND_MAXIMUM_FRAMES);
	free(mono);
	if (hit_sound.frames > 0)
		return TRUE;
	free(hit_sound.samples);
	hit_sound.samples = NULL;
	hit_sound.frames = 0;
	return FALSE;
}

/* the ting of Custom Edition's sounds.map, if the player has it */
static boolean ting_load(void)
{
	char path[256];
	unsigned char header_bytes[CONTENT_RESOURCE_HEADER_SIZE];
	struct content_resource_header header;
	struct content_resource_entry *entries = NULL;
	char *paths = NULL;
	unsigned char *bytes = NULL;
	unsigned long file_size, paths_size, index;
	boolean made = FALSE;
	HANDLE file;

	if (!map_family_resource("sounds", path, sizeof(path)))
		return FALSE;
	file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return FALSE;
	file_size = GetFileSize(file, NULL);
	if (file_size == INVALID_FILE_SIZE || file_size > 0x7fffffff || !file_read(file, 0, header_bytes, sizeof(header_bytes)))
		goto done;
	content_resource_header_read(header_bytes, &header);
	if (header.type != CONTENT_RESOURCE_SOUNDS || !header.count || header.count > 0x10000 ||
		!content_resource_header_within(&header, file_size) || header.table_offset < header.paths_offset ||
		header.table_offset - header.paths_offset > 0x400000)
	{
		goto done;
	}
	paths_size = header.table_offset - header.paths_offset;
	entries = malloc(header.count * sizeof(*entries));
	paths = malloc(paths_size + 1);
	if (!entries || !paths || !file_read(file, header.table_offset, entries, header.count * sizeof(*entries)) ||
		!file_read(file, header.paths_offset, paths, paths_size))
	{
		goto done;
	}
	paths[paths_size] = 0;
	content_resource_table_read(entries, header.count);
	for (index = 0; index < header.count && !made; index++)
	{
		struct content_resource_entry const *entry = &entries[index];
		struct hit_sound_samples samples;
		unsigned char resource[HIT_SOUND_SOUND_SIZE + 4 * HIT_SOUND_PITCH_RANGE_SIZE + HIT_SOUND_PERMUTATION_SIZE];
		unsigned long length = entry->size < sizeof(resource) ? entry->size : (unsigned long)sizeof(resource);

		if (entry->path_offset >= paths_size || !hit_sound_ting_path(paths + entry->path_offset) ||
			entry->offset > file_size || entry->size > file_size - entry->offset ||
			!file_read(file, entry->offset, resource, length) || !hit_sound_ce_samples(resource, length, &samples) ||
			samples.offset > file_size || samples.size > file_size - samples.offset)
		{
			continue;
		}
		bytes = malloc(samples.size);
		if (bytes && file_read(file, samples.offset, bytes, samples.size) && tone_make(bytes, &samples))
		{
			platform_log("hit sounds: the ting of %s", path);
			made = TRUE;
		}
		free(bytes);
		bytes = NULL;
	}
done:
	free(entries);
	free(paths);
	CloseHandle(file);
	return made;
}

/* the player's own sound file (audio.hit_sound_file) */
static boolean file_load(void)
{
	const char *value = config_string("audio.hit_sound_file");
	char folder[1024], path[1024];
	unsigned char *data = NULL;
	long size = -1;
	boolean made = FALSE;
	FILE *file;

	if (!value || !*value)
		return FALSE;
	snprintf(folder, sizeof(folder), "%s/sounds", platform_folder(_port_folder_data));
	posix_make_directory(folder);
	if (!port_folder_file(folder, value, path, sizeof(path)))
	{
		platform_log("hit sounds: \"%s\" is not a file in %s; no hit sound", value, folder);
		return FALSE;
	}
	file = fopen(path, "rb");
	if (file && !fseek(file, 0, SEEK_END))
		size = ftell(file);
	/* (its start alone: the tone is its first seconds) */
	if (size > HIT_SOUND_FILE_READ_BYTES)
		size = HIT_SOUND_FILE_READ_BYTES;
	if (file && size > 0 && !fseek(file, 0, SEEK_SET))
	{
		data = malloc((size_t)size);
		if (data && fread(data, 1, (size_t)size, file) == (size_t)size)
		{
			struct hit_sound_samples samples;

			if (hit_sound_wav(data, (unsigned long)size, &samples))
				made = tone_make(data + samples.offset, &samples);
			else if (size >= 4 && !memcmp(data, "OggS", 4))
			{
				memset(&samples, 0, sizeof(samples));
				samples.compression = HIT_SOUND_COMPRESSION_OGG;
				samples.size = (unsigned long)size;
				made = tone_make(data, &samples);
			}
		}
	}
	if (file)
		fclose(file);
	free(data);
	if (made)
		platform_log("hit sounds: %s", path);
	else
		platform_log("hit sounds: %s is not a WAV (16-bit PCM) or Ogg Vorbis file; no hit sound", path);
	return made;
}

/* the tone let go (closing the voice stops what it plays, before its
samples go) */
static void tone_forget(void)
{
	dsound_voice_close(hit_sound.voice);
	hit_sound.voice = NULL;
	free(hit_sound.samples);
	hit_sound.samples = NULL;
	hit_sound.frames = 0;
}

/* the tone read, if hit sounds are on: the ting, else the player's file */
static void tone_read(void)
{
	tone_forget();
	hit_sound.read_at = config_changes();
	if (config_boolean("audio.hit_sounds") && !ting_load())
		file_load();
}

void hit_sound_map_loaded(boolean map_has_ting)
{
	tone_forget();
	hit_sound.wanted = !map_has_ting;
	if (hit_sound.wanted)
		tone_read();
}

void hit_sound_map_unloaded(void)
{
	tone_forget();
	hit_sound.wanted = FALSE;
}

void hit_sound_update(void)
{
	if (hit_sound.wanted && hit_sound.read_at != config_changes())
		tone_read();
}

/* (only what was read as the map loaded, or after a settings change) */
void hit_sound_play(void)
{
	if (!hit_sound.frames)
		return;
	if (!hit_sound.voice)
		hit_sound.voice = dsound_voice_open();
	/* (one tone at a time: a hit while one plays adds none) */
	if (!hit_sound.voice || dsound_voice_queued(hit_sound.voice))
		return;
	dsound_voice_place(hit_sound.voice, 0, NULL, 0.0f, 0.0f, (float)PIN(config_real("audio.effects_volume"), 0.0, 1.0));
	dsound_voice_submit(hit_sound.voice, hit_sound.samples, (unsigned long)hit_sound.frames);
}

#endif
