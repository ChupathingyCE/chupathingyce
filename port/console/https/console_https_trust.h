/*
CONSOLE_HTTPS_TRUST.H

The hosts console_https.c asks and how each one's certificate is checked
(console_https_trust.c; docs/console-https.md, "Rotating keys"): by pinned
public keys (the SHA-256 of the certificate's SubjectPublicKeyInfo, as
`openssl pkey -pubin -outform der | openssl dgst -sha256` gives it), or by a
chain to pinned roots (console_https_roots.c, made by
tools/console_https_roots.py).
*/

#ifndef __CONSOLE_HTTPS_TRUST_H
#define __CONSOLE_HTTPS_TRUST_H

#define CONSOLE_HTTPS_PIN_SIZE 32

struct console_https_root
{
	const char *name;
	const unsigned char *der;
	unsigned long size;
};

struct console_https_trust
{
	const char *host;
	/* the keys taken (none: the chain is checked against roots instead) */
	const unsigned char (*pins)[CONSOLE_HTTPS_PIN_SIZE];
	int pin_count;
	const struct console_https_root *roots;
	int root_count;
};

extern const struct console_https_trust console_https_hosts[];
extern const int console_https_host_count;

/* (console_https_roots.c) */
extern const struct console_https_root console_https_roots[];
extern const int console_https_root_count;

#endif
