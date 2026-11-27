/*
D3D9_EFFECT.C

Direct3D 9 effect binaries read, their shaders turned into GLSL and their
preshaders run (d3d9_effect.h), for OpenSauce's post-processing
(port/linux/game/opensauce_postprocess.c). OpenSauce (Kornner Studios,
GPL-3.0-or-later) hands its tags' compiled effects to D3DX; this file does
the part of D3DX that matters for them, from the formats as they are laid
out in the binaries. No code of Microsoft's, D3DX's or OpenSauce's is used.

Every read is checked against the binary's size: a tag's effect is a map's
content and may be damaged or made to mislead.
*/

#include "d3d9_effect.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	FX_SIGNATURE = 0xFEFF0901,
	/* a pass's and a sampler's states (the effect's own numbering) */
	FX_STATE_VERTEX_SHADER = 146,
	FX_STATE_PIXEL_SHADER = 147,
	FX_STATE_SAMPLER_TEXTURE = 164,

	/* comment blocks in a shader's tokens */
	FOURCC_CTAB = 0x42415443,
	FOURCC_PRES = 0x53455250,
	FOURCC_CLIT = 0x54494C43,
	FOURCC_FXLC = 0x434C5846,

	/* the shader tokens' opcodes (D3DSHADER_INSTRUCTION_OPCODE_TYPE) */
	OP_NOP = 0, OP_MOV, OP_ADD, OP_SUB, OP_MAD, OP_MUL, OP_RCP, OP_RSQ, OP_DP3, OP_DP4, OP_MIN, OP_MAX, OP_SLT,
	OP_SGE, OP_EXP, OP_LOG, OP_LIT, OP_DST, OP_LRP, OP_FRC, OP_M4x4, OP_M4x3, OP_M3x4, OP_M3x3, OP_M3x2, OP_CALL,
	OP_CALLNZ, OP_LOOP, OP_RET, OP_ENDLOOP, OP_LABEL, OP_DCL, OP_POW, OP_CRS, OP_SGN, OP_ABS, OP_NRM, OP_SINCOS,
	OP_REP, OP_ENDREP, OP_IF, OP_IFC, OP_ELSE, OP_ENDIF, OP_BREAK, OP_BREAKC, OP_MOVA, OP_DEFB, OP_DEFI,
	OP_TEXCOORD = 64, OP_TEXKILL, OP_TEX, OP_EXPP = 78, OP_LOGP, OP_CND, OP_DEF, OP_CMP = 88, OP_BEM,
	OP_DP2ADD, OP_DSX, OP_DSY, OP_TEXLDD, OP_SETP, OP_TEXLDL, OP_BREAKP,
	OP_PHASE = 0xFFFD, OP_COMMENT = 0xFFFE, OP_END = 0xFFFF,

	/* register types (D3DSHADER_PARAM_REGISTER_TYPE) */
	REG_TEMP = 0, REG_INPUT, REG_CONST, REG_ADDR_TEXTURE, REG_RASTOUT, REG_ATTROUT, REG_OUTPUT, REG_CONSTINT,
	REG_COLOROUT, REG_DEPTHOUT, REG_SAMPLER, REG_CONST2, REG_CONST3, REG_CONST4, REG_CONSTBOOL, REG_LOOP,
	REG_TEMPFLOAT16, REG_MISCTYPE, REG_LABEL, REG_PREDICATE,

	/* declaration usages (D3DDECLUSAGE) */
	USAGE_POSITION = 0, USAGE_TEXCOORD = 5, USAGE_COLOR = 10, USAGE_FOG = 11, NUMBER_OF_USAGES = 14,

	MAXIMUM_TEMPORARIES = 32,
	MAXIMUM_LABELS = 32,
	MAXIMUM_OUTPUTS = 12,
	/* a rep or loop's most iterations, as Direct3D 9 has it */
	MAXIMUM_LOOP_ITERATIONS = 255,
	/* the most a translation takes: ifs and loops inside one another (fxc
	allows 24), and the text's size (SPV3's largest is under 64 KB) */
	MAXIMUM_NESTING = 64,
	MAXIMUM_GLSL_SIZE = 1024 * 1024,
};

static const char *const usage_names[NUMBER_OF_USAGES] =
{
	"POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD", "TANGENT", "BINORMAL",
	"TESSFACTOR", "POSITIONT", "COLOR", "FOG", "DEPTH", "SAMPLE"
};

/* ---------- reading */

struct reader
{
	const unsigned char *base;
	unsigned long size;
	unsigned long at;
	int failed;
};

static unsigned long reader_word_at(struct reader *reader, unsigned long offset)
{
	const unsigned char *p;

	if (offset > reader->size || reader->size - offset < 4)
	{
		reader->failed = 1;
		return 0;
	}
	p = reader->base + offset;
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned long reader_word(struct reader *reader)
{
	unsigned long value = reader_word_at(reader, reader->at);

	reader->at += 4;
	return value;
}

static void copy_name(char *destination, size_t size, const char *source, unsigned long length)
{
	size_t count = length < size - 1 ? (size_t)length : size - 1;
	size_t index;

	/* (up to its first NUL, or count) */
	for (index = 0; index < count && source[index]; index++)
		destination[index] = source[index];
	destination[index] = 0;
}

/* a string of the effect's (a length, then its characters) */
static void reader_string(struct reader *reader, unsigned long offset, char *destination, size_t size)
{
	unsigned long length = reader_word_at(reader, offset);

	destination[0] = 0;
	if (reader->failed || length > reader->size - offset - 4)
	{
		reader->failed = 1;
		return;
	}
	copy_name(destination, size, (const char *)reader->base + offset + 4, length);
}

/* a type's description (the effect's typedef): its kind and size */
struct fx_type
{
	unsigned long type, klass, rows, columns, elements;
	char name[D3D9_EFFECT_NAME_SIZE], semantic[D3D9_EFFECT_NAME_SIZE];
	/* its value's size in words */
	unsigned long words;
};

static unsigned long fx_type_read(struct reader *reader, unsigned long offset, struct fx_type *type, int depth)
{
	unsigned long name_offset, semantic_offset, member_count, member, words = 0;

	memset(type, 0, sizeof(*type));
	if (depth > 4)
	{
		reader->failed = 1;
		return offset;
	}
	type->type = reader_word_at(reader, offset);
	type->klass = reader_word_at(reader, offset + 4);
	name_offset = reader_word_at(reader, offset + 8);
	semantic_offset = reader_word_at(reader, offset + 12);
	type->elements = reader_word_at(reader, offset + 16);
	offset += 20;
	reader_string(reader, name_offset, type->name, sizeof(type->name));
	reader_string(reader, semantic_offset, type->semantic, sizeof(type->semantic));
	if (type->klass <= _d3d9_class_matrix_columns)
	{
		unsigned long first = reader_word_at(reader, offset), second = reader_word_at(reader, offset + 4);

		offset += 8;
		/* (a vector's columns come first, a matrix's rows: a vector is one
		row) */
		if (type->klass == _d3d9_class_vector)
		{
			type->rows = 1;
			type->columns = first * second;
		}
		else
		{
			type->rows = first;
			type->columns = second;
		}
		if (type->rows > 4 || type->columns > 4)
			reader->failed = 1;
		words = type->rows * type->columns;
	}
	else if (type->klass == _d3d9_class_struct)
	{
		member_count = reader_word_at(reader, offset);
		offset += 4;
		for (member = 0; member < member_count && member < 64 && !reader->failed; member++)
		{
			struct fx_type member_type;

			offset = fx_type_read(reader, offset, &member_type, depth + 1);
			words += member_type.words;
		}
	}
	else if (type->klass == _d3d9_class_object)
	{
		/* (an object's value is its id, a sampler's its states) */
		words = 1;
	}
	if (type->elements > 1024)
		reader->failed = 1;
	type->words = words * (type->elements ? type->elements : 1);
	return offset;
}

/* an object's data by id (shaders and the texture names samplers take) */
struct fx_object
{
	const unsigned char *data;
	unsigned long size;
};

/* a state of a pass or a sampler: what it sets, and the object or value */
struct fx_state
{
	unsigned long operation, index;
	unsigned long value_type;
	unsigned long value;
	/* the value's words in the binary */
	unsigned long value_offset;
};

static void fx_state_read(struct reader *reader, struct fx_state *state)
{
	struct fx_type type;
	unsigned long type_offset;

	state->operation = reader_word(reader);
	state->index = reader_word(reader);
	type_offset = reader_word(reader);
	state->value_offset = reader_word(reader);
	fx_type_read(reader, type_offset, &type, 0);
	state->value_type = type.type;
	state->value = reader_word_at(reader, state->value_offset);
}

static void fx_annotations_skip(struct reader *reader, unsigned long count)
{
	if (count > 256)
	{
		reader->failed = 1;
		return;
	}
	reader->at += count * 8;
}

/* ---------- shaders' comment blocks */

/* a comment block's words (after its FOURCC) by FOURCC, in tokens from the
version token on; NULL if there is none */
static const uint32_t *comment_find(const uint32_t *tokens, unsigned long count, unsigned long fourcc,
	unsigned long *words)
{
	unsigned long at = 1;

	while (at < count)
	{
		unsigned long token = tokens[at];

		if (token == OP_END)
			break;
		if ((token & 0xFFFF) == OP_COMMENT)
		{
			unsigned long length = (token >> 16) & 0x7FFF;

			if (length > count - at - 1)
				break;
			if (length >= 1 && tokens[at + 1] == fourcc)
			{
				*words = length - 1;
				return tokens + at + 2;
			}
			at += 1 + length;
		}
		else
		{
			/* (instructions are skipped by their lengths: SM2's on; an
			older shader's comments come before its instructions) */
			unsigned long length = (token >> 24) & 0xF;

			if (!length)
				break;
			at += 1 + length;
		}
	}
	return NULL;
}

/* a constant table (CTAB) into constants (malloc'd) */
static int constant_table_read(const uint32_t *words, unsigned long count, struct d3d9_constant **constants,
	unsigned long *constant_count)
{
	struct reader reader;
	unsigned long constants_offset, number, index;

	*constants = NULL;
	*constant_count = 0;
	reader.base = (const unsigned char *)words;
	reader.size = count * 4;
	reader.at = 0;
	reader.failed = 0;
	number = reader_word_at(&reader, 12);
	constants_offset = reader_word_at(&reader, 16);
	if (reader.failed || number > 1024)
		return 0;
	*constants = calloc(number ? number : 1, sizeof(**constants));
	if (!*constants)
		return 0;
	for (index = 0; index < number; index++)
	{
		unsigned long at = constants_offset + index * 20;
		unsigned long name_offset = reader_word_at(&reader, at);
		unsigned long regset_reg = reader_word_at(&reader, at + 4);
		unsigned long count_reserved = reader_word_at(&reader, at + 8);
		unsigned long type_offset = reader_word_at(&reader, at + 12);
		struct d3d9_constant *constant = &(*constants)[index];
		unsigned long class_type, rows_columns, elements;

		class_type = reader_word_at(&reader, type_offset);
		rows_columns = reader_word_at(&reader, type_offset + 4);
		elements = reader_word_at(&reader, type_offset + 8);
		if (reader.failed || name_offset >= reader.size)
		{
			free(*constants);
			*constants = NULL;
			return 0;
		}
		copy_name(constant->name, sizeof(constant->name), (const char *)reader.base + name_offset,
			reader.size - name_offset);
		constant->regset = (unsigned short)(regset_reg & 0xFFFF);
		constant->reg = (unsigned short)(regset_reg >> 16);
		constant->count = (unsigned short)(count_reserved & 0xFFFF);
		constant->klass = (unsigned short)(class_type & 0xFFFF);
		constant->type = (unsigned short)(class_type >> 16);
		constant->rows = (unsigned short)(rows_columns & 0xFFFF);
		constant->columns = (unsigned short)(rows_columns >> 16);
		constant->elements = (unsigned short)(elements & 0xFFFF);
	}
	*constant_count = number;
	return 1;
}

static void shader_dispose(struct d3d9_shader *shader);

/* a shader's tokens: its version, constant table and preshader (what it
made let go if it is refused: shader_read) */
static int shader_read_parts(const unsigned char *data, unsigned long size, struct d3d9_shader *shader)
{
	const uint32_t *tokens;
	const uint32_t *words;
	unsigned long count, word_count, version;

	memset(shader, 0, sizeof(*shader));
	if (size < 8 || (size & 3) || ((unsigned long)(size_t)data & 3))
		return 0;
	tokens = (const uint32_t *)(const void *)data;
	count = size / 4;
	version = tokens[0];
	if ((version >> 16) != 0xFFFF && (version >> 16) != 0xFFFE)
		return 0;
	shader->pixel = (version >> 16) == 0xFFFF;
	shader->major = (int)((version >> 8) & 0xFF);
	shader->minor = (int)(version & 0xFF);
	shader->tokens = tokens;
	shader->token_count = count;
	words = comment_find(tokens, count, FOURCC_CTAB, &word_count);
	if (words && !constant_table_read(words, word_count, &shader->constants, &shader->constant_count))
		return 0;
	words = comment_find(tokens, count, FOURCC_PRES, &word_count);
	if (words && word_count > 1)
	{
		struct d3d9_preshader *preshader = &shader->preshader;
		const uint32_t *block;
		unsigned long block_count, index;

		/* (its own version token, 'FX' then the version, and its own
		comment blocks) */
		if ((words[0] & 0xFFFF0000) != 0x46580000)
			return 0;
		block = comment_find(words, word_count, FOURCC_CLIT, &block_count);
		if (block && block_count >= 1)
		{
			preshader->literal_count = block[0];
			if (preshader->literal_count * 2 > block_count - 1)
				return 0;
			preshader->literals = calloc(preshader->literal_count + 1, sizeof(double));
			if (!preshader->literals)
				return 0;
			for (index = 0; index < preshader->literal_count; index++)
				memcpy(&preshader->literals[index], &block[1 + index * 2], sizeof(double));
		}
		block = comment_find(words, word_count, FOURCC_FXLC, &block_count);
		if (!block || block_count < 1)
			return 0;
		/* (its instructions' count, then the instructions) */
		preshader->code = block;
		preshader->code_size = block_count;
		block = comment_find(words, word_count, FOURCC_CTAB, &block_count);
		if (block && !constant_table_read(block, block_count, &preshader->inputs, &preshader->input_count))
			return 0;
		for (index = 0; index < preshader->input_count; index++)
		{
			unsigned long end = ((unsigned long)preshader->inputs[index].reg + preshader->inputs[index].count) * 4;

			if (end > preshader->input_components)
				preshader->input_components = end;
		}
		/* (its inputs are as many as a shader's registers at most: a table
		that says more is not one fxc wrote) */
		if (preshader->input_components > D3D9_SHADER_FLOAT_REGISTERS * 4)
			return 0;
		shader->has_preshader = 1;
	}
	return 1;
}

static int shader_read(const unsigned char *data, unsigned long size, struct d3d9_shader *shader)
{
	if (shader_read_parts(data, size, shader))
		return 1;
	shader_dispose(shader);
	return 0;
}

static void shader_dispose(struct d3d9_shader *shader)
{
	free(shader->constants);
	free(shader->preshader.inputs);
	free(shader->preshader.literals);
	memset(shader, 0, sizeof(*shader));
}

/* ---------- the effect */

static void set_error(char *error, size_t size, const char *format, ...)
{
	va_list arguments;

	if (!error || !size)
		return;
	va_start(arguments, format);
	vsnprintf(error, size, format, arguments);
	va_end(arguments);
}

/* an object's shader added to the effect's list (once per object); its
index, or -1 */
static long effect_shader_add(struct d3d9_effect *effect, const struct fx_object *object, long *object_shader)
{
	struct d3d9_shader shader;

	if (*object_shader >= 0)
		return *object_shader;
	if (!object->data || !shader_read(object->data, object->size, &shader))
		return -1;
	effect->shaders[effect->shader_count] = shader;
	*object_shader = (long)effect->shader_count++;
	return *object_shader;
}

int d3d9_effect_read(const void *data, unsigned long size, struct d3d9_effect *effect, char *error, size_t error_size)
{
	struct reader reader;
	unsigned long object_count, parameter_index, technique_index, string_count, resource_count, index;
	struct fx_object *objects = NULL;
	long *object_shaders = NULL;
	/* each state that takes an object: where it is, by state (the pass or
	sampler's) */
	struct fx_state **pass_states = NULL;
	unsigned long **pass_state_counts = NULL;
	struct fx_state **sampler_states = NULL;
	unsigned long *sampler_state_counts = NULL;
	int result = 0;

	memset(effect, 0, sizeof(*effect));
	reader.base = data;
	reader.size = size;
	reader.failed = 0;
	if (size < 24 || reader_word_at(&reader, 0) != FX_SIGNATURE)
	{
		set_error(error, error_size, "not a Direct3D 9 effect (fx_2_0)");
		return 0;
	}
	/* (the offsets that follow are from the end of the 8-byte header) */
	reader.at = reader_word_at(&reader, 4);
	reader.base = (const unsigned char *)data + 8;
	reader.size = size - 8;
	effect->parameter_count = reader_word(&reader);
	effect->technique_count = reader_word(&reader);
	reader_word(&reader);
	object_count = reader_word(&reader);
	if (reader.failed || effect->parameter_count > 1024 || effect->technique_count > 64 || object_count > 4096)
	{
		set_error(error, error_size, "the effect's counts are out of range");
		return 0;
	}
	effect->parameters = calloc(effect->parameter_count + 1, sizeof(*effect->parameters));
	effect->techniques = calloc(effect->technique_count + 1, sizeof(*effect->techniques));
	objects = calloc(object_count + 1, sizeof(*objects));
	object_shaders = malloc((object_count + 1) * sizeof(*object_shaders));
	effect->shaders = calloc(object_count + 1, sizeof(*effect->shaders));
	sampler_states = calloc(effect->parameter_count + 1, sizeof(*sampler_states));
	sampler_state_counts = calloc(effect->parameter_count + 1, sizeof(*sampler_state_counts));
	pass_states = calloc(effect->technique_count + 1, sizeof(*pass_states));
	pass_state_counts = calloc(effect->technique_count + 1, sizeof(*pass_state_counts));
	if (!effect->parameters || !effect->techniques || !objects || !object_shaders || !effect->shaders ||
		!sampler_states || !sampler_state_counts || !pass_states || !pass_state_counts)
	{
		set_error(error, error_size, "out of memory");
		goto done;
	}
	for (index = 0; index <= object_count; index++)
		object_shaders[index] = -1;

	/* the parameters */
	for (parameter_index = 0; parameter_index < effect->parameter_count && !reader.failed; parameter_index++)
	{
		struct d3d9_parameter *parameter = &effect->parameters[parameter_index];
		unsigned long type_offset = reader_word(&reader);
		unsigned long value_offset = reader_word(&reader);
		struct fx_type type;
		int state;

		reader_word(&reader);
		fx_annotations_skip(&reader, reader_word(&reader));
		fx_type_read(&reader, type_offset, &type, 0);
		strcpy(parameter->name, type.name);
		strcpy(parameter->semantic, type.semantic);
		parameter->type = type.type;
		parameter->klass = type.klass;
		parameter->rows = type.rows;
		parameter->columns = type.columns;
		parameter->elements = type.elements;
		parameter->texture = -1;
		for (state = 0; state < NUMBER_OF_D3D9_SAMPLER_STATES; state++)
			parameter->sampler_states[state] = D3D9_SAMPLER_STATE_UNSET;
		if (type.klass <= _d3d9_class_matrix_columns || type.klass == _d3d9_class_struct)
		{
			if (value_offset > reader.size || (reader.size - value_offset) / 4 < type.words)
				reader.failed = 1;
			else
			{
				parameter->defaults = (const uint32_t *)(const void *)(reader.base + value_offset);
				parameter->default_count = type.words;
			}
		}
		else if (type.type >= _d3d9_type_sampler && type.type <= _d3d9_type_samplercube)
		{
			/* (an array of samplers: the first's states) */
			unsigned long count = reader_word_at(&reader, value_offset), saved = reader.at, state_index;

			if (count > 32)
			{
				reader.failed = 1;
				break;
			}
			sampler_states[parameter_index] = calloc(count + 1, sizeof(struct fx_state));
			if (!sampler_states[parameter_index])
			{
				reader.failed = 1;
				break;
			}
			sampler_state_counts[parameter_index] = count;
			reader.at = value_offset + 4;
			for (state_index = 0; state_index < count && !reader.failed; state_index++)
			{
				struct fx_state *sampler_state = &sampler_states[parameter_index][state_index];

				fx_state_read(&reader, sampler_state);
				if (sampler_state->operation > FX_STATE_SAMPLER_TEXTURE &&
					sampler_state->operation - FX_STATE_SAMPLER_TEXTURE < NUMBER_OF_D3D9_SAMPLER_STATES)
				{
					parameter->sampler_states[sampler_state->operation - FX_STATE_SAMPLER_TEXTURE] =
						sampler_state->value;
				}
			}
			reader.at = saved;
		}
	}

	/* the techniques and their passes */
	for (technique_index = 0; technique_index < effect->technique_count && !reader.failed; technique_index++)
	{
		struct d3d9_technique *technique = &effect->techniques[technique_index];
		unsigned long name_offset = reader_word(&reader), pass_index;
		unsigned long annotation_count = reader_word(&reader);

		technique->pass_count = reader_word(&reader);
		fx_annotations_skip(&reader, annotation_count);
		reader_string(&reader, name_offset, technique->name, sizeof(technique->name));
		if (technique->pass_count > 64)
		{
			reader.failed = 1;
			break;
		}
		technique->passes = calloc(technique->pass_count + 1, sizeof(*technique->passes));
		pass_states[technique_index] = calloc((technique->pass_count + 1) * 64, sizeof(struct fx_state));
		pass_state_counts[technique_index] = calloc(technique->pass_count + 1, sizeof(unsigned long));
		if (!technique->passes || !pass_states[technique_index] || !pass_state_counts[technique_index])
		{
			reader.failed = 1;
			break;
		}
		for (pass_index = 0; pass_index < technique->pass_count && !reader.failed; pass_index++)
		{
			struct d3d9_pass *pass = &technique->passes[pass_index];
			unsigned long pass_name = reader_word(&reader), state_count, state_index;
			unsigned long annotation_count = reader_word(&reader);

			state_count = reader_word(&reader);
			fx_annotations_skip(&reader, annotation_count);
			reader_string(&reader, pass_name, pass->name, sizeof(pass->name));
			pass->vertex_shader = pass->pixel_shader = -1;
			if (state_count > 64)
			{
				reader.failed = 1;
				break;
			}
			pass_state_counts[technique_index][pass_index] = state_count;
			for (state_index = 0; state_index < state_count && !reader.failed; state_index++)
				fx_state_read(&reader, &pass_states[technique_index][pass_index * 64 + state_index]);
		}
	}

	/* the objects' data: strings and shared objects, then each state's */
	string_count = reader_word(&reader);
	resource_count = reader_word(&reader);
	if (reader.failed || string_count > 4096 || resource_count > 4096)
	{
		set_error(error, error_size, "the effect's parameters or techniques do not read");
		goto done;
	}
	for (index = 0; index < string_count && !reader.failed; index++)
	{
		unsigned long id = reader_word(&reader);
		unsigned long length = reader_word(&reader);

		if (id >= object_count || length > reader.size - reader.at)
		{
			reader.failed = 1;
			break;
		}
		objects[id].data = reader.base + reader.at;
		objects[id].size = length;
		reader.at += (length + 3) & ~3UL;
	}
	for (index = 0; index < resource_count && !reader.failed; index++)
	{
		unsigned long technique = reader_word(&reader);
		unsigned long item = reader_word(&reader);
		unsigned long element = reader_word(&reader);
		unsigned long state_index = reader_word(&reader);
		unsigned long usage = reader_word(&reader);
		unsigned long length = reader_word(&reader);
		const unsigned char *bytes = reader.base + reader.at;
		struct fx_state *state = NULL;

		(void)element;
		if (length > reader.size - reader.at)
		{
			reader.failed = 1;
			break;
		}
		reader.at += (length + 3) & ~3UL;
		if (technique == 0xFFFFFFFFUL)
		{
			if (item < effect->parameter_count && state_index < sampler_state_counts[item])
				state = &sampler_states[item][state_index];
			/* (a sampler's Texture by the texture parameter's name) */
			if (state && usage == 1 && state->operation == FX_STATE_SAMPLER_TEXTURE)
			{
				char name[D3D9_EFFECT_NAME_SIZE];

				copy_name(name, sizeof(name), (const char *)bytes, length);
				effect->parameters[item].texture = d3d9_effect_parameter_find(effect, name);
			}
			continue;
		}
		if (technique >= effect->technique_count || item >= effect->techniques[technique].pass_count ||
			state_index >= pass_state_counts[technique][item])
		{
			continue;
		}
		state = &pass_states[technique][item * 64 + state_index];
		if ((state->operation == FX_STATE_VERTEX_SHADER || state->operation == FX_STATE_PIXEL_SHADER) &&
			state->value < object_count)
		{
			long shader = -1;
			struct d3d9_pass *pass = &effect->techniques[technique].passes[item];

			if (usage == 0)
			{
				struct fx_object object;

				object.data = bytes;
				object.size = length;
				objects[state->value] = object;
				shader = effect_shader_add(effect, &objects[state->value], &object_shaders[state->value]);
			}
			else if (usage == 1)
			{
				/* (a shader parameter by name: its object) */
				char name[D3D9_EFFECT_NAME_SIZE];
				long parameter;

				copy_name(name, sizeof(name), (const char *)bytes, length);
				parameter = d3d9_effect_parameter_find(effect, name);
				if (parameter >= 0 && effect->parameters[parameter].default_count == 0)
				{
					/* (an object parameter's id is its value word) */
					(void)parameter;
				}
			}
			if (state->operation == FX_STATE_VERTEX_SHADER)
				pass->vertex_shader = shader;
			else
				pass->pixel_shader = shader;
		}
	}
	if (reader.failed)
	{
		set_error(error, error_size, "the effect's objects do not read");
		goto done;
	}
	result = 1;
done:
	for (index = 0; index < effect->parameter_count; index++)
		free(sampler_states ? sampler_states[index] : NULL);
	for (index = 0; index < effect->technique_count; index++)
	{
		free(pass_states ? pass_states[index] : NULL);
		free(pass_state_counts ? pass_state_counts[index] : NULL);
	}
	free(sampler_states);
	free(sampler_state_counts);
	free(pass_states);
	free(pass_state_counts);
	free(objects);
	free(object_shaders);
	if (!result)
		d3d9_effect_dispose(effect);
	return result;
}

void d3d9_effect_dispose(struct d3d9_effect *effect)
{
	unsigned long index;

	for (index = 0; index < effect->shader_count; index++)
		shader_dispose(&effect->shaders[index]);
	for (index = 0; effect->techniques && index < effect->technique_count; index++)
		free(effect->techniques[index].passes);
	free(effect->shaders);
	free(effect->techniques);
	free(effect->parameters);
	memset(effect, 0, sizeof(*effect));
}

long d3d9_effect_parameter_find(const struct d3d9_effect *effect, const char *name)
{
	unsigned long index;

	for (index = 0; name && index < effect->parameter_count; index++)
	{
		if (!strcmp(effect->parameters[index].name, name))
			return (long)index;
	}
	return -1;
}

static int same_ignoring_case(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		int x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;

		if (x != y)
			return 0;
	}
	return *a == *b;
}

long d3d9_effect_parameter_by_semantic(const struct d3d9_effect *effect, const char *semantic)
{
	unsigned long index;

	for (index = 0; semantic && index < effect->parameter_count; index++)
	{
		if (effect->parameters[index].semantic[0] && same_ignoring_case(effect->parameters[index].semantic, semantic))
			return (long)index;
	}
	return -1;
}

long d3d9_effect_technique_find(const struct d3d9_effect *effect, const char *name)
{
	unsigned long index;

	for (index = 0; name && index < effect->technique_count; index++)
	{
		if (!strcmp(effect->techniques[index].name, name))
			return (long)index;
	}
	return -1;
}

/* ---------- the preshader */

/* (its register tables, as the code names them) */
enum
{
	PRES_TABLE_LITERAL = 1,
	PRES_TABLE_INPUT = 2,
	PRES_TABLE_OUTPUT = 4,
	PRES_TABLE_TEMPORARY = 7,
	PRES_TEMPORARIES = 256 * 4,
};

struct pres_context
{
	const struct d3d9_preshader *preshader;
	const float *inputs;
	float *outputs;
	double temporaries[PRES_TEMPORARIES];
};

static int pres_read(struct pres_context *context, unsigned long table, unsigned long offset, double *value)
{
	switch (table)
	{
	case PRES_TABLE_LITERAL:
		if (offset >= context->preshader->literal_count)
			return 0;
		*value = context->preshader->literals[offset];
		return 1;
	case PRES_TABLE_INPUT:
		*value = offset < context->preshader->input_components ? context->inputs[offset] : 0.0;
		return 1;
	case PRES_TABLE_OUTPUT:
		if (offset >= D3D9_SHADER_FLOAT_REGISTERS * 4)
			return 0;
		*value = context->outputs[offset];
		return 1;
	case PRES_TABLE_TEMPORARY:
		if (offset >= PRES_TEMPORARIES)
			return 0;
		*value = context->temporaries[offset];
		return 1;
	}
	return 0;
}

static int pres_write(struct pres_context *context, unsigned long table, unsigned long offset, double value)
{
	if (table == PRES_TABLE_OUTPUT && offset < D3D9_SHADER_FLOAT_REGISTERS * 4)
	{
		context->outputs[offset] = (float)value;
		return 1;
	}
	if (table == PRES_TABLE_TEMPORARY && offset < PRES_TEMPORARIES)
	{
		context->temporaries[offset] = value;
		return 1;
	}
	return 0;
}

int d3d9_preshader_run(const struct d3d9_preshader *preshader, const float *inputs, float *outputs)
{
	struct pres_context *context = calloc(1, sizeof(*context));
	unsigned long at = 0, instruction, instruction_count;
	const uint32_t *code = preshader->code;
	int result = 0;

	if (!context)
		return 0;
	context->preshader = preshader;
	context->inputs = inputs;
	context->outputs = outputs;
	if (preshader->code_size < 1)
		goto done;
	instruction_count = code[0];
	at = 1;
	for (instruction = 0; instruction < instruction_count; instruction++)
	{
		unsigned long word, opcode, components, operand_count, operand, component;
		unsigned long tables[4], offsets[4];
		int scalar;

		if (at + 2 > preshader->code_size)
			goto done;
		word = code[at++];
		opcode = (word >> 20) & 0x7FF;
		components = word & 0xFFFF;
		scalar = (word & 0x80000000UL) != 0;
		operand_count = code[at++];
		if (operand_count > 3 || components > 4 * 64)
			goto done;
		/* (the inputs, then the output: each "relative" (0 here), then its
		table and offset) */
		for (operand = 0; operand <= operand_count; operand++)
		{
			if (at + 3 > preshader->code_size || code[at] != 0)
				goto done;
			tables[operand] = code[at + 1];
			offsets[operand] = code[at + 2];
			at += 3;
		}
		if (opcode == 0x500)
		{
			/* dot: the components summed into one */
			double sum = 0.0;

			for (component = 0; component < components; component++)
			{
				double a, b;

				if (!pres_read(context, tables[0], offsets[0] + (scalar ? 0 : component), &a) ||
					!pres_read(context, tables[1], offsets[1] + component, &b))
				{
					goto done;
				}
				sum += a * b;
			}
			if (!pres_write(context, tables[operand_count], offsets[operand_count], sum))
				goto done;
			continue;
		}
		for (component = 0; component < components; component++)
		{
			double a = 0.0, b = 0.0, c = 0.0, value;

			if (operand_count >= 1 && !pres_read(context, tables[0], offsets[0] + (scalar ? 0 : component), &a))
				goto done;
			if (operand_count >= 2 && !pres_read(context, tables[1], offsets[1] + component, &b))
				goto done;
			if (operand_count >= 3 && !pres_read(context, tables[2], offsets[2] + component, &c))
				goto done;
			switch (opcode)
			{
			case 0x000: value = 0.0; break;
			case 0x100: value = a; break;
			case 0x101: value = -a; break;
			case 0x103: value = a == 0.0 ? INFINITY : 1.0 / a; break;
			case 0x104: value = a - floor(a); break;
			case 0x105: value = exp2(a); break;
			case 0x106: value = a == 0.0 ? -INFINITY : log2(fabs(a)); break;
			case 0x107: value = a == 0.0 ? INFINITY : 1.0 / sqrt(fabs(a)); break;
			case 0x108: value = sin(a); break;
			case 0x109: value = cos(a); break;
			case 0x10A: value = asin(a); break;
			case 0x10B: value = acos(a); break;
			case 0x10C: value = atan(a); break;
			case 0x200: value = a < b ? a : b; break;
			case 0x201: value = a > b ? a : b; break;
			case 0x202: value = a < b ? 1.0 : 0.0; break;
			case 0x203: value = a >= b ? 1.0 : 0.0; break;
			case 0x204: value = a + b; break;
			case 0x205: value = a * b; break;
			case 0x206: value = atan2(a, b); break;
			case 0x208: value = a / b; break;
			case 0x300: value = a >= 0.0 ? b : c; break;
			default:
				goto done;
			}
			if (!pres_write(context, tables[operand_count], offsets[operand_count] + component, value))
				goto done;
		}
	}
	result = 1;
done:
	free(context);
	return result;
}

/* ---------- translation into GLSL */

struct text
{
	char *buffer;
	size_t length, capacity;
	int failed;
};

static void text_append(struct text *text, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void text_append(struct text *text, const char *format, ...)
{
	va_list arguments;
	int needed;

	if (text->failed || text->length > MAXIMUM_GLSL_SIZE)
	{
		text->failed = 1;
		return;
	}
	for (;;)
	{
		va_start(arguments, format);
		needed = vsnprintf(text->buffer ? text->buffer + text->length : NULL,
			text->buffer ? text->capacity - text->length : 0, format, arguments);
		va_end(arguments);
		if (needed < 0)
		{
			text->failed = 1;
			return;
		}
		if (text->buffer && text->length + (size_t)needed < text->capacity)
		{
			text->length += (size_t)needed;
			return;
		}
		text->capacity = (text->capacity + (size_t)needed + 1) * 2;
		text->buffer = realloc(text->buffer, text->capacity);
		if (!text->buffer)
		{
			text->failed = 1;
			return;
		}
	}
}

struct translation
{
	const struct d3d9_shader *shader;
	int pixel, major;
	struct text *out;
	/* where the code goes: main, or a label's function */
	struct text main_text, label_text, *current;
	int in_label, main_done;
	int depth;
	int loop_serial;
	char *error;
	size_t error_size;
	int failed;
	/* the float constants' count, and each that is def'd */
	unsigned long float_count;
	int relative_constants;
	unsigned char defined[D3D9_SHADER_FLOAT_REGISTERS];
	float definitions[D3D9_SHADER_FLOAT_REGISTERS][4];
	unsigned char int_defined[D3D9_SHADER_INT_REGISTERS];
	long int_definitions[D3D9_SHADER_INT_REGISTERS][4];
	unsigned char bool_defined[D3D9_SHADER_BOOL_REGISTERS];
	unsigned long bool_definitions[D3D9_SHADER_BOOL_REGISTERS];
	/* registers used */
	unsigned long temporaries_used;
	unsigned char *sampler_kinds;
	/* inputs and outputs: their varyings' names by register */
	char input_names[16][24];
	unsigned char input_declared[16];
	char output_names[MAXIMUM_OUTPUTS][24];
	unsigned char output_declared[MAXIMUM_OUTPUTS];
	unsigned char output_is_position[MAXIMUM_OUTPUTS];
	unsigned long color_outputs;
	int writes_depth;
	int uses_position, uses_face;
	unsigned char labels[MAXIMUM_LABELS];
	struct d3d9_glsl_varyings varyings_out;
	struct d3d9_glsl_varyings varyings_in;
};

static void translation_fail(struct translation *t, const char *format, ...)
{
	va_list arguments;

	if (t->failed)
		return;
	t->failed = 1;
	if (t->error && t->error_size)
	{
		va_start(arguments, format);
		vsnprintf(t->error, t->error_size, format, arguments);
		va_end(arguments);
	}
}

static void varying_add(struct d3d9_glsl_varyings *varyings, const char *name)
{
	unsigned long index;

	for (index = 0; index < varyings->count; index++)
	{
		if (!strcmp(varyings->names[index], name))
			return;
	}
	if (varyings->count < 16)
		snprintf(varyings->names[varyings->count++], sizeof(varyings->names[0]), "%s", name);
}

static void usage_name(unsigned long usage, unsigned long index, char *name, size_t size)
{
	snprintf(name, size, "%s%lu", usage < NUMBER_OF_USAGES ? usage_names[usage] : "UNKNOWN", index);
}

static unsigned long register_type(unsigned long token)
{
	return ((token >> 28) & 7) | ((token >> 8) & 0x18);
}

static unsigned long register_number(unsigned long token)
{
	return token & 0x7FF;
}

/* parameter tokens an instruction has: SM2's on say so; an older shader's
by its opcode */
static unsigned long instruction_length(const struct translation *t, unsigned long token)
{
	unsigned long opcode = token & 0xFFFF;

	if (t->major >= 2)
		return (token >> 24) & 0xF;
	switch (opcode)
	{
	case OP_NOP: case OP_RET: case OP_ENDLOOP: case OP_ENDREP: case OP_ELSE: case OP_ENDIF: case OP_BREAK:
		return 0;
	case OP_MOV: case OP_RCP: case OP_RSQ: case OP_EXP: case OP_LOG: case OP_LIT: case OP_FRC: case OP_EXPP:
	case OP_LOGP: case OP_DCL: case OP_ABS: case OP_NRM: case OP_SGN: case OP_MOVA:
		return 2;
	case OP_ADD: case OP_SUB: case OP_MUL: case OP_DP3: case OP_DP4: case OP_MIN: case OP_MAX: case OP_SLT:
	case OP_SGE: case OP_DST: case OP_M4x4: case OP_M4x3: case OP_M3x4: case OP_M3x3: case OP_M3x2: case OP_POW:
	case OP_CRS:
		return 3;
	case OP_MAD: case OP_LRP:
		return 4;
	case OP_DEF:
		return 5;
	}
	return 0;
}

static const char *mask_text(unsigned long mask, char *buffer)
{
	static const char components[] = "xyzw";
	int index, length = 0;

	for (index = 0; index < 4; index++)
	{
		if (mask & (1UL << index))
			buffer[length++] = components[index];
	}
	buffer[length] = 0;
	return buffer;
}

/* a varying's GLSL name */
static void varying_name(const char *semantic, char *name, size_t size)
{
	snprintf(name, size, "vr_%s", semantic);
}

/* the GLSL of a register as a source or destination: its name, with
index the relative address's text (NULL for none) */
static void register_text(struct translation *t, unsigned long type, unsigned long number, const char *index,
	int destination, char *text, size_t size)
{
	switch (type)
	{
	case REG_TEMP:
	case REG_TEMPFLOAT16:
		if (number >= MAXIMUM_TEMPORARIES)
		{
			translation_fail(t, "temporary register r%lu", number);
			number = 0;
		}
		t->temporaries_used |= 1UL << number;
		snprintf(text, size, "r%lu", number);
		return;
	case REG_INPUT:
		if (!t->pixel)
		{
			snprintf(text, size, "v%lu", number & 15);
			return;
		}
		if (t->major >= 3)
		{
			if (number < 16 && t->input_declared[number])
			{
				char name[32];

				varying_name(t->input_names[number], name, sizeof(name));
				snprintf(text, size, "%s", name);
				return;
			}
			translation_fail(t, "undeclared input v%lu", number);
			snprintf(text, size, "vec4(0.0)");
			return;
		}
		/* (ps_2_0's colors, clamped as the hardware has them) */
		snprintf(text, size, "in_color%lu", number & 1);
		return;
	case REG_CONST:
	case REG_CONST2:
	case REG_CONST3:
	case REG_CONST4:
		number += (type == REG_CONST2 ? 2048 : type == REG_CONST3 ? 4096 : type == REG_CONST4 ? 6144 : 0);
		if (number >= D3D9_SHADER_FLOAT_REGISTERS)
		{
			translation_fail(t, "float constant c%lu", number);
			number = 0;
		}
		if (index)
			snprintf(text, size, "%sc[clamp(%lu + %s, 0, %lu)]", t->pixel ? "p" : "v", number, index, t->float_count - 1);
		else if (t->defined[number])
			snprintf(text, size, "cdef%lu", number);
		else
			snprintf(text, size, "%sc[%lu]", t->pixel ? "p" : "v", number);
		return;
	case REG_ADDR_TEXTURE:
		if (!t->pixel)
		{
			snprintf(text, size, destination ? "a0" : "vec4(a0)");
			return;
		}
		snprintf(text, size, "vr_TEXCOORD%lu", number & 7);
		{
			char semantic[24];

			snprintf(semantic, sizeof(semantic), "TEXCOORD%lu", number & 7);
			varying_add(&t->varyings_in, semantic);
		}
		return;
	case REG_RASTOUT:
		snprintf(text, size, number == 0 ? "o_position" : number == 1 ? "o_fog" : "o_point");
		return;
	case REG_ATTROUT:
		snprintf(text, size, "vr_COLOR%lu", number & 1);
		{
			char semantic[24];

			snprintf(semantic, sizeof(semantic), "COLOR%lu", number & 1);
			varying_add(&t->varyings_out, semantic);
		}
		return;
	case REG_OUTPUT:
		if (t->major >= 3)
		{
			if (number < MAXIMUM_OUTPUTS && t->output_declared[number])
			{
				if (t->output_is_position[number])
					snprintf(text, size, "o_position");
				else
					varying_name(t->output_names[number], text, size);
				return;
			}
			translation_fail(t, "undeclared output o%lu", number);
			snprintf(text, size, "o_unused");
			return;
		}
		snprintf(text, size, "vr_TEXCOORD%lu", number & 7);
		{
			char semantic[24];

			snprintf(semantic, sizeof(semantic), "TEXCOORD%lu", number & 7);
			varying_add(&t->varyings_out, semantic);
		}
		return;
	case REG_CONSTINT:
		number &= 15;
		if (t->int_defined[number])
			snprintf(text, size, "cidef%lu", number);
		else
			snprintf(text, size, "ivec4(%sci[%lu])", t->pixel ? "p" : "v", number);
		return;
	case REG_COLOROUT:
		number &= 3;
		t->color_outputs |= 1UL << number;
		snprintf(text, size, "o_color%lu", number);
		return;
	case REG_DEPTHOUT:
		t->writes_depth = 1;
		snprintf(text, size, "o_depth");
		return;
	case REG_SAMPLER:
		snprintf(text, size, "s%lu", number & 15);
		return;
	case REG_CONSTBOOL:
		number &= 15;
		if (t->bool_defined[number])
			snprintf(text, size, "%s", t->bool_definitions[number] ? "true" : "false");
		else
			snprintf(text, size, "(%scb[%lu] != 0)", t->pixel ? "p" : "v", number);
		return;
	case REG_LOOP:
		snprintf(text, size, destination ? "aL" : "vec4(float(aL))");
		return;
	case REG_MISCTYPE:
		if (number == 0)
		{
			t->uses_position = 1;
			snprintf(text, size, "vec4(floor(gl_FragCoord.xy), 0.0, 0.0)");
		}
		else
		{
			t->uses_face = 1;
			snprintf(text, size, "vec4(gl_FrontFacing ? 1.0 : -1.0)");
		}
		return;
	case REG_PREDICATE:
		snprintf(text, size, destination ? "p0" : "vec4(p0)");
		return;
	}
	translation_fail(t, "register type %lu", type);
	snprintf(text, size, "vec4(0.0)");
}

/* a source operand as a vec4 expression; extra: the next register's (m4x4's
rows); *consumed: whether its relative address took a token */
static void source_text(struct translation *t, const uint32_t *tokens, unsigned long *at, unsigned long end,
	unsigned long extra, char *text, size_t size)
{
	static const char components[] = "xyzw";
	unsigned long token, swizzle, modifier, type;
	char name[160], index[48], swizzled[200];
	const char *relative = NULL;

	if (*at >= end)
	{
		translation_fail(t, "an instruction's operands are cut short");
		snprintf(text, size, "vec4(0.0)");
		return;
	}
	token = tokens[(*at)++];
	type = register_type(token);
	if (token & (1UL << 13))
	{
		if (t->major >= 2 && (t->pixel || t->major >= 2))
		{
			unsigned long address;

			if (*at >= end)
			{
				translation_fail(t, "a relative address is cut short");
				snprintf(text, size, "vec4(0.0)");
				return;
			}
			address = tokens[(*at)++];
			if (register_type(address) == REG_LOOP)
				snprintf(index, sizeof(index), "aL");
			else
				snprintf(index, sizeof(index), "a0.%c", components[(address >> 16) & 3]);
		}
		else
			snprintf(index, sizeof(index), "a0.x");
		relative = index;
		t->relative_constants = 1;
	}
	register_text(t, type, register_number(token) + extra, relative, 0, name, sizeof(name));
	swizzle = (token >> 16) & 0xFF;
	modifier = (token >> 24) & 0xF;
	if (type == REG_CONSTBOOL)
	{
		snprintf(text, size, "%s%s", modifier == 13 ? "!" : "", name);
		return;
	}
	if (type == REG_PREDICATE)
	{
		snprintf(swizzled, sizeof(swizzled), "bvec4(p0.%c%c%c%c)", components[swizzle & 3],
			components[(swizzle >> 2) & 3], components[(swizzle >> 4) & 3], components[(swizzle >> 6) & 3]);
		if (modifier == 13)
			snprintf(text, size, "not(%s)", swizzled);
		else
			snprintf(text, size, "%s", swizzled);
		return;
	}
	if (type == REG_CONSTINT)
	{
		snprintf(text, size, "vec4(%s)", name);
		return;
	}
	if (swizzle == 0xE4)
		snprintf(swizzled, sizeof(swizzled), "%s", name);
	else
		snprintf(swizzled, sizeof(swizzled), "%s.%c%c%c%c", name, components[swizzle & 3],
			components[(swizzle >> 2) & 3], components[(swizzle >> 4) & 3], components[(swizzle >> 6) & 3]);
	switch (modifier)
	{
	case 0: snprintf(text, size, "%s", swizzled); break;
	case 1: snprintf(text, size, "(-%s)", swizzled); break;
	case 2: snprintf(text, size, "(%s - 0.5)", swizzled); break;
	case 3: snprintf(text, size, "(0.5 - %s)", swizzled); break;
	case 4: snprintf(text, size, "(2.0 * %s - 1.0)", swizzled); break;
	case 5: snprintf(text, size, "(1.0 - 2.0 * %s)", swizzled); break;
	case 6: snprintf(text, size, "(1.0 - %s)", swizzled); break;
	case 7: snprintf(text, size, "(2.0 * %s)", swizzled); break;
	case 8: snprintf(text, size, "(-2.0 * %s)", swizzled); break;
	case 9: snprintf(text, size, "(%s / %s.z)", swizzled, swizzled); break;
	case 10: snprintf(text, size, "(%s / %s.w)", swizzled, swizzled); break;
	case 11: snprintf(text, size, "abs(%s)", swizzled); break;
	case 12: snprintf(text, size, "(-abs(%s))", swizzled); break;
	default:
		translation_fail(t, "source modifier %lu", modifier);
		snprintf(text, size, "%s", swizzled);
		break;
	}
}

/* the destination of an instruction: its register's text and write mask */
struct destination
{
	unsigned long token;
	unsigned long type;
	char name[64];
	unsigned long mask;
	int saturate;
	int shift;
	/* the predicate's swizzle text ("" without) */
	char predicate[64];
};

static void destination_read(struct translation *t, const uint32_t *tokens, unsigned long *at, unsigned long end,
	struct destination *destination)
{
	unsigned long token, shift;

	memset(destination, 0, sizeof(*destination));
	if (*at >= end)
	{
		translation_fail(t, "an instruction has no destination");
		return;
	}
	token = tokens[(*at)++];
	destination->token = token;
	destination->type = register_type(token);
	if (token & (1UL << 13))
	{
		translation_fail(t, "a relative destination");
		if (t->major >= 2 && *at < end)
			(*at)++;
	}
	register_text(t, destination->type, register_number(token), NULL, 1, destination->name,
		sizeof(destination->name));
	destination->mask = (token >> 16) & 0xF;
	destination->saturate = ((token >> 20) & 1) != 0;
	shift = (token >> 24) & 0xF;
	destination->shift = shift >= 8 ? (int)shift - 16 : (int)shift;
}

/* dst = expression (a vec4's), masked, saturated and predicated */
static void destination_write(struct translation *t, const struct destination *destination, const char *expression)
{
	char mask[8], value[1400];

	if (t->failed)
		return;
	if (destination->shift)
		snprintf(value, sizeof(value), "((%s) * %g)", expression, ldexp(1.0, destination->shift));
	else
		snprintf(value, sizeof(value), "(%s)", expression);
	if (destination->saturate)
	{
		char saturated[1420];

		snprintf(saturated, sizeof(saturated), "clamp(%s, 0.0, 1.0)", value);
		snprintf(value, sizeof(value), "%s", saturated);
	}
	mask_text(destination->mask ? destination->mask : 0xF, mask);
	if (destination->type == REG_DEPTHOUT)
	{
		text_append(t->current, "\to_depth = %s.x;\n", value);
		return;
	}
	if (destination->type == REG_ADDR_TEXTURE && !t->pixel)
	{
		/* (an address register takes the value rounded: vs_1_1's mov
		floors) */
		text_append(t->current, "\ta0.%s = ivec4(floor(%s%s)).%s;\n", mask, t->major >= 2 ? "0.5 + " : "", value,
			mask);
		return;
	}
	if (destination->predicate[0])
	{
		text_append(t->current, "\t%s.%s = mix(%s, vec4(%s), %s).%s;\n", destination->name, mask, destination->name,
			value, destination->predicate, mask);
		return;
	}
	text_append(t->current, "\t%s.%s = vec4(%s).%s;\n", destination->name, mask, value, mask);
}

static const char *comparison_text(unsigned long control)
{
	switch (control)
	{
	case 1: return ">";
	case 2: return "==";
	case 3: return ">=";
	case 4: return "<";
	case 5: return "!=";
	case 6: return "<=";
	}
	return "==";
}

static const char *comparison_function(unsigned long control)
{
	switch (control)
	{
	case 1: return "greaterThan";
	case 2: return "equal";
	case 3: return "greaterThanEqual";
	case 4: return "lessThan";
	case 5: return "notEqual";
	case 6: return "lessThanEqual";
	}
	return "equal";
}

/* the declarations (dcl, def, defi, defb) read first, so that the code
knows each register */
static void declarations_read(struct translation *t)
{
	const uint32_t *tokens = t->shader->tokens;
	unsigned long count = t->shader->token_count, at = 1;

	while (at < count && !t->failed)
	{
		unsigned long token = tokens[at], opcode = token & 0xFFFF, length;

		if (token == OP_END)
			break;
		if (opcode == OP_COMMENT)
		{
			at += 1 + ((token >> 16) & 0x7FFF);
			continue;
		}
		length = instruction_length(t, token);
		if (at + 1 + length > count)
		{
			translation_fail(t, "an instruction runs past the end");
			return;
		}
		if (opcode == OP_DEF && length >= 5)
		{
			unsigned long reg = register_number(tokens[at + 1]);

			if (reg < D3D9_SHADER_FLOAT_REGISTERS && register_type(tokens[at + 1]) == REG_CONST)
			{
				t->defined[reg] = 1;
				memcpy(t->definitions[reg], &tokens[at + 2], sizeof(float) * 4);
			}
		}
		else if (opcode == OP_DEFI && length >= 5)
		{
			unsigned long reg = register_number(tokens[at + 1]) & 15;
			int component;

			t->int_defined[reg] = 1;
			for (component = 0; component < 4; component++)
				t->int_definitions[reg][component] = (long)(int)tokens[at + 2 + component];
		}
		else if (opcode == OP_DEFB && length >= 2)
		{
			unsigned long reg = register_number(tokens[at + 1]) & 15;

			t->bool_defined[reg] = 1;
			t->bool_definitions[reg] = tokens[at + 2];
		}
		else if (opcode == OP_DCL && length >= 2)
		{
			unsigned long info = tokens[at + 1], destination = tokens[at + 2];
			unsigned long type = register_type(destination), number = register_number(destination);
			unsigned long usage = info & 0x1F, usage_index = (info >> 16) & 0xF;

			if (type == REG_SAMPLER && number < D3D9_SHADER_SAMPLERS)
			{
				unsigned long kind = (info >> 27) & 0xF;

				t->sampler_kinds[number] = (unsigned char)(kind >= 2 && kind <= 4 ? kind : 2);
			}
			else if (type == REG_INPUT && t->pixel && t->major >= 3 && number < 16)
			{
				usage_name(usage, usage_index, t->input_names[number], sizeof(t->input_names[0]));
				t->input_declared[number] = 1;
				varying_add(&t->varyings_in, t->input_names[number]);
			}
			else if (type == REG_INPUT && !t->pixel && number < 16)
			{
				usage_name(usage, usage_index, t->input_names[number], sizeof(t->input_names[0]));
				t->input_declared[number] = 1;
			}
			else if (type == REG_OUTPUT && !t->pixel && t->major >= 3 && number < MAXIMUM_OUTPUTS)
			{
				usage_name(usage, usage_index, t->output_names[number], sizeof(t->output_names[0]));
				t->output_declared[number] = 1;
				t->output_is_position[number] = usage == USAGE_POSITION && usage_index == 0;
				if (!t->output_is_position[number])
					varying_add(&t->varyings_out, t->output_names[number]);
			}
		}
		else if (opcode == OP_TEX && t->pixel && t->major < 2)
		{
			translation_fail(t, "ps_1_x texture instructions");
			return;
		}
		at += 1 + length;
	}
}

/* the float constants' count the code reads: those it names, the
constant table's, the preshader's outputs */
static void float_count_find(struct translation *t)
{
	const uint32_t *tokens = t->shader->tokens;
	unsigned long count = t->shader->token_count, at = 1, highest = 0, index;

	while (at < count)
	{
		unsigned long token = tokens[at], opcode = token & 0xFFFF, length, operand;

		if (token == OP_END)
			break;
		if (opcode == OP_COMMENT)
		{
			at += 1 + ((token >> 16) & 0x7FFF);
			continue;
		}
		length = instruction_length(t, token);
		if (opcode != OP_DEF && opcode != OP_DCL)
		{
			for (operand = 1; operand <= length && at + operand < count; operand++)
			{
				unsigned long parameter = tokens[at + operand];

				if (!(parameter & 0x80000000UL))
					continue;
				if (register_type(parameter) == REG_CONST && register_number(parameter) + 4 > highest)
					highest = register_number(parameter) + 4;
				if (parameter & (1UL << 13))
					t->relative_constants = 1;
			}
		}
		at += 1 + length;
	}
	for (index = 0; index < t->shader->constant_count; index++)
	{
		const struct d3d9_constant *constant = &t->shader->constants[index];

		if (constant->regset == _d3d9_regset_float4 && (unsigned long)constant->reg + constant->count > highest)
			highest = (unsigned long)constant->reg + constant->count;
	}
	if (t->relative_constants && !t->pixel)
		highest = D3D9_SHADER_FLOAT_REGISTERS;
	if (highest > D3D9_SHADER_FLOAT_REGISTERS)
		highest = D3D9_SHADER_FLOAT_REGISTERS;
	t->float_count = highest ? highest : 1;
}

static void instruction_translate(struct translation *t, const uint32_t *tokens, unsigned long at,
	unsigned long end)
{
	unsigned long token = tokens[at], opcode = token & 0xFFFF, control = (token >> 16) & 0xFF;
	struct destination destination;
	char a[256], b[256], c[256], d[256], expression[1200];
	unsigned long cursor = at + 1;
	char predicate[128];

	predicate[0] = 0;
	switch (opcode)
	{
	case OP_NOP:
	case OP_DCL:
	case OP_DEF:
	case OP_DEFI:
	case OP_DEFB:
	case OP_PHASE:
		return;
	case OP_LABEL:
	{
		unsigned long label = register_number(tokens[cursor]);

		if (label >= MAXIMUM_LABELS)
		{
			translation_fail(t, "label %lu", label);
			return;
		}
		if (t->in_label)
			text_append(t->current, "}\n");
		t->labels[label] = 1;
		t->in_label = 1;
		t->current = &t->label_text;
		t->depth = 0;
		text_append(t->current, "void label%lu()\n{\n", label);
		return;
	}
	case OP_RET:
		if (t->in_label && t->depth == 0)
		{
			text_append(t->current, "}\n");
			t->in_label = 0;
			t->current = &t->main_text;
			t->main_done = 1;
		}
		else if (!t->in_label && t->depth == 0)
			t->main_done = 1;
		else
			text_append(t->current, "\treturn;\n");
		return;
	case OP_CALL:
		text_append(t->current, "\tlabel%lu();\n", register_number(tokens[cursor]) % MAXIMUM_LABELS);
		return;
	case OP_CALLNZ:
	{
		unsigned long label = register_number(tokens[cursor++]) % MAXIMUM_LABELS;

		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		text_append(t->current, "\tif (%s%s) label%lu();\n", a, strncmp(a, "bvec4", 5) ? "" : ".x", label);
		return;
	}
	case OP_IF:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		text_append(t->current, "\tif (%s%s) {\n", a, strncmp(a, "bvec4", 5) && strncmp(a, "not(", 4) ? "" : ".x");
		t->depth++;
		if (t->depth > MAXIMUM_NESTING)
			translation_fail(t, "ifs and loops nested past %d", MAXIMUM_NESTING);
		return;
	case OP_IFC:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		text_append(t->current, "\tif ((%s).x %s (%s).x) {\n", a, comparison_text(control), b);
		t->depth++;
		if (t->depth > MAXIMUM_NESTING)
			translation_fail(t, "ifs and loops nested past %d", MAXIMUM_NESTING);
		return;
	case OP_ELSE:
		text_append(t->current, "\t} else {\n");
		return;
	case OP_ENDIF:
	case OP_ENDREP:
	case OP_ENDLOOP:
		if (t->depth > 0)
			t->depth--;
		text_append(t->current, "\t}\n");
		return;
	case OP_REP:
	{
		int serial = t->loop_serial++;

		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		text_append(t->current, "\tfor (int k%d = 0; k%d < min(int((%s).x), %d); k%d++) {\n", serial, serial, a,
			MAXIMUM_LOOP_ITERATIONS, serial);
		t->depth++;
		if (t->depth > MAXIMUM_NESTING)
			translation_fail(t, "ifs and loops nested past %d", MAXIMUM_NESTING);
		return;
	}
	case OP_LOOP:
	{
		int serial = t->loop_serial++;

		cursor++;
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		text_append(t->current, "\tfor (int k%d = 0; k%d < min(int((%s).x), %d); k%d++) {\n"
			"\taL = int((%s).y) + k%d * int((%s).z);\n", serial, serial, a, MAXIMUM_LOOP_ITERATIONS, serial, a,
			serial, a);
		t->depth++;
		if (t->depth > MAXIMUM_NESTING)
			translation_fail(t, "ifs and loops nested past %d", MAXIMUM_NESTING);
		return;
	}
	case OP_BREAK:
		text_append(t->current, "\tbreak;\n");
		return;
	case OP_BREAKC:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		text_append(t->current, "\tif ((%s).x %s (%s).x) break;\n", a, comparison_text(control), b);
		return;
	case OP_BREAKP:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		text_append(t->current, "\tif (%s.x) break;\n", a);
		return;
	case OP_TEXKILL:
	{
		char mask[8];
		unsigned long kill = tokens[cursor];
		unsigned long kill_mask = (kill >> 16) & 0xF;
		struct destination operand;

		destination_read(t, tokens, &cursor, end, &operand);
		mask_text(kill_mask ? kill_mask : (t->major >= 2 ? 0xF : 0x7), mask);
		text_append(t->current, "\tif (any(lessThan(vec4(%s).%s, vec4(0.0).%s))) discard;\n", operand.name, mask,
			mask);
		return;
	}
	}

	destination_read(t, tokens, &cursor, end, &destination);
	if (token & (1UL << 28))
	{
		/* (predicated: the predicate's swizzle follows the destination) */
		source_text(t, tokens, &cursor, end, 0, predicate, sizeof(predicate));
		snprintf(destination.predicate, sizeof(destination.predicate), "%s", predicate);
	}
	switch (opcode)
	{
	case OP_MOV:
	case OP_MOVA:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		snprintf(expression, sizeof(expression), "%s", a);
		break;
	case OP_ADD:
	case OP_SUB:
	case OP_MUL:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		snprintf(expression, sizeof(expression), "%s %s %s", a, opcode == OP_ADD ? "+" : opcode == OP_SUB ? "-" : "*",
			b);
		break;
	case OP_MAD:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		source_text(t, tokens, &cursor, end, 0, c, sizeof(c));
		snprintf(expression, sizeof(expression), "%s * %s + %s", a, b, c);
		break;
	case OP_RCP:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		snprintf(expression, sizeof(expression), "1.0 / vec4(%s).w", a);
		break;
	case OP_RSQ:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		snprintf(expression, sizeof(expression), "inversesqrt(abs(vec4(%s).w))", a);
		break;
	case OP_EXP:
	case OP_EXPP:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		snprintf(expression, sizeof(expression), "exp2(vec4(%s).w)", a);
		break;
	case OP_LOG:
	case OP_LOGP:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		snprintf(expression, sizeof(expression), "log2(abs(vec4(%s).w))", a);
		break;
	case OP_POW:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		snprintf(expression, sizeof(expression), "pow(abs(vec4(%s).w), vec4(%s).w)", a, b);
		break;
	case OP_DP3:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		snprintf(expression, sizeof(expression), "dot(vec4(%s).xyz, vec4(%s).xyz)", a, b);
		break;
	case OP_DP4:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		snprintf(expression, sizeof(expression), "dot(vec4(%s), vec4(%s))", a, b);
		break;
	case OP_DP2ADD:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		source_text(t, tokens, &cursor, end, 0, c, sizeof(c));
		snprintf(expression, sizeof(expression), "dot(vec4(%s).xy, vec4(%s).xy) + vec4(%s).x", a, b, c);
		break;
	case OP_MIN:
	case OP_MAX:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		snprintf(expression, sizeof(expression), "%s(vec4(%s), vec4(%s))", opcode == OP_MIN ? "min" : "max", a, b);
		break;
	case OP_SLT:
	case OP_SGE:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		snprintf(expression, sizeof(expression), "vec4(%s(vec4(%s), vec4(%s)))",
			opcode == OP_SLT ? "lessThan" : "greaterThanEqual", a, b);
		break;
	case OP_LRP:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		source_text(t, tokens, &cursor, end, 0, c, sizeof(c));
		snprintf(expression, sizeof(expression), "mix(vec4(%s), vec4(%s), vec4(%s))", c, b, a);
		break;
	case OP_FRC:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		snprintf(expression, sizeof(expression), "fract(vec4(%s))", a);
		break;
	case OP_ABS:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		snprintf(expression, sizeof(expression), "abs(vec4(%s))", a);
		break;
	case OP_SGN:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		snprintf(expression, sizeof(expression), "sign(vec4(%s))", a);
		break;
	case OP_NRM:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		snprintf(expression, sizeof(expression), "d3d9_nrm(vec4(%s))", a);
		break;
	case OP_CRS:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		snprintf(expression, sizeof(expression), "vec4(cross(vec4(%s).xyz, vec4(%s).xyz), 0.0)", a, b);
		break;
	case OP_CMP:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		source_text(t, tokens, &cursor, end, 0, c, sizeof(c));
		snprintf(expression, sizeof(expression), "mix(vec4(%s), vec4(%s), greaterThanEqual(vec4(%s), vec4(0.0)))", c,
			b, a);
		break;
	case OP_CND:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		source_text(t, tokens, &cursor, end, 0, c, sizeof(c));
		snprintf(expression, sizeof(expression), "mix(vec4(%s), vec4(%s), greaterThan(vec4(%s), vec4(0.5)))", c, b,
			a);
		break;
	case OP_SINCOS:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		/* (ps_2_0 and vs_2_0 take two constants as well: their own
		approximation's, not needed here) */
		snprintf(expression, sizeof(expression), "vec4(cos(vec4(%s).w), sin(vec4(%s).w), 0.0, 0.0)", a, a);
		break;
	case OP_LIT:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		snprintf(expression, sizeof(expression), "d3d9_lit(vec4(%s))", a);
		break;
	case OP_DST:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		snprintf(expression, sizeof(expression), "vec4(1.0, vec4(%s).y * vec4(%s).y, vec4(%s).z, vec4(%s).w)", a, b,
			a, b);
		break;
	case OP_M4x4:
	case OP_M4x3:
	case OP_M3x4:
	case OP_M3x3:
	case OP_M3x2:
	{
		int rows = opcode == OP_M4x4 || opcode == OP_M3x4 ? 4 : opcode == OP_M3x2 ? 2 : 3;
		int four = opcode == OP_M4x4 || opcode == OP_M4x3;
		char row[4][256];
		unsigned long matrix_at;
		int index;

		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		matrix_at = cursor;
		for (index = 0; index < rows; index++)
		{
			unsigned long row_cursor = matrix_at;

			source_text(t, tokens, &row_cursor, end, (unsigned long)index, row[index], sizeof(row[index]));
			cursor = row_cursor;
		}
		snprintf(expression, sizeof(expression), "vec4(");
		for (index = 0; index < 4; index++)
		{
			char part[600];

			if (index < rows)
				snprintf(part, sizeof(part), four ? "dot(vec4(%s), vec4(%s))" : "dot(vec4(%s).xyz, vec4(%s).xyz)", a,
					row[index]);
			else
				snprintf(part, sizeof(part), "0.0");
			strncat(expression, part, sizeof(expression) - strlen(expression) - 4);
			strncat(expression, index < 3 ? ", " : ")", sizeof(expression) - strlen(expression) - 1);
		}
		break;
	}
	case OP_TEX:
	case OP_TEXLDL:
	case OP_TEXLDD:
	{
		unsigned long sampler_token, sampler, swizzle;
		int kind;
		const char *coordinates;

		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		if (cursor >= end)
		{
			translation_fail(t, "a texture instruction without its sampler");
			return;
		}
		sampler_token = tokens[cursor++];
		sampler = register_number(sampler_token) & 15;
		swizzle = (sampler_token >> 16) & 0xFF;
		kind = t->sampler_kinds[sampler] ? t->sampler_kinds[sampler] : 2;
		t->sampler_kinds[sampler] = (unsigned char)kind;
		coordinates = kind == 2 ? "xy" : "xyz";
		if (opcode == OP_TEXLDL)
			snprintf(expression, sizeof(expression), "textureLod(s%lu, vec4(%s).%s, vec4(%s).w)", sampler, a,
				coordinates, a);
		else if (opcode == OP_TEXLDD)
		{
			source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
			source_text(t, tokens, &cursor, end, 0, c, sizeof(c));
			snprintf(expression, sizeof(expression), "textureGrad(s%lu, vec4(%s).%s, vec4(%s).%s, vec4(%s).%s)",
				sampler, a, coordinates, b, coordinates, c, coordinates);
		}
		else if (control == 1)
		{
			if (kind == 2)
				snprintf(expression, sizeof(expression), "textureProj(s%lu, vec4(%s))", sampler, a);
			else
				snprintf(expression, sizeof(expression), "texture(s%lu, vec4(%s).xyz / vec4(%s).w)", sampler, a, a);
		}
		else if (control == 2)
			snprintf(expression, sizeof(expression), "texture(s%lu, vec4(%s).%s, vec4(%s).w)", sampler, a, coordinates,
				a);
		else
			snprintf(expression, sizeof(expression), "texture(s%lu, vec4(%s).%s)", sampler, a, coordinates);
		if (swizzle != 0xE4)
		{
			static const char components[] = "xyzw";
			char swizzled[1300];

			snprintf(swizzled, sizeof(swizzled), "(%s).%c%c%c%c", expression, components[swizzle & 3],
				components[(swizzle >> 2) & 3], components[(swizzle >> 4) & 3], components[(swizzle >> 6) & 3]);
			snprintf(expression, sizeof(expression), "%s", swizzled);
		}
		break;
	}
	case OP_DSX:
	case OP_DSY:
		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		snprintf(expression, sizeof(expression), "%s(vec4(%s))", opcode == OP_DSX ? "dFdx" : "dFdy", a);
		break;
	case OP_SETP:
	{
		char mask[8];

		source_text(t, tokens, &cursor, end, 0, a, sizeof(a));
		source_text(t, tokens, &cursor, end, 0, b, sizeof(b));
		mask_text(destination.mask ? destination.mask : 0xF, mask);
		text_append(t->current, "\tp0.%s = %s(vec4(%s), vec4(%s)).%s;\n", mask, comparison_function(control), a, b,
			mask);
		return;
	}
	default:
		translation_fail(t, "opcode %lu", opcode);
		return;
	}
	(void)d;
	destination_write(t, &destination, expression);
}

static void body_translate(struct translation *t)
{
	const uint32_t *tokens = t->shader->tokens;
	unsigned long count = t->shader->token_count, at = 1;

	t->current = &t->main_text;
	while (at < count && !t->failed)
	{
		unsigned long token = tokens[at], opcode = token & 0xFFFF, length;

		if (token == OP_END)
			break;
		if (opcode == OP_COMMENT)
		{
			at += 1 + ((token >> 16) & 0x7FFF);
			continue;
		}
		length = instruction_length(t, token);
		if (at + 1 + length > count)
		{
			translation_fail(t, "an instruction runs past the end");
			return;
		}
		if (!t->main_done || t->in_label || opcode == OP_LABEL)
			instruction_translate(t, tokens, at, at + 1 + length);
		at += 1 + length;
	}
	if (t->in_label)
		text_append(&t->label_text, "}\n");
}

static void float_text(float value, char *text, size_t size)
{
	if (value != value)
		snprintf(text, size, "(0.0 / 0.0)");
	else if (isinf(value))
		snprintf(text, size, value > 0 ? "(1.0 / 0.0)" : "(-1.0 / 0.0)");
	else
		snprintf(text, size, "%.9g", (double)value);
}

char *d3d9_shader_glsl(const struct d3d9_shader *shader, const struct d3d9_glsl_varyings *want,
	struct d3d9_glsl_varyings *inputs, unsigned long *float_count, unsigned char sampler_kinds[D3D9_SHADER_SAMPLERS],
	char *error, size_t error_size)
{
	struct translation *t = calloc(1, sizeof(*t));
	struct text out = { 0 };
	unsigned long index;
	char *result = NULL;

	if (!t)
		return NULL;
	memset(sampler_kinds, 0, D3D9_SHADER_SAMPLERS);
	t->shader = shader;
	t->pixel = shader->pixel;
	t->major = shader->major;
	t->error = error;
	t->error_size = error_size;
	t->sampler_kinds = sampler_kinds;
	if (shader->pixel ? shader->major < 2 : shader->major < 1)
	{
		translation_fail(t, "%s_%d_%d is too old to translate", shader->pixel ? "ps" : "vs", shader->major,
			shader->minor);
		goto done;
	}
	declarations_read(t);
	float_count_find(t);
	body_translate(t);
	if (t->failed || t->main_text.failed || t->label_text.failed)
	{
		if (!t->failed)
			translation_fail(t, "the GLSL is too long, or out of memory");
		goto done;
	}

	/* the declarations */
	text_append(&out, "uniform vec4 %sc[%lu];\nuniform vec4 %sci[%d];\nuniform int %scb[%d];\n", t->pixel ? "p" : "v",
		t->float_count, t->pixel ? "p" : "v", D3D9_SHADER_INT_REGISTERS, t->pixel ? "p" : "v", D3D9_SHADER_BOOL_REGISTERS);
	for (index = 0; index < D3D9_SHADER_FLOAT_REGISTERS; index++)
	{
		if (t->defined[index])
		{
			char x[48], y[48], z[48], w[48];

			float_text(t->definitions[index][0], x, sizeof(x));
			float_text(t->definitions[index][1], y, sizeof(y));
			float_text(t->definitions[index][2], z, sizeof(z));
			float_text(t->definitions[index][3], w, sizeof(w));
			text_append(&out, "const vec4 cdef%lu = vec4(%s, %s, %s, %s);\n", index, x, y, z, w);
		}
	}
	for (index = 0; index < D3D9_SHADER_INT_REGISTERS; index++)
	{
		if (t->int_defined[index])
			text_append(&out, "const ivec4 cidef%lu = ivec4(%ld, %ld, %ld, %ld);\n", index, t->int_definitions[index][0],
				t->int_definitions[index][1], t->int_definitions[index][2], t->int_definitions[index][3]);
	}
	for (index = 0; index < D3D9_SHADER_SAMPLERS; index++)
	{
		if (sampler_kinds[index])
			text_append(&out, "uniform %s s%lu;\n", sampler_kinds[index] == 3 ? "samplerCube" :
				sampler_kinds[index] == 4 ? "sampler3D" : "sampler2D", index);
	}
	for (index = 0; index < MAXIMUM_TEMPORARIES; index++)
	{
		if (t->temporaries_used & (1UL << index))
			text_append(&out, "vec4 r%lu = vec4(0.0);\n", index);
	}
	text_append(&out,
		"ivec4 a0 = ivec4(0);\n"
		"int aL = 0;\n"
		"bvec4 p0 = bvec4(false);\n"
		"vec4 d3d9_nrm(vec4 v)\n{\n\tfloat l = dot(v.xyz, v.xyz);\n\treturn l > 0.0 ? v * inversesqrt(l) : vec4(0.0);\n}\n"
		"vec4 d3d9_lit(vec4 v)\n{\n\treturn vec4(1.0, max(v.x, 0.0), v.x > 0.0 ? "
		"pow(max(v.y, 0.0), clamp(v.w, -128.0, 128.0)) : 0.0, 1.0);\n}\n");
	if (t->pixel)
	{
		for (index = 0; index < t->varyings_in.count; index++)
			text_append(&out, "in vec4 vr_%s;\n", t->varyings_in.names[index]);
		if (t->major < 3)
		{
			/* (ps_2_0's two colors, whether or not it reads them) */
			text_append(&out, "in vec4 vr_COLOR0;\nin vec4 vr_COLOR1;\nvec4 in_color0, in_color1;\n");
			varying_add(&t->varyings_in, "COLOR0");
			varying_add(&t->varyings_in, "COLOR1");
		}
		for (index = 0; index < 4; index++)
		{
			if (t->color_outputs & (1UL << index))
				text_append(&out, "layout(location = %lu) out vec4 o_color%lu;\n", index, index);
		}
		if (!t->color_outputs)
			text_append(&out, "layout(location = 0) out vec4 o_color0;\n");
		text_append(&out, "float o_depth = 0.0;\n");
	}
	else
	{
		struct d3d9_glsl_varyings all = t->varyings_out;

		for (index = 0; index < 16; index++)
		{
			if (t->input_declared[index])
			{
				unsigned long location = 9;
				const char *name = t->input_names[index];

				if (!strcmp(name, "POSITION0"))
					location = 0;
				else if (!strncmp(name, "TEXCOORD", 8) && name[8] >= '0' && name[8] <= '7' && !name[9])
					location = 1 + (unsigned long)(name[8] - '0');
				if (location == 9)
					text_append(&out, "vec4 v%lu = vec4(0.0, 0.0, 0.0, 1.0);\n", index);
				else
					text_append(&out, "layout(location = %lu) in vec4 v%lu;\n", location, index);
			}
		}
		for (index = 0; want && index < want->count; index++)
			varying_add(&all, want->names[index]);
		for (index = 0; index < all.count; index++)
			text_append(&out, "out vec4 vr_%s;\n", all.names[index]);
		t->varyings_out = all;
		text_append(&out,
			"uniform vec4 d3d9_pixel_fix;\n"
			"vec4 o_position = vec4(0.0, 0.0, 0.0, 1.0);\n"
			"vec4 o_fog = vec4(0.0);\nvec4 o_point = vec4(0.0);\nvec4 o_unused = vec4(0.0);\n");
	}
	for (index = 0; index < MAXIMUM_LABELS; index++)
	{
		if (t->labels[index])
			text_append(&out, "void label%lu();\n", index);
	}
	if (t->label_text.buffer)
		text_append(&out, "%s", t->label_text.buffer);
	text_append(&out, "void main()\n{\n");
	if (t->pixel && t->major < 3)
		text_append(&out, "\tin_color0 = clamp(vr_COLOR0, 0.0, 1.0);\n\tin_color1 = clamp(vr_COLOR1, 0.0, 1.0);\n");
	if (!t->pixel)
	{
		for (index = 0; index < t->varyings_out.count; index++)
			text_append(&out, "\tvr_%s = vec4(0.0, 0.0, 0.0, 1.0);\n", t->varyings_out.names[index]);
	}
	if (t->main_text.buffer)
		text_append(&out, "%s", t->main_text.buffer);
	if (t->pixel)
	{
		if (t->writes_depth)
			text_append(&out, "\tgl_FragDepth = o_depth;\n");
	}
	else
	{
		/* (Direct3D 9's pixel centers are half a pixel off OpenGL's; and
		rows from the top, as the game's targets are) */
		text_append(&out,
			"\tgl_Position = o_position;\n"
			"\tgl_Position.xy += vec2(d3d9_pixel_fix.x, -d3d9_pixel_fix.y) * o_position.w;\n"
			"\tgl_Position.y *= d3d9_pixel_fix.z;\n"
			"\tgl_Position.z = 0.0;\n");
	}
	text_append(&out, "}\n");
	if (out.failed)
	{
		translation_fail(t, "the GLSL is too long, or out of memory");
		free(out.buffer);
		goto done;
	}
	result = out.buffer;
	*float_count = t->float_count;
	if (inputs)
		*inputs = t->pixel ? t->varyings_in : t->varyings_out;
done:
	free(t->main_text.buffer);
	free(t->label_text.buffer);
	free(t);
	return result;
}
