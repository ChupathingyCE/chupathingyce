/* Halo 1 MCC map discovery. This list never includes the Xbox or CE folders. */
#ifndef __MCC_MAPS_H
#define __MCC_MAPS_H

/* port: MCC maps go in maps_mcc, beside maps, maps_ce, maps_pc and maps_md;
GulchCE's mcc_maps is read when there is no maps_mcc (mcc_maps_directory) */
#define MCC_MAPS_FOLDER "d:\\maps_mcc\\"
#define MCC_MAPS_FALLBACK_FOLDER "d:\\mcc_maps\\"
#define MCC_MAPS_DIRECTORY mcc_maps_directory()
#define MCC_MAPS_LEVEL_PREFIX "mcc_maps\\"
#define MCC_MAPS_FIRST_DISPLAY_INDEX 0x1000
#define MCC_MAPS_MAXIMUM 1024

char const *mcc_maps_directory(void);
void mcc_maps_rescan(void);
short mcc_maps_count(boolean multiplayer_only);
short mcc_maps_index(short row, boolean multiplayer_only);
short mcc_maps_find(char const *level_name);
char const *mcc_maps_level_name(short index);
wchar_t const *mcc_maps_name(short index);
wchar_t const *mcc_maps_description(short index);
boolean mcc_maps_campaign(short index);
boolean mcc_maps_level_campaign(char const *level_name);

#endif
