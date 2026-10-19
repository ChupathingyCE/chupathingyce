/* Provisional CE values. Infinite's Lua goal policy and numerical tag rows
 * are MISSING: do not interpret this table as recovered Infinite tuning. */
#include "bot_profile.h"

#include <stddef.h>
#include <math.h>
#include <string.h>

struct bot_skill const bot_skills[BOT_PROFILE_COUNT] =
{
    /* Existing CE aim/movement values; new ranges/memory are CE defaults. */
    { "recruit", 7.f, 200.f, 30.f, 55.f, 10.f, 0.6f, 0.8f,
        false, false, false, false, 16.f, 8.f, 1.2f, 3.f, 0.45f, false, 1.8f,
        {0.9f, 1.17f}, {0.9f, 1.17f}, 0.5f, 4.f, 0.15f, 1.f, 0.05f,
        { {1.f, 1.2f}, {8.f, 16.f}, 0.75f, 3.f, 1.5f, 3.f, 3.f,
            0.2f, 0.3f, 0.3f, 0.f, 8.f, 20.f, 5.f, false, false } },
    { "marine", 4.f, 320.f, 40.f, 65.f, 8.f, 1.f, 0.5f,
        true, true, false, false, 24.f, 12.f, 0.8f, 1.8f, 0.7f, false, 1.4f,
        {0.5f, 0.65f}, {0.5f, 0.65f}, 0.75f, 4.f, 0.15f, 1.f, 0.05f,
        { {1.f, 1.2f}, {8.f, 16.f}, 0.75f, 3.f, 1.5f, 3.f, 3.f,
            0.2f, 0.3f, 0.3f, 0.f, 8.f, 20.f, 5.f, false, false } },
    { "odst", 2.5f, 450.f, 50.f, 75.f, 6.f, 1.5f, 0.3f,
        true, true, true, false, 32.f, 16.f, 0.55f, 1.35f, 0.85f, false, 1.1f,
        {0.3f, 0.39f}, {0.3f, 0.39f}, 1.f, 4.f, 0.15f, 1.f, 0.05f,
        { {1.f, 1.2f}, {8.f, 16.f}, 0.75f, 3.f, 1.5f, 3.f, 3.f,
            0.2f, 0.3f, 0.3f, 0.35f, 8.f, 20.f, 5.f, true, false } },
    { "spartan", 1.5f, 720.f, 60.f, 85.f, 5.f, 1.2f, 0.45f,
        true, true, true, true, 40.f, 22.f, 0.35f, 1.15f, 1.f, true, 0.8f,
        {0.18f, 0.234f}, {0.18f, 0.234f}, 1.25f, 4.f, 0.15f, 1.f, 0.05f,
        { {1.f, 1.2f}, {8.f, 16.f}, 0.75f, 3.f, 1.5f, 3.f, 3.f,
            0.2f, 0.3f, 0.3f, 0.45f, 8.f, 20.f, 5.f, true, false } }
};

/* Missing Infinite rows are intentionally absent, not guessed by weapon name. */
static struct
{
    uint32_t count[BOT_PROFILE_COUNT];
    struct bot_weapon_attributes rows[BOT_PROFILE_COUNT][BOT_WEAPON_ATTRIBUTES_MAXIMUM];
} bot_weapon_tuning;

void bot_profile_reset_weapon_attributes(void)
{
    memset(&bot_weapon_tuning, 0, sizeof(bot_weapon_tuning));
}

static bool bot_profile_weapon_row_valid(struct bot_weapon_attributes const *row)
{
    uint32_t index;
    if (row->definition_key == UINT32_MAX || (row->flags & ~0x7fu)) {
        return false;
    }
    for (index = 0; index < BOT_WEAPON_ATTRIBUTE_VALUES; index++) {
        float maximum = index < BOT_WEAPON_RANGE_MIN ? 16.f : 1000.f;
        if (!isfinite(row->values[index]) || row->values[index] < 0.f ||
            row->values[index] > maximum) {
            return false;
        }
    }
    for (index = BOT_WEAPON_AIM_ERROR_SCALE; index <= BOT_WEAPON_MOVING_ERROR_SCALE; index++) {
        if (row->values[index] < 0.05f) {
            return false;
        }
    }
    return row->values[BOT_WEAPON_RANGE_MIN] <= row->values[BOT_WEAPON_RANGE_MAX];
}

bool bot_profile_set_weapon_attributes(uint32_t profile_id,
    struct bot_weapon_attributes const *rows, uint32_t count)
{
    uint32_t index;
    uint32_t previous;
    if (profile_id >= BOT_PROFILE_COUNT || count > BOT_WEAPON_ATTRIBUTES_MAXIMUM ||
        (count != 0 && rows == NULL)) {
        return false;
    }
    for (index = 0; index < count; index++) {
        if (!bot_profile_weapon_row_valid(&rows[index])) {
            return false;
        }
        for (previous = 0; previous < index; previous++) {
            if (rows[previous].definition_key == rows[index].definition_key) {
                return false;
            }
        }
    }
    for (index = 0; index < count; index++) {
        bot_weapon_tuning.rows[profile_id][index] = rows[index];
    }
    bot_weapon_tuning.count[profile_id] = count;
    return true;
}

float bot_profile_weapon_attribute(uint32_t profile_id, uint32_t definition_key,
    enum bot_weapon_attribute attribute)
{
    uint32_t index;
    if (attribute < 0 || attribute >= BOT_WEAPON_ATTRIBUTE_COUNT) {
        return 0.f;
    }
    if (profile_id < BOT_PROFILE_COUNT) {
        for (index = 0; index < bot_weapon_tuning.count[profile_id]; index++) {
            struct bot_weapon_attributes const *row = &bot_weapon_tuning.rows[profile_id][index];
            if (row->definition_key == definition_key) {
                if (attribute < BOT_WEAPON_ATTRIBUTE_VALUES) {
                    return row->values[attribute];
                }
                return (row->flags & (1u << (attribute - BOT_WEAPON_ATTRIBUTE_VALUES))) ? 1.f : 0.f;
            }
        }
    }
    if (attribute <= BOT_WEAPON_MOVING_ERROR_SCALE || attribute == BOT_WEAPON_CAN_SWAP) {
        return 1.f;
    }
    return 0.f;
}

struct bot_skill const *bot_profile_get(uint32_t id)
{
    return id < BOT_PROFILE_COUNT ? &bot_skills[id] : NULL;
}

uint32_t bot_profile_delay_ticks(struct bot_delay_range const *range,
    float sample, uint32_t ticks_per_second)
{
    double ticks;
    uint32_t rounded;
    if (range == NULL || ticks_per_second == 0 || !isfinite(sample) ||
        sample < 0.f || sample > 1.f || !isfinite(range->minimum_seconds) ||
        !isfinite(range->maximum_seconds) || range->minimum_seconds < 0.f ||
        range->maximum_seconds < range->minimum_seconds) {
        return 0;
    }
    ticks = ((double)range->minimum_seconds + (double)sample *
        ((double)range->maximum_seconds - (double)range->minimum_seconds)) * ticks_per_second;
    if (ticks >= UINT32_MAX) {
        return UINT32_MAX;
    }
    /* A tiny representation error at an integral tick must not add a tick. */
    rounded = (uint32_t)ticks;
    if (ticks - rounded > 0.00001) {
        rounded++;
    }
    return rounded;
}
