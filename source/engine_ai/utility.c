#include "utility.h"

#include <stddef.h>

static bool utility_finite(float value)
{
    /* (NaN fails the first, the infinities the second) */
    return value == value && value - value == 0.f;
}

static float utility_clamp01(float value)
{
    if (!(value > 0.f))
        return 0.f;
    if (value > 1.f)
        return 1.f;
    return value;
}

bool engine_ai_utility_curve_valid(const engine_ai_utility_curve *curve)
{
    if (curve == NULL)
        return false;
    if (!utility_finite(curve->x0) || !utility_finite(curve->x1) ||
        !utility_finite(curve->y0) || !utility_finite(curve->y1))
        return false;
    switch (curve->kind) {
    case ENGINE_AI_UTILITY_CONSTANT:
    case ENGINE_AI_UTILITY_LINEAR:
    case ENGINE_AI_UTILITY_SMOOTHSTEP:
    case ENGINE_AI_UTILITY_STEP:
        return true;
    case ENGINE_AI_UTILITY_POWER:
    case ENGINE_AI_UTILITY_INVERSE_POWER:
        return curve->power >= 1 && curve->power <= ENGINE_AI_UTILITY_POWER_MAX;
    }
    return false;
}

static float utility_power(float t, uint32_t power)
{
    float result = 1.f;
    uint32_t index;

    for (index = 0; index < power; index++)
        result *= t;
    return result;
}

float engine_ai_utility_curve_evaluate(const engine_ai_utility_curve *curve, float x)
{
    float t;
    float shape;

    if (!engine_ai_utility_curve_valid(curve) || !utility_finite(x))
        return 0.f;
    if (curve->kind == ENGINE_AI_UTILITY_CONSTANT)
        return curve->y0;
    if (curve->x1 == curve->x0)
        t = x >= curve->x0 ? 1.f : 0.f;
    else
        t = utility_clamp01((x - curve->x0) / (curve->x1 - curve->x0));
    switch (curve->kind) {
    case ENGINE_AI_UTILITY_LINEAR:
        shape = t;
        break;
    case ENGINE_AI_UTILITY_POWER:
        shape = utility_power(t, curve->power);
        break;
    case ENGINE_AI_UTILITY_INVERSE_POWER:
        shape = 1.f - utility_power(1.f - t, curve->power);
        break;
    case ENGINE_AI_UTILITY_SMOOTHSTEP:
        shape = t * t * (3.f - 2.f * t);
        break;
    case ENGINE_AI_UTILITY_STEP:
        shape = t >= 0.5f ? 1.f : 0.f;
        break;
    default:
        return 0.f;
    }
    return curve->y0 + (curve->y1 - curve->y0) * shape;
}

float engine_ai_utility_weighted(const float *values, const float *weights, uint32_t count)
{
    float total = 0.f;
    float weight_total = 0.f;
    uint32_t index;

    if (values == NULL || weights == NULL || count == 0 || count > ENGINE_AI_UTILITY_OPTIONS_MAX)
        return 0.f;
    for (index = 0; index < count; index++) {
        float weight = weights[index];

        if (!utility_finite(weight) || weight <= 0.f)
            continue;
        total += utility_clamp01(utility_finite(values[index]) ? values[index] : 0.f) * weight;
        weight_total += weight;
    }
    if (weight_total <= 0.f)
        return 0.f;
    return utility_clamp01(total / weight_total);
}

bool engine_ai_utility_config_valid(const engine_ai_utility_selector_config *config)
{
    return config != NULL &&
        config->option_count >= 1 && config->option_count <= ENGINE_AI_UTILITY_OPTIONS_MAX &&
        config->decision_interval >= 1 &&
        utility_finite(config->hysteresis_bonus) && config->hysteresis_bonus >= 0.f &&
        utility_finite(config->switch_margin) && config->switch_margin >= 0.f;
}

void engine_ai_utility_selector_init(
    engine_ai_utility_selector *selector, uint64_t first_decision_tick)
{
    if (selector == NULL)
        return;
    selector->current = ENGINE_AI_UTILITY_NONE;
    selector->initialized = 1;
    selector->chosen_tick = 0;
    selector->next_decision_tick = first_decision_tick;
}

float engine_ai_utility_hysteresis(
    const engine_ai_utility_selector *selector,
    const engine_ai_utility_selector_config *config, uint64_t tick)
{
    uint64_t elapsed;

    if (selector == NULL || !engine_ai_utility_config_valid(config) || !selector->initialized ||
        selector->current >= config->option_count || config->hysteresis_ticks == 0 ||
        tick < selector->chosen_tick)
        return 0.f;
    elapsed = tick - selector->chosen_tick;
    if (elapsed >= config->hysteresis_ticks)
        return 0.f;
    return config->hysteresis_bonus *
        (1.f - (float)elapsed / (float)config->hysteresis_ticks);
}

engine_ai_utility_decision engine_ai_utility_select(
    engine_ai_utility_selector *selector,
    const engine_ai_utility_selector_config *config,
    const float *scores, uint64_t tick, bool force, uint32_t *chosen)
{
    uint32_t index;
    uint32_t current;
    uint32_t best = ENGINE_AI_UTILITY_NONE;
    float best_score = 0.f;
    bool current_runs;
    bool restored;

    if (selector == NULL || !selector->initialized || !engine_ai_utility_config_valid(config) ||
        scores == NULL || chosen == NULL)
        return ENGINE_AI_UTILITY_INVALID;
    for (index = 0; index < config->option_count; index++) {
        if (!utility_finite(scores[index]))
            return ENGINE_AI_UTILITY_INVALID;
    }
    current = selector->current < config->option_count ? selector->current : ENGINE_AI_UTILITY_NONE;
    current_runs = current != ENGINE_AI_UTILITY_NONE && scores[current] > 0.f;
    restored = current != ENGINE_AI_UTILITY_NONE && tick < selector->chosen_tick;
    if (current_runs && !force && !restored && tick < selector->next_decision_tick) {
        *chosen = current;
        return ENGINE_AI_UTILITY_WAITING;
    }

    selector->next_decision_tick = tick + config->decision_interval;
    if (restored)
        selector->chosen_tick = tick;
    for (index = 0; index < config->option_count; index++) {
        if (index == current || scores[index] <= 0.f)
            continue;
        if (best == ENGINE_AI_UTILITY_NONE || scores[index] > best_score) {
            best = index;
            best_score = scores[index];
        }
    }
    if (current_runs) {
        float defended = scores[current] +
            engine_ai_utility_hysteresis(selector, config, tick) + config->switch_margin;

        if (best == ENGINE_AI_UTILITY_NONE || best_score <= defended) {
            *chosen = current;
            return ENGINE_AI_UTILITY_KEPT;
        }
    }
    if (best == ENGINE_AI_UTILITY_NONE) {
        selector->current = ENGINE_AI_UTILITY_NONE;
        *chosen = ENGINE_AI_UTILITY_NONE;
        return ENGINE_AI_UTILITY_IDLE;
    }
    selector->current = best;
    selector->chosen_tick = tick;
    *chosen = best;
    return ENGINE_AI_UTILITY_CHANGED;
}
