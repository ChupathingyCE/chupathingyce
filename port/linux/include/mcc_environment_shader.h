#ifndef MCC_ENVIRONMENT_SHADER_H
#define MCC_ENVIRONMENT_SHADER_H

/* Explicit draw scope, separate from the emulated Xbox render states. */
void mcc_environment_shader_begin(unsigned short flags);
void mcc_environment_shader_end(void);
void mcc_environment_shader_set(int enabled);
int mcc_environment_shader_active(void);
struct xgpu_text;
void mcc_environment_shader_emit(struct xgpu_text *text);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_environment_shader_begin(a0) ((void)sizeof(a0), (void)0)
#define mcc_environment_shader_end() ((void)0)
#define mcc_environment_shader_set(a0) ((void)sizeof(a0), (void)0)
#define mcc_environment_shader_active() ((int)0)
#define mcc_environment_shader_emit(a0) ((void)sizeof(a0), (void)0)
#endif

#endif
