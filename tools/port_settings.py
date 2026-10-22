"""The port's settings screens, which tools/ce_menus.py writes in place of the
PC version's (port/assets/menus/ce): laid out as its are (rows of a label and
a spinner over its row backgrounds, a line of help for the row chosen,
Defaults, OK and Cancel), with what this port has to set instead of what the
PC version had (no connection speed or texture quality: a window or the full
screen, the frame rate, the keyboard's own controls).

A row's spinner sets one of config.toml's settings (setting=, values=), or of
the profile being edited ("profile.<field>": the controller's settings), by
port/linux/game/menu_functions.c: "port setting load" shows its value,
"port settings save" (OK) writes those changed and applies them, "port
settings defaults" shows the defaults. A screen's VIEWS are nested lists in
it, which a row of its list opens. Controls Setup has every device's
controls in one list: the keyboard's keys, the mouse's settings, and the
controller's settings and buttons.
"""

from xml.sax.saxutils import quoteattr

PE = "main_menu/settings_select/player_setup/player_profile_edit"
YES_NO = [("YES", "true"), ("NO", "false")]
ON_OFF = [("ON", "true"), ("OFF", "false")]
SENSITIVITIES = [(f"{value:g}", f"{value:g}") for value in (0.1, 0.15, 0.25, 0.4, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4)]
VOLUMES = [(str(step), f"{step / 10:g}") for step in range(11)]

# each screen: its folder below PE, its screen's widget (the name the profile
# menu opens), its header (widget, bitmap), the row spacing, its rows:
# (label, setting, [(shown, value)], help, platform[, key]): key names the
# row's widgets where two rows set one setting (one for each platform), else
# the setting's name does; and its VIEWS, nested lists of more rows:
# (name, the label of the row that opens it, its help, its rows)
SCREENS = {
    "video_settings": {
        "screen": "video_settings_screen",
        "header": ("header_profile_video_settings", f"{PE}/video_settings/header_profile_video_settings"),
        # (close together, for the eleven rows of INTERFACE to fit above a
        # help line)
        "spacing": 24,
        # (rows in the place of the row before them: Window Size in
        # Resolution's, port/linux/game/menu_functions.c showing the one the
        # display mode chosen uses; Android's Smooth Motion in Frame Rate's,
        # and its anti-aliasing in the desktop's)
        "same_place": ["display.window_size", "display.interpolation", "anti_aliasing_android"],
        "rows": [
            ("DISPLAY MODE:", "display.mode",
             [("FULLSCREEN", "fullscreen"), ("BORDERLESS", "borderless"), ("WINDOWED", "windowed")],
             "Fullscreen, borderless or a window. F11 switches.",
             "desktop"),
            # (port/linux/game/menu_tags.c adds the display's resolutions)
            ("RESOLUTION:", "display.resolution", [("NATIVE", "native")],
             "The resolution for fullscreen and borderless.",
             "desktop"),
            # (port/linux/game/menu_tags.c puts the sizes that fit the desktop
            # in place of this one)
            ("WINDOW SIZE:", "display.window_size", [("1280 x 960", "1280x960")],
             "The size of the window.", "desktop"),
            ("RESOLUTION SCALING:", "display.resolution_scaling", [("NATIVE", "native"), ("ORIGINAL", "original")],
             "Native draws at full resolution. Original draws at\n640x480 and scales it up.", "desktop"),
            ("V-SYNC:", "display.vsync", ON_OFF,
             "Sync frames to the display.", None),
            # (port/linux/game/menu_functions.c: 30 also turns display.interpolation
            # off, the others on; Android, with no frame rate cap, has Smooth
            # Motion in its place. Instant Aim, display.direct_camera, is in
            # config.toml alone)
            ("FRAME RATE:", "display.max_fps",
             [("30 (XBOX)", "30"), ("AUTO", "0"), ("60", "60"), ("120", "120"), ("144", "144"),
              ("165", "165"), ("240", "240"), ("NONE", "-1")],
             "The frame rate limit. Auto is twice the display's\nrefresh rate.",
             "desktop"),
            ("SMOOTH MOTION:", "display.interpolation", ON_OFF,
             "Draw a frame for every display refresh.", "android"),
            ("ANTI-ALIASING:", "display.anti_aliasing",
             [("OFF", "off"), ("FXAA", "fxaa"), ("SMAA", "smaa"), ("SSAA 2X", "ssaa2x"), ("MSAA 2X", "msaa2x"),
              ("MSAA 4X", "msaa4x"), ("MSAA 8X", "msaa8x")],
             "Smooth jagged edges.",
             "desktop"),
            ("ANTI-ALIASING:", "display.anti_aliasing",
             [("OFF", "off"), ("FXAA", "fxaa"), ("MSAA 2X", "msaa2x"), ("MSAA 4X", "msaa4x")],
             "Smooth jagged edges.",
             "android", "anti_aliasing_android"),
            ("FIELD OF VIEW:", "display.field_of_view",
             [(f"{value}" + (" (XBOX)" if value == 70 else ""), f"{value}")
              for value in (60, 65, 70, 75, 80, 85, 90, 95, 100, 105, 110, 120)],
             "Adjust the field of view.",
             None),
            ("VIEWMODEL FOV:", "display.viewmodel_field_of_view",
             [("SAME", "0")] + [(f"{value}", f"{value}") for value in (50, 55, 60, 65, 70, 75, 80, 85, 90)],
             "How wide your weapon is drawn. Same follows the view.", None),
        ],
        # (rows that open a nested list in the same screen: VIEWS)
        "views": [
            ("advanced", "ADVANCED", "Shadows, lighting and detail.", [
                ("SHADOW RESOLUTION:", "display.shadow_resolution",
                 [("128", "128"), ("256", "256"), ("512", "512"), ("1024", "1024")],
                 "The detail of object shadows.",
                 None),
                ("PER-PIXEL LIGHTING:", "display.per_pixel_lighting", ON_OFF,
                 "Light models per pixel instead of per vertex.", None),
                ("FULL MODEL DETAIL:", "display.full_model_detail", ON_OFF,
                 "Always draw models at their highest detail.", None),
                ("ANISOTROPIC FILTERING:", "display.anisotropic_filtering",
                 [("OFF", "0"), ("2X", "2"), ("4X", "4"), ("8X", "8"), ("16X", "16")],
                 "Sharper textures seen at an angle.", None),
                ("FAR CLIP:", "display.far_clip_distance",
                 [("1024 (XBOX)", "1024"), ("2048", "2048"), ("4096", "4096"), ("8192", "8192"), ("10000", "10000")],
                 "How far the world is drawn.", None),
            ]),
            # (input.keyboard_prompts is in config.toml alone: the prompts
            # follow the device used last)
            ("interface", "INTERFACE", "The menus, HUD and scoreboard.", [
                # (read when the game starts: menu_tags.c)
                ("MENU STYLE:", "display.menus",
                 [("CHUPATHINGY", "chupathingy"), ("XBOX", "xbox"), ("GEARBOX", "gearbox")],
                 "Takes effect on restart.", None),
                ("HIGH-RES HUD:", "display.high_res_hud", ON_OFF,
                 "Draw the HUD in high resolution.", None),
                ("HIGH-RES TEXT:", "display.high_res_text", ON_OFF,
                 "Draw text and titles in high resolution.", None),
                ("PLAYER NAMES:", "display.player_names",
                 [("ALL", "all"), ("ALLIES", "allies"), ("ENEMIES", "enemies"), ("NONE", "none")],
                 "Choose whose names show above players.", None),
                ("PLAYER NAME SIZE:", "display.player_name_scale",
                 [(f"{value:g}x", f"{value:g}") for value in (0.5, 0.75, 1, 1.25, 1.5, 2)],
                 "Adjust the size of player names.", None),
                ("SCOREBOARD LAYOUT:", "display.scoreboard_team_layout", [("TEAMS", "teams"), ("BY SCORE", "score")],
                 "Show team games by team, or all players by score.", None),
                ("SCOREBOARD PANEL:", "display.scoreboard_background", ON_OFF,
                 "Draw a panel behind the scoreboard.", None),
                ("POWERUP TIMERS:", "display.powerup_timers", ON_OFF,
                 "Show the time left on your powerups.", None),
                ("MATCH TIMER:", "display.match_timer", ON_OFF,
                 "Show the time left at the top of the screen.", None),
                ("FOLLOW CAMERA:", "display.follow_camera",
                 [("LONG WAITS", "long_waits"), ("ALWAYS", "always"), ("OFF", "off")],
                 "While dead, follow a living player's view.", None),
                ("VERSION WATERMARK:", "display.version_watermark", ON_OFF,
                 "Show the version while playing.", None),
            ]),
        ],
    },
    "audio_settings": {
        "screen": "audio_settings_screen",
        "header": ("header_profile_audio_settings", f"{PE}/audio_settings/header_profile_audio_settings"),
        "spacing": 30,
        "rows": [
            ("MASTER VOLUME:", "audio.volume", VOLUMES, "Adjust the overall volume.", None),
            ("MUSIC VOLUME:", "audio.music_volume", VOLUMES, "Adjust the music volume.", None),
            ("EFFECTS VOLUME:", "audio.effects_volume", VOLUMES,
             "Adjust the effects and speech volume.", None),
            # (port/linux/game/menu_tags.c adds the system's outputs; a name
            # too long for the spinner scrolls while its row has the focus:
            # menu_functions.c's pc_menu_text_marquee)
            ("OUTPUT DEVICE:", "audio.device", [("DEFAULT", "default")],
             "Choose the audio output device.", "desktop"),
            ("REVERB:", "audio.reverb", ON_OFF,
             "Room echo, and muffled sound behind walls.", None),
            ("SOUND:", "audio.enabled", ON_OFF,
             "Turn all sound on or off. Takes effect on restart.", None),
        ],
    },
    # (chat.quick_messages, a list of lines, is in config.toml alone. The
    # Delta profile's privacy rows, PR #382's, are its own page:
    # CHUPATHINGY_ONLY)
    "network_setup": {
        "screen": "network_settings_screen",
        "header": ("header_profile_network_settings", f"{PE}/network_setup/header_profile_network_settings"),
        "spacing": 30,
        "rows": [
            ("INTERNET PLAY:", "network.online", ON_OFF,
             "Host and join games over the internet.", None),
            ("UPNP PORT FORWARDING:", "network.allow_upnp", ON_OFF,
             "Ask the router to forward the game's port.", None),
            ("MAP DOWNLOADS:", "network.map_downloads", [("ASK", "ask"), ("ALWAYS", "always"), ("NEVER", "never")],
             "Download missing Custom Edition maps.", None),
            ("JOIN FROM CLIPBOARD:", "network.join_from_clipboard",
             [("ASK", "ask"), ("ALWAYS", "always"), ("NEVER", "never")],
             "Join a game from an invite link on the clipboard.", None),
            ("CHECK FOR UPDATES:", "update.auto", ON_OFF,
             "Check for a new version at startup.", None),
            ("START WITH ONE PLAYER:", "game.start_with_one_player", ON_OFF,
             "Let a game you host start with only you.", None),
        ],
        "views": [
            ("chat", "CHAT", "Text chat and the word filter.", [
                ("CHAT:", "chat.enabled", ON_OFF, "Text chat with other players.", None),
                ("SHOW CHAT:", "chat.show", [("ALL", "all"), ("TEAM", "team"), ("OFF", "off")],
                 "Choose whose chat lines show.", None),
                ("WORD FILTER:", "chat.filter", [("MASK", "mask"), ("HIDE", "hide"), ("SHOW", "show")],
                 "Mask, hide or show blocked words.", None),
                ("STREAMER MODE:", "chat.streamer_mode", ON_OFF,
                 "Show players in chat by team, not by name.", None),
            ]),
            ("privacy", "PRIVACY", "What stats and reports are sent.", [
                ("HOSTED GAME STATS:", "network.report_hosted_games", ON_OFF,
                 "Send stats of games you host to the game list.", None),
                ("DELTA STATS:", "network.report_events", ON_OFF,
                 "Record games you host for Delta match pages.", None),
                ("JOINED GAME STATS:", "network.report_joined_games", ON_OFF,
                 "Add games you join to your game list profile.", None),
                ("CRASH REPORTS:", "crash_reports.upload", [("ASK", "ask"), ("YES", "yes"), ("NO", "no")],
                 "Send crash reports to the developers.", "desktop"),
                ("DISCORD STATUS:", "discord.presence", [("FULL", "full"), ("MINIMAL", "minimal"), ("OFF", "off")],
                 "Choose what your Discord status shows.", "desktop"),
                ("DISCORD SERVER NAME:", "discord.show_server_name", [("SHOW", "true"), ("HIDE", "false")],
                 "Show the game's name on Discord.", "desktop"),
                ("DISCORD JOIN BUTTON:", "discord.show_join", ON_OFF,
                 "Show a Join button on Discord while hosting.", "desktop"),
            ]),
        ],
    },
}

# the Controls screen's device settings (CONTROLS_SCREEN): the mouse's
# (config.toml's), and the controller's (the profile's; its button layout is
# the BUTTON LAYOUT row of Controller Buttons' list, CONTROLLER_PRESETS)
MOUSE_ROWS = [
    ("HORIZONTAL SENSITIVITY:", "input.mouse_sensitivity", SENSITIVITIES,
     "Adjust the horizontal look sensitivity.", None),
    ("VERTICAL SENSITIVITY:", "input.mouse_vertical_sensitivity", [("SAME", "0")] + SENSITIVITIES,
     "Adjust the vertical look sensitivity.", None),
    ("INVERT VERTICAL AXIS:", "input.invert_mouse", YES_NO,
     "Moving the mouse forward looks down.", None),
    ("AIM ASSIST:", "input.mouse_aim_assist", ON_OFF,
     "Use controller aim assist with the mouse.", None),
]
GAMEPAD_ROWS = [
    ("LOOK SENSITIVITY:", "profile.look_sensitivity", [(str(step), str(step)) for step in range(1, 11)],
     "Adjust the look sensitivity.", None),
    ("INVERT LOOK:", "profile.invert_look", YES_NO, "Pushing the right stick up looks down.", None),
    ("INVERT FLIGHT:", "profile.flight_inversion", YES_NO,
     "Pushing up flies down in flying vehicles.", None),
    ("AUTO-CENTER LOOK:", "profile.autocenter", YES_NO,
     "Level the view while walking.", None),
    ("STICK LAYOUT:", "profile.joystick_preset",
     [("DEFAULT", "0"), ("SOUTHPAW", "1"), ("LEGACY", "2"), ("LEGACY SOUTHPAW", "3")],
     "Change the thumbstick layout.", None),
    ("VIBRATION:", "profile.vibration", ON_OFF, "Rumble the controller.", None),
    ("IN-GAME HELP:", "profile.ingame_help", ON_OFF, "Show control hints during play.", None),
]

# Controls Setup: the keyboard and mouse's actions, in groups (the order of
# port/linux/game/menu_functions.c's table of them), then the mouse's
# settings (MOUSE_ROWS) in the same list
CONTROL_GROUPS = ["MOVEMENT", "WEAPONS", "ACTIONS", "MOUSE"]
CONTROL_ROWS = 7

# the profile menu's words for what its items now open
STRING_OVERRIDES = {
    f"{PE}/profile_edit_descriptions": [
        "Rename this profile.\\n\\n\\nProfile:",
        "Change the current profile's\\nfree-for-all multiplayer color.\\n\\nProfile:",
        "Change the keyboard, mouse\\nand controller settings.\\n\\nProfile:",
        "Adjust the video and\\ninterface settings.\\n\\nProfile:",
        "Adjust the audio settings.\\n\\n\\nProfile:",
        "Adjust the network, chat\\nand privacy settings.\\n\\nProfile:",
        "About Chupathingy.\\n\\n\\nProfile:",
    ],
}


def attributes(pairs: list) -> str:
    return "".join(f" {key}={quoteattr(str(value))}" for key, value in pairs if value is not None)


def _widget(name: str, pairs: list, inner: list) -> list:
    return [f"\t<widget{attributes([('name', name)] + pairs)}>", *[f"\t\t{line}" for line in inner], "\t</widget>"]


def _strings(name: str, texts: list) -> list:
    return [f"\t<strings{attributes([('name', name)])}>",
            *[f"\t\t<string{attributes([('text', text)])}/>" for text in texts], "\t</strings>"]


def _button(name: str, caption: int, handlers: list) -> list:
    pairs = [("type", "text"), ("width", 128), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
             ("string_list", "strings/common_button_captions"), ("string_index", caption), ("font", "ui\\small_ui"),
             ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)]
    return _widget(name, pairs, handlers + ['<on event="left_mouse" run="mouse emit accept event"/>'])


def _screen(folder: str, spec: dict, rows: list, list_inputs: list, list_handlers: list, extra: list) -> list:
    """a screen: its header, the list of rows (with the help line as its
    description), and the buttons"""
    base = folder if folder.startswith("main_menu/") else f"{PE}/{folder}"
    header, header_bitmap = spec["header"]
    lines = []
    lines += _widget(f"{base}/{spec['screen']}",
                     [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child pause_game"),
                      ("bitmap", "bitmaps/gradient")],
                     [*_back_handlers(spec),
                      f'<child{attributes([("widget", f"{base}/{header}")])}/>',
                      f'<child{attributes([("widget", f"{base}/options_menu")])}/>'])
    lines += _widget(f"{base}/{header}", [("controller", 1), ("left", 35), ("top", 11), ("width", 605), ("height", 59),
                                          ("bitmap", header_bitmap)], [])
    # (a help of its own text, set by its screen's function: help_text)
    lines += _widget(f"{base}/help", [("type", "text"), ("controller", 1), ("left", 68),
                                      ("top", spec.get("help_top", 350)), ("width", 482), ("height", 60),
                                      ("string_list", None if spec.get("help_text") else f"{base}/help_strings"),
                                      ("font", "ui\\large_ui"), ("color", "#FFFFFFFF")], [])
    children = [f'<data input="{name}"/>' for name in list_inputs] + list_handlers
    # (each row: its widget, platform, and the place it is in, else the next)
    for index, (row, platform, *place) in enumerate(rows):
        y = 73 + (place[0] if place else index) * spec["spacing"]
        children.append(f'<child{attributes([("widget", row), ("x", 54), ("y", y), ("platform", platform)])}/>')
    children.append(f'<child{attributes([("widget", f"{base}/button_bar"), ("y", 414)])}/>')
    lines += _widget(f"{base}/options_menu",
                     [("type", "column_list"), ("width", 640), ("height", 480),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                      ("description", f"{base}/help")], children)
    buttons = [f'<child{attributes([("widget", f"{base}/button_defaults"), ("y", 1)])}/>']
    buttons += [f'<child{attributes([("widget", f"{base}/button_ok"), ("x", 380), ("y", 1)])}/>',
                '<child widget="common_button_cancel" x="510" y="1"/>']
    lines += _widget(f"{base}/button_bar",
                     [("type", "column_list"), ("width", 640), ("height", 28),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")], buttons)
    lines += extra
    return lines


def _back_handlers(spec: dict) -> list:
    """a screen's B and Back: back, or with VIEWS out of the nested list
    first (menu_functions.c's "port settings back")"""
    if spec.get("views"):
        return ['<on event="b" run="port settings back"/>', '<on event="back" run="port settings back"/>']
    return ['<on event="b" back="true"/>', '<on event="back" back="true"/>']


# A screen's VIEWS ("views": (name, label, help, rows)) are nested lists in
# the same screen: a row of its list (op_link_<name>, its label alone) opens
# one, and B goes back out of it (port/linux/game/menu_functions.c's "port
# settings view" and "port settings back"). Each list's rows follow a mark,
# an empty widget named view_<name> (view_main for the screen's own rows);
# rows before any mark are in every list, and a mark of two names
# (view_gamepad_buttons) is in both. menu_functions.c shows the rows of the
# list open, in the places their list gives them, and hides the rest.


def _row_key(row: tuple) -> str:
    label, setting, choices, _, platform, *named = row
    return named[0] if named else setting.split(".", 1)[1]


def _layout(spec: dict) -> list:
    """the list's children in order: ("mark", view), ("row", row, key,
    place, label index) or ("link", view, key, place, label index); and the
    labels and helps by label index"""
    entries, labels, helps = [], [], []
    same = spec.get("same_place", ())

    def add(rows: list) -> int:
        place = -1
        for row in rows:
            key = _row_key(row)
            if row[1] not in same and key not in same:
                place += 1
            entries.append(("row", row, key, place, len(labels)))
            labels.append(row[0])
            helps.append(row[3])
        return place

    views = spec.get("views", [])
    if views:
        entries.append(("mark", "main"))
    place = add(spec["rows"])
    for view, label, help_text, _ in views:
        place += 1
        entries.append(("link", view, f"link_{view}", place, len(labels)))
        labels.append(label)
        helps.append(help_text)
    for view, _, _, rows in views:
        entries.append(("mark", view))
        add(rows)
    return entries, labels, helps


def _setting_row(base: str, row: tuple, key: str, label_index: int) -> list:
    """a setting's row: its label and its spinner"""
    label, setting, choices, _, platform, *_ = row
    lines = _widget(f"{base}/op_{key}", [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                         ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF"),
                                         ("platform", platform)],
                    [f'<child{attributes([("widget", f"{base}/{key}_label")])}/>',
                     f'<child{attributes([("widget", f"{base}/{key}_spinner"), ("x", 320), ("y", 1)])}/>'])
    lines += _widget(f"{base}/{key}_label",
                     [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                      ("string_list", f"{base}/labels"), ("string_index", label_index), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    lines += _widget(f"{base}/{key}_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 147), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "|".join(shown for shown, _ in choices)), ("setting", setting),
                      ("values", "|".join(value for _, value in choices)), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 150 19 156")],
                     ['<on event="created" run="port setting load"/>'])
    return lines


def _link_row(base: str, key: str, label_index: int) -> list:
    """a row that opens a nested list (VIEWS): its label alone"""
    lines = _widget(f"{base}/op_{key}", [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                         ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                    ['<on event="a" run="port settings view"/>', '<on event="start" run="port settings view"/>',
                     '<on event="left_mouse" run="mouse emit accept event"/>',
                     f'<child{attributes([("widget", f"{base}/{key}_label")])}/>'])
    lines += _widget(f"{base}/{key}_label",
                     [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                      ("string_list", f"{base}/labels"), ("string_index", label_index), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    return lines


def _mark(base: str, view: str) -> list:
    return _widget(f"{base}/view_{view}", [("width", 1), ("height", 1)], [])


def _setting_rows(base: str, spec: dict) -> tuple:
    """a settings screen's list children ((widget, platform, place)) and
    the widgets of its rows, labels and helps"""
    entries, labels, helps = _layout(spec)
    rows, extra = [], []
    for entry in entries:
        if entry[0] == "mark":
            rows.append((f"{base}/view_{entry[1]}", None, 0))
            extra += _mark(base, entry[1])
        elif entry[0] == "link":
            _, _, key, place, label_index = entry
            rows.append((f"{base}/op_{key}", None, place))
            extra += _link_row(base, key, label_index)
        else:
            _, row, key, place, label_index = entry
            rows.append((f"{base}/op_{key}", row[4], place))
            extra += _setting_row(base, row, key, label_index)
    # (the help of the row whose label is string n is n + 1: the buttons' is 0)
    strings = _strings(f"{base}/labels", labels)
    strings += _strings(f"{base}/help_strings", [""] + [text.replace("\n", "\\n") for text in helps])
    return rows, extra, strings


def _setting_screen(folder: str, spec: dict) -> list:
    base = folder if folder.startswith("main_menu/") else f"{PE}/{folder}"
    rows, extra, strings = _setting_rows(base, spec)
    extra += _button(f"{base}/button_defaults", 3, ['<on event="a" run="port settings defaults"/>',
                                                    '<on event="start" run="port settings defaults"/>'])
    extra += _button(f"{base}/button_ok", 1, ['<on event="a" run="port settings save" back="true"/>',
                                              '<on event="start" run="port settings save" back="true"/>'])
    handlers = ['<on event="created" run="port settings views init"/>'] if spec.get("views") else []
    return _screen(folder, spec, rows, ["port settings help"], handlers, extra + strings)


# Controls Setup (CONTROLS_SCREEN): every device's controls in one list,
# chosen by its first row (DEVICE), as Halo PC's Controls Setup chose a
# device. The keyboard and mouse: SHOW chooses a group of the keys
# (menu_functions.c's controls[]) or the mouse's settings (MOUSE_ROWS). The
# controller: SHOW chooses its settings (the button layout, then
# GAMEPAD_ROWS) or its buttons: each action's button, which A sets
# ("controller begin binding": the next button pressed, swapped with the
# action that had it, confirmed with A), making the layout Custom
# (config.toml's controls.controller_layout). Its rows are VIEWS' marks'
# lists, by the two SHOWs (menu_functions.c's "port controls update", which
# writes the help line too, by the controller's own names for its buttons).
CONTROLS_SCREEN = "controls_setup"
CONTROLS_DEVICES = ["KEYBOARD/MOUSE", "GAMEPAD"]
CONTROLLER_PAGES = ["SETTINGS", "BUTTONS"]
CONTROLLER_PRESETS = ["DEFAULT", "SWAP TRIGGERS", "SWAP A, L TRIGGER", "SWAP B, L TRIGGER", "SWAP B, R STICK",
                      "BUMPER JUMPER", "CUSTOM"]
# (port/linux/game/menu_functions.c's button_row_actions, in order)
CONTROLLER_ACTIONS = ["FIRE:", "GRENADE:", "MELEE:", "JUMP:", "ACTION / RELOAD:", "SWITCH WEAPON:",
                      "SWITCH GRENADE:", "FLASHLIGHT:", "CROUCH:", "ZOOM:"]
CONTROLS_SPEC = {"screen": "controls_settings_screen", "spacing": 24, "help_top": 368, "help_text": True,
                 "header": ("header_profile_controls_settings",
                            f"{PE}/controls_setup/header_profile_controls_settings")}


def _chooser(base: str, key: str, label: str, values: list) -> list:
    """a row of a label and a wide spinner of no setting (DEVICE, SHOW)"""
    lines = _widget(f"{base}/op_{key}", [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                         ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                    [f'<child{attributes([("widget", f"{base}/{key}_label")])}/>',
                     f'<child{attributes([("widget", f"{base}/{key}_spinner"), ("x", 200), ("y", 1)])}/>'])
    lines += _widget(f"{base}/{key}_label",
                     [("type", "text"), ("controller", 1), ("width", 200), ("height", 22), ("text", label),
                      ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    lines += _widget(f"{base}/{key}_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 297), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "|".join(values)), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                      ("align", "center"), ("text_y", 1), ("header_bitmap", "bitmaps/arrow_sm_left"),
                      ("footer_bitmap", "bitmaps/arrow_sm_right"), ("header_bounds", "7 -6 19 0"),
                      ("footer_bounds", "7 300 19 306")], [])
    return lines


def _controls_rows() -> tuple:
    """Controls Setup's list children ((widget, platform, place)) and the
    widgets of its rows"""
    base = f"{PE}/{CONTROLS_SCREEN}"
    rows, extra, labels, helps = [], [], [], []

    def mark(view: str):
        nonlocal extra
        rows.append((f"{base}/view_{view}", None, 0))
        extra += _mark(base, view)

    def settings(setting_rows: list, first: int):
        nonlocal extra
        for place, row in enumerate(setting_rows, first):
            key = _row_key(row)
            rows.append((f"{base}/op_{key}", row[4], place))
            extra += _setting_row(base, row, key, len(labels))
            labels.append(row[0])
            helps.append(row[3])

    rows.append((f"{base}/op_device", None, 0))
    extra += _chooser(base, "device", "DEVICE:", CONTROLS_DEVICES)
    # the keyboard and mouse's
    mark("keyboard_mouse")
    rows.append((f"{base}/op_group", None, 1))
    extra += _chooser(base, "group", "SHOW:", CONTROL_GROUPS)
    mark("keyboard")
    for index in range(CONTROL_ROWS):
        row = f"{base}/op_command_{index + 1}"
        rows.append((row, None, index + 2))
        extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         ['<on event="a" run="controls begin binding"/>',
                          '<on event="start" run="controls begin binding"/>',
                          '<on event="left right" run="controls binding slot"/>',
                          f'<child{attributes([("widget", f"{base}/command_label")])}/>',
                          f'<child{attributes([("widget", f"{base}/command_binding"), ("x", 200)])}/>',
                          f'<child{attributes([("widget", f"{base}/command_binding"), ("x", 356)])}/>'])
    extra += _widget(f"{base}/command_label",
                     [("type", "text"), ("controller", 1), ("width", 200), ("height", 22), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/command_binding",
                     [("type", "text"), ("controller", 1), ("width", 152), ("height", 22), ("font", "ui\\small_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 5), ("text_flags", "no_focus_test")],
                     [])
    mark("mouse")
    settings(MOUSE_ROWS, 2)
    # the controller's
    mark("gamepad_buttons")
    rows.append((f"{base}/op_page", None, 1))
    extra += _chooser(base, "page", "SHOW:", CONTROLLER_PAGES)
    mark("gamepad")
    rows.append((f"{base}/op_layout", None, 2))
    extra += _widget(f"{base}/op_layout", [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                           ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child{attributes([("widget", f"{base}/layout_label")])}/>',
                      f'<child{attributes([("widget", f"{base}/layout_spinner"), ("x", 320), ("y", 1)])}/>'])
    extra += _widget(f"{base}/layout_label",
                     [("type", "text"), ("controller", 1), ("width", 300), ("height", 22), ("text", "BUTTON LAYOUT:"),
                      ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/layout_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 147), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "|".join(CONTROLLER_PRESETS)), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                      ("align", "center"), ("text_y", 1), ("header_bitmap", "bitmaps/arrow_sm_left"),
                      ("footer_bitmap", "bitmaps/arrow_sm_right"), ("header_bounds", "7 -6 19 0"),
                      ("footer_bounds", "7 150 19 156")], [])
    settings(GAMEPAD_ROWS, 3)
    mark("buttons")
    for index, label in enumerate(CONTROLLER_ACTIONS):
        rows.append((f"{base}/op_button_{index + 1}", None, index + 2))
        extra += _widget(f"{base}/op_button_{index + 1}",
                         [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                          ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         ['<on event="a" run="controller begin binding"/>',
                          '<on event="start" run="controller begin binding"/>',
                          f'<child{attributes([("widget", f"{base}/button_label_{index + 1}")])}/>',
                          f'<child{attributes([("widget", f"{base}/button_value"), ("x", 323), ("y", 1)])}/>'])
        extra += _widget(f"{base}/button_label_{index + 1}",
                         [("type", "text"), ("controller", 1), ("width", 300), ("height", 22), ("text", label),
                          ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/button_value",
                     [("type", "text"), ("controller", 1), ("width", 147), ("height", 22), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 3), ("text_flags", "no_focus_test")], [])
    # (the settings' labels and helps, by label as the settings screens';
    # the other rows' helps are menu_functions.c's own)
    extra += _strings(f"{base}/labels", labels)
    extra += _strings(f"{base}/help_strings", [""] + helps)
    return rows, extra


# (the list's: its rows filled in and the help written each frame; on its
# way out, the profile it began editing, if it did, let go of)
CONTROLS_LIST_INPUTS = ["port controls update"]
CONTROLS_LIST_HANDLERS = ['<on event="created" run="port controls init"/>',
                          '<on event="deleted" run="port controls closed"/>']


def _controls_screen() -> list:
    base = f"{PE}/{CONTROLS_SCREEN}"
    rows, extra = _controls_rows()
    extra += _button(f"{base}/button_defaults", 3, ['<on event="a" run="port controls defaults"/>',
                                                    '<on event="start" run="port controls defaults"/>'])
    extra += _button(f"{base}/button_ok", 1, ['<on event="a" run="port controls accept" back="true"/>',
                                              '<on event="start" run="port controls accept" back="true"/>'])
    return _screen(CONTROLS_SCREEN, CONTROLS_SPEC, rows, CONTROLS_LIST_INPUTS, CONTROLS_LIST_HANDLERS, extra)


# the profile menu's rows (the PC style's) that open DISPLAY's and ONLINE's
# screens, laid out as the profile menu is: each the row's string of
# profile_edit_options, its top, and the screen's folder (CONTROLS' row is
# the PC version's own: WIDGET_PATCHES)
PROFILE_ROWS = {
    "display": (13, 177, "video_settings"),
    "online": (15, 243, "network_setup"),
}


def _profile_row(name: str) -> list:
    string_index, top, folder = PROFILE_ROWS[name]
    screen = f"{PE}/{folder}/{SCREENS[folder]['screen']}"
    return _widget(f"{PE}/{name}_profile_item",
                   [("type", "text"), ("left", 51), ("top", top), ("width", 232), ("height", 32),
                    ("bitmap", "bitmaps/list_item_bkd"), ("string_list", f"{PE}/profile_edit_options"),
                    ("string_index", string_index), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                    ("text_x", 13), ("text_y", 5)],
                   [f'<on event="a" open="{screen}"/>', f'<on event="start" open="{screen}"/>',
                    '<on event="left_mouse" run="mouse emit accept event"/>'])


def settings_files() -> dict:
    """the files of the port's screens, by their names in ce/"""
    files = {}
    for folder, spec in SCREENS.items():
        files[f"{PE}/{folder}".replace("/", ".") + ".xml"] = _setting_screen(folder, spec)
    for name, (_, _, folder) in PROFILE_ROWS.items():
        files[f"{PE}/{folder}".replace("/", ".") + ".xml"] += _profile_row(name)
    files[f"{PE}/{CONTROLS_SCREEN}".replace("/", ".") + ".xml"] = _controls_screen()
    return {name: ['<?xml version="1.0" encoding="UTF-8"?>',
                   "<!-- The port's settings screen, in the PC version's style (tools/port_settings.py) -->",
                   "<menus>", *lines, "</menus>", ""] for name, lines in files.items()}


# ---------- the Chupathingy menus' settings pages

# (display.menus "chupathingy": port/assets/menus/chupathingy) the same rows
# and help as the screens above, laid out as the Xbox's ADVANCED CONTROLS:
# no button bar, the Xbox's prompts instead (B cancels, A accepts, Y shows the
# defaults; Controls Setup's A changes a key or button, so X accepts there)
CHUPATHINGY = "chupathingy/settings"
# (every page SCREENS has, so that a page added there is here too)
CHUPATHINGY_PAGES = [CONTROLS_SCREEN, *SCREENS]


# the Chupathingy menus' own pages, which the PC version's menus have not:
# Delta Profile's PRIVACY (chupathingy/delta_profile.xml opens it;
# port/linux/game/profile_screen.c has the same rows for the other menus).
# A "site." row is the profile's own setting on the site, which
# menu_functions.c reads from and writes to it
DELTA_PRIVACY = "delta_privacy_setup"
CHUPATHINGY_ONLY = {
    DELTA_PRIVACY: {
        "screen": "delta_privacy_settings_screen",
        "header": ("header_privacy", "chupathingy/delta_profile/header_privacy"),
        "spacing": 30,
        "rows": [
            ("SHARE MY PROFILE:", "network.share_profile", ON_OFF,
             "Show your Delta name to Chupathingy players.", None),
            ("RECORD GAMES I JOIN:", "network.report_joined_games", ON_OFF,
             "Report the games you join.", None),
            ("RECORD GAMES I HOST:", "network.report_hosted_games", ON_OFF,
             "Report the games you host.", None),
            ("DELTA STATS DETAIL:", "network.report_events", ON_OFF,
             "Send kills, weapons and medals as host.", None),
            ("HIDE MY RECORD:", "site.hide_stats", [("NO", "false"), ("YES", "true")],
             "Hide your record from other players.", None),
            ("HIDE MY HEATMAPS:", "site.hide_heatmaps", [("NO", "false"), ("YES", "true")],
             "Hide your heatmaps from other players.", None),
            ("SHOW CLAN TAG:", "site.show_clan_tag", ON_OFF,
             "Show your clan tag to Chupathingy players.", None),
            ("DISCORD STATUS:", "discord.presence", [("FULL", "full"), ("MINIMAL", "minimal"), ("OFF", "off")],
             "What Discord shows while you play.", "desktop"),
            ("SHOW PROFILE NAMES:", "display.profile_names", ON_OFF,
             "Show players by their Delta names.", None),
        ],
    },
}


def _xbox_prompt(button: str, x: int, word: str) -> list:
    """a prompt laid out as the Xbox's: its button, then its word (the
    menus' own, chupathingy/prompts.xml, which a game's map has too)"""
    return [f'<child{attributes([("widget", f"chupathingy/prompts/{button}"), ("x", x), ("y", 414)])}/>',
            f'<child{attributes([("widget", f"chupathingy/prompts/{word}"), ("x", x + 22), ("y", 414)])}/>']


def chupathingy_settings_pages() -> list:
    """the pages' file (chupathingy/settings_pages.xml)"""
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             "<!-- The Chupathingy menus' settings pages, in the Xbox's look (tools/port_settings.py) -->",
             "<menus>"]
    for folder in [*CHUPATHINGY_PAGES, *CHUPATHINGY_ONLY]:
        base = f"{PE}/{folder}"
        back = ['<on event="b" back="true"/>', '<on event="back" back="true"/>']
        if folder == CONTROLS_SCREEN:
            spec = CONTROLS_SPEC
            rows, _ = _controls_rows()
            inputs = CONTROLS_LIST_INPUTS
            handlers = [*CONTROLS_LIST_HANDLERS, '<on event="x" run="port controls accept" back="true"/>',
                        '<on event="y" run="port controls defaults"/>']
            prompts = (_xbox_prompt("y", 60, "defaults") + _xbox_prompt("x", 215, "accept") +
                       _xbox_prompt("b", 350, "cancel") + _xbox_prompt("a", 490, "change"))
        else:
            spec = SCREENS.get(folder) or CHUPATHINGY_ONLY[folder]
            rows, _, _ = _setting_rows(base, spec)
            inputs = ["port settings help"]
            prompts = _xbox_prompt("y", 60, "defaults") + _xbox_prompt("a", 490, "accept")
            if spec.get("views"):
                # (A on a row that opens a nested list opens it, else
                # accepts; B goes back out of a nested list, BACK shown
                # there in CANCEL's place: menu_functions.c)
                handlers = ['<on event="created" run="port settings views init"/>',
                            '<on event="a" run="port settings accept"/>',
                            '<on event="y" run="port settings defaults"/>']
                back = _back_handlers(spec)
                prompts += _xbox_prompt("b", 360, "cancel") + _xbox_prompt("b", 360, "back")[1:]
            else:
                handlers = ['<on event="a" run="port settings save" back="true"/>',
                            '<on event="y" run="port settings defaults"/>']
                prompts += _xbox_prompt("b", 360, "cancel")
        header = spec["header"][0]
        lines += _widget(f"{CHUPATHINGY}/{folder}_screen",
                         [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child pause_game"),
                          ("bitmap", "bitmaps/gradient")],
                         [*back,
                          f'<child{attributes([("widget", f"{base}/{header}")])}/>',
                          f'<child{attributes([("widget", f"{CHUPATHINGY}/{folder}_list")])}/>', *prompts])
        children = [f'<data input="{name}"/>' for name in inputs] + handlers
        for row, platform, place in rows:
            children.append(f'<child{attributes([("widget", row), ("x", 54), ("y", 73 + place * spec["spacing"]), ("platform", platform)])}/>')
        lines += _widget(f"{CHUPATHINGY}/{folder}_list",
                         [("type", "column_list"), ("width", 640), ("height", 480),
                          ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                          ("description", f"{base}/help")], children)
    # (the rows, labels and help of the pages the PC version's menus have not)
    for folder, spec in CHUPATHINGY_ONLY.items():
        lines += _setting_screen(folder, spec)
    return lines + ["</menus>", ""]


def replaced(folder: str) -> bool:
    """whether the PC version's folder of widgets is replaced by ours (and
    those below it: the controls' advanced screen, the video test). Mouse
    Setup's and Gamepads' are Controls Setup's now"""
    return any(folder == path or folder.startswith(f"{path}/")
               for path in [*(f"{PE}/{name}" for name in [*SCREENS, CONTROLS_SCREEN, "mouse_settings",
                                                          "gamepad_setup"]),
                            *REPLACED_FOLDERS])


# ---------- multiplayer (PLAN.md section 11)

MT = "main_menu/multiplayer_type_select"

# strings added to the PC version's lists: (at, strings) of each list. Slayer's
# kills to win goes up to 500, for big games (the Xbox editor's are
# source/interface/ui_widget.c's kills_to_win_extra_strings): the values,
# and their helps after the five of its own (its helps are the rows' values'
# in turn: menu_functions.c's gametype_option_help)
SLAYER_EDIT = "main_menu/settings_select/multiplayer_setup/playlist_edit/slayer_edit"
STRING_INSERTS = {
    f"{PE}/profile_edit_options": [(10, ["PRIVACY", "INTERFACE", "CONTROLS", "DISPLAY", "AUDIO", "ONLINE"])],
    f"{SLAYER_EDIT}/var_kills_to_win": [(5, ["75", "100", "150", "200", "250", "500"])],
    f"{SLAYER_EDIT}/cap_slayer": [(11, [
        "Seventy-five kills to win.",
        "A hundred kills to win.",
        "A hundred and fifty kills to win.",
        "Two hundred kills to win.",
        "Two hundred and fifty kills to win.",
        "Five hundred kills to win.",
    ])],
}


# a custom loadout's weapons (game_engine.h's _loadout_weapon_*)
LOADOUT_WEAPONS = ["NONE", "RANDOM", "ASSAULT RIFLE", "PISTOL", "SHOTGUN", "SNIPER RIFLE", "ROCKET LAUNCHER",
                   "PLASMA PISTOL", "PLASMA RIFLE", "NEEDLER"]


def LOADOUT_HELPS(slot: str) -> list:
    """the helps of a custom loadout's weapon's values"""
    helps = [f"No {slot} weapon.", f"A random {slot} weapon each time you spawn."]
    for weapon in LOADOUT_WEAPONS[2:]:
        article = "an" if weapon[0] in "AEIOU" else "a"
        helps.append(f"Everyone spawns with {article} {weapon.lower()} as their {slot}\\nweapon.")
    return helps


STRING_OVERRIDES.update({
    f"{MT}/multiplayer_options": ["JOIN GAME", "CREATE GAME", "INTERNET", "LAN", "DIRECT LINK", "EDIT GAMETYPES",
                                  "SERVER BROWSER", "CO-OP CAMPAIGN"],
    f"{MT}/multiplayer_option_descriptions": [
        "Browse the games on the\\nInternet.",
        "Join a multiplayer game on\\nyour LAN.",
        "Join a game by its invite\\nlink.",
        "Host a game on the\\nInternet.",
        "Host a game on your LAN\\nonly.",
        "Play the campaign with a\\nfriend in split screen.",
        "Set all the attributes for your \\nmultiplayer gametypes and\\nkeep them for future use.",
    ],
    "main_menu/gametype_select/var_gametype_banks": ["STANDARD", "CUSTOM"],
    # (the PC's weapon sets, then the Xbox's NO GRENADES: menu_functions.c's
    # gametype_options maps them to the engine's)
    "main_menu/settings_select/multiplayer_setup/item_options_edit/item_options_labels": ["INFINITE GRENADES:", "WEAPON SET:", "VEHICLE SET:", "STARTING EQUIPMENT:",
                                  "VEHICLE RESPAWN TIME:", "LOADOUT:", "PRIMARY WEAPON:", "SECONDARY WEAPON:",
                                  "MAP WEAPONS:"],
    # (the helps of each option's values in turn: menu_functions.c's
    # gametype_option_help)
    "main_menu/settings_select/multiplayer_setup/item_options_edit/cap_item_options": [
        "Everyone has a limitless supply of grenades, which\\nmakes for loud, explosive and occasionally messy fun.",
        "Each player can only carry up to 4 grenades of each\\ntype at any given time.",
        "The map will contain whatever weapons the designers\\nplaced on it.",
        "All the weapons on the map will be replaced by\\npistols.",
        "All the weapons on the map will be replaced by\\nassault rifles and plasma rifles.",
        "All the weapons on the map will be replaced by\\nplasma weapons.",
        "The guns on the map will be replaced by weapons\\nwith sniper scopes.",
        "Sniper rifles and pistols will not appear on the map.",
        "All the weapons on the map will be replaced by\\nrocket launchers.",
        "All the weapons on the map will be replaced by\\nshotguns.",
        "All the weapons on the map are only effective at\\nshort ranges.",
        "There are no weapons on the map that were made in\\nCovenant sweat shops.",
        "Only weapons made by the Covenant will appear\\nin the map.",
        "The flamethrower and fuel rod gun will not appear\\nin the map.",
        "Only the most devastating and dangerous of weapons\\nwill appear in the map.",
        "The map's weapons, without any grenades.",
        "You will start the game with the weapon set that the \\nmap designers custom tailored for it.",
        "Use the generic starting weapons across all maps.",
        "The map's weapons appear where its designers\\nplaced them.",
        "No weapons appear on the map.",
        "The weapons follow the weapon set above.",
        "Everyone starts with the primary and secondary\\nweapons below.",
        *LOADOUT_HELPS("primary"),
        *LOADOUT_HELPS("secondary"),
    ],
    "main_menu/settings_select/multiplayer_setup/item_options_edit/var_weapon_set": [
        "NORMAL", "PISTOLS", "RIFLES", "PLASMA WEAPONS", "SNIPER", "NO SNIPING", "ROCKET LAUNCHERS", "SHOTGUNS",
        "SHORT RANGE", "HUMAN", "COVENANT", "CLASSIC", "HEAVY WEAPONS", "NO GRENADES"],
})

# the map lists' first row: SINGLEPLAYER or MULTIPLAYER maps (_map_kind)
MAP_KIND_CHOOSER = "main_menu/new_select/list_item_0_map_kind"

# changes to the PC version's widgets (by our names): attributes set, all
# their handlers replaced, children added, game data inputs added
WIDGET_PATCHES = {
    # The host returns here after a round with no menu history. Back must
    # reopen the main menu when it closes the last screen, as the lobby does.
    f"{MT}/connected/connected_map_select_wrapper": {"set": {
        "flags": "pass_unhandled_to_focused_child pause_game main_menu_if_no_history tag_controller_index force_handle_mouse",
    }},
    # (the rows in the order Change Name, Change Color, Controls, Display,
    # Audio, Online, About, 33 apart as the PC version's: each in the place
    # of the PC version's row before it ("swap"; Controls Setup's row is
    # CONTROLS, Audio Setup's AUDIO, and DISPLAY and ONLINE are
    # PROFILE_ROWS'),
    # the last two places left out; offsets from the tops each row has)
    f"{PE}/profile_edit_select_list": {
        "swap": {f"{PE}/controller_setup_profile_item": f"{PE}/color_profile_item",
                 f"{PE}/gamepads_profile_item": f"{PE}/controller_setup_profile_item",
                 f"{PE}/mouse_settings_profile_item": f"{PE}/display_profile_item",
                 f"{PE}/video_settings_profile_item": f"{PE}/online_profile_item",
                 f"{PE}/network_settings_profile_item": f"{PE}/about_item"},
        "leave_out": [f"{PE}/color_profile_item", f"{PE}/about_item"],
        "offsets": {f"{PE}/controller_setup_profile_item": (0, -166), f"{PE}/gamepads_profile_item": (0, 33),
                    f"{PE}/network_settings_profile_item": (0, -34)},
    },
    f"{PE}/controller_setup_profile_item": {"set": {"string_index": 12}, "handlers": [
        f'<on event="a" open="{PE}/{CONTROLS_SCREEN}/{CONTROLS_SPEC["screen"]}"/>',
        f'<on event="start" open="{PE}/{CONTROLS_SCREEN}/{CONTROLS_SPEC["screen"]}"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    # (the PC version's GAMEPADS and MOUSE rows, left out of the list: their
    # screens are Controls Setup's now)
    **{f"{PE}/{row}_profile_item": {"handlers": [
        f'<on event="a" open="{PE}/{CONTROLS_SCREEN}/{CONTROLS_SPEC["screen"]}"/>',
        f'<on event="start" open="{PE}/{CONTROLS_SCREEN}/{CONTROLS_SPEC["screen"]}"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]} for row in ("gamepads", "mouse_settings")},
    f"{PE}/audio_settings_profile_item": {"set": {"string_index": 14}},
    # (the profile settings' picture: on CONTROLS' row, the profile's
    # button settings, BITMAP_FRAMES; menu_functions.c's
    # profile_gamepad_layout)
    f"{PE}/profile_edit_extended_desc_pic": {"inputs": ["port gamepad layout preview"]},
    # (straight to their screens: no "checking for updates" dialog, which
    # asked the PC version's servers)
    f"{MT}/multiplayer_type_join_internet_item": {"set": {"string_index": 6}, "handlers": [
        f'<on event="a" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        f'<on event="start" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    f"{MT}/multiplayer_type_create_internet_item": {"handlers": [
        '<on event="a" run="join controller to mp game"/>',
        f'<on event="a" run="mp type set mode" open="{MT}/connected/connected_map_select_wrapper"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    f"{MT}/multiplayer_type_join_direct_item": {"handlers": [
        f'<on event="a" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        f'<on event="start" run="mp type set mode" open="{MT}/join_game/join_game_screen"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    # (Co-op, after Create Game's: _coop)
    f"{MT}/multiplayer_type_select_list": {"insert_before": {
        f"{MT}/multiplayer_type_gametypes_item": [f'<child widget="{MT}/multiplayer_type_coop_item" y="309"/>'],
    }},
    # (the Map screen's and New Game's lists: SINGLEPLAYER or MULTIPLAYER
    # maps, the first row's chooser's (_map_kind); B on the Map screen goes
    # from a co-op level's difficulties back to the level, else out of the
    # screen: menu_functions.c's map list)
    f"{MT}/mp_map_select/mp_map_select_list_2": {"swap": {"main_menu/new_select/list_item_0": MAP_KIND_CHOOSER},
                                                 "handlers": [
        '<on event="created" run="mp level list initialize"/>',
        '<on event="deleted" run="mp level list dispose"/>',
        f'<on event="custom_activation" run="mp level select" open="{MT}/connected/gametype_select_screen_wrapper"/>',
        '<on event="b" run="port map list back"/>',
        '<on event="back" run="port map list back"/>',
    ]},
    "main_menu/solo_level_select/solo_level_select_list": {"swap": {"main_menu/new_select/list_item_0": MAP_KIND_CHOOSER}},
    # (each description shows the other kind's maps too: a campaign level's
    # picture, name and words on the Map screen, a multiplayer map's on New
    # Game's)
    f"{MT}/mp_map_select/mp_map_right_item": {"children": [
        '<child widget="main_menu/solo_level_select/replay_level_right_name"/>',
        '<child widget="main_menu/solo_level_select/replay_level_right_pic"/>',
        '<child widget="main_menu/solo_level_select/replay_level_right_data"/>',
    ]},
    "main_menu/solo_level_select/replay_level_right_item": {"children": [
        f'<child widget="{MT}/mp_map_select/mp_map_right_name"/>',
        f'<child widget="{MT}/mp_map_select/mp_map_right_pic"/>',
        f'<child widget="{MT}/mp_map_select/mp_map_right_data"/>',
    ]},
    f"{MT}/join_game/header_join_game": {"children": [
        f'<child widget="{MT}/join_game/header_server_browser"/>',
        f'<child widget="{MT}/join_game/header_direct_link"/>',
    ]},
    # (the small font's line does not fit below their 4 units of offset)
    f"{MT}/join_game/ticker_player_info": {"set": {"text_y": 1}},
    f"{MT}/join_game/ticker_rules_info": {"set": {"text_y": 1}},
    # (Item Options' loadout rows, over its buttons)
    "main_menu/settings_select/multiplayer_setup/item_options_edit/item_options_menu": {"insert_before": {
        "main_menu/settings_select/multiplayer_setup/item_options_edit/item_button_bar": [
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_map_weapons" x="54" y="163"/>',
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_loadout" x="54" y="193"/>',
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_primary_weapon" x="54" y="223"/>',
            f'<child widget="main_menu/settings_select/multiplayer_setup/item_options_edit/op_secondary_weapon" x="54" y="253"/>',
        ]}},
    # (the PC's Vehicles row's Start opened Item Options)
    "main_menu/settings_select/multiplayer_setup/playlist_edit/playlist_edit_vehicles_list_item": {"handlers": [
        '<on event="a" open="main_menu/settings_select/multiplayer_setup/vehicle_options_edit/vehicle_options_screen"/>',
        '<on event="start" open="main_menu/settings_select/multiplayer_setup/vehicle_options_edit/vehicle_options_screen"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
    # (Direct Link's clipboard button in the place of the PC version's
    # Refresh, and Refresh in Get List's: the Server Browser's only, which
    # asks the public games' hosts again; "swap" puts another widget in a
    # child's place)
    f"{MT}/join_game/join_game_button_bar": {"swap": {
        f"{MT}/join_game/join_game_button_refresh": f"{MT}/join_game/button_clipboard",
        f"{MT}/join_game/join_game_button_update": f"{MT}/join_game/join_game_button_refresh",
    }},
}

# frames added after a PC bitmap's (by our names): (the Xbox map's bitmap,
# its frame, the size drawn at, where in the widget). The profile settings'
# picture gets the Xbox's Controller Setup's pictures of its five button
# settings (its first four frames are the thumbstick settings'), which show
# 512 by 235 of their 512 by 512: drawn at 279 wide, in the middle of the
# picture's 279 by 202
XBOX_CONTROLLER_PICTURES = f"ui\\shell\\{PE.replace('/', chr(92))}\\controller_edit\\config_controller"
# the frames of a PC bitmap in another order (by our names): the frame (as
# the PC version numbers them) for each place. The profile settings' picture
# numbers its frames as the list its rows (CONTROLS shows Gamepads' picture,
# the controller; DISPLAY Video Setup's, ONLINE Network Setup's)
FRAME_ORDERS = {
    f"{PE}/profile_options": [0, 7, 2, 5, 4, 6, 8],
}
BITMAP_FRAMES = {
    f"{PE}/profile_options": [(XBOX_CONTROLLER_PICTURES, 4 + preset, 279, 279, 0, 34) for preset in range(5)],
}

# the titles this port has that the PC version has not, set as its headers
# are (port/assets/menus/port_svg): bitmap name, text
TITLES = {
    f"{MT}/join_game/header_server_browser": "SERVER BROWSER",
    f"{MT}/join_game/header_direct_link": "DIRECT LINK",
    f"{MT}/join_game/header_password": "PASSWORD",
    f"{MT}/lobby/header_lobby": "GAME LOBBY",
    f"{MT}/coop/header_player_2": "PLAYER 2 PROFILE",
    f"{MT}/lobby/header_add_player": "ADD PLAYER",
    # (the Chupathingy menus' GAME SETUP and game type editor:
    # port/assets/menus/chupathingy/host_setup.xml; Server Setup's HOST
    # OPTIONS)
    f"{MT}/host_setup/header_game_setup": "GAME SETUP",
    f"{MT}/host_setup/header_edit_game_type": "EDIT GAME TYPE",
    f"{MT}/server_settings/header_host_options": "HOST OPTIONS",
}


def title_backdrop(width: float) -> str:
    """a header's dark backdrop, as the PC headers' redraws have it, for text
    width units wide (the text is set over it in OpenCE: ce_menus.py)"""
    return "\n".join([
        '<svg xmlns="http://www.w3.org/2000/svg" width="512" height="64" viewBox="0 0 512 64">',
        "  <defs>",
        '    <filter id="soft" filterUnits="userSpaceOnUse" x="-60" y="-60" width="632" height="184">',
        '      <feGaussianBlur stdDeviation="14.23 13.06"/>', "    </filter>",
        '    <mask id="cut" maskUnits="userSpaceOnUse" x="0" y="0" width="512" height="64">',
        '      <rect width="512" height="59" fill="#fff"/>', "    </mask>", "  </defs>",
        '  <g mask="url(#cut)">',
        f'    <rect x="29.08" y="28.05" width="{width + 28:.2f}" height="40.95" fill="#021931" fill-opacity="0.893" '
        'filter="url(#soft)"/>',
        "  </g>", "</svg>", ""])


# where the PC headers' text is (units of their 512x64), and its colour
TITLE_LEFT, TITLE_TOP, TITLE_CAP = 30.68, 33.5, 21.5
TITLE_COLOR = (0x29, 0x95, 0xFD, 255)


def _header(name: str, bitmap: str, platform=None) -> list:
    return _widget(name, [("controller", 1), ("left", 35), ("top", 11), ("width", 363), ("height", 59),
                          ("bitmap", bitmap), ("platform", platform)], [])


# a text field's B and Back do nothing of their own (the engine would go
# back from the field): its screen's (SCREEN_BACK) cancel the edit, else
# leave
FIELD_BACK = ['<on event="b"/>', '<on event="back"/>']
SCREEN_BACK = ['<on event="b" run="gamespy back handler"/>', '<on event="back" run="gamespy back handler"/>']


def _value_row(base: str, key: str, label_index: int, run: str) -> list:
    """a row of a label and a value the player sets (A edits it)"""
    lines = _widget(f"{base}/op_{key}", [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                         ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                    [f'<on event="a" run="{run}"/>', f'<on event="start" run="{run}"/>', *FIELD_BACK,
                     '<on event="left_mouse" run="mouse emit accept event"/>',
                     f'<child widget="{base}/{key}_label"/>', f'<child widget="{base}/{key}_value" x="230" y="1"/>'])
    lines += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 230), ("height", 22),
                                             ("string_list", f"{base}/labels"), ("string_index", label_index or None),
                                             ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                             ("text_y", 4)], [])
    lines += _widget(f"{base}/{key}_value", [("type", "text"), ("controller", 1), ("width", 280), ("height", 22),
                                             ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("align", "center"),
                                             ("text_y", 5), ("text_flags", "no_focus_test")], [])
    return lines


def _join_game_extras() -> list:
    """the browser's added titles, Direct Link's clipboard button, and the
    password screen"""
    base = f"{MT}/join_game"
    lines = _password_screen()
    lines += _header(f"{base}/header_server_browser", f"{base}/header_server_browser")
    lines += _header(f"{base}/header_direct_link", f"{base}/header_direct_link")
    lines += _widget(f"{base}/button_clipboard", [("type", "text"), ("width", 128), ("height", 24),
                                                  ("bitmap", "bitmaps/text_button_background"),
                                                  ("text", "PASTE LINK"), ("font", "ui\\small_ui"),
                                                  ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                     ['<on event="a" run="direct ip connect go"/>', '<on event="start" run="direct ip connect go"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    return lines


def _password_screen() -> list:
    """the server browser's password screen: a game with a password chosen
    (menu_functions.c's lobby_browser_select), its password typed (shown as
    stars), JOIN GAME joins it if it is the game's; else its help says so"""
    base = f"{MT}/join_game/password"
    spec = {"screen": "password_screen", "spacing": 28, "help_top": 120,
            "header": ("header_password", f"{MT}/join_game/header_password")}
    extra = _value_row(base, "password", 0, "port password edit")
    extra += _button(f"{base}/button_defaults", 3, [])
    extra += _widget(f"{base}/button_ok", [("type", "text"), ("width", 128), ("height", 24),
                                           ("bitmap", "bitmaps/text_button_background"), ("text", "JOIN GAME"),
                                           ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
                                           ("text_y", 2)],
                     ['<on event="a" run="port password join"/>', '<on event="start" run="port password join"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    extra += _strings(f"{base}/labels", ["PASSWORD:"])
    # (by its state: menu_functions.c's password_screen_update)
    extra += _strings(f"{base}/help_strings", [
        "",
        "Enter the game's password.",
        "Incorrect password.",
        "Could not join the game.",
    ])
    lines = _screen(base, spec, [(f"{base}/op_password", None)], ["port password update"],
                    ['<on event="created" run="port password init"/>'], extra)
    # (no Defaults: the bar is JOIN GAME and CANCEL; B goes back at once,
    # even while typing, which the screen starts with)
    lines = [line.replace(f'<child widget="{base}/button_defaults" y="1"/>', "") for line in lines]
    lines = [line.replace('<on event="b" back="true"/>', '<on event="b" run="port password back"/>')
             .replace('<on event="back" back="true"/>', '<on event="back" run="port password back"/>')
             for line in lines]
    return lines


def _server_settings() -> list:
    """Create Game's server settings (in the PC version's place): the game's
    name, the most players (up to the port's 128), its invite link, and
    whether the server browser lists it (PUBLIC or PRIVATE: an internet
    game's)"""
    base = f"{MT}/server_settings"
    # (eleven rows: closer together, the help lower)
    spec = {"screen": "server_settings_screen", "spacing": 24, "help_top": 366,
            "header": ("header_server_settings", f"{base}/header_server_settings")}
    rows, extra = [], []
    extra += _value_row(base, "server_name", 0, "ss edit server name")
    rows.append((f"{base}/op_server_name", None))
    extra += _widget(f"{base}/op_max_players", [("width", 512), ("height", 28),
                                                ("flags", "pass_unhandled_to_focused_child"),
                                                ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child widget="{base}/max_players_label"/>',
                      f'<child widget="{base}/max_players_spinner" x="320" y="1"/>'])
    extra += _widget(f"{base}/max_players_label", [("type", "text"), ("controller", 1), ("width", 300),
                                                   ("height", 22), ("string_list", f"{base}/labels"),
                                                   ("string_index", 1), ("font", "ui\\large_ui"),
                                                   ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/max_players_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 147), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "|".join(str(count) for count in MAXIMUM_PLAYERS)), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 150 19 156")], [])
    rows.append((f"{base}/op_max_players", None))
    extra += _value_row(base, "invite", 2, "ss copy invite")
    rows.append((f"{base}/op_invite", None))
    # (PUBLIC or PRIVATE: menu_functions.c's server_settings_update)
    extra += _widget(f"{base}/op_listing", [("width", 512), ("height", 28),
                                            ("flags", "pass_unhandled_to_focused_child"),
                                            ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child widget="{base}/listing_label"/>',
                      f'<child widget="{base}/listing_spinner" x="320" y="1"/>'])
    extra += _widget(f"{base}/listing_label", [("type", "text"), ("controller", 1), ("width", 300),
                                               ("height", 22), ("string_list", f"{base}/labels"),
                                               ("string_index", 9), ("font", "ui\\large_ui"),
                                               ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/listing_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 147), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "PUBLIC|PRIVATE"), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 150 19 156")],
                     ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    rows.append((f"{base}/op_listing", None))
    # PASSWORD (a public internet game's: menu_functions.c's server_settings_update),
    # which players from the server browser must type
    extra += _value_row(base, "password", 15, "ss edit server password")
    rows.append((f"{base}/op_password", None))
    # co-op's FRIENDLY FIRE (between its players), in the place of the
    # gametype's rows, which co-op hides as multiplayer hides it, laid out
    # as Teamplay Options' (menu_functions.c's server_settings_update); its
    # choice kept in network.coop_friendly_fire
    extra += _widget(f"{base}/op_friendly_fire", [("width", 512), ("height", 28),
                                                  ("flags", "pass_unhandled_to_focused_child"),
                                                  ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child widget="{base}/friendly_fire_label"/>',
                      f'<child widget="{base}/friendly_fire_spinner" x="286" y="1"/>'])
    extra += _widget(f"{base}/friendly_fire_label", [("type", "text"), ("controller", 1), ("width", 300),
                                                     ("height", 22), ("string_list", f"{base}/labels"),
                                                     ("string_index", 10), ("font", "ui\\large_ui"),
                                                     ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/friendly_fire_spinner",
                     [("type", "spinner"), ("top", 2), ("width", 206), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "OFF|ON|SHIELD ONLY|EXPLOSIVES ONLY"), ("setting", "network.coop_friendly_fire"),
                      ("values", "|".join(COOP_FRIENDLY_FIRE_VALUES)), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -13 19 -7"), ("footer_bounds", "7 208 19 214")],
                     ['<on event="created" run="port setting load"/>', '<on event="deleted" run="port setting save"/>',
                      '<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    rows.append((f"{base}/op_friendly_fire", None, 5))
    # ... and its EXTRA ENEMIES (port/linux/game/coop_enemies.c), below it:
    # NONE, PER PLAYER or STATIC MULTIPLIER, and below that the amount of the
    # one chosen (its own row, the other hidden: menu_functions.c's
    # server_settings_update); each kept in its network.coop_enemies setting
    def spinner_row(key, label_index, strings, setting, values):
        nonlocal extra
        extra += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                              ("flags", "pass_unhandled_to_focused_child"),
                                              ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<child widget="{base}/{key}_label"/>',
                          f'<child widget="{base}/{key}_spinner" x="286" y="1"/>'])
        extra += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 300),
                                                 ("height", 22), ("string_list", f"{base}/labels"),
                                                 ("string_index", label_index), ("font", "ui\\large_ui"),
                                                 ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        extra += _widget(f"{base}/{key}_spinner",
                         [("type", "spinner"), ("top", 2), ("width", 206), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("strings", "|".join(strings)), ("setting", setting),
                          ("values", "|".join(str(value) for value in values)), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("align", "center"), ("text_y", 4),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -13 19 -7"), ("footer_bounds", "7 208 19 214")],
                         ['<on event="created" run="port setting load"/>',
                          '<on event="deleted" run="port setting save"/>',
                          '<on event="left_mouse" run="mouse spinner 1wide click"/>'])

    spinner_row("extra_enemies", 11, ["NONE", "PER PLAYER", "STATIC MULTIPLIER"], "network.coop_enemies_mode",
                COOP_ENEMIES_MODES)
    rows.append((f"{base}/op_extra_enemies", None, 6))
    spinner_row("enemies_per_player", 12, [f"{value}%" for value in COOP_ENEMIES_PERCENTAGES],
                "network.coop_enemies", COOP_ENEMIES_PERCENTAGES)
    rows.append((f"{base}/op_enemies_per_player", None, 7))
    spinner_row("enemies_multiplier", 13, [f"{value}X" for value in COOP_ENEMIES_MULTIPLIERS],
                "network.coop_enemies_multiplier", COOP_ENEMIES_MULTIPLIERS)
    rows.append((f"{base}/op_enemies_multiplier", None, 7))
    # ... and its PLAYER COLLISIONS, below them (network.coop_player_collisions,
    # which the host sends its players as the game begins: menu_functions.c's
    # server_start)
    spinner_row("player_collisions", 14, ["ON", "OFF"], "network.coop_player_collisions", ["true", "false"])
    rows.append((f"{base}/op_player_collisions", None, 8))
    # the gametype's options for this game (the gametype editor's screens,
    # editing a copy of the gametype chosen: "port setup edit")
    for index, (key, screen) in enumerate(SETUP_OPTION_SCREENS):
        target = f"main_menu/settings_select/multiplayer_setup/{screen}"
        extra += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                             ("flags", "pass_unhandled_to_focused_child"),
                                             ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<on event="a" run="port setup edit" open="{target}"/>',
                          f'<on event="start" run="port setup edit" open="{target}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          f'<child widget="{base}/{key}_label"/>', f'<child widget="{base}/{key}_value" x="230" y="1"/>'])
        extra += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 230), ("height", 22),
                                                 ("string_list", f"{base}/labels"), ("string_index", 3 + index),
                                                 ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13),
                                                 ("text_y", 4)], [])
        extra += _widget(f"{base}/{key}_value", [("type", "text"), ("controller", 1), ("width", 280), ("height", 22),
                                                 ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("align", "center"),
                                                 ("text_y", 5), ("text_flags", "no_focus_test")], [])
        rows.append((f"{base}/op_{key}", None, 5 + index))
    # HOST OPTIONS: the host's rules of config.toml (HOST_OPTIONS' screen,
    # below), in co-op too
    target = f"{base}/host_options/{HOST_OPTIONS['screen']}"
    extra += _widget(f"{base}/op_host_options", [("width", 512), ("height", 28),
                                                ("flags", "pass_unhandled_to_focused_child"),
                                                ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<on event="a" open="{target}"/>', f'<on event="start" open="{target}"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>',
                      f'<child widget="{base}/host_options_label"/>'])
    extra += _widget(f"{base}/host_options_label", [("type", "text"), ("controller", 1), ("width", 300),
                                                    ("height", 22), ("string_list", f"{base}/labels"),
                                                    ("string_index", 16), ("font", "ui\\large_ui"),
                                                    ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    rows.append((f"{base}/op_host_options", None, 11))
    extra += _button(f"{base}/button_defaults", 3, [])
    extra += _widget(f"{base}/button_ok", [("type", "text"), ("width", 128), ("height", 24),
                                           ("bitmap", "bitmaps/text_button_background"), ("text", "START GAME"),
                                           ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"),
                                           ("text_y", 2)],
                     [f'<on event="a" run="ss start game" open="{MT}/lobby/lobby_screen"/>',
                      f'<on event="start" run="ss start game" open="{MT}/lobby/lobby_screen"/>',
                      '<on event="left_mouse" run="mouse emit accept event"/>'])
    extra += _strings(f"{base}/labels", ["GAME NAME:", "MAXIMUM PLAYERS:", "INVITE LINK:", "GAME TYPE:",
                                         "PLAYER OPTIONS:", "ITEM OPTIONS:", "VEHICLE OPTIONS:", "INDICATOR OPTIONS:",
                                         "TEAMPLAY OPTIONS:", "LISTING:", "FRIENDLY FIRE:", "EXTRA ENEMIES:",
                                         "PER PLAYER:", "MULTIPLIER:", "PLAYER COLLISIONS:", "PASSWORD:",
                                         "HOST OPTIONS"])
    extra += _strings(f"{base}/help_strings", [
        "",
        "The game's name in the game list.",
        "The most players allowed in the game.",
        "Press %a-button to copy the invite link.",
        "Choose the gametype.",
        "Lives, health, shields and respawning.",
        "Weapons, grenades and starting equipment.",
        "Vehicles and their respawn time.",
        "The motion tracker and nav points.",
        "Friendly fire and team balance.",
        # (LISTING's, by its choice)
        "Anyone can join from the Server Browser.",
        "Only players with your invite link can join.",
        # (FRIENDLY FIRE's, by its choice, as Teamplay Options' words them)
        "Players can not be hurt by weapons and explosives\\nfired by the other players.",
        "Players can be hurt by weapons or explosives\\nfired by the other players.",
        "Damage from the other players will only reduce\\nshields. Health will be unaffected.",
        "Players can be hurt by damage from explosives\\nfired by the other players.",
        # (EXTRA ENEMIES', by its choice: COOP_ENEMIES_MODES' order)
        "Normal enemy squads.",
        "Enemy squads grow with each player.",
        "Enemy squads grow by the multiplier below.",
        # (PER PLAYER's and MULTIPLIER's)
        "Extra enemies for each player past the first.",
        "Each enemy squad is this many times as large.",
        # (PLAYER COLLISIONS', by its choice)
        "Players bump into each other.",
        "Players walk through each other.",
        # (PASSWORD's)
        "Required to join from the Server Browser.",
        # (HOST OPTIONS')
        "Team switching, chat filter, aim assist and port.",
    ])
    lines = _screen(base, spec, rows, ["server settings update"],
                    ['<on event="created" run="server settings init"/>'], extra)
    # (no Defaults: the bar is START and CANCEL; B cancels the name's edit
    # first)
    lines = [line.replace(f'<child widget="{base}/button_defaults" y="1"/>', "") for line in lines]
    lines = [line.replace('<on event="b" back="true"/>', SCREEN_BACK[0])
             .replace('<on event="back" back="true"/>', SCREEN_BACK[1]) for line in lines]
    return lines


MAXIMUM_PLAYERS = [2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128]
# Server Setup's HOST OPTIONS: the host's rules of config.toml, as the
# Chupathingy menus' GAME SETUP has them (menu_functions.c), on a settings
# screen of their own
HOST_OPTIONS = {
    "screen": "host_options_screen",
    "header": ("header_host_options", f"{MT}/server_settings/header_host_options"),
    "spacing": 30,
    "rows": [
        ("TEAM SWITCHING:", "network.team_change", [("BALANCED", "balanced"), ("ALLOW", "allow"), ("OFF", "off")],
         "When players can change teams.",
         None),
        ("TEAM SWITCH MARGIN:", "network.team_change_margin", [(str(count), str(count)) for count in range(17)],
         "How uneven the teams can get when BALANCED.", None),
        ("CHAT FILTER:", "network.chat_filter", [("ON", "true"), ("OFF", "false")],
         "Block chat lines with blocked words.", None),
        ("MOUSE AIM ASSIST:", "network.block_mouse_aim_assist", [("ALLOWED", "false"), ("BLOCKED", "true")],
         "Allow or block aim assist for mouse players.",
         None),
        ("PORT:", "network.tunnel_port", [("AUTO", "0")] + [(str(port), str(port)) for port in range(2302, 2311)],
         "The UDP port for internet play. Applies on restart.",
         None),
    ],
}
# network.coop_friendly_fire's values, as Server Setup's FRIENDLY FIRE shows
# them (menu_functions.c's cooperative_friendly_fire_modes, in this order)
COOP_FRIENDLY_FIRE_VALUES = ["off", "on", "shields_only", "explosives_only"]
# Server Setup's EXTRA ENEMIES: network.coop_enemies_mode's values (in
# coop_enemies.c's order), and the amounts of PER PLAYER (network.coop_enemies,
# percentages) and STATIC MULTIPLIER (network.coop_enemies_multiplier)
COOP_ENEMIES_MODES = ["none", "per_player", "multiplier"]
COOP_ENEMIES_PERCENTAGES = [25, 50, 100, 150, 200]
COOP_ENEMIES_MULTIPLIERS = [2, 4, 8, 16, 32]
# Server Setup's rows of the gametype's options: the gametype editor's
# screens
SETUP_OPTION_SCREENS = [
    ("game_type", "playlist_edit/game_type_select/gametype_select_screen"),
    ("player_options", "player_options_edit/player_options_screen"),
    ("item_options", "item_options_edit/item_options_screen"),
    ("vehicle_options", "vehicle_options_edit/vehicle_options_screen"),
    ("indicator_options", "indicator_options_edit/indicator_options_screen"),
    ("team_options", "teamplay_options_edit/teamplay_options_screen"),
]


def _lobby() -> list:
    """the lobby the host's and the joining players wait in (the Xbox's
    pregame's functions, which need no widget of theirs): up to the port's
    128 players, scrolling; the game's map and gametype; the countdown"""
    base = f"{MT}/lobby"
    # (split screen: another controller's START joins it, its B leaves alone:
    # menu_functions.c's lobby_join. After a game it is the only screen,
    # network_game_reset_to_pregame_ui opening it in place of the Xbox's
    # pregame: leaving it goes to the main menu, as the PC version's postgame
    # screens do)
    lines = _widget(f"{base}/lobby_screen", [("width", 640), ("height", 480),
                                             ("flags", "pass_unhandled_to_focused_child main_menu_if_no_history"),
                                             ("bitmap", "bitmaps/gradient")],
                    ['<on event="created" run="port lobby open"/>',
                     '<on event="created" run="net server accept conx"/>',
                     '<on event="created" run="net server allow start"/>',
                     '<on event="b" run="port lobby leave" back="true"/>',
                     '<on event="back" run="port lobby leave" back="true"/>',
                     f'<on event="start" run="port lobby join" open="{base}/player_profile_screen" branch="true"/>',
                     '<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                     f'<child widget="{base}/lobby_list"/>',
                     f'<child widget="{base}/header_lobby"/>',
                     f'<child widget="{base}/lobby_join_help"/>'])
    lines += _header(f"{base}/header_lobby", f"{base}/header_lobby")
    lines += _widget(f"{base}/lobby_join_help", [("type", "text"), ("controller", 1), ("left", 355), ("top", 446),
                                                 ("width", 275), ("height", 24), ("font", "ui\\small_ui"),
                                                 ("color", "#FF2896FF"), ("align", "right"), ("text_y", 5),
                                                 ("text_flags", "no_focus_test")], [])
    # (its rows: any controller's left and right switch its own player's team,
    # on the rows only, so that they move along the buttons)
    rows = [f'<child widget="{base}/list_item_{index}" x="20" y="{73 + 30 * index}"/>' for index in range(11)]
    lines += _widget(f"{base}/lobby_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                            ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                            ("description", f"{base}/lobby_desc")],
                     ['<data input="net splitscreen prejoin players"/>', '<data input="port lobby update"/>',
                      *rows,
                      f'<child widget="{base}/lobby_button_bar" y="414"/>'])
    for index in range(11):
        lines += _widget(f"{base}/list_item_{index}",
                         [("width", 390), ("height", 28),
                          ("bitmap", "bitmaps/sel_list_item_bkd_top" if index == 0 else "bitmaps/sel_list_item_bkd"),
                          ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("align", "center"), ("text_y", 3)],
                         ['<on event="left right" run="swap player team"/>',
                          '<child widget="main_menu/new_select/list_item_text" x="25"/>',
                          '<child widget="main_menu/new_select/list_item_arrows"/>'])
    lines += _widget(f"{base}/lobby_desc", [("width", 640), ("height", 480)],
                     ['<child widget="main_menu/current_profile_name"/>',
                      f'<child widget="{base}/lobby_right_item" x="22" y="2"/>'])
    lines += _widget(f"{base}/lobby_right_item", [("controller", 1), ("left", 406), ("top", 75), ("width", 162),
                                                  ("height", 326),
                                                  ("bitmap", "bitmaps/spinner_list_3_wide_item_background")],
                     # (a campaign level's picture and name in place of the
                     # map's, for network co-op: menu_functions.c's
                     # lobby_map_show)
                     [f'<child widget="{base}/lobby_map_pic"/>', f'<child widget="{base}/lobby_map_name"/>',
                      '<child widget="main_menu/solo_level_select/replay_level_right_pic"/>',
                      '<child widget="main_menu/solo_level_select/replay_level_right_name"/>',
                      f'<child widget="{base}/lobby_game_data"/>'])
    lines += _widget(f"{base}/lobby_map_pic", [("controller", 1), ("left", 419), ("top", 87), ("width", 140),
                                               ("height", 114), ("bitmap", "ui\\shell\\bitmaps\\mp_map_grafix")], [])
    lines += _widget(f"{base}/lobby_map_name", [("type", "text"), ("controller", 1), ("left", 417), ("top", 204),
                                                ("width", 146), ("height", 43), ("string_list", "main_menu/mp_map_list"),
                                                ("font", "ui\\large_ui"), ("color", "#FF2896FF")], [])
    lines += _widget(f"{base}/lobby_game_data", [("type", "text"), ("controller", 1), ("left", 417), ("top", 250),
                                                 ("width", 146), ("height", 144), ("font", "ui\\small_ui"),
                                                 ("color", "#FF2896FF")], [])
    lines += _widget(f"{base}/lobby_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                  ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     # (SWITCH TEAM first: hidden in a game without teams, which
                     # moves the focus to the first shown, START NOW; at the end
                     # it traps none)
                     [f'<child widget="{base}/lobby_button_team" x="120" y="1"/>',
                      f'<child widget="{base}/lobby_button_start" x="250" y="1"/>',
                      f'<child widget="{base}/lobby_button_add" x="380" y="1"/>',
                      f'<child widget="{base}/lobby_button_leave" x="510" y="1"/>'])
    for key, caption, handlers in (
        ("add", "ADD PLAYER", ['<on event="a" run="port lobby add player"/>',
                               '<on event="start" run="port lobby add player"/>']),
        ("team", "SWITCH TEAM", ['<on event="a" run="swap player team"/>', '<on event="start" run="swap player team"/>']),
        ("start", "START NOW", ['<on event="a" run="net game speed start"/>',
                                '<on event="start" run="net game speed start"/>']),
        ("leave", "LEAVE", ['<on event="a" run="mouse emit back event"/>',
                            '<on event="start" run="mouse emit back event"/>'])):
        lines += _widget(f"{base}/lobby_button_{key}", [("type", "text"), ("width", 128), ("height", 24),
                                                       ("bitmap", "bitmaps/text_button_background"),
                                                       ("text", caption), ("font", "ui\\small_ui"),
                                                       ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                         handlers + ['<on event="left_mouse" run="mouse emit accept event"/>'])
    # a split screen player's profile, chosen as they join the lobby (any
    # controller's presses: Co-op's rows; the host's countdown waits)
    lines += _widget(f"{base}/player_profile_screen", [("width", 640), ("height", 480),
                                                       ("flags", "pass_unhandled_to_focused_child"),
                                                       ("bitmap", "bitmaps/gradient")],
                     ['<on event="created" run="net server defer start"/>',
                      '<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{base}/player_profile_list"/>',
                      f'<child widget="{base}/header_add_player"/>',
                      f'<child widget="{base}/player_profile_help" x="20" y="416"/>'])
    lines += _header(f"{base}/header_add_player", f"{base}/header_add_player")
    lines += _widget(f"{base}/player_profile_list",
                     [("type", "column_list"), ("width", 640), ("height", 480),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                      ("description", "main_menu/profile_manager/player_profile_extended_desc")],
                     ['<data input="3wide player profile list update"/>',
                      '<on event="created" run="port lobby player list initialize"/>',
                      '<on event="deleted" run="player profile list dispose"/>',
                      '<on event="custom_activation" run="port lobby player choose" back="true" branch="true"/>',
                      *[f'<child widget="{MT}/coop/list_item_{index}" x="20" y="{73 + 30 * index}"/>'
                        for index in range(11)],
                      f'<child widget="{MT}/coop/player_2_button_bar" y="414"/>'])
    lines += _widget(f"{base}/player_profile_help", [("type", "text"), ("width", 350), ("height", 24),
                                                     ("text", "Choose the new player's profile."),
                                                     ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("text_y", 5),
                                                     ("text_flags", "no_focus_test")], [])
    # a game under way's lobby, before joining it (the browser's rows of
    # games in progress): what its advertisement tells, JOIN GAME. Split
    # screen players join here, as in the lobby, before JOIN GAME: the game
    # starts at once for the machine, with the players it brings
    lines += _widget(f"{base}/preview_screen", [("width", 640), ("height", 480),
                                                ("flags", "pass_unhandled_to_focused_child"),
                                                ("bitmap", "bitmaps/gradient")],
                     ['<on event="created" run="port lobby open"/>',
                      '<on event="b" run="port lobby preview leave" back="true"/>',
                      '<on event="back" run="port lobby preview leave" back="true"/>',
                      f'<on event="start" run="port lobby preview add" open="{base}/player_profile_screen" branch="true"/>',
                      '<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{base}/preview_list"/>',
                      f'<child widget="{base}/header_lobby"/>',
                      f'<child widget="{base}/lobby_join_help"/>'])
    lines += _widget(f"{base}/preview_list", [("type", "column_list"), ("width", 640), ("height", 480),
                                              ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                                              ("description", f"{base}/lobby_desc")],
                     ['<data input="port lobby preview update"/>',
                      f'<child widget="{base}/preview_status" x="30" y="75"/>',
                      f'<child widget="{base}/preview_button_bar" y="414"/>'])
    lines += _widget(f"{base}/preview_status", [("type", "text"), ("controller", 1), ("width", 360), ("height", 300),
                                                ("font", "ui\\large_ui"), ("color", "#FF2896FF")], [])
    lines += _widget(f"{base}/preview_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                    ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     [f'<child widget="{base}/preview_button_join" x="250" y="1"/>',
                      f'<child widget="{base}/preview_button_add" x="380" y="1"/>',
                      f'<child widget="{base}/preview_button_back" x="510" y="1"/>'])
    for key, caption, run in (("join", "JOIN GAME", "port lobby preview join"),
                              ("add", "ADD PLAYER", "port lobby add player"),
                              ("back", "BACK", "mouse emit back event")):
        lines += _widget(f"{base}/preview_button_{key}", [("type", "text"), ("width", 128), ("height", 24),
                                                         ("bitmap", "bitmaps/text_button_background"),
                                                         ("text", caption), ("font", "ui\\small_ui"),
                                                         ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)],
                         [f'<on event="a" run="{run}"/>', f'<on event="start" run="{run}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>'])
    return lines


def _coop() -> list:
    """Co-op, the campaign for two players on this machine in split screen
    (the Xbox's Cooperative Play): Multiplayer's CO-OP CAMPAIGN, then player
    2's profile, chosen with player 2's controller (its rows take any
    controller's presses, as the shared rows do only controller 1's), then
    New Game's levels and difficulty, which either player's controller uses
    (ui_widget.c's widget_takes_events_of_controller; menu_functions.c's
    coop_begin)"""
    base = f"{MT}/coop"
    lines = _widget(f"{MT}/multiplayer_type_coop_item",
                    [("type", "text"), ("left", 51), ("width", 232), ("height", 32), ("bitmap", "bitmaps/list_item_bkd"),
                     ("string_list", f"{MT}/multiplayer_options"), ("string_index", 7), ("font", "ui\\large_ui"),
                     ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 5)],
                    [f'<on event="a" run="port coop begin" open="{base}/player_2_profile_screen" branch="true"/>',
                     f'<on event="start" run="port coop begin" open="{base}/player_2_profile_screen" branch="true"/>',
                     '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _widget(f"{base}/player_2_profile_screen", [("width", 640), ("height", 480),
                                                         ("flags", "pass_unhandled_to_focused_child"),
                                                         ("bitmap", "bitmaps/gradient")],
                     ['<child widget="main_menu/new_select/sel_list_desc_bkd"/>',
                      f'<child widget="{base}/player_2_profile_list"/>',
                      f'<child widget="{base}/header_player_2"/>',
                      f'<child widget="{base}/player_2_help" x="20" y="416"/>'])
    lines += _header(f"{base}/header_player_2", f"{base}/header_player_2")
    rows = [f'<child widget="{base}/list_item_{index}" x="20" y="{73 + 30 * index}"/>' for index in range(11)]
    lines += _widget(f"{base}/player_2_profile_list",
                     [("type", "column_list"), ("width", 640), ("height", 480),
                      ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                      ("description", "main_menu/profile_manager/player_profile_extended_desc")],
                     ['<data input="3wide player profile list update"/>',
                      '<on event="created" run="port coop player 2 list initialize"/>',
                      '<on event="deleted" run="player profile list dispose"/>',
                      '<on event="custom_activation" run="port coop player 2" '
                      'open="main_menu/solo_level_select/solo_level_select_screen" branch="true"/>',
                      *rows, f'<child widget="{base}/player_2_button_bar" y="414"/>'])
    for index in range(11):
        lines += _widget(f"{base}/list_item_{index}",
                         [("width", 390), ("height", 28),
                          ("bitmap", "bitmaps/sel_list_item_bkd_top" if index == 0 else "bitmaps/sel_list_item_bkd"),
                          ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("align", "center"), ("text_y", 3)],
                         ['<on event="a" run="single prev cl item activated"/>',
                          '<on event="start" run="single prev cl item activated"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>',
                          '<child widget="main_menu/new_select/list_item_text" x="25"/>',
                          '<child widget="main_menu/new_select/list_item_arrows"/>'])
    lines += _widget(f"{base}/player_2_help", [("type", "text"), ("width", 350), ("height", 24),
                                               ("text", "Player 2, choose a profile."),
                                               ("font", "ui\\small_ui"), ("color", "#FF2896FF"), ("text_y", 5),
                                               ("text_flags", "no_focus_test")], [])
    lines += _widget(f"{base}/player_2_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                     ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     ['<data input="common button bar update"/>',
                      '<child widget="main_menu/profile_manager/profile_manager_button_ok" x="380" y="1"/>',
                      '<child widget="common_button_back" x="510" y="1"/>'])
    return lines


def _item_options_extras() -> list:
    """Item Options' rows of the port's: the map's weapons (YES or NO), and
    the loadout, CATEGORY (the weapon set) or CUSTOM (each player's primary
    and secondary weapons)"""
    base = "main_menu/settings_select/multiplayer_setup/item_options_edit"
    lines = []
    for key, strings, label in (("map_weapons", "var_map_weapons", 8), ("loadout", "var_loadout", 5),
                                ("primary_weapon", "var_loadout_weapon", 6),
                                ("secondary_weapon", "var_loadout_weapon", 7)):
        lines += _widget(f"{base}/op_{key}", [("width", 512), ("height", 28),
                                               ("flags", "pass_unhandled_to_focused_child"),
                                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                         [f'<child widget="{base}/{key}_label"/>',
                          f'<child widget="{base}/{key}_spinner" x="286" y="1"/>'])
        lines += _widget(f"{base}/{key}_label", [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                                                   ("string_list", f"{base}/item_options_labels"),
                                                   ("string_index", label), ("font", "ui\\large_ui"),
                                                   ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        lines += _widget(f"{base}/{key}_spinner",
                         [("type", "spinner"), ("top", 2), ("width", 206), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("string_list", f"{base}/{strings}"), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                          ("align", "center"), ("text_y", 4), ("list_flags", "items_from_strings"),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -13 19 -7"), ("footer_bounds", "7 208 19 214")],
                         ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    lines += _strings(f"{base}/var_map_weapons", ["YES", "NO"])
    lines += _strings(f"{base}/var_loadout", ["CATEGORY", "CUSTOM"])
    lines += _strings(f"{base}/var_loadout_weapon", LOADOUT_WEAPONS)
    return lines


def _map_kind() -> list:
    """the map lists' first row (New Game's and the Map screen's), as the
    gametype list's chooser: a spinner of SINGLEPLAYER or MULTIPLAYER maps,
    for either controller (split screen co-op's New Game takes both)"""
    lines = _widget(MAP_KIND_CHOOSER, [("width", 256), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                       ("bitmap", "bitmaps/option_bkds_sm"), ("font", "ui\\large_ui"),
                                       ("color", "#FF2896FF"), ("align", "center"), ("text_y", 3)],
                    [f'<child widget="{MAP_KIND_CHOOSER}_spinner" x="15" y="2"/>'])
    lines += _widget(f"{MAP_KIND_CHOOSER}_spinner",
                     [("type", "spinner"), ("width", 226), ("height", 22), ("flags", "left_right_tabs_items"),
                      ("string_list", "main_menu/new_select/var_map_kinds"), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1), ("list_flags", "items_from_strings"),
                      ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                      ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 226 19 232")],
                     ['<on event="left_mouse" run="mouse spinner 1wide click"/>'])
    lines += _strings("main_menu/new_select/var_map_kinds", ["SINGLEPLAYER", "MULTIPLAYER"])
    return lines


def multiplayer_files() -> dict:
    """the port's multiplayer widgets: the browser's additions, the server
    settings, the lobby"""
    head = ['<?xml version="1.0" encoding="UTF-8"?>',
            "<!-- The port's multiplayer screens, in the PC version's style (tools/port_settings.py) -->", "<menus>"]
    return {
        f"{MT}/join_game".replace("/", ".") + ".port.xml": head + _join_game_extras() + ["</menus>", ""],
        f"{MT}/server_settings".replace("/", ".") + ".xml": head + _server_settings() +
        _setting_screen(f"{MT}/server_settings/host_options", HOST_OPTIONS) + ["</menus>", ""],
        f"{MT}/lobby".replace("/", ".") + ".xml": head + _lobby() + ["</menus>", ""],
        f"{MT}/coop".replace("/", ".") + ".xml": head + _coop() + ["</menus>", ""],
        "main_menu/new_select".replace("/", ".") + ".port.xml": head + _map_kind() + ["</menus>", ""],
        "main_menu/settings_select/multiplayer_setup/item_options_edit".replace("/", ".") + ".port.xml": head + _item_options_extras() + ["</menus>", ""],
    }


REPLACED_FOLDERS = [f"{MT}/server_settings"]
