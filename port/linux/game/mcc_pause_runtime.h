#ifndef MCC_PAUSE_RUNTIME_H
#define MCC_PAUSE_RUNTIME_H

boolean mcc_pause_runtime_load(void);
void mcc_pause_runtime_unload(void);
boolean mcc_pause_runtime_active(void);
long mcc_pause_runtime_screen(short local_count, boolean first_player);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_pause_runtime_active() ((boolean)0)
#define mcc_pause_runtime_load() ((boolean)0)
#define mcc_pause_runtime_screen(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (long)NONE)
#define mcc_pause_runtime_unload() ((void)0)
#endif

#endif
