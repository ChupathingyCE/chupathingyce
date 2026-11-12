/* Content-matched local MCC paths; the host's name stays on the wire. */
#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */
#include "cseries.h"
#include "errors.h"
#include "game/game.h"
#include "networking/network_game_manager.h"
#include "networking/network_game_globals.h"
#include "interface/ui_widget.h"
#include "mcc_cache.h"
#include "mcc_identity.h"
#include "mcc_maps.h"
#include "mcc_package.h"
#include "mcc_precache.h"
#include "mcc_validation.h"
#include "mcc_workshop.h"
#include <string.h>

#define MCC_IDENTITY_MAX_CANDIDATES 16
static struct {
    char name[64];
    unsigned char digest[32];
    struct mcc_precache_snapshot files;
    struct mcc_workshop_item item;
    int valid;
} mcc_host_identity;
static struct {
    char session[64], local[64];
    unsigned char digest[32];
    struct mcc_precache_snapshot files;
} mcc_client_identity;

static int mcc_identity_error(char const *message)
{
    wchar_t text[256];
    unsigned i;
    error(_error_silent,"mcc identity: %s",message);
    for (i=0;message[i] && i<255;i++) text[i]=(unsigned char)message[i];
    text[i]=0;
    display_error_text_when_main_menu_loaded(text);
    return 0;
}

/* port: a client's leaving clears only its own binding. This machine may
still host (its lobby keeps the server while the menus make a new client, a
dedicated server's next game): the host's identity goes with its server
(mcc_identity_host_clear), and so does a check its lobby waits for (#1129) */
void mcc_identity_disconnect(void)
{
    if (!global_network_game_server_get()) mcc_validation_cancel();
    mcc_cache_validation_clear();
    memset(&mcc_client_identity,0,sizeof(mcc_client_identity));
    mcc_cache_precache_clear();
}

void mcc_identity_host_clear(void)
{
    memset(&mcc_host_identity,0,sizeof(mcc_host_identity));
}

char const *mcc_identity_local_name(char const *name)
{
    char canonical[64];
    if (mcc_client_identity.session[0] && mcc_package_name(name,canonical) &&
        !csstrcasecmp(canonical,mcc_client_identity.session)) return mcc_client_identity.local;
    return name;
}

static unsigned char const *mcc_identity_expected(char const *name)
{
    char canonical[64];
    if (!mcc_package_name(name,canonical)) return NULL;
    if (mcc_client_identity.session[0] &&
        (!csstrcasecmp(canonical,mcc_client_identity.session) ||
         !csstrcasecmp(canonical,mcc_client_identity.local))) return mcc_client_identity.digest;
    return NULL;
}

static int mcc_identity_host_current(char const *name,struct mcc_precache_snapshot const *files)
{
    return mcc_host_identity.valid && !strcmp(name,mcc_host_identity.name) &&
        !memcmp(files,&mcc_host_identity.files,sizeof(*files));
}

static void mcc_identity_host_set(char const *name,unsigned char const digest[32],
    struct mcc_precache_snapshot const *files)
{
    strcpy(mcc_host_identity.name,name);
    memcpy(mcc_host_identity.digest,digest,32);
    mcc_host_identity.files=*files;
    mcc_workshop_local_item(name,&mcc_host_identity.item);
    mcc_host_identity.valid=1;
}

/* Prepare alongside the map version, before sending its settings. Initial
 * MCC host setup does this before opening the lobby; mcc_ui_network defers
 * early map changes until the host's loopback client has joined. */
int mcc_identity_prepare_host(char const *map)
{
    struct mcc_precache_snapshot before,after;
    char name[64];
    unsigned char digest[32];
    if (!mcc_package_name(map,name) || !mcc_precache_snapshot(name,&before)) {
        mcc_host_identity.valid=0;
        return 0;
    }
    if (!mcc_identity_host_current(name,&before)) {
        mcc_host_identity.valid=0;
        if (mcc_validation_request(name,NULL,digest)!=1) return 0;
        if (!mcc_precache_snapshot(name,&after) ||
            memcmp(&before,&after,sizeof(before))) {
            mcc_identity_error("Couldn't check this MCC map: its files changed while it was checked. Try again once it has finished installing.");
            return 0;
        }
        mcc_identity_host_set(name,digest,&after);
    }
    return 1;
}

/* port: whether the host's settings for this map may go out (#1129): any
 * other map's always; an MCC map's once its identity is prepared, taken here
 * from a finished check of its package (never starting one: the lobby's
 * update does, mcc_ui_network_update_map). Until then every machine's
 * record is held, not sent with an all-zero digest that every joiner,
 * the host's own client too, refuses */
int mcc_identity_host_ready(char const *map)
{
    struct mcc_precache_snapshot files;
    char name[64];
    unsigned char digest[32];
    static char held[64];
    if (!map || !mcc_level_name(map)) return 1;
    if (mcc_package_name(map,name) && mcc_precache_snapshot(name,&files)) {
        if (mcc_identity_host_current(name,&files)) {held[0]=0;return 1;}
        if (mcc_validation_lookup(name,NULL,digest)) {
            mcc_identity_host_set(name,digest,&files);
            held[0]=0;
            return 1;
        }
    }
    if (strncmp(held,map,sizeof(held)-1)) {
        strncpy(held,map,sizeof(held)-1);
        held[sizeof(held)-1]=0;
        error(_error_silent,"mcc identity: holding the game settings until %s is checked",map);
    }
    return 0;
}

/* port: the host's MCC part of a 43-layout settings record (13,200 bytes:
docs/delta.md's chupa-43a), over the bytes of it at offset (length of them)
in data: the map name's Workshop tail (bytes 64 to 127 of the name, at
name_offset), and the two digests after the local data, its last 64 bytes:
the content's (chester's fingerprint), then the package metadata's (zero: not
made here yet). Nothing for another map, record or a host not prepared */
void mcc_identity_host_overlay(char const *map, long record_size, long name_offset, long offset, long length,
    unsigned char *data)
{
    char canonical[64], name[128];
    long index;
    if (record_size != 13200 || !mcc_level_name(map) || !mcc_host_identity.valid ||
        !mcc_package_name(map,canonical) || strcmp(canonical,mcc_host_identity.name)) return;
    memset(name,0,sizeof(name));
    strncpy(name,map,63);
    mcc_workshop_wire_pack(name,&mcc_host_identity.item);
    for (index=0;index<length;index++) {
        long at=offset+index;
        if (at>=name_offset+64 && at<name_offset+128) data[index]=(unsigned char)name[at-name_offset];
        else if (at>=record_size-64 && at<record_size-32) data[index]=mcc_host_identity.digest[at-(record_size-64)];
        else if (at>=record_size-32 && at<record_size) data[index]=0;
    }
}

static int mcc_identity_matches(char const *name,unsigned char const digest[32],
    struct mcc_precache_snapshot *validated)
{
    struct mcc_precache_snapshot before,after;
    unsigned char actual[32];
    int ok;
    if (!mcc_precache_snapshot(name,&before)) return 0;
    /* The host's loopback client admits the very package just fingerprinted
     * for these settings. Rehashing it here stalls remote join requests.
     * Reuse only the exact identity and unchanged complete file snapshot;
     * actual loading still fingerprints the opened content independently. */
    if (mcc_host_identity.valid && !strcmp(name,mcc_host_identity.name) &&
        !memcmp(digest,mcc_host_identity.digest,32) &&
        !memcmp(&before,&mcc_host_identity.files,sizeof(before))) {
        if (validated) *validated=before;
        return 1;
    }
    ok=mcc_content_probe(name,actual) &&
        !memcmp(actual,digest,32) && mcc_precache_snapshot(name,&after) &&
        !memcmp(&before,&after,sizeof(before));
    if (ok && validated) *validated=after;
    return ok;
}

/* port: the host's MCC content fingerprint travels in its game settings,
which our wire does not change: with HALO_MCC_NETWORK only (mcc_network.c) */
#ifdef HALO_MCC_NETWORK
void const *mcc_identity_host_settings(void const *settings,long bytes,struct network_game *copy)
{
    struct network_game const *game=settings;
    struct mcc_precache_snapshot files;
    char canonical[64];
    uint32_t checksum;
    if (!game || bytes!=sizeof(*game)) return NULL;
    memcpy(copy,game,sizeof(*copy));
    memset(copy->mcc_content_sha256,0,32);
    if (!mcc_level_name(game->map.name)) {
        mcc_host_identity.valid=0;
        return copy;
    }
    /* Recheck every resource candidate before reusing the prepared digest.
     * A changed installation still requires fresh validation. */
    /* Sending or tearing down a session must never initiate package work.
     * Only pregame preparation may establish the host's identity. */
    if (!mcc_host_identity.valid || !mcc_package_name(game->map.name,canonical) ||
        strcmp(canonical,mcc_host_identity.name) || !mcc_precache_snapshot(canonical,&files) ||
        memcmp(&files,&mcc_host_identity.files,sizeof(files)) ||
        !mcc_validation_lookup(canonical,&checksum,NULL) ||
        checksum!=(uint32_t)game->map.version) return NULL;
    memcpy(copy->mcc_content_sha256,mcc_host_identity.digest,32);
    /* MCC protocol 64 reserves the name tail for advisory Workshop metadata. */
    mcc_workshop_wire_pack(copy->map.name,&mcc_host_identity.item);
    return copy;
}
#endif

/* port: the host's content digest, as its settings carry it: our build's own
record (HALO_MCC_NETWORK), or the 43-layout record's (13,200 bytes), the first
of the 64 bytes that end it (mcc_identity_host_overlay); zero for none */
static unsigned char mcc_wire_digest[32];

void mcc_identity_wire_record(unsigned char const *record, long size)
{
    if (record && size == 13200) memcpy(mcc_wire_digest,record+size-64,32);
    else memset(mcc_wire_digest,0,32);
}

static unsigned char const *mcc_identity_session_digest(struct network_game const *game)
{
#ifdef HALO_MCC_NETWORK
    return game->mcc_content_sha256;
#else
    (void)game;
    return mcc_wire_digest;
#endif
}


/* Only a known item ID establishes presence. Its older package may not yet
 * contain the requested level, so search independently of the filename. */
static int mcc_identity_refusal(struct network_game const *game,char const *session)
{
    struct mcc_workshop_item host,local;
    enum mcc_workshop_refusal kind=MCC_WORKSHOP_MISSING;
    char const *leaf=strrchr(session,'\\')+1,*candidate;
    char text[256];
    short i,count=mcc_maps_count(FALSE);
    if (!mcc_workshop_wire_unpack(game->map.name,&host)) kind=MCC_WORKSHOP_NOT_WORKSHOP;
    else for (i=0;i<count;i++) {
        candidate=mcc_maps_level_name(i);
        if (mcc_workshop_local_item(candidate,&local) && !strcmp(local.id,host.id)) {
            kind=MCC_WORKSHOP_DIFFERS;
            break;
        }
    }
    mcc_workshop_refusal_text(kind,&host,leaf,text,sizeof(text));
#if defined(HALO_GAME_BROWSER) && !defined(HALO_SERVER)
    /* port: a Workshop item this player lacks: offered over the main menu,
     * through their own Steam, then the game joined again (browser_screen.c) */
    if (kind==MCC_WORKSHOP_MISSING) {
        void browser_screen_workshop_missing(char const *id, char const *title);
        browser_screen_workshop_missing(host.id,host.title);
    }
#endif
    return mcc_identity_error(text);
}

int mcc_identity_accept(struct network_game const *game)
{
    char session[64],selected[64];
    char const *leaf,*candidate,*candidate_leaf;
    unsigned char zero[32]={0};
    short i,count;
    unsigned candidates=0,matches=0;
    struct mcc_precache_snapshot selected_files;
    unsigned char const *digest=mcc_identity_session_digest(game);
    memset(&mcc_client_identity,0,sizeof(mcc_client_identity));
    mcc_cache_precache_clear();
#ifdef HALO_MCC_NETWORK
    if (!mcc_level_name(game->map.name)) return !memcmp(digest,zero,32);
#else
    /* (a legacy map's record is the host's: its last bytes are not ours to judge) */
    if (!mcc_level_name(game->map.name)) return 1;
#endif
    if (!mcc_package_name(game->map.name,session) || !memcmp(digest,zero,32))
        return mcc_identity_error("Couldn't check this game's MCC map. The host may need to restart the game.");
    /* A verified exact installation is unambiguous even with duplicate copies. */
    if (mcc_identity_matches(session,digest,&selected_files)) {
        strcpy(selected,session);
        matches=1;
    } else {
        leaf=strrchr(session,'\\')+1;
        mcc_maps_rescan();
        count=mcc_maps_count(FALSE);
        /* Discovery may have made the exact package available since the first
         * probe. Prefer it before looking for aliases with the same leaf. */
        if (mcc_identity_matches(session,digest,&selected_files)) {
            strcpy(selected,session);
            matches=1;
        }
        if (!matches) {
            /* Bound work before hashing any fallback candidates. */
            for (i=0;i<count;i++) {
                candidate=mcc_maps_level_name(i);
                candidate_leaf=strrchr(candidate,'\\')+1;
                if (!csstrcasecmp(leaf,candidate_leaf) && ++candidates>MCC_IDENTITY_MAX_CANDIDATES)
                    return mcc_identity_error("Too many MCC maps share this filename. Keep one matching package installed and retry.");
            }
            for (i=0;i<count;i++) {
                candidate=mcc_maps_level_name(i);
                candidate_leaf=strrchr(candidate,'\\')+1;
                if (csstrcasecmp(leaf,candidate_leaf) || !csstrcasecmp(candidate,session)) continue;
                if (mcc_identity_matches(candidate,digest,&selected_files)) {
                    strcpy(selected,candidate);
                    if (++matches>1) return mcc_identity_error("Several local MCC packages match the host. Keep one matching copy, or use the host's package folder name.");
                }
            }
        }
    }
    if (!matches) return mcc_identity_refusal(game,session);
    mcc_client_identity.files=selected_files;
    strcpy(mcc_client_identity.session,session);
    strcpy(mcc_client_identity.local,selected);
    memcpy(mcc_client_identity.digest,digest,32);
    error(_error_silent,"mcc identity: %s resolved to %s (SHA-256 matched)",session,selected);
    return 1;
}


int mcc_identity_check(char const *name)
{
    unsigned char const *expected=mcc_identity_expected(name);
    struct mcc_precache_snapshot files;
    if (!expected) return 1;
    if (!mcc_precache_snapshot(mcc_client_identity.local,&files)) return 0;
    if (!memcmp(&files,&mcc_client_identity.files,sizeof(files))) return 1;
    return mcc_identity_matches(mcc_client_identity.local,expected,NULL);
}

int mcc_identity_loaded(char const *name,struct mcc_runtime *runtime)
{
    unsigned char actual[32];
    unsigned char const *expected=mcc_identity_expected(name);
    if (!expected) return game_connection()!=_game_connection_network_client &&
        game_connection()!=_game_connection_network_server;
    return mcc_resources_fingerprint(runtime,actual) && !memcmp(actual,expected,32);
}
#endif /* HALO_MCC */
