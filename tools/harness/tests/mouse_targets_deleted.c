/*
MOUSE_TARGETS_DELETED.C

The pointer's targets when the menus' widgets are deleted between the frame
that noted them and the next (#1498): the game's ui_widget_delete and the
pointer's target lookup (source/interface/ui_widget.c), over a fake screen
whose deleted widgets are filled with garbage, as the 0.9.1a crash read
(0x7400...). A target the lookup still finds must be a live widget.
*/

#include "harness.h"
#undef match_assert
#define match_assert(file, line, condition) CHECK(condition, "%s:%d", file, line)
#define match_vassert(file, line, condition, text) CHECK(condition, "%s:%d: %s", file, line, text)

#define csmemset memset
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
#define WIDGET_DELETED_PLAYER_CONTROL_INHIBIT_FLAGS 0
#define _error_silent 0

typedef struct { short x0, y0, x1, y1; } rectangle2d;

enum { _widget_event_deleted = 9 };
enum { _event_handler_run_function_bit, _event_handler_open_widget_bit };
enum { _ui_widget_type_container, _ui_widget_type_text_box, _ui_widget_type_spinner_list,
	_ui_widget_type_column_list };

struct event_record { short controller_index; };
struct ui_widget_event_handler_reference
{
	unsigned long flags;
	short event_type;
	short function;
	struct { long index; } widget_tag;
};
struct ui_widget_definition
{
	char const *name;
	struct { long count; void *address; } event_handlers;
};

struct widget_instance
{
	long definition_tag_index;
	short type;
	short local_player_index;
	boolean delete_recursion_lock;
	boolean pause_game_time;
	struct widget_instance *parent, *child, *next, *previous, *focused_child;
	union
	{
		struct { wchar_t *text; } text_box;
		struct { void *list_items; wchar_t *item_text; struct widget_instance *extended_description; } list;
	} parameters;
};

static struct
{
	boolean initialized;
	boolean sound_paused;
	long pause_game_time_count;
	struct widget_instance *active_widgets[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
} widget_globals;

static struct ui_widget_definition no_handlers = { "screen", { 0, NULL } };
static boolean we_are_at_the_main_menu;
static struct widget_instance *ui_widget_port_error_text_box;
static wchar_t const *ui_widget_port_error_text;
static struct widget_instance *ui_widget_port_override_box;
static wchar_t const *ui_widget_port_override_text;
static void *widget_memory_pool;

static struct ui_widget_definition *ui_widget_definition_get(long tag_index)
{
	(void)tag_index;
	return &no_handlers;
}
static void player_control_inhibit_buttons(short local_player_index, unsigned long flags, boolean inhibit)
{
	(void)local_player_index; (void)flags; (void)inhibit;
}
static boolean ui_widget_event_handler_function_invoke(struct widget_instance *widget, struct event_record *event,
	short function, boolean *deleted)
{
	(void)widget; (void)event; (void)function; (void)deleted;
	return FALSE;
}
static boolean game_time_get_paused(void) { return FALSE; }
static void game_time_set_paused(boolean paused) { (void)paused; }
static void main_menu_ensure_player_queues_exist(void) {}
static void game_time_dispose_from_old_map(void) {}
static void game_time_initialize_for_new_map(void) {}
static void game_time_start(void) {}
static void sound_pause(boolean paused) { (void)paused; }
static void error(int kind, char const *format, ...) { (void)kind; (void)format; }

/* the freed widgets: filled with garbage, as reused memory is */
#define MAXIMUM_WIDGETS 32
static struct widget_instance *freed[MAXIMUM_WIDGETS];
static long freed_count;

static void dispose_pointer(void *pool, void *pointer)
{
	(void)pool;
	CHECK(freed_count < MAXIMUM_WIDGETS, "too many frees");
	freed[freed_count++] = pointer;
	memset(pointer, 0x74, sizeof(struct widget_instance));
}

static boolean widget_is_freed(struct widget_instance const *widget)
{
	long index;

	for (index = 0; index < freed_count; index++)
	{
		if (freed[index] == widget)
			return TRUE;
	}
	return FALSE;
}

#include "under_test.inc"

static struct widget_instance *widget_new(struct widget_instance *parent, short type)
{
	struct widget_instance *widget = calloc(1, sizeof(*widget));

	widget->type = type;
	widget->local_player_index = NONE;
	widget->parent = parent;
	if (parent)
	{
		struct widget_instance *last = parent->child;

		while (last && last->next)
			last = last->next;
		if (last)
		{
			last->next = widget;
			widget->previous = last;
		}
		else
		{
			parent->child = widget;
		}
		if (!parent->focused_child)
			parent->focused_child = widget;
	}
	return widget;
}

static void note(struct widget_instance *widget, short kind, short x0, short y0, short x1, short y1)
{
	struct ui_mouse_target *target = &ui_mouse_targets[ui_mouse_target_count++];

	memset(target, 0, sizeof(*target));
	target->widget = widget;
	target->kind = kind;
	target->bounds.x0 = x0;
	target->bounds.y0 = y0;
	target->bounds.x1 = x1;
	target->bounds.y1 = y1;
}

/* the next frame's hover, as ui_widgets_process_mouse takes it: a target
found is given the focus, which walks its widget's parents */
static void hover(short x, short y)
{
	struct ui_mouse_target *target = ui_mouse_target_at(x, y);

	if (target)
	{
		struct widget_instance *widget = target->widget;

		CHECK(widget && !widget_is_freed(widget), "the target at %d,%d is a deleted widget", x, y);
		while (widget->parent)
		{
			widget = widget->parent;
			CHECK(!widget_is_freed(widget), "a deleted parent above %d,%d", x, y);
		}
	}
}

/* a screen with a list of three rows, a value in its first row, and a
legend button with a label; the frame's targets are noted over them */
static struct widget_instance *screen, *list, *rows[3], *value, *label;

static void build_screen(void)
{
	long index;

	memset(&widget_globals, 0, sizeof(widget_globals));
	widget_globals.initialized = TRUE;
	freed_count = 0;
	ui_mouse_target_count = 0;
	screen = widget_new(NULL, _ui_widget_type_container);
	widget_globals.active_widgets[0] = screen;
	list = widget_new(screen, _ui_widget_type_column_list);
	for (index = 0; index < 3; index++)
		rows[index] = widget_new(list, _ui_widget_type_container);
	value = widget_new(rows[0], _ui_widget_type_spinner_list);
	label = widget_new(screen, _ui_widget_type_text_box);
	for (index = 0; index < 3; index++)
		note(rows[index], _ui_mouse_target_item, 100, (short)(100 + 40 * index), 500, (short)(130 + 40 * index));
	note(value, _ui_mouse_target_value, 300, 100, 500, 130);
	note(screen, _ui_mouse_target_button, 40, 400, 60, 420);
	ui_mouse_targets[ui_mouse_target_count - 1].label = label;
	ui_mouse_hover_pending = TRUE;
	ui_mouse_click_pending = TRUE;
}

static void hover_everywhere(void)
{
	short x, y;

	for (y = 0; y < 480; y += 5)
	{
		for (x = 0; x < 640; x += 5)
			hover(x, y);
	}
}

int main(int argc, char **argv)
{
	char const *case_name = argc == 2 ? argv[1] : "";

	/* the host goes down: the whole screen goes back to the main menu
	between the frame that noted its targets and the next */
	CASE("screen")
	{
		long index;

		build_screen();
		ui_widget_delete(screen);
		CHECK(freed_count == 7, "%ld widgets freed", freed_count);
		hover_everywhere();
		for (index = 0; index < ui_mouse_target_count; index++)
			CHECK(ui_mouse_targets[index].kind == NONE, "target %ld kept its kind", index);
		CHECK(!ui_mouse_hover_pending && !ui_mouse_click_pending, "the screen's hover or click still waits");
		return 0;
	}
	/* a row deleted (its list refilled): the other rows are still targets */
	CASE("row")
	{
		build_screen();
		ui_widget_delete(rows[0]);
		hover_everywhere();
		CHECK(ui_mouse_target_at(110, 110) == NULL, "the deleted row is still hit");
		CHECK(ui_mouse_target_at(110, 150) && ui_mouse_target_at(110, 150)->widget == rows[1], "row 2 is no target");
		CHECK(ui_mouse_target_at(110, 190) && ui_mouse_target_at(110, 190)->widget == rows[2], "row 3 is no target");
		CHECK(ui_mouse_target_at(50, 410) && ui_mouse_target_at(50, 410)->widget == screen, "the button is lost");
		CHECK(ui_mouse_hover_pending && ui_mouse_click_pending, "a row's deletion dropped the screen's hover or click");
		return 0;
	}
	/* a legend's label deleted: its button stays, without the label */
	CASE("label")
	{
		struct ui_mouse_target *button;

		build_screen();
		ui_widget_delete(label);
		button = ui_mouse_target_at(50, 410);
		CHECK(button && button->kind == _ui_mouse_target_button && button->widget == screen, "the button is lost");
		CHECK(button->label == NULL, "the button keeps its deleted label");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
