/* MCC lobby checks are advanced only while the server is in pregame. */
#ifndef MCC_VALIDATION_H
#define MCC_VALIDATION_H
#include <stdint.h>

/* One pending package, four bounded successful results; no session identity. */
/* Request returns 1 when ready, 0 while pending, -1 after failure. Lookup
 * never schedules work. Cancel releases readers but retains completed facts. */
int mcc_validation_request(char const *name, uint32_t *checksum, unsigned char digest[32]);
int mcc_validation_lookup(char const *name, uint32_t *checksum, unsigned char digest[32]);
void mcc_validation_update(void);
void mcc_validation_cancel(void);
char const *mcc_validation_error(void);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_validation_cancel() ((void)0)
#define mcc_validation_error() ((char const *)0)
#define mcc_validation_lookup(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (int)0)
#define mcc_validation_request(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (int)0)
#define mcc_validation_update() ((void)0)
#endif

#endif
