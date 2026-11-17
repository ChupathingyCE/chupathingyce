#ifndef AMMO_COUNTER_LIGHTING_H
#define AMMO_COUNTER_LIGHTING_H
#ifdef HALO_CONSOLE_NO_LIGHT_EFFECTS
/* (a console draws no Light effects: halo_port_features.h) */
struct shader;
static __inline void ammo_counter_lighting_screen(struct shader *shader, real_rgb_color *color) { (void)shader; (void)color; }
static __inline void ammo_counter_lighting_draw_begin(struct shader *shader, unsigned long output) { (void)shader; (void)output; }
static __inline void ammo_counter_lighting_draw_end(void) {}
#else

/* Pose result: 0 unavailable, 1 world model, 2 first-person model. */
int ammo_counter_lighting_position(long weapon, real_point3d *position, real_vector3d *direction);

struct shader;
void ammo_counter_lighting_screen(struct shader *shader, real_rgb_color *color);
/* Balanced around one model material draw, before any following fog pass. */
void ammo_counter_lighting_draw_begin(struct shader *shader, unsigned long output);
void ammo_counter_lighting_draw_end(void);
void ammo_counter_lighting_forget(void);

#endif
#endif
