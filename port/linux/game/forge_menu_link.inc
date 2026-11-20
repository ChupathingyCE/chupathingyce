/*
FORGE_MENU_LINK.INC

What the Forge menu (forge_menu.c) reads of the session, and what it asks
of it (included by forge.c, for its statics). The menu is drawn and driven
by the menus' code (ui_widget.c), over the game; what it asks - an object
placed, a property stepped, a save, a layout loaded - waits here and is done
by forge_update before the next tick, as the monitor's own buttons are, so
no object is touched while the tick runs or a frame draws.
*/

/* ---------- constants */

enum
{
	FORGE_MENU_ASKS_MOST = 8,
};

/* ---------- globals */

static struct
{
	short ask;
	short first;
	short second;
	/* (a layout's file, or SAVE AS NEW's title) */
	char file[FORGE_FILE_TITLE_SIZE];
} forge_menu_asks[FORGE_MENU_ASKS_MOST];
static short forge_menu_ask_count;

/* ---------- private code */

/* the layout's title a save takes: the session's, else "<map> Layout" */
static void forge_menu_save_title(
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

/* the session's map (forge_maps' index), NONE */
static short forge_menu_map(
	void)
{
	return forge_globals.game_active ? forge_map_of_level(forge_globals.game_map) : NONE;
}

/* (forge_update, the monitor's frame) what the menu asked, done */
static void forge_menu_asks_run(
	void)
{
	short index;

	for (index = 0; index < forge_menu_ask_count; index++)
	{
		short first = forge_menu_asks[index].first;
		short second = forge_menu_asks[index].second;
		char const *file = forge_menu_asks[index].file;
		char title[FORGE_FILE_TITLE_SIZE];
		char why[FORGE_STORE_WHY_SIZE];

		switch (forge_menu_asks[index].ask)
		{
		case _forge_menu_ask_place:
			forge_category_choose(first, second);
			break;
		case _forge_menu_ask_property:
			forge_property_apply(first, second);
			break;
		case _forge_menu_ask_undo:
			forge_undo_step(FALSE);
			break;
		case _forge_menu_ask_redo:
			forge_undo_step(TRUE);
			break;
		case _forge_menu_ask_snap:
			forge_globals.snap = !forge_globals.snap;
			forge_menu_notice("%s", forge_globals.snap ? "Snap: On" : "Snap: Off");
			break;
		case _forge_menu_ask_speed:
			forge_globals.speed = (short)((forge_globals.speed + first + NUMBER_OF_FORGE_SPEEDS) % NUMBER_OF_FORGE_SPEEDS);
			forge_menu_notice("%s", forge_speed_names[forge_globals.speed]);
			break;
		case _forge_menu_ask_rotate_snap:
		{
			char snap[16];
			short count = (short)NUMBEROF(forge_rotate_snaps);

			forge_globals.rotate_snap = (short)((forge_globals.rotate_snap + first + count) % count);
			forge_rotate_snap_text(snap, sizeof(snap));
			forge_menu_notice("Rotation snap: %s", snap);
			break;
		}
		case _forge_menu_ask_round:
			forge_globals.round_requested = TRUE;
			break;
		case _forge_menu_ask_spartan:
			forge_end();
			break;
		case _forge_menu_ask_save:
			forge_menu_save_title(title, sizeof(title));
			forge_variant_save(title);
			break;
		case _forge_menu_ask_save_new:
			/* (the title the menu made, one no layout of the map has) */
			forge_variant_save(file);
			break;
		case _forge_menu_ask_load:
			why[0] = 0;
			if (forge_menu_map() == NONE || !forge_game_start(forge_menu_map(), file, why, sizeof(why)))
				forge_menu_notice("%s", why[0] ? why : "That layout can't be loaded.");
			else
				platform_log("forge: the menu's LOAD: %s", file);
			break;
		case _forge_menu_ask_delete:
			if (forge_menu_map() != NONE && forge_store_delete(forge_map_short(forge_menu_map()), file))
			{
				platform_log("forge: the menu's DELETE: %s/%s", forge_map_short(forge_menu_map()), file);
				forge_menu_notice("%s", "Layout deleted.");
				/* (the session's own: a save makes a new file, its title kept) */
				if (!strcmp(file, forge_globals.variant_file))
					forge_globals.variant_file[0] = 0;
			}
			else
			{
				forge_menu_notice("%s", "That layout can't be deleted.");
			}
			break;
		}
	}
	forge_menu_ask_count = 0;
}

/* ---------- public code */

void forge_menu_ask(
	short ask,
	short first,
	short second,
	char const *file)
{
	if (forge_menu_ask_count >= FORGE_MENU_ASKS_MOST || ask < 0 || ask >= NUMBER_OF_FORGE_MENU_ASKS)
		return;
	forge_menu_asks[forge_menu_ask_count].ask = ask;
	forge_menu_asks[forge_menu_ask_count].first = first;
	forge_menu_asks[forge_menu_ask_count].second = second;
	snprintf(forge_menu_asks[forge_menu_ask_count].file, sizeof(forge_menu_asks[0].file), "%s", file ? file : "");
	forge_menu_ask_count++;
}

void forge_menu_view_get(
	struct forge_menu_view *view)
{
	char snap[16];

	csmemset(view, 0, sizeof(*view));
	view->session = forge_globals.game_active && !forge_globals.playing;
	view->monitor = forge_globals.active;
	view->holding = object_try_and_get(forge_globals.held_object_index) != NULL || forge_globals.held_marker_id != 0;
	view->map_index = forge_menu_map();
	view->objects = forge_objects_counted();
	view->undo = forge_globals.undo_cursor;
	view->redo = (short)(forge_globals.undo_count - forge_globals.undo_cursor);
	view->snap = forge_globals.snap;
	view->speed = forge_globals.speed;
	forge_rotate_snap_text(snap, sizeof(snap));
	snprintf(view->rotate_snap, sizeof(view->rotate_snap), "%s", !strcmp(snap, "Off") ? "OFF" : snap);
	snprintf(view->title, sizeof(view->title), "%s", forge_globals.variant_title);
	snprintf(view->file, sizeof(view->file), "%s", forge_globals.variant_file);
	forge_menu_save_title(view->save_title, sizeof(view->save_title));
	forge_level_name(view->level, sizeof(view->level));
	snprintf(view->status, sizeof(view->status), "%s", forge_globals.menu_status);
	view->status_time = forge_globals.menu_status_time;
}

char const *forge_speed_name(
	short speed)
{
	static char const *const names[NUMBER_OF_FORGE_SPEEDS] = { "SLOW", "NORMAL", "FAST" };

	return names[PIN(speed, 0, NUMBER_OF_FORGE_SPEEDS - 1)];
}

short forge_property_count(
	void)
{
	struct forge_properties properties;

	if (!forge_properties_get(&properties))
		return 0;
	/* (a marker's: team, game type, kind, yaw) */
	return properties.spawn ? 4 : NUMBER_OF_FORGE_PROPERTIES;
}

boolean forge_property_text(
	short row,
	char *label,
	int label_size,
	char *value,
	int value_size)
{
	static char const *const physics[NUMBER_OF_FORGE_PHYSICS_MODES] = { "NORMAL", "FIXED", "PHASED" };
	struct forge_properties properties;

	label[0] = 0;
	value[0] = 0;
	if (!forge_properties_get(&properties) || row < 0 || row >= forge_property_count())
		return FALSE;
	if (properties.spawn)
	{
		boolean teleporter = properties.spawn_kind == _forge_spawn_teleporter_sender ||
			properties.spawn_kind == _forge_spawn_teleporter_receiver;
		boolean objective = properties.spawn_kind >= _forge_spawn_flag;

		switch (row)
		{
		case _forge_property_team:
			if (teleporter)
			{
				snprintf(label, (size_t)label_size, "%s", "CHANNEL");
				snprintf(value, (size_t)value_size, "%d%s", properties.channel + 1, properties.linked ? "" : " (UNLINKED)");
			}
			else if (objective)
			{
				snprintf(label, (size_t)label_size, "%s", "INDEX");
				snprintf(value, (size_t)value_size, "%d", properties.channel + 1);
			}
			else
			{
				snprintf(label, (size_t)label_size, "%s", "TEAM");
				snprintf(value, (size_t)value_size, "%s", properties.team == 0 ? "RED" : properties.team == 1 ? "BLUE" : "ANY");
			}
			break;
		case _forge_property_respawn:
			snprintf(label, (size_t)label_size, "%s", "GAME TYPE");
			snprintf(value, (size_t)value_size, "%s", teleporter || objective ? "-" : properties.game_type_name);
			break;
		case _forge_property_place_at_start:
			snprintf(label, (size_t)label_size, "%s", "KIND");
			snprintf(value, (size_t)value_size, "%s", forge_spawn_kind_names[properties.spawn_kind % NUMBER_OF_FORGE_MARKER_KINDS]);
			break;
		default:
			snprintf(label, (size_t)label_size, "%s", "YAW");
			snprintf(value, (size_t)value_size, "%d", properties.yaw);
			break;
		}
		return TRUE;
	}
	switch (row)
	{
	case _forge_property_team:
		snprintf(label, (size_t)label_size, "%s", "TEAM");
		snprintf(value, (size_t)value_size, "%s", properties.team == 0 ? "RED" : properties.team == 1 ? "BLUE" : "NEUTRAL");
		break;
	case _forge_property_respawn:
		snprintf(label, (size_t)label_size, "%s", "RESPAWN");
		if (properties.respawn_seconds)
			snprintf(value, (size_t)value_size, "%d S", properties.respawn_seconds);
		else
			snprintf(value, (size_t)value_size, "%s", "NEVER");
		break;
	case _forge_property_place_at_start:
		snprintf(label, (size_t)label_size, "%s", "PLACE AT START");
		snprintf(value, (size_t)value_size, "%s", properties.place_at_start ? "YES" : "NO");
		break;
	case _forge_property_yaw:
		snprintf(label, (size_t)label_size, "%s", "YAW");
		snprintf(value, (size_t)value_size, "%d", properties.yaw);
		break;
	case _forge_property_pitch:
		snprintf(label, (size_t)label_size, "%s", "PITCH");
		snprintf(value, (size_t)value_size, "%d", properties.pitch);
		break;
	case _forge_property_roll:
		snprintf(label, (size_t)label_size, "%s", "ROLL");
		snprintf(value, (size_t)value_size, "%d", properties.roll);
		break;
	case _forge_property_reset_rotation:
		/* (an action: no value) */
		snprintf(label, (size_t)label_size, "%s", "RESET ROTATION");
		break;
	case _forge_property_physics:
		snprintf(label, (size_t)label_size, "%s", "PHYSICS");
		snprintf(value, (size_t)value_size, "%s", physics[PIN(properties.physics, 0, NUMBER_OF_FORGE_PHYSICS_MODES - 1)]);
		break;
	}
	return TRUE;
}
