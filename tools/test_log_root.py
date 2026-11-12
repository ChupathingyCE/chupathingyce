"""Where debug.txt goes (#1148): on the desktops a save root that is named
(paths.saves, HALO_SAVE_ROOT: a test's, a second copy's) gets the logs too,
in its logs/, so that copy never writes into the player's log; without one
they stay in logs/ beside maps/, as before. paths.logs (HALO_LOG_ROOT) still
wins. Android, iOS, the consoles and the dedicated server keep logs/ beside
maps/ (their apps and scripts name a save root every time).

The rule itself is port_folder_logs_override (port/linux/src/port_folders.c,
tested by tools/test_port_folders.py); here, that every writer of the logs
folder uses it, and a run of a built game (LOG_ROOT_GAME, else the build in
build/: the macOS application, linux64, freebsd64 or the 32-bit linux one
linux-tests has; skipped without one) with HOME, HALO_SAVE_ROOT and
HALO_DATA_ROOT in a scratch folder. Every build says its logs folder on
standard error ("logs folder: ..."): the save root's logs/, or without
HALO_SAVE_ROOT the data root's, as before. A 64-bit build also writes its own
lines into debug.txt from the start (xbox_kernel.c's platform_log, HALO_64BIT
only): that debug.txt is in the save root's logs/, none under HOME or beside
maps/. A 32-bit build's debug.txt has only the game's own lines, so no file
may be there yet; any that is must be in the logs folder it named."""
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"


def function_body(text, start):
    body = text[text.index(start):]
    return body[:body.index("\n}\n")]


def test_every_logs_writer_follows_the_save_root():
    files = (SOURCE / "xbox_files.c").read_text()
    for name in ("const char *platform_folder(enum port_folder folder)\n{",
                 "int platform_logs_folder_early(char *path, unsigned long size)\n{"):
        body = function_body(files, name)
        assert 'port_folder_logs_override(config_string("paths.logs"),' in body, name
        assert 'PLATFORM_LOGS_FOLLOW_SAVES ? config_string("paths.saves") : ""' in body, name
    # (platform_log before the logs folder is known: from the environment
    # while config.toml is being read, from the settings after)
    kernel = function_body((SOURCE / "xbox_kernel.c").read_text(), "void platform_log(const char *format, ...)\n{")
    assert 'loaded ? config_string("paths.logs") : getenv("HALO_LOG_ROOT")' in kernel
    assert 'loaded ? config_string("paths.saves") : getenv("HALO_SAVE_ROOT")' in kernel
    assert kernel.index("port_folder_logs_override(logs, saves") < kernel.index("platform_app_folder(folder")


def test_only_the_desktops_follow():
    platform = (SOURCE / "platform.h").read_text()
    gate = platform[platform.index("#if defined(HALO_ANDROID) || defined(HALO_SERVER)"):]
    gate = gate[:gate.index("#endif")]
    for name in ("HALO_ANDROID", "HALO_SERVER", "HALO_XBOX_CONSOLE", "HALO_CONSOLE_GL_LAYER",
                 "HALO_CONSOLE_CONFIG_IN_DATA_ROOT"):
        assert f"defined({name})" in gate
    assert gate.index("#define PLATFORM_LOGS_FOLLOW_SAVES 0") < gate.index("#define PLATFORM_LOGS_FOLLOW_SAVES 1")


def test_harnesses_read_both_places(tmp_path):
    sys.path.insert(0, str(ROOT / "tools"))
    from run_logs import find_debug_log, read_debug_log
    saves, data = tmp_path / "saves", tmp_path / "data"
    assert find_debug_log(saves, data) is None and read_debug_log(saves, data) == ""
    (data / "logs").mkdir(parents=True)
    (data / "logs" / "debug.txt").write_text("older release\n")
    assert read_debug_log(saves, data) == "older release\n"
    (saves / "logs").mkdir(parents=True)
    (saves / "logs" / "debug.txt").write_text("this build\n")
    assert read_debug_log(saves, data) == "this build\n"


def game():
    named = os.environ.get("LOG_ROOT_GAME")
    if named:
        return Path(named)
    if sys.platform == "darwin":
        candidates = [ROOT / "build" / "macos" / "ChupathingyCE.app" / "Contents" / "MacOS" / "chupathingy"]
    elif sys.platform.startswith("linux") or sys.platform.startswith("freebsd"):
        candidates = [ROOT / "build" / name / "chupathingy" for name in ("linux64", "freebsd64", "linux")]
    else:
        candidates = []
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    return None


def is_64bit(binary):
    """An ELF's class (32- or 64-bit); a Mach-O is 64-bit."""
    with open(binary, "rb") as file:
        head = file.read(5)
    return not (head[:4] == b"\x7fELF" and head[4] == 1)


def logs_folder_said(text):
    said = None
    for line in text.splitlines():
        if "logs folder: " in line:
            said = line.split("logs folder: ", 1)[1].strip()
    return said


def run_game(binary, folder, save_root):
    """Starts the game in folder (HOME, the data root, and save_root if
    given), waits for it to say its logs folder, and stops it. Returns HOME,
    the data root, the logs folder it said and the debug.txt files under
    folder."""
    home = folder / "home"
    data = folder / "data"
    (data / "maps").mkdir(parents=True)
    home.mkdir()
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith("HALO_") and key not in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "APPDATA")}
    environment.update({"HOME": str(home), "HALO_DATA_ROOT": str(data), "SDL_VIDEO_DRIVER": "dummy",
                        "SDL_AUDIO_DRIVER": "dummy", "HALO_UPDATE_AUTO": "0", "HALO_NET_ONLINE": "0"})
    if save_root:
        environment["HALO_SAVE_ROOT"] = str(save_root)
    errors = folder / "stderr.txt"
    with open(errors, "wb") as output:
        process = subprocess.Popen([str(binary)], cwd=str(folder), env=environment, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.DEVNULL, stderr=output, start_new_session=True)
        try:
            deadline = time.time() + 30
            while time.time() < deadline:
                if logs_folder_said(errors.read_text(errors="replace")) or process.poll() is not None:
                    # (and a moment for the lines after it)
                    time.sleep(1)
                    break
                time.sleep(0.2)
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()
    text = errors.read_text(errors="replace")
    said = logs_folder_said(text)
    assert said, text[-2000:]
    return home, data, Path(said), sorted(folder.rglob("debug.txt"))


@pytest.fixture(scope="module")
def binary():
    found = game()
    # (linux-tests unpacks the linux build: there it must run, never skip)
    if not found and os.environ.get("GITHUB_ACTIONS") and sys.platform.startswith("linux"):
        pytest.fail("no build/linux/chupathingy in linux-tests")
    if not found:
        pytest.skip("no built game (LOG_ROOT_GAME, or build/macos, linux64, freebsd64 or linux)")
    return found


def test_named_save_root_gets_the_log(binary, tmp_path):
    saves = tmp_path / "saves"
    home, data, said, logs = run_game(binary, tmp_path, saves)
    assert said == saves / "logs"
    if is_64bit(binary):
        assert logs == [saves / "logs" / "debug.txt"], logs
        assert "save root" in logs[0].read_text(errors="replace")
    else:
        assert all(path == saves / "logs" / "debug.txt" for path in logs), logs
    assert not list(home.rglob("debug.txt"))
    assert not (data / "logs" / "debug.txt").exists()


def test_without_a_save_root_the_log_stays_beside_maps(binary, tmp_path):
    home, data, said, logs = run_game(binary, tmp_path, None)
    assert said == data / "logs"
    if is_64bit(binary):
        assert data / "logs" / "debug.txt" in logs, logs
