/*
CONSOLE_MBEDTLS_CONFIG.H

Mbed TLS (port/third_party/mbedtls, 3.6 LTS) as the consoles build it for
console_https.c: a TLS 1.2 and 1.3 client and nothing else. Built with
-DMBEDTLS_CONFIG_FILE='"console_mbedtls_config.h"' and port/console/https
on the include path, in place of the library's default configuration (which
the desktop builds keep).

- Key exchange: ECDHE alone (X25519, P-256, P-384), no RSA key transport,
  no PSK, no DHE. Ciphers: AES-GCM and ChaCha20-Poly1305 alone.
- Signatures: ECDSA, and RSA (PKCS#1 v1.5 for certificates and TLS 1.2,
  PSS for TLS 1.3), verified only: the console never signs.
- No server, no DTLS, no renegotiation, no session tickets or resumption,
  no files, no sockets (console_https.c gives the library the console's own
  through its BIO), no threads (console_https.c lets one request in at a
  time).
- No clock: MBEDTLS_HAVE_TIME and MBEDTLS_HAVE_TIME_DATE are left out, so
  certificates' dates are never checked (a console's clock is often unset);
  console_https.c pins keys and roots instead.
- Entropy from the console's hardware (MBEDTLS_ENTROPY_HARDWARE_ALT:
  mbedtls_hardware_poll in console_https.c asks the port).
- Memory: every allocation comes from an arena on the requesting thread's
  stack (MBEDTLS_MEMORY_BUFFER_ALLOC_C, console_https.c), none from the
  game's heap. A received record may be 16 KB (servers seldom honor a
  smaller maximum fragment length, and TLS 1.3 here has no way to ask), a
  sent one is at most 4 KB.
*/

#ifndef CONSOLE_MBEDTLS_CONFIG_H
#define CONSOLE_MBEDTLS_CONFIG_H

/* ---------- the platform */

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C
/* (the host test measures the arena: tools/test_console_https.py) */
#ifdef CONSOLE_HTTPS_TESTS
#define MBEDTLS_MEMORY_DEBUG
#endif
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
/* (plain C everywhere: the consoles are x86, PowerPC and Arm, and some of
their compilers' inline assembly is not GCC's) */
#define MBEDTLS_NO_UDBL_DIVISION
#define MBEDTLS_DEPRECATED_REMOVED
/* (no text descriptions of certificates: console_https.c logs the flags) */
#define MBEDTLS_X509_REMOVE_INFO
#define MBEDTLS_ERROR_STRERROR_DUMMY

/* ---------- the crypto */

#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_SHA512_SMALLER
#define MBEDTLS_HKDF_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

#define MBEDTLS_BIGNUM_C
/* (ISRG Root X1 is a 4096-bit RSA key) */
#define MBEDTLS_MPI_MAX_SIZE 512
#define MBEDTLS_MPI_WINDOW_SIZE 2
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_WINDOW_SIZE 2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
/* (TLS 1.3 servers with RSA keys sign with PSS) */
#define MBEDTLS_X509_RSASSA_PSS_SUPPORT
#define MBEDTLS_X509_MAX_INTERMEDIATE_CA 4

/* TLS 1.3 needs PSA's crypto (HKDF, its key exchange); its key store holds
a handful of keys at once, all of them gone when a request ends */
#define MBEDTLS_PSA_CRYPTO_C
#define MBEDTLS_PSA_KEY_SLOT_COUNT 8

/* ---------- TLS */

#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_PROTO_TLS1_3
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED
#define MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_ALL_ALERT_MESSAGES

#define MBEDTLS_SSL_IN_CONTENT_LEN 16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096
#define MBEDTLS_SSL_CIPHERSUITES \
	MBEDTLS_TLS1_3_AES_128_GCM_SHA256, \
	MBEDTLS_TLS1_3_AES_256_GCM_SHA384, \
	MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256, \
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, \
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, \
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, \
	MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, \
	MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384, \
	MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256

#endif
