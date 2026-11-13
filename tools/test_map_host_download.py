"""A missing Custom Edition map downloaded from the game's host, peer to peer
(port/linux/src/map_download.c, "From the host"): network.map_downloads_from_host
and its order after the repositories (map_host_download.c), the quick
checks (map_quick_check.c), the quarantine folder and the move into maps_ce,
and the blacklist on it, through port/linux/tests/map_host_download_test.c,
built with the build machine's C compiler and the port's zlib and
Monocypher, under AddressSanitizer and UndefinedBehaviorSanitizer where it
has them. Then the game's wiring of it, read from the sources: off by
default, the consoles and the dedicated server without it, the blacklist
asked before a byte comes, the file written into the quarantine folder.

The fuzz target for the quick checks is tools/tests/host_fuzz/map_quick_check
(tools/test_host_fuzz.py).

With HALO_MAP_HOST_E2E (a game binary) and HALO_MAP_HOST_E2E_DATA (a data
folder of links to your own maps), test_dedicated_server_shares_the_map
runs a dedicated server with network.map_share on a Custom Edition map and a
client without it, network.map_downloads_from_host "always", which joins,
gets the map from the host and joins again. It needs loopback addresses
127.0.0.2 and .3 (Linux has them; macOS needs `sudo ifconfig lo0 alias`)."""
import os
import re
import shutil
import signal
import struct
import subprocess
import time
from pathlib import Path

import pytest

from tools import fuzz_support  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"
GAME = ROOT / "port" / "linux" / "game"
ZLIB = ROOT / "port" / "third_party" / "zlib"
ZLIB_SOURCES = ["adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c"]
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"
TEST = ROOT / "port" / "linux" / "tests" / "map_host_download_test.c"
SANITIZERS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
BASE = 0x40440000
MB = 1 << 20

# map_download.h's numbers
FAILED, DONE, CANCELLED, RUNNING = 3, 2, 4, 1
COMMUNITY, CHUPATHINGY, HOST = 0, 1, 2
OFF, ASK, ALWAYS = 0, 1, 2
NONE, STEP_ASK, STEP_START = 0, 1, 2


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    directory = tmp_path_factory.mktemp("map_host_download")
    binary = directory / "map_host_download_test"
    objects = []
    sanitize = SANITIZERS
    for source in [ZLIB / name for name in ZLIB_SOURCES] + [MONOCYPHER / "monocypher.c"]:
        obj = directory / (source.name + ".o")
        result = subprocess.run([cc, "-c", "-O1", "-g", "-w", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg",
                                 str(source), "-o", str(obj), *sanitize], capture_output=True, text=True)
        if result.returncode != 0 and sanitize:
            fuzz_support.fallback("no sanitizer runtime")
            sanitize = []
            result = subprocess.run([cc, "-c", "-O1", "-g", "-w", "-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg",
                                     str(source), "-o", str(obj)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        objects.append(obj)
    result = subprocess.run(
        [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", "-g", *sanitize, "-DZ_PREFIX",
         "-Dz_errmsg=z_port_errmsg", f"-I{SOURCE}", f"-I{ZLIB}", f"-I{MONOCYPHER}", str(TEST),
         str(SOURCE / "map_host_download.c"), str(SOURCE / "map_quick_check.c"), str(SOURCE / "map_archive.c"),
         *map(str, objects), "-o", str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return binary


def run(harness, *arguments):
    result = subprocess.run([str(harness), *map(str, arguments)], capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()


def make_map(name="testmap", size=0x4000, checksum=0x1234ABCD, tag_offset=0x800, tag_size=0x1000, tag_count=3,
             instances=0x28, scenario=0, handle_index=None, name_pointer=0x100, data_pointer=0x200,
             model_offset=0x1800, model_size=0x100, vertex_size=0x80, signature=0x74616773, bsp_data=0,
             indexed_group=b"mtib", unterminated=False, version=609):
    """a small Custom Edition multiplayer map whose tag data is whole: a
    scenario, a bitmap held by bitmaps.map and a structure BSP (cache
    files' layout, as ce_map_checks.c reads it)"""
    data = bytearray(size)
    data[0:4] = b"daeh"
    struct.pack_into("<I", data, 0x4, version)
    struct.pack_into("<I", data, 0x8, size)
    struct.pack_into("<I", data, 0x10, tag_offset)
    struct.pack_into("<I", data, 0x14, tag_size)
    data[0x20:0x20 + len(name)] = name.encode("ascii")
    data[0x40:0x40 + 10] = b"01.00.00.0"
    struct.pack_into("<H", data, 0x60, 1)
    struct.pack_into("<I", data, 0x64, checksum)
    data[0x7FC:0x800] = b"toof"
    tags = bytearray(max(tag_size, 0x1000))
    struct.pack_into("<IIIIIIIIII", tags, 0, BASE + instances, 0xE1740000 | scenario, 0, tag_count, 1, model_offset,
                     1, vertex_size, model_size, signature)
    groups = [(b"rncs", 0), (indexed_group, 1), (b"psbs", 0)]
    for index in range(min(tag_count, 3)):
        group, indexed = groups[index]
        at = instances + index * 0x20
        if at + 0x20 > len(tags):
            break
        tags[at:at + 4] = group
        handle = index if handle_index is None or index != 1 else handle_index
        data_address = bsp_data if group == b"psbs" else (5 if indexed else BASE + data_pointer)
        struct.pack_into("<IIIII", tags, at + 0xC, 0xE1740000 | handle, BASE + name_pointer + index * 0x20,
                         data_address, indexed, 0)
        name_at = name_pointer + index * 0x20
        if 0 <= name_at < len(tags) - 0x20:
            text = b"x" * 0x20 if unterminated else f"tags\\tag{index}".encode() + b"\0"
            tags[name_at:name_at + len(text)] = text
    if unterminated:
        tags[name_pointer:] = b"x" * (len(tags) - name_pointer)
    count = max(0, min(len(tags), size - tag_offset))
    data[tag_offset:tag_offset + count] = tags[:count]
    return bytes(data)


def check(harness, tmp_path, data, name="testmap", limit=512 * MB):
    path = tmp_path / "check.map"
    path.write_bytes(data)
    return run(harness, "check", path, name, limit)


# ---------- the setting and the order


@pytest.mark.parametrize("text,word", [("off", "off"), ("ask", "ask"), ("always", "always"), ("ALWAYS", "always"),
                                       ("Ask", "ask"), ("", "off"), ("never", "off"), ("yes", "off"),
                                       ("always ", "off")])
def test_setting_words(harness, text, word):
    assert run(harness, "setting", text) == word


@pytest.mark.parametrize("setting,state,source,blocked,step", [
    # (the host only after the repositories failed, as the player allows)
    (OFF, FAILED, COMMUNITY, 0, NONE),
    (ASK, FAILED, COMMUNITY, 0, STEP_ASK),
    (ALWAYS, FAILED, COMMUNITY, 0, STEP_START),
    (ASK, FAILED, CHUPATHINGY, 0, STEP_ASK),
    (ALWAYS, FAILED, CHUPATHINGY, 0, STEP_START),
    # (never after the repositories gave it, the player cancelled, or the
    # blacklist refused it)
    (ALWAYS, DONE, COMMUNITY, 0, NONE),
    (ALWAYS, DONE, CHUPATHINGY, 0, NONE),
    (ALWAYS, CANCELLED, COMMUNITY, 0, NONE),
    (ALWAYS, RUNNING, COMMUNITY, 0, NONE),
    (ALWAYS, FAILED, COMMUNITY, 1, NONE),
    (ASK, FAILED, CHUPATHINGY, 1, NONE),
    # (and never twice: a download from the host that failed ends there)
    (ALWAYS, FAILED, HOST, 0, NONE),
    (ASK, FAILED, HOST, 0, NONE),
])
def test_host_comes_last_and_only_as_allowed(harness, setting, state, source, blocked, step):
    assert int(run(harness, "step", setting, state, source, blocked)) == step


@pytest.mark.parametrize("megabytes,cap", [(512, 512 * MB), (0, 512 * MB), (-5, 512 * MB), (1, 16 * MB),
                                           (16, 16 * MB), (768, 768 * MB), (1024, 768 * MB), (4000, 768 * MB), (100, 100 * MB)])
def test_size_cap(harness, megabytes, cap):
    assert int(run(harness, "limit", megabytes)) == cap


# ---------- the quick checks


def test_quick_checks_pass_a_whole_map(harness, tmp_path):
    assert check(harness, tmp_path, make_map()) == "ok"
    assert check(harness, tmp_path, make_map(), "TESTMAP") == "ok"


@pytest.mark.parametrize("kwargs,name,why", [
    ({}, "..", "name"),
    ({}, "a/b", "name"),
    ({}, "con", "name"),
    ({"name": "bloodgulch"}, "bloodgulch", "name"),
    ({"name": "bitmaps"}, "bitmaps", "name"),
    ({"version": 7}, "testmap", "version 7"),
    ({"name": "other"}, "testmap", "named other inside"),
    ({"tag_size": 0x10}, "testmap", "too small"),
    ({"tag_offset": 0x3800, "tag_size": 0x1000}, "testmap", "tag data"),
    ({"signature": 0}, "testmap", "no tag header"),
    ({"tag_count": 0}, "testmap", "0 tags"),
    ({"tag_count": 0x10000}, "testmap", "65536 tags"),
    ({"instances": 0xFF0}, "testmap", "list of tags"),
    ({"instances": 0x2A}, "testmap", "list of tags"),
    ({"handle_index": 5}, "testmap", "another tag's handle"),
    ({"name_pointer": 0x2000}, "testmap", "name is not in"),
    ({"unterminated": True}, "testmap", "does not end"),
    ({"data_pointer": 0x2000}, "testmap", "data is not in"),
    ({"bsp_data": BASE}, "testmap", "structure BSP"),
    ({"indexed_group": b"rncs"}, "testmap", "resource map"),
    ({"scenario": 1}, "testmap", "scenario"),
    ({"scenario": 9}, "testmap", "scenario"),
    ({"model_offset": 0x3F80}, "testmap", "model data"),
    ({"vertex_size": 0x200}, "testmap", "model data"),
])
def test_quick_checks_refuse(harness, tmp_path, kwargs, name, why):
    output = check(harness, tmp_path, make_map(**kwargs), name)
    assert output.startswith("refused:") and why in output, output


def test_quick_checks_size_cap(harness, tmp_path):
    output = check(harness, tmp_path, make_map(size=0x4000), limit=0x3FFF)
    assert output.startswith("refused:") and "more than" in output, output
    assert check(harness, tmp_path, make_map(size=0x4000), limit=0x4000) == "ok"


def test_quick_checks_never_crash_on_damage(harness, tmp_path):
    """random bytes changed in a whole map: refused or passed, never a
    crash (the fuzz target goes further)"""
    import random
    generator = random.Random(1109)
    pristine = make_map()
    for round_index in range(150):
        data = bytearray(pristine)
        for _ in range(generator.randint(1, 16)):
            data[generator.randrange(0x800, 0x1100)] = generator.randrange(256)
        output = check(harness, tmp_path, bytes(data))
        assert output == "ok" or output.startswith("refused:"), output


# ---------- the quarantine folder and the move into maps_ce


def quarantine(tmp_path, data, name="testmap"):
    root = tmp_path / "data"
    (root / "maps_quarantine").mkdir(parents=True, exist_ok=True)
    (root / "maps_ce").mkdir(parents=True, exist_ok=True)
    (root / "maps_quarantine" / f"{name}.map").write_bytes(data)
    (root / "maps_quarantine" / f"{name}.map.part").write_bytes(b"left over")
    return root


def version_of(harness, path):
    text = run(harness, "hash", path)
    return "00000001" if text[:8] == "00000000" else text[:8]


def test_quarantine_moves_a_checked_map_into_maps_ce(harness, tmp_path):
    root = quarantine(tmp_path, make_map())
    version = version_of(harness, root / "maps_quarantine" / "testmap.map")
    assert run(harness, "install", root, "testmap", version, "file", 512 * MB) == "ok testmap"
    assert (root / "maps_ce" / "testmap.map").read_bytes() == make_map()
    assert not (root / "maps_quarantine" / "testmap.map").exists()


def test_quarantine_accepts_the_hosts_checksum_as_its_version(harness, tmp_path):
    root = quarantine(tmp_path, make_map(checksum=0x0BADF00D))
    assert run(harness, "install", root, "testmap", "0badf00d", "file", 512 * MB) == "ok testmap"


def test_quarantine_never_replaces_a_map_there(harness, tmp_path):
    root = quarantine(tmp_path, make_map())
    (root / "maps_ce" / "testmap.map").write_bytes(b"the player's own")
    hash_text = run(harness, "hash", root / "maps_quarantine" / "testmap.map")
    assert run(harness, "install", root, "testmap", "0", "file", 512 * MB) == f"ok testmap_{hash_text[:8]}"
    assert (root / "maps_ce" / "testmap.map").read_bytes() == b"the player's own"
    assert (root / "maps_ce" / f"testmap_{hash_text[:8]}.map").exists()


def test_quarantine_hashed_name_goes_in_by_its_base(harness, tmp_path):
    data = make_map()
    probe = tmp_path / "probe.map"
    probe.write_bytes(data)
    digits = run(harness, "hash", probe)[:8]
    root = quarantine(tmp_path, data, f"testmap_{digits}")
    assert run(harness, "install", root, f"testmap_{digits}", "0", "file", 512 * MB) == "ok testmap"
    root2 = quarantine(tmp_path / "wrong", data, "testmap_00000000")
    output = run(harness, "install", root2, "testmap_00000000", "0", "file", 512 * MB)
    assert output.startswith("refused:") and "hash doesn't begin" in output


@pytest.mark.parametrize("case", ["damaged", "offered", "version", "cap", "missing"])
def test_quarantine_refusals_delete_the_file(harness, tmp_path, case):
    data = make_map(signature=0) if case == "damaged" else make_map()
    root = quarantine(tmp_path, data)
    if case == "missing":
        (root / "maps_quarantine" / "testmap.map").unlink()
    offered = "ab" * 32 if case == "offered" else "file"
    version = "12345678" if case == "version" else "0"
    limit = 0x100 if case == "cap" else 512 * MB
    output = run(harness, "install", root, "testmap", version, offered, limit)
    assert output.startswith("refused:"), output
    assert not (root / "maps_quarantine" / "testmap.map").exists()
    assert not (root / "maps_quarantine" / "testmap.map.part").exists()
    assert not list((root / "maps_ce").iterdir())


def test_blacklist_holds_on_maps_from_the_host(harness, tmp_path):
    root = quarantine(tmp_path, make_map())
    output = run(harness, "install", root, "testmap", "0", "file", 512 * MB, "block")
    assert output == "blocked: it is on the blacklist: crashes games"
    assert not (root / "maps_quarantine" / "testmap.map").exists()
    assert not list((root / "maps_ce").iterdir())


def test_install_refuses_names_with_paths(harness, tmp_path):
    root = quarantine(tmp_path, make_map())
    for name in ("../testmap", "..", "maps_ce\\testmap", "bloodgulch"):
        output = run(harness, "install", root, name, "0", "file", 512 * MB)
        assert output.startswith("refused:") and "name" in output, (name, output)
    assert (root / "maps_quarantine" / "testmap.map").exists()


# ---------- the game's wiring, read from its sources


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def test_setting_is_off_by_default_and_documented():
    config = read("port/linux/src/port_config.c")
    assert re.search(r'\{ "network\.map_downloads_from_host", _config_string, "\\"off\\""', config)
    assert '{ "network.map_downloads_from_host", { "off", "ask", "always", NULL }, "\\"off\\"" }' in config
    assert re.search(r'\{ "network\.map_downloads_from_host_limit", _config_integer, "512"', config)
    for doc in ("docs/maps.md", "docs/settings.md", "port/linux/README.md"):
        assert "network.map_downloads_from_host" in read(doc), doc


def test_consoles_and_the_server_leave_it_out():
    features = read("port/linux/include/halo_console_features.h")
    assert ("#if defined(HALO_GAME_BROWSER) && !defined(HALO_CONSOLE_NO_HOST_MAP_DOWNLOADS) && "
            "!defined(HALO_SERVER)\n#define HALO_HOST_MAP_DOWNLOADS 1") in features
    config = read("port/linux/src/port_config.c")
    block = config[config.index("#ifdef HALO_HOST_MAP_DOWNLOADS\n\t{ \"network.map_downloads_from_host\""):]
    assert "_platform_server" not in block[:block.index("#endif")]
    client = read("port/linux/game/map_share_client.c")
    assert client.count("#ifdef HALO_HOST_MAP_DOWNLOADS") >= 5


def test_the_disclaimer_is_shown_wherever_always_can_be_picked():
    header = read("port/linux/src/map_download.h")
    assert '#define MAP_HOST_DISCLAIMER_FIRST "This map comes straight from the host, not from us,"' in header
    assert '#define MAP_HOST_DISCLAIMER_SECOND "and we haven\'t checked it."' in header
    screen = read("port/linux/game/browser_screen.c")
    asking = screen[screen.index("\tcase _offer_asking_host:\n\t{"):]
    asking = asking[:asking.index("break;")]
    assert "MAP_HOST_DISCLAIMER_FIRST" in asking and "MAP_HOST_DISCLAIMER_SECOND" in asking
    assert "map_download_host_set_setting(MAP_HOST_DOWNLOADS_ALWAYS)" in screen
    launcher = read("port/linux/src/launcher.c")
    assert '"network.map_downloads_from_host"))' in launcher and "MAP_HOST_DISCLAIMER_SETTING" in launcher
    import sys
    sys.path.insert(0, str(ROOT / "tools"))
    import port_settings
    rows = [row for row in port_settings.SCREENS["network_setup"]["rows"]
            if row[1] == "network.map_downloads_from_host"]
    assert rows and "not us" in rows[0][3] and "checked" in rows[0][3]
    assert [value for _, value in rows[0][2]] == ["off", "ask", "always"]


def test_blacklist_is_asked_before_a_byte_comes():
    client = read("port/linux/game/map_share_client.c")
    offer = client[client.index("fetch.offered = TRUE;"):]
    assert offer.index("map_download_host_refused(fetch.hash") < offer.index("fetch.piece_count = map_share_piece_count")


def test_the_file_goes_into_quarantine_not_maps_ce():
    header = read("port/linux/src/map_host_download.h")
    assert '#define MAP_HOST_QUARANTINE_FOLDER "d:\\\\maps_quarantine"' in header
    client = read("port/linux/game/map_share_client.c")
    assert "map_download_host_paths(fetch.wanted, part, whole" in client
    assert "maps_ce" not in client[client.index("map_download_host_paths(fetch.wanted"):][:400]


def test_no_address_in_what_the_download_logs():
    client = read("port/linux/game/map_share_client.c")
    download = read("port/linux/src/map_download.c")
    for text in (client, download):
        for line in re.findall(r'platform_log\("map download:[^;]*;', text):
            assert "address" not in line and "%u.%u" not in line, line


def test_screen_asks_the_host_only_after_the_repositories():
    screen = read("port/linux/game/browser_screen.c")
    follow = screen[screen.index("static void follow_download("):]
    follow = follow[:follow.index("\n}\n")]
    failed = follow[follow.index("case MAP_DOWNLOAD_FAILED:"):]
    assert "map_download_host_next(&browser_screen.offer_progress)" in failed
    assert "map_download_host_next" not in follow[:follow.index("case MAP_DOWNLOAD_FAILED:")]


def test_the_fetch_is_driven_every_frame_and_never_left_alone():
    """map_share_fetching() is read by the game's networking whenever a
    player's fetch is set, so it is driven from the main loop whatever screen
    is up, and cancelled once the screen no longer shows it"""
    main = read("source/main/main.c")
    assert ("#ifdef HALO_HOST_MAP_DOWNLOADS\n\t\t\t/* a player's download of a map from the host, whatever screen is\n"
            "\t\t\tup (port/linux/game/browser_screen.c) */\n\t\t\tMAIN_STAGE(\"browser_screen_host_download_update\");\n"
            "\t\t\tbrowser_screen_host_download_update();") in main
    screen = read("port/linux/game/browser_screen.c")
    hook = screen[screen.index("void browser_screen_host_download_update("):]
    hook = hook[:hook.index("\n}\n")]
    assert "(!browser_screen.active || browser_screen.offer != _offer_downloading)" in hook
    assert hook.index("map_download_cancel();") < hook.index("map_share_player_update();")
    assert "map_download_host_sweep();" in hook
    # (the screen itself only reads its progress now)
    assert screen.count("map_share_player_update();") == 1
    client = read("port/linux/game/map_share_client.c")
    update = client[client.index("void map_share_player_update("):]
    assert update.index("map_download_host_cancelled()") < update.index("switch (fetch.state)")



def test_the_leave_resets_only_the_fetchs_own_network():
    """#1041: the player's fetch waits a second after it ends (LEAVE_DELAY)
    before it starts the network searching again; a game hosted, joined or
    the network reset from the menus in that second is left alone"""
    client = read("port/linux/game/map_share_client.c")
    update = client[client.index("void map_share_player_update("):]
    update = update[:update.index("\n}\n")]
    leave = update[update.index("case _fetch_leaving:"):]
    leave = leave[:leave.index("break;")]
    assert leave.index("if (fetch_network_still_ours())") < leave.index("ui_online_games_start_network();")
    assert client.count("ui_online_games_start_network();") == 2
    finish = client[client.index("static void fetch_finish("):]
    player = finish[finish.index("if (fetch.player)"):]
    assert player.index("fetch_note_left_host();") < player.index("fetch_state(_fetch_leaving);")
    guard = client[client.index("static boolean fetch_network_still_ours("):]
    guard = guard[:guard.index("\n}\n")]
    # a game of this machine's, no client, another host, or a client reset
    # after the fetch had joined: not the fetch's
    assert "if (!client || global_network_game_server_get())\n\t\treturn FALSE;" in guard
    assert "if (!fetch.left_connected)\n\t\treturn state == 0;" in guard
    assert "if (state < 1)\n\t\treturn FALSE;" in guard
    assert "!memcmp(&host, &fetch.left_host, sizeof(host))" in guard

def test_no_host_download_without_the_blacklist():
    download = read("port/linux/src/map_download.c")
    allowed = download[download.index("int map_download_host_allowed("):]
    allowed = allowed[:allowed.index("\n}\n")]
    assert "if (map_repo_index_held())\n\t\treturn 1;" in allowed
    begin = download[download.index("int map_download_host_begin("):]
    assert "!map_download_host_allowed(why, (int)sizeof(why))" in begin[:600]
    screen = read("port/linux/game/browser_screen.c")
    start = screen[screen.index("static void start_host_download("):]
    assert start.index("map_download_host_allowed(") < start.index("map_share_player_start(")
    repo = read("port/linux/src/map_repo.c")
    assert "held = map_repo.index != NULL;" in repo


def test_a_blacklist_refusal_before_the_transfer_is_marked_blocked():
    client = read("port/linux/game/map_share_client.c")
    refusal = client[client.index("if (map_download_host_refused(fetch.hash"):]
    assert refusal.index("fetch.blocked = TRUE;") < refusal.index("fetch_finish(")
    assert "fetch.cancelled,\n\t\t\t\tfetch.blocked);" in client
    download = read("port/linux/src/map_download.c")
    assert "finish_blocked(MAP_DOWNLOAD_FAILED, message, blocked);" in download


def test_the_transfer_limit_and_the_setting_agree():
    client = read("port/linux/game/map_share_client.c")
    assert "MAP_LIMIT = 0x30000000," in client
    header = read("port/linux/src/map_quick_check.h")
    assert "MAP_QUICK_CHECK_MAXIMUM_LIMIT_MB = 768," in header
    assert "more than the %d MB any map may be" in client
    assert "16 to 768" in read("port/linux/src/port_config.c")


def test_shared_state_is_read_under_the_lock():
    download = read("port/linux/src/map_download.c")
    cancelled = download[download.index("int map_download_host_cancelled("):]
    cancelled = cancelled[:cancelled.index("\n}\n")]
    assert "pthread_mutex_lock(&map_download.lock);" in cancelled
    thread = download[download.index("static void *host_install_thread("):]
    thread = thread[:thread.index("\n}\n")]
    assert "version = map_download.version;" in thread and "map_download.version" not in thread.replace(
        "version = map_download.version;", "")


def test_listen_hosts_share_slower_unless_set():
    host = read("port/linux/game/map_share_host.c")
    assert "LISTEN_HOST_UPLOAD = 256," in host
    assert ('if (!browser_dedicated() && !config_chosen("network.map_share_upload"))\n'
            "\t\tkilobytes = LISTEN_HOST_UPLOAD;") in host


def test_listen_hosts_share_behind_the_same_setting():
    host = read("port/linux/game/map_share_host.c")
    sharing = host[host.index("static boolean host_sharing("):]
    sharing = sharing[:sharing.index("\n}\n")]
    assert 'config_boolean("network.map_share")' in sharing and "browser_dedicated" not in sharing


# ---------- end to end: a dedicated server shares its map

E2E_BINARY = os.environ.get("HALO_MAP_HOST_E2E")
E2E_DATA = os.environ.get("HALO_MAP_HOST_E2E_DATA")
E2E_MAP = os.environ.get("HALO_MAP_HOST_E2E_MAP", "sneak")
E2E_SECONDS = int(os.environ.get("HALO_MAP_HOST_E2E_SECONDS", "90"))


def e2e_copy(folder, address, other, environment, ce_map=None):
    data = folder / "data"
    for sub in ("home", "saves", "data/maps", "data/maps_ce"):
        (folder / sub).mkdir(parents=True)
    source = Path(E2E_DATA)
    for entry in (source / "maps").glob("*.map"):
        (data / "maps" / entry.name).symlink_to(entry.resolve())
    for name in ("bitmaps.map", "sounds.map", "loc.map"):
        for where in (source / "maps_ce", source / "maps" / "ce"):
            if (where / name).exists():
                (data / "maps_ce" / name).symlink_to((where / name).resolve())
                break
    if ce_map:
        (data / "maps_ce" / f"{E2E_MAP}.map").symlink_to(ce_map.resolve())
    env = {"PATH": "/usr/bin:/bin", "HOME": str(folder / "home"), "HALO_DATA_ROOT": str(data),
           "HALO_SAVE_ROOT": str(folder / "saves"), "HALO_UPDATE_AUTO": "false", "HALO_DISCORD_APPLICATION": "",
           "HALO_NET_ONLINE": "false", "HALO_NET_LIST_GAMES": "false", "HALO_NET_REPORT_GAMES": "false",
           "HALO_NET_BROWSER": "http://127.0.0.1:9", "HALO_NET_JOIN_FROM_CLIPBOARD": "false",
           "HALO_NET_ALLOW_UPNP": "false", "HALO_NET_ADDRESS": address, "HALO_NET_BROADCAST": other,
           "SDL_VIDEO_DRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy", **environment}
    output = open(folder / "out.log", "wb")
    return subprocess.Popen([E2E_BINARY], cwd=data, env=env, stdout=output, stderr=subprocess.STDOUT,
                            start_new_session=True)


@pytest.mark.skipif(not E2E_BINARY or not E2E_DATA, reason="HALO_MAP_HOST_E2E and _DATA not set")
@pytest.mark.parametrize("setting", ["always", "off", "no_index"])
def test_dedicated_server_shares_the_map(tmp_path, setting):
    ce_map = next((path for path in (Path(E2E_DATA) / "maps_ce" / f"{E2E_MAP}.map",
                                     Path(E2E_DATA) / "maps_community" / f"{E2E_MAP}.map") if path.exists()), None)
    if not ce_map:
        pytest.skip(f"no {E2E_MAP}.map")
    playlist = tmp_path / "server" / "data" / "playlists" / "share.txt"
    server = None
    playlist.parent.mkdir(parents=True)
    playlist.write_text(f"{E2E_MAP}@ce slayer\n")
    server = e2e_copy(tmp_path / "server", "127.0.0.2", "127.0.0.3",
                      {"HALO_DEDICATED": "playlists/share.txt", "HALO_NET_MAP_SHARE": "true", "HALO_NET_MAP_SHARE_UPLOAD": "20480",
                       "HALO_NET_MAPS_URL": ""}, ce_map)
    time.sleep(6)
    # (the community repositories asked first: here a port nothing answers
    # on; then Chupathingy maps, whose signed index (its blacklist) the host
    # needs; then the host. "no_index": Chupathingy maps off, so no blacklist,
    # and nothing comes from the host)
    client_env = {"HALO_NETWORK_TEST": "join", "HALO_NET_MAP_DOWNLOADS": "always",
                  "HALO_NET_MAP_DOWNLOADS_FROM_HOST": "always" if setting == "no_index" else setting,
                  "HALO_MAP_REPOSITORIES": "http://127.0.0.1:9"}
    if setting == "no_index":
        client_env["HALO_NET_MAPS_URL"] = ""
    client = e2e_copy(tmp_path / "client", "127.0.0.3", "127.0.0.2", client_env)
    try:
        time.sleep(E2E_SECONDS)
    finally:
        for process in (server, client):
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    log = (tmp_path / "client" / "out.log").read_text(errors="replace")
    server_log = (tmp_path / "server" / "out.log").read_text(errors="replace")
    if "network test: joining" not in log:
        pytest.skip("the client found no host (loopback addresses 127.0.0.2 and .3?)")
    installed = tmp_path / "client" / "data" / "maps_ce" / f"{E2E_MAP}.map"
    if setting == "always" and "blacklist isn't loaded" in log:
        pytest.skip("Chupathingy maps' index couldn't be fetched")
    if setting == "no_index":
        assert "map download: no map from the host: Chupathingy maps' blacklist isn't loaded" in log
        assert "joining the host's game to ask it" not in log
        assert not installed.exists()
    elif setting == "always":
        assert f"map download: joining the host's game to ask it for {E2E_MAP}.map (no player)" in log, log[-4000:]
        assert f"map download: {E2E_MAP}.map from the host passed its checks" in log, log[-4000:]
        assert installed.exists() and installed.read_bytes() == ce_map.read_bytes()
        assert not list((tmp_path / "client" / "data" / "maps_quarantine").glob("*"))
        assert f"map download: {E2E_MAP}.map in place: joining the game again" in log
        assert "map sharing: machine" in server_log
        # (the host's address nowhere in what the client says about it)
        assert not re.search(r"map (download|fetch):.*127\.0\.0\.2", log)
    else:
        assert "joining the host's game to ask it" not in log
        assert not installed.exists()
