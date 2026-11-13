#ifndef MCC_CACHE_H
#define MCC_CACHE_H

struct cache_file_tag_header;
struct scenario_structure_bsp_reference;
struct bitmap_data;

/* Path ownership is decided before stripping a filename. MCC never searches
 * maps/, custom_maps/ or the optional Custom Edition installation. */
boolean mcc_level_name(char const *name);
void mcc_cache_validation_clear(void);
boolean mcc_cache_available(char const *name);
boolean mcc_cache_present(char const *name, unsigned long checksum,
    char *message, long message_size);
boolean mcc_cache_require(char const *name, unsigned long checksum);
boolean mcc_cache_precache_ready(char const *name);
void mcc_cache_precache_clear(void);
unsigned long mcc_cache_checksum(char const *name);
struct cache_file_tag_header *mcc_cache_tags_load(char const *name, void *header);
boolean mcc_cache_tags_loaded(void);
short mcc_cache_scenario_type(void);
void mcc_cache_tags_unload(void);
boolean mcc_cache_contains(void const *address, long bytes);
boolean mcc_cache_read(long tag, long offset, long bytes, void *out);
boolean mcc_cache_bsp_load(struct scenario_structure_bsp_reference const *reference,
    void **header, void **structure);
void mcc_cache_bsp_unload(void);
short mcc_cache_part_palette(void const *buffer, byte const **nodes);
boolean mcc_cache_bitmap_valid(struct bitmap_data *bitmap);
void const *mcc_cache_bitmap_pixels(struct bitmap_data const *bitmap, unsigned long *bytes, int *coverage);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_cache_available(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_cache_bitmap_valid(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_cache_bsp_load(a0, a1, a2) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (boolean)0)
#define mcc_cache_bsp_unload() ((void)0)
#define mcc_cache_checksum(a0) ((void)sizeof(a0), (unsigned long)0)
#define mcc_cache_precache_ready(a0) ((void)sizeof(a0), (boolean)0)
#define mcc_cache_read(a0, a1, a2, a3) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (boolean)0)
#define mcc_cache_require(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (boolean)0)
#define mcc_cache_tags_load(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (struct cache_file_tag_header *)0)
#define mcc_cache_tags_loaded() ((boolean)0)
#define mcc_cache_tags_unload() ((void)0)
#define mcc_level_name(a0) ((void)sizeof(a0), (boolean)0)
#endif

#endif
