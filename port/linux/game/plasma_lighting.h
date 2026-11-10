#ifndef PLASMA_LIGHTING_H
#define PLASMA_LIGHTING_H

void plasma_lighting_prepare(long light_index, long object_index, long definition_index);
float plasma_lighting_radius(long light_index, long object_index, float radius);
int light_port_attachment_radius(long light_index, long object_index, float *radius, int restore);
int plasma_lighting_vehicle_occlusion(long light_index);
void plasma_lighting_frame_end(void);
void plasma_lighting_reset(void);
void plasma_lighting_window_begin(void);
void plasma_lighting_submitted(long light_index, long rasterizer_index);
int plasma_lighting_selected(long rasterizer_index);
int plasma_lighting_prong_cone(long rasterizer, float axis[3], float origin[3], float *radius);
void plasma_falloff_begin(long rasterizer_index);
void plasma_falloff_specular_begin(long rasterizer_index);
void plasma_specular_begin(long rasterizer_index);
unsigned long plasma_specular_color(unsigned long material);
void plasma_falloff_end(void);
void plasma_falloff_dispose(void);

/* Chupathingy's way in: object_lights.c and main.c */
void plasma_lighting_update(void);
void plasma_lighting_forget(void);
long plasma_lighting_unit_light(long rasterizer_index, union real_point3d const *center);
void covenant_lighting_color(long object_index, long definition_index, union real_rgb_color *color);

/* object_lights.c: validate the owner before refreshing an attachment. */
int light_port_refresh_attachment(long light_index, long object_index);
int light_port_dynamic_set(long light_index, long object_index, int enabled);

#endif
