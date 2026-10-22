"""Tests of network versions 30 to 32: the C tests of the rules every machine
shares (port/linux/tests/network_rules_test.c: the aim assist policy
message's and the input entry's bytes, the stamp window, the elevator's
correction and riders, the policy's reach), built with the build machine's C
compiler, and what is in the sources that the C tests cannot reach: the
pregame messages' table and numbers, the message 90 number, the host's sends
and the client's drops, and the settings that carry the policy."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
MESSAGES_H = ROOT / "source/networking/network_messages.h"
MESSAGES_C = ROOT / "source/networking/network_messages.c"
SERVER_MANAGER_C = ROOT / "source/networking/network_server_manager.c"
DISTRIBUTED_H = ROOT / "port/linux/game/network_distributed.h"
DISTRIBUTED_C = ROOT / "port/linux/game/network_distributed.c"


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def test_rules(tmp_path):
    """the rules' C tests, warnings as errors"""
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    binary = tmp_path / "network_rules_test"
    build = subprocess.run(
        [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
         str(ROOT / "port/linux/tests/network_rules_test.c"), str(ROOT / "port/linux/game/network_rules.c"),
         "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "network_rules_test: ok" in result.stdout


def _field_sizes():
    """each game message's packet fields' bytes, by name (pad 0, bytes 1,
    shorts 2, longs 4), from network_messages.c's field lists"""
    sizes = {}
    source = MESSAGES_C.read_text(encoding="utf-8")
    for fields, name in re.findall(r"\t\{\n((?:\t\t.*\n)*?)\t\},\n\tNETWORK_GAME_MESSAGE_DEFINITION\((\w+),", source):
        sizes[name] = sum(int(count) * {"pad": 0, "bytes": 1, "shorts": 2, "longs": 4}.get(kind, 0)
                          for kind, count in re.findall(r"_data_packet_field_(\w+), (\d+)", fields))
        sizes[name] += sum(int(count) for kind, count in re.findall(r"_data_packet_field_(pad), (\d+)", fields))
    return sizes


def test_pregame_messages_numbers_and_payloads():
    """begin game 8, graceful exit 9, pregame keep-alive 10, postgame
    keep-alive 11; begin game a 16-bit value and 2 bytes of padding (4 bytes),
    graceful exit and the pregame keep-alive 32 bits (4), the postgame
    keep-alive 16 (2), each message with its own field list"""
    header = MESSAGES_H.read_text(encoding="utf-8")
    enum = header[header.index("enum network_game_message_type"):]
    names = re.findall(r"\t_message_(\w+)", enum[:enum.index("};")])
    assert names.index("server_begin_game") == 8
    assert names.index("server_graceful_game_exit_pregame") == 9
    assert names.index("server_pregame_keep_alive") == 10
    assert names.index("server_postgame_keep_alive") == 11
    sizes = _field_sizes()
    assert sizes["server_begin_game"] == 4
    assert sizes["server_graceful_game_exit_pregame"] == 4
    assert sizes["server_pregame_keep_alive"] == 4
    assert sizes["server_postgame_keep_alive"] == 2
    source = MESSAGES_C.read_text(encoding="utf-8")
    # (the structs the table sizes: 4, 4, 4 and 2 bytes)
    for name, size in (("server_begin_game", "0x04"), ("server_graceful_game_exit_pregame", "0x04"),
                       ("server_pregame_keep_alive", "0x04"), ("server_postgame_keep_alive", "0x02")):
        assert f"DEFINE_NETWORK_GAME_MESSAGE(message_{name}, {size});" in source, name


def test_keep_alive_is_sent_zero_and_taken_whatever_it_says():
    """the host's keep-alive carries 0 in all four bytes; the client reads the
    value and ignores it (an older sender's last two bytes were its stack)"""
    server = SERVER_MANAGER_C.read_text(encoding="utf-8")
    assert "struct message_server_pregame_keep_alive message_packet = { 0 };" in server
    client = (ROOT / "source/networking/network_client_message_handler.c").read_text(encoding="utf-8")
    handler = client[client.index("static boolean network_game_client_handle_message_server_pregame_keep_alive(\n"):]
    handler = handler[:handler.index("network_game_client_handle_message_server_postgame_keep_alive")]
    assert "keep_alive.unused" not in handler


def test_aim_assist_policy_is_message_90():
    header = DISTRIBUTED_H.read_text(encoding="utf-8")
    assert re.search(r"_distributed_message_aim_assist_policy = 90,", header)
    source = DISTRIBUTED_C.read_text(encoding="utf-8")
    # the host sends it to a machine that is ready and to all when it changes,
    # reliably; a client takes it from the host's stream only; a host takes none
    assert "distributed_tell_aim_assist(machine_index);" in source
    assert "_distributed_to_clients_reliably" in source[source.index("static void distributed_send_aim_assist"):]
    handler = source[source.index("case _distributed_message_aim_assist_policy:\n\t{"):]
    handler = handler[:handler.index("break;")]
    assert "machine_index == NONE" in handler and "_game_connection_network_client" in handler
    assert "!main_menu_is_active()" in handler and "network_connection_last_read_was_unreliable()" in handler
    # a client starts every game blocked, when its host's version has the policy
    assert "network_game_client_host_network_version() >= AIM_ASSIST_POLICY_NETWORK_VERSION;" in source
    assert "#define AIM_ASSIST_POLICY_NETWORK_VERSION 32" in source


def test_policy_setting_is_documented():
    config = (ROOT / "port/linux/src/port_config.c").read_text(encoding="utf-8")
    assert re.search(r'"network\.block_mouse_aim_assist", _config_boolean, "false", "HALO_NET_BLOCK_MOUSE_AIM_ASSIST"', config)
    assert "`network.block_mouse_aim_assist` | `false` |" in (ROOT / "port/linux/README.md").read_text(encoding="utf-8")
    assert "`HALO_NET_BLOCK_MOUSE_AIM_ASSIST` | `false` |" in (ROOT / "server/docs/settings.md").read_text(encoding="utf-8")


def test_the_version_and_the_wire():
    limits = (ROOT / "port/linux/include/halo_port_limits.h").read_text(encoding="utf-8")
    delta = (ROOT / "port/linux/include/delta.h").read_text(encoding="utf-8")
    assert "#define HALO_PORT_NETWORK_VERSION 32\n" in limits
    assert '#define DELTA_WIRE "chupa-32a"' in delta
    for number in (30, 31, 32):
        assert re.search(rf'X\({number}, "build-\d+", additive\)', delta)


def test_elevator_carry_of_the_tick_is_for_network_games_only():
    """a local game (campaign, single player, film) carries an elevator's
    riders as the game always did; the tick's own carry is sampled and applied
    only in a network game, host or client"""
    source = (ROOT / "source/devices/device_machines.c").read_text(encoding="utf-8")
    networked = source[source.index("boolean machine_elevator_networked("):]
    networked = networked[:networked.index("\n}")]
    assert "_game_connection_network_client" in networked and "_game_connection_network_server" in networked
    assert "_game_connection_local" not in networked and "film" not in networked
    sample = source[source.index("boolean machine_elevator_sample("):]
    sample = sample[:sample.index("\n}")]
    assert "!machine_elevator_networked()" in sample.split("return FALSE")[0]
    update = source[source.index("boolean machine_update("):]
    update = update[:update.index("\n}")]
    legacy = update[update.index("if (!machine_elevator_networked()"):]
    assert "previous = machine->machine.elevator_position" in legacy
    assert legacy.index("machine_elevator_carry_riders") < legacy.index("_device_position_changed_bit")
    # objects.c samples through the same gate (the sample is refused locally)
    assert "machine_elevator_sample(object_index, &elevator_sample)" in \
        (ROOT / "source/objects/objects.c").read_text(encoding="utf-8")
