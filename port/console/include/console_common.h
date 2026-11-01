/*
CONSOLE_COMMON.H

Common platform definitions for Warthog (ChupathingyCE console family):
Original Xbox, Xbox 360, Nintendo Wii U, and Nintendo Switch.
*/

#ifndef __CONSOLE_COMMON_H
#define __CONSOLE_COMMON_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Target console identification */
typedef enum {
	CONSOLE_TYPE_UNKNOWN = 0,
	CONSOLE_TYPE_XBOX_ORIGINAL = 1,
	CONSOLE_TYPE_XBOX_360 = 2,
	CONSOLE_TYPE_WIIU = 3,
	CONSOLE_TYPE_SWITCH = 4
} console_platform_type_t;

#if defined(HALO_XBOX_CONSOLE)
#define CURRENT_CONSOLE_TYPE CONSOLE_TYPE_XBOX_ORIGINAL
#define CONSOLE_PLATFORM_NAME "Original Xbox"
#elif defined(HALO_XBOX360_CONSOLE)
#define CURRENT_CONSOLE_TYPE CONSOLE_TYPE_XBOX_360
#define CONSOLE_PLATFORM_NAME "Xbox 360"
#elif defined(HALO_WIIU_CONSOLE)
#define CURRENT_CONSOLE_TYPE CONSOLE_TYPE_WIIU
#define CONSOLE_PLATFORM_NAME "Nintendo Wii U"
#elif defined(HALO_SWITCH_CONSOLE)
#define CURRENT_CONSOLE_TYPE CONSOLE_TYPE_SWITCH
#define CONSOLE_PLATFORM_NAME "Nintendo Switch"
#else
#define CURRENT_CONSOLE_TYPE CONSOLE_TYPE_UNKNOWN
#define CONSOLE_PLATFORM_NAME "Unknown Console"
#endif

/* Standard console display resolutions */
typedef struct {
	uint32_t width;
	uint32_t height;
	uint32_t refresh_rate;
	bool widescreen;
	bool progressive;
} console_display_mode_t;

/* Standard controller digital button masks */
#define CONSOLE_BUTTON_DPAD_UP        (1 << 0)
#define CONSOLE_BUTTON_DPAD_DOWN      (1 << 1)
#define CONSOLE_BUTTON_DPAD_LEFT      (1 << 2)
#define CONSOLE_BUTTON_DPAD_RIGHT     (1 << 3)
#define CONSOLE_BUTTON_START          (1 << 4)
#define CONSOLE_BUTTON_BACK           (1 << 5) /* Select / Minus */
#define CONSOLE_BUTTON_LEFT_THUMB     (1 << 6) /* L3 */
#define CONSOLE_BUTTON_RIGHT_THUMB    (1 << 7) /* R3 */
#define CONSOLE_BUTTON_LEFT_SHOULDER  (1 << 8) /* L / LB / White */
#define CONSOLE_BUTTON_RIGHT_SHOULDER (1 << 9) /* R / RB / Black */
#define CONSOLE_BUTTON_A              (1 << 12)
#define CONSOLE_BUTTON_B              (1 << 13)
#define CONSOLE_BUTTON_X              (1 << 14)
#define CONSOLE_BUTTON_Y              (1 << 15)

/* Normalized analog controller state */
typedef struct {
	uint32_t buttons;
	int16_t thumb_lx;
	int16_t thumb_ly;
	int16_t thumb_rx;
	int16_t thumb_ry;
	uint8_t trigger_l;
	uint8_t trigger_r;
	bool connected;
} console_controller_state_t;

/* Maximum players on a single console */
#define CONSOLE_MAX_LOCAL_PLAYERS 4

#ifdef __cplusplus
}
#endif

#endif /* __CONSOLE_COMMON_H */
