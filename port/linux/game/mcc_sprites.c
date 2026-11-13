/* MCC effects may span more than eight bitmap pages. Submit full batches
 * through the normal renderer instead of dropping the remaining sprites. */
#ifndef HALO_MCC
#include "../include/halo_port_features.h"
#endif
#if !defined(HALO_MCC) || HALO_MCC /* Halo MCC maps (halo_port_features.h, above): a console builds this to nothing */
#include "cseries.h"
#include "mcc_cache.h"
#include "mcc_sprites.h"
#include "render/render_sprite.h"

int mcc_sprites_continue(struct build_sprite_data *data)
{
    short remaining;
    unsigned long flags;
    if (!mcc_cache_tags_loaded()) return FALSE;
    remaining = data->maximum_sprite_count - data->sprite_count;
    flags = data->flags & ((1u << NUMBER_OF_BUILD_SPRITES_FLAGS) - 1u);
    build_sprites_end(data);
    build_sprites_begin(data, remaining, data->bitmap_group_index, data->shader, flags);
    return TRUE;
}
#endif /* HALO_MCC */
