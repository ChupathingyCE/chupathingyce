/*
HALO_STEREO.H

The frame's stereo state for the Apple Vision Pro port: whether this frame
renders two eyes, where each eye sits and what its frustum is. Latched once
per frame by halo_stereo_frame_begin (port/linux/game/stereo.c). Plain C
types only: both the game's files and the platform's files include it.
*/

#ifndef HALO_STEREO_H
#define HALO_STEREO_H

#include <stdint.h>

enum halo_stereo_mode
{
	HALO_STEREO_OFF,
	HALO_STEREO_HEAD,
	HALO_STEREO_SCREEN,
	HALO_STEREO_SIDE_BY_SIDE
};

enum
{
	HALO_STEREO_LAYER_MONO = -1,
	HALO_STEREO_LAYER_HUD = 2,
	/* the zoomed picture (halo_stereo_zoom_begin) */
	HALO_STEREO_LAYER_ZOOM = 3,
	/* the crosshairs (halo_stereo_reticle_overlay): the presenter's
	head-locked reticle quad shows this layer whole */
	HALO_STEREO_LAYER_RETICLE = 4,
	/* render.c's UI spans (a menu, a help panel, the console, the progress
	bar) while the HUD is split (halo_stereo_hud_split): the presenter's UI
	quad shows this layer whole, and the HUD's own draws stay in their
	pieces */
	HALO_STEREO_LAYER_UI = 5,
	/* d3d8_device.c's key for a HUD group's own target: this plus the group
	(enum halo_hud_group). The game's layer stays HALO_STEREO_LAYER_HUD; the
	device keys the HUD layer's color target by the current group, and the
	HUD layer's own target is the catch-all's (HALO_HUD_GROUP_NONE) */
	HALO_STEREO_LAYER_HUD_GROUP = 8
};

/* The HUD split by the function that draws each element (the stereo spec's
"The reticle split, by draw"; port/linux/game/hud_group.c). The HUD's
drawing functions open a span for their group (hud_weapon.c, hud_unit.c,
hud_messaging.c); in a stereo frame's HUD layer, d3d8_device.c draws each
group into a target of its own and adds each draw's screen extent to its
group's rectangle, which only sizes and places the group's quad
(gpu_stereo_present). A draw in no span takes the group of the corner
hud_calculate_point last placed an element from (halo_hud_group_corner),
else HALO_HUD_GROUP_NONE: the catch-all, the HUD layer itself. What the HUD
projects onto the world (nav points, whose distance numbers CE places from
the top-left corner, damage indicators, players' markers and names) is in
an explicit HALO_HUD_GROUP_NONE span (hud.c), so no corner claims it. The
weapon group holds the grenades, which CE draws in the same top-left
corner. */
enum halo_hud_group
{
	HALO_HUD_GROUP_NONE = -1,
	HALO_HUD_GROUP_WEAPON,             /* render_weapon_hud, render_grenade_hud */
	HALO_HUD_GROUP_UNIT,               /* hud_render_unit_interface, but for its motion sensor */
	HALO_HUD_GROUP_TRACKER,            /* the motion sensor */
	HALO_HUD_GROUP_PROMPT,             /* help text, objectives and state messages (pickup prompts) */
	HALO_HUD_GROUP_MESSAGES,           /* the message list (pickups, checkpoints) */
	/* the unit span's elements CE anchors top left: a vehicle driver's seat
	labels and their bars (halo_hud_group_corner) */
	HALO_HUD_GROUP_SEATS,
	HALO_HUD_GROUP_COUNT
};
/* spans nest: the innermost wins */
void halo_hud_group_begin(int group);
void halo_hud_group_end(void);
/* hud_calculate_point, with the element's corner (_hud_anchor_*): outside
every span, the draws that follow take the corner's group (top left the
weapon's, top right the unit's, bottom left the tracker's) until the next
span begins or ends, the next element's corner, the HUD's end
(halo_hud_group_forget_corner) or the next frame; bottom right and the
center are the catch-all's. Inside the unit's span it splits that span by
corner the same way: top left is the seats' group (a driver's seat labels,
so no group crosses the layout's center), the center the catch-all's, and
the rest stay the unit's */
void halo_hud_group_corner(short corner);
/* interface_draw_hud, as the HUD ends: a corner taken outside the spans
doesn't reach past it */
void halo_hud_group_forget_corner(void);
/* the group a HUD-layer draw goes to now */
int halo_hud_group_current(void);
/* d3d8_device.c, per HUD-layer draw: its screen extent in layout lines (the
target's units: x across the layout's width, y down 480 lines), added to the
current group's rectangle (HALO_HUD_GROUP_NONE's too). With measuring off,
nothing is added (render.c's screen flash, which covers the whole layer) */
void halo_hud_group_extent(float x0, float y0, float x1, float y1);
/* the same for the group whose target the draw went to, which d3d8_device.c
names: while the HUD isn't split (halo_stereo_hud_split: the film, SCREEN
gameplay) every draw goes to the HUD layer, the catch-all's, whatever
span it's in */
void halo_hud_group_extent_in(int group, float x0, float y0, float x1, float y1);
void halo_hud_group_measure(int on);
/* render.c, as its HUD pass begins: every group's rectangle empty again */
void halo_hud_group_frame_begin(void);
/* a group's rectangle this frame (HALO_HUD_GROUP_NONE included): x0, y0,
x1, y1 in layout lines; returns 0 and an empty rectangle (all zero) if
nothing drew */
int halo_hud_group_rectangle(int group, float rectangle[4]);

struct halo_stereo_eye
{
	float offset[3];                   /* world units, in the camera's frame: right, up, back */
	float left, right, up, down;       /* positive tangents of the eye's frustum */
};

struct halo_stereo_frame
{
	int32_t eye_count;                 /* 0: mono this frame; 2: stereo */
	int32_t mode;                      /* enum halo_stereo_mode */
	/* HEAD mode, radians: the head's yaw since the last frame (about the
	room's up, left positive), and its pitch (up positive) and roll (left ear
	down positive) now. The yaw adds to the player's look, the pitch sets it;
	the roll only tilts the eye cameras, since the game's look has none. */
	float head_yaw, head_pitch, head_roll;
	struct halo_stereo_eye eyes[2];
	int32_t eye_width, eye_height;     /* pixels per eye picture */
	/* HEAD mode's full view with display.foveation: 1 while the views render
	through the Compositor's rate maps. eye_width and eye_height are then the
	maps' screen (logical) size, and display.render_scale doesn't apply: the
	render quality takes its place */
	int32_t foveated;
	/* and with debug.foveation_eye_passes (host_stereo_foveated_size): the
	size each eye's screen-sized targets are allocated at, the drawable's
	color texture per view, which the eye's rate map fills from the top left
	(its physical size, at most this); the game's eye passes render through
	the map. 0 when the eyes render unfoveated at eye_width by eye_height
	(composite-only foveation, or none) */
	int32_t foveated_width, foveated_height;
};

void halo_stereo_frame_begin(void);                    /* latches this frame's state */
const struct halo_stereo_frame *halo_stereo_frame(void);
/* -1 mono, 0/1 an eye, 2 the HUD, 3 the zoomed picture, 4 the reticle. The
HUD layer (and the reticle's and each HUD group's target) holds
premultiplied color and, in alpha, how much of the picture still shows
under it (d3d8_device.c, hud_layer_blend): the presenters put it over each
eye as rgb + eye * alpha */
void halo_stereo_layer(int layer);
int halo_stereo_current_layer(void);
/* 1 in a stereo pass that draws the frame's moment again after eye 0 (eye
1): what advances by the frame's time while it renders (glow, fog's wind)
advances in eye 0 only, or in the zoomed pass, a zoomed frame's only one */
int halo_stereo_repeat_pass(void);

/* Zoom in head-tracked stereo (the stereo spec's D5 as amended, "Zoom fills
the view"). While the local player is zoomed in HEAD mode's full view (or
the side-by-side view, which stands for it on the Mac), the eye passes are
skipped and the zoomed camera renders once, mono, into the zoom layer: one
pass with a symmetric frustum spanning the view (halo_stereo_zoom_view),
each tangent divided by the zoom's magnification, so the picture is that
many times the world's angular scale, with the zoom's screen effects (the
scope mask, the zoom's blur, night vision) and the zoomed view's HUD
elements (halo_stereo_zoom_overlay). Its target is the view's span at the
eyes' density (halo_stereo_zoom_density), unfoveated. The presenter shows
it to both eyes over the whole view on an opaque head-locked quad at the
HUD's plane, HALO_STEREO_ZOOM_DISTANCE_METERS ahead, so its depth is the
HUD's, with the HUD's pieces and the reticle over it. Not on the screen
(SCREEN mode, the film): there the game's own zoom shows on the screen, as
in mono. */
/* the debug side-by-side view's eyes (stereo.c): each eye's half tangents,
and its offset from the camera in world units */
#define HALO_STEREO_SIDE_BY_SIDE_TANGENT 0.8f
#define HALO_STEREO_SIDE_BY_SIDE_OFFSET 0.0105f
/* the HUD's plane (host_stereo_hud.h, HOST_STEREO_HUD_DISTANCE) */
#define HALO_STEREO_ZOOM_DISTANCE_METERS 2.0f
/* render.c's eye loop, before the eyes and after halo_stereo_head_orient:
whether the local player is zoomed this frame; returns 1 if the frame
renders the zoomed pass in place of the eyes. The pass waits while the last
frame's HUD layer held a menu (halo_stereo_set_ui_shown), where the
presenter shows the eyes under the UI's quad; the frame is still zoomed for
the eyes (halo_stereo_eye_unzoomed) and the HUD (halo_stereo_zoom_overlay),
so the eyes never show the zoom's mask */
int halo_stereo_zoom_begin(int zoomed);
/* d3d8_device.c, as it presents a stereo frame: whether the HUD layer went
whole on the UI's quad (a menu, the console, a progress bar) */
void halo_stereo_set_ui_shown(int shown);
/* the zoomed view's half tangents across and up, before the magnification:
each the widest of the frame's eyes' (left or right, up or down), out to
where that eye's edge meets the HUD's plane from the eye's own place, so
the presenter's quad there covers both eyes' views */
void halo_stereo_zoom_view(float tangents[2]);
/* the zoom layer's screen-sized targets against an eye's, across and up:
the view's span over the narrowest eye's, so the zoomed picture has the
eyes' pixels per tangent (and so per degree) over the whole view */
void halo_stereo_zoom_density(float scale[2]);
/* what the game lays out on its screen, layout_aspect (its width over
480 lines) across, is shown over the zoomed view's shape: the share across
that keeps it unstretched, the screen's shape over the view's (1 for an
unknown screen). The zoom's 4:3 scope mask (rasterizer_xbox_screen_effect.c)
and the zoomed view's HUD elements (d3d8_device.c) are fit to the view's
height, centered */
float halo_stereo_zoom_fit(float layout_aspect);
/* port/linux/game/cinematic_screen.c: the local player's zoom
magnification, from the weapon of the unit that aims (a seat's gun too); 1
unzoomed */
float halo_zoom_magnification(short local_player_index);
/* 1 while this frame renders the zoomed pass (halo_stereo_zoom_begin's
result, to the frame's end) */
int halo_stereo_zoom(void);
/* 1 in an eye's layer of a zoomed frame (the pass waiting under a menu):
the zoom's screen effects (interface.c) stay out of the eyes */
int halo_stereo_eye_unzoomed(void);
/* hud_weapon.c, around the zoomed view's elements (the scope's angle ticks
and range numbers, the "2x"): with on, in the HUD layer of a zoomed frame,
they draw into the zoom layer instead; off puts the HUD layer back.
halo_stereo_zoom_overlay_on: 1 between the two, where d3d8_device.c fits
the draws to the view (halo_stereo_zoom_fit) */
void halo_stereo_zoom_overlay(int on);
int halo_stereo_zoom_overlay_on(void);
/* 1 while this frame's HUD is split into the reticle's layer and the HUD
groups' targets: a stereo frame of the full view (HEAD mode, or the
side-by-side view, which stands for it on the Mac), not the film or SCREEN
gameplay, where the HUD goes on the screen whole */
int halo_stereo_hud_split(void);
/* hud_weapon.c, around crosshairs_draw: with on, in the HUD layer, the
crosshairs draw into the reticle's layer (HALO_STEREO_LAYER_RETICLE) while
the HUD is split, zoomed or not; off puts the HUD layer back. Nothing else
draws into the reticle's layer */
void halo_stereo_reticle_overlay(int on);
/* 1 once a crosshair drew into the reticle's layer this frame (render.c
flashes it as it does the HUD layer); d3d8_device.c calls
halo_stereo_reticle_drew as a draw goes to that layer */
int halo_stereo_reticle_drawn(void);
void halo_stereo_reticle_drew(void);
/* the zoomed camera, from the game's camera before the head turned it
(render.c): on foot it turns with the head as the eyes' cameras do
(halo_stereo_head_orient), since the look is the head's; in a
head-tracked third-person seat, and in a first-person seat whose yaw limit
holds the aim back from the head, it stays the game's camera, where the gun
aims */
void halo_stereo_zoom_orient(float forward[3], float up[3]);

/* render.c sets this around what draws a menu, the console or the progress
bar into the HUD layer: in HEAD mode the presenter then puts the whole layer
on the UI's quad rather than splitting it into the HUD's pieces
(d3d8_device.c notes whether anything drew under it) */
void halo_stereo_set_ui_span(int on);
int halo_stereo_ui_span(void);
/* The widgets' full-screen dim (source/interface/ui_widget.c), the dark
translucent fill behind a menu or a help panel: 1 while it goes to the
presenter instead of being drawn (a UI span in a frame whose HUD is split,
where the UI layer's quad would otherwise carry a dark rectangle); then
halo_stereo_ui_dim_add takes its texture (the Xbox texture header the
texture cache gives, or NULL) and the draw's alpha, and the presenter
darkens the eyes by the dims' opacity, gpu_stereo_present.ui_dim
(d3d8_device.c) */
int halo_stereo_ui_dim_active(void);
void halo_stereo_ui_dim_add(const void *texture, float alpha);
/* HEAD mode: where the game's crosshair (the center of the HUD layer)
points, as a direction in the eye cameras' frame (x right, y up, z back,
unnormalized): straight ahead (0, 0, -1) on foot, where the game's look is
the head's; in a head-tracked third-person camera (a vehicle seat), and in
a first-person seat whose yaw limit holds the aim back from the head, the
game camera's forward, which the gun follows, in the picture the head turned
(halo_stereo_head_orient). For the presenter's reticle (gpu_stereo_present) */
void halo_stereo_reticle(float direction[3]);
/* the HUD pass's projection this frame (render.c): its half tangents across
and up, centered on the eye cameras' forward; 0 before it runs. The
presenter's catch-all quad shows the HUD layer at that size, so what the
game projects onto the HUD (nav points) points where it projected it */
void halo_stereo_set_hud_tangents(float across, float up);
void halo_stereo_hud_tangents(float tangents[2]);

/* HEAD mode's look (source/game/player_control.c): the stick turns yaw only,
as input.turn says (in input.snap_angle steps, smoothly at
input.smooth_turn_speed, or not at all), and never pitch; it takes the
stick's yaw (*yaw, -1 to 1), the game's response curve at the stick's own
yaw, without the diagonal boost the pitch gives *yaw (yaw_response)
and the frame's time in seconds, and zeroes the game's own stick turn. And the
head's yaw since the look last took it in, with the stick's turn, unscaled by
the zoom, and the pitch that brings the look's (current_pitch) to the head's.
Both do nothing unless the head drives the view, and nothing in a
third-person camera (a vehicle seat's chase camera), where the stick turns
and pitches the look as in mono and the head never turns it. */
void halo_stereo_stick_look(short gamepad_index, float yaw_response, float time_delta, float *yaw, float *pitch);
int halo_stereo_head_look(short gamepad_index, float current_pitch, float *yaw, float *pitch);
/* A seat's yaw limit (player_control_modify_desired_angles, inside its seat
block): 0, and the game's own clamp runs, unless the head drives local
player 0's look. Otherwise it sets *desired_yaw from the look before this
turn (yaw_before, 0 to 2 pi), the turn (delta_yaw, which holds the head's
share halo_stereo_head_look handed the game on this call of
player_control_update), the seat marker's yaw and the seat's bounds off it
(radians): the look before is dragged inside the bounds as the game's rule
drags it, then takes the head's share, then the rest of the turn, each
inside the bounds; what the bounds refuse of the head's share stays out of
the yaw the look took (halo_stereo_head_yaw_taken), the eye cameras add it
(halo_stereo_head_orient) and the next look asks for it again, so the aim
stops at the bound while the view follows the head. What they refuse of the
rest is dropped, as in mono. Returns 1. The yaw the eye cameras keep has two
sources: what a seat's bounds refuse, and the head's turn through a
vehicle's third-person entry or exit camera, kept from the camera's end
until the camera holds the facing (halo_stereo_camera_posed); the look asks
for either only after a frame whose camera held the facing */
int halo_stereo_seat_yaw_clamp(short local_player_index, float *desired_yaw, float yaw_before, float delta_yaw,
	float marker_yaw, float yaw_minimum, float yaw_maximum);
/* render_interpolation.c, once a frame: whether player one's camera holds
the facing with the look's yaw as of now (holds_facing): the observer posed
from the facing this frame, the direct camera, or a blend of two cameras
both posed from it; not a glide, a chase camera or a blend with one side
not posed from it. The next frame's look gives the facing the yaw the eye
cameras kept only after such a frame, so the camera that shows it turns
with it. The cut is the fallback: a kept yaw the look took into the facing
this frame on a camera that doesn't hold it (the camera changed in the
fold's tick) would step the view back by it, so above 2 degrees HEAD mode's
full view goes black (halo_stereo_cut_requested) until the camera reaches
the eyes (halo_cutscene_camera_settled) */
void halo_stereo_camera_posed(int holds_facing);
/* 1 while that cut holds: the presenter keeps the full view black, and
fades it in through the ordinary cut when it ends (gpu_stereo_present's
cut_requested) */
int halo_stereo_cut_requested(void);
/* 1 while the head drives the look (HEAD mode with the Compositor's eyes),
for the look's autolevel */
int halo_stereo_head_drives_look(short gamepad_index);
/* input.comfort_vignette: how strongly (0 to 1) the HEAD-mode presenter
darkens the eyes' edges this frame, while the stick turns the look smoothly;
0 otherwise */
float halo_stereo_vignette(void);
/* the vignette's next strength from its strength now, the turn rate's share of
120 degrees per second (0 to 1, more is full) and the time since, in seconds: it eases
toward the share, in over 0.1 s and out over 0.2 s */
float halo_stereo_vignette_ease(float strength, float turn_fraction, float time_delta);
/* HEAD mode's render (source/render/render.c): gives a camera the head's yaw
at render time (the game camera's yaw less the head yaw it holds,
halo_stereo_camera_head_yaw, plus the head's yaw now: what the look took
since the camera was posed and what it hasn't taken yet, which it does next
frame, or after a pause) and the head's pitch and roll, so the camera's
orientation is the head's this frame, paused or not. In a
third-person camera (unless display.stereo_vehicle_screen puts it on the
screen) it turns the game's camera by the head's yaw and change of pitch
since that camera began, and gives it the head's roll: the picture only */
void halo_stereo_head_orient(float forward[3], float up[3]);
/* HEAD mode's head yaw in the game's camera (render_interpolation.c). The head
yaw the look has taken so far, all told (radians, left positive, wrapped to
-pi..pi): the game's camera holds it as of the frame the camera was posed.
And, as render_interpolation.c hands main.c player one's camera each frame,
the head yaw that camera holds (as that sum read when it was posed), the
blend between ticks it drew (0 to 1), and how it made the camera ("blended",
"direct", "cut", "live"): halo_stereo_head_orient takes the head yaw the
camera holds out of it, and debug.head_yaw_log logs all three. A frame
without the call (no player camera) counts its camera as holding all the
look took */
float halo_stereo_head_yaw_taken(void);
void halo_stereo_camera_head_yaw(float head_yaw, float fraction, const char *source);
/* The first-person body (port/linux/game/first_person_body.c): nonzero
while object_index is the local player's unit, drawn below the view in
first person: HEAD mode or the side-by-side view, an eye layer, not the
film or SCREEN gameplay, the director's first person (not a scripted
camera), display.first_person_body, and a pose the game drives under the
camera (no custom animation, not dead, the pelvis under the camera; on foot
or in a vehicle's first-person seat, with display.first_person_body_seats,
not while climbing in or out). render_objects.c draws it after the
first-person weapon, under that weapon's stencil, then again for its inside
faces (halo_first_person_body_fill) */
int halo_first_person_body(long object_index);
/* the render-only node matrices for that body, set back along the facing
(facing: the unit's world forward; only its horizontal part counts) by
display.first_person_body_offset, or seated by
display.first_person_body_seat_offset: the body's with the neck and head
collapsed to a cap above the collar (a model with no neck node: the head to
its parent), with collapse_arms (while the first-person weapon shows) the
arms from the upper arms down collapsed too, the aiming pose's forward bend
undone, and the spine's bend clamped so no kept node comes within 0.1 units
of camera; or with shadow, the whole silhouette for its shadow. Each in its
own static array, valid until the next call of the same kind; node_count
is the smaller of the model's nodes and the object's node matrices; returns
matrices unchanged if the model's nodes aren't recognized */
struct real_matrix4x3;
const struct real_matrix4x3 *halo_first_person_body_matrices(long model_index,
	const struct real_matrix4x3 *matrices, short node_count, const float facing[3], const float camera[3],
	int collapse_arms, int seated, int shadow);
/* debug.gpu_stats, once a second of game time: the camera, the drawn
pelvis and feet in the camera's frame, and the kept chest's nearest
distance to the camera with the bend clamp's angle */
void halo_first_person_body_log(long object_index, const struct real_matrix4x3 *drawn, short node_count);
/* 1 while render_objects.c draws the body's inside faces: the second pass,
with the cull reversed, which the model rasterizer
(rasterizer_xbox_models.c) draws in a flat dark color */
void halo_first_person_body_set_fill(int on);
int halo_first_person_body_fill(void);
/* 1 while render_objects.c draws the body (both passes): the backends clamp
its depth to the viewport's range instead of clipping it at the near plane,
so the shell nearer than the eye passes' near plane (0.0625 units) draws at
the nearest depth rather than opening onto its inside; the projection, and
so every depth compare past the near plane, stays the world's */
void halo_first_person_body_set_depth_clamp(int on);
int halo_first_person_body_depth_clamp(void);
/* the eye cameras' near and far planes in world units, for the presenter's
depth (d3d8_device.c), set by the eye loop each stereo frame */
void halo_stereo_set_depth_range(float z_near, float z_far);
void halo_stereo_depth_range(float *z_near, float *z_far);

/* The screen. In stereo, while the letterbox is in, the game plays as a 3D
film on a 16:9 screen (stereo.c): the cinematic camera rendered twice,
framed on the letterbox's inside with no bars except while a title shows.
A director's scripted camera goes on the screen the same way, and in HEAD
mode the player's camera under a script, and a third-person camera with
display.stereo_vehicle_screen; in HEAD mode a cutscene's camera only once it
isn't first person (stereo.c). In
SCREEN mode the rest is gameplay as a 3D TV: the player's camera rendered
twice, by the same mapping with gameplay's own depth, leaning with the
head. */
/* 1 when this frame is the 3D film: stereo with eyes, and the letterbox in
or a camera the head doesn't steer (above) */
int halo_stereo_film(void);
/* 1 while this frame's eyes are SCREEN gameplay's: SCREEN mode (or the
side-by-side view with debug.side_by_side_screen), not the film */
int halo_stereo_screen_gameplay(void);
/* the film's or SCREEN gameplay's frusta from the camera's vertical half
tangent, after set_window_camera_values (render.c's eye loop, before it
reads the eyes); nothing unless the frame is one of them */
void halo_stereo_screen_frusta(float vertical_tangent);
/* 1 while the screen's framing applies: the letterbox's film, or SCREEN
mode (or debug.side_by_side_screen) with display.screen_framing = "band",
in either case only while the frame has two eyes (held across the film's
hold); main.c then narrows the vertical view by 0.75, so the screen shows
the game's horizontal view across 16:9. With the immersive space closed,
SCREEN mode's window keeps mono's Hor+ view */
int halo_stereo_screen_framing(void);
/* in an eye's layer of a frame on the screen in SCREEN mode (or
debug.side_by_side_screen), gameplay or the film (its hold shows the
player's camera): the first-person weapon's eye (its offset right and up
from the camera, world units, and its tangents) by the weapon's own nearly
flat mapping, its nearest point on the screen's surface
(rasterizer_set_frustum_z); returns 0 elsewhere, HEAD mode included */
int halo_stereo_first_person_eye(struct halo_stereo_eye *eye);
/* in an eye's layer of HEAD mode's full view (or the side-by-side view's;
not the film or SCREEN gameplay): display.weapon_offset_down and
display.weapon_offset_back (world units, 0 to 0.2), how far the
first-person weapon's draws move down along the camera's up and back along
its forward (rasterizer_set_frustum_z); returns 0 elsewhere, or when both
are 0. display.eye_height_offset raises the same eyes (stereo.c) */
int halo_stereo_weapon_offset(float *down, float *back);
/* debug.gpu_stats: logs the culling camera's distance back behind the
center camera (render.c), once for each mode and mapping */
void halo_stereo_log_culling(float distance_back);
/* debug.gpu_stats: logs each film frame's camera (render.c, the eye loop):
its vertical field of view (radians in, degrees logged), position, forward,
whether it's first person, and how render_interpolation.c made it ("cut" on
a camera cut), for Task 12k's count of telephoto shots */
void halo_stereo_log_film_camera(const float position[3], const float forward[3], float vertical_field_of_view);
/* Task 12k's spike, debug.cutscene_immersive (halo_stereo_cutscene.h): 1
on a frame that renders a cutscene's third-person film immersive (HEAD mode
or the side-by-side view); _letterbox, if the letterbox made it the film
(cinematics.c draws the title bars then, onto the director's frame) */
int halo_stereo_cutscene_immersive(void);
int halo_stereo_cutscene_immersive_letterbox(void);
/* on such a frame: the director's frame's forward and up in the eye
camera's frame (x right, y up, z back; the eyes' frusta are the head's), its
half tangents across and up, and debug.cutscene_outside_dim; 0 otherwise */
int halo_stereo_cutscene_frame(float forward[3], float up[3], float tangents[2], float *dim);
/* the side-by-side view's eye's frustum on such a frame (left, right, up,
down tangents), for d3d8_device.c's debug screenshot of the mask; 0
otherwise */
int halo_stereo_side_by_side_cutscene(int eye, float tangents[4]);
/* the cutscene camera's horizontal field of view (radians; the observer's,
70 degrees for the game's default lens), 0 without one (cinematic_screen.c) */
float halo_cutscene_camera_field_of_view(void);
/* display.lod_scale in HEAD mode and the side-by-side view (1.0 otherwise):
a multiplier on the pixel size the game picks model detail and particle
distance by, 1.0 the Xbox's, clamped to 0.5 to 4 */
float halo_stereo_lod_scale(void);
/* display.hud_resolution in HEAD mode and the side-by-side view (1.0
otherwise): the pixels per axis of the HUD's targets (the HUD layer, its
groups', the reticle's and the UI's, which share one depth and stencil)
against the view's, clamped to 0.5 to 1 */
float halo_stereo_hud_resolution(void);
/* render.c's stereo frame, after it builds the culling frustum
(port/linux/game/stereo_lod.c): sets that frustum's pixel scale
(projection_world_to_screen) to mono's for the same camera, .j times
halo_stereo_lod_scale (model detail, the model cull, particles), .i not
(sprites' size), since they read it; the union of the eyes' bounds would give about half
mono's. The planes are unchanged */
struct render_camera;
struct render_frustum;
void halo_stereo_lod_projection(const struct render_camera *camera, struct render_frustum *cull_frustum);
/* The eye pass's camera position (world units). render_player_frame_stereo
(render.c) sets it around each eye's render_window and clears it (NULL)
after; it stays NULL in mono, the HUD, the zoomed pass and a mirror's
window. render_sky centers the sky on it, so the sky sits at infinity in
each eye. The pointer is the caller's, valid only while it is set */
union real_point3d;
void halo_stereo_set_eye_position(const union real_point3d *position);
const union real_point3d *halo_stereo_eye_position(void);
/* Far scenery (port/linux/game/stereo_far.c): objects that stand in for
something far bigger and farther than their model, as a10's ring
(scenery\halo\halo, about 120 m across, 134 m beyond the bridge's window),
which stereo would otherwise show at its real size and distance. 1 if the
tag name is on the list */
int halo_stereo_far_scenery(const char *tag_name);
/* render_objects.c, as an object draws: in an eye pass
(halo_stereo_eye_position), a far scenery object's node matrices translated
by the eye's position less the center camera's (center), a render-only copy,
so each eye sees it where the center camera does: no disparity, at
infinity. Otherwise matrices, unchanged */
struct real_matrix4x3;
const struct real_matrix4x3 *halo_stereo_far_matrices(const char *tag_name,
	const struct real_matrix4x3 *matrices, short node_count, const union real_point3d *center);
/* 1 when this frame is the film for a cutscene: the letterbox, held
through the film's hold after it drops. The 16:9 narrowing (main.c) and the
title bars (cinematics.c) follow this, not the letterbox flag itself, so the
framing doesn't change in the frames before the cut */
int halo_stereo_film_letterbox(void);
/* 1 while a script fade shows over the picture this frame (any intensity;
halo_stereo_set_fade): a cut between the full view and the screen then needs
no fade through black, which would otherwise dip a colored fade to black */
int halo_stereo_cut_covered(void);
/* the script fade the eyes draw this frame, with the picture's tick
fraction (render.c sets it before the eyes; zero until then), which the
host's room tint takes as well, so the two move in step */
void halo_stereo_set_fade(const float rgb_intensity[4]);
void halo_stereo_fade(float rgb_intensity[4]);
/* The film's and the 3D TV's one mapping (stereo.c has the math): from
the depth share (infinity's parallax as a share of the viewer's eye
separation), the convergence (meters ahead of the camera on the screen's
surface), the viewer's eye separation and the screen's half width (one unit
for both), the picture's vertical half tangent and an optional lean (world
units, right and up), each eye's offset and frustum */
void halo_stereo_tv_eyes(float depth_share, float convergence_meters, float viewer_separation, float half_width,
	float vertical_tangent, const float lean[2], struct halo_stereo_eye eyes[2]);
/* port/linux/game/cinematic_screen.c: 1 while the letterbox is in; while
the camera is scripted (the script's camera_control, or the player's camera
with its look taken away); while the director's scripted camera alone is
(camera_control, the scripted perspective); while it's third person */
int halo_cinematic_screen(void);
int halo_scripted_camera(void);
int halo_scripted_director_camera(void);
int halo_third_person_camera(void);
/* A first-person cutscene camera: the director's own first person (the
look on or off) or a scripted camera in first-person mode. HEAD mode's film
is only for the others (stereo.c) */
int halo_cutscene_camera_first_person(void);
/* A first-person camera the player can't look around in: the player's own
with the script holding its look (a10's cryo pod), or a scripted camera in
first-person mode. HEAD mode turns its picture by the head, never the look */
int halo_look_disabled_first_person(void);
/* The player's own first-person camera with the look enabled (the
director's first person, player_camera_control on): a cutscene's film is
released for it, even after a third-person shot (stereo.c) */
int halo_player_camera_first_person(void);
/* 1 while the director holds player one's facing (director_inhibited_facing:
the debug camera's controls, director_update_controls): player_control drops
the frame's turn, so the look hands it none of the yaw the eye cameras kept
(stereo.c) */
int halo_director_inhibited_facing(void);
/* the camera has reached the player's eyes after a cutscene: the observer's
command finished, or the camera within this many world units of the unit's
camera position with its orientation settled. The film holds until it has
(stereo.c) */
#define HALO_CUTSCENE_SETTLED_DISTANCE 0.05f
int halo_cutscene_camera_settled(void);
/* cinematics.c, each stereo frame cinematic_render runs: the bars it drew
(0 to 1 of the letterbox's), which the expansion carries on from when the
film for a cutscene ends */
void halo_stereo_set_title_bars(float bars);
/* the film's title bars (cinematics.c, halo_cinematic_title_bars) a frame
of seconds on from bars: none while the script fade shows (fade, its
intensity, above 0), else toward limit at the letterbox's rate (one amount a
second) and never above it. limit is 1 while a title is up and fading in or
holding, the title's own fade while it fades out, 0 with none */
float halo_stereo_title_bars_ease(float bars, float limit, float fade, float seconds);
/* HEAD mode's cutscene window (halo_stereo_window.h): 1 while the film's
rectangle is expanding out to the full view after a cutscene's film, with
its progress (0 on the first full-view frame, toward 1 over
HALO_STEREO_EXPANSION_SECONDS) and the bars it still carries; 0 otherwise */
int halo_stereo_expansion(float *progress, float *bars);
/* the side-by-side view's model of what HEAD mode's presenter shows for an
eye, for its debug screenshot (d3d8_device.c, "-window"): the theater's
default screen 4 m ahead of the eye's fixed frustum */
struct halo_stereo_window_model
{
	int kind;                  /* 0: nothing to model; 1: the film on its screen; 2: the expansion */
	float tangents[4];         /* the eye's full view: left, right, up, down */
	float screen[4];           /* the screen as the eye sees it, in tangents: left, right, down, up */
	float window[4];           /* the expansion's window (halo_stereo_window.h): left, right, down, up */
	float bars;                /* the bars the expansion still carries */
};
int halo_stereo_side_by_side_window(int eye, struct halo_stereo_window_model *model);
/* debug.gpu_stats's cutscene line (stereo.c): what decides the film */
struct halo_cutscene_state
{
	int letterbox;             /* cinematic_globals->show_letterbox */
	int director_scripted;     /* director_camera_scripted */
	int perspective;           /* director_peek_perspective: first person, third, scripted, neutral */
	int script_mode;           /* the scripted camera's mode: point, animation, first person, dead */
	int look_disabled;         /* player_control_camera_control_disabled */
	int player_first_person;   /* halo_player_camera_first_person */
	int observer_finished;     /* observer_command_has_finished */
	int orientation_settled;   /* observer_orientation_settled */
	float distance;            /* the observer's camera from the unit's camera position (world units; -1 none) */
};
void halo_cutscene_state(struct halo_cutscene_state *state);
/* the script fade (fade_in, fade_out; never a screen flash) at the picture's
time: the game's tick before this one plus tick_fraction (the render's
interpolation fraction; 1.0 gives the game's own whole-tick intensity). Its
RGB and intensity into rgb_intensity, zero intensity if none; 1 while the
game has a fade active (it draws one, perhaps at intensity 0) */
int halo_screen_fade(float tick_fraction, float rgb_intensity[4]);
/* source/cutscene/cinematics.c, once a frame of the film: 0..1, how far
the bars are in: out while the script fade shows, then in at one a second
while a title is up, out with its fade-out (halo_stereo_title_bars_ease) */
float halo_cinematic_title_bars(void);
/* HEAD mode's help text (port/linux/game/stereo_help_text.c): how much of
the help message named message_name hud_messaging.c draws. In HEAD mode and
the side-by-side view, a10's look-only prompts not at all and its moving
lesson's first line alone; otherwise, and for every other message, all of
it. hud_messaging.c declares these itself (source/ includes no port header) */
enum
{
	HALO_HELP_TEXT_ALL,
	HALO_HELP_TEXT_NONE,
	HALO_HELP_TEXT_FIRST_LINE
};
int halo_stereo_help_text_part(const char *message_name);
/* the characters of a help text element (length of them, its terminator
counted, as the tag's elements count them) before its first line break
("|n"); length if it has none */
int halo_stereo_help_text_line_end(const unsigned short *text, int length);

#endif
