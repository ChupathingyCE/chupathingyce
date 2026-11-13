/*
STEAM_CONTENT.H

Chupathingy's own Steam content client: Halo: MCC's (app 976730) Workshop
items fetched straight from Steam's content network, on every platform,
with no Steam client or steamcmd. Plain C types only across this boundary.

Sign-in is by QR code only, scanned with the Steam mobile app: the game
never sees a password. The sign-in gives a refresh token, kept in the
game's data folder (owner-only permissions, never logged) with the
account's name, a LoginID made at random once per install, and the Steam
persona name to show; the token itself goes in the system's secret store
where there is one (sc_secret.c), else in that file. Sign out deletes both
and revokes the token at Steam.

The LoginID: Steam keeps one session per account, public address and
LoginID. The Steam client makes its LoginID from the computer's private
address, so another program on the same computer doing the same would
replace the player's running Steam session. Ours is random, never zero and
never taken from an address, and goes in every logon.

A download (steam_content_get_item): log on to a Steam CM over a TLS
WebSocket, ask for the item's manifest id (GetPublishedFileDetails'
hcontent_file), the manifest's request code, the depot's decryption key
and the content servers; then fetch the manifest (filenames decrypted),
and each file's chunks (AES, then LZMA or Zstandard, SHA-1 checked) from
the content servers, a few at a time, retried on another server when one
fails. Files already whole (their SHA-1 matches) are kept, so a stopped
download resumes. The item lands in <library>/steamapps/workshop/content/
976730/<id>/, the same layout as Steam's, so the game's Workshop code
reads it in place (steam_workshop.c).

All network work is on the client's own threads; these calls only take
a lock, copy and return.

Errors are one or two words for the player (steam_content_error_text);
the reason goes to the log (debug.txt). The Steam name and SteamID are
for showing on this computer only: never logged and never sent anywhere
but Steam.

SPDX-FileCopyrightText: Milenko x Horrible Dev
SPDX-License-Identifier: MIT
*/

#ifndef STEAM_CONTENT_H
#define STEAM_CONTENT_H

enum
{
	STEAM_CONTENT_APP = 976730,
	STEAM_CONTENT_PERSONA_SIZE = 64,
	STEAM_CONTENT_QR_SIZE = 256,
	STEAM_CONTENT_PATH_SIZE = 512,
	/* how long a QR sign-in waits for the scan */
	STEAM_CONTENT_SIGN_IN_TIMEOUT = 5 * 60 * 1000,
	/* chunks fetched at once */
	STEAM_CONTENT_WORKERS = 4,
};

/* the player's words for what went wrong */
enum
{
	STEAM_CONTENT_OK = 0,
	/* the depot key (or the manifest) was refused: the account doesn't own MCC */
	STEAM_CONTENT_NOT_OWNED,
	/* no sign-in, or Steam refused the stored one */
	STEAM_CONTENT_SIGN_IN,
	/* Steam couldn't be reached */
	STEAM_CONTENT_OFFLINE,
	/* anything else (why: the log) */
	STEAM_CONTENT_FAILED,
	STEAM_CONTENT_CANCELLED,
	/* the item, or one of its files, is over the size limit (steam_content_host) */
	STEAM_CONTENT_TOO_LARGE,
	/* not enough free space on the disk for it */
	STEAM_CONTENT_DISK_FULL,
};

/* the size limits when the program gives none: an item's files together, and one file */
#define STEAM_CONTENT_DEFAULT_ITEM_LIMIT (8ull * 1024 * 1024 * 1024)
#define STEAM_CONTENT_DEFAULT_FILE_LIMIT (4ull * 1024 * 1024 * 1024)

/* the QR sign-in */
enum
{
	STEAM_CONTENT_QR_IDLE = 0,
	/* asking Steam for a QR */
	STEAM_CONTENT_QR_STARTING,
	/* the QR's text is ready to draw: waiting for the scan */
	STEAM_CONTENT_QR_WAITING,
	/* the phone has seen it: waiting for the player to approve */
	STEAM_CONTENT_QR_SCANNED,
	STEAM_CONTENT_QR_DONE,
	STEAM_CONTENT_QR_FAILED,
};

/* a download */
enum
{
	STEAM_CONTENT_ITEM_NONE = 0,
	STEAM_CONTENT_ITEM_QUEUED,
	STEAM_CONTENT_ITEM_DOWNLOADING,
	STEAM_CONTENT_ITEM_INSTALLED,
	STEAM_CONTENT_ITEM_FAILED,
};

struct steam_content_progress
{
	int state;
	/* STEAM_CONTENT_* when FAILED */
	int error;
	/* the item's files' bytes: written (or found whole) and all */
	unsigned long long done;
	unsigned long long total;
};

/* ---------- the program's side (set before anything else) */

struct steam_content_host
{
	/* the folder the sign-in file lives in (the game's data folder) */
	char data_folder[STEAM_CONTENT_PATH_SIZE];
	/* the program's lock around Mbed TLS (posix_browser_lock), or NULL */
	void (*tls_lock)(void);
	void (*tls_unlock)(void);
	/* the certificate authorities (an mbedtls_x509_crt *), or NULL for
	the system's bundle (/etc/ssl/cert.pem and the usual places) */
	void *(*certificates)(void);
	/* debug.txt */
	void (*log)(const char *format, ...);
	/* 1: the token only in the sign-in file, never the system's secret store (tests) */
	int file_only;
	/* the largest item (its files together) and file a download takes, in
	bytes; 0 for STEAM_CONTENT_DEFAULT_ITEM_LIMIT and _FILE_LIMIT */
	unsigned long long item_limit;
	unsigned long long file_limit;
};

void steam_content_init(const struct steam_content_host *host);

/* ---------- the interface */

/* a sign-in is stored: 1, with the Steam persona name to show (may be "" until the first logon) */
int steam_content_linked(char *persona, int size);

/* a QR sign-in started (one at a time; any previous one ends) */
void steam_content_sign_in_qr(void);
/* where it stands (STEAM_CONTENT_QR_*), with the QR's text to draw while
WAITING or SCANNED (it can change: draw it each time), and the error when FAILED */
int steam_content_sign_in_poll(char *qr_text, int size, int *error);
/* the QR screen closed */
void steam_content_sign_in_cancel(void);

/* the item downloaded into library/steamapps/workshop/content/976730/<id>/
(queued: one item at a time; its progress through steam_content_progress).
0 when not signed in or the id or folder is bad */
int steam_content_get_item(unsigned long long id, const char *library);
/* an item's progress: 1 if known this run */
int steam_content_progress(unsigned long long id, struct steam_content_progress *progress);
/* stop the item's download (or take it off the queue); files kept for a resume */
void steam_content_cancel(unsigned long long id);
/* where the sign-in's token is kept, for the player ("the macOS Keychain",
"a private file in the data folder") */
const char *steam_content_store(void);
/* 0 when the stored token is linked but only in the private file (no
secret store took it), so the program can say so; 1 otherwise */
int steam_content_store_secure(void);
/* stop everything, forget the sign-in (the file and the stored token
deleted, LoginID with them) and revoke the token at Steam (in the background) */
void steam_content_sign_out(void);
/* the threads told to stop and waited for (the game closing) */
void steam_content_shutdown(void);

/* "Not owned", "Sign in", "Offline", "Failed", "Cancelled"; "" for OK */
const char *steam_content_error_text(int error);

/* ---------- the pieces, for the tests */

/* the player's word for a Steam EResult at a step of the download */
enum
{
	STEAM_CONTENT_STEP_CONNECT = 0,
	STEAM_CONTENT_STEP_LOGON,
	STEAM_CONTENT_STEP_DEPOT_KEY,
	STEAM_CONTENT_STEP_MANIFEST,
	STEAM_CONTENT_STEP_CHUNK,
};
int steam_content_error_from_result(int step, int eresult);

#endif
