"""The log's addresses (port/linux/src/log_address.c): a public one is a
tag, never whole, unless debug.log_addresses is on; local ones are whole.
And the environment variables that only have to be set
(port/linux/src/port_config.c): HALO_LOG_ADDRESSES=0 does not turn
whole addresses on.

log_address.c is built with the build machine's C compiler beside stand-ins
for the platform layer (its config, log, random bytes and hash)."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"

STUBS = {
    "platform.h": "void platform_log(const char *format, ...);\n",
    "posix.h": "void posix_random_bytes(void *buffer, unsigned long size);\n",
    "port_config.h": "int config_boolean(const char *name);\n",
    "p2p_internal.h": "enum { P2P_SHA256_SIZE = 32 };\n"
    "void p2p_sha256(const void *data, int size, unsigned char *digest);\n"
    "void p2p_hex(const unsigned char *bytes, int size, char *text);\n",
}

MAIN = r"""
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "log_address.h"

static int whole;
static int said;

void platform_log(const char *format, ...) { (void)format; said++; }
void posix_random_bytes(void *buffer, unsigned long size) { memset(buffer, 0x5A, size); }
int config_boolean(const char *name) { return !strcmp(name, "debug.log_addresses") && whole; }
void p2p_sha256(const void *data, int size, unsigned char *digest)
{
	/* (not SHA-256: any hash of every byte does here) */
	unsigned long long h = 1469598103934665603ULL;
	int i;
	for (i = 0; i < size; i++)
		h = (h ^ ((const unsigned char *)data)[i]) * 1099511628211ULL;
	for (i = 0; i < 32; i++)
		digest[i] = (unsigned char)(h >> (8 * (i % 8))) ^ (unsigned char)i;
}
void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	int i;
	for (i = 0; i < size; i++)
		sprintf(text + 2 * i, "%02x", bytes[i]);
}

ENVIRONMENT_FUNCTIONS

int main(int count, char **arguments)
{
	char text[LOG_ADDRESS_SIZE];
	unsigned char v4[4];
	unsigned char v6[16];
	unsigned long network;
	unsigned short port;

	if (count > 1 && !strcmp(arguments[1], "environment"))
	{
		int index;
		for (index = 2; index < count; index++)
			printf("%d\n", config_environment_set(arguments[index]));
		return 0;
	}
	whole = count > 1 && !strcmp(arguments[1], "whole");
	{
		int index;
		for (index = 0; index < 4; index++)
			v4[index] = (unsigned char)atoi(arguments[2 + index]);
	}
	printf("%s\n", log_address(v4, 4, 2302, text, sizeof(text)));
	printf("%s\n", log_address(v4, 4, -1, text, sizeof(text)));
	/* (network byte order, as a sockaddr_in has them) */
	memcpy(&network, v4, 4);
	network &= 0xFFFFFFFFUL;
	port = (unsigned short)((2302 >> 8) | ((2302 & 255) << 8));
	{
		unsigned int value = (unsigned int)network;
		printf("%s\n", log_address_ipv4(value, port, text, sizeof(text)));
	}
	memset(v6, 0, sizeof(v6));
	v6[0] = 0x26;
	v6[1] = 0x01;
	v6[15] = 7;
	printf("%s\n", log_address(v6, 16, 5160, text, sizeof(text)));
	printf("said %d\n", said);
	return 0;
}
"""


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def environment_functions():
    config = (SOURCE / "port_config.c").read_text()
    functions = []
    for name in ("config_text_is_false", "config_environment_set"):
        match = re.search(r"static int " + name + r"\(const char \*text\)\n\{.*?\n\}\n", config, re.S)
        assert match, name + " is not in port_config.c"
        functions.append(match.group(0))
    return "\n".join(functions)


@pytest.fixture(scope="module")
def program(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    folder = tmp_path_factory.mktemp("log_address")
    for name in ("log_address.c", "log_address.h"):
        shutil.copy(SOURCE / name, folder / name)
    for name, text in STUBS.items():
        (folder / name).write_text(text)
    (folder / "main.c").write_text(MAIN.replace("ENVIRONMENT_FUNCTIONS", environment_functions()))
    out = folder / "log_address_test"
    result = subprocess.run([cc, "-std=c99", "-Wall", "-Werror", "-o", str(out), str(folder / "main.c"),
                             str(folder / "log_address.c"), "-lpthread"], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return out


def run(program, *arguments):
    result = subprocess.run([str(program), *arguments], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return result.stdout.splitlines()


def test_public_ipv4_is_a_tag(program):
    lines = run(program, "tags", "203", "0", "113", "45")
    assert re.fullmatch(r"addr#[0-9a-f]{6}:2302", lines[0])
    assert re.fullmatch(r"addr#[0-9a-f]{6}", lines[1])
    # (the same address, the same tag, from a sockaddr_in's bytes too)
    assert lines[2] == lines[0]
    assert re.fullmatch(r"addr#[0-9a-f]{6}:5160", lines[3])
    assert not any("203.0.113" in line or "2601" in line for line in lines)
    assert lines[4] == "said 0"


def test_local_addresses_are_whole(program):
    for address in (["192", "168", "1", "20"], ["10", "0", "0", "5"], ["127", "0", "0", "2"],
                    ["100", "64", "0", "9"], ["172", "16", "3", "4"]):
        lines = run(program, "tags", *address)
        assert lines[0] == ".".join(address) + ":2302"
        assert lines[2] == lines[0]
        assert lines[4] == "said 0"


def test_whole_only_when_asked(program):
    lines = run(program, "whole", "203", "0", "113", "45")
    assert lines[0] == "203.0.113.45:2302"
    assert lines[2] == lines[0]
    assert lines[3] == "[2601:0:0:0:0:0:0:7]:5160"
    # (the log says once why it has them)
    assert lines[4] == "said 1"


def test_environment_false_values_do_not_set(program):
    values = ["1", "true", "yes", "on", "anything", "0", "false", "FALSE", "no", "Off", ""]
    assert run(program, "environment", *values) == ["1"] * 5 + ["0"] * 6
