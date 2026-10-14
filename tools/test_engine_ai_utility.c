#include "engine_ai/utility.h"

#include <assert.h>
#include <stdio.h>

static int near(float a, float b)
{
    float d = a - b;
    return d < 1e-5f && d > -1e-5f;
}

static void test_curves(void)
{
    engine_ai_utility_curve curve = {ENGINE_AI_UTILITY_LINEAR, 50.f, 0.f, 0.f, 1.f, 0};
    float nan_value = 0.f / 0.f;

    /* Distance 50 -> 0 m raises the desire to fight (slide 26). */
    assert(near(engine_ai_utility_curve_evaluate(&curve, 50.f), 0.f));
    assert(near(engine_ai_utility_curve_evaluate(&curve, 25.f), 0.5f));
    assert(near(engine_ai_utility_curve_evaluate(&curve, 0.f), 1.f));
    assert(near(engine_ai_utility_curve_evaluate(&curve, 80.f), 0.f));
    assert(near(engine_ai_utility_curve_evaluate(&curve, -3.f), 1.f));
    assert(near(engine_ai_utility_curve_evaluate(&curve, nan_value), 0.f));

    curve.kind = ENGINE_AI_UTILITY_POWER;
    curve.x0 = 1.f; curve.x1 = 0.f; curve.power = 2;
    assert(near(engine_ai_utility_curve_evaluate(&curve, 0.5f), 0.25f));
    curve.kind = ENGINE_AI_UTILITY_INVERSE_POWER;
    assert(near(engine_ai_utility_curve_evaluate(&curve, 0.5f), 0.75f));
    curve.power = 0;
    assert(!engine_ai_utility_curve_valid(&curve));
    assert(near(engine_ai_utility_curve_evaluate(&curve, 0.5f), 0.f));
    curve.power = ENGINE_AI_UTILITY_POWER_MAX + 1;
    assert(!engine_ai_utility_curve_valid(&curve));

    curve.kind = ENGINE_AI_UTILITY_SMOOTHSTEP;
    curve.x0 = 0.f; curve.x1 = 1.f; curve.y0 = 0.2f; curve.y1 = 0.6f;
    assert(near(engine_ai_utility_curve_evaluate(&curve, 0.5f), 0.4f));
    curve.kind = ENGINE_AI_UTILITY_STEP;
    assert(near(engine_ai_utility_curve_evaluate(&curve, 0.49f), 0.2f));
    assert(near(engine_ai_utility_curve_evaluate(&curve, 0.5f), 0.6f));
    curve.kind = ENGINE_AI_UTILITY_CONSTANT;
    assert(near(engine_ai_utility_curve_evaluate(&curve, 123.f), 0.2f));
    curve.kind = ENGINE_AI_UTILITY_LINEAR;
    curve.x1 = 0.f;
    assert(near(engine_ai_utility_curve_evaluate(&curve, -1.f), 0.2f));
    assert(near(engine_ai_utility_curve_evaluate(&curve, 0.f), 0.6f));
    curve.y0 = nan_value;
    assert(!engine_ai_utility_curve_valid(&curve));
    curve.y0 = 0.f;
    curve.kind = (engine_ai_utility_curve_kind)99;
    assert(!engine_ai_utility_curve_valid(&curve));
    assert(!engine_ai_utility_curve_valid(NULL));
}

static void test_weighted(void)
{
    float values[] = {1.f, 0.f, 0.5f, 7.f};
    float weights[] = {3.f, 1.f, 0.f, -2.f};

    assert(near(engine_ai_utility_weighted(values, weights, 4), 0.75f));
    weights[0] = 0.f; weights[1] = 0.f;
    assert(near(engine_ai_utility_weighted(values, weights, 4), 0.f));
    weights[3] = 1.f; /* clamped input */
    assert(near(engine_ai_utility_weighted(values, weights, 4), 1.f));
    assert(near(engine_ai_utility_weighted(values, weights, 0), 0.f));
    assert(near(engine_ai_utility_weighted(NULL, weights, 2), 0.f));
}

static void test_selection(void)
{
    engine_ai_utility_selector_config config = {6, 8, 30, 0.2f, 0.05f};
    engine_ai_utility_selector selector;
    /* Pickup, Deliver, Fight, Interact, Guard, Traversal (slide 23). */
    float scores[6] = {0.f, 0.6f, 0.91f, 0.f, 0.f, 0.1f};
    uint32_t chosen = 99;

    assert(engine_ai_utility_config_valid(&config));
    engine_ai_utility_selector_init(&selector, 3);
    assert(engine_ai_utility_select(&selector, &config, scores, 0, false, &chosen) ==
        ENGINE_AI_UTILITY_CHANGED);
    assert(chosen == 2 && selector.next_decision_tick == 8);

    /* Between decisions, the running option continues. */
    scores[1] = 2.f;
    assert(engine_ai_utility_select(&selector, &config, scores, 4, false, &chosen) ==
        ENGINE_AI_UTILITY_WAITING);
    assert(chosen == 2);

    /* Hysteresis: at tick 8 the bonus is 0.2 * (1 - 8/30); a near rival loses. */
    scores[1] = 0.91f + 0.1f;
    assert(engine_ai_utility_select(&selector, &config, scores, 8, false, &chosen) ==
        ENGINE_AI_UTILITY_KEPT);
    assert(chosen == 2);
    /* Once the bonus has faded, the margin still defends... */
    scores[1] = 0.91f + 0.04f;
    assert(engine_ai_utility_select(&selector, &config, scores, 40, false, &chosen) ==
        ENGINE_AI_UTILITY_KEPT);
    /* ...but a clear winner takes over. */
    scores[1] = 0.91f + 0.06f;
    assert(engine_ai_utility_select(&selector, &config, scores, 48, false, &chosen) ==
        ENGINE_AI_UTILITY_CHANGED);
    assert(chosen == 1 && selector.chosen_tick == 48);
    assert(near(engine_ai_utility_hysteresis(&selector, &config, 48), 0.2f));
    assert(near(engine_ai_utility_hysteresis(&selector, &config, 63), 0.1f));
    assert(near(engine_ai_utility_hysteresis(&selector, &config, 78), 0.f));

    /* A running option that cannot run any more is replaced at once. */
    scores[1] = 0.f;
    assert(engine_ai_utility_select(&selector, &config, scores, 49, false, &chosen) ==
        ENGINE_AI_UTILITY_CHANGED);
    assert(chosen == 2);

    /* Forced decisions skip the cadence. */
    scores[5] = 5.f;
    assert(engine_ai_utility_select(&selector, &config, scores, 50, true, &chosen) ==
        ENGINE_AI_UTILITY_CHANGED);
    assert(chosen == 5);

    /* Ties go to the lower index; nothing above zero is idle. */
    {
        float tie[6] = {0.f, 0.5f, 0.5f, 0.f, 0.f, 0.f};
        float none[6] = {0.f, 0.f, -1.f, 0.f, 0.f, 0.f};

        engine_ai_utility_selector_init(&selector, 0);
        assert(engine_ai_utility_select(&selector, &config, tie, 0, false, &chosen) ==
            ENGINE_AI_UTILITY_CHANGED);
        assert(chosen == 1);
        assert(engine_ai_utility_select(&selector, &config, none, 1, false, &chosen) ==
            ENGINE_AI_UTILITY_IDLE);
        assert(chosen == ENGINE_AI_UTILITY_NONE && selector.current == ENGINE_AI_UTILITY_NONE);
        assert(near(engine_ai_utility_hysteresis(&selector, &config, 1), 0.f));
    }

    /* A tick earlier than the last choice (a restored state) decides again. */
    {
        float two[6] = {0.4f, 0.9f, 0.f, 0.f, 0.f, 0.f};

        engine_ai_utility_selector_init(&selector, 0);
        assert(engine_ai_utility_select(&selector, &config, two, 100, false, &chosen) ==
            ENGINE_AI_UTILITY_CHANGED);
        assert(engine_ai_utility_select(&selector, &config, two, 50, false, &chosen) ==
            ENGINE_AI_UTILITY_KEPT);
        assert(selector.chosen_tick == 50 && selector.next_decision_tick == 58);
    }
}

static void test_invalid(void)
{
    engine_ai_utility_selector_config config = {2, 1, 0, 0.f, 0.f};
    engine_ai_utility_selector selector = {0};
    float scores[2] = {0.5f, 0.2f};
    uint32_t chosen = 7;

    /* Uninitialized selectors are refused. */
    assert(engine_ai_utility_select(&selector, &config, scores, 0, false, &chosen) ==
        ENGINE_AI_UTILITY_INVALID);
    engine_ai_utility_selector_init(&selector, 0);
    scores[1] = 1.f / 0.f;
    assert(engine_ai_utility_select(&selector, &config, scores, 0, false, &chosen) ==
        ENGINE_AI_UTILITY_INVALID);
    assert(chosen == 7);
    scores[1] = 0.2f;
    config.option_count = 0;
    assert(!engine_ai_utility_config_valid(&config));
    config.option_count = ENGINE_AI_UTILITY_OPTIONS_MAX + 1;
    assert(!engine_ai_utility_config_valid(&config));
    config.option_count = 2;
    config.decision_interval = 0;
    assert(!engine_ai_utility_config_valid(&config));
    config.decision_interval = 1;
    config.switch_margin = -0.1f;
    assert(!engine_ai_utility_config_valid(&config));
    config.switch_margin = 0.f;
    assert(engine_ai_utility_select(&selector, &config, NULL, 0, false, &chosen) ==
        ENGINE_AI_UTILITY_INVALID);
    assert(engine_ai_utility_select(&selector, &config, scores, 0, false, NULL) ==
        ENGINE_AI_UTILITY_INVALID);
    assert(engine_ai_utility_select(&selector, &config, scores, 0, false, &chosen) ==
        ENGINE_AI_UTILITY_CHANGED);
    assert(chosen == 0);
}

int main(void)
{
    test_curves();
    test_weighted();
    test_selection();
    test_invalid();
    puts("engine_ai utility tests passed");
    return 0;
}
