/* One optional package directory, with explicit MCC-root resource fallback. */
#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */
#include "cseries.h"
#include "mcc_package.h"
#include "mcc_maps.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

int mcc_package_name(char const *name, char canonical[64])
{
    size_t i, start=9, separators=0, length;
    static char const prefix[]="mcc_maps";
    if (!name) return 0;
    for (length=0;length<64 && name[length];length++) {}
    if (length<10 || length>63) return 0;
    for (i=0;i<8;i++) {
        unsigned char c=(unsigned char)name[i];
        if (c>='A' && c<='Z') c+='a'-'A';
        if (c!=prefix[i]) return 0;
    }
    if (name[8]!='/' && name[8]!='\\') return 0;
    memcpy(canonical,"mcc_maps\\",9);
    for (i=9;i<=length;i++) {
        unsigned char c=(unsigned char)name[i];
        if (!c || c=='/' || c=='\\') {
            if (i==start || name[i-1]=='.' || name[i-1]==' ') return 0;
            if (c && ++separators>1) return 0;
            start=i+1;
            canonical[i]=c ? '\\' : 0;
        } else {
            if (c<32 || strchr(":*?\"<>|",c)) return 0;
            canonical[i]=(char)c;
        }
    }
    return 1;
}

int mcc_package_map_path(char const *name, char path[128])
{
    char canonical[64];
    if (!mcc_package_name(name,canonical)) return 0;
    /* port: (in maps_mcc: mcc_maps_folder; the name keeps GulchCE's mcc_maps) */
    sprintf(path,"d:\\%s%s.map",mcc_maps_folder(),canonical+8);
    return 1;
}

int mcc_package_resource_path(char const *name, unsigned type, char path[128])
{
    char canonical[64], *separator;
    size_t i;
    if (type<1 || type>2 || !mcc_package_name(name,canonical)) return 0;
    separator=strrchr(canonical,'\\');
    *separator=0;
    for (i=0;canonical[i];i++) if (canonical[i]=='\\') canonical[i]='/';
    /* port: (in maps_mcc: mcc_maps_folder) */
    sprintf(path,"%s%s/%s.map",mcc_maps_folder(),canonical+8,type==1 ? "bitmaps" : "sounds");
    return 1;
}

FILE *mcc_package_resource_open(char const *name,unsigned type,char path[128])
{
    FILE *file;
    char root[128];
    path[0]=0;
    if (!mcc_package_resource_path(name,type,path) ||
        !mcc_package_resource_path("mcc_maps/root",type,root)) {errno=EINVAL;return NULL;}
    errno=0;
    file=fopen(path,"rb");
    /* A present but unreadable or invalid override must never be replaced. */
    if (!file && errno==ENOENT && strcmp(path,root)) {
        strcpy(path,root);
        file=fopen(path,"rb");
    }
    return file;
}

/* chester (GulchCE): the base resource sets in the loader's order, the
legacy root first, then MCC's singleplayer and multiplayer sets */
int mcc_package_base_resource_path(unsigned type,unsigned set,char path[128])
{
    if (type<1 || type>2 || set>=MCC_PACKAGE_BASE_SETS) return 0;
    if (!set) return mcc_package_resource_path("mcc_maps/root",type,path);
    snprintf(path,128,"mcc_maps/_shared/%s/%s.map",set==1 ? "singleplayer" : "multiplayer",
        type==1 ? "bitmaps" : "sounds");
    return 1;
}

int mcc_package_shared_resource(char const *path,unsigned type)
{
    char expected[128];
    unsigned set;
    if (!path) return 0;
    for (set=0;set<MCC_PACKAGE_BASE_SETS;set++)
        if (mcc_package_base_resource_path(type,set,expected) && !strcmp(path,expected)) return 1;
    return 0;
}

int mcc_package_resource_candidate(char const *name,unsigned type,unsigned set,char path[128])
{
    FILE *file;
    if (set>=MCC_PACKAGE_BASE_SETS || !mcc_package_resource_path(name,type,path)) return 0;
    if (!mcc_package_shared_resource(path,type)) {
        errno=0;
        file=fopen(path,"rb");
        if (file) {fclose(file);return 1;}
        if (errno!=ENOENT) return 0;
    }
    return mcc_package_base_resource_path(type,set,path);
}
#endif /* HALO_MCC */
