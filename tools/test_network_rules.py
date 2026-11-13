"""Tests of network versions 30 to 38: the C tests of the rules every machine
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


def test_shield_effects_are_message_91():
    header = DISTRIBUTED_H.read_text(encoding="utf-8")
    assert re.search(r"_distributed_message_shield_effects = 91,", header)
    source = DISTRIBUTED_C.read_text(encoding="utf-8")
    # a client takes it from the host's stream only; a host takes none; it is
    # stock again at every game
    handler = source[source.index("case _distributed_message_shield_effects:\n\t{"):]
    handler = handler[:handler.index("break;")]
    assert "machine_index == NONE" in handler and "_game_connection_network_client" in handler
    assert "network_connection_last_read_was_unreliable()" in handler
    assert "distributed_shield_effects = 0;" in source


def test_settings_record_by_the_hosts_version():
    """the client takes the record's size by the host's version, makes a newer
    record the older layout, and leaves a game of OpenCE's own game types"""
    source = (ROOT / "source/networking/network_client_message_handler.c").read_text(encoding="utf-8")
    piece = source[source.index("static boolean network_game_client_receive_game_settings_piece("):]
    piece = piece[:piece.index("\n}\n")]
    # (the host's version; the host's own machine's, told none, the number it announces)
    assert "unsigned int host_version = network_game_client_settings_version();" in piece
    assert "network_rules_settings_size_taken(host_version, piece->total_size)" in piece
    helper = source[source.index("static unsigned int network_game_client_settings_version("):]
    assert "global_network_game_server_get() != NULL, (unsigned int)delta_legacy_announce()" in \
        helper[:helper.index("\n}\n")]
    # (and its version is never noted from a record: it stays the announced one)
    manager = (ROOT / "source/networking/network_client_manager.c").read_text(encoding="utf-8")
    assert "network_game_client_joined_version == 0xFFFF && !global_network_game_server_get()" in manager
    assert "network_rules_settings_fold(" in piece and "network_rules_extended_options_sanitize(" in piece
    # (a newer version's layout from the table: the options' bytes by the
    # host's version, the record no larger than the staging's room)
    assert "network_rules_settings_options_size(host_version," in piece
    assert "piece->total_size > NETWORK_GAME_CLIENT_SETTINGS_MAXIMUM" in piece
    assert piece.count("network_game_client_leave_unsupported_game_type(") == 2
    assert "Chupathingy doesn't have yet" in source


def test_policy_setting_is_documented():
    config = (ROOT / "port/linux/src/port_config.c").read_text(encoding="utf-8")
    assert re.search(r'"network\.block_mouse_aim_assist", _config_boolean, "false", "HALO_NET_BLOCK_MOUSE_AIM_ASSIST"', config)
    assert "`network.block_mouse_aim_assist` | `false` |" in (ROOT / "port/linux/README.md").read_text(encoding="utf-8")
    assert "`HALO_NET_BLOCK_MOUSE_AIM_ASSIST` | `false` |" in (ROOT / "server/docs/settings.md").read_text(encoding="utf-8")


def test_the_version_and_the_wire():
    limits = (ROOT / "port/linux/include/halo_port_limits.h").read_text(encoding="utf-8")
    delta = (ROOT / "port/linux/include/delta.h").read_text(encoding="utf-8")
    assert "#define HALO_PORT_NETWORK_VERSION 32\n" in limits
    assert '#define DELTA_WIRE "chupa-44a"' in delta
    for number in (30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44):
        assert re.search(rf'X\({number}, "build-\d+", additive\)', delta)
    assert "#define HALO_PORT_NETWORK_VERSION_MAXIMUM 45\n" in limits


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


def _function(source, name):
    """a function's definition (its name, then its parameters on lines of
    their own), to its closing brace"""
    at = source.index(name + "(\n")
    # (past a prototype, whose parameters end in ");": the definition's in ")\n{")
    while source[source.index(")", at) + 1] == ";":
        at = source.index(name + "(\n", at + 1)
    body = source[at:]
    return body[:body.index("\n}\n")]


def test_epoch_glue_uses_the_rules():
    """network_distributed.c's glue for a layout with a checkpoint epoch goes
    by network_rules.c's tested rules: what a client holds back and takes
    before the host's snapshot ends, the host's taking of a client's ready
    (over its stream alone), the split of a reliable message on its kind's
    entries alone, the queue flushed before a revert's epoch, and a machine
    gone told no boundary"""
    source = DISTRIBUTED_C.read_text(encoding="utf-8")
    assert "network_rules_client_held_back(" in _function(source, "static boolean distributed_held_back")
    send = _function(source, "void distributed_send")
    assert send.index("distributed_held_back(type)") < send.index("distributed_fill_header(")
    assert "distributed_held_back(packer->type)" in _function(source, "static void distributed_packer_flush")
    handle = _function(source, "static void distributed_handle_message")
    assert "network_rules_client_takes_unsynchronized(header.type" in handle
    entry = _function(source, "void network_distributed_handle_message")
    assert "network_rules_host_takes(epoch, distributed_epoch, type, narrow_size" in entry
    assert "distributed_handling_stream_message, FALSE)" in entry
    assert "has_epoch, distributed_epoch, 0, &epoch)" in entry
    wire = _function(source, "static void distributed_wire_send")
    assert "distributed_kind_entry_size(bytes[2], &uniform)" in wire
    assert "network_rules_reliable_split(size, bytes[3], uniform ? entry_size : 0" in wire
    boundary = _function(source, "void network_distributed_checkpoint_boundary")
    assert boundary.index("distributed_batches_flush();") < boundary.index("network_rules_epoch_next(")
    assert "network_rules_revert_boundary_to(" in boundary
    assert "distributed_machine_ready[machine_index] = FALSE;" in _function(source, "void network_distributed_machine_gone")
    manager = (ROOT / "source/networking/network_server_manager.c").read_text(encoding="utf-8")
    assert "network_distributed_machine_gone(client->machine_index);" in \
        _function(manager, "boolean network_game_server_remove_client_machine_from_game")
    # (the unit state reader checks its length against the rules' walk)
    assert "network_rules_unit_state_length(layout, buffer" in _function(source, "static word distributed_unit_state_read")


def test_join_paths_know_the_hosts_number():
    """every join of another machine's game goes by an advertisement (the
    LAN, the list, an invite once its host's game is advertised through the
    tunnel), whose number the join request's tail is made for; an address
    alone (no advertisement) sends the plain request, and once more with the
    newest version word after a silence; the host's own machine sends the
    word of the number it announces"""
    manager = (ROOT / "source/networking/network_client_manager.c").read_text(encoding="utf-8")
    invite = _function(manager, "long network_game_client_join_invite_host")
    assert "struct network_advertised_game *game = &client->available_games[game_index];" in invite
    assert "network_game_client_initiate_join_game(client, game, &join_parameters, &address)" in invite
    initiate = _function(manager, "boolean network_game_client_initiate_join_game")
    assert "network_game_client_advertised_versions[advertised_index].version" in initiate
    joining = _function(manager, "static boolean network_game_client_idle_joining")
    assert "network_game_client_send_join_request(client, network_game_client_joined_version);" in joining
    assert "network_game_client_send_join_request(client, retry);" in joining
    assert "network_game_client_join_retried = TRUE;" in joining
    request = _function(manager, "static void network_game_client_send_join_request")
    assert "network_rules_join_request_tail(version, tail," in request
    # (the host's own game, joined at home with no advertisement: its
    # request made for the number it announces, which tools/test_net43_host.py
    # runs for real)
    assert "else if (global_network_game_server_get())\n\t\t\tnetwork_game_client_joined_version = (unsigned int)delta_legacy_announce();" in initiate
