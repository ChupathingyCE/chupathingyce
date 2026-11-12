"""Where debug.txt goes (#1148): on the desktops a save root that is named
(paths.saves, HALO_SAVE_ROOT: a test's, a second copy's) gets the logs too,
in its logs/, so that copy never writes into the player's log; without one
they stay in logs/ beside maps/, as before. paths.logs (HALO_LOG_ROOT) still
wins. Android, iOS, the consoles and the dedicated server keep logs/ beside
maps/ (their apps and scripts name a save root every time).

The rule itself is port_folder_logs_override (port/linux/src/port_folders.c,
tested by tools/test_port_folders.py); here, that every writer of the logs
folder uses it, and a run of a built game (LOG_ROOT_GAME, else the macOS or
Linux build in build/, skipped without one) with HOME, HALO_SAVE_ROOT and
HALO_DATA_ROOT in a scratch folder: its debug.txt lands in the save root's
logs/, nothing under HOME or beside maps/, and without HALO_SAVE_ROOT in
the data root's logs/ as before."""
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
    # (platform_log before the logs folder is known: from the environment,
    # as config.toml may still be being read)
    kernel = function_body((SOURCE / "xbox_kernel.c").read_text(), "void platform_log(const char *format, ...)\n{")
    early = kernel.index('port_folder_logs_override(getenv("HALO_LOG_ROOT"),')
    assert 'PLATFORM_LOGS_FOLLOW_SAVES ? getenv("HALO_SAVE_ROOT")' in kernel
    assert early < kernel.index("platform_app_folder(folder")


def test_only_the_desktops_follow():
    platform = (SOURCE / "platform.h").read_text()
    gate = platform[platform.index("#if defined(HALO_ANDROID) || defined(HALO_SERVER)"):]
    gate = gate[:gate.index("#endif")]
    for name in ("HALO_ANDROID", "HALO_SERVER", "HALO_XBOX_CONSOLE", "HALO_CONSOLE_GL_LAYER",
                 "HALO_CONSOLE_CONFIG_IN_DATA_ROOT"):
        assert f"defined({name})" in gate
    assert gate.index("#define PLATFORM_LOGS_FOLLOW_SAVES 0") < gate.index("#define PLATFORM_LOGS_FOLLOW_SAVES 1")


def game():
    named = os.environ.get("LOG_ROOT_GAME")
    if named:
        return Path(named)
    if sys.platform == "darwin":
        candidates = [ROOT / "build" / "macos" / "ChupathingyCE.app" / "Contents" / "MacOS" / "chupathingy"]
    elif sys.platform.startswith("linux") or sys.platform.startswith("freebsd"):
        candidates = [ROOT / "build" / "linux64" / "chupathingy", ROOT / "build" / "freebsd64" / "chupathingy"]
    else:
        candidates = []
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    return None


def run_game(binary, folder, save_root):
    """Starts the game in folder (HOME, the data root, and save_root if
    given), waits for its log to say where the saves are, and stops it."""
    home = folder / "home"
    data = folder / "data"
    (data / "maps").mkdir(parents=True)
    home.mkdir()
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith("HALO_") and key not in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "APPDATA")}
    environment.update({"HOME": str(home), "HALO_DATA_ROOT": str(data), "SDL_VIDEO_DRIVER": "dummy",
                        "SDL_AUDIO_DRIVER": "dummy", "HALO_UPDATE_AUTO": "0"})
    if save_root:
        environment["HALO_SAVE_ROOT"] = str(save_root)
    process = subprocess.Popen([str(binary)], cwd=str(folder), env=environment, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, start_new_session=True)
    try:
        deadline = time.time() + 30
        while time.time() < deadline and process.poll() is None:
            if any("save root" in path.read_text(errors="replace") for path in folder.rglob("debug.txt")):
                break
            time.sleep(0.2)
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
        process.wait()
    return home, data, sorted(folder.rglob("debug.txt"))


@pytest.fixture(scope="module")
def binary():
    found = game()
    if not found:
        pytest.skip("no built game (LOG_ROOT_GAME, or build/macos or build/linux64)")
    return found


def test_named_save_root_gets_the_log(binary, tmp_path):
    saves = tmp_path / "saves"
    home, data, logs = run_game(binary, tmp_path, saves)
    assert logs == [saves / "logs" / "debug.txt"], logs
    assert "save root" in logs[0].read_text(errors="replace")
    assert not list(home.rglob("debug.txt"))
    assert not (data / "logs" / "debug.txt").exists()


def test_without_a_save_root_the_log_stays_beside_maps(binary, tmp_path):
    home, data, logs = run_game(binary, tmp_path, None)
    assert data / "logs" / "debug.txt" in logs, logs
