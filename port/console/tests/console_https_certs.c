/*
CONSOLE_HTTPS_CERTS.C

The host test's keys and certificates (tools/test_console_https.py), made
with Mbed TLS's default build, so that the test needs no OpenSSL tools and
can give certificates any dates:

  console_https_certs key FILE ec|rsa
  console_https_certs cert FILE SUBJECT_KEY ISSUER_KEY ISSUER_NAME NAME HOST CA FROM TO
  console_https_certs pin KEY        (the SHA-256 of its SubjectPublicKeyInfo, in hex)
  console_https_certs der PEM FILE   (a certificate as DER)

HOST "-" gives no subjectAltName; CA is 1 for a certificate authority; FROM
and TO are YYYYMMDDhhmmss.
*/

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;

static void fail(const char *what, int code)
{
	fprintf(stderr, "console_https_certs: %s (-0x%04x)\n", what, (unsigned)-code);
	exit(1);
}

static void write_file(const char *path, const unsigned char *data, size_t size)
{
	FILE *file = fopen(path, "wb");

	if (!file || fwrite(data, 1, size, file) != size)
		fail("could not write", 0);
	fclose(file);
}

static void load_key(mbedtls_pk_context *key, const char *path)
{
	int code;

	mbedtls_pk_init(key);
	if ((code = mbedtls_pk_parse_keyfile(key, path, NULL, mbedtls_ctr_drbg_random, &drbg)) != 0)
		fail("could not read a key", code);
}

static int make_key(const char *path, const char *kind)
{
	mbedtls_pk_context key;
	unsigned char pem[16000];
	int code;

	mbedtls_pk_init(&key);
	if (!strcmp(kind, "rsa"))
	{
		if ((code = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA))) != 0 ||
			(code = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &drbg, 2048, 65537)) != 0)
			fail("could not make an RSA key", code);
	}
	else
	{
		if ((code = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) != 0 ||
			(code = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random,
				&drbg)) != 0)
			fail("could not make an EC key", code);
	}
	if ((code = mbedtls_pk_write_key_pem(&key, pem, sizeof(pem))) != 0)
		fail("could not write a key", code);
	write_file(path, pem, strlen((char *)pem));
	mbedtls_pk_free(&key);
	return 0;
}

static int make_cert(char **argv)
{
	mbedtls_pk_context subject_key, issuer_key;
	mbedtls_x509write_cert writer;
	unsigned char pem[16000];
	unsigned char serial[8];
	int ca = atoi(argv[8]);
	int code;

	load_key(&subject_key, argv[3]);
	load_key(&issuer_key, argv[4]);
	mbedtls_x509write_crt_init(&writer);
	mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
	mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
	mbedtls_x509write_crt_set_subject_key(&writer, &subject_key);
	mbedtls_x509write_crt_set_issuer_key(&writer, &issuer_key);
	mbedtls_ctr_drbg_random(&drbg, serial, sizeof(serial));
	serial[0] &= 0x7f;
	serial[0] |= 0x01;
	if ((code = mbedtls_x509write_crt_set_serial_raw(&writer, serial, sizeof(serial))) != 0 ||
		(code = mbedtls_x509write_crt_set_issuer_name(&writer, argv[5])) != 0 ||
		(code = mbedtls_x509write_crt_set_subject_name(&writer, argv[6])) != 0 ||
		(code = mbedtls_x509write_crt_set_validity(&writer, argv[9], argv[10])) != 0 ||
		(code = mbedtls_x509write_crt_set_basic_constraints(&writer, ca, ca ? 1 : -1)) != 0 ||
		(code = mbedtls_x509write_crt_set_subject_key_identifier(&writer)) != 0 ||
		(code = mbedtls_x509write_crt_set_authority_key_identifier(&writer)) != 0)
		fail("could not describe the certificate", code);
	if (ca)
		code = mbedtls_x509write_crt_set_key_usage(&writer, MBEDTLS_X509_KU_KEY_CERT_SIGN | MBEDTLS_X509_KU_CRL_SIGN);
	else
		code = mbedtls_x509write_crt_set_key_usage(&writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE |
			MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
	if (code != 0)
		fail("could not set the key usage", code);
	if (strcmp(argv[7], "-"))
	{
		mbedtls_x509_san_list name;

		memset(&name, 0, sizeof(name));
		name.node.type = MBEDTLS_X509_SAN_DNS_NAME;
		name.node.san.unstructured_name.p = (unsigned char *)argv[7];
		name.node.san.unstructured_name.len = strlen(argv[7]);
		if ((code = mbedtls_x509write_crt_set_subject_alternative_name(&writer, &name)) != 0)
			fail("could not set the name", code);
	}
	if ((code = mbedtls_x509write_crt_pem(&writer, pem, sizeof(pem), mbedtls_ctr_drbg_random, &drbg)) != 0)
		fail("could not write the certificate", code);
	write_file(argv[2], pem, strlen((char *)pem));
	mbedtls_x509write_crt_free(&writer);
	mbedtls_pk_free(&subject_key);
	mbedtls_pk_free(&issuer_key);
	return 0;
}

static int print_pin(const char *path)
{
	mbedtls_pk_context key;
	unsigned char der[2048], hash[32];
	int length, index;

	load_key(&key, path);
	length = mbedtls_pk_write_pubkey_der(&key, der, sizeof(der));
	if (length <= 0)
		fail("could not write the public key", length);
	mbedtls_sha256(der + sizeof(der) - length, (size_t)length, hash, 0);
	for (index = 0; index < 32; index++)
		printf("%02x", hash[index]);
	printf("\n");
	mbedtls_pk_free(&key);
	return 0;
}

static int write_der(const char *pem, const char *path)
{
	mbedtls_x509_crt certificate;
	int code;

	mbedtls_x509_crt_init(&certificate);
	if ((code = mbedtls_x509_crt_parse_file(&certificate, pem)) != 0)
		fail("could not read the certificate", code);
	write_file(path, certificate.raw.p, certificate.raw.len);
	mbedtls_x509_crt_free(&certificate);
	return 0;
}

int main(int argc, char **argv)
{
	int code;

	psa_crypto_init();
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&drbg);
	if ((code = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, NULL, 0)) != 0)
		fail("no random numbers", code);
	if (argc == 4 && !strcmp(argv[1], "key"))
		return make_key(argv[2], argv[3]);
	if (argc == 11 && !strcmp(argv[1], "cert"))
		return make_cert(argv);
	if (argc == 3 && !strcmp(argv[1], "pin"))
		return print_pin(argv[2]);
	if (argc == 4 && !strcmp(argv[1], "der"))
		return write_der(argv[2], argv[3]);
	fprintf(stderr, "usage: see console_https_certs.c\n");
	return 2;
}
