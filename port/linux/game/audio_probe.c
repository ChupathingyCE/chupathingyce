/*
AUDIO_PROBE.C

A scripted recording of the game's sound, to compare one build's mix with
another's, or the desktop builds' software DirectSound (its I3DL2 reverb and
its obstruction and occlusion low passes: port/linux/src/dsound_sdl.c) with
the Xbox's audio hardware, without microphones
(tools/audio_probe_compare.py).

debug.audio_probe in config.toml (HALO_AUDIO_PROBE) names a probe file, as
the game names its files: d:\audio_probe\retail_maps.txt is in the game's
folder (the data folder, paths.data, on the desktop builds); empty (the
default) leaves all of this off. Once the main menu is up, the game loads
the file's first map as a solo game, stops the player's controls mattering
by moving the camera (a flying camera, as the debug camera's
director_load_camera) to each of the map's spots in turn, and at each plays
the file's sounds at fixed times, ahead of the camera, then leaves the reverb
its tail in silence. The platform records its final mix
(audio_capture_start: audio_probe.h) from the map's first spot to its last;
then the next map, and once the last is done the game quits (a console goes
back to the main menu).

The probe file, one statement a line, # starting a comment:

	map bloodgulch
		a multiplayer map's name, or a level's path (levels\b30\b30); the
		spots after it are the map's
	mute all
		sound classes (sound_classes.c's names, or a part of them: mute
		ambient device music) kept silent, so the ambience is not in the
		recording; all for every class but those of the probe's sounds
	settle 4
		seconds at each spot before its first sound, for the environment
		to be the spot's
	tail 6
		seconds of silence after a spot's last sound, for the reverb's tail
	sound <seconds> <distance> <label> <tag>
		at every spot, the sound tag (sound\sfx\weapons\pistol\fire) that
		many seconds after the spot settled, that many world units ahead
		of the camera; the label names it in the log
	spot <name> <x> <y> <z> <yaw> [<pitch>]
		a camera position, its facing in degrees

debug.audio_capture_seconds (a number of seconds; 0, the default, for none)
records real play instead, with no probe file: each time this machine's
first player is in a game (a solo, system link or online one) with a unit,
the platform records its final mix for that many seconds, as
d:\audio_probe\capture-<map>-<n>-<platform>-*.wav, and a log beside it
(-events.txt): the player's position each second, and every sound a channel
starts, with the channel's type, gain, obstruction and occlusion. Then
nothing until the next game. The folder d:\audio_probe must be there.

Every sound plays its first permutation, from the same random seed, so that
two runs play the same samples at the same pitch.

What happens is written beside the probe file as <probe>-<platform>-
events.txt (and in debug.txt): each spot, each sound with the recording's
frame it started at, and each change of the listener's I3DL2 properties.
The recording is <probe>-<platform>-*.wav (audio_probe.h). A file of
several maps has a recording and a log for each, <probe>-<map>-<platform>.
*/

/* port: a console may leave all of this out (HALO_CONSOLE_NO_AUDIO_PROBE),
and its two calls then do nothing */
#ifndef HALO_CONSOLE_NO_AUDIO_PROBE

#include "cseries.h"
#include "main/main.h"
#include "game/game.h"
#include "game/players.h"
#include "camera/director.h"
#include "camera/observer.h"
#include "scenario/scenario.h"
#include "sound/sound_classes.h"
#include "sound/sound_definitions.h"
#include "sound/game_sound.h"
#include "tag_files/tag_groups.h"
#include "cache/cache_files.h"
#include "interface/ui_widget.h"
#include "audio_probe.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *config_string(char const *name);
double config_real(char const *name);
unsigned long system_milliseconds(void);
/* (sound_classes.c's) */
extern char const *sound_class_names[];
void platform_log(char const *format, ...);

#define AUDIO_PROBE_MAXIMUM_MAPS 8
#define AUDIO_PROBE_MAXIMUM_SOUNDS 16
#define AUDIO_PROBE_MAXIMUM_SPOTS 16
#define AUDIO_PROBE_MAXIMUM_MUTES 8
#define AUDIO_PROBE_NAME_SIZE 64
#define AUDIO_PROBE_PATH_SIZE 128
/* seconds at the main menu before the map is loaded, and in the map before
the first spot (the map's sounds cached, its start over) */
#define AUDIO_PROBE_MENU_WAIT 2.0f
#define AUDIO_PROBE_MAP_WAIT 3.0f
/* the seed every sound is played from */
#define AUDIO_PROBE_SEED 0x51A7E5EDUL
/* (sound_classes.c's NUMBER_OF_SOUND_CLASSES) */
#define AUDIO_PROBE_SOUND_CLASSES 51
#define AUDIO_PROBE_RADIANS_PER_DEGREE 0.017453292519943295

enum
{
	_audio_probe_off,
	_audio_probe_menu,
	_audio_probe_loading,
	_audio_probe_waiting,
	_audio_probe_spots,
	_audio_probe_done
};

struct audio_probe_sound
{
	real time;
	real distance;
	char label[AUDIO_PROBE_NAME_SIZE];
	char tag[AUDIO_PROBE_PATH_SIZE];
};

struct audio_probe_spot
{
	char name[AUDIO_PROBE_NAME_SIZE];
	short map_index;
	real_point3d position;
	real yaw, pitch;
};

static struct
{
	boolean checked;
	short state;
	char maps[AUDIO_PROBE_MAXIMUM_MAPS][AUDIO_PROBE_PATH_SIZE];
	short map_count;
	/* the probe file's path without its extension, and the current map's
	recording's */
	char base[AUDIO_PROBE_PATH_SIZE];
	char prefix[AUDIO_PROBE_PATH_SIZE + AUDIO_PROBE_NAME_SIZE];
	char mutes[AUDIO_PROBE_MAXIMUM_MUTES][AUDIO_PROBE_NAME_SIZE];
	short mute_count;
	real settle, tail;
	struct audio_probe_sound sounds[AUDIO_PROBE_MAXIMUM_SOUNDS];
	short sound_count;
	struct audio_probe_spot spots[AUDIO_PROBE_MAXIMUM_SPOTS];
	short spot_count;

	/* the run, timed by the system's clock (the frame's seconds are not
	the same on every platform's main loop) */
	unsigned long clock;
	real timer;
	/* whether the wait for the map to load has been said to be long */
	boolean told_waiting, told_started;
	short map_index;
	long request_time;
	short spot_index;
	short next_sound_index;
	short event_count;
	boolean capturing;
	FILE *events;
	/* the listener's last I3DL2 properties, logged again at each spot */
	struct audio_probe_environment environment;
	boolean have_environment;
} audio_probe;

static void audio_probe_log_environment(void);

/* ---------- the log */

static void audio_probe_log(char const *format, ...)
{
	char line[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	platform_log("audio probe: %s", line);
	if (audio_probe.events)
	{
		fprintf(audio_probe.events, "%s\n", line);
		fflush(audio_probe.events);
	}
}

/* ---------- the probe file */

static char *audio_probe_token(char **cursor)
{
	char *start = *cursor;

	while (*start == ' ' || *start == '\t')
		start++;
	if (!*start)
		return NULL;
	*cursor = start;
	while (**cursor && **cursor != ' ' && **cursor != '\t')
		(*cursor)++;
	if (**cursor)
		*(*cursor)++ = 0;
	return start;
}

/* the rest of the line, its spaces trimmed (a tag's path may have spaces) */
static char *audio_probe_rest(char **cursor)
{
	char *start = *cursor, *end;

	while (*start == ' ' || *start == '\t')
		start++;
	end = start + strlen(start);
	while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = 0;
	return *start ? start : NULL;
}

static void audio_probe_copy(char *destination, size_t size, char const *source)
{
	snprintf(destination, size, "%s", source);
}

static boolean audio_probe_read(char const *path)
{
	char line[512];
	long line_number = 0;
	FILE *file = fopen(path, "r");

	if (!file)
	{
		platform_log("audio probe: cannot read %s", path);
		return FALSE;
	}
	audio_probe.settle = 4.0f;
	audio_probe.tail = 6.0f;
	while (fgets(line, sizeof(line), file))
	{
		char *cursor = line, *comment = strchr(line, '#'), *keyword;
		boolean valid = TRUE;

		line_number++;
		if (comment)
			*comment = 0;
		line[strcspn(line, "\r\n")] = 0;
		keyword = audio_probe_token(&cursor);
		if (!keyword)
			continue;
		if (!strcmp(keyword, "map"))
		{
			char *map = audio_probe_token(&cursor);

			valid = map != NULL && audio_probe.map_count < AUDIO_PROBE_MAXIMUM_MAPS;
			if (valid)
				audio_probe_copy(audio_probe.maps[audio_probe.map_count++], AUDIO_PROBE_PATH_SIZE, map);
		}
		else if (!strcmp(keyword, "mute"))
		{
			char *name;

			while ((name = audio_probe_token(&cursor)) != NULL && audio_probe.mute_count < AUDIO_PROBE_MAXIMUM_MUTES)
				audio_probe_copy(audio_probe.mutes[audio_probe.mute_count++], AUDIO_PROBE_NAME_SIZE, name);
		}
		else if (!strcmp(keyword, "settle") || !strcmp(keyword, "tail"))
		{
			char *value = audio_probe_token(&cursor);

			valid = value != NULL && atof(value) >= 0.0;
			if (valid)
				*(keyword[0] == 's' ? &audio_probe.settle : &audio_probe.tail) = (real)atof(value);
		}
		else if (!strcmp(keyword, "sound"))
		{
			char *time = audio_probe_token(&cursor);
			char *distance = time ? audio_probe_token(&cursor) : NULL;
			char *label = distance ? audio_probe_token(&cursor) : NULL;
			char *tag = label ? audio_probe_rest(&cursor) : NULL;

			valid = tag != NULL && audio_probe.sound_count < AUDIO_PROBE_MAXIMUM_SOUNDS;
			if (valid)
			{
				struct audio_probe_sound *sound = &audio_probe.sounds[audio_probe.sound_count];

				sound->time = (real)atof(time);
				sound->distance = (real)atof(distance);
				audio_probe_copy(sound->label, sizeof(sound->label), label);
				audio_probe_copy(sound->tag, sizeof(sound->tag), tag);
				/* (in the order they play) */
				while (audio_probe.sound_count > 0 && sound > audio_probe.sounds && sound[-1].time > sound->time)
				{
					struct audio_probe_sound earlier = sound[-1];

					sound[-1] = *sound;
					*sound = earlier;
					sound--;
				}
				audio_probe.sound_count++;
			}
		}
		else if (!strcmp(keyword, "spot"))
		{
			char *name = audio_probe_token(&cursor);
			char *values[5];
			short count = 0;

			while (name && count < 5 && (values[count] = audio_probe_token(&cursor)) != NULL)
				count++;
			valid = name != NULL && count >= 4 && audio_probe.map_count > 0 &&
				audio_probe.spot_count < AUDIO_PROBE_MAXIMUM_SPOTS;
			if (valid)
			{
				struct audio_probe_spot *spot = &audio_probe.spots[audio_probe.spot_count++];

				audio_probe_copy(spot->name, sizeof(spot->name), name);
				spot->map_index = audio_probe.map_count - 1;
				spot->position.x = (real)atof(values[0]);
				spot->position.y = (real)atof(values[1]);
				spot->position.z = (real)atof(values[2]);
				spot->yaw = (real)atof(values[3]);
				spot->pitch = count > 4 ? (real)atof(values[4]) : 0.0f;
			}
		}
		else
		{
			valid = FALSE;
		}
		if (!valid)
			platform_log("audio probe: %s line %ld is not understood (or one too many)", path, line_number);
	}
	fclose(file);
	if (!audio_probe.map_count || !audio_probe.sound_count || !audio_probe.spot_count)
	{
		platform_log("audio probe: %s needs a map, a sound and a spot", path);
		return FALSE;
	}
	/* (each map's spots after it, every map with one) */
	{
		short map_index;

		for (map_index = 0; map_index < audio_probe.map_count; map_index++)
		{
			short spot_index;

			for (spot_index = 0; spot_index < audio_probe.spot_count; spot_index++)
			{
				if (audio_probe.spots[spot_index].map_index == map_index)
					break;
			}
			if (spot_index == audio_probe.spot_count)
			{
				platform_log("audio probe: %s: the map %s has no spot", path, audio_probe.maps[map_index]);
				return FALSE;
			}
		}
	}
	return TRUE;
}

/* the last part of a path */
static char *audio_probe_leaf(char *path)
{
	char *leaf = path, *cursor;

	for (cursor = path; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			leaf = cursor + 1;
	}
	return leaf;
}

static void audio_probe_read_settings(void)
{
	char const *path = config_string("debug.audio_probe");
	char *extension, *separator;

	(void)separator;

	audio_probe.checked = TRUE;
	audio_probe.state = _audio_probe_off;
	if (!path || !path[0] || !audio_probe_read(path))
		return;
	/* (the files beside the probe file, named after it) */
	audio_probe_copy(audio_probe.base, sizeof(audio_probe.base), path);
	extension = strrchr(audio_probe.base, '.');
	separator = audio_probe_leaf(audio_probe.base);
	if (extension && extension > separator)
		*extension = 0;
	audio_probe.state = _audio_probe_menu;
	platform_log("audio probe: armed: %s: %d maps, %d spots, %d sounds a spot; the first map loads %.0f s after "
		"the main menu", path, audio_probe.map_count, audio_probe.spot_count, audio_probe.sound_count,
		AUDIO_PROBE_MENU_WAIT);
}

/* ---------- the run */

static void audio_probe_facing(struct audio_probe_spot const *spot, real_vector3d *forward)
{
	double yaw = spot->yaw * AUDIO_PROBE_RADIANS_PER_DEGREE, pitch = spot->pitch * AUDIO_PROBE_RADIANS_PER_DEGREE;

	forward->i = (real)(cos(yaw) * cos(pitch));
	forward->j = (real)(sin(yaw) * cos(pitch));
	forward->k = (real)sin(pitch);
}

static void audio_probe_spot_start(void)
{
	struct audio_probe_spot const *spot = &audio_probe.spots[audio_probe.spot_index];
	real_vector3d forward;

	audio_probe_facing(spot, &forward);
	director_set_flying_camera(0, &spot->position, &forward);
	audio_probe.timer = 0.0f;
	audio_probe.next_sound_index = 0;
	audio_probe_log("spot index=%d name=%s frame=%lu x=%.3f y=%.3f z=%.3f yaw=%.1f pitch=%.1f",
		audio_probe.spot_index, spot->name, audio_capture_frames(), spot->position.x, spot->position.y,
		spot->position.z, spot->yaw, spot->pitch);
	audio_probe_log_environment();
}

static void audio_probe_play(struct audio_probe_sound const *sound)
{
	struct audio_probe_spot const *spot = &audio_probe.spots[audio_probe.spot_index];
	long definition_index = tag_loaded(SOUND_DEFINITION_TAG, sound->tag);
	struct sound_location location;
	real_vector3d ahead;
	long sound_index;
	struct observer_result const *camera;

	if (definition_index == NONE)
	{
		audio_probe_log("missing spot=%s sound=%s tag=%s", spot->name, sound->label, sound->tag);
		return;
	}
	/* the first permutation of each pitch range, and the same seed for the
	pitch and gain the sound picks, on every platform */
	{
		struct sound_definition *definition = sound_definition_get(definition_index);
		long range_index;

		for (range_index = 0; range_index < definition->pitch_ranges.count; range_index++)
		{
			struct sound_pitch_range *range = TAG_BLOCK_GET_ELEMENT(&definition->pitch_ranges, range_index,
				struct sound_pitch_range);

			if (range->permutations.count > 0)
				range->forced_permutation_index = 0;
		}
		/* (its class heard, if muted) */
		if (audio_probe.mute_count && definition->sound_class >= 0 && definition->sound_class < AUDIO_PROBE_SOUND_CLASSES &&
			sound_class_names[definition->sound_class][0])
		{
			debug_sound_classes_enable(sound_class_names[definition->sound_class], TRUE);
			sound_class_set_gain(sound_class_names[definition->sound_class], 1.0f, 0);
		}
	}
	*get_global_local_random_seed_address() = AUDIO_PROBE_SEED + (unsigned long)audio_probe.event_count;

	/* ahead of the camera, level with it */
	ahead.i = (real)cos(spot->yaw * AUDIO_PROBE_RADIANS_PER_DEGREE);
	ahead.j = (real)sin(spot->yaw * AUDIO_PROBE_RADIANS_PER_DEGREE);
	ahead.k = 0.0f;
	csmemset(&location, 0, sizeof(location));
	location.position.x = spot->position.x + ahead.i * sound->distance;
	location.position.y = spot->position.y + ahead.j * sound->distance;
	location.position.z = spot->position.z;
	location.forward.i = -ahead.i;
	location.forward.j = -ahead.j;
	location.forward.k = 0.0f;
	scenario_location_from_point(&location.game_location, &location.position);
	sound_index = unattached_impulse_sound_new(definition_index, &location, 1.0f);
	/* (and where the camera is, which is where the listener is) */
	camera = observer_get_camera(0);
	audio_probe_log("sound index=%d spot=%s label=%s frame=%lu distance=%.2f started=%s camera=%.2f,%.2f,%.2f tag=%s",
		audio_probe.event_count, spot->name, sound->label, audio_capture_frames(), sound->distance,
		sound_index != NONE ? "yes" : "no", camera->position.x, camera->position.y, camera->position.z, sound->tag);
	audio_probe.event_count++;
}

static real audio_probe_spot_length(void)
{
	return audio_probe.settle + audio_probe.sounds[audio_probe.sound_count - 1].time + audio_probe.tail;
}

/* the map's recording and log finished */
static void audio_probe_map_done(void)
{
	if (audio_probe.capturing)
	{
		audio_probe_log("end frame=%lu", audio_capture_frames());
		audio_capture_stop();
		audio_probe.capturing = FALSE;
		platform_log("audio probe: the recording is %s-*.wav", audio_probe.prefix);
	}
	if (audio_probe.events)
	{
		fclose(audio_probe.events);
		audio_probe.events = NULL;
	}
}

static void audio_probe_finish(void)
{
	audio_probe_map_done();
	audio_probe.state = _audio_probe_done;
	platform_log("audio probe: done");
#ifdef HALO_CONSOLE_NO_EXIT
	main_goto_main_menu();
#else
	exit(0);
#endif
}

/* the map loaded, as the main menu's campaign does (or the map_name
command, in a game) */
static void audio_probe_load_map(short map_index)
{
	char path[AUDIO_PROBE_PATH_SIZE];
	char const *map = audio_probe.maps[map_index];

	if (strchr(map, '\\'))
		audio_probe_copy(path, sizeof(path), map);
	else
		snprintf(path, sizeof(path), "levels\\test\\%s\\%s", map, map);
	platform_log("audio probe: load requested: %s", path);
	audio_probe.map_index = map_index;
	audio_probe.request_time = game_in_progress() ? game_time_get() : 0;
	game_connection_set(_game_connection_local);
	main_set_map_name(path);
	main_menu_switch_to_single_player();
	audio_probe.state = _audio_probe_loading;
	audio_probe.timer = 0.0f;
	audio_probe.told_waiting = FALSE;
	audio_probe.told_started = FALSE;
}

/* ---------- recording real play (debug.audio_capture_seconds) */

static struct
{
	boolean checked;
	real seconds;
	boolean capturing;
	/* recorded in this game already, and the game (its map and the game
	time it was at) */
	boolean done;
	char map[64];
	long last_time;
	unsigned long clock, started, last_position;
	short count;
	char prefix[96];
	FILE *events;
} play_capture;

static void play_capture_stop(char const *why)
{
	if (!play_capture.capturing)
		return;
	if (play_capture.events)
	{
		fprintf(play_capture.events, "end frame=%lu why=%s\n", audio_capture_frames(), why);
		fclose(play_capture.events);
		play_capture.events = NULL;
	}
	audio_capture_stop();
	play_capture.capturing = FALSE;
	platform_log("audio capture: %s-*.wav done (%s)", play_capture.prefix, why);
}

static void play_capture_update(boolean main_menu_loaded)
{
	long player_index = local_player_get_player_index(0);
	boolean in_game = !main_menu_loaded && game_in_progress() && player_index != NONE;
	long unit_index = in_game ? player_get(player_index)->unit_index : NONE;
	char const *map = in_game ? cache_file_loaded_map_name() : "";
	unsigned long now = system_milliseconds();

	if (!play_capture.checked)
	{
		play_capture.checked = TRUE;
		play_capture.seconds = (real)config_real("debug.audio_capture_seconds");
		if (play_capture.seconds > 0.0f)
			platform_log("audio capture: armed: %.0f s of each game to d:\\audio_probe", play_capture.seconds);
	}
	if (play_capture.seconds <= 0.0f)
		return;
	/* (a new game: out of one, or another map, or its time started over) */
	if (!in_game || strcmp(map, play_capture.map) || game_time_get() < play_capture.last_time)
	{
		play_capture_stop(in_game ? "a new game" : "the game ended");
		play_capture.done = FALSE;
		audio_probe_copy(play_capture.map, sizeof(play_capture.map), map ? map : "");
	}
	play_capture.last_time = in_game ? game_time_get() : 0;
	if (!in_game)
		return;
	if (!play_capture.capturing && !play_capture.done && unit_index != NONE)
	{
		char path[160];
		char const *leaf = map;
		FILE *existing;

		play_capture.done = TRUE;
		for (; *map; map++)
		{
			if (*map == '\\' || *map == '/')
				leaf = map + 1;
		}
		/* (the next number not used, so that a run never writes over the last) */
		do
		{
			play_capture.count++;
			snprintf(play_capture.prefix, sizeof(play_capture.prefix), "d:\\audio_probe\\capture-%s-%d-%s", leaf,
				play_capture.count, audio_capture_platform());
			snprintf(path, sizeof(path), "%s-events.txt", play_capture.prefix);
			existing = fopen(path, "r");
			if (existing)
				fclose(existing);
		}
		while (existing && play_capture.count < 999);
		play_capture.events = fopen(path, "w");
		if (!play_capture.events || !audio_capture_start(play_capture.prefix))
		{
			platform_log("audio capture: cannot record %s (is there a folder d:\\audio_probe?)", play_capture.prefix);
			if (play_capture.events)
				fclose(play_capture.events);
			play_capture.events = NULL;
			return;
		}
		play_capture.capturing = TRUE;
		play_capture.started = now;
		play_capture.last_position = 0;
		fprintf(play_capture.events, "start platform=%s map=%s rate=%lu connection=%d seconds=%.0f\n",
			audio_capture_platform(), leaf, audio_capture_rate(), game_connection(), play_capture.seconds);
		platform_log("audio capture: recording %s-*.wav for %.0f s", play_capture.prefix, play_capture.seconds);
	}
	if (!play_capture.capturing)
		return;
	audio_capture_update();
	if (now - play_capture.last_position >= 1000 || !play_capture.last_position)
	{
		struct observer_result const *camera = observer_get_camera(0);

		play_capture.last_position = now;
		fprintf(play_capture.events, "position frame=%lu time=%.1f alive=%d x=%.2f y=%.2f z=%.2f\n",
			audio_capture_frames(), (now - play_capture.started) * 0.001f, unit_index != NONE,
			camera->position.x, camera->position.y, camera->position.z);
		fflush(play_capture.events);
	}
	if (now - play_capture.started >= (unsigned long)(play_capture.seconds * 1000.0f))
		play_capture_stop("its time is up");
}

void audio_probe_sound_started(short channel, short type, char const *name, long bytes, float gain,
	float obstruction, float occlusion, int spatialized)
{
	if (!play_capture.capturing || !play_capture.events)
		return;
	fprintf(play_capture.events, "sound frame=%lu channel=%d type=%d gain=%.2f obstruction=%.2f occlusion=%.2f "
		"spatialized=%d bytes=%ld name=%.32s\n", audio_capture_frames(), channel, type, gain, obstruction, occlusion,
		spatialized, bytes, name);
}

void audio_probe_update(
	unsigned char main_menu_loaded,
	float seconds)
{
	if (!audio_probe.checked)
		audio_probe_read_settings();
	/* (real play's recording, when no probe runs or once it is done) */
	if (audio_probe.state == _audio_probe_off || audio_probe.state == _audio_probe_done)
		play_capture_update(main_menu_loaded || main_menu_is_active());
	if (audio_probe.state == _audio_probe_off || audio_probe.state == _audio_probe_done)
		return;
	if (audio_probe.capturing)
		audio_capture_update();
	(void)seconds;
	{
		unsigned long now = system_milliseconds();

		if (audio_probe.clock)
			audio_probe.timer += PIN((real)(now - audio_probe.clock) * 0.001f, 0.0f, 1.0f);
		audio_probe.clock = now;
	}
	/* (the main menu as the main loop has it, or as its widgets do) */
	main_menu_loaded = main_menu_loaded || main_menu_is_active();

	switch (audio_probe.state)
	{
	case _audio_probe_menu:
		if (!main_menu_loaded)
		{
			audio_probe.timer = 0.0f;
		}
		else if (!audio_probe.told_waiting)
		{
			audio_probe.told_waiting = TRUE;
			platform_log("audio probe: the main menu is up");
		}
		if (main_menu_loaded && audio_probe.timer >= AUDIO_PROBE_MENU_WAIT)
			audio_probe_load_map(0);
		else if (!main_menu_loaded && !audio_probe.told_started)
		{
			audio_probe.told_started = TRUE;
			platform_log("audio probe: waiting for the main menu (scenario loaded %d, its widgets %d)",
				main_menu_loaded, main_menu_is_active());
		}
		break;

	case _audio_probe_loading:
		/* (a map after the first: its game time started over) */
		if (!main_menu_loaded && game_in_progress() && local_player_get_player_index(0) != NONE &&
			(!audio_probe.request_time || game_time_get() < audio_probe.request_time))
		{
			platform_log("audio probe: load done: %s is up; its first spot in %.0f s",
				audio_probe.maps[audio_probe.map_index], AUDIO_PROBE_MAP_WAIT);
			audio_probe.state = _audio_probe_waiting;
			audio_probe.timer = 0.0f;
		}
		else if (!main_menu_loaded && !audio_probe.told_started)
		{
			audio_probe.told_started = TRUE;
			platform_log("audio probe: load started (the main menu is gone)");
		}
		if (audio_probe.state == _audio_probe_loading && audio_probe.timer >= 60.0f && !audio_probe.told_waiting)
		{
			audio_probe.told_waiting = TRUE;
			platform_log("audio probe: still waiting for %s after a minute (main menu %d, game %d, local player %ld)",
				audio_probe.maps[audio_probe.map_index], main_menu_loaded, game_in_progress(),
				local_player_get_player_index(0));
		}
		break;

	case _audio_probe_waiting:
		if (audio_probe.timer >= AUDIO_PROBE_MAP_WAIT)
		{
			char path[AUDIO_PROBE_PATH_SIZE + AUDIO_PROBE_NAME_SIZE + 16];
			short index;

			/* (no new sounds of the class, and those playing silent; every
			class's name has the empty string in it) */
			for (index = 0; index < audio_probe.mute_count; index++)
			{
				char const *name = strcmp(audio_probe.mutes[index], "all") ? audio_probe.mutes[index] : "";

				debug_sound_classes_enable(name, FALSE);
				sound_class_set_gain(name, 0.0f, 0);
			}
			if (audio_probe.map_count > 1)
				snprintf(audio_probe.prefix, sizeof(audio_probe.prefix), "%s-%s-%s", audio_probe.base,
					audio_probe_leaf(audio_probe.maps[audio_probe.map_index]), audio_capture_platform());
			else
				snprintf(audio_probe.prefix, sizeof(audio_probe.prefix), "%s-%s", audio_probe.base,
					audio_capture_platform());
			snprintf(path, sizeof(path), "%s-events.txt", audio_probe.prefix);
			audio_probe.events = fopen(path, "w");
			if (!audio_probe.events)
				platform_log("audio probe: cannot write %s", path);
			audio_probe.capturing = audio_capture_start(audio_probe.prefix) != 0;
			if (!audio_probe.capturing)
			{
				audio_probe_log("cannot record on this platform");
				audio_probe_finish();
				return;
			}
			audio_probe_log("start platform=%s map=%s rate=%lu settle=%.2f tail=%.2f",
				audio_capture_platform(), audio_probe.maps[audio_probe.map_index], audio_capture_rate(),
				audio_probe.settle, audio_probe.tail);
			audio_probe.state = _audio_probe_spots;
			for (audio_probe.spot_index = 0;
				audio_probe.spots[audio_probe.spot_index].map_index != audio_probe.map_index;
				audio_probe.spot_index++);
			audio_probe_spot_start();
		}
		break;

	case _audio_probe_spots:
		while (audio_probe.next_sound_index < audio_probe.sound_count &&
			audio_probe.timer >= audio_probe.settle + audio_probe.sounds[audio_probe.next_sound_index].time)
		{
			audio_probe_play(&audio_probe.sounds[audio_probe.next_sound_index++]);
		}
		if (audio_probe.timer >= audio_probe_spot_length())
		{
			short next = audio_probe.spot_index + 1;

			while (next < audio_probe.spot_count && audio_probe.spots[next].map_index != audio_probe.map_index)
				next++;
			if (next < audio_probe.spot_count)
			{
				audio_probe.spot_index = next;
				audio_probe_spot_start();
			}
			else
			{
				audio_probe_map_done();
				if (audio_probe.map_index + 1 < audio_probe.map_count)
					audio_probe_load_map(audio_probe.map_index + 1);
				else
					audio_probe_finish();
			}
		}
		break;
	}
}

static void audio_probe_log_environment(void)
{
	struct audio_probe_environment const *environment = &audio_probe.environment;

	if (!audio_probe.have_environment)
		return;
	audio_probe_log("environment frame=%lu room=%ld room_hf=%ld room_rolloff=%.3f decay_time=%.3f "
		"decay_hf_ratio=%.3f reflections=%ld reflections_delay=%.4f reverb=%ld reverb_delay=%.4f "
		"diffusion=%.1f density=%.1f hf_reference=%.1f",
		audio_capture_frames(), environment->room, environment->room_hf, environment->room_rolloff_factor,
		environment->decay_time, environment->decay_hf_ratio, environment->reflections,
		environment->reflections_delay, environment->reverb, environment->reverb_delay, environment->diffusion,
		environment->density, environment->hf_reference);
}

void audio_probe_environment_changed(
	struct audio_probe_environment const *environment)
{
	if (audio_probe.state == _audio_probe_off)
		return;
	audio_probe.environment = *environment;
	audio_probe.have_environment = TRUE;
	if (audio_probe.state == _audio_probe_spots)
		audio_probe_log_environment();
}

#else

#include "audio_probe.h"

void audio_probe_update(unsigned char main_menu_loaded, float seconds)
{
	(void)main_menu_loaded;
	(void)seconds;
}

void audio_probe_environment_changed(struct audio_probe_environment const *environment)
{
	(void)environment;
}

void audio_probe_sound_started(short channel, short type, char const *name, long bytes, float gain,
	float obstruction, float occlusion, int spatialized)
{
	(void)channel;
	(void)type;
	(void)name;
	(void)bytes;
	(void)gain;
	(void)obstruction;
	(void)occlusion;
	(void)spatialized;
}

#endif
