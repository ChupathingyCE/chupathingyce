"""Check that each controller drives its own local player in split screen.

Two parts of the real code, compiled on their own and driven as the game
drives them:

- the menus' routing of a controller's events (source/interface/ui_widget.c:
  widget_event_controller, widget_takes_events_of_controller and what they
  call), on the screens split screen goes through: Xbox SELECT PROFILE's four
  quarters (each controller presses A to join its own), a one-person main
  menu (any controller drives it), and a second player's own pause menu;
- the gamepads' ports (port/linux/src/xinput_sdl.c's gamepad_ports_assign):
  the order controllers were connected in while one person plays, and while
  two or more do, a port kept for each controller as others are plugged in
  and out.

Where SDL3 is installed (pkg-config sdl3, or Homebrew's), the ports are also
taken from SDL's own virtual gamepads, attached, unplugged and plugged back.

The negative controls put back what main had before: the first player's menus
taking every controller's events while nobody has joined (a second
controller's A joined the first player's quarter, as the first player), and
the ports in connection order always (unplugging the first controller handed
the second player's controller to the first player).

Needs only a C compiler. Usage: test_split_screen_input.py [--cc clang]
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UI_WIDGET = ROOT / "source/interface/ui_widget.c"
XINPUT = ROOT / "port/linux/src/xinput_sdl.c"


def function(source, name):
    """The definition of the static function name (its comment left out)."""
    match = re.search(r"^static [^\n;]*\b%s\(\n?[^;{]*?\)\n\{" % re.escape(name), source, re.M)
    if not match:
        raise SystemExit("%s: no function %s" % (UI_WIDGET.name, name))
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if not depth:
                return source[match.start():index + 1] + "\n"
    raise SystemExit("unbalanced function %s" % name)


ROUTING_PRELUDE = r'''
#include <stdio.h>
#include <string.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
struct widget_instance
{
	const char *name;
	short local_player_index;
	struct widget_instance *next;
	struct widget_instance *parent;
	struct widget_instance *child;
};
static struct { struct widget_instance *active_widgets[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS]; } widget_globals;
static boolean we_are_at_the_main_menu;
static short player_spawn_count;
static short local_players;
static unsigned char split_players;
static short local_player_count(void) { return local_players; }
unsigned char pc_menu_split_players(void) { return split_players; }
static short player_ui_get_single_player_local_player_controller(short player) { return player; }
'''

ROUTING_DRIVER = r'''
static int failures;

#define CHECK(condition, text) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s\n", text); \
			failures++; \
		} \
	} while (0)

/* a widget's own leaf that takes the event (process_ui_widgets: its
_widget_pass_unhandled_events_to_all_children_bit screens) */
static struct widget_instance *dispatch(struct widget_instance *widget, short controller_index)
{
	struct widget_instance *child;

	if (!widget_takes_events_of_controller(widget, controller_index))
		return NULL;
	if (!widget->child)
		return widget;
	for (child = widget->child; child; child = child->next)
	{
		struct widget_instance *taken = dispatch(child, controller_index);

		if (taken)
			return taken;
	}
	return NULL;
}

/* where a button of the controller ends up: each local player's screen in
turn reads the events of the controller widget_event_controller names (all
of them for NONE: get_next_event), and the first that reads it has it */
static struct widget_instance *press(short controller_index)
{
	short widget_index;

	for (widget_index = 0; widget_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; widget_index++)
	{
		struct widget_instance *widget = widget_globals.active_widgets[widget_index];
		short reads;

		if (!widget)
			continue;
		reads = widget_event_controller(widget);
		if (reads == NONE || reads == controller_index)
			return dispatch(widget, controller_index);
	}
	return NULL;
}

static void link_children(struct widget_instance *parent, struct widget_instance *children, int count)
{
	int index;

	parent->child = count ? &children[0] : NULL;
	for (index = 0; index < count; index++)
	{
		children[index].parent = parent;
		children[index].next = index + 1 < count ? &children[index + 1] : NULL;
	}
}

static void reset(void)
{
	memset(&widget_globals, 0, sizeof(widget_globals));
	we_are_at_the_main_menu = TRUE;
	player_spawn_count = 0;
	local_players = 1;
	split_players = 0;
}

int main(void)
{
	struct widget_instance join = { "4way_start2join_screen", NONE, NULL, NULL, NULL };
	struct widget_instance quarters[4] =
	{
		{ "qtr_screen_start2join", 0, NULL, NULL, NULL },
		{ "qtr_screen_start2join", 1, NULL, NULL, NULL },
		{ "qtr_screen_start2join", 2, NULL, NULL, NULL },
		{ "qtr_screen_start2join", 3, NULL, NULL, NULL },
	};
	struct widget_instance main_menu = { "main_menu", 0, NULL, NULL, NULL };
	struct widget_instance main_menu_items[2] = { { "campaign", 0, NULL, NULL, NULL }, { "multiplayer", 0, NULL, NULL, NULL } };
	struct widget_instance pause_1 = { "pause_menu", 0, NULL, NULL, NULL };
	struct widget_instance pause_2 = { "pause_menu", 1, NULL, NULL, NULL };
	short controller;

	link_children(&join, quarters, 4);
	link_children(&main_menu, main_menu_items, 2);

	/* SELECT PROFILE, nobody joined yet: each controller's A is its own quarter's */
	reset();
	widget_globals.active_widgets[0] = &join;
	for (controller = 0; controller < 4; controller++)
	{
		char text[96];

		snprintf(text, sizeof(text), "SELECT PROFILE: controller %d presses A in quarter %d", controller + 1, controller + 1);
		CHECK(press(controller) == &quarters[controller], text);
	}
	/* ... and once two have joined */
	split_players = 1;
	CHECK(press(1) == &quarters[1], "SELECT PROFILE, two joined: controller 2 stays in quarter 2");
	CHECK(press(3) == &quarters[3], "SELECT PROFILE, two joined: controller 4 joins quarter 4");

	/* one person at the main menu: any controller drives it (a phone's
	gamepad behind its touch controls, a second pad picked up) */
	reset();
	widget_globals.active_widgets[0] = &main_menu;
	CHECK(press(0) == &main_menu_items[0], "main menu: controller 1 drives it");
	CHECK(press(1) == &main_menu_items[0], "main menu, one person: controller 2 drives it");
	CHECK(press(3) == &main_menu_items[0], "main menu, one person: controller 4 drives it");
	/* ... but not once a second player is being added */
	split_players = 1;
	CHECK(press(1) == NULL, "main menu, a second player: controller 2 does not drive player 1's menu");

	/* in a split screen game: each player's pause menu is their own */
	reset();
	we_are_at_the_main_menu = FALSE;
	local_players = 2;
	split_players = 1;
	widget_globals.active_widgets[0] = &pause_1;
	widget_globals.active_widgets[1] = &pause_2;
	CHECK(press(0) == &pause_1, "game: controller 1 drives player 1's pause menu");
	CHECK(press(1) == &pause_2, "game: controller 2 drives player 2's pause menu");
	widget_globals.active_widgets[0] = NULL;
	CHECK(press(0) == NULL, "game: controller 1 does not drive player 2's pause menu");
	widget_globals.active_widgets[0] = &pause_1;
	widget_globals.active_widgets[1] = NULL;
	CHECK(press(1) == NULL, "game: controller 2 does not drive player 1's pause menu");

	/* one player, a second controller's own screen up (its sign-in): it keeps to it */
	reset();
	widget_globals.active_widgets[0] = &main_menu;
	widget_globals.active_widgets[1] = &pause_2;
	CHECK(press(1) == &pause_2, "one player: a controller with a screen of its own keeps to it");
	CHECK(press(0) == &main_menu_items[0], "one player: controller 1 still drives the main menu");

	printf("menus: %s\n", failures ? "FAILED" : "each controller drives its own player's screens");
	return failures != 0;
}
'''

PORTS_PRELUDE = r'''
#include <stdio.h>
#include <string.h>
typedef int BOOL;
#ifdef WITH_SDL
#include <SDL3/SDL.h>
#else
typedef unsigned int SDL_JoystickID;
#endif
#define TRUE 1
#define FALSE 0
#define PORT_COUNT 4
'''

PORTS_DRIVER = r'''
static int failures;

#define CHECK(condition, text) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s\n", text); \
			failures++; \
		} \
	} while (0)

/* the gamepads connected (their ids, in connection order) */
static SDL_JoystickID connected[8];
static int connected_count;

#ifdef WITH_SDL
static SDL_JoystickID sdl_ids[8];

static SDL_JoystickID plug(int pad)
{
	SDL_VirtualJoystickDesc desc;

	SDL_INIT_INTERFACE(&desc);
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
	desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
	desc.name = "Test Gamepad";
	sdl_ids[pad] = SDL_AttachVirtualJoystick(&desc);
	SDL_OpenGamepad(sdl_ids[pad]);
	return sdl_ids[pad];
}

static void unplug(int pad)
{
	SDL_CloseGamepad(SDL_GetGamepadFromID(sdl_ids[pad]));
	SDL_DetachVirtualJoystick(sdl_ids[pad]);
	sdl_ids[pad] = 0;
}

/* as sdl_gamepads lists them: SDL's own order */
static void list(void)
{
	SDL_JoystickID *ids;
	int count = 0, index;

	SDL_UpdateJoysticks();
	ids = SDL_GetGamepads(&count);
	connected_count = 0;
	for (index = 0; ids && index < count && connected_count < PORT_COUNT; index++)
	{
		if (SDL_GetGamepadFromID(ids[index]))
			connected[connected_count++] = ids[index];
	}
	SDL_free(ids);
}

static SDL_JoystickID id_of(int pad)
{
	return sdl_ids[pad];
}
#else
static SDL_JoystickID next_id = 100;
static SDL_JoystickID fake_ids[8];

static SDL_JoystickID plug(int pad)
{
	fake_ids[pad] = next_id++;
	connected[connected_count++] = fake_ids[pad];
	return fake_ids[pad];
}

static void unplug(int pad)
{
	int index, kept = 0;

	for (index = 0; index < connected_count; index++)
	{
		if (connected[index] != fake_ids[pad])
			connected[kept++] = connected[index];
	}
	connected_count = kept;
	fake_ids[pad] = 0;
}

static void list(void)
{
}

static SDL_JoystickID id_of(int pad)
{
	return fake_ids[pad];
}
#endif

/* the port of a pad (from 0; -1 for none), as XInputGetState reads it */
static int ports_in_use;
static int port_of_pad(int pad, BOOL keep)
{
	int port_of[PORT_COUNT];
	int index;

	list();
	ports_in_use = gamepad_ports_assign(connected, connected_count, keep, port_of);
	for (index = 0; index < connected_count; index++)
	{
		if (connected[index] == id_of(pad))
			return port_of[index];
	}
	return -1;
}

int main(void)
{
#ifdef WITH_SDL
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (!SDL_Init(SDL_INIT_GAMEPAD))
	{
		printf("FAIL SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
#endif
	/* one person: the ports follow the connection order */
	plug(0);
	plug(1);
	CHECK(port_of_pad(0, FALSE) == 0 && port_of_pad(1, FALSE) == 1, "one person: pads 1 and 2 on ports 1 and 2");

	/* split screen starts: the same ports */
	CHECK(port_of_pad(0, TRUE) == 0 && port_of_pad(1, TRUE) == 1, "split screen: pads 1 and 2 keep ports 1 and 2");
	/* pad 1 unplugged mid-game: pad 2 stays player 2's */
	unplug(0);
	CHECK(port_of_pad(1, TRUE) == 1, "split screen: pad 1 unplugged, pad 2 stays on port 2");
	CHECK(ports_in_use == 2, "split screen: pad 1's port kept while it is out");
	/* plugged back (a new controller to SDL): player 1's again */
	plug(0);
	CHECK(port_of_pad(0, TRUE) == 0, "split screen: pad 1 plugged back takes port 1");
	CHECK(port_of_pad(1, TRUE) == 1, "split screen: pad 2 still on port 2");
	/* pad 2 unplugged: pad 1 is not moved to port 2 (one-gamepad split) */
	unplug(1);
	CHECK(port_of_pad(0, TRUE) == 0, "split screen: pad 2 unplugged, pad 1 stays on port 1");
	CHECK(ports_in_use == 2, "split screen: pad 2's port kept while it is out");
	/* four players; the third unplugged and another plugged in: port 3 */
	plug(1);
	plug(2);
	plug(3);
	CHECK(port_of_pad(1, TRUE) == 1 && port_of_pad(2, TRUE) == 2 && port_of_pad(3, TRUE) == 3,
		"split screen: pads 2, 3 and 4 on ports 2, 3 and 4");
	unplug(2);
	CHECK(port_of_pad(3, TRUE) == 3, "split screen: pad 3 unplugged, pad 4 stays on port 4");
	plug(4);
	CHECK(port_of_pad(4, TRUE) == 2, "split screen: a new pad takes port 3, the one left");
	CHECK(port_of_pad(0, TRUE) == 0 && port_of_pad(1, TRUE) == 1 && port_of_pad(3, TRUE) == 3,
		"split screen: the other pads keep their ports");

	/* back to one person: the ports follow the connection order again */
	unplug(0);
	CHECK(port_of_pad(1, FALSE) == 0, "one person: the first connected pad on port 1");
#ifdef WITH_SDL
	printf("ports (SDL virtual gamepads): %s\n", failures ? "FAILED" : "each controller keeps its player's port");
	SDL_Quit();
#else
	printf("ports: %s\n", failures ? "FAILED" : "each controller keeps its player's port");
#endif
	return failures != 0;
}
'''


def routing_code(old):
    source = UI_WIDGET.read_text()
    names = ["widget_tree_has_local_player", "controller_has_own_widget", "widget_takes_any_controller",
        "widget_event_controller", "widget_takes_events_of_controller"]
    code = "".join(function(source, name) for name in names)
    if old:
        # main before: a controller's own widget did not keep its events
        code, count = re.subn(r"controller_has_own_widget\((controller_index)\)", "FALSE", code)
        if count < 2:
            raise SystemExit("negative control: controller_has_own_widget not found where expected")
    return ROUTING_PRELUDE + code + ROUTING_DRIVER


def ports_code(old):
    source = XINPUT.read_text()
    start = source.index("/* ---------- gamepad ports")
    end = source.index("/* ---------- SDL gamepads */", start)
    code = source[start:end]
    if old:
        # main before: the ports in connection order always
        code = code.replace("\tif (!keep)\n", "\t(void)keep;\n\tif (TRUE)\n", 1)
    return PORTS_PRELUDE + code + PORTS_DRIVER


def sdl_flags():
    """The compiler flags for SDL3, or None without it."""
    if shutil.which("pkg-config"):
        result = subprocess.run(["pkg-config", "--cflags", "--libs", "sdl3"], capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.split()
    homebrew = Path("/opt/homebrew")
    if (homebrew / "include/SDL3/SDL.h").exists():
        return ["-I", str(homebrew / "include"), "-L", str(homebrew / "lib"), "-lSDL3"]
    return None


def build_and_run(cc, folder, name, code, flags=()):
    path = Path(folder) / (name + ".c")
    binary = Path(folder) / name
    path.write_text(code)
    subprocess.run([cc, "-std=c99", "-Wall", "-Werror", "-Wno-unused-function", "-o", str(binary), str(path),
        *flags], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    return result.returncode, result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cc", default=shutil.which("cc") or "cc")
    args = parser.parse_args()
    failed = False
    with tempfile.TemporaryDirectory() as folder:
        runs = [("routing", routing_code), ("ports", ports_code)]
        for name, make in runs:
            code, output = build_and_run(args.cc, folder, name, make(old=False))
            print(output, end="")
            failed |= code != 0
            code, output = build_and_run(args.cc, folder, name + "_old", make(old=True))
            if not code:
                print("negative control (%s): main's old code passed, so this test proves nothing" % name)
                failed = True
            else:
                print("negative control (%s): main's old code fails, as it should (%s)" %
                    (name, output.strip().splitlines()[0]))
        flags = sdl_flags()
        if flags:
            code, output = build_and_run(args.cc, folder, "ports_sdl", "#define WITH_SDL 1\n" + ports_code(old=False),
                ["-D_DEFAULT_SOURCE", *flags])
            print(output, end="")
            failed |= code != 0
        else:
            print("ports (SDL virtual gamepads): skipped, no SDL3 here")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
