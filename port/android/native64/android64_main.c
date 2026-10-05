/*
ANDROID64_MAIN.C

Entry point of the native 64-bit Android build (tools/android64_build.py):
SDL_main, which SDL3's SDLActivity calls on its own thread once it has
loaded libmain.so.

The game is native code here, so this does what the guest build's host
library (port/android/host/host_main.c) does around the guest image, and
nothing more:

- the standard output and error streams, where the platform layer writes
  its log, go to logcat (tag "halo");
- the game data is the app's external files directory,
  /sdcard/Android/data/<package>/files, where the launcher activity copies
  it on first run, and the saves its save/ folder; the environment tells
  the platform layer so (HALO_DATA_ROOT, HALO_SAVE_ROOT, HOME), and the
  width the game renders at (HALO_DISPLAY_WIDTH);
- the game's main() runs on a thread with a stack as large as the guest
  build gives it, and the process ends with it;
- the touch controls (port/linux/src/touch_input.c) get Android's system
  gesture insets from the activity (host_gesture_insets); the rest of
  their link to the overlay is the guest build's
  port/android/host/host_touch.c, compiled into this library as it is.

The Xbox address space (source/cseries/xbox_address.h) is reserved by
port/linux/src/xbox_memory.c as the library loads; if Android had already
put something there, the player is told here rather than the app vanishing.
*/

#include <SDL3/SDL.h>
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <ftw.h>
#include <jni.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unwind.h>

/* XBOX_ADDRESS_SPACE_BASE, for the log (this unit has the host's ABI) */
#ifndef HALO_64BIT
#define HALO_64BIT 1
#endif
#include "../../../source/cseries/xbox_address.h"

#define LOG_TAG "halo"
/* the guest build's main thread stack (host_main.c) */
#define GAME_STACK_SIZE (16 * 1024 * 1024)

/* the game's entry point (source/shell/shell_xbox.c) */
extern int main(int argc, char *argv[]);

/* port/linux/src/xbox_memory.c: why the Xbox address space could not be
reserved, or empty */
extern char platform_xbox_address_space_error[];

static void fatal(const char *format, ...) __attribute__((format(printf, 1, 2), noreturn));

static void fatal(const char *format, ...)
{
	char message[1024];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	__android_log_write(ANDROID_LOG_FATAL, LOG_TAG, message);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ChupathingyCE", message, NULL);
	_exit(1);
}

/* ---------- the standard streams, to logcat */

static int log_pipe[2] = { -1, -1 };

static void *log_thread(void *unused)
{
	char line[1024];
	size_t length = 0;

	(void)unused;
	for (;;)
	{
		char buffer[512];
		ssize_t count = read(log_pipe[0], buffer, sizeof(buffer));
		ssize_t index;

		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			break;
		for (index = 0; index < count; index++)
		{
			if (buffer[index] == '\n' || length == sizeof(line) - 1)
			{
				line[length] = 0;
				if (length)
					__android_log_write(ANDROID_LOG_INFO, LOG_TAG, line);
				length = 0;
				if (buffer[index] == '\n')
					continue;
			}
			line[length++] = buffer[index];
		}
	}
	return NULL;
}

static void log_to_logcat(void)
{
	pthread_t thread;

	if (pipe(log_pipe) != 0)
		return;
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	dup2(log_pipe[1], STDOUT_FILENO);
	dup2(log_pipe[1], STDERR_FILENO);
	if (pthread_create(&thread, NULL, log_thread, NULL) == 0)
		pthread_detach(thread);
}

/* ---------- the Xbox address space */

/* the mappings around the address space wanted, to logcat: what took it */
static void log_mappings_near(uintptr_t base, uintptr_t size)
{
	FILE *maps = fopen("/proc/self/maps", "r");
	char line[512];

	if (!maps)
		return;
	while (fgets(line, sizeof(line), maps))
	{
		unsigned long long start, end;

		if (sscanf(line, "%llx-%llx", &start, &end) == 2 && end > base - 0x40000000ULL &&
			start < base + size + 0x40000000ULL)
		{
			line[strcspn(line, "\n")] = 0;
			__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "mapping %s", line);
		}
	}
	fclose(maps);
}

/* ---------- storage */

static int directory_has_maps(const char *root)
{
	char path[600];
	struct stat information;

	snprintf(path, sizeof(path), "%s/maps/ui.map", root);
	return stat(path, &information) == 0;
}

/* Directories the app creates in its external storage are private to it
(mode 0770 under the app's own group), so the shell user (adb) cannot list
them. Open the save tree for reading, as the guest build does
(host_main.c). */
static int share_entry(const char *path, const struct stat *information, int type, struct FTW *walk)
{
	(void)information;
	(void)walk;
	if (type == FTW_D || type == FTW_DP)
		chmod(path, 02775);
	else if (type == FTW_F)
		chmod(path, 0664);
	return 0;
}

/* ---------- the game */

static void *game_thread(void *unused)
{
	static char name[] = "halo";
	char *arguments[] = { name, NULL };
	int code;

	(void)unused;
	code = main(1, arguments);
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "the game exited (%d)", code);
	fflush(stdout);
	/* the process ends with the game; Android restarts it from the
	launcher next time (the game's state is static: it cannot run twice
	in one process) */
	_exit(code);
}

__attribute__((visibility("default"))) int SDL_main(int argc, char *argv[])
{
	char data_root[512], save_root[512];
	const char *external;
	pthread_attr_t attributes;
	pthread_t thread;

	(void)argc;
	(void)argv;
	log_to_logcat();
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Halo for Android starting (native 64-bit, %ld-byte pages)",
		sysconf(_SC_PAGESIZE));
	if (platform_xbox_address_space_error[0])
	{
		log_mappings_near(XBOX_ADDRESS_SPACE_BASE, XBOX_ADDRESS_SPACE_SIZE);
		fatal("This device's memory layout leaves no room for the game's address space (%s).\n\n"
			"Please report it, with your device's model and Android version.",
			platform_xbox_address_space_error);
	}

	external = SDL_GetAndroidExternalStoragePath();
	if (!external)
		fatal("Android storage is unavailable: %s", SDL_GetError());
	snprintf(data_root, sizeof(data_root), "%s", external);
	snprintf(save_root, sizeof(save_root), "%s/save", external);
	/* readable by adb (the shell user), for managing saves */
	mkdir(save_root, 0775);
	nftw(save_root, share_entry, 16, FTW_PHYS);
	if (!directory_has_maps(data_root))
	{
		fatal("The Halo game data was not found.\n\nCopy the game data (the folder that contains maps) "
			"into\n%s\nor import it from the launcher screen.", data_root);
	}
	setenv("HOME", save_root, 1);
	setenv("HALO_DATA_ROOT", data_root, 1);
	setenv("HALO_SAVE_ROOT", save_root, 1);
	{
		/* the game renders 480 lines at the display's aspect ratio
		(landscape) unless display.screen_width says otherwise (d3d8_gl.c) */
		const SDL_DisplayMode *mode;

		SDL_InitSubSystem(SDL_INIT_VIDEO);
		mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
		if (mode && mode->w > 0 && mode->h > 0)
		{
			int longer = mode->w > mode->h ? mode->w : mode->h;
			int shorter = mode->w > mode->h ? mode->h : mode->w;
			char width[16];

			snprintf(width, sizeof(width), "%d", (480 * longer / shorter) & ~1);
			setenv("HALO_DISPLAY_WIDTH", width, 1);
			__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "display %dx%d: rendering %sx480", mode->w, mode->h,
				width);
		}
	}
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "data %s, saves %s; Xbox address space at %p", data_root,
		save_root, (void *)XBOX_ADDRESS_SPACE_BASE);

	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, GAME_STACK_SIZE);
	if (pthread_create(&thread, &attributes, game_thread, NULL) != 0)
		fatal("cannot start the game thread");
	pthread_attr_destroy(&attributes);
	/* the game ends the process itself (game_thread) */
	pthread_join(thread, NULL);
	return 0;
}

/* ---------- the touch controls

Android's system gesture insets, from the activity
(HaloActivity.getSystemGestureInsetsPixels), as the guest build's
host_main.c gets them: the game asks at every finger down, because they
change when the phone rotates. A change is logged; any failure gives all 0
(the touch controls then use the whole screen). */

void host_gesture_insets(int *insets)
{
	static int logged[4] = { -1, -1, -1, -1 };
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = (jobject)SDL_GetAndroidActivity();
	jint values[4] = { 0, 0, 0, 0 };
	int index;

	if (env && activity)
	{
		jclass activity_class = (*env)->GetObjectClass(env, activity);
		jmethodID method = activity_class ? (*env)->GetMethodID(env, activity_class, "getSystemGestureInsetsPixels", "()[I") : NULL;
		jintArray array = method ? (jintArray)(*env)->CallObjectMethod(env, activity, method) : NULL;

		if (array && !(*env)->ExceptionCheck(env) && (*env)->GetArrayLength(env, array) == 4)
			(*env)->GetIntArrayRegion(env, array, 0, 4, values);
		/* a pending Java exception would break the thread's next JNI call */
		if ((*env)->ExceptionCheck(env))
		{
			(*env)->ExceptionClear(env);
			for (index = 0; index < 4; index++)
				values[index] = 0;
		}
		if (array)
			(*env)->DeleteLocalRef(env, array);
		if (activity_class)
			(*env)->DeleteLocalRef(env, activity_class);
		(*env)->DeleteLocalRef(env, activity);
	}
	for (index = 0; index < 4; index++)
		insets[index] = (int)values[index];
	if (memcmp(logged, insets, sizeof(logged)))
	{
		memcpy(logged, insets, sizeof(logged));
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "system gesture insets %d,%d,%d,%d",
			insets[0], insets[1], insets[2], insets[3]);
	}
}

/* ---------- stack traces (port/linux/src/memory_watch.c's crash report)

bionic has backtrace() only from Android 13 (API 33); the app runs on
Android 9 (API 28) and later. The game's own calls (stack_walk_windows.c,
memory_watch.c) find these: the library binds its references to its own
definitions (-Bsymbolic, tools/android64_build.py). */

struct backtrace_state
{
	void **frames;
	int count;
	int capacity;
};

static _Unwind_Reason_Code backtrace_frame(struct _Unwind_Context *context, void *argument)
{
	struct backtrace_state *state = argument;
	uintptr_t pc = _Unwind_GetIP(context);

	if (pc)
	{
		if (state->count == state->capacity)
			return _URC_END_OF_STACK;
		state->frames[state->count++] = (void *)pc;
	}
	return _URC_NO_REASON;
}

int backtrace(void **frames, int capacity)
{
	struct backtrace_state state = { frames, 0, capacity };

	if (capacity <= 0)
		return 0;
	_Unwind_Backtrace(backtrace_frame, &state);
	return state.count;
}

/* the frames as addresses within libmain.so, which llvm-symbolizer reads
(llvm-symbolizer --obj=build/android64/jniLibs/arm64-v8a/libmain.so) */
void backtrace_symbols_fd(void *const *frames, int count, int descriptor)
{
	int index;

	for (index = 0; index < count; index++)
	{
		Dl_info information;
		char line[160];
		int length;

		if (dladdr(frames[index], &information) && information.dli_fbase)
		{
			length = snprintf(line, sizeof(line), "#%02d pc %p %s+0x%lx\n", index, frames[index],
				information.dli_fname ? strrchr(information.dli_fname, '/') ? strrchr(information.dli_fname, '/') + 1 :
				information.dli_fname : "?", (unsigned long)((uintptr_t)frames[index] - (uintptr_t)information.dli_fbase));
		}
		else
		{
			length = snprintf(line, sizeof(line), "#%02d pc %p\n", index, frames[index]);
		}
		if (length > 0)
			write(descriptor, line, (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1);
	}
}
