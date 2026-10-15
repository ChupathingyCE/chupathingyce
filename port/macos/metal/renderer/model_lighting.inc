/* Model-program recognition from ChupathingyCE's OpenGL nv2a_vsh.c.
   Included after the NV2A operand decoder; the lighting equation must only
   replace programs with these register/constant and single-writer contracts. */
struct operand_fields
{
	unsigned long negate, swizzle[4], index, mux;
};

static void operand_fields(const DWORD *instruction, char which, struct operand_fields *operand)
{
	if (which == 'A')
	{
		operand->negate = field(instruction, 1, 8, 1);
		for (int axis = 0; axis < 4; axis++)
			operand->swizzle[axis] = field(instruction, 1, 6 - axis * 2, 2);
		operand->index = field(instruction, 2, 28, 4);
		operand->mux = field(instruction, 2, 26, 2);
	}
	else if (which == 'B')
	{
		operand->negate = field(instruction, 2, 25, 1);
		for (int axis = 0; axis < 4; axis++)
			operand->swizzle[axis] = field(instruction, 2, 23 - axis * 2, 2);
		operand->index = field(instruction, 2, 13, 4);
		operand->mux = field(instruction, 2, 11, 2);
	}
	else
	{
		operand->negate = field(instruction, 2, 10, 1);
		for (int axis = 0; axis < 4; axis++)
			operand->swizzle[axis] = field(instruction, 2, 8 - axis * 2, 2);
		operand->index = (field(instruction, 2, 0, 2) << 2) | field(instruction, 3, 30, 2);
		operand->mux = field(instruction, 3, 28, 2);
	}
}

static BOOL xyz_unswizzled(const struct operand_fields *operand)
{
	return operand->swizzle[0] == 0 && operand->swizzle[1] == 1 && operand->swizzle[2] == 2;
}

static BOOL light_constant(const DWORD *instruction, const struct operand_fields *operand, int reg, BOOL negated)
{
	return operand->mux == _mux_constant && !field(instruction, 3, 1, 1) &&
		field(instruction, 1, 13, 8) == (unsigned long)(XGPU_VERTEX_CONSTANT_BIAS + reg) &&
		operand->negate == (negated ? 1UL : 0UL) && xyz_unswizzled(operand);
}

static BOOL light_temporary(const struct operand_fields *operand, BOOL negated)
{
	return operand->mux == _mux_temporary && operand->index < 12 &&
		operand->negate == (negated ? 1UL : 0UL) && xyz_unswizzled(operand);
}

static BOOL temporary_written(const DWORD *instructions, unsigned long first, unsigned long last, unsigned long reg)
{
	unsigned long index;

	for (index = first; index < last; index++)
	{
		const DWORD *instruction = instructions + index * 4;
		unsigned long mac = field(instruction, 1, 21, 4);
		unsigned long ilu = field(instruction, 1, 25, 3);
		unsigned long temporary = field(instruction, 3, 20, 4);

		if (mac != _mac_nop && mac != _mac_arl && temporary == reg && (field(instruction, 3, 24, 4) & 0xe))
			return TRUE;
		/* The ILU writes r1 when both units run. */
		if (ilu != _ilu_nop && (mac != _mac_nop ? 1 : temporary) == reg && (field(instruction, 3, 16, 4) & 0xe))
			return TRUE;
	}
	return FALSE;
}

BOOL nv2a_vertex_shader_lighting(const DWORD *instructions, unsigned long instruction_count,
	struct nv2a_vertex_lighting *lighting)
{
	unsigned long count, index, diffuse;
	unsigned long distant[2], distant_register[2] = { 0, 0 }, point[2], point_register[2] = { 0, 0 };
	BOOL cone[2] = { FALSE, FALSE }, translucency = FALSE, point_lights = FALSE;
	int light;

	memset(lighting, 0, sizeof(*lighting));
	/* NV2A programs have at most 136 instructions (the game's loader's bound). */
	if (!instructions || !instruction_count || instruction_count > 136)
		return FALSE;
	for (count = 0; count < instruction_count;)
	{
		if (field(instructions + count++ * 4, 3, 0, 1))
			break;
	}
	diffuse = distant[0] = distant[1] = point[0] = point[1] = count;
	for (index = 0; index < count; index++)
	{
		const DWORD *instruction = instructions + index * 4;
		unsigned long mac = field(instruction, 1, 21, 4);
		unsigned long output_mask = field(instruction, 3, 12, 4);
		unsigned long constant = field(instruction, 1, 13, 8);
		struct operand_fields a, b, c;

		operand_fields(instruction, 'A', &a);
		operand_fields(instruction, 'B', &b);
		operand_fields(instruction, 'C', &c);
		/* Diffuse xyz is written once, last, by the second distant term
		   times its color plus the accumulated other terms. Alpha is separate. */
		if ((output_mask & 0xe) && field(instruction, 3, 11, 1) && field(instruction, 3, 3, 8) == 3)
		{
			if (diffuse != count || (output_mask & 0xe) != 0xe || field(instruction, 3, 2, 1) ||
				mac != _mac_mad || !light_constant(instruction, &b, -70, FALSE) || c.mux != _mux_temporary)
				return FALSE;
			diffuse = index;
		}
		for (light = 0; light < 2; light++)
		{
			if (mac == _mac_dp3 && light_constant(instruction, &b, light ? -71 : -73, TRUE))
			{
				if (distant[light] != count || !light_temporary(&a, FALSE))
					return FALSE;
				distant[light] = index;
				distant_register[light] = a.index;
			}
			if (mac == _mac_add && light_constant(instruction, &a, light ? -76 : -79, FALSE) &&
				light_temporary(&c, TRUE))
			{
				if (point[light] != count)
					return FALSE;
				point[light] = index;
				point_register[light] = c.index;
			}
			if (mac == _mac_dp3 && b.mux == _mux_constant && !field(instruction, 3, 1, 1) &&
				constant == (unsigned long)(XGPU_VERTEX_CONSTANT_BIAS + (light ? -75 : -78)))
			{
				if (!light_constant(instruction, &b, light ? -75 : -78, TRUE))
					return FALSE;
				cone[light] = TRUE;
			}
		}
		if (mac == _mac_mul && a.mux == _mux_temporary && a.negate && b.mux == _mux_constant &&
			!field(instruction, 3, 1, 1) && constant == XGPU_VERTEX_CONSTANT_BIAS - 82 && b.swizzle[0] == 2)
			translucency = TRUE;
		if ((a.mux == _mux_constant || b.mux == _mux_constant || c.mux == _mux_constant) &&
			!field(instruction, 3, 1, 1) && constant >= XGPU_VERTEX_CONSTANT_BIAS - 79 &&
			constant <= XGPU_VERTEX_CONSTANT_BIAS - 74)
			point_lights = TRUE;
	}
	if (diffuse == count || distant[0] >= diffuse || distant[1] >= diffuse ||
		distant_register[0] != distant_register[1] || !translucency)
		return FALSE;
	if (temporary_written(instructions, distant[0] < distant[1] ? distant[0] : distant[1], diffuse, distant_register[0]))
		return FALSE;
	if (point_lights)
	{
		unsigned long last = point[0] > point[1] ? point[0] : point[1];
		unsigned long first = point[0] < point[1] ? point[0] : point[1];
		if (point[0] >= diffuse || point[1] >= diffuse || point_register[0] != point_register[1] ||
			!cone[0] || !cone[1] || temporary_written(instructions, first, last, point_register[0]))
			return FALSE;
		lighting->position_register = point_register[0];
		lighting->position_instruction = first;
	}
	lighting->lights = point_lights ? 2 : 1;
	lighting->normal_register = distant_register[0];
	lighting->normal_instruction = distant[0] < distant[1] ? distant[0] : distant[1];
	return TRUE;
}
