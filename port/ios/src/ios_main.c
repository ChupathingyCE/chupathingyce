/*
IOS_MAIN.C

The game's entry point on iOS: halo_ios_run (halo_ios.h), which the app's
native module calls on the main thread. It does what chupathingyce's
port/android/native64/android64_main.c does around the game for the native
64-bit Android build, and the iOS build compiles the same game and platform
layer as that one (HALO_ANDROID with HALO_64BIT, and HALO_IOS where iOS
differs: port/ios/build.py):

- the standard output and error streams, where the platform layer writes
  its log, go to logs/console.txt in the data folder (the game's own log is
  logs/debug.txt, as on Android);
- the game data is the app's Documents folder, where the launcher copies
  it, and the saves its save/ folder; the environment tells the platform
  layer so (HALO_DATA_ROOT, HALO_SAVE_ROOT, HOME), and the width the game
  renders at and the display's scale (HALO_DISPLAY_WIDTH,
  HALO_DISPLAY_DENSITY);
- the device's id for online play goes in hardware_id.txt, where the
  platform layer reads Android's (port/linux/src/p2p.c);
- internet play's broker list, from the app's bundle, is written beside
  config.toml;
- the game's main() (renamed halo_main for this build: the app has its own)
  runs on the main thread, as SDL's video and events need on iOS, but on a
  stack of its own as large as the Android build's game thread
  (ios_stack.S): iOS's main thread has 1 MB. The process ends with the
  game.

The Xbox address space (source/cseries/xbox_address.h) is reserved by
port/linux/src/xbox_memory.c as the library loads: 4 GB, 4 GB-aligned when
iOS has room at such an address. If that fails, halo_ios_run says why
rather than the app vanishing.
*/

#include "halo_ios.h"

#include <SDL3/SDL.h>
/* (SDL_SetMainReady only: the app's main() is its own, not SDL's) */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <crt_externs.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* the Android build's game thread stack (android64_main.c) */
#define GAME_STACK_SIZE (16 * 1024 * 1024)
/* an unmapped page below it: an overflow faults rather than writing over
whatever lies there */
#define GAME_STACK_GUARD (16 * 1024)

/* the game's entry point (source/shell/shell_xbox.c, compiled with
-Dmain=halo_main) */
extern int halo_main(int argc, char *argv[]);

/* port/linux/src/xbox_memory.c: where the Xbox address space is, and why it
could not be reserved, or empty */
extern unsigned long long xbox_address_space_base;
extern char platform_xbox_address_space_error[];

/* ios_stack.S */
extern void ios_run_on_stack(void (*function)(void *), void *argument, void *stack_top);

static void log_line(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void log_line(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	fputs("ios: ", stderr);
	vfprintf(stderr, format, arguments);
	fputc('\n', stderr);
	va_end(arguments);
}

static int fail(char *error, int error_size, const char *format, ...) __attribute__((format(printf, 3, 4)));

static int fail(char *error, int error_size, const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	if (error && error_size > 0)
		vsnprintf(error, (size_t)error_size, format, arguments);
	va_end(arguments);
	if (error)
		log_line("cannot start: %s", error);
	return 0;
}

/* ---------- the standard streams, to a file */

static void log_to_file(const char *data_root)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/logs", data_root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/logs/console.txt", data_root);
	if (freopen(path, "w", stdout))
		dup2(fileno(stdout), STDERR_FILENO);
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
}

/* ---------- storage */

static int directory_has_maps(const char *root)
{
	char path[1024];
	struct stat information;

	snprintf(path, sizeof(path), "%s/maps/ui.map", root);
	return stat(path, &information) == 0;
}

static void write_text(const char *path, const char *text)
{
	FILE *file = fopen(path, "w");

	if (!file)
	{
		log_line("cannot write %s: %s", path, strerror(errno));
		return;
	}
	fputs(text, file);
	fclose(file);
}

/* internet play's MQTT brokers (network.brokers_file): the bundle's list,
written beside config.toml (in save_root) at each start, as a desktop update
replaces the file beside its game */
static void write_brokers(const char *save_root)
{
	char source[1024], path[1024];
	const char *base = SDL_GetBasePath();
	size_t size = 0;
	void *brokers;

	snprintf(source, sizeof(source), "%sbrokers.txt", base ? base : "");
	brokers = SDL_LoadFile(source, &size);
	if (!brokers)
	{
		log_line("no broker list in the bundle (%s): internet play uses config.toml's", source);
		return;
	}
	snprintf(path, sizeof(path), "%s/brokers.txt", save_root);
	if (!SDL_SaveFile(path, brokers, size))
		log_line("cannot write %s: %s", path, SDL_GetError());
	SDL_free(brokers);
}

/* the certificate authorities HTTPS is checked against (the game list, the
Delta table: posix_browser.c, through SSL_CERT_FILE): iOS keeps its own
where no app reads them, so the bundle carries Mozilla's, as curl publishes
them (.github/workflows/ios.yml) */
static void use_certificates(void)
{
	char path[1024];
	const char *base = SDL_GetBasePath();

	if (getenv("SSL_CERT_FILE"))
		return;
	snprintf(path, sizeof(path), "%scacert.pem", base ? base : "");
	if (access(path, R_OK) != 0)
	{
		log_line("no certificate authorities in the bundle (%s): HTTPS cannot be checked", path);
		return;
	}
	setenv("SSL_CERT_FILE", path, 1);
}

/* the game renders 480 lines at the display's aspect ratio (landscape)
unless display.screen_width says otherwise (d3d8_gl.c), and the touch
controls size themselves by the display's scale (touch_input.c) */
static void describe_display(void)
{
	SDL_DisplayID display;
	const SDL_DisplayMode *mode;
	float scale;
	char text[32];

	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
	{
		log_line("cannot start SDL's video: %s", SDL_GetError());
		return;
	}
	display = SDL_GetPrimaryDisplay();
	mode = SDL_GetDesktopDisplayMode(display);
	if (mode && mode->w > 0 && mode->h > 0)
	{
		int longer = mode->w > mode->h ? mode->w : mode->h;
		int shorter = mode->w > mode->h ? mode->h : mode->w;

		snprintf(text, sizeof(text), "%d", (480 * longer / shorter) & ~1);
		setenv("HALO_DISPLAY_WIDTH", text, 1);
		log_line("display %dx%d: rendering %sx480", mode->w, mode->h, text);
	}
	scale = SDL_GetDisplayContentScale(display);
	if (scale > 0.0f)
	{
		snprintf(text, sizeof(text), "%g", scale);
		setenv("HALO_DISPLAY_DENSITY", text, 1);
		log_line("display scale %s", text);
	}
}

/* ---------- the game */

/* an invite link the app was opened with before the game started
(halo_ios_set_invite), and whether it has started (halo_ios_open_link) */
static char startup_invite[512];
static int game_started;

static void game_entry(void *unused)
{
	static char name[] = "halo";
	static char *arguments[] = { name, startup_invite, NULL };
	int count = startup_invite[0] ? 2 : 1;
	int code;

	(void)unused;
	/* the game reads an invite from the process's own arguments, as when
	macOS starts it from a link (posix_command_line_argument: _NSGetArgv),
	and joins it once internet play is up (p2p.c) */
	if (startup_invite[0])
	{
		*_NSGetArgc() = count;
		*_NSGetArgv() = arguments;
		log_line("starting with an invite");
	}
	if (count == 1)
		arguments[1] = NULL;
	game_started = 1;
	code = halo_main(count, arguments);
	log_line("the game exited (%d)", code);
	fflush(stdout);
	/* the process ends with the game: its state is static, and it cannot
	run twice in one process */
	exit(code);
}

void halo_ios_set_invite(const char *link)
{
	snprintf(startup_invite, sizeof(startup_invite), "%s", link ? link : "");
}

int halo_ios_open_link(const char *link)
{
	SDL_Event event;

	if (!game_started || !link || strncmp(link, "halo://", 7))
		return 0;
	/* as SDL hands macOS's opened links to the game (sdl_platform.c,
	SDL_EVENT_DROP_FILE); the copy is the event's, kept: links come rarely,
	and the game reads it in its next pump */
	SDL_zero(event);
	event.type = SDL_EVENT_DROP_FILE;
	event.drop.data = SDL_strdup(link);
	return SDL_PushEvent(&event) ? 1 : 0;
}

int halo_ios_run(const char *data_root, const char *hardware_id, char *error, int error_size)
{
	char save_root[1024], path[1024];
	unsigned char *stack;

	if (error && error_size > 0)
		error[0] = 0;
	if (!data_root || !*data_root)
		return fail(error, error_size, "The app gave no data folder.");
	log_to_file(data_root);
	log_line("Halo for iOS starting (native 64-bit, %ld-byte pages)", sysconf(_SC_PAGESIZE));
	if (platform_xbox_address_space_error[0])
	{
		return fail(error, error_size,
			"The game cannot start: iOS gave it no room for its 4 GB address space (%s).\n\n"
			"Please report it, with your device's model and iOS version.",
			platform_xbox_address_space_error);
	}
	log_line("Xbox address space at %p (4 GB%s)", (void *)(uintptr_t)xbox_address_space_base,
		(xbox_address_space_base & 0xffffffffULL) ? ", not 4 GB-aligned" : ", 4 GB-aligned");

	if (!directory_has_maps(data_root))
	{
		return fail(error, error_size,
			"The Halo game data was not found.\n\nImport an Xbox disc image of Halo, or a zip of its maps "
			"folder, from the launcher first.");
	}
	snprintf(save_root, sizeof(save_root), "%s/save", data_root);
	mkdir(save_root, 0755);
	setenv("HOME", save_root, 1);
	setenv("HALO_DATA_ROOT", data_root, 1);
	setenv("HALO_SAVE_ROOT", save_root, 1);
	if (hardware_id && *hardware_id)
	{
		snprintf(path, sizeof(path), "%s/hardware_id.txt", data_root);
		write_text(path, hardware_id);
	}

	/* the app's main() is SDL's to know nothing of: the game is started
	from it, not by SDL_RunApp */
	SDL_SetMainReady();
	/* the game is played in landscape, as on Android (its manifest's
	sensorLandscape), whichever way the launcher was held */
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
	/* the home bar hidden, and a swipe up from the bottom edge (where the
	touch controls' Crouch and stick are) taken twice before iOS acts on it */
	SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
	describe_display();
	write_brokers(save_root);
	use_certificates();
	log_line("data %s, saves %s", data_root, save_root);

	stack = mmap(NULL, GAME_STACK_SIZE + GAME_STACK_GUARD, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (stack == MAP_FAILED)
		return fail(error, error_size, "cannot allocate the game's stack (%s)", strerror(errno));
	mprotect(stack, GAME_STACK_GUARD, PROT_NONE);
	ios_run_on_stack(game_entry, NULL, stack + GAME_STACK_GUARD + GAME_STACK_SIZE);
	/* (game_entry ends the process) */
	return 0;
}
