/*
FORGE_PANEL.INC

The Forge panel (included by forge.c, for its statics): what the monitor
opens with X, drawn on the HUD by forge_draw, no menu of the map's. While it
is open the monitor's camera stays and its buttons are the panel's:

- the D-pad's left and right: the page - each of the panel's categories
  (VEHICLES, WEAPONS, EQUIPMENT, SCENERY, TELEPORTERS, OBJECTIVES, SPAWNING)
  and FORGE, the Forge menu's rows;
- the D-pad's up and down: the row;
- A: the row chosen - an item of a category spawned in the monitor's hand
  (its marker held), a menu row done; B or X: closed.

Holding an object or a marker, X opens its PROPERTIES instead: up and down
the row, left and right (or A) its value stepped (forge_property_apply).

The Forge menu's rows: UNDO, REDO, SNAP, SAVE LAYOUT (under the session's
layout's title, else the map's name and "Layout"), NEW ROUND and PLAY AS
SPARTAN. The tests step it with "forge panel <step>".
*/

/* ---------- constants */

enum
{
	_forge_panel_page_items = 0,
	_forge_panel_page_properties,
};

/* (the FORGE page: past the categories) */
#define FORGE_PANEL_MENU_PAGE NUMBER_OF_FORGE_CATEGORIES

enum
{
	_forge_panel_menu_undo = 0,
	_forge_panel_menu_redo,
	_forge_panel_menu_snap,
	_forge_panel_menu_save,
	_forge_panel_menu_round,
	_forge_panel_menu_spartan,
	NUMBER_OF_FORGE_PANEL_MENU_ROWS
};

/* (forge panel <step>) */
enum
{
	_forge_panel_step_none = 0,
	_forge_panel_step_open,
	_forge_panel_step_close,
	_forge_panel_step_up,
	_forge_panel_step_down,
	_forge_panel_step_left,
	_forge_panel_step_right,
	_forge_panel_step_choose,
	_forge_panel_step_back,
};

/* ---------- private code */

static void forge_panel_close(
	void)
{
	if (forge_globals.panel_open)
		platform_log("forge: the panel closed");
	forge_globals.panel_open = FALSE;
}

/* how many rows the page has */
static short forge_panel_rows(
	void)
{
	if (forge_globals.panel_page == _forge_panel_page_properties)
	{
		struct forge_properties properties;

		if (!forge_properties_get(&properties))
			return 0;
		/* (a marker's: team, game type, kind, yaw) */
		return properties.spawn ? 4 : NUMBER_OF_FORGE_PROPERTIES;
	}
	if (forge_globals.panel_category == FORGE_PANEL_MENU_PAGE)
		return NUMBER_OF_FORGE_PANEL_MENU_ROWS;
	return forge_category_item_count(forge_globals.panel_category);
}

/* the layout's title a save takes: the session's, else "<map> Layout" */
static void forge_panel_save_title(
	char *title,
	int size)
{
	char level[FORGE_NAME_SIZE];

	if (forge_globals.variant_title[0])
	{
		snprintf(title, (size_t)size, "%s", forge_globals.variant_title);
		return;
	}
	forge_level_name(level, sizeof(level));
	snprintf(title, (size_t)size, "%s Layout", level);
}

/* a property row's words, as the panel shows it */
static void forge_panel_property_row(
	struct forge_properties const *properties,
	short row,
	char *text,
	int size)
{
	static char const *const physics[NUMBER_OF_FORGE_PHYSICS_MODES] = { "NORMAL", "FIXED", "PHASED" };
	char const *team = properties->team == 0 ? "RED" : properties->team == 1 ? "BLUE" : "NEUTRAL";

	if (properties->spawn)
	{
		boolean teleporter = properties->spawn_kind == _forge_spawn_teleporter_sender ||
			properties->spawn_kind == _forge_spawn_teleporter_receiver;
		boolean objective = properties->spawn_kind >= _forge_spawn_flag;

		switch (row)
		{
		case _forge_property_team:
			if (teleporter)
				snprintf(text, (size_t)size, "CHANNEL: %d%s", properties->channel + 1, properties->linked ? "" : " (unlinked)");
			else if (objective)
				snprintf(text, (size_t)size, "INDEX: %d", properties->channel + 1);
			else
				snprintf(text, (size_t)size, "TEAM: %s", properties->team == 0 ? "RED" : properties->team == 1 ? "BLUE" : "ANY");
			return;
		case _forge_property_respawn:
			snprintf(text, (size_t)size, "GAME TYPE: %s", teleporter || objective ? "-" : properties->game_type_name);
			return;
		case _forge_property_place_at_start:
			snprintf(text, (size_t)size, "KIND: %s", forge_spawn_kind_names[properties->spawn_kind % NUMBER_OF_FORGE_MARKER_KINDS]);
			return;
		default:
			snprintf(text, (size_t)size, "YAW: %d", properties->yaw);
			return;
		}
	}
	switch (row)
	{
	case _forge_property_team:
		snprintf(text, (size_t)size, "TEAM: %s", team);
		return;
	case _forge_property_respawn:
		if (properties->respawn_seconds)
			snprintf(text, (size_t)size, "RESPAWN: %d s", properties->respawn_seconds);
		else
			snprintf(text, (size_t)size, "RESPAWN: NEVER");
		return;
	case _forge_property_place_at_start:
		snprintf(text, (size_t)size, "PLACE AT START: %s", properties->place_at_start ? "YES" : "NO");
		return;
	case _forge_property_yaw:
		snprintf(text, (size_t)size, "YAW: %d", properties->yaw);
		return;
	case _forge_property_pitch:
		snprintf(text, (size_t)size, "PITCH: %d", properties->pitch);
		return;
	case _forge_property_roll:
		snprintf(text, (size_t)size, "ROLL: %d", properties->roll);
		return;
	case _forge_property_reset_rotation:
		snprintf(text, (size_t)size, "RESET ROTATION");
		return;
	case _forge_property_physics:
		snprintf(text, (size_t)size, "PHYSICS: %s", physics[PIN(properties->physics, 0, NUMBER_OF_FORGE_PHYSICS_MODES - 1)]);
		return;
	}
	text[0] = 0;
}

/* the page's lines for forge_draw (its title, its rows from the first
shown, its hint) */
static void forge_panel_lines(
	void)
{
	short count = forge_panel_rows();
	short row;

	forge_globals.panel_row = (short)(count > 0 ? PIN(forge_globals.panel_row, 0, count - 1) : 0);
	if (forge_globals.panel_row < forge_globals.panel_first_row)
		forge_globals.panel_first_row = forge_globals.panel_row;
	if (forge_globals.panel_row >= forge_globals.panel_first_row + FORGE_PANEL_ROWS)
		forge_globals.panel_first_row = (short)(forge_globals.panel_row - FORGE_PANEL_ROWS + 1);
	forge_globals.panel_first_row = (short)PIN(forge_globals.panel_first_row, 0, MAX(count - 1, 0));
	forge_globals.panel_row_count = 0;
	if (forge_globals.panel_page == _forge_panel_page_properties)
	{
		struct forge_properties properties;

		if (!forge_properties_get(&properties))
		{
			forge_panel_close();
			return;
		}
		snprintf(forge_globals.panel_title, sizeof(forge_globals.panel_title), "PROPERTIES: %s", properties.name);
		for (row = forge_globals.panel_first_row; row < count && forge_globals.panel_row_count < FORGE_PANEL_ROWS; row++)
		{
			forge_panel_property_row(&properties, row, forge_globals.panel_rows[forge_globals.panel_row_count],
				sizeof(forge_globals.panel_rows[0]));
			forge_globals.panel_row_count++;
		}
		snprintf(forge_globals.panel_hint, sizeof(forge_globals.panel_hint), "D-pad: row and value   B: close");
		return;
	}
	if (forge_globals.panel_category == FORGE_PANEL_MENU_PAGE)
	{
		short undo = forge_globals.undo_cursor;
		short redo = (short)(forge_globals.undo_count - forge_globals.undo_cursor);
		char title[FORGE_FILE_TITLE_SIZE];

		forge_panel_save_title(title, sizeof(title));
		snprintf(forge_globals.panel_title, sizeof(forge_globals.panel_title), "< FORGE >");
		for (row = forge_globals.panel_first_row; row < count && forge_globals.panel_row_count < FORGE_PANEL_ROWS; row++)
		{
			char *line = forge_globals.panel_rows[forge_globals.panel_row_count];
			size_t size = sizeof(forge_globals.panel_rows[0]);

			switch (row)
			{
			case _forge_panel_menu_undo: snprintf(line, size, "UNDO (%d)", undo); break;
			case _forge_panel_menu_redo: snprintf(line, size, "REDO (%d)", redo); break;
			case _forge_panel_menu_snap: snprintf(line, size, "SNAP: %s", forge_globals.snap ? "ON" : "OFF"); break;
			case _forge_panel_menu_save: snprintf(line, size, "SAVE LAYOUT \"%s\"", title); break;
			case _forge_panel_menu_round: snprintf(line, size, "NEW ROUND"); break;
			default: snprintf(line, size, "PLAY AS SPARTAN"); break;
			}
			forge_globals.panel_row_count++;
		}
		snprintf(forge_globals.panel_hint, sizeof(forge_globals.panel_hint), "%s",
			forge_globals.menu_status[0] ? forge_globals.menu_status : "A: choose   B: close");
		return;
	}
	snprintf(forge_globals.panel_title, sizeof(forge_globals.panel_title), "< %s >",
		forge_category_name(forge_globals.panel_category));
	for (row = forge_globals.panel_first_row; row < count && forge_globals.panel_row_count < FORGE_PANEL_ROWS; row++)
	{
		struct forge_item item;

		if (!forge_category_item(forge_globals.panel_category, row, &item))
			break;
		if (item.placed)
			snprintf(forge_globals.panel_rows[forge_globals.panel_row_count], sizeof(forge_globals.panel_rows[0]), "%s (%d)",
				item.name, item.placed);
		else
			snprintf(forge_globals.panel_rows[forge_globals.panel_row_count], sizeof(forge_globals.panel_rows[0]), "%s",
				item.name);
		forge_globals.panel_row_count++;
	}
	if (!count)
		snprintf(forge_globals.panel_rows[forge_globals.panel_row_count++], sizeof(forge_globals.panel_rows[0]), "%s",
			"(none on this map)");
	snprintf(forge_globals.panel_hint, sizeof(forge_globals.panel_hint), "%s   %d/%d", "A: place   B: close",
		forge_objects_counted(), FORGE_MAXIMUM_OBJECTS);
}

static void forge_panel_open(
	void)
{
	forge_globals.panel_open = TRUE;
	forge_globals.panel_row = 0;
	forge_globals.panel_first_row = 0;
	if (object_try_and_get(forge_globals.held_object_index) || forge_globals.held_marker_id)
	{
		struct forge_properties properties;

		if (!forge_properties_get(&properties))
		{
			forge_globals.panel_open = FALSE;
			forge_notice("%s", "Only objects you placed have properties");
			return;
		}
		forge_globals.panel_page = _forge_panel_page_properties;
		platform_log("forge: the panel opened: properties of %s", properties.name);
	}
	else
	{
		forge_globals.panel_page = _forge_panel_page_items;
		forge_palette_ready();
		platform_log("forge: the panel opened: %s", forge_globals.panel_category == FORGE_PANEL_MENU_PAGE ? "forge menu" :
			forge_category_log_names[PIN(forge_globals.panel_category, 0, NUMBER_OF_FORGE_CATEGORIES - 1)]);
	}
	forge_panel_lines();
}

/* the Forge menu's row done */
static void forge_panel_menu_choose(
	short row)
{
	switch (row)
	{
	case _forge_panel_menu_undo:
		forge_undo_step(FALSE);
		break;
	case _forge_panel_menu_redo:
		forge_undo_step(TRUE);
		break;
	case _forge_panel_menu_snap:
		forge_globals.snap = !forge_globals.snap;
		forge_menu_notice("%s", forge_globals.snap ? "Snap: On" : "Snap: Off");
		break;
	case _forge_panel_menu_save:
	{
		char title[FORGE_FILE_TITLE_SIZE];

		forge_panel_save_title(title, sizeof(title));
		forge_variant_save(title);
		break;
	}
	case _forge_panel_menu_round:
		forge_panel_close();
		forge_globals.round_requested = TRUE;
		break;
	case _forge_panel_menu_spartan:
		forge_panel_close();
		forge_end();
		break;
	}
}

/* (forge_update, the monitor's, each frame) the panel opened, stepped and
closed by the monitor's buttons (or the tests' step) */
static void forge_panel_update(
	struct forge_pad const *pad,
	boolean const pressed[NUMBER_OF_GAMEPAD_BUTTONS])
{
	short step = forge_globals.test_panel_step;
	short count;

	forge_globals.test_panel_step = _forge_panel_step_none;
	(void)pad;
	if (!forge_globals.panel_open)
	{
		if (pressed[_forge_button_panel] || step == _forge_panel_step_open)
			forge_panel_open();
		return;
	}
	if (step == _forge_panel_step_none)
	{
		step = pressed[_forge_button_panel] || pressed[_gamepad_analog_button_b] ? _forge_panel_step_close :
			pressed[_gamepad_binary_button_dpad_up] ? _forge_panel_step_up :
			pressed[_gamepad_binary_button_dpad_down] ? _forge_panel_step_down :
			pressed[_gamepad_binary_button_dpad_left] ? _forge_panel_step_left :
			pressed[_gamepad_binary_button_dpad_right] ? _forge_panel_step_right :
			pressed[_gamepad_analog_button_a] ? _forge_panel_step_choose : _forge_panel_step_none;
	}
	count = forge_panel_rows();
	switch (step)
	{
	case _forge_panel_step_close:
	case _forge_panel_step_back:
		forge_panel_close();
		return;
	case _forge_panel_step_up:
		if (count > 0)
			forge_globals.panel_row = (short)((forge_globals.panel_row + count - 1) % count);
		break;
	case _forge_panel_step_down:
		if (count > 0)
			forge_globals.panel_row = (short)((forge_globals.panel_row + 1) % count);
		break;
	case _forge_panel_step_left:
	case _forge_panel_step_right:
		if (forge_globals.panel_page == _forge_panel_page_properties)
		{
			forge_property_apply(forge_globals.panel_row, (short)(step == _forge_panel_step_left ? -1 : 1));
		}
		else
		{
			forge_globals.panel_category = (short)((forge_globals.panel_category + (step == _forge_panel_step_left ? -1 : 1) +
				FORGE_PANEL_MENU_PAGE + 1) % (FORGE_PANEL_MENU_PAGE + 1));
			forge_globals.panel_row = 0;
			forge_globals.panel_first_row = 0;
		}
		break;
	case _forge_panel_step_choose:
		if (forge_globals.panel_page == _forge_panel_page_properties)
		{
			forge_property_apply(forge_globals.panel_row, 1);
		}
		else if (forge_globals.panel_category == FORGE_PANEL_MENU_PAGE)
		{
			forge_panel_menu_choose(forge_globals.panel_row);
		}
		else if (count > 0)
		{
			short category = forge_globals.panel_category;
			short row = forge_globals.panel_row;

			forge_panel_close();
			forge_category_choose(category, row);
			return;
		}
		break;
	}
	if (forge_globals.panel_open)
		forge_panel_lines();
}

/* (forge_draw) the panel, at the left of the view */
static void forge_panel_draw(
	long font_index,
	short height)
{
	rectangle2d window = render.camera.window_bounds;
	rectangle2d bounds;
	short row;

	bounds = window;
	bounds.x0 = (short)(window.x0 + 40);
	bounds.y0 = (short)(window.y0 + 20 + height * 3);
	bounds.y1 = (short)(bounds.y0 + height);
	forge_draw_line(font_index, &bounds, 0, 1.f, 0.86f, 0.45f, forge_globals.panel_title);
	for (row = 0; row < forge_globals.panel_row_count && row < FORGE_PANEL_ROWS; row++)
	{
		boolean selected = forge_globals.panel_first_row + row == forge_globals.panel_row;
		char line[FORGE_LINE_SIZE];

		bounds.y0 = bounds.y1;
		bounds.y1 = (short)(bounds.y0 + height);
		snprintf(line, sizeof(line), "%s%s", selected ? "> " : "   ", forge_globals.panel_rows[row]);
		if (selected)
			forge_draw_line(font_index, &bounds, 0, 0.95f, 0.62f, 0.30f, line);
		else
			forge_draw_line(font_index, &bounds, 0, 0.85f, 0.9f, 0.95f, line);
	}
	bounds.y0 = (short)(bounds.y1 + height / 2);
	bounds.y1 = (short)(bounds.y0 + height);
	forge_draw_line(font_index, &bounds, 0, 0.62f, 0.71f, 0.80f, forge_globals.panel_hint);
}
