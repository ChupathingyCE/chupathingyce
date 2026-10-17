/*
CABINET.H

Halo PC's maps out of Microsoft cabinets (cabinet.c), for the importers of a
Halo PC disc image (pc_disc.c) and of Custom Edition's installer
(ce_installer.c).
*/

#ifndef __HALO_LINUX_CABINET_H
#define __HALO_LINUX_CABINET_H

/* called as the copy goes: the map being written, and the bytes written of
all the maps' */
typedef void (*cabinet_progress_proc)(void *context, const char *file, unsigned long long done,
	unsigned long long total);

/* one import: the file read (a disc image or an installer, read in place)
and the folder its maps go in */
struct cabinet_import
{
	int descriptor;
	unsigned long long file_size;
	char folder[1024];
	cabinet_progress_proc progress;
	void *context;
	unsigned long long done;
	unsigned long long total;
	/* maps found, and of them the ones the folder already has (left as
	they are) */
	int found;
	int kept;
	char *error;
	int error_size;
};

/* opens the file to import from, its maps going in <destination>/<folder>
(made if missing); 0 with the reason in error */
int cabinet_import_open(struct cabinet_import *import, const char *path, const char *destination,
	const char *folder, cabinet_progress_proc progress, void *context, char *error, int error_size);
void cabinet_import_close(struct cabinet_import *import);

/* reads size bytes at offset of the file; 0 if they are not all there */
int cabinet_read(struct cabinet_import *import, unsigned long long offset, void *buffer, unsigned int size);

/* sets the player's reason (format has one %s, for detail) and returns 0 */
int cabinet_fail(struct cabinet_import *import, const char *format, const char *detail);

/* whether a cabinet starts at offset, and fits in the size bytes there */
int cabinet_is_cabinet(struct cabinet_import *import, unsigned long long offset, unsigned long long size);

/* counts the maps of the cabinet at offset (found), and adds the ones the
folder lacks to total: what cabinet_extract_maps writes. 0 if it is not a
cabinet this reads */
int cabinet_count_maps(struct cabinet_import *import, unsigned long long offset, unsigned long long size);

/* writes the maps of the cabinet at offset that the folder lacks; 0 with
the reason in error */
int cabinet_extract_maps(struct cabinet_import *import, unsigned long long offset, unsigned long long size);

/* the same for a map stored as it is (on a disc that has its maps folder
uncompressed): name is its file's name */
int cabinet_count_plain_map(struct cabinet_import *import, const char *name, unsigned long long size);
int cabinet_copy_plain_map(struct cabinet_import *import, const char *name, unsigned long long offset,
	unsigned long long size);

/* whether a path in a cabinet or on a disc is a map: a .map file in a
folder named maps; its file's name (lowercase) in name */
int cabinet_map_name(const char *path, char *name, int name_size);

#endif
