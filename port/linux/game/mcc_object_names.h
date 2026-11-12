/* Expanded MCC names keep the legacy table and game-state layout intact. */
#ifndef MCC_OBJECT_NAMES_H
#define MCC_OBJECT_NAMES_H
#define MCC_OBJECT_NAMES_MAXIMUM 32767
#define MCC_OBJECT_NAMES_SNAPSHOT_MAXIMUM (4u * MCC_OBJECT_NAMES_MAXIMUM)
int mcc_object_names_active(void);
long mcc_object_names_count(void);
void mcc_object_names_clear(void);
long mcc_object_names_get(short name);
void mcc_object_names_set(short name, long object);
void mcc_object_names_claim(short name, long object);
void mcc_object_names_delete(long object);
unsigned long mcc_object_names_snapshot(void *out);
int mcc_object_names_validate(void const *in, unsigned long bytes);
int mcc_object_names_restore(void const *in, unsigned long bytes);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_object_names_active() ((int)0)
#define mcc_object_names_claim(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (void)0)
#define mcc_object_names_clear() ((void)0)
#define mcc_object_names_delete(a0) ((void)sizeof(a0), (void)0)
#define mcc_object_names_get(a0) ((void)sizeof(a0), (long)NONE)
#define mcc_object_names_set(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (void)0)
#endif

#endif
