/*
SDL_PLATFORM.H

Window, OpenGL context and input state shared by the renderer and the
controller emulation (see sdl_platform.c).
*/

#ifndef __HALO_LINUX_SDL_PLATFORM_H
#define __HALO_LINUX_SDL_PLATFORM_H

#include <SDL3/SDL_scancode.h>
#include <stddef.h>

#define PLATFORM_MOUSE_BUTTON_COUNT 8

/* the window closed, or the game was told to quit: logs "window closed",
lets go of the OpenGL context and the window, and exits (sdl_platform.c) */
void platform_exit_window_closed(void);

struct platform_input_state
{
	unsigned char keys[SDL_SCANCODE_COUNT];
	unsigned char mouse_buttons[PLATFORM_MOUSE_BUTTON_COUNT]; /* SDL_BUTTON_* */
	float mouse_dx, mouse_dy;
	float mouse_wheel;
	BOOL focused;
	BOOL mouse_released;
	/* the mouse drives the menus' pointer (platform_ui_pointer_set_active)
	instead of the controller */
	BOOL ui_pointer;
	/* a menu is up (platform_menus_set_active): the keys drive the first
	controller, to move about it, instead of the player's actions */
	BOOL menus;
};

/* an input of the keyboard and mouse's controls (xinput_sdl.c): a scancode,
a mouse button (INPUT_MOUSE + SDL_BUTTON_*), or the wheel */
#define INPUT_MOUSE SDL_SCANCODE_COUNT
#define INPUT_WHEEL (INPUT_MOUSE + PLATFORM_MOUSE_BUTTON_COUNT)
#define INPUT_WHEEL_UP (INPUT_WHEEL + 1)
#define INPUT_WHEEL_DOWN (INPUT_WHEEL + 2)
/* an input by its name in config.toml ("W", "Mouse Left"), -1 if none */
int halo_input_from_name(const char *name);
void halo_input_name(int input, char *name, size_t size);
/* a binding's name ("Z") as the keyboard's layout labels its key ("Y"),
for the menus */
void halo_input_shown_name(const char *binding, char *shown, size_t size);

/* whether a menu is up (halo_ui_pointer_update, every frame) */
void platform_menus_set_active(BOOL active);
/* rebinding a control (Settings > Controls Setup): from now the next key,
mouse button or wheel turn is taken, and none reaches the game or the
menus; nor does the keyboard after, until every key and button is up */
void platform_binding_capture_begin(void);
/* 0 while waiting; else the capture ends: 1 with the input, 2 for none
(Delete), 3 if cancelled (Escape) */
int platform_binding_capture_poll(int *input);

/* the gamepads connected, and one's name and kind (controller_layout.h's
CONTROLLER_KIND_*; index -1: the one last used, else the first; -1 if
none): the Controls screen's (xinput_sdl.c) */
int platform_gamepad_count(void);
int platform_gamepad_describe(int index, char *name, size_t size);

struct platform_keystroke
{
	BYTE virtual_key;
	CHAR ascii;
	BYTE flags;
};

/* the device the player used last, for the button prompts (xinput_sdl.c):
a gamepad (an SDL_Gamepad), or NULL for the keyboard and mouse. The
launcher's own input (launcher.c), which the controller emulation never
sees */
void platform_input_scheme_used(void *gamepad);

/* whether the game runs without anyone to answer a question (a test run, a
null renderer, the Steam Deck's Game Mode): no message boxes */
BOOL platform_run_is_quiet(void);
/* whether it runs without anyone to watch it (a test run, a null renderer;
not Game Mode, which shows the setup screen, drawn in the game's own window):
no setup screen either */
BOOL platform_run_is_unwatched(void);
/* whether this is the thread with the window and its events */
int platform_on_event_thread(void);

/* the maps out of a file the player picked (the first start, the
launcher's Add maps), on a thread of their own: the kind of file it is
(an Xbox disc image, a Halo PC CD image, Custom Edition's installer), by
what it holds; the copy started into destination (its maps/, or maps_pc/,
maps_ce/); then, polled, how far it has got, and TRUE once it is done, with
whether it worked (and why not in error) */
enum
{
	PLATFORM_IMPORT_UNKNOWN,
	PLATFORM_IMPORT_XBOX_DISC,
	PLATFORM_IMPORT_HALO_PC_DISC,
	PLATFORM_IMPORT_CE_INSTALLER,
	PLATFORM_IMPORT_SPV3,
};
int platform_import_kind(const char *path);
BOOL platform_import_start(int kind, const char *image, const char *destination, char *error, int error_size);
/* the import under way stopped (SPV3's install; the others run to their end) */
void platform_import_cancel(void);
BOOL platform_import_poll(char *file, int file_size, unsigned long long *done, unsigned long long *total,
	BOOL *succeeded, char *error, int error_size);

BOOL platform_sdl_initialize(void);
/* creates the window and makes its OpenGL context current on this thread */
BOOL platform_video_initialize(unsigned long width, unsigned long height);
#if defined(HALO_BGFX) || defined(HALO_VULKAN)
/* the window again for OpenGL, when bgfx does not start (d3d8_bgfx.inl) */
BOOL platform_video_reopen_for_gl(void);
#ifdef HALO_VULKAN
/* the one-time questions after a fallback (d3d8_vk.inl): to stay on OpenGL
ES, or on the phone's own Vulkan driver, for good */
void platform_offer_opengl_es(void);
void platform_offer_system_driver(const char *driver);
#endif
/* display.renderer chooses bgfx: "bgfx", or on Windows "d3d11" (bgfx on
Direct3D 11) */
BOOL platform_bgfx_chosen(void);
#endif
#ifndef HALO_ANDROID
BOOL platform_screen_mode(long *width, long *height);
#endif
/* Video Setup's resolutions (port/linux/game/menu_tags.c), in pixels: the
display's modes the game can draw at, largest first, each once, but the
display's own (Native), and display.resolution's where it is none of them;
how many. None on Android. */
int platform_display_resolutions(long *widths, long *heights, int maximum);
/* the size Native is: the window's display's own, in pixels; 0 if not
known (Android) */
int platform_display_native_size(long *width, long *height);
/* Video Setup's window sizes: those of each shape (4:3, 16:10, 16:9, 21:9)
that fit the desktop, and display.window_size's; how many. None on
Android. */
int platform_window_sizes(long *widths, long *heights, int maximum);
void platform_video_drawable_size(int *width, int *height);
/* whether the window's display shows HDR, with the desktop's white (in
scRGB's units, 1.0 being 80 nits; 1.0 for macOS's EDR) and how many times
brighter than it the display goes (1.0 without HDR); and whether the
window's pixels are floating point (display.hdr_output, scRGB) */
BOOL platform_video_hdr(float *sdr_white, float *headroom);
BOOL platform_video_float_window(void);
#ifdef HALO_IOS
/* the part of the drawable the picture is shown in, in pixels from its top
left: all of it, but where the platform keeps part for itself (iPhone Duo
held part open like a laptop, its touch controls below the fold) */
void platform_video_present_area(int *x, int *y, int *width, int *height);
#endif
/* the window's mode and size and V-Sync, from config.toml as Settings has
just written it (the main thread's) */
void platform_display_apply(void);
/* the frame's end, every renderer's: OpenGL's window swapped (bgfx and the
Android Vulkan renderer present on their own first), then the clock that
holds display.max_fps with V-Sync off, or display.vrr's ceiling */
void platform_video_swap(void);
/* How every renderer shows its frames, from display.vsync, display.vrr and
tuning.present_mode, decided here once for all of them: OpenGL's swap
interval (sdl_platform.c), bgfx's reset flags and Vulkan present mode
(d3d8_bgfx.inl, tools/bgfx-patches) and the Android Vulkan renderer's
swapchain (d3d8_vk.inl, port/android/host/host_vk_present.c). */
enum
{
	/* V-Sync off: each frame shown at once, torn if need be */
	PLATFORM_PRESENT_IMMEDIATE,
	/* each frame waits for the display (V-Sync on, or VRR) */
	PLATFORM_PRESENT_VBLANK,
	/* tuning.present_mode = "adaptive": each frame waits, but one that
	missed the display is shown at once, with a tear */
	PLATFORM_PRESENT_ADAPTIVE,
};
int platform_present_mode(void);
/* the window's display's refresh rate in Hz, 0 where it is not known */
float platform_display_refresh_rate(void);
/* frames between the 30 Hz ticks at the display's refresh rate, unless
display.interpolation is false (port/linux/game/render_interpolation.c) */
int halo_interpolation_enabled(void);
void platform_mouse_capture(BOOL capture);

/* handles the window's and input devices' events; main thread only, a no-op
elsewhere; on Android this is where finger events reach touch_input.c, which
is why that module's state is only touched from the main thread */
void platform_pump_events(void);
void platform_show_message(const char *title, const char *message);
/* after a Vulkan start that did not get going: asks whether to stay on OpenGL */
void platform_offer_opengl(void);
/* a snapshot of the input state; consume_motion resets the mouse deltas */
void platform_input_read(struct platform_input_state *state, BOOL consume_motion);
/* a trigger past this (of SDL's 0 to 32767) is pulled */
#define PLATFORM_GAMEPAD_TRIGGER_PRESSED 8000
/* a gamepad's buttons pressed (bits by SDL_GamepadButton) and triggers pulled
(bit 0 the left, 1 the right) since this was last asked of it, by its
joystick ID: the controller's reading ORs them into what is held, so a tap
between two readings still counts */
void platform_gamepad_presses_take(Uint32 id, Uint32 *buttons, unsigned int *triggers);
#ifdef HALO_PORT_GYRO_SDL
/* a gamepad's sensor reading (SDL_EVENT_GAMEPAD_SENSOR_UPDATE), for gyro
aiming (xinput_sdl.c): sensor is SDL's SDL_SENSOR_GYRO or SDL_SENSOR_ACCEL,
data its three values, timestamp_ns when it was read */
void platform_gamepad_sensor(Uint32 id, int sensor, const float *data, Uint64 timestamp_ns);
#endif
/* the pointer in the menus (d3d8_gl.c, halo_ui_pointer_update) */
struct platform_ui_pointer
{
	/* in window coordinates, as SDL reports them (on Android the window's
	pixels: touch_input.c) */
	float x, y;
	float click_x, click_y;
	BOOL moved;
	int left_clicks, right_clicks;
	int wheel_steps;
	/* fingers down since the last read, and where the latest went down
	(the touchscreen only; the debug view of the menus' targets shows it) */
	int downs;
	float down_x, down_y;
	/* the pointer is the touchscreen (touch_input.c), not a mouse */
	BOOL touch;
	/* the left button held now (a scroll bar's drag); the mouse only */
	BOOL left_down;
};
/* says whether a menu is up; the mouse is released for the menus (desktop),
or the touchscreen goes to them (Android); a change of mode drops the gesture
in progress */
void platform_ui_pointer_set_active(BOOL active);

/* pointer receives what the pointer did since the last call; returns
nonzero while a menu is up (platform_ui_pointer_set_active) */
BOOL platform_ui_pointer_read(struct platform_ui_pointer *pointer);
/* debug.test_input's mouse: a move (button 0), a click of an SDL button, or
wheel notches, at a point of the window (desktop builds) */
void platform_test_pointer_event(float x, float y, int button, int wheel);

/* returns the window's size in the units that pointer positions come in,
which differ from the drawable's pixels on displays that scale */
void platform_video_window_size(int *width, int *height);
BOOL platform_next_keystroke(struct platform_keystroke *keystroke);
/* debug.test_input's typing (press: and type:): a key pressed and let go,
as the keyboard's would be, into the keystrokes the game reads (a text
box's, the console's); modifiers are SDL's (SDL_KMOD_*) */
void platform_test_keystroke(int scancode, int modifiers);
/* nonzero when a key, mouse button or touch was pressed since the last call
(which forgets them): a movie's skip (bink_playback.c) */
int platform_movie_skip_pressed(void);
/* the multiplayer scoreboard (game_engine.c) open or not: while it is, the
mouse wheel and Page Up/Down scroll it instead of switching weapons; how
far they moved it since the last call (notches and pages, down positive) */
void platform_scoreboard_scroll(int open, long *notches, long *pages);

#endif
