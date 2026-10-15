/* The high-res HUD's PNGs decoded ahead, on a thread of their own, as a map
   loads (hud_hires_map_loaded), so the HUD's first frame only uploads them;
   what is decoded and not drawn is let go when the map is unloaded. The same
   scheme as the OpenGL renderer's (port/linux/src/hud_hires.c), its state
   kept apart from the decoder so a native test can run it under
   ThreadSanitizer and AddressSanitizer (tools/test_metal_hud_preload.py). */
#ifndef HALO_METAL_HUD_PRELOAD_H
#define HALO_METAL_HUD_PRELOAD_H
#include <pthread.h>
#include <stdlib.h>

enum
{
    HUD_PRELOAD_NONE,
    HUD_PRELOAD_QUEUED,
    HUD_PRELOAD_DECODING,
    HUD_PRELOAD_DONE,
};

struct hud_preload_slot
{
    int state;
    unsigned char *pixels;
    unsigned long width, height;
};

/* decodes asset's picture (malloc'd, or NULL), its size in *width, *height */
typedef unsigned char *(*hud_preload_decoder)(long asset, unsigned long *width, unsigned long *height, void *context);

struct hud_preload
{
    pthread_mutex_t lock;
    pthread_cond_t finished;
    int running, stopping;
    struct hud_preload_slot *slots;
    long count;
    hud_preload_decoder decode;
    void *context;
};

#define HUD_PRELOAD_INITIALIZER(slots, count, decode, context) \
    { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, (slots), (count), (decode), (context) }

static void *hud_preload_thread(void *argument)
{
    struct hud_preload *preload = argument;
    long asset;

    for (asset = 0; asset < preload->count; asset++)
    {
        struct hud_preload_slot *slot = &preload->slots[asset];
        unsigned long width = 0, height = 0;
        unsigned char *pixels;

        pthread_mutex_lock(&preload->lock);
        if (preload->stopping)
        {
            pthread_mutex_unlock(&preload->lock);
            break;
        }
        if (slot->state != HUD_PRELOAD_QUEUED)
        {
            pthread_mutex_unlock(&preload->lock);
            continue;
        }
        slot->state = HUD_PRELOAD_DECODING;
        pthread_mutex_unlock(&preload->lock);
        pixels = preload->decode(asset, &width, &height, preload->context);
        pthread_mutex_lock(&preload->lock);
        slot->pixels = pixels;
        slot->width = width;
        slot->height = height;
        slot->state = HUD_PRELOAD_DONE;
        pthread_cond_broadcast(&preload->finished);
        pthread_mutex_unlock(&preload->lock);
    }
    pthread_mutex_lock(&preload->lock);
    preload->running = 0;
    pthread_cond_broadcast(&preload->finished);
    pthread_mutex_unlock(&preload->lock);
    return NULL;
}

/* stops the thread (waiting for the picture in hand) and frees what it
   decoded that was not taken */
static inline void hud_preload_stop(struct hud_preload *preload)
{
    long asset;

    pthread_mutex_lock(&preload->lock);
    preload->stopping = 1;
    while (preload->running)
        pthread_cond_wait(&preload->finished, &preload->lock);
    for (asset = 0; asset < preload->count; asset++)
    {
        free(preload->slots[asset].pixels);
        preload->slots[asset].pixels = NULL;
        preload->slots[asset].state = HUD_PRELOAD_NONE;
    }
    pthread_mutex_unlock(&preload->lock);
}

/* queues the assets (those in range, and wanted when wanted is given) and
   starts decoding them; returns how many were queued */
static inline long hud_preload_start(struct hud_preload *preload, const long *assets, long count,
    int (*wanted)(long asset, void *context))
{
    long index, queued = 0;
    pthread_t thread;

    hud_preload_stop(preload);
    pthread_mutex_lock(&preload->lock);
    for (index = 0; index < count; index++)
    {
        long asset = assets[index];

        if (asset >= 0 && asset < preload->count && preload->slots[asset].state == HUD_PRELOAD_NONE &&
            (!wanted || wanted(asset, preload->context)))
        {
            preload->slots[asset].state = HUD_PRELOAD_QUEUED;
            queued++;
        }
    }
    if (queued)
    {
        preload->stopping = 0;
        preload->running = 1;
        if (pthread_create(&thread, NULL, hud_preload_thread, preload) == 0)
            pthread_detach(thread);
        else
        {
            preload->running = 0;
            for (index = 0; index < preload->count; index++)
                preload->slots[index].state = HUD_PRELOAD_NONE;
            queued = 0;
        }
    }
    pthread_mutex_unlock(&preload->lock);
    return queued;
}

/* asset's decoded picture, waiting for it while it is being decoded, now
   the caller's (returns 1); 0 when it was not decoded ahead (the caller
   decodes it itself) */
static inline int hud_preload_take(struct hud_preload *preload, long asset, unsigned char **pixels,
    unsigned long *width, unsigned long *height)
{
    int taken = 0;

    if (asset < 0 || asset >= preload->count)
        return 0;
    pthread_mutex_lock(&preload->lock);
    while (preload->slots[asset].state == HUD_PRELOAD_DECODING ||
        (preload->slots[asset].state == HUD_PRELOAD_QUEUED && preload->running))
        pthread_cond_wait(&preload->finished, &preload->lock);
    if (preload->slots[asset].state == HUD_PRELOAD_DONE)
    {
        *pixels = preload->slots[asset].pixels;
        *width = preload->slots[asset].width;
        *height = preload->slots[asset].height;
        preload->slots[asset].pixels = NULL;
        taken = 1;
    }
    preload->slots[asset].state = HUD_PRELOAD_NONE;
    pthread_mutex_unlock(&preload->lock);
    return taken;
}
#endif
