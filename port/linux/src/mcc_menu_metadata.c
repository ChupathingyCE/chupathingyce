#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */
#include "mcc_menu_metadata.h"
#include "mcc_manifest.h"
#include "posix.h"
#include "mcc_json.h" /* port: our own JSON lookups, json_reader.h */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *const missions_path[] = { "CampaignMaps", NULL };
static const char *const kind_path[] = { "CampaignMapKind", NULL };

static int same(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return !*a && !*b;
}

const struct steam_workshop_map_text *mcc_menu_metadata_find(const struct mcc_menu_metadata *metadata, const char *name)
{
    int i;
    const struct steam_workshop_map_text *found=NULL;
    for (i=0; i<metadata->count; i++) if (same(name,metadata->maps[i].name)) {
        if (found) return NULL;
        found=&metadata->maps[i];
    }
    return found;
}

/* Read at most one package/family at a time. The catalog calls this once per
 * package, not once per level. Every path uses the same contained reader as
 * images, including metadata that names another metadata file. */
int mcc_menu_metadata_load(const char *root, int campaign, struct mcc_menu_metadata *metadata)
{
    struct steam_workshop_item_text *item;
    struct json value;
    char *text;
    int length, count, index, valid=0;
    memset(metadata,0,sizeof(*metadata));
    text=malloc(STEAM_WORKSHOP_MAXIMUM_JSON);
    if (!text) return 0;
    /* A full collection is too large for the native/guest thread stacks. Reuse
     * its path buffer for campaigns instead of keeping a second copy. */
    item=malloc(sizeof(*item));
    if (!item) goto done;
    length=posix_mcc_preview_read(root,"ModInfo.json",text,STEAM_WORKSHOP_MAXIMUM_JSON);
    if (!steam_workshop_read_item(text,length,item) || !item->halo1) goto done;
    strcpy(metadata->title,item->title);
    count=item->map_count;
    if (campaign) {
        if (!item->has_campaign) goto done;
        length=posix_mcc_preview_read(root,"CampaignInfo.json",text,STEAM_WORKSHOP_MAXIMUM_JSON);
        /* port: (mcc_json.h) the missions' files, in order */
        count=mcc_json_strings(text,length,missions_path,item->map_json[0],sizeof(item->map_json[0]),
            sizeof(item->map_json[0]),MCC_MANIFEST_MAXIMUM_MISSIONS);
        if (count<1) goto done;
    }
    for (index=0; index<count; index++) {
        struct steam_workshop_map_text *map=&metadata->maps[metadata->count];
        length=posix_mcc_preview_read(root,item->map_json[index],text,STEAM_WORKSHOP_MAXIMUM_JSON);
        if (!steam_workshop_read_map(text,length,map)) continue;
        if (campaign) {
            int supported; char kind[32];
            supported=!mcc_json_find(text,length,kind_path,&value) ||
                (mcc_json_string(text,length,kind_path,kind,sizeof(kind)) && mcc_manifest_default_kind(kind));
            if (!supported) continue;
        }
        metadata->count++;
    }
    valid=1;
done:
    free(item); free(text); return valid;
}
#endif /* HALO_MCC */
