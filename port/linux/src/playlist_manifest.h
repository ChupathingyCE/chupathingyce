/*
PLAYLIST_MANIFEST.H

A playlist's map set (docs/playlist-maps.md): the signed manifest the site
serves for a verified playlist, naming each file the playlist plays, the
folder it goes in, its size and SHA-256, and where it comes from (the
community map repositories, Chupathingy's mirror, or the player's own
copy). playlist_manifest.c checks its signature (Delta's keys, delta_key.h,
over PLAYLIST_MANIFEST_CONTEXT and the document) and reads it strictly;
playlist_download.c fetches it and installs what it names.

No file, network or game dependency (Monocypher's Ed25519 alone): the tests
build it by itself (tools/test_playlist_maps.py). Plain types only.
*/

#ifndef PLAYLIST_MANIFEST_H
#define PLAYLIST_MANIFEST_H

#include <stddef.h>

/* what is signed: this, then the document's exact bytes */
#define PLAYLIST_MANIFEST_CONTEXT "chupathingy-playlist-maps 1\n"

enum
{
	PLAYLIST_MANIFEST_FORMAT = 1,
	PLAYLIST_MANIFEST_DOCUMENT_SIZE = 65536,
	PLAYLIST_MANIFEST_FILES = 128,
	PLAYLIST_MANIFEST_PACKS = 16,
	PLAYLIST_MANIFEST_ID_SIZE = 33,
	PLAYLIST_MANIFEST_NAME_SIZE = 49,
	PLAYLIST_MANIFEST_MAP_SIZE = 49,
	PLAYLIST_MANIFEST_FOLDER_SIZE = 24,
	PLAYLIST_MANIFEST_FILE_SIZE = 41,
	PLAYLIST_MANIFEST_PATH_SIZE = 161,
	PLAYLIST_MANIFEST_PACK_ID_SIZE = 25,
	PLAYLIST_MANIFEST_CREDITS_SIZE = 256,
	/* a file's largest size: 1 GB (map_archive.h's limit) */
	PLAYLIST_MANIFEST_MAXIMUM_SIZE = 1024 * 1024 * 1024,
	PLAYLIST_MANIFEST_SHA256_SIZE = 32,
};

/* a file's source */
enum
{
	PLAYLIST_SOURCE_HALONET,
	PLAYLIST_SOURCE_CHUPATHINGY,
	PLAYLIST_SOURCE_OWN,
};

struct playlist_pack
{
	char id[PLAYLIST_MANIFEST_PACK_ID_SIZE];
	char name[PLAYLIST_MANIFEST_NAME_SIZE];
	char author[PLAYLIST_MANIFEST_NAME_SIZE];
	char credits[PLAYLIST_MANIFEST_CREDITS_SIZE];
};

struct playlist_file
{
	/* the game's name for it (empty: a pack's other file, ui.map) */
	char map[PLAYLIST_MANIFEST_MAP_SIZE];
	char title[PLAYLIST_MANIFEST_NAME_SIZE];
	char folder[PLAYLIST_MANIFEST_FOLDER_SIZE];
	char file[PLAYLIST_MANIFEST_FILE_SIZE];
	/* the dl.chupathingy.org path (PLAYLIST_SOURCE_CHUPATHINGY's) */
	char path[PLAYLIST_MANIFEST_PATH_SIZE];
	/* (0 and no hash: an own file's, when not given) */
	unsigned int size;
	unsigned char has_sha256;
	unsigned char sha256[PLAYLIST_MANIFEST_SHA256_SIZE];
	unsigned char source;
	/* its pack's index in packs, or -1 */
	signed char pack;
};

struct playlist_manifest
{
	char playlist[PLAYLIST_MANIFEST_ID_SIZE];
	char name[PLAYLIST_MANIFEST_NAME_SIZE];
	unsigned int serial;
	long long issued;
	int pack_count;
	struct playlist_pack packs[PLAYLIST_MANIFEST_PACKS];
	int file_count;
	struct playlist_file files[PLAYLIST_MANIFEST_FILES];
};

/* the document (size bytes) and its signature's text (128 hex digits, white
space after them allowed), checked: the signature one of Delta's keys' over
the context and the document, the key allowed the serial's epoch, then the
document read; it must be playlist's own (NULL: any). 1 with it in manifest,
else 0 with why in why */
int playlist_manifest_check(const char *document, size_t size, const char *signature, size_t signature_length,
	const char *playlist, struct playlist_manifest *manifest, char *why, int why_size);

/* the document alone read (its signature not checked: the tests' and
playlist_manifest_check's) */
int playlist_manifest_parse(const char *document, size_t size, struct playlist_manifest *manifest, char *why,
	int why_size);

/* the names the manifest allows */
int playlist_manifest_id_valid(const char *text);
int playlist_manifest_file_valid(const char *text);
int playlist_manifest_folder_valid(const char *text);
int playlist_manifest_path_valid(const char *text);

/* SHA-256 in pieces, for files too large to hold */
struct playlist_sha256
{
	unsigned int state[8];
	unsigned long long length;
	unsigned char block[64];
	unsigned int used;
};

void playlist_sha256_begin(struct playlist_sha256 *context);
void playlist_sha256_add(struct playlist_sha256 *context, const void *data, size_t size);
void playlist_sha256_end(struct playlist_sha256 *context, unsigned char digest[PLAYLIST_MANIFEST_SHA256_SIZE]);

#endif
