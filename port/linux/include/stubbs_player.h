/*
STUBBS_PLAYER.H

Stubbs mode's player: the camera behind Stubbs and his HUD
(port/linux/game/stubbs_player.c), and the image effects of the scripts'
image_effect_* functions (port/linux/game/stubbs_image_effects.c). Each
hook is called only in Stubbs mode (stubbs_mode_active); until a body is
written, each does nothing.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef STUBBS_PLAYER_H
#define STUBBS_PLAYER_H

/* (HALO_CONSOLE_NO_STUBBS: which build has Stubbs mode, read here so
every unit sees the same side of the switch) */
#include "halo_console_features.h"

/* ---------- stubbs_player.c */

#ifndef HALO_CONSOLE_NO_STUBBS
/* the perspective a unit is seen from (director.c's
director_desired_perspective, beside MCC's weapon cameras): TRUE when it is
Stubbs's own, *perspective set (a director_perspective) and the camera
following; FALSE for now, the perspective left as Halo chose it. The same
answer serves drawing and aiming (aiming is simulated) */
boolean stubbs_player_camera_following(long unit_index, short *perspective);

/* the player's melee button pressed when Halo would not swing (bipeds.c):
TRUE when Stubbs's blow started, *ticks the blow's length and *attack_tick
the tick (counted down) its damage is done; FALSE leaves it to Halo (no
melee) */
boolean stubbs_player_melee_begin(long biped_index, short *ticks, short *attack_tick);

/* a player's biped about to move (bipeds.c, *state_desired its move
state): TRUE with *state_desired his sprint state while the player holds
the crouch button and the unit has a sprint animation */
boolean stubbs_player_sprint(long biped_index, short *state_desired);

/* whether a biped's crouch button never crouches it (bipeds.c): a Stubbs
player's is his sprint */
boolean stubbs_player_never_crouches(long biped_index);

/* draws a living player's Stubbs HUD over Halo's (hud.c's hud_draw_screen,
inside the HUD's begin and end): nothing for now */
void stubbs_player_hud_draw(short local_player_index, long unit_index);

/* the script function (hud_show_special_attack <short> <boolean>)
(stubbs_functions.c): nothing for now */
void stubbs_player_hud_show_special_attack(short special_attack, boolean show);

/* ---------- stubbs_image_effects.c */

/* the script functions (image_effect_set <name>), (image_effect_start_blend
<name> <seconds>) and (image_effect_end) (stubbs_functions.c): nothing for
now */
void stubbs_image_effect_set(char const *name);
void stubbs_image_effect_start_blend(char const *name, real seconds);
void stubbs_image_effect_end(void);

/* draws the image effect over a player's 3D view (render.c, after the lens
flares, before the antialiasing and the HUD): nothing for now */
void stubbs_image_effects_render(short local_player_index);

#else
/* (a console leaves Stubbs mode out, HALO_CONSOLE_NO_STUBBS in
halo_console_features.h, and may not build this part's file: each hook
is an empty one here) */
static __inline boolean stubbs_player_camera_following(long unit_index, short *perspective) { (void)unit_index; (void)perspective; return FALSE; }
static __inline boolean stubbs_player_melee_begin(long biped_index, short *ticks, short *attack_tick) { (void)biped_index; (void)ticks; (void)attack_tick; return FALSE; }
static __inline boolean stubbs_player_sprint(long biped_index, short *state_desired) { (void)biped_index; (void)state_desired; return FALSE; }
static __inline boolean stubbs_player_never_crouches(long biped_index) { (void)biped_index; return FALSE; }
static __inline void stubbs_player_hud_draw(short local_player_index, long unit_index) { (void)local_player_index; (void)unit_index; }
static __inline void stubbs_player_hud_show_special_attack(short special_attack, boolean show) { (void)special_attack; (void)show; }
static __inline void stubbs_image_effect_set(char const *name) { (void)name; }
static __inline void stubbs_image_effect_start_blend(char const *name, real seconds) { (void)name; (void)seconds; }
static __inline void stubbs_image_effect_end(void) { }
static __inline void stubbs_image_effects_render(short local_player_index) { (void)local_player_index; }
#endif

#endif
