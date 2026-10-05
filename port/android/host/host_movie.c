/*
HOST_MOVIE.C

The movies an Android build plays for the game (port/linux/src/bink_null.c).

The port has no Bink decoder (the RAD SDK is proprietary), so the guest asks
for a movie here and the device's own player (MoviePlayer.java) plays the
port's transcoded file over the game's surface. Nothing blocks: the guest's
BinkOpen asks and goes on, and the engine's playback loop (bink_playback.c)
reads the frame the player is on and ends the movie when it is over.

The guest calls host_movie_*; the player calls the natives below (every
100 ms: what to play, how far it is, and that it ended).
*/

#include "host.h"

#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

enum
{
	_movie_none = 0,
	_movie_intro,
	_movie_credits,
	_movie_attract1,
	_movie_attract2,
	_movie_attract3,
	_movie_count,
};

/* the part of the requested path that names the movie, the asset the player
opens for each language (the suffix the engine puts in the name, attract_mode.c),
and the frames of the transcoded file (an estimate: the frame the engine reads
is taken from the player's position and duration). English is the name with no
suffix; a language with no file leaves the engine to try the next one. */
static struct
{
	const char *match;
	const char *file_es;
	const char *file_en;
	unsigned long width;
	unsigned long height;
	unsigned long frames;
	int requested;
	int playing;
	int finished;
	unsigned long position_ms;
	unsigned long duration_ms;
	/* the asset the player opens, chosen from the language asked for */
	const char *chosen;
} movies[_movie_count] = {
	{ "",         "",                "",              0,   0,   0,    0, 0, 0,  0, 0, NULL },
	{ "intro",    "intro_es.bik",    "intro.bik",     640, 480, 481,  0, 0, 0,  0, 0, NULL },
	{ "credits",  "credits_es.bik",  "credits.bik",   640, 480, 5214, 0, 0, 0,  0, 0, NULL },
	{ "attract1", "attract1_es.bik", "attract1.bik",  640, 480, 4538, 0, 0, 0,  0, 0, NULL },
	{ "attract2", "attract2_es.bik", "attract2.bik",  640, 480, 3982, 0, 0, 0,  0, 0, NULL },
	{ "attract3", "attract3_es.bik", "attract3.bik",  640, 480, 2042, 0, 0, 0,  0, 0, NULL },
};

/* the transcoded files the APK shipped, as MoviePlayer.java lists them at
start-up: a language only plays when its file is really here, so the game
falls back to the one it has (and adding a file is all a new language needs) */
enum
{
	MAXIMUM_MOVIE_ASSETS = 32,
	MOVIE_ASSET_NAME_SIZE = 40,
};

static struct
{
	char name[MOVIE_ASSET_NAME_SIZE];
} movie_assets[MAXIMUM_MOVIE_ASSETS];
static int movie_asset_count;

static int movie_file_available(const char *file)
{
	int index;

	if (!file)
		return 0;

	for (index = 0; index < movie_asset_count; index++)
	{
		if (!strcasecmp(movie_assets[index].name, file))
			return 1;
	}

	return 0;
}

static pthread_mutex_t movie_lock = PTHREAD_MUTEX_INITIALIZER;

/* the player told us its view is attached and it can play */
static int movie_player_ready;

/* when the movie was asked for (a player that never starts one, its file
missing, would leave the engine on a black screen for ever) */
static int64_t movie_opened_ms;

/* the host has no game clock: its own is enough for the guard below */
static int64_t movie_now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* the movie a requested path names, and the asset its language needs.

The engine builds the name from the game's language (attract_mode.c:
"d:\\bink\\intro<language>.bik", the suffix "_es" for Spanish and none for
English) and asks for each language in turn until one exists: the port says
which of them it has a transcoded file for, and a language with no file
resolves to none so the engine goes on to the next. */
static int movie_resolve(const char *name, const char **file)
{
	const char *filename;
	const char *dot;
	unsigned long length;
	int movie;

	if (file)
		*file = NULL;
	if (!name || !name[0])
		return _movie_none;

	filename = strrchr(name, '\\');
	if (!filename)
		filename = strrchr(name, '/');
	filename = filename ? filename + 1 : name;
	dot = strrchr(filename, '.');
	length = dot ? (unsigned long)(dot - filename) : (unsigned long)strlen(filename);

	for (movie = 1; movie < _movie_count; movie++)
	{
		unsigned long match = (unsigned long)strlen(movies[movie].match);
		const char *suffix;

		if (length < match || strncasecmp(filename, movies[movie].match, match))
			continue;

		suffix = filename + match;
		{
			unsigned long code_length = length - match;

			/* (the engine names a language with a suffix, "_es" for Spanish
			and none for English; the port's snprintf drops the leading
			underscore, so both spellings are taken) */
			if (code_length && *suffix == '_')
			{
				suffix++;
				code_length--;
			}

			if (code_length == 0 && movie_file_available(movies[movie].file_en))
			{
				if (file)
					*file = movies[movie].file_en;
				return movie;
			}
			if (code_length == 2 && !strncmp(suffix, "es", 2) && movie_file_available(movies[movie].file_es))
			{
				if (file)
					*file = movies[movie].file_es;
				return movie;
			}
		}

		/* a language the port has no file for: the engine tries the next */
		return _movie_none;
	}

	return _movie_none;
}

/* whether the host can play the movie a requested path names. The engine
checks that a movie's file exists before opening it (attract_mode.c), the port
ships no Bink file, and so that check is answered here: the movie exists when
the host has a transcoded file for it (file_exists in files_windows.c). */
int host_movie_exists(const char *name)
{
	return movie_resolve(name, NULL) != _movie_none;
}

int host_movie_open(const char *name)
{
	const char *file = NULL;
	int movie = movie_resolve(name, &file);

	if (!movie || !file)
	{
		host_logf(HOST_LOG_INFO, "movie: no file for \"%s\"", name ? name : "");
		return 0;
	}
	if (!movie_player_ready)
	{
		/* (the Surface is not there yet: the player starts as soon as it is,
		and the engine's own guard skips the movie if it never does) */
		host_logf(HOST_LOG_INFO, "movie: waiting for the player's Surface");
	}

	pthread_mutex_lock(&movie_lock);
	movies[movie].chosen = file;
	movies[movie].requested = 1;
	movies[movie].playing = 1;
	movies[movie].finished = 0;
	movies[movie].position_ms = 0;
	pthread_mutex_unlock(&movie_lock);
	movie_opened_ms = movie_now_ms();

	return movie;
}

unsigned long host_movie_width(int movie)
{
	return ((movie) > 0 && (movie) < _movie_count) ? movies[movie].width : 0;
}

unsigned long host_movie_height(int movie)
{
	return ((movie) > 0 && (movie) < _movie_count) ? movies[movie].height : 0;
}

unsigned long host_movie_frames(int movie)
{
	return ((movie) > 0 && (movie) < _movie_count) ? movies[movie].frames : 0;
}

/* the frame the player is on: the engine ends the movie when it reaches the
last one (bink_playback.c, its update) */
unsigned long host_movie_frame(int movie)
{
	unsigned long frame = 0;

	if (!((movie) > 0 && (movie) < _movie_count))
		return 0;

	pthread_mutex_lock(&movie_lock);
	/* (the player never started it: a missing file, or a device that
	refused it. The movie is over as far as the engine is concerned) */
	if (!movies[movie].finished && movies[movie].position_ms == 0 &&
		movie_opened_ms && movie_now_ms() - movie_opened_ms > 5000)
	{
		movies[movie].finished = 1;
		movies[movie].playing = 0;
		movies[movie].requested = 0;
		host_logf(HOST_LOG_INFO, "movie: the player never started it; skipped");
	}
	if (movies[movie].finished)
	{
		frame = movies[movie].frames - 1;
	}
	else if (movies[movie].duration_ms && movies[movie].position_ms < movies[movie].duration_ms)
	{
		frame = movies[movie].frames * movies[movie].position_ms / movies[movie].duration_ms;
	}
	pthread_mutex_unlock(&movie_lock);

	return frame;
}

int host_movie_finished(int movie)
{
	int finished;

	if (!((movie) > 0 && (movie) < _movie_count))
		return 1;

	pthread_mutex_lock(&movie_lock);
	finished = movies[movie].finished;
	pthread_mutex_unlock(&movie_lock);

	return finished;
}

void host_movie_close(int movie)
{
	if (!((movie) > 0 && (movie) < _movie_count))
		return;

	pthread_mutex_lock(&movie_lock);
	movies[movie].requested = 0;
	movies[movie].playing = 0;
	pthread_mutex_unlock(&movie_lock);
}

/* ---------- what the player (MoviePlayer.java) calls */

/* one of the files the APK shipped (MoviePlayer.java lists the movie folder
at start-up): the languages the port can play are the ones registered here */
JNIEXPORT void JNICALL Java_com_halo_decomp_MoviePlayer_nativeRegisterAsset(JNIEnv *env, jclass cls, jstring name)
{
	const char *utf8;

	(void)cls;

	if (!name)
		return;

	utf8 = (*env)->GetStringUTFChars(env, name, NULL);
	if (!utf8)
		return;

	if (movie_asset_count < MAXIMUM_MOVIE_ASSETS)
	{
		if (snprintf(movie_assets[movie_asset_count].name, MOVIE_ASSET_NAME_SIZE, "%s", utf8) > 0)
			movie_asset_count++;
	}

	(*env)->ReleaseStringUTFChars(env, name, utf8);
}

/* the asset the player opens for the movie the guest asked for: the language
the engine chose (MoviePlayer.java keeps no table of its own) */
JNIEXPORT jstring JNICALL Java_com_halo_decomp_MoviePlayer_nativeAssetName(JNIEnv *env, jclass cls, jint movie)
{
	const char *name;

	(void)cls;

	if (!((movie) > 0 && (movie) < _movie_count))
		return NULL;

	pthread_mutex_lock(&movie_lock);
	name = movies[movie].chosen;
	pthread_mutex_unlock(&movie_lock);

	return name ? (*env)->NewStringUTF(env, name) : NULL;
}

JNIEXPORT jint JNICALL Java_com_halo_decomp_MoviePlayer_nativePoll(JNIEnv *env, jclass cls)
{
	int movie;

	(void)env;
	(void)cls;

	for (movie = 1; movie < _movie_count; movie++)
	{
		int wanted;

		pthread_mutex_lock(&movie_lock);
		wanted = movies[movie].requested && !movies[movie].playing;
		pthread_mutex_unlock(&movie_lock);

		if (wanted)
			return movie;
	}

	/* (a movie the guest opened: the player starts it) */
	for (movie = 1; movie < _movie_count; movie++)
	{
		int wanted;

		pthread_mutex_lock(&movie_lock);
		wanted = movies[movie].requested;
		pthread_mutex_unlock(&movie_lock);

		if (wanted)
			return movie;
	}

	return _movie_none;
}

JNIEXPORT void JNICALL Java_com_halo_decomp_MoviePlayer_nativeReady(JNIEnv *env, jclass cls, jboolean ready)
{
	(void)env;
	(void)cls;

	movie_player_ready = ready ? 1 : 0;
}

JNIEXPORT void JNICALL Java_com_halo_decomp_MoviePlayer_nativeStarted(JNIEnv *env, jclass cls, jint movie)
{
	(void)env;
	(void)cls;

	if (!((movie) > 0 && (movie) < _movie_count))
		return;

	pthread_mutex_lock(&movie_lock);
	movies[movie].playing = 1;
	pthread_mutex_unlock(&movie_lock);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_MoviePlayer_nativeProgress(JNIEnv *env, jclass cls,
	jint movie, jint position_ms, jint duration_ms)
{
	(void)env;
	(void)cls;

	if (!((movie) > 0 && (movie) < _movie_count))
		return;

	pthread_mutex_lock(&movie_lock);
	movies[movie].position_ms = (unsigned long)position_ms;
	if (duration_ms > 0)
		movies[movie].duration_ms = (unsigned long)duration_ms;
	pthread_mutex_unlock(&movie_lock);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_MoviePlayer_nativeFinished(JNIEnv *env, jclass cls, jint movie)
{
	(void)env;
	(void)cls;

	if (!((movie) > 0 && (movie) < _movie_count))
		return;

	pthread_mutex_lock(&movie_lock);
	movies[movie].finished = 1;
	movies[movie].playing = 0;
	movies[movie].requested = 0;
	pthread_mutex_unlock(&movie_lock);
}
