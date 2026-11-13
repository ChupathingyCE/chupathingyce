/* MCC recording conversion; the established playback codecs stay unchanged. */
#ifndef MCC_RECORDINGS_H
#define MCC_RECORDINGS_H
#include <stdint.h>

/* Validate the complete stream before changing any bytes. Invalid weapon
 * selections become NONE (keep the unit's current weapon). */
int mcc_recording_prepare(unsigned char *stream, uint32_t size,
    unsigned version, unsigned control_version, uint32_t *corrected);

/* Valid version-4 weapon events with no movement or authored directions. */
int mcc_recording_weapon_only(unsigned char *stream, uint32_t size,
    unsigned version, unsigned control_version);

struct recorded_animation_definition;
struct unit_control_data;
void mcc_recording_control_begin(long unit_index,
    struct recorded_animation_definition const *animation, struct unit_control_data *control);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_recording_control_begin(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)0)
#endif

#endif
