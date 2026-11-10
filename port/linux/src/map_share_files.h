/*
MAP_SHARE_FILES.H

Map sharing's files (map_share_files.c), for the game's side of it
(port/linux/game/map_share_host.c, map_share_client.c; port/linux/README.md, "Map sharing"):
the hashes the wire carries, the host's catalog of the one file it serves
(its game's current map, hashed on a thread of its own the first time it is
asked for), and the fetch's part file and result.

The hashes: a piece is 0x40000 bytes of the file (the last one shorter);
its hash is BLAKE2b-256 of its bytes; the file's is BLAKE2b-256 of
"OpenCE map 1", the size as 4 bytes little-endian, and every piece's hash in
order (the same as map_archive_map_hash).

Plain types only: the game's units (64-bit builds rewrite their long) and
the platform's both call it.
*/

#ifndef MAP_SHARE_FILES_H
#define MAP_SHARE_FILES_H

enum
{
	MAP_SHARE_PIECE_SIZE = 0x40000,
	MAP_SHARE_HASH_SIZE = 32,
};

/* a piece's hash */
void map_share_piece_hash(const void *data, unsigned int size, unsigned char *hash);
/* a file's hash, from its size and its count piece hashes (32 bytes each) */
void map_share_file_hash(unsigned int size, const unsigned char *pieces, unsigned int count, unsigned char *hash);
/* the number of pieces of a file of size bytes */
unsigned int map_share_piece_count(unsigned int size);

/* ---------- the host's catalog: one file, the game's current map

The file at path (a game path, as map_family_find gives it): 1 when it is
hashed, with its size and hash; 0 while it is being hashed (the first call
for a path, or a file that changed since, starts that); -1 if it cannot be
(no such file, or not read whole). A file of another path replaces the one
kept. */
int map_share_catalog_file(const char *path, unsigned int *size, unsigned char *hash);
/* count piece hashes from first of the file hashed (the last call's, which
returned 1), into hashes (32 bytes each): 1, else 0 */
int map_share_catalog_piece_hashes(unsigned int first, unsigned int count, unsigned char *hashes);
/* size bytes at offset of that file, into buffer: 1, else 0 */
int map_share_catalog_read(unsigned int offset, void *buffer, unsigned int size);
/* the catalog forgets its file (the host's game ended, or sharing turned off) */
void map_share_catalog_clear(void);

/* ---------- the fetch's files (paths of this computer's, not game paths) */

/* a part file, made empty: NULL if it can't be */
void *map_share_part_open(const char *path);
/* size bytes written at offset: 1, else 0 */
int map_share_part_write(void *part, unsigned int offset, const void *data, unsigned int size);
/* closed; renamed to final when keep is set (1 if that worked), else deleted */
int map_share_part_close(void *part, const char *path, const char *final, int keep);
/* a text file written whole (to a temporary file beside it, then renamed): 1, else 0 */
int map_share_write_text(const char *path, const char *text);
/* whether a file of that path exists */
int map_share_file_exists(const char *path);

#endif
