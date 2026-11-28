#ifndef MCC_SHA256_H
#define MCC_SHA256_H
/* MCC's name for the game's one SHA-256 (content_sha256.h), kept for a release
for code outside this repository that still uses it */
#include "content_sha256.h"
#define mcc_sha256 content_sha256
#define mcc_sha256_begin content_sha256_begin
/* (as before: a size of zero or less adds nothing) */
void mcc_sha256_add(struct content_sha256 *context, void const *data, int size);
#define mcc_sha256_end content_sha256_end
#define mcc_sha256_implementation content_sha256_implementation
#endif
