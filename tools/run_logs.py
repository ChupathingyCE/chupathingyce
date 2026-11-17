"""Where a test run's copy of the game wrote its log (debug.txt), for the
harnesses that start copies with a data root and a save root of their own
(crossplay_test.py, item_sync_check.py, test_net43_host.py, shader_keys.py).

Since #1148 a desktop build with a save root named (HALO_SAVE_ROOT) writes
its logs in <save root>/logs/; older releases (which crossplay_test.py still
plays against) wrote <data root>/logs/, and OpenCE <data root> itself. The
first that exists is the run's."""
import os


def debug_log_paths(save_root, data_root):
    """The places a run's debug.txt may be, newest layout first."""
    return [os.path.join(str(save_root), "logs", "debug.txt"),
            os.path.join(str(data_root), "logs", "debug.txt"),
            os.path.join(str(data_root), "debug.txt")]


def find_debug_log(save_root, data_root):
    """The run's debug.txt: the first of debug_log_paths that exists, else
    None."""
    for path in debug_log_paths(save_root, data_root):
        if os.path.exists(path):
            return path
    return None


def read_debug_log(save_root, data_root):
    """The run's debug.txt as text ("" when there is none)."""
    path = find_debug_log(save_root, data_root)
    if not path:
        return ""
    with open(path, "rb") as file:
        return file.read().decode("utf-8", "replace")
