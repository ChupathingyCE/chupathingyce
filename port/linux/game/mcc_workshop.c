#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */
#include <stddef.h>
#include "cseries.h"
#include "mcc_workshop.h"
#include "mcc_maps.h"
#include "mcc_package.h"
#include <stdio.h>
#include <string.h>

static int mcc_workshop_digits(char const *text, unsigned length)
{
    unsigned i;
    if (!length || length>20) return 0;
    for (i=0;i<length;i++) if (text[i]<'0' || text[i]>'9') return 0;
    return 1;
}

/* Printable ASCII only: the title is shown in an error box and sent as bytes. */
static void mcc_workshop_title_copy(char const *text, unsigned length, char title[MCC_WORKSHOP_TITLE_SIZE])
{
    unsigned i,used=0;
    for (i=0;i<length && used<MCC_WORKSHOP_TITLE_SIZE-1;i++) {
        unsigned char c=(unsigned char)text[i];
        if (c>=32 && c<127) title[used++]=(char)c;
    }
    while (used && title[used-1]==' ') used--;
    title[used]=0;
}

int mcc_workshop_parse_sidecar(char const *text, struct mcc_workshop_item *item)
{
    unsigned length=0,title_start,title_length=0;
    memset(item,0,sizeof(*item));
    while (text[length] && text[length]!='\r' && text[length]!='\n') length++;
    if (!mcc_workshop_digits(text,length)) return 0;
    memcpy(item->id,text,length);
    title_start=length;
    while (text[title_start]=='\r' || text[title_start]=='\n') title_start++;
    while (text[title_start+title_length] && text[title_start+title_length]!='\r' &&
        text[title_start+title_length]!='\n') title_length++;
    mcc_workshop_title_copy(text+title_start,title_length,item->title);
    return 1;
}

int mcc_workshop_package_folder(char const *name, char folder[64])
{
    char canonical[64],*first,*last;
    if (!mcc_package_name(name,canonical)) return 0;
    first=strchr(canonical,'\\')+1;
    last=strrchr(canonical,'\\');
    if (last<=first) return 0;
    *last=0;
    strcpy(folder,first);
    return 1;
}

int mcc_workshop_local_item(char const *name, struct mcc_workshop_item *item)
{
    char folder[64],path[160],text[128];
    FILE *file;
    size_t length;
    memset(item,0,sizeof(*item));
    if (!mcc_workshop_package_folder(name,folder)) return 0;
    snprintf(path,sizeof(path),"%s%s\\%s",MCC_MAPS_DIRECTORY,folder,MCC_WORKSHOP_SIDECAR);
    file=fopen(path,"rb");
    if (file) {
        length=fread(text,1,sizeof(text)-1,file);
        fclose(file);
        text[length]=0;
        if (mcc_workshop_parse_sidecar(text,item)) return 1;
    }
    if (mcc_workshop_digits(folder,(unsigned)strlen(folder))) {
        char title[MCC_WORKSHOP_TITLE_SIZE*2];
        strcpy(item->id,folder);
        /* the Steam item's own title when this machine has the item */
        if (mcc_maps_workshop_title(folder,title,sizeof(title)))
            mcc_workshop_title_copy(title,(unsigned)strlen(title),item->title);
        if (!item->title[0]) mcc_workshop_title_from_level(name,item->title);
        return 1;
    }
    memset(item,0,sizeof(*item));
    return 0;
}

void mcc_workshop_title_from_level(char const *name, char title[MCC_WORKSHOP_TITLE_SIZE])
{
    char const *leaf=strrchr(name,'\\');
    unsigned i,start_word=1;
    leaf=leaf ? leaf+1 : name;
    for (i=0;leaf[i] && i<MCC_WORKSHOP_TITLE_SIZE-1;i++) {
        unsigned char c=(unsigned char)leaf[i];
        if (c=='_') c=' ';
        else if (start_word && c>='a' && c<='z') c=(unsigned char)(c-'a'+'A');
        title[i]=(c>=32 && c<127) ? (char)c : '?';
        start_word=c==' ';
    }
    title[i]=0;
}

/* Layout: 'W' 'S' 1, the id, NUL, the title, NUL, zeros. */
void mcc_workshop_wire_pack(char name[128], struct mcc_workshop_item const *item)
{
    char *tail=name+MCC_WORKSHOP_WIRE_OFFSET;
    unsigned id_length=(unsigned)strlen(item->id),title_length=(unsigned)strlen(item->title);
    memset(tail,0,MCC_WORKSHOP_WIRE_SIZE);
    if (!mcc_workshop_digits(item->id,id_length)) return;
    if (title_length>MCC_WORKSHOP_WIRE_SIZE-3-id_length-2) title_length=MCC_WORKSHOP_WIRE_SIZE-3-id_length-2;
    tail[0]='W';
    tail[1]='S';
    tail[2]=MCC_WORKSHOP_WIRE_VERSION;
    memcpy(tail+3,item->id,id_length);
    memcpy(tail+4+id_length,item->title,title_length);
}

int mcc_workshop_wire_unpack(char const name[128], struct mcc_workshop_item *item)
{
    char const *tail=name+MCC_WORKSHOP_WIRE_OFFSET,*end;
    unsigned id_length;
    memset(item,0,sizeof(*item));
    if (tail[0]!='W' || tail[1]!='S' || tail[2]!=MCC_WORKSHOP_WIRE_VERSION) return 0;
    end=memchr(tail+3,0,MCC_WORKSHOP_WIRE_SIZE-3);
    if (!end) return 0;
    id_length=(unsigned)(end-(tail+3));
    if (!mcc_workshop_digits(tail+3,id_length)) return 0;
    memcpy(item->id,tail+3,id_length);
    if (!memchr(end+1,0,(size_t)(tail+MCC_WORKSHOP_WIRE_SIZE-(end+1)))) {
        memset(item,0,sizeof(*item));
        return 0;
    }
    mcc_workshop_title_copy(end+1,(unsigned)strlen(end+1),item->title);
    return 1;
}

void mcc_workshop_refusal_text(enum mcc_workshop_refusal kind, struct mcc_workshop_item const *item,
    char const *level, char *text, unsigned size)
{
    char title[MCC_WORKSHOP_TITLE_SIZE];
    if (item->title[0]) strcpy(title,item->title);
    else mcc_workshop_title_from_level(level,title);
    switch (kind) {
    case MCC_WORKSHOP_MISSING:
        /* (said when the Workshop download is not offered: network.workshop_downloads
         * "never", or a build without the Workshop) */
        snprintf(text,size,"You don't have %s (Steam Workshop item %s). "
            "Get it in the Delta Guide's Workshop, or turn Workshop downloads on when you join.",title,item->id);
        break;
    case MCC_WORKSHOP_DIFFERS:
        snprintf(text,size,"Your copy of %s (Steam Workshop item %s) doesn't match the host's. "
            "Update it in the Delta Guide's Workshop, or get the host's version.",
            title,item->id);
        break;
    default:
        /* (the host's package carries no Workshop item: nothing to fetch) */
        snprintf(text,size,"The host's MCC map %s isn't on the Steam Workshop, so it can't be fetched for you. "
            "You need the same package in mcc_maps (its folder name may differ).",level);
        break;
    }
}

int mcc_workshop_host_item(char const name[128], struct mcc_workshop_item *item)
{
    char level[64],folder[64],title[MCC_WORKSHOP_TITLE_SIZE*2];
    unsigned length;
    if (mcc_workshop_wire_unpack(name,item)) return 1;
    memset(item,0,sizeof(*item));
    memcpy(level,name,sizeof(level)-1);
    level[sizeof(level)-1]=0;
    if (!mcc_workshop_package_folder(level,folder)) return 0;
    length=(unsigned)strlen(folder);
    if (length>=MCC_WORKSHOP_ID_SIZE || !mcc_workshop_digits(folder,length)) return 0;
    strcpy(item->id,folder);
    if (mcc_maps_workshop_title(folder,title,sizeof(title)))
        mcc_workshop_title_copy(title,(unsigned)strlen(title),item->title);
    return 1;
}

int mcc_workshop_listing(char const name[128], struct mcc_workshop_item *item)
{
    char level[64];
    memset(item,0,sizeof(*item));
    if (mcc_workshop_wire_unpack(name,item)) return 1;
    memset(item,0,sizeof(*item));
    /* (the name proper: its first 64 bytes, ended) */
    memcpy(level,name,sizeof(level)-1);
    level[sizeof(level)-1]=0;
    return mcc_workshop_local_item(level,item) && item->id[0];
}

/* (mcc_workshop_map_title) a and b the same, ignoring case */
static int mcc_workshop_same_text(char const *a, char const *b)
{
    for (;*a && *b;a++,b++) {
        char x=*a>='A' && *a<='Z' ? (char)(*a+32) : *a, y=*b>='A' && *b<='Z' ? (char)(*b+32) : *b;
        if (x!=y) return 0;
    }
    return !*a && !*b;
}

int mcc_workshop_map_title(char const *level, wchar_t const *catalog_name, char const *item_title, char *title,
    unsigned size)
{
    char plain[64];
    char const *leaf=level ? strrchr(level,'\\') : NULL;
    char *mark;
    unsigned used=0,n;
    if (!size) return 0;
    title[0]=0;
    /* (the catalog's name for the level, printable ASCII, when it is more
     * than its file's own made readable: "Blood Gulch" for 20.map, never "20") */
    if (leaf && catalog_name) {
        for (n=0;leaf[n+1] && n<sizeof(plain)-1;n++) plain[n]=leaf[n+1]=='_' ? ' ' : leaf[n+1];
        plain[n]=0;
        for (n=0;catalog_name[n] && used+1<size;n++)
            title[used++]=catalog_name[n]>=32 && catalog_name[n]<127 ? (char)catalog_name[n] : '?';
        title[used]=0;
        /* (less this machine's own package mark, " [<folder>]") */
        mark=strrchr(title,'[');
        if (used && title[used-1]==']' && mark && mark>title && mark[-1]==' ') mark[-1]=0;
        if (mcc_workshop_same_text(title,plain)) title[0]=0;
    }
    /* (else the item's own title, as the host knows it) */
    if (!title[0] && item_title && item_title[0]) snprintf(title,size,"%s",item_title);
    return title[0]!=0;
}
#endif /* HALO_MCC */
