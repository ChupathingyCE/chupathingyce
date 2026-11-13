"""The button prompts' devices (port/linux/include/halo_prompt_schemes.h,
read in xinput_sdl.c) and their glyphs (ui_overlay.c): the Steam Deck and
the Steam Controller are told apart by Valve's numbers, every glyph a
device's table names is in its Kenney font, and a build embeds only the
prompt fonts it asks for (tools/embed_assets.py --prompt-fonts)."""
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "port" / "linux" / "include" / "halo_prompt_schemes.h"
OVERLAY = ROOT / "port" / "linux" / "src" / "ui_overlay.c"
FONTS = ROOT / "port" / "linux" / "ui" / "fonts"
sys.path.insert(0, str(ROOT))

from tools import embed_assets  # noqa: E402

# halo_prompt_schemes.h's order, and the font ui_overlay.c's button_fonts
# gives each
SCHEMES = ["keyboard", "xbox", "playstation", "nintendo", "steam_deck", "wiiu", "steam_controller"]
SCHEME_FONTS = ["input_keyboard.ttf", "input_xbox.ttf", "input_playstation.ttf", "input_nintendo.ttf",
                "input_steam_deck.ttf", "input_wiiu.ttf", "input_steam_controller.ttf"]


def compiler():
    cc = shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler")
    return cc


def test_header_tells_the_pads_apart(tmp_path):
    source = tmp_path / "schemes.c"
    source.write_text(
        '#include <stdio.h>\n#define HALO_PROMPT_SCHEME_FUNCTIONS\n#include "halo_prompt_schemes.h"\n'
        "int main(void)\n{\n"
        '\tprintf("%d %d %d %d %d %d %d\\n",\n'
        # (a Steam Deck's own controls; a Steam Controller, wired and by its
        # dongle; Steam's virtual pad on a Deck, and elsewhere over a
        # PlayStation pad; an Xbox pad)
        "\t\tprompt_scheme_of_pad(PROMPT_SCHEME_XBOX, 0x28de, 0x1205, 0),\n"
        "\t\tprompt_scheme_of_pad(PROMPT_SCHEME_XBOX, 0x28de, 0x1102, 0),\n"
        "\t\tprompt_scheme_of_pad(PROMPT_SCHEME_XBOX, 0x28de, 0x1142, 0),\n"
        "\t\tprompt_scheme_of_pad(PROMPT_SCHEME_XBOX, 0x28de, 0x11ff, 1),\n"
        "\t\tprompt_scheme_of_pad(PROMPT_SCHEME_PLAYSTATION, 0x28de, 0x11ff, 0),\n"
        "\t\tprompt_scheme_of_pad(PROMPT_SCHEME_XBOX, 0x28de, 0x11ff, 0),\n"
        "\t\tprompt_scheme_of_pad(PROMPT_SCHEME_XBOX, 0x045e, 0x0b12, 1));\n"
        '\tprintf("%d %d %d %d\\n", prompt_scheme_named("steam_deck"), prompt_scheme_named("wiiu"),\n'
        '\t\tprompt_scheme_named(""), prompt_scheme_named("auto"));\n'
        "\treturn 0;\n}\n")
    shutil.copy(HEADER, tmp_path / HEADER.name)
    binary = tmp_path / "schemes"
    subprocess.run([compiler(), "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-I", str(tmp_path),
                    str(source), "-o", str(binary)], check=True)
    lines = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout.split("\n")
    deck, controller, xbox, playstation, wiiu = (SCHEMES.index(name) for name in (
        "steam_deck", "steam_controller", "xbox", "playstation", "wiiu"))
    assert lines[0].split() == [str(value) for value in (deck, controller, controller, deck, playstation, xbox, xbox)]
    assert lines[1].split() == [str(deck), str(wiiu), "-1", "-1"]


def test_header_enum_without_functions(tmp_path):
    # (ui_overlay.c takes the enumeration alone; -Wunused-function stays quiet)
    source = tmp_path / "enum.c"
    source.write_text('#include "halo_prompt_schemes.h"\nint main(void)\n{\n\treturn NUMBER_OF_PROMPT_SCHEMES - 7;\n}\n')
    shutil.copy(HEADER, tmp_path / HEADER.name)
    subprocess.run([compiler(), "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror", "-I", str(tmp_path),
                    str(source), "-o", str(tmp_path / "enum")], check=True)


def cmap(path):
    """The code points a TrueType font maps (its cmap's format 4 table)."""
    data = path.read_bytes()
    tables = {data[12 + 16 * i:16 + 16 * i].decode("latin-1"): struct.unpack(">I", data[20 + 16 * i:24 + 16 * i])[0]
              for i in range(struct.unpack(">H", data[4:6])[0])}
    base = tables["cmap"]
    codes = set()
    for index in range(struct.unpack(">H", data[base + 2:base + 4])[0]):
        offset = struct.unpack(">I", data[base + 8 + 8 * index:base + 12 + 8 * index])[0]
        table = base + offset
        if struct.unpack(">H", data[table:table + 2])[0] != 4:
            continue
        segments = struct.unpack(">H", data[table + 6:table + 8])[0] // 2
        ends = struct.unpack(f">{segments}H", data[table + 14:table + 14 + 2 * segments])
        starts = struct.unpack(f">{segments}H", data[table + 16 + 2 * segments:table + 16 + 4 * segments])
        for start, end in zip(starts, ends):
            codes.update(range(start, end + 1))
    return codes


def glyph_tables():
    source = OVERLAY.read_text()
    start = source.index("button_glyphs[NUMBER_OF_PROMPT_SCHEMES][NUMBER_OF_UI_BUTTONS] =")
    body = source[source.index("{", start) + 1:source.index("\n};", start)]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return [[int(value, 0) for value in row.split(",") if value.strip()] for row in re.findall(r"\{([^}]*)\}", body)]


def test_every_glyph_is_in_its_font():
    rows = glyph_tables()
    assert len(rows) == len(SCHEMES)
    for scheme, (row, font) in enumerate(zip(rows, SCHEME_FONTS)):
        assert len(row) == 12, SCHEMES[scheme]
        codes = cmap(FONTS / font)
        missing = [hex(code) for code in row if code and code not in codes]
        assert not missing, f"{SCHEMES[scheme]}: {font} has no {missing}"


def test_fonts_follow_the_enumeration():
    # (ui_font.h's prompt fonts come after the platform logos, in
    # PROMPT_FONTS' order)
    header = (ROOT / "port" / "linux" / "src" / "ui_font.h").read_text()
    order = re.findall(r"\bPOSIX_UI_FONT_(STEAM_DECK|STEAM_CONTROLLER|WIIU|PLATFORMS),", header)
    assert order == ["PLATFORMS", "STEAM_DECK", "STEAM_CONTROLLER", "WIIU"]
    names = [path.name for path in embed_assets.UI_FONT_FILES]
    assert names[-4:] == ["platform_icons.ttf", "input_steam_deck.ttf", "input_steam_controller.ttf", "input_wiiu.ttf"]
    assert list(embed_assets.PROMPT_FONTS) == ["steam_deck", "steam_controller", "wiiu"]


def test_a_build_embeds_only_its_prompt_fonts(tmp_path):
    def sizes(*arguments):
        output = tmp_path / "fonts.c"
        subprocess.run([sys.executable, str(ROOT / "tools" / "embed_assets.py"), "--fonts", *arguments, str(output)],
                       check=True, cwd=ROOT)
        text = output.read_text()
        # (each of the overlay's fonts by the data ui_font_files gives it: a
        # file several fonts share is embedded once)
        table = text[text.index("ui_font_files["):]
        arrays = re.findall(r"\b(font\d+)\b", table[table.index("{"):table.index("};")])
        result = []
        for array in arrays:
            body = text[text.index(f"{array}[] = {{"):]
            result.append(body[:body.index("};")].count(","))
        return result

    everything = sizes()
    steam_controller = sizes("--prompt-fonts", "steam_controller")
    none = sizes("--prompt-fonts", "none")
    count = len(embed_assets.UI_FONT_FILES)
    deck, controller, wiiu = count - 3, count - 2, count - 1
    assert all(everything[index] > 1 for index in (deck, controller, wiiu))
    assert steam_controller[controller] == everything[controller]
    assert steam_controller[deck] == 1 and steam_controller[wiiu] == 1
    assert all(none[index] == 1 for index in (deck, controller, wiiu))
    # (the other fonts are always whole)
    assert none[:deck] == everything[:deck]


def test_unknown_prompt_font_is_refused(tmp_path):
    result = subprocess.run([sys.executable, str(ROOT / "tools" / "embed_assets.py"), "--fonts", "--prompt-fonts",
                             "gamecube", str(tmp_path / "fonts.c")], cwd=ROOT, capture_output=True, text=True)
    assert result.returncode != 0 and "gamecube" in result.stderr


GLYPH_PROGRAM = r"""
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <stdio.h>
#include <stdlib.h>

/* argv: font, line, raise, code points (hex): each glyph's top and bottom,
in hundredths of a button 100 high, drawn as ui_overlay.c's draw_kenney
(the line's top at y - raise * size, its baseline an ascent below) */
int main(int argc, char **argv)
{
	FILE *file = fopen(argv[1], "rb");
	long length;
	unsigned char *data;
	stbtt_fontinfo info;
	float line = (float)atof(argv[2]) * 100.0f, raise = (float)atof(argv[3]) * 100.0f, scale;
	int ascent, descent, gap, index;

	fseek(file, 0, SEEK_END);
	length = ftell(file);
	rewind(file);
	data = malloc((size_t)length);
	if (fread(data, 1, (size_t)length, file) != (size_t)length || !stbtt_InitFont(&info, data, 0))
		return 1;
	scale = stbtt_ScaleForPixelHeight(&info, line);
	stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
	for (index = 4; index < argc; index++)
	{
		int x0, y0, x1, y1;

		stbtt_GetCodepointBitmapBox(&info, (int)strtoul(argv[index], NULL, 16), scale, scale, &x0, &y0, &x1, &y1);
		printf("%.1f %.1f\n", -raise + ascent * scale + y0, -raise + ascent * scale + y1);
	}
	return 0;
}
"""


def test_kenney_glyphs_fill_the_button_as_the_icons(tmp_path):
    # (a face button's glyph, drawn size high, covers about 10 to 90 of it,
    # as snapchipper's icons do: not 27 to 99, as before, nor past its box)
    source = OVERLAY.read_text()
    line = float(re.search(r"#define KENNEY_LINE ([0-9.]+)f", source).group(1))
    raise_ = float(re.search(r"#define KENNEY_RAISE ([0-9.]+)f", source).group(1))
    program = tmp_path / "glyphs.c"
    program.write_text(GLYPH_PROGRAM)
    binary = tmp_path / "glyphs"
    subprocess.run([compiler(), "-O1", "-w", "-I", str(ROOT / "port" / "third_party" / "stb"), str(program), "-o",
                    str(binary), "-lm"], check=True)
    rows = glyph_tables()
    for scheme in ("playstation", "steam_deck", "wiiu", "steam_controller"):
        index = SCHEMES.index(scheme)
        faces = [f"{code:x}" for code in rows[index][:4]]
        out = subprocess.run([str(binary), str(FONTS / SCHEME_FONTS[index]), str(line), str(raise_), *faces],
                             check=True, capture_output=True, text=True).stdout.split("\n")
        for code, box in zip(faces, out):
            top, bottom = (float(value) for value in box.split())
            assert 5 <= top <= 15 and 85 <= bottom <= 95, f"{scheme} {code}: {top}..{bottom}"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
