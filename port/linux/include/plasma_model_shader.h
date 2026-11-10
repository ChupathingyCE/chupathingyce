/* Render-only plasma selection and shader additions. No tag data is changed. */
#ifndef PLASMA_MODEL_SHADER_H
#define PLASMA_MODEL_SHADER_H
struct xgpu_text;
struct nv2a_vertex_lighting;
#ifdef HALO_CONSOLE_NO_LIGHT_EFFECTS
/* (a console draws no Light effects: halo_port_features.h) */
static __inline void plasma_model_lighting_reset(void) {}
static __inline void plasma_model_lighting_slot(long light_index, short slot) { (void)light_index; (void)slot; }
static __inline unsigned int plasma_model_lighting_mask(void) { return 0; }
static __inline void plasma_model_emit_uniform(struct xgpu_text *text) { (void)text; }
static __inline void plasma_model_emit_vertex(struct xgpu_text *text, const unsigned long *instruction,
	unsigned long index, const struct nv2a_vertex_lighting *lighting) { (void)text; (void)instruction; (void)index; (void)lighting; }
static __inline void plasma_model_emit_pixel(struct xgpu_text *text) { (void)text; }
#else
void plasma_model_lighting_reset(void);
void plasma_model_lighting_slot(long light_index, short slot);
unsigned int plasma_model_lighting_mask(void);
void plasma_model_emit_uniform(struct xgpu_text *text);
void plasma_model_emit_vertex(struct xgpu_text *text, const unsigned long *instruction,
	unsigned long index, const struct nv2a_vertex_lighting *lighting);
void plasma_model_emit_pixel(struct xgpu_text *text);
#endif
#endif
