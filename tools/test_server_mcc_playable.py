"""A dedicated server plays an MCC playlist (server/src/server_commands.c's
server_map_playable, which the playlist asks of each game before it starts):
an MCC multiplayer map the catalog has is playable in a build with MCC maps
(HALO_MCC 1, hosted at OpenCE's 43), a missing one or a campaign level is
not, and a build without MCC maps (HALO_MCC 0, a console's) plays none. The
function is taken from the game's source and compiled with stand-ins."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from harness import function, read  # noqa: E402

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
struct command_output { char text[256]; };
static void command_output_printf(struct command_output *output, char const *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(output->text, sizeof(output->text), format, arguments);
	va_end(arguments);
}
/* (the MCC catalog: one multiplayer map, one campaign level) */
static boolean mcc_level_name(char const *name) { return !strncmp(name, "mcc_maps\\", 9); }
static void mcc_maps_rescan(void) {}
static short mcc_maps_find(char const *name)
{
	if (!strcmp(name, "mcc_maps\\3477809506\\20")) return 0;
	if (!strcmp(name, "mcc_maps\\pack\\a10")) return 1;
	return NONE;
}
static boolean mcc_maps_campaign(short index) { return index == 1; }
/* (every other family: no map, this test's maps are MCC's) */
enum { _map_family_xbox = 0 };
static short map_family_parse(char const *map, char *file, long size)
{
	(void)map; snprintf(file, (size_t)size, "none"); return _map_family_xbox;
}
static char const *cache_files_map_directory(void) { return "/nonexistent/"; }
static boolean file_is_multiplayer_map(char const *path) { (void)path; return FALSE; }
@FUNCTION@
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
	struct command_output output;
#if HALO_MCC
	CHECK(server_map_playable("mcc_maps\\3477809506\\20", &output));
	CHECK(!server_map_playable("mcc_maps\\3477809506\\21", &output) && strstr(output.text, "no MCC map"));
	CHECK(!server_map_playable("mcc_maps\\pack\\a10", &output) && strstr(output.text, "campaign"));
#else
	CHECK(!server_map_playable("mcc_maps\\3477809506\\20", &output) && strstr(output.text, "no MCC maps"));
#endif
	printf("ok\n");
	return 0;
}
"""


@pytest.mark.parametrize("mcc", [1, 0])
def test_server_mcc_playable(tmp_path, mcc):
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    body = function(read("server/src/server_commands.c"), "server_map_playable")
    # (only its MCC part and the Xbox family's answer are this test's)
    body = body[:body.index("#ifdef HALO_MAP_LIST")] + "\treturn FALSE;\n}\n"
    source = tmp_path / "playable.c"
    source.write_text(HARNESS.replace("@FUNCTION@", body.replace("boolean server_map_playable(", "static boolean server_map_playable(", 1)))
    binary = tmp_path / "playable"
    subprocess.run([cc, "-std=gnu99", "-Wall", "-Wno-unused-function", f"-DHALO_MCC={mcc}", str(source), "-o", str(binary)],
                   check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0 and "ok" in result.stdout, result.stdout + result.stderr
