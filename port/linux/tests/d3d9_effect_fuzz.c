/*
D3D9_EFFECT_FUZZ.C

libFuzzer target (tools/tests/host_fuzz/d3d9_effect): a Direct3D 9 effect
binary as an OpenSauce map's shader_postprocess_generic tag holds it, read,
each shader turned into GLSL with and without the other stage's inputs, and
each preshader run with a shader's registers for its inputs, as the OpenGL
renderer does with them (port/linux/src/opensauce_postprocess_gl.inl). A
map's tags are its maker's: nothing in one may read or write past what the
effect has.
*/

#include "d3d9_effect.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct d3d9_effect effect;
	char error[256];
	void *copy;
	unsigned long index;

	if (size > (1u << 20))
		return 0;
	/* (aligned, as the renderer keeps it) */
	copy = malloc(size + 4);
	if (!copy)
		return 0;
	memcpy(copy, data, size);
	if (d3d9_effect_read(copy, (unsigned long)size, &effect, error, sizeof(error)))
	{
		for (index = 0; index < effect.shader_count; index++)
		{
			struct d3d9_glsl_varyings inputs;
			unsigned long floats;
			unsigned char kinds[D3D9_SHADER_SAMPLERS];
			char *text;

			memset(&inputs, 0, sizeof(inputs));
			text = d3d9_shader_glsl(&effect.shaders[index], NULL, &inputs, &floats, kinds, error, sizeof(error));
			free(text);
			text = d3d9_shader_glsl(&effect.shaders[index], &inputs, NULL, &floats, kinds, error, sizeof(error));
			free(text);
			if (effect.shaders[index].has_preshader)
			{
				float in[D3D9_SHADER_FLOAT_REGISTERS * 4], out[D3D9_SHADER_FLOAT_REGISTERS * 4];

				memset(in, 0, sizeof(in));
				memset(out, 0, sizeof(out));
				d3d9_preshader_run(&effect.shaders[index].preshader, in, out);
			}
		}
		d3d9_effect_dispose(&effect);
	}
	free(copy);
	return 0;
}
