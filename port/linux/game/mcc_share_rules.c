#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */
#include "mcc_share_rules.h"
#include "mcc_package.h"
#include <string.h>

static int mcc_share_same(char const *a, char const *b)
{
    for (;*a && *b;a++,b++) {
        unsigned char x=(unsigned char)*a,y=(unsigned char)*b;
        if (x>='A' && x<='Z') x=(unsigned char)(x+'a'-'A');
        if (y>='A' && y<='Z') y=(unsigned char)(y+'a'-'A');
        if (x!=y) return 0;
    }
    return !*a && !*b;
}

/* Sharing names must be portable even when the sender is not on Windows.
 * This is stricter than local discovery and does not change its accepted names. */
static int mcc_share_component(char const *name)
{
    char device[64];
    size_t i,length=0;
    if (!*name || *name=='.' || *name==' ') return 0;
    for (i=0;name[i];i++) {
        unsigned char c=(unsigned char)name[i];
        if (c<32 || c>126) return 0;
    }
    /* A device name remains a device before an extension or trailing spaces. */
    while (name[length] && name[length]!='.') length++;
    while (length && name[length-1]==' ') length--;
    memcpy(device,name,length);
    device[length]=0;
    if (mcc_share_same(device,"con") || mcc_share_same(device,"prn") ||
        mcc_share_same(device,"aux") || mcc_share_same(device,"nul") ||
        mcc_share_same(device,"conin$") || mcc_share_same(device,"conout$")) return 0;
    if (length==4 && device[3]>='0' && device[3]<='9') {
        device[3]=0;
        if (mcc_share_same(device,"com") || mcc_share_same(device,"lpt")) return 0;
    }
    return 1;
}

static int mcc_share_digits(char const *text)
{
    unsigned i;
    for (i=0;text[i];i++) if (text[i]<'0' || text[i]>'9') return 0;
    return i>0;
}

enum mcc_share_reason mcc_share_decide(struct mcc_share_package const *package)
{
    char input[64],canonical[64];
    char *folder,*level;
    if (!package || !package->name || !package->name_length ||
        package->name_length>=sizeof(input) ||
        memchr(package->name,0,package->name_length)) return MCC_SHARE_BAD_NAME;
    memcpy(input,package->name,package->name_length);
    input[package->name_length]=0;
    if (!mcc_package_name(input,canonical)) return MCC_SHARE_BAD_NAME;
    folder=canonical+9;
    level=strchr(folder,'\\');
    if (!level) return MCC_SHARE_NOT_A_PACKAGE;
    *level++=0;
    if (!mcc_share_component(folder) || !mcc_share_component(level) ||
        mcc_share_same(level,"bitmaps") || mcc_share_same(level,"sounds") ||
        mcc_share_same(level,"loc")) return MCC_SHARE_BAD_NAME;
    if (mcc_share_same(folder,"_shared")) return MCC_SHARE_SHARED_RESOURCES;
    if (package->content==MCC_SHARE_CONTENT_STOCK) return MCC_SHARE_STOCK;
    if (mcc_share_digits(folder) || package->sidecar==MCC_SHARE_SIDECAR_VALID) return MCC_SHARE_WORKSHOP;
    if (package->sidecar!=MCC_SHARE_SIDECAR_ABSENT) return MCC_SHARE_UNKNOWN_ORIGIN;
    if (package->content!=MCC_SHARE_CONTENT_CUSTOM || !package->content_known) return MCC_SHARE_UNKNOWN_CONTENT;
    if (!package->host_opt_in) return MCC_SHARE_NOT_OPTED_IN;
    return MCC_SHARE_ELIGIBLE;
}

int mcc_share_eligible(struct mcc_share_package const *package)
{
    return mcc_share_decide(package)==MCC_SHARE_ELIGIBLE;
}

char const *mcc_share_reason_word(enum mcc_share_reason reason)
{
    switch (reason) {
    case MCC_SHARE_ELIGIBLE: return "eligible";
    case MCC_SHARE_NOT_OPTED_IN: return "not-opted-in";
    case MCC_SHARE_BAD_NAME: return "bad-name";
    case MCC_SHARE_NOT_A_PACKAGE: return "not-a-package";
    case MCC_SHARE_SHARED_RESOURCES: return "shared-resources";
    case MCC_SHARE_STOCK: return "stock";
    case MCC_SHARE_WORKSHOP: return "workshop";
    case MCC_SHARE_UNKNOWN_ORIGIN: return "unknown-origin";
    case MCC_SHARE_UNKNOWN_CONTENT: return "unknown-content";
    }
    return "unknown";
}

int mcc_share_file_allowed(char const *leaf, size_t length)
{
    char name[12];
    if (!leaf || (length!=10 && length!=11) || memchr(leaf,0,length)) return 0;
    memcpy(name,leaf,length);
    name[length]=0;
    return mcc_share_same(name,"bitmaps.map") || mcc_share_same(name,"sounds.map");
}

int mcc_share_decline(unsigned char const *request, size_t size, unsigned char *reply)
{
    size_t i;
    if (!request || !reply || size!=MCC_SHARE_REQUEST_SIZE) return 0;
    if (request[0]<MCC_SHARE_OP_MANIFEST || request[0]>MCC_SHARE_OP_HAVE) return 0;
    if (!memchr(request+16,0,MCC_SHARE_LEVEL_SIZE)) return 0;
    memset(reply,0,MCC_SHARE_RESPONSE_SIZE);
    reply[0]=request[0];                 /* op, echoed */
    reply[1]=MCC_SHARE_RESULT_OFF;       /* result: sharing off */
    reply[2]=request[1];                 /* file number */
    reply[3]=0;                          /* count: no entries */
    for (i=4;i<12;i++) reply[i]=request[i]; /* session and offset, echoed */
    /* length 0 at 12: nothing follows */
    memcpy(reply+16,request+16,MCC_SHARE_LEVEL_SIZE); /* the level, echoed */
    /* credit at 80: none */
    return 1;
}

int mcc_share_answer_result(unsigned char const *reply, size_t size)
{
    if (!reply || size<MCC_SHARE_RESPONSE_SIZE) return -1;
    return reply[1];
}

static uint32_t admission_word(unsigned char const *at)
{
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
}

int mcc_admission_take(struct mcc_admission *hold, unsigned long address, unsigned char const *body, size_t size,
    unsigned char const *host, unsigned long now)
{
    uint32_t op, generation;
    if (!hold || !body || size!=MCC_ADMISSION_SIZE) return -1;
    op=admission_word(body);
    generation=admission_word(body+4);
    /* (a name that ends within its first 64 bytes, of an MCC map's) */
    if (!generation || !memchr(body+8,0,64) || strncmp((char const *)body+8,"mcc_maps\\",9)) return -1;
    /* (another machine on the slot, or the host on another map: anew) */
    if (hold->address!=address ||
        (hold->state!=MCC_ADMISSION_NONE && (!host || memcmp(hold->identity,host,MCC_ADMISSION_COMPARED)))) {
        memset(hold,0,sizeof(*hold));
        hold->address=address;
    }
    /* (only the host's own identity) */
    if (!host || memcmp(body+8,host,MCC_ADMISSION_COMPARED)) return MCC_ADMISSION_REFUSED;
    /* (a pending hold's end, by its age or its quiet: closed) */
    if (hold->state==MCC_ADMISSION_PENDING &&
        (now-hold->started>=MCC_ADMISSION_LIMIT || now-hold->heard>=MCC_ADMISSION_QUIET))
        hold->state=MCC_ADMISSION_CLOSED;
    if (op==MCC_ADMISSION_BEGIN && hold->state==MCC_ADMISSION_NONE) {
        hold->state=MCC_ADMISSION_PENDING;
        hold->generation=generation;
        hold->started=now;
        memcpy(hold->identity,host,MCC_ADMISSION_COMPARED);
    }
    if (hold->state!=MCC_ADMISSION_PENDING || hold->generation!=generation) return MCC_ADMISSION_REFUSED;
    hold->heard=now;
    switch (op) {
    case MCC_ADMISSION_BEGIN:
    case MCC_ADMISSION_PING:
        return MCC_ADMISSION_ACK;
    case MCC_ADMISSION_READY:
        hold->state=MCC_ADMISSION_HELD_READY;
        hold->started=now;
        return 0;
    case MCC_ADMISSION_CANCEL:
        hold->state=MCC_ADMISSION_CLOSED;
        return 0;
    default:
        return MCC_ADMISSION_REFUSED;
    }
}

int mcc_admission_holding(struct mcc_admission const *hold, unsigned long address, unsigned long now)
{
    if (!hold || hold->address!=address) return 0;
    if (hold->state==MCC_ADMISSION_PENDING)
        return now-hold->started<MCC_ADMISSION_LIMIT && now-hold->heard<MCC_ADMISSION_QUIET;
    if (hold->state==MCC_ADMISSION_HELD_READY) return now-hold->started<MCC_ADMISSION_GRACE;
    return 0;
}
#endif /* HALO_MCC */
