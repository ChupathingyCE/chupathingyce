/* Utility scoring and selection: response curves, weighted inputs, and a
 * selector with a decision cadence, hysteresis and a switching margin. Scores
 * and policy are game-owned; this module only ranks them. No heap, no libm. */
#ifndef ENGINE_AI_UTILITY_H
#define ENGINE_AI_UTILITY_H

#include <stdbool.h>
#include <stdint.h>

enum {
    ENGINE_AI_UTILITY_OPTIONS_MAX = 32,
    ENGINE_AI_UTILITY_POWER_MAX = 8,
    ENGINE_AI_UTILITY_NONE = ENGINE_AI_UTILITY_OPTIONS_MAX
};

/* ---------- responses */

typedef enum engine_ai_utility_curve_kind {
    ENGINE_AI_UTILITY_CONSTANT = 0, /* y0 for every input */
    ENGINE_AI_UTILITY_LINEAR,       /* t */
    ENGINE_AI_UTILITY_POWER,        /* t^power: slow start, fast finish */
    ENGINE_AI_UTILITY_INVERSE_POWER,/* 1 - (1 - t)^power: fast start, slow finish */
    ENGINE_AI_UTILITY_SMOOTHSTEP,   /* 3t^2 - 2t^3: an S between the ends */
    ENGINE_AI_UTILITY_STEP          /* 0 below t = 0.5, 1 from it */
} engine_ai_utility_curve_kind;

/* The input is mapped to t in [0, 1] between x0 and x1 and clamped; x1 may be
 * below x0 for a response that rises as the input falls (distance 50 to 0).
 * The output is y0 + (y1 - y0) * shape(t). power is used only by the POWER
 * kinds, 1..POWER_MAX. A curve with x0 == x1 takes t = 1 at or beyond x0
 * (in the x1 direction, which is upward) and 0 below. */
typedef struct engine_ai_utility_curve {
    engine_ai_utility_curve_kind kind;
    float x0;
    float x1;
    float y0;
    float y1;
    uint32_t power;
} engine_ai_utility_curve;

/* False for an unknown kind, a power out of range, or non-finite fields. */
bool engine_ai_utility_curve_valid(const engine_ai_utility_curve *curve);

/* The response; 0 for an invalid curve or a non-finite input. */
float engine_ai_utility_curve_evaluate(const engine_ai_utility_curve *curve, float x);

/* The weighted mean of count inputs, each clamped to [0, 1]; negative or
 * non-finite weights count as zero. 0 when no weight remains or count is 0.
 * count is at most OPTIONS_MAX. */
float engine_ai_utility_weighted(const float *values, const float *weights, uint32_t count);

/* ---------- selection */

/* option_count: 1..OPTIONS_MAX.
 * decision_interval: ticks between decisions, at least 1; the running option
 *   is kept between them unless its score falls to zero or the caller forces.
 * hysteresis_bonus, hysteresis_ticks: the running option's score is raised by
 *   the bonus as it is chosen, fading linearly to nothing over that many ticks
 *   (zero ticks: no bonus).
 * switch_margin: even after the bonus has faded, a challenger must beat the
 *   running option's score by more than this. */
typedef struct engine_ai_utility_selector_config {
    uint32_t option_count;
    uint32_t decision_interval;
    uint32_t hysteresis_ticks;
    float hysteresis_bonus;
    float switch_margin;
} engine_ai_utility_selector_config;

/* Caller-owned, pointer-free; snapshot its fields explicitly. */
typedef struct engine_ai_utility_selector {
    uint32_t current;            /* NONE: nothing running */
    uint32_t initialized;
    uint64_t chosen_tick;
    uint64_t next_decision_tick;
} engine_ai_utility_selector;

typedef enum engine_ai_utility_decision {
    ENGINE_AI_UTILITY_INVALID = 0,  /* bad arguments or a non-finite score */
    ENGINE_AI_UTILITY_WAITING,      /* not due: the running option continues */
    ENGINE_AI_UTILITY_KEPT,         /* decided: the running option stays */
    ENGINE_AI_UTILITY_CHANGED,      /* decided: another option, or the first */
    ENGINE_AI_UTILITY_IDLE          /* decided: no option scores above zero */
} engine_ai_utility_decision;

bool engine_ai_utility_config_valid(const engine_ai_utility_selector_config *config);

/* Nothing running; the first decision is due at first_decision_tick (use it to
 * stagger agents so their decisions spread across ticks). */
void engine_ai_utility_selector_init(
    engine_ai_utility_selector *selector, uint64_t first_decision_tick);

/* The running option's current bonus; 0 with nothing running. */
float engine_ai_utility_hysteresis(
    const engine_ai_utility_selector *selector,
    const engine_ai_utility_selector_config *config, uint64_t tick);

/* Scores are config->option_count finite values; zero or less means the
 * option cannot run. A decision is made when it is due, when force is set,
 * when nothing runs, when the running option's score is zero or less, or when
 * tick is earlier than the last choice (a restored state). Ties go to the
 * lower index; the running option wins ties with challengers. *chosen is
 * written for every result except INVALID (NONE when IDLE). A decision
 * schedules the next one decision_interval ticks later. */
engine_ai_utility_decision engine_ai_utility_select(
    engine_ai_utility_selector *selector,
    const engine_ai_utility_selector_config *config,
    const float *scores, uint64_t tick, bool force, uint32_t *chosen);

#endif
