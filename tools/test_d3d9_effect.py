"""port/linux/src/d3d9_effect.c: Direct3D 9 effect binaries read, their
shaders turned into GLSL and their preshaders run (OpenSauce maps'
post-processing, port/linux/game/opensauce_postprocess.c).

An effect is built here by hand, laid out as fxc lays one out: two
parameters (a float4 and a matrix with a semantic), one technique of one pass
with a vs_2_0 and a ps_2_0 shader, the pixel shader's constant table, and a
preshader that writes c1 from the float4. A small C program reads it with
d3d9_effect.c and prints what it found; the GLSL is checked for what each
instruction becomes, and the preshader's output for its value. A damaged
copy (cut short) must be refused, not read past its end."""
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port" / "linux" / "src"

DRIVER = r"""
#include "d3d9_effect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	FILE *file = fopen(argv[1], "rb");
	static unsigned char buffer[65536];
	size_t size = fread(buffer, 1, sizeof(buffer), file);
	void *data = malloc(size + 4);
	struct d3d9_effect effect;
	struct d3d9_glsl_varyings inputs;
	unsigned long floats;
	unsigned char kinds[D3D9_SHADER_SAMPLERS];
	char error[256], *pixel, *vertex;
	float in[4] = { 0.25f, 0.5f, 1.0f, 2.0f };
	static float out[D3D9_SHADER_FLOAT_REGISTERS * 4];

	fclose(file);
	memcpy(data, buffer, size);
	if (!d3d9_effect_read(data, (unsigned long)size, &effect, error, sizeof(error)))
	{
		printf("refused: %s\n", error);
		free(data);
		return 0;
	}
	printf("parameters %lu techniques %lu shaders %lu\n", effect.parameter_count, effect.technique_count,
		effect.shader_count);
	printf("semantic %ld name %ld technique %ld\n", d3d9_effect_parameter_by_semantic(&effect, "orthoworldviewprojection"),
		d3d9_effect_parameter_find(&effect, "Tint"), d3d9_effect_technique_find(&effect, "T"));
	printf("tint %g %g\n", *(const float *)&effect.parameters[0].defaults[0],
		*(const float *)&effect.parameters[0].defaults[3]);
	memset(&inputs, 0, sizeof(inputs));
	pixel = d3d9_shader_glsl(&effect.shaders[effect.techniques[0].passes[0].pixel_shader], NULL, &inputs, &floats,
		kinds, error, sizeof(error));
	printf("pixel floats %lu\n----pixel\n%s\n", floats, pixel ? pixel : error);
	vertex = d3d9_shader_glsl(&effect.shaders[effect.techniques[0].passes[0].vertex_shader], &inputs, NULL, &floats,
		kinds, error, sizeof(error));
	printf("----vertex\n%s\n----\n", vertex ? vertex : error);
	printf("preshader %d\n", d3d9_preshader_run(&effect.shaders[effect.techniques[0].passes[0].pixel_shader].preshader,
		in, out));
	printf("c1 %g %g %g %g\n", out[4], out[5], out[6], out[7]);
	free(pixel);
	free(vertex);
	d3d9_effect_dispose(&effect);
	free(data);
	return 0;
}
"""


def words(*values):
    return struct.pack("<%dI" % len(values), *values)


def comment(fourcc, body):
    """a comment token and its words: the FOURCC, then body"""
    data = fourcc + body
    while len(data) % 4:
        data += b"\0"
    return words(0xFFFE | ((len(data) // 4) << 16)) + data


def constant_table(version, constants):
    """a CTAB: its header, the constants (name, regset, reg, count, class,
    type, rows, columns) and their names and types after them"""
    header_size = 28
    entries = b""
    tail = b""
    tail_at = header_size + 20 * len(constants)
    for name, regset, reg, count, klass, kind, rows, columns in constants:
        name_offset = tail_at + len(tail)
        tail += name.encode() + b"\0"
        while len(tail) % 4:
            tail += b"\0"
        type_offset = tail_at + len(tail)
        tail += struct.pack("<HHHHHHI", klass, kind, rows, columns, 1, 0, 0)
        entries += struct.pack("<IHHHHII", name_offset, regset, reg, count, 0, type_offset, 0)
    creator = tail_at + len(tail)
    tail += b"test\0\0\0\0"
    return struct.pack("<7I", header_size, creator, version, len(constants), header_size, 0, creator) + entries + tail


def pixel_shader():
    ctab = constant_table(0xFFFF0200, [("Tint", 2, 0, 1, 1, 3, 1, 4)])
    # the preshader: c1 = Tint * 2, four components
    literals = struct.pack("<I4d", 4, 2.0, 2.0, 2.0, 2.0)
    code = words(1, (0x205 << 20) | 4, 2, 0, 2, 0, 0, 1, 0, 0, 4, 4)
    pres = (words(0x46580201) + comment(b"CLIT", literals) + comment(b"FXLC", code) +
            comment(b"CTAB", constant_table(0x46580201, [("Tint", 2, 0, 1, 1, 3, 1, 4)])) + words(0xFFFF))
    return (words(0xFFFF0200) + comment(b"CTAB", ctab) + comment(b"PRES", pres) +
            # mul r0, c0, c1 ; mov oC0, r0
            words(0x03000005, 0x800F0000, 0xA0E40000, 0xA0E40001) +
            words(0x02000001, 0x800F0800, 0x80E40000) +
            words(0x0000FFFF))


def vertex_shader():
    ctab = constant_table(0xFFFE0200, [("OrthoWVPMatrix", 2, 0, 4, 3, 3, 4, 4)])
    return (words(0xFFFE0200) + comment(b"CTAB", ctab) +
            # dcl_position v0 ; m4x4 oPos, v0, c0
            words(0x0200001F, 0x80000000, 0x900F0000) +
            words(0x03000014, 0xC00F0000, 0x90E40000, 0xA0E40000) +
            words(0x0000FFFF))


def effect_binary():
    body = bytearray()

    def add(blob):
        offset = len(body)
        body.extend(blob)
        while len(body) % 4:
            body.append(0)
        return offset

    def string(text):
        data = text.encode() + b"\0"
        return add(struct.pack("<I", len(data)) + data)

    tint_type = add(words(3, 1, string("Tint"), string(""), 0, 4, 1))
    tint_value = add(struct.pack("<4f", 0.25, 0.5, 1.0, 2.0))
    matrix_type = add(words(3, 2, string("OrthoWVPMatrix"), string("ORTHOWORLDVIEWPROJECTION"), 0, 4, 4))
    matrix_value = add(struct.pack("<16f", *([0.0] * 16)))
    vs_type = add(words(16, 4, string(""), string(""), 0))
    vs_value = add(words(1))
    ps_type = add(words(15, 4, string(""), string(""), 0))
    ps_value = add(words(2))
    technique_name = string("T")
    pass_name = string("P")
    start = len(body)
    body.extend(words(2, 1, 0, 3))
    body.extend(words(tint_type, tint_value, 0, 0))
    body.extend(words(matrix_type, matrix_value, 0, 0))
    body.extend(words(technique_name, 0, 1))
    body.extend(words(pass_name, 0, 2))
    body.extend(words(146, 0, vs_type, vs_value))
    body.extend(words(147, 0, ps_type, ps_value))
    body.extend(words(0, 2))
    for state, shader in ((0, vertex_shader()), (1, pixel_shader())):
        body.extend(words(0, 0, 0xFFFFFFFF, state, 0, len(shader)))
        body.extend(shader)
    return words(0xFEFF0901, start) + bytes(body)


def compiler():
    return shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")


@pytest.fixture(scope="module")
def driver(tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    work = tmp_path_factory.mktemp("d3d9_effect")
    (work / "driver.c").write_text(DRIVER)
    program = work / "driver"
    command = [cc, "-std=gnu99", "-O0", "-g", "-I", str(SOURCE), str(work / "driver.c"), str(SOURCE / "d3d9_effect.c"),
               "-lm", "-o", str(program)]
    # (with the sanitizers where the compiler has them: a read past a damaged
    # effect's end fails the test)
    if subprocess.run(command[:1] + ["-fsanitize=address,undefined"] + command[1:]).returncode:
        subprocess.run(command, check=True)
    return program, work


def run(driver, data):
    program, work = driver
    path = work / "effect.fxo"
    path.write_bytes(data)
    return subprocess.run([str(program), str(path)], capture_output=True, text=True, check=True).stdout


def test_effect_reads(driver):
    out = run(driver, effect_binary())
    assert "parameters 2 techniques 1 shaders 2" in out
    # (a semantic matched without minding case, as D3DX matches it)
    assert "semantic 1 name 0 technique 0" in out
    assert "tint 0.25 2" in out


def test_pixel_shader_glsl(driver):
    out = run(driver, effect_binary())
    pixel = out.split("----pixel\n")[1].split("----vertex")[0]
    assert "uniform vec4 pc[" in pixel
    assert "pc[0] * pc[1]" in pixel
    assert "o_color0.xyzw = vec4((r0)).xyzw;" in pixel
    assert "in vec4 vr_COLOR0;" in pixel


def test_vertex_shader_glsl(driver):
    out = run(driver, effect_binary())
    vertex = out.split("----vertex\n")[1].split("----\n")[0]
    assert "layout(location = 0) in vec4 v0;" in vertex
    # (m4x4: a dot product with each of the four registers)
    assert "dot(vec4(v0), vec4(vc[3]))" in vertex
    # (the pixel shader's inputs written, and Direct3D 9's half pixel)
    assert "out vec4 vr_COLOR0;" in vertex
    assert "d3d9_pixel_fix" in vertex


def test_preshader_runs(driver):
    out = run(driver, effect_binary())
    assert "preshader 1" in out
    assert "c1 0.5 1 2 4" in out


@pytest.mark.parametrize("cut", [7, 40, 200, 400])
def test_damaged_effect_refused(driver, cut):
    out = run(driver, effect_binary()[:cut])
    assert out.startswith("refused") or "parameters" in out
