"""A tag named in a typed expression (the console, execute_script) that the
map's scripts don't reference (#1972): hs_compile.c's hs_parse_tag_reference,
compiled from the source with the build machine's C compiler on a stand-in
scenario. The console refuses it with a message instead of running the
function with none as the tag (damage_object halted on "ffffffff is a tag
index"); a referenced tag parses as before, and so does every tag while the
scenario's own scripts compile."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
HS_COMPILE_C = ROOT / "source/hs/hs_compile.c"


def _function(text, name):
    start = text.index(f"static boolean {name}(\n\tlong expression_index)\n{{")
    end = text.index("\n}\n", start) + 3
    return text[start:end]


PRELUDE = """
#include <stdio.h>
#include <string.h>
typedef int boolean;
typedef unsigned int tag;
#define TRUE 1
#define FALSE 0
#define NONE (-1L)
#define csstrcmp strcmp
#define xbox_pointer(p) (p)
#define match_assert(file, line, condition) ((void)0)
#define HS_TYPE_IS_TAG_REFERENCE(type) ((type) >= _hs_type_sound && (type) <= _hs_type_damage)
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->address)[index])

enum { _hs_type_sound, _hs_type_effect, _hs_type_damage };
static char const *const hs_type_names[] = { "sound", "effect", "damage" };
static tag const hs_tag_reference_type_group_tags[] = { 'snd!', 'effe', 'jpt!' };

struct tag_block { long count; void *address; };
struct tag_reference { tag group_tag; char const *name; long index; };
struct hs_reference { struct tag_reference reference; };
struct scenario { struct tag_block hs_references; };
struct hs_syntax_node { short type; long source_offset; long data; };

static struct hs_reference references[] = {
	{ { 'jpt!', "weapons\\\\frag grenade\\\\explosion", 7 } },
	{ { 'snd!', "sound\\\\dialog\\\\a10\\\\hello", 3 } },
	{ { 'jpt!', "unloaded", NONE } },
};
static struct scenario the_scenario = { { 3, references } };
static struct scenario *global_scenario_get(void) { return &the_scenario; }

static struct
{
	char *compiled_source;
	char const *error;
	long error_offset;
	char error_buffer[256];
	boolean compiling_scenario;
} hs_compile_globals;

static struct hs_syntax_node node;
static struct hs_syntax_node *hs_syntax_get(long index) { (void)index; return &node; }
"""

MAIN = """
static int failures;

static boolean parse(short type, char *name, boolean compiling_scenario)
{
	node.type = type;
	node.source_offset = 0;
	node.data = NONE;
	hs_compile_globals.compiled_source = name;
	hs_compile_globals.compiling_scenario = compiling_scenario;
	hs_compile_globals.error = NULL;
	hs_compile_globals.error_offset = NONE;
	return hs_parse_tag_reference(0);
}

static void expect(boolean condition, char const *what)
{
	if (!condition)
	{
		printf("failed: %s\\n", what);
		failures++;
	}
}

int main(void)
{
	char frag[] = "weapons\\\\frag grenade\\\\explosion";
	char missing[] = "weapons\\\\plasma grenade\\\\explosion";
	char hello[] = "sound\\\\dialog\\\\a10\\\\hello";
	char unloaded[] = "unloaded";

	(void)hs_type_names;

	expect(parse(_hs_type_damage, frag, FALSE) && node.data == 7 && !hs_compile_globals.error,
		"a referenced damage tag parses");
	expect(parse(_hs_type_sound, hello, FALSE) && node.data == 3, "a referenced sound parses");

	expect(!parse(_hs_type_damage, missing, FALSE), "an unreferenced damage tag is refused");
	expect(hs_compile_globals.error && strstr(hs_compile_globals.error, "damage"),
		"the refusal names the tag's type");
	expect(hs_compile_globals.error_offset == 0, "the refusal points at the tag's name");
	expect(!parse(_hs_type_sound, frag, FALSE), "a damage tag's name as a sound is refused");
	expect(!parse(_hs_type_damage, unloaded, FALSE), "a reference whose tag isn't loaded is refused");

	expect(parse(_hs_type_damage, missing, TRUE) && node.data == NONE && !hs_compile_globals.error,
		"the scenario's own scripts compile as before");

	if (!failures)
		printf("console_tag_refs_test: ok\\n");
	return failures != 0;
}
"""


def test_console_refuses_unreferenced_tags(tmp_path):
    cc = next((shutil.which(name) for name in ("clang", "cc", "gcc") if shutil.which(name)), None)
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "console_tag_refs_test.c"
    body = _function(HS_COMPILE_C.read_text(encoding="utf-8"), "hs_parse_tag_reference")
    source.write_text(PRELUDE + body + MAIN, encoding="utf-8")
    binary = tmp_path / "console_tag_refs_test"
    build = subprocess.run([cc, "-std=c11", "-Wall", "-Werror", "-Wno-multichar", str(source), "-o", str(binary)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "console_tag_refs_test: ok" in result.stdout


def test_console_prints_the_refusal():
    """the console's compile error reaches the player (hs.c prints it)"""
    text = (ROOT / "source/hs/hs.c").read_text(encoding="utf-8")
    body = text[text.index("hs_compile_initialize(FALSE);"):]
    body = body[:body.index("hs_compile_dispose();")]
    assert "hs_compile_source_error(" in body


def test_map_load_parses_as_the_scenario():
    """A cache map's scripts are compiled already: at load,
    hs_compile_postprocess parses their tag references again, through
    hs_parse_tag_reference, outside hs_compile_initialize. It has to count as
    the scenario's compile, or every map's own tags are refused and the map
    halts ("vehicles\\pelican\\pelican: this is not a object_definition tag
    this map's scripts use." on a10), and it gives the flag back after."""
    text = HS_COMPILE_C.read_text(encoding="utf-8")
    start = text.index("boolean hs_compile_postprocess(")
    end = text.index("\n}\n", start)
    body = text[start:end]
    loop = body.index("for (expression_index = data_next_index(hs_syntax_data, NONE);")
    assert "hs_compile_globals.compiling_scenario = TRUE;" in body[:loop]
    assert "hs_compile_globals.compiling_scenario = compiling_scenario_before;" in body[loop:]
