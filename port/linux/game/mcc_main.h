#ifndef MCC_MAIN_H
#define MCC_MAIN_H
boolean mcc_main_load_failed(char const *name);
boolean mcc_main_network_load_failed(char const *name);
boolean mcc_main_map_completed(void);
boolean mcc_main_campaign_won(void);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_main_campaign_won() ((boolean)0)
#define mcc_main_load_failed(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_main_network_load_failed(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_main_map_completed() ((boolean)0)
#endif

#endif
