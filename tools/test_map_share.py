"""Map sharing (port/linux/game/map_share_*.c, port/linux/README.md "Map
sharing"): the host's side is off unless the host turns it on, the messages
stay additive (no network version change, builds without them drop them),
the host names no other player, and the map bot's fetch never adds a
player, never loads the game and never listens."""
import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "port/linux/game"


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def test_settings_registered_off_and_documented():
    config = read("port/linux/src/port_config.c")
    assert re.search(r'\{ "network\.map_share", _config_boolean, "false", "HALO_NET_MAP_SHARE"', config)
    assert re.search(r'\{ "network\.map_share_upload", _config_integer, "1024", "HALO_NET_MAP_SHARE_UPLOAD"', config)
    for name in ("folder", "result", "name", "held", "maximum"):
        assert f'"debug.map_fetch_{name}"' in config
        assert f"`HALO_MAP_FETCH_{name.upper()}`" in read("port/linux/README.md")
    readme = read("port/linux/README.md")
    assert "| `network.map_share` | `false` | `HALO_NET_MAP_SHARE` |" in readme
    assert "### Map sharing" in readme
    assert "`network.map_share`" in read("docs/settings.md")
    assert "`HALO_NET_MAP_SHARE`" in read("server/docs/settings.md")


def test_host_clamps_upload_and_reads_its_setting():
    host = (GAME / "map_share_host.c").read_text(encoding="utf-8")
    assert 'config_boolean("network.map_share")' in host
    assert "kilobytes < 16 ? 16 : kilobytes > 20480 ? 20480 : kilobytes" in host


def test_listen_hosts_share_as_servers_do_and_a_512_mb_fetch_cap():
    # a dedicated server's game and a game hosted from the menus alike, as
    # the setting says (off by default)
    host = (GAME / "map_share_host.c").read_text(encoding="utf-8")
    assert 'return config_boolean("network.map_share") != 0;' in host
    sharing = host[host.index("static boolean host_sharing("):]
    assert "browser_dedicated" not in sharing[:sharing.index("\n}\n")]
    # the bot takes at most 512 MB
    client = (GAME / "map_share_client.c").read_text(encoding="utf-8")
    assert "MIN(maximum, 512)) << 20;" in client
    assert "(up to 512)" in read("port/linux/README.md")


def test_messages_stay_unknown_to_the_distributed_netcode():
    # 83 to 86 are not distributed kinds this build knows: anything but the
    # map share hooks drops them, as before
    header = read("port/linux/game/network_distributed.h")
    for number in ("83", "84", "85", "86"):
        assert f"= {number}," not in header
    assert "NETWORK_VERSION" not in (GAME / "map_share.h").read_text(encoding="utf-8")


def test_host_sends_no_other_players_addresses():
    host = (GAME / "map_share_host.c").read_text(encoding="utf-8")
    # the sources message is the 4-byte header alone, count 0
    assert host.count("MAP_SHARE_MESSAGE_SOURCES, sources, sizeof(sources)") == 2
    assert "unsigned char sources[MAP_SHARE_SOURCES_SIZE];" in host
    assert "MAP_SHARE_SOURCE_SIZE" not in host


def test_fetch_takes_nothing_it_does_not_need():
    client = (GAME / "map_share_client.c").read_text(encoding="utf-8")
    # no player, no listening port, the map bit alone
    assert "player_ui_local_player_joined_multiplayer_game" not in client
    assert "listen_endpoint" not in client and "bind_endpoint" not in client
    assert "body[0x44] = MAP_SHARE_WANT_MAP;" in client
    assert '#define MAP_FETCH_MACHINE_NAME "Chupathingy map bot"' in client
    # the sources list is never read
    sources = client[client.index("if (type == MAP_SHARE_MESSAGE_SOURCES)"):]
    sources = sources[:sources.index("}")]
    assert "body[" not in sources
    # it never loads the game or precaches the host's map
    manager = read("source/networking/network_client_manager.c")
    assert "map_share_fetch_game_record(message_packet->map.name" in manager
    assert "if (!map_share_fetching())\n#endif\n\t\t\tnetwork_game_client_update_precache_status(client);" in manager
    handler = read("source/networking/network_client_message_handler.c")
    assert "if (map_share_fetching())\n\t\t\t\t\tresult = TRUE;" in handler


def test_downloading_machines_are_kept():
    manager = read("source/networking/network_server_manager.c")
    assert manager.count("!NETWORK_GAME_SERVER_MACHINE_DOWNLOADING(") == 2


def test_hash_vectors_of_the_specification():
    # the hashes map_share_files.c computes (BLAKE2b-256 of each 256 KiB
    # piece; of "OpenCE map 1", the size and the pieces' hashes for the file)
    def file_hash(data):
        pieces = [hashlib.blake2b(data[i:i + 0x40000], digest_size=32).digest()
                  for i in range(0, len(data), 0x40000)]
        whole = hashlib.blake2b(digest_size=32)
        whole.update(b"OpenCE map 1" + len(data).to_bytes(4, "little") + b"".join(pieces))
        return whole.hexdigest()

    assert file_hash(b"hello") == "569813a159fc9ea719da6d80df57632f97caa44458109a790342ff911852c59a"
    assert file_hash(bytes(0x40001)) == "bd2a6f67dee7f8b19b5d21168d08a373c6b6dc257a0e83438ac709c5e019139f"
    files = read("port/linux/src/map_share_files.c")
    assert 'static const char tag[] = "OpenCE map 1";' in files
    assert "MAP_SHARE_PIECE_SIZE = 0x40000" in read("port/linux/src/map_share_files.h")


def test_a_download_cannot_keep_a_machine_forever():
    host = (GAME / "map_share_host.c").read_text(encoding="utf-8")
    # asking again restarts nothing; the stay grows with bytes really sent
    assert "if (!machine->offered_once || machine->total != host.serving_size)" in host
    assert "host_served(stream->address, (unsigned long)size);" in host
    assert "machine->growth_time = now;" not in host.split("case MAP_SHARE_MESSAGE_PROGRESS:")[1]
    assert "(long)(now - machine->started) >= (long)host_download_limit(machine->total)" in host
    assert "DOWNLOADERS = 8," in host and "DOWNLOADERS_PER_ADDRESS = 2," in host


def test_file_names_are_plain():
    wire = (GAME / "map_share_wire.c").read_text(encoding="utf-8")
    assert "character == '_' || character == '.' || character == '-'" in wire
