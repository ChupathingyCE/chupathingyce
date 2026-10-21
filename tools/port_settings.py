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
settings defaults" shows the defaults. Controls Setup binds the keyboard and
mouse's controls, by the functions the PC version names for it.
"""

from xml.sax.saxutils import quoteattr

PE = "main_menu/settings_select/player_setup/player_profile_edit"
YES_NO = [("YES", "true"), ("NO", "false")]
ON_OFF = [("ON", "true"), ("OFF", "false")]
# (after a screen's helps: shown for Button Layout set to Bumper Jumper, by
# menu_functions.c's settings_help)
BUMPER_JUMPER_HELP = ["Jump with the left bumper."]
SENSITIVITIES = [(f"{value:g}", f"{value:g}") for value in (0.1, 0.15, 0.25, 0.4, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4)]
VOLUMES = [(str(step), f"{step / 10:g}") for step in range(11)]

# each screen: its folder below PE, its screen's widget (the name the profile
# menu opens), its header (widget, bitmap), the row spacing, and its rows:
# (label, setting, [(shown, value)], help, platform[, key]): key names the
# row's widgets where two rows set one setting (one for each platform), else
# the setting's name does
SCREENS = {
    "video_settings": {
        "screen": "video_settings_screen",
        "header": ("header_profile_video_settings", f"{PE}/video_settings/header_profile_video_settings"),
        # (close together, for all thirteen places to fit above a help line)
        "spacing": 24,
        "help_top": 388,
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
            ("SHADOW RESOLUTION:", "display.shadow_resolution",
             [("128", "128"), ("256", "256"), ("512", "512"), ("1024", "1024")],
             "The detail of object shadows.",
             None),
            ("PER-PIXEL LIGHTING:", "display.per_pixel_lighting", ON_OFF,
             "Light models per pixel instead of per vertex.", None),
            ("FIELD OF VIEW:", "display.field_of_view",
             [(f"{value}" + (" (XBOX)" if value == 70 else ""), f"{value}")
              for value in (60, 65, 70, 75, 80, 85, 90, 95, 100, 105, 110, 120)],
             "Adjust the field of view.",
             None),
            ("FAR CLIP:", "display.far_clip_distance",
             [("1024 (XBOX)", "1024"), ("2048", "2048"), ("4096", "4096"), ("8192", "8192"), ("10000", "10000")],
             "How far the world is drawn.", None),
            ("ANISOTROPIC FILTERING:", "display.anisotropic_filtering",
             [("OFF", "0"), ("2X", "2"), ("4X", "4"), ("8X", "8"), ("16X", "16")],
             "Sharper textures seen at an angle.", None),
            ("FULL MODEL DETAIL:", "display.full_model_detail", ON_OFF,
             "Always draw models at their highest detail.", None),
            ("VIEWMODEL FOV:", "display.viewmodel_field_of_view",
             [("SAME", "0")] + [(f"{value}", f"{value}") for value in (50, 55, 60, 65, 70, 75, 80, 85, 90)],
             "How wide your weapon is drawn. Same follows the view.", None),
        ],
    },
    "mouse_settings": {
        "screen": "mouse_settings_screen",
        "header": ("header_profile_mouse_settings", f"{PE}/mouse_settings/header_profile_mouse_settings"),
        "spacing": 30,
        "rows": [
            ("HORIZONTAL SENSITIVITY:", "input.mouse_sensitivity", SENSITIVITIES,
             "Adjust the horizontal look sensitivity.", None),
            ("VERTICAL SENSITIVITY:", "input.mouse_vertical_sensitivity", [("SAME", "0")] + SENSITIVITIES,
             "Adjust the vertical look sensitivity.", None),
            ("INVERT VERTICAL AXIS:", "input.invert_mouse", YES_NO,
             "Moving the mouse forward looks down.", None),
            ("AIM ASSIST:", "input.mouse_aim_assist", ON_OFF,
             "Use controller aim assist with the mouse.", None),
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
            # (port/linux/game/menu_tags.c adds the system's outputs)
            ("OUTPUT DEVICE:", "audio.device", [("DEFAULT", "default")],
             "Choose the audio output device.", "desktop"),
            ("REVERB:", "audio.reverb", ON_OFF,
             "Room echo, and muffled sound behind walls.", None),
            ("SOUND:", "audio.enabled", ON_OFF,
             "Turn all sound on or off. Takes effect on restart.", None),
        ],
    },
    # (opened from the profile menu's DISPLAY list: SUBMENUS)
    "interface_setup": {
        "screen": "interface_settings_screen",
        "header": ("header_profile_interface_settings", f"{PE}/interface_setup/header_interface"),
        "spacing": 24,
        "help_top": 364,
        "rows": [
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
            ("KEYBOARD PROMPTS:", "input.keyboard_prompts", ON_OFF,
             "Show keys in prompts when using the keyboard.", "desktop"),
            ("VERSION WATERMARK:", "display.version_watermark", ON_OFF,
             "Show the version while playing.", None),
        ],
    },
    # (opened from the profile menu's ONLINE list: SUBMENUS;
    # chat.quick_messages, a list of lines, is in config.toml alone)
    "chat_setup": {
        "screen": "chat_settings_screen",
        "header": ("header_profile_chat_settings", f"{PE}/chat_setup/header_chat"),
        "spacing": 30,
        "rows": [
            ("CHAT:", "chat.enabled", ON_OFF, "Text chat with other players.", None),
            ("SHOW CHAT:", "chat.show", [("ALL", "all"), ("TEAM", "team"), ("OFF", "off")],
             "Choose whose chat lines show.", None),
            ("WORD FILTER:", "chat.filter", [("MASK", "mask"), ("HIDE", "hide"), ("SHOW", "show")],
             "Mask, hide or show blocked words.", None),
            ("STREAMER MODE:", "chat.streamer_mode", ON_OFF,
             "Show players in chat by team, not by name.", None),
        ],
    },
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
    },
    # (opened from the profile menu's ONLINE list: SUBMENUS. The Delta
    # profile's rows, PR #382's, go after Joined Game Stats)
    "privacy_setup": {
        "screen": "privacy_settings_screen",
        "header": ("header_profile_privacy_settings", f"{PE}/privacy_setup/header_privacy"),
        "spacing": 30,
        "rows": [
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
        ],
    },
    "gamepad_setup": {
        "screen": "gamepad_setup_screen",
        # (a button beside Defaults: Controller Buttons, which sets the
        # buttons one by one: CONTROLLER_SCREEN)
        "extra_button": ("button_controller", "BUTTONS",
                         [f'<on event="a" open="{PE}/controller_buttons/controller_buttons_screen"/>',
                          f'<on event="start" open="{PE}/controller_buttons/controller_buttons_screen"/>']),
        "header": ("header_profile_controller_setting", f"{PE}/controller_edit/header_profile_controller_setting"),
        "spacing": 30,
        "rows": [
            ("LOOK SENSITIVITY:", "profile.look_sensitivity", [(str(step), str(step)) for step in range(1, 11)],
             "Adjust the look sensitivity.", None),
            ("INVERT LOOK:", "profile.invert_look", YES_NO, "Pushing the right stick up looks down.", None),
            ("INVERT FLIGHT:", "profile.flight_inversion", YES_NO,
             "Pushing up flies down in flying vehicles.", None),
            ("AUTO-CENTER LOOK:", "profile.autocenter", YES_NO,
             "Level the view while walking.", None),
            ("BUTTON LAYOUT:", "profile.button_preset",
             [("DEFAULT", "0"), ("SWAP TRIGGERS", "1"), ("SWAP A, L TRIGGER", "2"), ("SWAP B, L TRIGGER", "3"),
              ("SWAP B, R STICK", "4"), ("BUMPER JUMPER", "5"), ("CUSTOM", "6")],
             "Change the controller button layout.", None),
            ("STICK LAYOUT:", "profile.joystick_preset",
             [("DEFAULT", "0"), ("SOUTHPAW", "1"), ("LEGACY", "2"), ("LEGACY SOUTHPAW", "3")],
             "Change the thumbstick layout.", None),
            ("VIBRATION:", "profile.vibration", ON_OFF, "Rumble the controller.", None),
            ("IN-GAME HELP:", "profile.ingame_help", ON_OFF, "Show control hints during play.", None),
        ],
    },
}

# the settings whose values are long (names the system gives): their
# spinner starts nearer the label, as wide as Controls Setup's
WIDE_SPINNERS = {"audio.device"}

# Controls Setup: the keyboard and mouse's actions, in groups (the order of
# port/linux/game/menu_functions.c's table of them)
CONTROL_GROUPS = ["MOVEMENT", "WEAPONS", "ACTIONS"]
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


def _text_button(name: str, text: str, handlers: list) -> list:
    """a button like _button's, with its own text"""
    pairs = [("type", "text"), ("width", 128), ("height", 24), ("bitmap", "bitmaps/text_button_background"),
             ("text", text), ("font", "ui\\small_ui"), ("color", "#FFFFFFFF"), ("align", "center"), ("text_y", 2)]
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
                     ['<on event="b" back="true"/>', '<on event="back" back="true"/>',
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
    if "extra_button" in spec:
        name, text, handlers = spec["extra_button"]
        buttons.append(f'<child{attributes([("widget", f"{base}/{name}"), ("x", 130), ("y", 1)])}/>')
        extra = extra + _text_button(f"{base}/{name}", text, handlers)
    buttons += [f'<child{attributes([("widget", f"{base}/button_ok"), ("x", 380), ("y", 1)])}/>',
                '<child widget="common_button_cancel" x="510" y="1"/>']
    lines += _widget(f"{base}/button_bar",
                     [("type", "column_list"), ("width", 640), ("height", 28),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")], buttons)
    lines += extra
    return lines


def _setting_screen(folder: str, spec: dict) -> list:
    base = folder if folder.startswith("main_menu/") else f"{PE}/{folder}"
    rows, extra = [], []
    place = -1
    for index, (label, setting, choices, _, platform, *named) in enumerate(spec["rows"]):
        key = named[0] if named else setting.split(".", 1)[1]
        wide = setting in WIDE_SPINNERS
        row = f"{base}/op_{key}"
        if setting not in spec.get("same_place", ()) and key not in spec.get("same_place", ()):
            place += 1
        rows.append((row, platform, place))
        extra += _widget(row, [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                               ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF"), ("platform", platform)],
                         [f'<child{attributes([("widget", f"{base}/{key}_label")])}/>',
                          f'<child{attributes([("widget", f"{base}/{key}_spinner"), ("x", 200 if wide else 320), ("y", 1)])}/>'])
        extra += _widget(f"{base}/{key}_label",
                         [("type", "text"), ("controller", 1), ("width", 300), ("height", 22),
                          ("string_list", f"{base}/labels"), ("string_index", index), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
        extra += _widget(f"{base}/{key}_spinner",
                         [("type", "spinner"), ("left", 3), ("top", 2), ("width", 297 if wide else 147), ("height", 20),
                          ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                          ("strings", "|".join(shown for shown, _ in choices)), ("setting", setting),
                          ("values", "|".join(value for _, value in choices)), ("font", "ui\\large_ui"),
                          ("color", "#FF2896FF"), ("align", "center"), ("text_y", 1),
                          ("header_bitmap", "bitmaps/arrow_sm_left"), ("footer_bitmap", "bitmaps/arrow_sm_right"),
                          ("header_bounds", "7 -6 19 0"), ("footer_bounds", "7 300 19 306" if wide else "7 150 19 156")],
                         ['<on event="created" run="port setting load"/>'])
    extra += _button(f"{base}/button_defaults", 3, ['<on event="a" run="port settings defaults"/>',
                                                    '<on event="start" run="port settings defaults"/>'])
    extra += _button(f"{base}/button_ok", 1, ['<on event="a" run="port settings save" back="true"/>',
                                              '<on event="start" run="port settings save" back="true"/>'])
    extra += _strings(f"{base}/labels", [label for label, *_ in spec["rows"]])
    # (the help of the row whose label is string n is n + 1: the buttons' is 0)
    extra += _strings(f"{base}/help_strings",
                      [""] + [help_text.replace("\n", "\\n") for _, _, _, help_text, *_ in spec["rows"]]
                      + BUMPER_JUMPER_HELP * any(row[1] == "profile.button_preset" for row in spec["rows"]))
    return _screen(folder, spec, rows, ["port settings help"], [], extra)


def _controls_screen() -> list:
    folder = "controls_setup"
    base = f"{PE}/{folder}"
    spec = {"screen": "controls_settings_screen", "spacing": 30,
            "header": ("header_profile_controls_settings", f"{base}/header_profile_controls_settings")}
    rows, extra = [(f"{base}/op_group", None)], []
    extra += _widget(f"{base}/op_group", [("width", 512), ("height", 28), ("flags", "pass_unhandled_to_focused_child"),
                                          ("bitmap", "bitmaps/option_bkds"), ("color", "#FF2896FF")],
                     [f'<child{attributes([("widget", f"{base}/group_label")])}/>',
                      f'<child{attributes([("widget", f"{base}/group_spinner"), ("x", 200), ("y", 1)])}/>'])
    extra += _widget(f"{base}/group_label",
                     [("type", "text"), ("controller", 1), ("width", 200), ("height", 22), ("text", "SHOW:"),
                      ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 4)], [])
    extra += _widget(f"{base}/group_spinner",
                     [("type", "spinner"), ("left", 3), ("top", 2), ("width", 297), ("height", 20),
                      ("flags", "pass_unhandled_to_focused_child left_right_tabs_items"),
                      ("strings", "|".join(CONTROL_GROUPS)), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                      ("align", "center"), ("text_y", 1), ("header_bitmap", "bitmaps/arrow_sm_left"),
                      ("footer_bitmap", "bitmaps/arrow_sm_right"), ("header_bounds", "7 -6 19 0"),
                      ("footer_bounds", "7 300 19 306")], [])
    for index in range(CONTROL_ROWS):
        row = f"{base}/op_command_{index + 1}"
        rows.append((row, None))
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
    extra += _button(f"{base}/button_defaults", 3, ['<on event="a" run="controls screen defaults"/>',
                                                    '<on event="start" run="controls screen defaults"/>'])
    extra += _button(f"{base}/button_ok", 1, ['<on event="a" run="controls screen change set" back="true"/>',
                                              '<on event="start" run="controls screen change set" back="true"/>'])
    extra += _strings(f"{base}/help_strings", [
        "Choose a group of controls.",
        "Enter changes a control. Left and right choose\\nthe first or second key.",
        "Press a key or mouse button. Escape cancels,\\nDelete clears.",
    ])
    return _screen(folder, spec, rows, ["controls update menu"], ['<on event="created" run="controls screen init"/>'],
                   extra)


# Controller Buttons: the controller's button layout (the profile's, as
# Gamepad Setup's BUTTON LAYOUT sets it), then each action's button, which A
# sets (port/linux/game/menu_functions.c's "controller begin binding": the
# next button pressed, swapped with the action that had it, confirmed with
# A). Changing one makes the layout Custom (config.toml's
# controls.controller_layout). The help line says what is happening, by the
# controller's own names for its buttons.
CONTROLLER_SCREEN = "controller_buttons"
CONTROLLER_PRESETS = ["DEFAULT", "SWAP TRIGGERS", "SWAP A, L TRIGGER", "SWAP B, L TRIGGER", "SWAP B, R STICK",
                      "BUMPER JUMPER", "CUSTOM"]
# (port/linux/game/menu_functions.c's button_row_actions, in order)
CONTROLLER_ACTIONS = ["FIRE:", "GRENADE:", "MELEE:", "JUMP:", "ACTION / RELOAD:", "SWITCH WEAPON:",
                      "SWITCH GRENADE:", "FLASHLIGHT:", "CROUCH:", "ZOOM:"]
CONTROLLER_SPEC = {"screen": "controller_buttons_screen", "spacing": 26, "help_top": 366, "help_text": True,
                   "header": ("header_profile_controller_setting",
                              f"{PE}/controller_edit/header_profile_controller_setting")}


def _controller_rows() -> list:
    """Controller Buttons' rows: (widget, platform, place)"""
    base = f"{PE}/{CONTROLLER_SCREEN}"
    return [(f"{base}/op_layout", None, 0)] + [(f"{base}/op_button_{index + 1}", None, index + 1)
                                                for index in range(len(CONTROLLER_ACTIONS))]


def _controller_screen() -> list:
    base = f"{PE}/{CONTROLLER_SCREEN}"
    extra = []
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
    for index, label in enumerate(CONTROLLER_ACTIONS):
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
    extra += _button(f"{base}/button_defaults", 3, ['<on event="a" run="controller screen defaults"/>',
                                                    '<on event="start" run="controller screen defaults"/>'])
    # (OK and Cancel both go back: each change is saved as it is made)
    extra += _button(f"{base}/button_ok", 1, ['<on event="a" back="true"/>', '<on event="start" back="true"/>'])
    return _screen(CONTROLLER_SCREEN, CONTROLLER_SPEC, [(row, platform) for row, platform, _ in _controller_rows()],
                   ["controller update menu"], ['<on event="created" run="controller screen init"/>'], extra)


# the profile menu's rows that open a list of pages (CONTROLS, DISPLAY and
# ONLINE), laid out as the profile menu is (its rows, their help and the
# profile's name). Each: its title, the row's string of profile_edit_options,
# the PC style's row (None: the PC version's own, WIDGET_PATCHES) and its top,
# and its pages: (label, the page's folder, help)
SUBMENUS = {
    "controls_menu": ("CONTROLS", 12, None, [
        ("KEYBOARD", "controls_setup", "Change the keyboard and\\nmouse controls."),
        ("MOUSE", "mouse_settings", "Adjust the mouse settings.\\n"),
        ("GAMEPADS", "gamepad_setup", "Change the controller\\nconfiguration."),
        ("GAMEPAD BUTTONS", CONTROLLER_SCREEN, "Choose the button for\\neach action."),
    ]),
    "display_menu": ("DISPLAY", 13, 177, [
        ("VIDEO", "video_settings", "Adjust the video settings.\\n"),
        ("INTERFACE", "interface_setup", "Adjust the menus, HUD and\\nscoreboard."),
    ]),
    "online_menu": ("ONLINE", 15, 243, [
        ("NETWORK", "network_setup", "Adjust the network settings.\\n"),
        ("CHAT", "chat_setup", "Adjust the chat settings.\\n"),
        ("PRIVACY", "privacy_setup", "Choose what stats and crash\\nreports are sent."),
    ]),
}
# (the pictures: the Xbox's controller for the gamepad's pages, else none)
BLANK_FRAME = '<frame png="ce/port/blank__0.png" width="4" height="4"/>'
CONTROLLER_FRAME = f'<frame map="ui\\shell\\{PE.replace("/", chr(92))}\\profile_options" index="1"/>'


def _page_screen(folder: str) -> str:
    """the PC style's screen of a settings page"""
    name = {"controls_setup": "controls_settings_screen", CONTROLLER_SCREEN: "controller_buttons_screen"}.get(folder)
    return f"{PE}/{folder}/{name or SCREENS[folder]['screen']}"


def _submenu_list(menu: str, base: str, list_name: str, description: str, top: int, spacing: int,
                  screen_of, profile: str) -> list:
    """a list of a submenu's pages (its rows, help and picture): the PC
    style's (base and names below PE) or the Chupathingy menus'"""
    _, _, _, pages = SUBMENUS[menu]
    lines = []
    children = ['<data input="plyr prof edit select menu upd8"/>']
    children += [f'<child widget="{base}_item_{index}"/>' for index in range(len(pages))]
    if list_name.startswith(PE):
        children.append(f'<child widget="{base}_button_bar" y="414"/>')
    lines += _widget(list_name, [("type", "column_list"), ("width", 640), ("height", 480),
                                 ("flags", "pass_unhandled_to_focused_child up_down_tabs_items"),
                                 ("description", description)], children)
    for index, (_, folder, _) in enumerate(pages):
        screen = screen_of(folder)
        lines += _widget(f"{base}_item_{index}",
                         [("type", "text"), ("left", 51), ("top", top + index * spacing), ("width", 232),
                          ("height", 32),
                          ("bitmap", "bitmaps/list_item_bkd_top" if index == 0 else "bitmaps/list_item_bkd"),
                          ("string_list", f"{base}_options"), ("string_index", index or None),
                          ("font", "ui\\large_ui"), ("color", "#FF2896FF"), ("text_x", 13), ("text_y", 5)],
                         [f'<on event="a" open="{screen}"/>', f'<on event="start" open="{screen}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>'])
    lines += _widget(description, [("controller", 1), ("left", 275), ("top", 78), ("width", 320),
                                   ("height", 362), ("bitmap", "bitmaps/list_field")],
                     [f'<child widget="{base}_description_text"/>', f'<child widget="{base}_picture" x="4"/>'])
    lines += _widget(f"{base}_description_text",
                     [("type", "text"), ("controller", 1), ("left", 294), ("top", 290), ("width", 296),
                      ("height", 110), ("string_list", f"{base}_descriptions"), ("font", "ui\\large_ui"),
                      ("color", "#FF2896FF")], [])
    lines += _widget(f"{base}_picture", [("controller", 1), ("left", 288), ("top", 85), ("width", 279),
                                         ("height", 202), ("bitmap", f"{base}_pictures")], [])
    frames = [CONTROLLER_FRAME if folder in ("gamepad_setup", CONTROLLER_SCREEN) else BLANK_FRAME
              for _, folder, _ in pages]
    lines += [f'\t<bitmap name="{base}_pictures">', *[f"\t\t{frame}" for frame in frames], "\t</bitmap>"]
    lines += _strings(f"{base}_options", [label for label, _, _ in pages])
    lines += _strings(f"{base}_descriptions", [f"{text}\\n\\n{profile}" for _, _, text in pages])
    return lines


def _submenu(menu: str) -> list:
    """the PC style's submenu: its header, list, and a Back button; and the
    profile menu's row that opens it"""
    title, string_index, row_top, _ = SUBMENUS[menu]
    base = f"{PE}/{menu}"
    screen = f"{base}/{menu}_screen"
    lines = []
    lines += _widget(screen, [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child pause_game"),
                              ("bitmap", "bitmaps/gradient")],
                     ['<on event="b" back="true"/>', '<on event="back" back="true"/>',
                      f'<child widget="{base}/header"/>', f'<child widget="{base}/list"/>',
                      f'<child widget="{PE}/profile_label" x="293" y="375"/>'])
    lines += _widget(f"{base}/header", [("controller", 1), ("left", 35), ("top", 11), ("width", 605),
                                        ("height", 59), ("bitmap", f"{base}/header_{title.lower()}")], [])
    lines += _submenu_list(menu, f"{base}/list", f"{base}/list", f"{base}/description", 78, 33, _page_screen,
                           "Profile:")
    lines += _widget(f"{base}/list_button_bar", [("type", "column_list"), ("width", 640), ("height", 28),
                                                 ("flags", "pass_unhandled_to_focused_child left_right_tabs_items")],
                     [f'<child widget="{base}/button_back" x="510" y="1"/>'])
    lines += _button(f"{base}/button_back", 0, ['<on event="a" back="true"/>', '<on event="start" back="true"/>'])
    if row_top is not None:
        lines += _widget(f"{PE}/{menu.split('_')[0]}_profile_item",
                         [("type", "text"), ("left", 51), ("top", row_top), ("width", 232), ("height", 32),
                          ("bitmap", "bitmaps/list_item_bkd"), ("string_list", f"{PE}/profile_edit_options"),
                          ("string_index", string_index), ("font", "ui\\large_ui"), ("color", "#FF2896FF"),
                          ("text_x", 13), ("text_y", 5)],
                         [f'<on event="a" open="{screen}"/>', f'<on event="start" open="{screen}"/>',
                          '<on event="left_mouse" run="mouse emit accept event"/>'])
    return lines


def settings_files() -> dict:
    """the files of the port's screens, by their names in ce/"""
    files = {}
    for folder, spec in SCREENS.items():
        files[f"{PE}/{folder}".replace("/", ".") + ".xml"] = _setting_screen(folder, spec)
    files[f"{PE}/controls_setup".replace("/", ".") + ".xml"] = _controls_screen()
    files[f"{PE}/{CONTROLLER_SCREEN}".replace("/", ".") + ".xml"] = _controller_screen()
    for menu in SUBMENUS:
        files[f"{PE}/{menu}".replace("/", ".") + ".xml"] = _submenu(menu)
    return {name: ['<?xml version="1.0" encoding="UTF-8"?>',
                   "<!-- The port's settings screen, in the PC version's style (tools/port_settings.py) -->",
                   "<menus>", *lines, "</menus>", ""] for name, lines in files.items()}


# ---------- the Chupathingy menus' settings pages

# (display.menus "chupathingy": port/assets/menus/chupathingy) the same rows
# and help as the screens above, laid out as the Xbox's ADVANCED CONTROLS:
# no button bar, the Xbox's prompts instead (B cancels, A accepts, Y shows the
# defaults; Controls Setup's A changes a key, so X accepts there)
CHUPATHINGY = "chupathingy/settings"
# (every page SCREENS has, so that a page added there is here too)
CHUPATHINGY_PAGES = ["controls_setup", CONTROLLER_SCREEN, *SCREENS]


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


def _setting_places(spec: dict) -> list:
    """each row's key, platform and place (as _setting_screen lays them)"""
    places, place = [], -1
    for label, setting, choices, _, platform, *named in spec["rows"]:
        key = named[0] if named else setting.split(".", 1)[1]
        if setting not in spec.get("same_place", ()) and key not in spec.get("same_place", ()):
            place += 1
        places.append((key, platform, place))
    return places


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
        if folder == "controls_setup":
            spec = {"screen": "controls_settings_screen", "spacing": 30,
                    "header": ("header_profile_controls_settings", None)}
            rows = [(f"{base}/op_group", None, 0)] + [(f"{base}/op_command_{index + 1}", None, index + 1)
                                                       for index in range(CONTROL_ROWS)]
            inputs = ["controls update menu"]
            handlers = ['<on event="created" run="controls screen init"/>',
                        '<on event="x" run="controls screen change set" back="true"/>',
                        '<on event="y" run="controls screen defaults"/>']
            prompts = (_xbox_prompt("y", 60, "defaults") + _xbox_prompt("x", 215, "accept") +
                       _xbox_prompt("b", 350, "cancel") + _xbox_prompt("a", 490, "change"))
        elif folder == CONTROLLER_SCREEN:
            spec = CONTROLLER_SPEC
            rows = _controller_rows()
            inputs = ["controller update menu"]
            # (X: the keyboard's Controls Setup)
            handlers = ['<on event="created" run="controller screen init"/>',
                        f'<on event="x" open="{CHUPATHINGY}/controls_setup_screen"/>',
                        '<on event="y" run="controller screen defaults"/>']
            prompts = (_xbox_prompt("y", 60, "defaults") + _xbox_prompt("x", 215, "keyboard") +
                       _xbox_prompt("b", 375, "back") + _xbox_prompt("a", 490, "change"))
        else:
            spec = SCREENS.get(folder) or CHUPATHINGY_ONLY[folder]
            rows = [(f"{base}/op_{key}", platform, place) for key, platform, place in _setting_places(spec)]
            inputs = ["port settings help"]
            handlers = ['<on event="a" run="port settings save" back="true"/>',
                        '<on event="y" run="port settings defaults"/>']
            prompts = (_xbox_prompt("y", 60, "defaults") + _xbox_prompt("b", 360, "cancel") +
                       _xbox_prompt("a", 490, "accept"))
            # (Gamepad Setup's X: Controller Buttons)
            if folder == "gamepad_setup":
                handlers.append(f'<on event="x" open="{CHUPATHINGY}/{CONTROLLER_SCREEN}_screen"/>')
                prompts += _xbox_prompt("x", 215, "buttons")
        header = spec["header"][0]
        lines += _widget(f"{CHUPATHINGY}/{folder}_screen",
                         [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child pause_game"),
                          ("bitmap", "bitmaps/gradient")],
                         ['<on event="b" back="true"/>', '<on event="back" back="true"/>',
                          f'<child{attributes([("widget", f"{base}/{header}")])}/>',
                          f'<child{attributes([("widget", f"{CHUPATHINGY}/{folder}_list")])}/>', *prompts])
        children = [f'<data input="{name}"/>' for name in inputs] + handlers
        for row, platform, place in rows:
            children.append(f'<child{attributes([("widget", row), ("x", 54), ("y", 73 + place * spec["spacing"]), ("platform", platform)])}/>')
        lines += _widget(f"{CHUPATHINGY}/{folder}_list",
                         [("type", "column_list"), ("width", 640), ("height", 480),
                          ("flags", "pass_unhandled_to_focused_child up_down_tabs_children"),
                          ("description", f"{base}/help")], children)
    # (the profile menu's lists of pages, SUBMENUS: as its rows are laid out,
    # 28 apart, with the Xbox's prompts)
    for menu in SUBMENUS:
        base = f"{CHUPATHINGY}/{menu}"
        lines += _widget(f"{base}_screen",
                         [("width", 640), ("height", 480), ("flags", "pass_unhandled_to_focused_child pause_game"),
                          ("bitmap", "bitmaps/gradient")],
                         ['<on event="b" back="true"/>', '<on event="back" back="true"/>',
                          f'<child widget="{PE}/{menu}/header"/>', f'<child widget="{base}_list"/>',
                          f'<child widget="{PE}/profile_label" x="432" y="353"/>',
                          *_xbox_prompt("b", 360, "back"), *_xbox_prompt("a", 490, "select")])
        lines += _submenu_list(menu, base, f"{base}_list", f"{base}_description", 78, 28,
                               lambda folder: f"{CHUPATHINGY}/{folder}_screen", "Current Profile:")
    # (the rows, labels and help of the pages the PC version's menus have not)
    for folder, spec in CHUPATHINGY_ONLY.items():
        lines += _setting_screen(folder, spec)
    return lines + ["</menus>", ""]


def replaced(folder: str) -> bool:
    """whether the PC version's folder of widgets is replaced by ours (and
    those below it: the controls' advanced screen, the video test)"""
    return any(folder == path or folder.startswith(f"{path}/")
               for path in [*(f"{PE}/{name}" for name in [*SCREENS, "controls_setup", CONTROLLER_SCREEN]),
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
    # CONTROLS, Audio Setup's AUDIO, and DISPLAY and ONLINE are SUBMENUS'),
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
    f"{PE}/controller_setup_profile_item": {"set": {"string_index": SUBMENUS["controls_menu"][1]}, "handlers": [
        f'<on event="a" open="{PE}/controls_menu/controls_menu_screen"/>',
        f'<on event="start" open="{PE}/controls_menu/controls_menu_screen"/>',
        '<on event="left_mouse" run="mouse emit accept event"/>',
    ]},
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
    f"{PE}/privacy_setup/header_privacy": "PRIVACY",
    f"{PE}/interface_setup/header_interface": "INTERFACE",
    f"{PE}/chat_setup/header_chat": "CHAT",
    **{f"{PE}/{menu}/header_{title.lower()}": title for menu, (title, *_) in SUBMENUS.items()},
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
        "Press Enter to copy the invite link.",
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
