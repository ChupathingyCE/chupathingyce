/*
FORGE_PRECISION.INC

Forge's precision tools (included by forge.c, for its statics):

- NUDGE: LT held while holding an object is rotate mode; LT + the right
  stick's click (the console's "forge nudge <direction>" nudges once)
  flips it to nudge mode and back. In nudge mode a flick of the left stick
  moves the object one step along the ground - up away from the camera,
  down toward it, across to its side -, a flick of the right stick up or
  down one step up or down, and White steps the step's size: 5, 10, 25 or
  1 cm (5 at first). Along the camera's own axes; with SNAP on, along the
  grid's (the world's axis nearest the camera's).
  The nudge is an offset kept while the object is held (taken again with
  the camera, the object keeps it) and where it is let go; a new hold starts
  with none. The HUD says the mode and its step.
- POSITION: the properties' readout of where the object or marker held is,
  in metres (a world unit is 3.048 m).
*/

/* ---------- constants */

static char const *const forge_nudge_log_names[NUMBER_OF_FORGE_NUDGES] =
{
	"ahead", "back", "left", "right", "up", "down",
};

/* ---------- private code */

static real forge_nudge_step_world(
	void)
{
	short index = (short)PIN(forge_globals.nudge_step, 0, (short)NUMBEROF(forge_nudge_steps) - 1);

	return (real)forge_nudge_steps[index] * 0.01f / FORGE_METRES_PER_WORLD_UNIT;
}

/* the nudge's ground axes: the camera's ahead and left, along the ground
(snapping: the world's axes nearest them) */
static void forge_nudge_axes(
	real_vector3d *ahead,
	real_vector3d *left)
{
	real_point3d position;
	real_vector3d forward;

	if (!forge_camera_get(&position, &forward))
		forward = *global_forward3d;
	forward.k = 0.f;
	if (normalize3d(&forward) < 0.0001f)
		forward = *global_forward3d;
	if (forge_globals.snap)
	{
		if (forge_absolute(forward.i) >= forge_absolute(forward.j))
		{
			forward.i = forward.i < 0.f ? -1.f : 1.f;
			forward.j = 0.f;
		}
		else
		{
			forward.j = forward.j < 0.f ? -1.f : 1.f;
			forward.i = 0.f;
		}
	}
	*ahead = forward;
	left->i = -forward.j;
	left->j = forward.i;
	left->k = 0.f;
}

/* the object held nudged one step (_forge_nudge_*) */
static void forge_nudge(
	short direction)
{
	real_vector3d ahead, left, along;
	real step = forge_nudge_step_world();

	if (!object_try_and_get(forge_globals.held_object_index) || direction < 0 || direction >= NUMBER_OF_FORGE_NUDGES)
		return;
	forge_nudge_axes(&ahead, &left);
	switch (direction)
	{
	case _forge_nudge_ahead: along = ahead; break;
	case _forge_nudge_back: along.i = -ahead.i; along.j = -ahead.j; along.k = 0.f; break;
	case _forge_nudge_left: along = left; break;
	case _forge_nudge_right: along.i = -left.i; along.j = -left.j; along.k = 0.f; break;
	case _forge_nudge_up: along = *global_up3d; break;
	default: along.i = 0.f; along.j = 0.f; along.k = -1.f; break;
	}
	forge_globals.held_nudge.i += along.i * step;
	forge_globals.held_nudge.j += along.j * step;
	forge_globals.held_nudge.k += along.k * step;
	platform_log("forge: nudged %s by %d cm (%.4f world units): the hold's offset %.4f %.4f %.4f",
		forge_nudge_log_names[direction], forge_nudge_steps[forge_globals.nudge_step], step, forge_globals.held_nudge.i,
		forge_globals.held_nudge.j, forge_globals.held_nudge.k);
}

/* (each frame of nudge mode: L held while holding an object, its mode
flipped) the sticks' flicks nudge it */
static void forge_nudge_update(
	struct forge_pad const *pad)
{
	real sticks[3];
	short axis;

	sticks[0] = pad->sticks[_gamepad_stick_left][1];
	sticks[1] = pad->sticks[_gamepad_stick_left][0];
	sticks[2] = pad->sticks[_gamepad_stick_right][1];
	for (axis = 0; axis < 3; axis++)
	{
		if (forge_globals.nudge_flicked[axis])
		{
			if (forge_absolute(sticks[axis]) < FORGE_ROTATE_REARM)
				forge_globals.nudge_flicked[axis] = FALSE;
			continue;
		}
		if (forge_absolute(sticks[axis]) > FORGE_ROTATE_FLICK)
		{
			forge_globals.nudge_flicked[axis] = TRUE;
			if (axis == 0)
				forge_nudge(sticks[axis] > 0.f ? _forge_nudge_ahead : _forge_nudge_back);
			else if (axis == 1)
				forge_nudge(sticks[axis] > 0.f ? _forge_nudge_right : _forge_nudge_left);
			else
				forge_nudge(sticks[axis] > 0.f ? _forge_nudge_up : _forge_nudge_down);
		}
	}
	/* (the mouse's look up and down: a step each FORGE_ROTATE_MOUSE_STEP) */
	forge_globals.rotate_mouse[_forge_axis_pitch] += forge_globals.pending_mouse_pitch;
	while (forge_globals.rotate_mouse[_forge_axis_pitch] >= FORGE_ROTATE_MOUSE_STEP)
	{
		forge_globals.rotate_mouse[_forge_axis_pitch] -= FORGE_ROTATE_MOUSE_STEP;
		forge_nudge(_forge_nudge_up);
	}
	while (forge_globals.rotate_mouse[_forge_axis_pitch] <= -FORGE_ROTATE_MOUSE_STEP)
	{
		forge_globals.rotate_mouse[_forge_axis_pitch] += FORGE_ROTATE_MOUSE_STEP;
		forge_nudge(_forge_nudge_down);
	}
}

/* the nudge step stepped (D-pad right in nudge mode), said */
static void forge_nudge_step_next(
	void)
{
	forge_globals.nudge_step = (short)((forge_globals.nudge_step + 1) % (short)NUMBEROF(forge_nudge_steps));
	forge_notice(("Nudge: %d cm"), forge_nudge_steps[forge_globals.nudge_step]);
	platform_log("forge: nudge step %d cm", forge_nudge_steps[forge_globals.nudge_step]);
}
