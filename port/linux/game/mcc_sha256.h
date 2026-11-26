#ifndef MCC_SHA256_H
#define MCC_SHA256_H
/* MCC's name for the game's one SHA-256 (content_sha256.h), kept for a release
for code outside this repository that still uses it */
#include "content_sha256.h"
#define mcc_sha256 content_sha256
#define mcc_sha256_begin content_sha256_begin
#define mcc_sha256_add(context, data, size) content_sha256_add((context), (data), (size_t)(size))
#define mcc_sha256_end content_sha256_end
#define mcc_sha256_implementation content_sha256_implementation
#endif
