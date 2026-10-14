#include "engine_ai/aim.h"
#include "engine_ai/claims.h"

#include <assert.h>
#include <stdio.h>

static const struct engine_ai_aim_skill recruit = { 7.f, 1.f, 8.f, 1.2f, 3.f };
static const struct engine_ai_aim_skill spartan = { 1.5f, 0.6f, 22.f, 0.35f, 1.15f };

static void test_aim(void)
{
    float near_settled = engine_ai_aim_error_degrees(&spartan, 10.f, 5.f, false);
    float far_settled = engine_ai_aim_error_degrees(&spartan, 44.f, 5.f, false);
    float unsettled = engine_ai_aim_error_degrees(&spartan, 10.f, 0.f, false);
    float moving = engine_ai_aim_error_degrees(&spartan, 10.f, 5.f, true);

    assert(near_settled == 1.5f);
    assert(far_settled == 3.f);
    assert(unsettled == 4.5f);
    assert(moving > near_settled);
    /* distance scaling is capped */
    assert(engine_ai_aim_error_degrees(&spartan, 1000.f, 5.f, false) == 6.f);
    /* worst case for nonsense inputs */
    assert(engine_ai_aim_error_degrees(&spartan, -1.f, -1.f, false) == 1.5f * 3.f * 4.f);
    /* skills are ordered in every condition: recruits worse than spartans */
    assert(engine_ai_aim_error_degrees(&recruit, 10.f, 5.f, false) > near_settled);
    assert(engine_ai_aim_error_degrees(&recruit, 30.f, 0.5f, true) >
        engine_ai_aim_error_degrees(&spartan, 30.f, 0.5f, true));
    /* the recruit's moving penalty is the larger struggle */
    assert(engine_ai_aim_error_degrees(&recruit, 10.f, 5.f, true) /
        engine_ai_aim_error_degrees(&recruit, 10.f, 5.f, false) >
        moving / near_settled);
    {
        const struct engine_ai_aim_skill perfect = { 0.f, 0.25f, 10.f, 0.f, 1.f };
        assert(engine_ai_aim_error_degrees(&perfect, 5.f, 1.f, false) == 0.25f);
    }
}

static void test_claims(void)
{
    struct engine_ai_claims table;
    uint32_t object;

    engine_ai_claims_reset(&table);
    assert(engine_ai_claims_acquire(&table, 0, 100, 1, 10, 30));
    assert(!engine_ai_claims_acquire(&table, 0, 100, 2, 11, 30));
    assert(engine_ai_claims_held_by_other(&table, 0, 100, 2, 11));
    assert(!engine_ai_claims_held_by_other(&table, 0, 100, 1, 11));
    assert(engine_ai_claims_acquire(&table, 0, 100, 1, 20, 30)); /* renew */
    assert(!engine_ai_claims_acquire(&table, 0, 100, 2, 49, 30));
    assert(engine_ai_claims_acquire(&table, 0, 100, 2, 50, 30)); /* expired */
    engine_ai_claims_release(&table, 0, 100, 1); /* not the owner: no effect */
    assert(!engine_ai_claims_acquire(&table, 0, 100, 1, 51, 30));
    engine_ai_claims_release(&table, 0, 100, 2);
    assert(engine_ai_claims_acquire(&table, 0, 100, 1, 52, 30));
    assert(engine_ai_claims_acquire(&table, 0, 101, 1, 52, 30));
    engine_ai_claims_release_owner(&table, 0, 1);
    engine_ai_claims_release_owner(&table, 0, 1);
    assert(engine_ai_claims_acquire(&table, 0, 100, 2, 53, 30));
    assert(engine_ai_claims_acquire(&table, 0, 101, 3, 53, 30));
    assert(!engine_ai_claims_acquire(&table, 0, 102, 3, 53, 0));
    assert(!engine_ai_claims_acquire(&table, 0, UINT32_MAX, 3, 53, 30));
    assert(!engine_ai_claims_acquire(&table, 0, 102, 0, 53, 30));

    engine_ai_claims_reset(&table);
    for (object = 0; object < ENGINE_AI_CLAIMS_MAX; object++)
        assert(engine_ai_claims_acquire(&table, 0, object, object + 1, 0, 10));
    assert(!engine_ai_claims_acquire(&table, 0, 999, 999, 5, 10)); /* full */
    assert(engine_ai_claims_acquire(&table, 0, 999, 999, 10, 10)); /* slots expired */
}

/* Teams may independently claim the same world object. */
static void test_claim_scopes(void)
{
    struct engine_ai_claims table;

    engine_ai_claims_reset(&table);
    assert(engine_ai_claims_acquire(&table, 0, 100, 1, 10, 30));
    assert(!engine_ai_claims_acquire(&table, 0, 100, 3, 11, 30));
    assert(!engine_ai_claims_held_by_other(&table, 1, 100, 2, 11));
    assert(engine_ai_claims_acquire(&table, 1, 100, 2, 11, 30));
    assert(!engine_ai_claims_held_by_other(&table, 1, 100, 2, 11));
    assert(engine_ai_claims_held_by_other(&table, 0, 100, 2, 11));
    engine_ai_claims_release(&table, 1, 100, 2);
    assert(!engine_ai_claims_held_by_other(&table, 1, 100, 3, 11));
    assert(engine_ai_claims_held_by_other(&table, 0, 100, 3, 11));
    assert(engine_ai_claims_acquire(&table, 1, 100, 4, 11, 30));
    engine_ai_claims_release_owner(&table, 0, 1);
    assert(!engine_ai_claims_held_by_other(&table, 0, 100, 5, 11));
    assert(engine_ai_claims_held_by_other(&table, 1, 100, 5, 11));

    engine_ai_claims_reset(&table);
    {
        uint32_t object;
        for (object = 0; object < ENGINE_AI_CLAIMS_MAX; object++)
            assert(engine_ai_claims_acquire(&table, 0, object, object + 1, 0, 10));
    }
    assert(engine_ai_claims_acquire(&table, 1, 999, 1, 0, 10));
    assert(!engine_ai_claims_acquire(&table, 2, 999, 2, 0, 10));
}

int main(void)
{
    test_aim();
    test_claims();
    test_claim_scopes();
    printf("engine_ai aim and claims tests passed\n");
    return 0;
}
