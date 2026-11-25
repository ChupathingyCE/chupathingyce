"""input.raw_hid (port/linux/src/port_config.c, sdl_platform.c): USB
controllers through SDL's HIDAPI drivers on Android. Off by default, as SDL
has it there, since Android asks for each controller and the game then holds
it; the hint is set before SDL_Init, and SDL's own HIDDeviceManager asks for
the controllers, so the activity asks for none itself."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG = (ROOT / "port/linux/src/port_config.c").read_text()
PLATFORM = (ROOT / "port/linux/src/sdl_platform.c").read_text()
ACTIVITY = (ROOT / "port/android/app/src/main/java/com/halo/decomp/HaloActivity.java").read_text()


def test_setting_is_android_only_and_off():
    entry = re.search(r'\{ "input\.raw_hid", (_config_\w+), "(\w+)", "(\w+)", \w+, ([\w |]+),', CONFIG)
    assert entry, "input.raw_hid is not in config_settings"
    kind, default, environment, platforms = entry.groups()
    assert kind == "_config_boolean"
    assert default == "false"
    assert environment == "HALO_RAW_HID"
    assert platforms.strip() == "_platform_android"


def test_hint_set_before_sdl_init_on_android():
    start = PLATFORM.index("BOOL platform_sdl_initialize(void)")
    body = PLATFORM[start:PLATFORM.index("platform_sdl_started = TRUE;", start)]
    hint = body.index('if (config_boolean("input.raw_hid"))')
    assert 'SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");' in body[hint:hint + 120]
    assert hint < body.index("SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD")
    android = body.rindex("#ifdef HALO_ANDROID", 0, hint)
    assert "#endif" not in body[android:hint]


def test_no_per_event_logging_and_balanced_preprocessor():
    assert "RAW-HID-LATENCY" not in PLATFORM
    depth = 0
    for line in PLATFORM.splitlines():
        directive = line.strip()
        if directive.startswith(("#if ", "#ifdef ", "#ifndef ")):
            depth += 1
        elif directive.startswith("#endif"):
            depth -= 1
            assert depth >= 0
    assert depth == 0


def test_activity_leaves_usb_permission_to_sdl():
    assert "requestPermission" not in ACTIVITY
    assert "USB_PERMISSION" not in ACTIVITY
    assert not (ROOT / "port/android/app/src/main/res/xml/device_filter.xml").exists()
