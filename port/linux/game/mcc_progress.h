/* Per-profile MCC completion history; never uses Xbox campaign indices. */
#ifndef MCC_PROGRESS_H
#define MCC_PROGRESS_H
unsigned mcc_progress_flags(long profile, char const *level);
void mcc_progress_complete(char const *level, short difficulty);
boolean mcc_progress_copy(char const *profile_file, char const *new_directory);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_progress_copy(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (boolean)1)
#endif

#endif
