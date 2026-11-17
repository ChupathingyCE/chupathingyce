"""Checks that every console's build gets the console switches meant for it.

The shared headers leave the desktop's extras out of a console's build with
HALO_CONSOLE_NO_* switches (port/linux/include/halo_port_features.h,
halo_console_features.h). The 360 and the Wii U define HALO_CONSOLE, but the
original Xbox doesn't: it sets HALO_XBOX_CONSOLE and picks its own switches
(warthog-xbox's halo_xbox_features.h). A switch added only under
HALO_CONSOLE missed it: the six-stage HUD meter, the particle streams and the
smoothed shake ran on the Xbox (warthog-xbox#129: the meter's stages made its
models chrome).

Each console is preprocessed here with only the defines its build gives the
shared headers, as its prefix header and build.py set them, and the switches
it gets are compared."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "port" / "linux" / "include"

# what each console's build defines before the shared headers (its prefix
# header and its build.py), as far as the switches go
CONSOLES = {
    "xbox": ["HALO_XBOX_CONSOLE", "HALO_CONSOLE_DELTA", "HALO_CONSOLE_PLAYERS=32", "HALO_MAP_LIST",
             "HALO_CUSTOM_EDITION", "HALO_CONSOLE_GAME_REPORTS"],
    "xbox360": ["HALO_CONSOLE", "HALO_XBOX360_CONSOLE", "HALO_BIG_ENDIAN", "HALO_64BIT",
                "HALO_CONSOLE_DELTA", "HALO_CONSOLE_PLAYERS=32", "HALO_MAP_LIST"],
    "wiiu": ["HALO_CONSOLE", "HALO_WIIU_CONSOLE", "HALO_BIG_ENDIAN", "HALO_CONSOLE_DELTA",
             "HALO_CONSOLE_PLAYERS=32", "HALO_MAP_LIST"],
    "switch": ["HALO_SWITCH_CONSOLE", "HALO_GAME_BROWSER", "HALO_CUSTOM_EDITION", "HALO_MAP_LIST"],
}

DESKTOP = ["HALO_GAME_BROWSER", "HALO_CUSTOM_EDITION", "HALO_MAP_LIST"]

# what a console's system lacks, not an extra it leaves out: one console's
# own (halo_console_features.h)
PLATFORM_GATES = {"HALO_CONSOLE_NO_WRITE_WATCH", "HALO_CONSOLE_NO_THREAD_LOCAL"}

# what the original Xbox must leave out: the ones that reached it before
# (#1068's meter stages, the particle streams, the smoothed shake) and the
# ones its own header sets
XBOX_EXPECTED = {
    "HALO_CONSOLE_NO_SMOOTH_METER_EDGES",
    "HALO_CONSOLE_NO_PARTICLE_STREAMS",
    "HALO_CONSOLE_NO_SMOOTH_SHAKE",
    "HALO_CONSOLE_NO_SMOOTH_FUNCTIONS",
    "HALO_CONSOLE_NO_TEST_GAMEPADS",
    "HALO_CONSOLE_NO_VOICE",
    "HALO_CONSOLE_NO_HIRES",
    "HALO_CONSOLE_NO_TELNET",
    "HALO_CONSOLE_NO_UPDATER",
    "HALO_CONSOLE_NO_DISCORD",
    "HALO_CONSOLE_NO_LIGHT_EFFECTS",
    "HALO_CONSOLE_NO_TITLE_CHOICE",
    "HALO_CONSOLE_NO_GUIDE",
    "HALO_CONSOLE_NO_SHADER_PREP",
    "HALO_CONSOLE_NO_STUBBS",
    "HALO_CONSOLE_NO_DEBUG_CAMERA_KEYS",
}


def gates(defines):
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    # (in halo_port_limits.h's order: the consoles' own switches first)
    source = '#include "{}"\n#include "{}"\n'.format(
        (INCLUDE / "halo_console_features.h").as_posix(), (INCLUDE / "halo_port_features.h").as_posix())
    command = [compiler, "-E", "-dM", "-x", "c", "-"] + ["-D" + define for define in defines]
    result = subprocess.run(command, input=source, capture_output=True, text=True, check=True)
    names = {line.split()[1] for line in result.stdout.splitlines() if line.startswith("#define ")}
    return {name for name in names if name.startswith("HALO_CONSOLE_NO_")} - PLATFORM_GATES


def mcc(defines):
    """HALO_MCC as the shared headers set it for these defines (1: Halo MCC
    maps built in, 0: left out)"""
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    source = '#include "{}"\n#include "{}"\nint halo_mcc = HALO_MCC;\n'.format(
        (INCLUDE / "halo_console_features.h").as_posix(), (INCLUDE / "halo_port_features.h").as_posix())
    command = [compiler, "-E", "-P", "-x", "c", "-"] + ["-D" + define for define in defines]
    result = subprocess.run(command, input=source, capture_output=True, text=True, check=True)
    line = [line for line in result.stdout.splitlines() if "halo_mcc" in line][-1]
    return int(line.split("=")[1].strip(" ;"))


@pytest.mark.parametrize("console", sorted(CONSOLES))
def test_no_console_builds_mcc_maps(console):
    """Halo MCC maps are the desktop's and Android's: a console's build
    leaves their hooks out (HALO_MCC 0), whichever switches it sets"""
    assert mcc(CONSOLES[console]) == 0, "{} builds Halo MCC maps".format(console)


def test_the_desktop_builds_mcc_maps():
    assert mcc(DESKTOP) == 1
    assert mcc(["HALO_ANDROID", "HALO_GAME_BROWSER"]) == 1


# the MCC Workshop and the Steam link (the game's own Steam content client):
# MCC-only sources that build to nothing at HALO_MCC 0
WORKSHOP_SOURCES = sorted(
    [*(ROOT / "port" / "linux" / "src" / "steam_content").glob("*.c"),
     *(ROOT / "port" / "linux" / "src").glob("steam_*.c"),
     *(ROOT / "port" / "linux" / "src").glob("workshop_*.c"),
     ROOT / "port" / "linux" / "src" / "posix_steam_bridge.c",
     ROOT / "port" / "windows" / "src" / "win32_steam_bridge.c",
     ROOT / "port" / "linux" / "game" / "guide_workshop.c"])

# what the shared code (browser_screen.c, guide.c, settings_rows.c) calls
# of them: no-ops on a console
WORKSHOP_HOOKS = {
    "port/linux/src/workshop_browser.h": ["workshop_browser_offered", "workshop_browser_picture_of",
                                          "workshop_browser_cached_picture", "workshop_join_setting",
                                          "workshop_join_set_setting", "workshop_join_start", "workshop_join_poll",
                                          "workshop_join_cancel", "workshop_join_check", "workshop_join_checked"],
    "port/linux/src/steam_client_bridge.h": ["steam_link_available", "steam_link_linked", "steam_link_start",
                                             "steam_link_poll", "steam_link_cancel", "steam_link_set_armed",
                                             "steam_link_armed", "steam_link_unlink", "steam_link_store_secure"],
    "port/linux/game/guide_workshop.h": ["guide_workshop_shown", "guide_workshop_link_modal",
                                         "guide_workshop_link_modal_press", "guide_workshop_link_modal_back",
                                         "guide_workshop_link_modal_render", "guide_workshop_link_note",
                                         "guide_workshop_page_step", "guide_workshop_pages",
                                         "guide_workshop_page_offered", "guide_workshop_render",
                                         "guide_workshop_update"],
}


def test_workshop_sources_build_to_nothing_on_a_console():
    """each Workshop and Steam link source is inside HALO_MCC's gate, from
    before its first include to its end"""
    assert len(WORKSHOP_SOURCES) > 15
    for source in WORKSHOP_SOURCES:
        text = source.read_text(encoding="utf-8")
        gate = text.find("#if !defined(HALO_MCC) || HALO_MCC")
        # (the feature switch's own header may come first, so the gate sees HALO_MCC)
        include = next((match.start() for match in re.finditer(r"#include ([^\n]*)", text)
                        if "halo_port_features.h" not in match.group(1)), -1)
        assert gate >= 0 and (include < 0 or gate < include), "{} isn't gated".format(source.relative_to(ROOT))
        assert text.rstrip().endswith("#endif /* HALO_MCC */"), "{} isn't gated to its end".format(
            source.relative_to(ROOT))


def hooks_left(header, names, defines):
    """the hooks that stay calls (not the header's no-op macros) with these defines"""
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    source = 'typedef unsigned char boolean;\n#include "{}"\n#include "{}"\n#include "{}"\n'.format(
        (INCLUDE / "halo_console_features.h").as_posix(), (INCLUDE / "halo_port_features.h").as_posix(),
        (ROOT / header).as_posix())
    command = [compiler, "-E", "-dM", "-x", "c", "-", "-DHALO_GUIDE"] + ["-D" + define for define in defines]
    result = subprocess.run(command, input=source, capture_output=True, text=True, check=True)
    macros = {line.split()[1].split("(")[0] for line in result.stdout.splitlines() if line.startswith("#define ")}
    return [name for name in names if name not in macros]


@pytest.mark.parametrize("console", sorted(CONSOLES))
@pytest.mark.parametrize("header", sorted(WORKSHOP_HOOKS))
def test_no_console_calls_the_workshop(console, header):
    """the shared code's calls into the Workshop and the Steam link are
    no-op macros on every console, and real calls on the desktop"""
    names = WORKSHOP_HOOKS[header]
    assert hooks_left(header, names, CONSOLES[console]) == [], "{} calls the Workshop".format(console)
    assert hooks_left(header, names, DESKTOP) == names


def test_the_xbox_leaves_out_what_halo_console_leaves_out():
    """the Xbox doesn't define HALO_CONSOLE, so every switch HALO_CONSOLE
    sets has to reach it by HALO_XBOX_CONSOLE too"""
    missing = gates(["HALO_CONSOLE"]) - gates(CONSOLES["xbox"])
    assert not missing, "the original Xbox's build misses {}".format(sorted(missing))


def test_the_xbox_gets_its_switches():
    missing = XBOX_EXPECTED - gates(CONSOLES["xbox"])
    assert not missing, "the original Xbox's build misses {}".format(sorted(missing))


@pytest.mark.parametrize("console", sorted(CONSOLES))
def test_every_console_gets_the_every_console_switches(console):
    """what the Switch alone gets is what the headers give every console
    (their lists of every console's switch): each console gets it too"""
    missing = gates(CONSOLES["switch"]) - gates(CONSOLES[console])
    assert not missing, "{} misses {}".format(console, sorted(missing))


@pytest.mark.parametrize("console", ["xbox", "xbox360", "wiiu"])
def test_the_consoles_on_halo_console_agree(console):
    """the 360, the Wii U and the Xbox leave out the same desktop extras
    (a console's own extras, the Xbox's EXIT and medal icons, aside)"""
    own = {"HALO_CONSOLE_NO_EXIT", "HALO_CONSOLE_NO_MEDAL_ICONS"}
    expected = gates(CONSOLES["wiiu"]) | gates(CONSOLES["xbox360"])
    missing = expected - gates(CONSOLES[console]) - own
    assert not missing, "{} misses {}".format(console, sorted(missing))


def test_the_desktop_leaves_nothing_out():
    assert not gates(DESKTOP)
    assert not gates([])


def test_mcc_network_hooks_are_halo_mccs():
    """the MCC network code (the 43 host switch, a server's playlist check,
    the digest hooks, the mcc_maps capability) builds only with HALO_MCC 1:
    a console offers no mcc_maps and calls none of it"""
    server = (ROOT / "source" / "networking" / "network_server_manager.c").read_text()
    start = server.index("static void network_game_server_mcc_version(")
    gate = server.rfind("#if defined(HALO_MCC) && HALO_MCC", 0, start)
    assert gate != -1 and server.find("#endif", gate) > start
    assert "#define network_game_server_mcc_version(map_name) ((void)sizeof(map_name))" in server
    identity = (ROOT / "port" / "linux" / "game" / "mcc_identity.h").read_text()
    for hook in ("mcc_identity_host_overlay(", "mcc_identity_wire_record("):
        assert "#define " + hook in identity
    peer = (ROOT / "port" / "linux" / "src" / "delta_peer_game.c").read_text()
    offer = peer.index("local.capabilities |= (delta_u32)1 << _delta_capability_mcc_maps;")
    assert peer.rfind("#if defined(HALO_MCC) && HALO_MCC", 0, offer) > peer.rfind("#endif", 0, offer)


def test_workshop_play_hooks_are_the_guides():
    """the Delta Guide's Workshop PLAY's hooks in the shared code (the
    pause menu's QUIT, a lobby ended, a solo campaign, the screen it opened)
    build only where the Guide and MCC maps are: no console builds them"""
    gate = "#if defined(HALO_GUIDE) && (!defined(HALO_MCC) || HALO_MCC)"
    for path, names in (("source/interface/ui_widget_event_handler_functions.c",
                         ("void ui_widget_port_quit_to_main_menu(", "void ui_widget_port_end_network_game(",
                          "void ui_widget_port_campaign_solo(")),
                        ("source/interface/ui_widget.c",
                         ("long ui_widget_port_active_screen_tag(", "boolean ui_widget_port_screen_shown_or_behind(")),
                        ("port/linux/game/menu_functions.c",
                         ("boolean pc_menu_workshop_play(", "void pc_menu_workshop_play_update("))):
        text = (ROOT / path).read_text(encoding="latin-1")
        for name in names:
            at = text.index(name)
            start = text.rfind(gate, 0, at)
            assert start != -1, "{} in {} isn't gated".format(name, path)
            # (the gate still open at the function: its own #if ... #endif inside it counted)
            depth = 0
            for directive in re.findall(r"^\s*#\s*(if|ifdef|ifndef|endif)\b", text[start:at], re.M):
                depth += -1 if directive == "endif" else 1
                assert depth > 0, "{} in {} isn't gated".format(name, path)


def map_list_rows(defines):
    """HALO_CONSOLE_MAP_LIST_ROWS as the shared headers set it for these defines (None: undefined)"""
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    source = '#include "{}"\n#include "{}"\n'.format(
        (INCLUDE / "halo_console_features.h").as_posix(), (INCLUDE / "halo_port_features.h").as_posix())
    command = [compiler, "-E", "-dM", "-x", "c", "-"] + ["-D" + define for define in defines]
    result = subprocess.run(command, input=source, capture_output=True, text=True, check=True)
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] == "HALO_CONSOLE_MAP_LIST_ROWS":
            return int(parts[2])
    return None


@pytest.mark.parametrize("console", sorted(CONSOLES))
def test_every_console_sizes_its_map_list(console):
    """#1140: the menus' map list's rows (ui_map_list.c) by capability, never by a console's name: the
    original Xbox's 40, every other console's 256 (the Switch has no HALO_CONSOLE), the desktop's 2560"""
    assert map_list_rows(CONSOLES[console]) == (40 if console == "xbox" else 256)


def test_the_desktop_map_list_is_unsized():
    assert map_list_rows(DESKTOP) is None
    assert map_list_rows(["HALO_ANDROID", "HALO_GAME_BROWSER"]) is None


def defined(name, defines):
    """whether the shared headers define a name for these defines"""
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    source = '#include "{}"\n#include "{}"\n'.format(
        (INCLUDE / "halo_console_features.h").as_posix(), (INCLUDE / "halo_port_features.h").as_posix())
    command = [compiler, "-E", "-dM", "-x", "c", "-"] + ["-D" + define for define in defines]
    result = subprocess.run(command, input=source, capture_output=True, text=True, check=True)
    return any(line.split()[1] == name for line in result.stdout.splitlines() if line.startswith("#define "))


@pytest.mark.parametrize("console", sorted(CONSOLES))
def test_only_the_xbox_retries_cache_io_forever(console):
    """a cache read or write the system is short of resources for gives up
    after 5 seconds (#1149), but the original Xbox's XAPI keeps trying a
    stalled DVD read, as the retail game did (HALO_CONSOLE_IO_RETRY_FOREVER)"""
    assert defined("HALO_CONSOLE_IO_RETRY_FOREVER", CONSOLES[console]) == (console == "xbox")
    assert not defined("HALO_CONSOLE_IO_RETRY_FOREVER", DESKTOP)
    assert not defined("HALO_CONSOLE_IO_RETRY_FOREVER", ["HALO_ANDROID", "HALO_GAME_BROWSER"])


# the desktop menus' hooks (port/linux/game/menu_tags.c, ui_widget_port.c)
# that warthog-xbox answers itself (port/xbox/src/xbox_port.c, port/xbox/
# game/xbox_desktop_stubs.c and xbox_ce.c): the Xbox doesn't build the
# desktop's menus, so a hook the shared code calls on it that isn't one of
# these doesn't link there (#1170: pc_menu_button_key_cap_left, #1144)
XBOX_PC_MENU_HOOKS = {
    "pc_menu_button_key_cap",
    "pc_menu_event_function_invoke",
    "pc_menu_frame_placement",
    "pc_menu_game_data_function_invoke",
    "pc_menu_owns",
    "pc_menu_split_players",
    "pc_menu_tag",
    "pc_menu_text_color",
    "pc_menu_text_marquee",
}


def test_the_xbox_calls_only_the_menu_hooks_it_has():
    """the shared code the original Xbox builds, preprocessed with its
    defines, names no desktop menu hook (pc_menu_*) but the ones its own
    port answers: a new one is gated off it (#ifndef HALO_XBOX_CONSOLE, or
    the switch of the feature it is for), or added to warthog-xbox too"""
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        pytest.skip("no C compiler")
    head = '#include "{}"\n#include "{}"\n'.format(
        (INCLUDE / "halo_console_features.h").as_posix(), (INCLUDE / "halo_port_features.h").as_posix())
    sources = sorted(path for folder in ("source", "port/console") for path in (ROOT / folder).rglob("*.c")
                     if "pc_menu_" in path.read_text(encoding="utf-8", errors="replace"))
    assert sources
    command = [compiler, "-E", "-P", "-w", "-x", "c", "-"] + ["-D" + define for define in CONSOLES["xbox"]]
    unanswered = {}
    for path in sources:
        # (only the file's own #if lines: its headers aren't needed to see
        # what it calls)
        text = re.sub(r"^[ \t]*#[ \t]*(include|error|warning)\b.*$", "",
                      path.read_text(encoding="utf-8", errors="replace"), flags=re.M)
        result = subprocess.run(command, input=head + text, capture_output=True, text=True, check=True)
        for name in set(re.findall(r"\bpc_menu_\w+", result.stdout)) - XBOX_PC_MENU_HOOKS:
            unanswered.setdefault(name, []).append(path.relative_to(ROOT).as_posix())
    assert unanswered == {}, "the Xbox build calls desktop menu hooks it doesn't have: {}".format(unanswered)
