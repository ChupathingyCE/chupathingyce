/*
SC_SECRET.C

Where the Steam refresh token is kept (sc_internal.h): the system's own
secret store where there is one (macOS: the login Keychain; Windows: the
token sealed with DPAPI to the player's Windows account; Linux and BSD:
libsecret, the desktop's keyring, loaded at run time when installed),
else only the sign-in file (readable by the player alone). The token is
never logged.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h): a console builds this to nothing */

#include "sc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERVICE "Chupathingy Steam link"

#if defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

static CFMutableDictionaryRef query(const char *account)
{
	CFMutableDictionaryRef dictionary = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
		&kCFTypeDictionaryValueCallBacks);
	CFStringRef service = CFStringCreateWithCString(NULL, SERVICE, kCFStringEncodingUTF8);
	CFStringRef name = CFStringCreateWithCString(NULL, account, kCFStringEncodingUTF8);

	CFDictionarySetValue(dictionary, kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(dictionary, kSecAttrService, service);
	CFDictionarySetValue(dictionary, kSecAttrAccount, name);
	CFRelease(service);
	CFRelease(name);
	return dictionary;
}

const char *sc_secret_store_name(void)
{
	return "the macOS Keychain";
}

int sc_secret_put(const char *account, const char *token)
{
	CFMutableDictionaryRef item = query(account);
	CFDataRef data = CFDataCreate(NULL, (const UInt8 *)token, (CFIndex)strlen(token));
	OSStatus status;

	SecItemDelete(item);
	CFDictionarySetValue(item, kSecValueData, data);
	CFDictionarySetValue(item, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
	status = SecItemAdd(item, NULL);
	CFRelease(data);
	CFRelease(item);
	if (status != errSecSuccess)
		sc_log("Steam: the Keychain refused the sign-in (%d)", (int)status);
	return status == errSecSuccess;
}

int sc_secret_get(const char *account, char *token, size_t size)
{
	CFMutableDictionaryRef item = query(account);
	CFTypeRef result = NULL;
	OSStatus status;
	int ok = 0;

	CFDictionarySetValue(item, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(item, kSecMatchLimit, kSecMatchLimitOne);
	status = SecItemCopyMatching(item, &result);
	CFRelease(item);
	if (status == errSecSuccess && result && CFGetTypeID(result) == CFDataGetTypeID())
	{
		CFIndex length = CFDataGetLength((CFDataRef)result);

		if (length > 0 && (size_t)length < size)
		{
			memcpy(token, CFDataGetBytePtr((CFDataRef)result), (size_t)length);
			token[length] = 0;
			ok = 1;
		}
	}
	if (result)
		CFRelease(result);
	return ok;
}

void sc_secret_delete(const char *account)
{
	CFMutableDictionaryRef item = query(account);

	SecItemDelete(item);
	CFRelease(item);
}

#elif defined(_WIN32)

#include <windows.h>
#include <wincrypt.h>

/* (the sealed token, as hex, in a file beside the sign-in file: only this
Windows account on this computer can open it) */
static int sealed_path(char *path, size_t size)
{
	return sc_host.data_folder[0] && snprintf(path, size, "%s/steam_sign_in.sealed", sc_host.data_folder) < (int)size;
}

const char *sc_secret_store_name(void)
{
	return "a file sealed to your Windows account";
}

int sc_secret_put(const char *account, const char *token)
{
	DATA_BLOB in, out;
	char path[STEAM_CONTENT_PATH_SIZE + 32];
	FILE *file;
	DWORD index;
	int ok;

	(void)account;
	in.pbData = (BYTE *)token;
	in.cbData = (DWORD)strlen(token);
	if (!sealed_path(path, sizeof(path)) || !CryptProtectData(&in, L"Chupathingy Steam link", NULL, NULL, NULL,
		CRYPTPROTECT_UI_FORBIDDEN, &out))
	{
		return 0;
	}
	file = fopen(path, "wb");
	ok = file != NULL;
	for (index = 0; ok && index < out.cbData; index++)
		ok = fprintf(file, "%02x", out.pbData[index]) == 2;
	if (file)
		ok &= fclose(file) == 0;
	LocalFree(out.pbData);
	return ok;
}

int sc_secret_get(const char *account, char *token, size_t size)
{
	char path[STEAM_CONTENT_PATH_SIZE + 32];
	unsigned char sealed[8192];
	DATA_BLOB in, out;
	DWORD count = 0;
	FILE *file;
	unsigned value;
	int ok = 0;

	(void)account;
	if (!sealed_path(path, sizeof(path)) || !(file = fopen(path, "rb")))
		return 0;
	while (count < sizeof(sealed) && fscanf(file, "%2x", &value) == 1)
		sealed[count++] = (unsigned char)value;
	fclose(file);
	in.pbData = sealed;
	in.cbData = count;
	if (count && CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out))
	{
		if (out.cbData < size)
		{
			memcpy(token, out.pbData, out.cbData);
			token[out.cbData] = 0;
			ok = 1;
		}
		SecureZeroMemory(out.pbData, out.cbData);
		LocalFree(out.pbData);
	}
	return ok;
}

void sc_secret_delete(const char *account)
{
	char path[STEAM_CONTENT_PATH_SIZE + 32];

	(void)account;
	if (sealed_path(path, sizeof(path)))
		remove(path);
}

#elif !defined(__ANDROID__)

#include <dlfcn.h>

/* libsecret's public ABI (libsecret-1.so.0), declared here so that nothing
is needed to build: the store is used only where the desktop has it */
struct secret_schema_attribute
{
	const char *name;
	int type;
};

struct secret_schema
{
	const char *name;
	int flags;
	struct secret_schema_attribute attributes[32];
	int reserved;
	void *reserved1, *reserved2, *reserved3, *reserved4, *reserved5, *reserved6, *reserved7;
};

static const struct secret_schema schema = { "org.chupathingy.SteamLink", 0, { { "account", 0 }, { NULL, 0 } }, 0,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL };

static struct
{
	int looked;
	int (*store)(const struct secret_schema *, const char *, const char *, const char *, void *, void **, ...);
	char *(*lookup)(const struct secret_schema *, void *, void **, ...);
	int (*clear)(const struct secret_schema *, void *, void **, ...);
	void (*free_password)(char *);
	void (*free_error)(void *);
} secret;

static int secret_ready(void)
{
	if (!secret.looked)
	{
		void *library = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
		void *glib = dlopen("libglib-2.0.so.0", RTLD_NOW | RTLD_LOCAL);

		secret.looked = 1;
		if (library && glib)
		{
			*(void **)&secret.store = dlsym(library, "secret_password_store_sync");
			*(void **)&secret.lookup = dlsym(library, "secret_password_lookup_sync");
			*(void **)&secret.clear = dlsym(library, "secret_password_clear_sync");
			*(void **)&secret.free_password = dlsym(library, "secret_password_free");
			*(void **)&secret.free_error = dlsym(glib, "g_error_free");
		}
		if (!secret.store || !secret.lookup || !secret.clear || !secret.free_password || !secret.free_error)
			secret.store = NULL;
	}
	return secret.store != NULL;
}

const char *sc_secret_store_name(void)
{
	return secret_ready() ? "your desktop's keyring" : NULL;
}

int sc_secret_put(const char *account, const char *token)
{
	void *error = NULL;
	int ok;

	if (!secret_ready())
		return 0;
	ok = secret.store(&schema, "default", "Chupathingy Steam link", token, NULL, &error, "account", account, NULL);
	if (error)
	{
		secret.free_error(error);
		ok = 0;
	}
	return ok;
}

int sc_secret_get(const char *account, char *token, size_t size)
{
	void *error = NULL;
	char *found;
	int ok = 0;

	if (!secret_ready())
		return 0;
	found = secret.lookup(&schema, NULL, &error, "account", account, NULL);
	if (error)
		secret.free_error(error);
	if (found)
	{
		if (strlen(found) < size)
		{
			snprintf(token, size, "%s", found);
			ok = 1;
		}
		secret.free_password(found);
	}
	return ok;
}

void sc_secret_delete(const char *account)
{
	void *error = NULL;

	if (!secret_ready())
		return;
	secret.clear(&schema, NULL, &error, "account", account, NULL);
	if (error)
		secret.free_error(error);
}

#else

const char *sc_secret_store_name(void)
{
	return NULL;
}

int sc_secret_put(const char *account, const char *token)
{
	(void)account;
	(void)token;
	return 0;
}

int sc_secret_get(const char *account, char *token, size_t size)
{
	(void)account;
	(void)token;
	(void)size;
	return 0;
}

void sc_secret_delete(const char *account)
{
	(void)account;
}

#endif
#endif /* HALO_MCC */
