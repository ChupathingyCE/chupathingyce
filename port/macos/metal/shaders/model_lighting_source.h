/* ChupathingyCE OpenGL's per-pixel model lighting equation, with explicit
   parameters for MSL (Metal has no global uniforms). RGB only: diffuse alpha
   still comes from the original vertex program, including planar fog. */
#include "xgpu.h"

static void model_lighting_source(struct xgpu_text *text, BOOL msl, BOOL point_lights)
{
	xgpu_text_append(text,
		"vec3 model_lighting(vec4 xWorldNormal, vec3 xWorldPosition, %s)\n"
		"{\n"
		/* Restore the interpolated direction at the vertex normal's length;
		   some first-person model normals deliberately have length < 1. */
		"\tvec3 n = xWorldNormal.xyz * (xWorldNormal.w * inversesqrt(max(dot(xWorldNormal.xyz, xWorldNormal.xyz), 1.0e-24)));\n"
		"\tfloat facing = dot(n, -model_lights[7].xyz);\n"
		"\tvec3 light = model_lights[11].xyz + max(max(facing, -facing * model_lights[0].z), 0.0) * model_lights[8].xyz +\n"
		"\t\tmax(dot(n, -model_lights[9].xyz), 0.0) * model_lights[10].xyz;\n",
		msl ? "constant vec4 *model_lights" : "vec4 model_lights[12]");
	for (int light = 0; point_lights && light < 2; light++)
	{
		int first = 1 + light * 3;
		xgpu_text_append(text,
			"\t{\n"
			"\t\tvec3 to_light = model_lights[%d].xyz - xWorldPosition;\n"
			"\t\tfloat distance_squared = dot(to_light, to_light);\n"
			"\t\tvec3 l = to_light * inversesqrt(max(distance_squared, 1.0e-12));\n"
			"\t\tlight += max(1.0 - distance_squared * model_lights[%d].w, 0.0) * clamp(dot(l, n), 0.0, 1.0) *\n"
			"\t\t\tclamp(dot(l, -model_lights[%d].xyz) * model_lights[%d].w + model_lights[%d].w, 0.0, 1.0) *\n"
			"\t\t\tmodel_lights[%d].xyz;\n"
			"\t}\n", first, first, first + 1, first + 1, first + 2, first + 2);
	}
	xgpu_text_append(text, "\treturn clamp(light, 0.0, 1.0);\n}\n");
}
