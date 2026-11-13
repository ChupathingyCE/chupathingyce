"""Production layouts shared by the CPU-only transparent rendering tests."""
import re

from harness import enum_with, read, structure


def transparent_geometry_group(source):
    """The 32-bit layout of struct transparent_geometry_group. Its name is the #else of a 64-bit one's (the 64-bit
    builds' is rasterizer_transparent_geometry.h's); the body's own #ifdef HALO_64BIT is left to the compiler."""
    match = re.search(r"^(?:#else\s*\n)?struct\s+transparent_geometry_group\s*\n(?:#endif\s*\n)?\{", source, re.M)
    if not match:
        raise LookupError("structure not found in the sources: transparent_geometry_group")
    start = source.index("{", match.start())
    end = start + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return "struct transparent_geometry_group\n" + source[start:source.index(";", end) + 1]


def transparent_types():
    declarations = []
    for path, names in [
        ("source/tag_files/tag_groups.h", ["tag_block", "tag_reference"]),
        ("source/shaders/shader_definitions.h", ["shader_radiosity_properties", "shader_physics_properties", "shader_base", "shader"]),
        ("source/rasterizer/rasterizer_geometry.h", ["vertex_buffer", "triangle_buffer"]),
        ("source/rasterizer/xbox/rasterizer_xbox_transparent_geometry.c", ["shader_transparent_generic", "shader_transparent_generic_definition", "shader_transparent_glass_definition"]),
    ]:
        text = read(path)
        declarations += [structure(text, name) for name in names]
    declarations.append(transparent_geometry_group(read("source/rasterizer/xbox/rasterizer_xbox_transparent_geometry.c")))
    text = read("source/rasterizer/xbox/rasterizer_xbox_transparent_geometry.c")
    for member in ["_shader_type_screen", "_shader_transparent_flag_alpha_tested_bit", "_shader_transparent_glass_flag_alpha_tested_bit", "_shader_transparent_glass_reflection_type_bumped_cube_map", "_shader_radiosity_FILTHY_transparent_lit_bit", "_framebuffer_fade_mode_none", "_framebuffer_blend_function_alpha_blend", "_rasterizer_geometry_no_sort_bit"]:
        declarations.append(enum_with(text, member))
    text = read("source/rasterizer/rasterizer_geometry.h")
    declarations += [enum_with(text, "_rasterizer_vertex_type_environment_uncompressed"), enum_with(text, "_triangle_buffer_type_triangles")]
    return "\n".join(declarations)
