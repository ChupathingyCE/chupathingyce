"""Whether an OpenSauce map loads in the network session played now, through
the real wiring: port/linux/game/opensauce_session.c (the session, from the
network game's server or client), port/linux/game/opensauce_maps.c (the
refusal) and port/linux/game/network_rules.c (which hosts are on the LAN),
compiled together through the 64-bit builds' rewrite of long. What stands in
is what the menus and internet play hold: the game's server and client, the
hosted game's p2p_set_hosting_allowed (as System Link's Create Game, Online
Games' and online co-op's set it), the joined host's address as the game's
transport keeps it (this machine's byte order), and internet play's peers,
whose virtual addresses p2p.c keeps in the network's.

Hosted: system link and offline play load it; Online Games, online co-op
and a dedicated server do not. Joined: a host on the LAN loads it; one on
the internet, or an internet host reached through the tunnel, does not."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from test_ce_resource_maps import STUB_CSERIES, STUB_WINDOWS, rewrite
from test_opensauce_maps import STUB_ERRORS, STUB_PACKS, UPGRADES

ROOT = Path(__file__).resolve().parent.parent

STUB_TYPES = STUB_CSERIES + r"""
typedef unsigned char byte;
typedef unsigned short word;
"""
STUB_GLOBALS = r"""
struct network_game_server;
struct network_game_client;
struct network_game_server *global_network_game_server_get(void);
struct network_game_client *global_network_game_client_get(void);
"""
STUB_CLIENT = r"""
struct network_game_client;
struct transport_address;
void network_game_client_get_remote_server_address(struct network_game_client *client,
	struct transport_address *address);
"""
HARNESS = r"""#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "bungie_net/network/transport.h"
#include "memory/byte_swapping.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "halo_map_packs.h"
#include "opensauce.h"
#include "opensauce_maps.h"
#include "opensauce_survey.h"
#include "spv3_mode.h"
#include "src/p2p.h"

/* ---------- the menus and the network game */

static int server, client;
/* (the host joined, in the network's byte order, as a socket holds it) */
static unsigned long joined_host;

struct network_game_server *global_network_game_server_get(void)
{
	return server ? (struct network_game_server *)&server : NULL;
}

struct network_game_client *global_network_game_client_get(void)
{
	return client ? (struct network_game_client *)&client : NULL;
}

/* (as transport_endpoint_winsock.c keeps a socket's address: this machine's
byte order) */
void network_game_client_get_remote_server_address(struct network_game_client *game_client,
	struct transport_address *address)
{
	(void)game_client;
	memset(address, 0, sizeof(*address));
	address->address.ipv4_address = NETWORK_SWAP4(joined_host);
	address->address_length = IPV4_ADDRESS_LENGTH;
}

/* ---------- internet play (p2p.c's, in the network's byte order) */

static int hosting_lan_only;
static unsigned long peer_virtual, peer_public;

void p2p_set_hosting_allowed(int allowed)
{
	hosting_lan_only = !allowed;
}

int p2p_hosting_internet(void)
{
	return !hosting_lan_only;
}

unsigned long p2p_peer_public_address(unsigned long game_address)
{
	/* (100.64.0.0/10, a peer's virtual address) */
	if ((ntohl((uint32_t)game_address) & 0xFFC00000u) != 0x64400000u)
		return 0;
	return game_address == peer_virtual ? peer_public : 0;
}

/* ---------- the rest opensauce_maps.c calls */

HANDLE CreateFileA(char const *path, unsigned long access, unsigned long share, void *security, unsigned long how,
	unsigned long flags, void *template_file)
{
	return INVALID_HANDLE_VALUE;
}
int CloseHandle(HANDLE file) { return 1; }
void error(int priority, char const *format, ...) { (void)priority; (void)format; }
char const *map_pack_menus(void) { return NULL; }
boolean spv3_mode_active(void) { return FALSE; }
struct opensauce_header const *opensauce_map_header(void) { return NULL; }
boolean opensauce_header_read(void const *cache_header, struct opensauce_header *header)
{
	unsigned char const *bytes = (unsigned char const *)cache_header + OPENSAUCE_HEADER_OFFSET;

	if (memcmp(bytes, "oley", 4))
		return FALSE;
	memset(header, 0, sizeof(*header));
	header->flags = (unsigned short)(bytes[6] | (bytes[7] << 8));
	return TRUE;
}
char const *opensauce_header_flag_name(short bit) { return bit == 0 ? "memory_upgrades" : "another"; }
void opensauce_survey_log(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	printf("log: ");
	vprintf(format, arguments);
	putchar('\n');
	va_end(arguments);
}

static unsigned long address_of(char const *text)
{
	struct in_addr address;

	if (inet_pton(AF_INET, text, &address) != 1)
	{
		printf("bad address %s\n", text);
		exit(2);
	}
	return address.s_addr;
}

/* each argument a step: end (no game: offline), system-link-host,
online-host (Create Game > Internet), coop-host (Online Games' and online
co-op's Create Game), dedicated, join ADDRESS, peer VIRTUAL PUBLIC (an
internet play peer), map FLAGS (whether a map whose OpenSauce header asks
for FLAGS loads now) */
int main(int argc, char **argv)
{
	int index;

	for (index = 1; index < argc; index++)
	{
		char const *step = argv[index];
		char const *value = strchr(step, ' ') ? strchr(step, ' ') + 1 : "";

		if (!strcmp(step, "end"))
			server = client = 0;
		else if (!strcmp(step, "system-link-host"))
		{
			/* (menu_functions.c's multiplayer_host, Create Game > System Link) */
			p2p_set_hosting_allowed(FALSE);
			server = 1, client = 0;
		}
		else if (!strcmp(step, "online-host"))
		{
			/* (multiplayer_host, Create Game > Internet) */
			p2p_set_hosting_allowed(TRUE);
			server = 1, client = 0;
		}
		else if (!strcmp(step, "coop-host") || !strcmp(step, "dedicated"))
		{
			/* (network_game_start_new_server, for Online Games' and online
			co-op's Create Game; the dedicated server's start) */
			p2p_set_hosting_allowed(TRUE);
			server = 1, client = 0;
		}
		else if (!strncmp(step, "join ", 5))
		{
			joined_host = address_of(value);
			server = 0, client = 1;
		}
		else if (!strncmp(step, "peer ", 5))
		{
			char virtual_address[32];
			char const *space = strchr(value, ' ');

			snprintf(virtual_address, sizeof(virtual_address), "%.*s", (int)(space - value), value);
			peer_virtual = address_of(virtual_address);
			peer_public = address_of(space + 1);
		}
		else if (!strncmp(step, "map ", 4))
		{
			static unsigned char header[0x800];
			unsigned short flags = (unsigned short)strtoul(value, NULL, 0);
			struct opensauce_maps_session session;

			opensauce_maps_session_now(&session);
			memset(header, 0, sizeof(header));
			memcpy(header + OPENSAUCE_HEADER_OFFSET, "oley", 4);
			header[OPENSAUCE_HEADER_OFFSET + 6] = (unsigned char)flags;
			header[OPENSAUCE_HEADER_OFFSET + 7] = (unsigned char)(flags >> 8);
			/* (the map's checks, and the map already open) */
			printf("map %d %d\n", opensauce_maps_online_allows("test", flags, &session) ? 1 : 0,
				opensauce_maps_header_online_allows("test", header) ? 1 : 0);
		}
		else
		{
			printf("bad step %s\n", step);
			return 2;
		}
	}
	return 0;
}
"""


@pytest.fixture(scope="module")
def session(tmp_path_factory):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    work = tmp_path_factory.mktemp("opensauce-session")
    stubs = tmp_path_factory.mktemp("opensauce-session-stubs")
    written = {
        "cseries.h": STUB_TYPES,
        "cseries/cseries_windows.h": STUB_WINDOWS,
        "errors.h": STUB_ERRORS,
        "halo_map_packs.h": STUB_PACKS,
        "networking/network_game_globals.h": STUB_GLOBALS,
        "networking/network_client_manager.h": STUB_CLIENT,
        "harness.c": HARNESS,
    }
    for name, text in written.items():
        (stubs / name).parent.mkdir(parents=True, exist_ok=True)
        (stubs / name).write_text(text)
        rewrite(stubs / name, work / name)
    for header in ("opensauce.h", "opensauce_maps.h", "opensauce_survey.h", "spv3_mode.h"):
        rewrite(ROOT / "port/linux/include" / header, work / header)
    rewrite(ROOT / "source/bungie_net/network/transport.h", work / "bungie_net/network/transport.h")
    rewrite(ROOT / "source/memory/byte_swapping.h", work / "memory/byte_swapping.h")
    rewrite(ROOT / "port/linux/game/network_rules.h", work / "network_rules.h")
    # (opensauce_session.c's p2p.h is ../src/p2p.h, beside its folder)
    rewrite(ROOT / "port/linux/src/p2p.h", work / "src/p2p.h")
    rewrite(ROOT / "port/linux/game/opensauce_session.c", work / "game/opensauce_session.c")
    rewrite(ROOT / "port/linux/game/opensauce_maps.c", work / "opensauce_maps.c")
    binary = work / "session"
    command = [cc, "-std=gnu99", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-format",
               "-Wno-unused-parameter", "-DHALO_CUSTOM_EDITION", "-DHALO_MAP_LIST", "-DHALO_64BIT",
               "-DHALO_GAME_BROWSER", "-g", "-I", str(work), str(work / "harness.c"),
               str(work / "game/opensauce_session.c"), str(work / "opensauce_maps.c"),
               str(ROOT / "port/linux/game/network_rules.c"), "-o", str(binary)]
    sanitized = command + ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    if subprocess.run(sanitized, capture_output=True).returncode != 0:
        build = subprocess.run(command, capture_output=True, text=True)
        assert build.returncode == 0, build.stdout + build.stderr

    def run(*steps):
        result = subprocess.run([str(binary), *steps], capture_output=True, text=True, env={})
        assert result.returncode == 0, result.stdout + result.stderr
        return [line for line in result.stdout.splitlines() if line.startswith("map ")], result.stdout
    return run


# (documentation addresses: 203.0.113.0/24 and 198.51.100.0/24 are no one's)
@pytest.mark.parametrize("steps,allowed", [
    (["end"], True),                                     # (offline)
    (["system-link-host"], True),
    (["join 192.168.1.20"], True),                       # (a system link host)
    (["join 10.0.0.7"], True),
    (["join 127.0.0.2"], True),
    (["join 100.101.7.33"], True),                       # (a LAN reached through a VPN's 100.64/10, no peer's)
    (["online-host"], False),                            # (Create Game > Internet)
    (["coop-host"], False),                              # (online co-op, Online Games' Create Game)
    (["dedicated"], False),
    (["system-link-host", "end", "coop-host"], False),   # (online co-op after a system link game)
    (["coop-host", "end", "system-link-host"], True),
    (["join 203.0.113.5"], False),                       # (an online server)
    (["peer 100.72.14.9 198.51.100.4", "join 100.72.14.9"], False),  # (an internet host through the tunnel)
    (["peer 100.72.14.9 198.51.100.4", "join 100.72.14.10"], True),  # (another address there, no peer's)
])
def test_session(session, steps, allowed):
    lines, out = session(*steps, f"map {UPGRADES}", "map 0")
    assert lines[0] == ("map 1 1" if allowed else "map 0 0"), out
    # (a map that asks for nothing is a Custom Edition map: anywhere)
    assert lines[1] == "map 1 1", out
    assert ("refused" in out) == (not allowed), out


if __name__ == "__main__":
    sys.exit(pytest.main([__file__]))
