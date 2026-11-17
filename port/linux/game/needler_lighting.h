#ifndef NEEDLER_LIGHTING_H
#define NEEDLER_LIGHTING_H
#ifdef HALO_CONSOLE_NO_LIGHT_EFFECTS
/* (a console draws no Light effects: halo_port_features.h) */
struct shader;
struct pixel_shader_definition;
static __inline void needler_lighting_material(struct shader *shader, real_rgb_color *color) { (void)shader; (void)color; }
static __inline void needler_lighting_transparent(struct shader *shader, struct pixel_shader_definition *pixel, int stages) { (void)shader; (void)pixel; (void)stages; }
#else

int needler_lighting_position(long weapon, real_point3d *position, real_vector3d *direction);

struct shader;
void needler_lighting_material(struct shader *shader, real_rgb_color *color);
void needler_lighting_forget(void);
struct pixel_shader_definition;
void needler_lighting_transparent(struct shader *shader, struct pixel_shader_definition *pixel, int stages);

#endif
#endif
