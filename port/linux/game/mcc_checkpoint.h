/* MCC-only data carried in the checked, otherwise unused CPU arena tail. */
#ifndef MCC_CHECKPOINT_H
#define MCC_CHECKPOINT_H
int mcc_checkpoint_capture(void *image, unsigned long total_bytes,
    unsigned long cpu_used, unsigned long cpu_capacity);
int mcc_checkpoint_validate(void const *image, unsigned long total_bytes,
    unsigned long cpu_used, unsigned long cpu_capacity, void const *current_image);
int mcc_checkpoint_restore(void const *image, unsigned long total_bytes,
    unsigned long cpu_used, unsigned long cpu_capacity);
void mcc_checkpoint_dispose(void);
void *mcc_checkpoint_script_storage(void *image, unsigned long used,
    unsigned long capacity, unsigned long bytes);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_checkpoint_capture(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (int)1)
#define mcc_checkpoint_restore(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (int)1)
#define mcc_checkpoint_script_storage(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void *)0)
#define mcc_checkpoint_validate(a0, a1, a2, a3, a4) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (int)1)
#endif

#endif
