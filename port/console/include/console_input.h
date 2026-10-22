/*
CONSOLE_INPUT.H

Unified gamepad controller input mapping interface for Warthog.
Translates hardware-specific controller packets from:
- Original Xbox (XINPUT_GAMEPAD with Duke / S controller buttons)
- Xbox 360 (XINPUT_GAMEPAD with LB/RB/Triggers)
- Nintendo Wii U (VPAD / WPAD GamePad and Pro Controllers)
- Nintendo Switch (libnx PadState with Joy-Cons and Pro Controller)
*/

#ifndef __CONSOLE_INPUT_H
#define __CONSOLE_INPUT_H

#include "console_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
Console input mapping table to Halo CE virtual key/button actions.
Halo CE action codes match the game engine's internal input codes.
*/
typedef enum {
	HALO_ACTION_JUMP = 0,
	HALO_ACTION_MELEE,
	HALO_ACTION_ACTION_RELOAD,
	HALO_ACTION_SWITCH_GRENADE,
	HALO_ACTION_SWITCH_WEAPON,
	HALO_ACTION_FIRE_WEAPON,
	HALO_ACTION_THROW_GRENADE,
	HALO_ACTION_ZOOM,
	HALO_ACTION_CROUCH,
	HALO_ACTION_FLASHLIGHT,
	HALO_ACTION_MENU_ACCEPT,
	HALO_ACTION_MENU_CANCEL,
	HALO_ACTION_PAUSE,
	HALO_ACTION_COUNT
} halo_console_action_t;

/* Initialize console controller subsystem */
void console_input_initialize(void);

/* Poll connected controllers for a frame */
void console_input_poll(void);

/* Get normalized state for a given local controller port (0-3) */
bool console_input_get_state(uint32_t port, console_controller_state_t *out_state);

/* Vibrate / rumble controller motors */
void console_input_set_rumble(uint32_t port, uint16_t left_motor, uint16_t right_motor);

#ifdef __cplusplus
}
#endif

#endif /* __CONSOLE_INPUT_H */
