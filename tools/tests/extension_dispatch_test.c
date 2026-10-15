/* Dispatcher-only ownership regression tests. */
#include "../../source/extensions/extension_api.h"
#include <assert.h>

struct game_engine { int id; };
struct game_variant { int id; };
static struct game_engine base_engine, first_engine;
static struct game_variant variant;
static boolean first_accept, second_accept, first_open, second_open;
static boolean first_request, second_request, first_close, second_close;
static boolean first_reset[2], second_reset[2];
static long first_selections, second_selections, first_updates, second_updates;
static long first_renders, second_renders, notifications;
static long first_deaths, second_deaths, filtered_actions, committed_ticks;

static boolean select_first(struct game_engine *base, struct game_variant *v, struct game_engine **selected)
{
    assert(base == &base_engine && v == &variant);
    first_selections++;
    if (!first_accept) return FALSE;
    *selected = &first_engine;
    return TRUE;
}
static boolean select_second(struct game_engine *base, struct game_variant *v, struct game_engine **selected)
{
    assert(base == &base_engine && v == &variant);
    second_selections++;
    if (!second_accept) return FALSE;
    /* A ruleset may own callbacks while retaining the original engine. */
    *selected = base;
    return TRUE;
}
static void killed_first(long killer, long victim) { (void)killer; (void)victim; first_deaths++; }
static void killed_second(long killer, long victim) { (void)killer; (void)victim; second_deaths++; }
static void filter_first(long player, struct player_action *action) { (void)player; (void)action; filtered_actions++; }
static void tick_first(void) { committed_ticks++; }
static boolean end_false(void) { return FALSE; }
static boolean end_true(void) { return TRUE; }
static boolean report(void) { return first_accept; }
static boolean controls(long player) { return player == 7; }
static real damage(long a, long b) { (void)a; (void)b; return 2.f; }
static void notify_first(void) { notifications = notifications * 10 + 1; }
static void notify_second(void) { notifications = notifications * 10 + 2; }
static boolean active_first(void) { return first_open; }
static boolean active_second(void) { return second_open; }
static void update_first(real seconds)
{
    (void)seconds;
    first_updates++;
    if (first_request) first_open = TRUE;
    if (first_close) {
        first_open = FALSE;
        first_reset[0] = first_reset[1] = TRUE;
    }
}
static void update_second(real seconds)
{
    (void)seconds;
    second_updates++;
    if (second_request) second_open = TRUE;
    if (second_close) {
        second_open = FALSE;
        second_reset[0] = second_reset[1] = TRUE;
    }
}
static boolean camera_first(short player, void **proc, boolean *reset)
{
    assert(player >= 0 && player < 2);
    *reset = first_reset[player]; first_reset[player] = FALSE;
    *proc = &first_updates;
    return first_open && player == 0;
}
static boolean camera_second(short player, void **proc, boolean *reset)
{
    assert(player >= 0 && player < 2);
    *reset = second_reset[player]; second_reset[player] = FALSE;
    *proc = &second_updates;
    return second_open && player == 0;
}
static void render_first(void) { first_renders++; }
static void render_second(void) { second_renders++; }
static void initialize_first(void) { first_open = FALSE; first_reset[0] = first_reset[1] = FALSE; }
static void initialize_second(void) { second_open = FALSE; second_reset[0] = second_reset[1] = FALSE; }
static uint64_t revision(void) { return 3; }
static const struct halo_ruleset first_rules = {
    .select_game_engine = select_first, .local_only = TRUE,
    .should_end_game = end_false, .damage_multiplier = damage,
    .player_killed = killed_first, .filter_player_action = filter_first, .end_tick = tick_first
};
static const struct halo_ruleset second_rules = {
    .select_game_engine = select_second, .should_end_game = end_true, .player_killed = killed_second
};
static const struct halo_player_controller controller = { .controls_player = controls };
static const struct halo_editor first_editor = {
    .active = active_first, .update = update_first, .director_camera = camera_first,
    .render = render_first, .initialize_for_new_map = initialize_first,
    .dispose_from_old_map = initialize_first, .world_edit_revision = revision
};
static const struct halo_editor second_editor = {
    .active = active_second, .update = update_second, .director_camera = camera_second,
    .render = render_second, .initialize_for_new_map = initialize_second,
    .dispose_from_old_map = initialize_second
};
const struct halo_extension first_extension = {
    .name = "first", .objects_placed = notify_first, .ruleset = &first_rules,
    .suppress_game_report = report, .player_controller = &controller, .editor = &first_editor
};
const struct halo_extension second_extension = {
    .name = "second", .objects_placed = notify_second, .ruleset = &second_rules, .editor = &second_editor
};

static void test_ruleset_ownership(void)
{
    boolean result = TRUE;
    assert(!halo_extensions_active_ruleset());
    assert(!halo_extensions_should_end_game(&result) && result);
    assert(halo_extensions_select_game_engine(&base_engine, &variant) == &base_engine);
    assert(first_selections == 1 && second_selections == 1);
    assert(!halo_extensions_active_ruleset());
    first_accept = second_accept = TRUE;
    assert(halo_extensions_select_game_engine(&base_engine, &variant) == &first_engine);
    assert(first_selections == 2 && second_selections == 1);
    assert(halo_extensions_active_ruleset() == &first_rules);
    assert(halo_extensions_should_end_game(&result) && !result);
    assert(halo_extensions_suppress_network_state());
    assert(halo_extensions_suppress_game_report());
    assert(halo_extensions_damage_multiplier(1, 2) == 2.f);
    /* Runtime predicates cannot transfer callbacks to another ruleset. */
    first_accept = FALSE;
    assert(halo_extensions_active_ruleset() == &first_rules);
    assert(halo_extensions_suppress_network_state());
    assert(halo_extensions_player_killed(1, 2));
    halo_extensions_filter_player_action(1, NULL);
    halo_extensions_end_tick();
    assert(first_deaths == 1 && second_deaths == 0 && filtered_actions == 1 && committed_ticks == 1);
    halo_extensions_end_game_session();
    assert(!halo_extensions_active_ruleset());
    assert(!halo_extensions_suppress_network_state());
    assert(!halo_extensions_should_end_game(&result));
    assert(halo_extensions_select_game_engine(&base_engine, &variant) == &base_engine);
    assert(halo_extensions_active_ruleset() == &second_rules);
    first_accept = TRUE;
    assert(halo_extensions_active_ruleset() == &second_rules);
    assert(halo_extensions_should_end_game(&result) && result);
    assert(!halo_extensions_suppress_network_state());
    assert(halo_extensions_player_killed(1, 2));
    halo_extensions_filter_player_action(1, NULL);
    halo_extensions_end_tick();
    assert(first_deaths == 1 && second_deaths == 1 && filtered_actions == 1 && committed_ticks == 1);
    assert(halo_extensions_damage_multiplier(1, 2) == 1.f);
    /* A non-game initialization clears ownership too. */
    assert(halo_extensions_select_game_engine(NULL, NULL) == NULL);
    assert(!halo_extensions_active_ruleset());
    assert(halo_extensions_can_collect_items(1));
    assert(!halo_extensions_player_killed(1, 2));
}

static void test_editor_ownership(void)
{
    boolean reset = TRUE;
    void *proc = &notifications;
    assert(!halo_extensions_input_captured());
    assert(!halo_extensions_editor_director_camera(0, &proc, &reset) && !reset && !proc);
    /* With no owner both may poll, but no inactive editor renders. */
    halo_extensions_editor_update(0.1f);
    assert(first_updates == 1 && second_updates == 1);
    halo_extensions_editor_render();
    assert(first_renders == 0 && second_renders == 0);
    first_request = second_request = TRUE;
    halo_extensions_editor_update(0.1f);
    assert(first_updates == 2 && second_updates == 1);
    assert(first_open && !second_open);
    first_request = FALSE;
    /* A concurrent external activation cannot steal the current owner's frame. */
    second_open = TRUE;
    halo_extensions_editor_update(0.1f);
    assert(first_updates == 3 && second_updates == 1);
    halo_extensions_editor_render();
    assert(first_renders == 1 && second_renders == 0);
    assert(halo_extensions_editor_director_camera(0, &proc, &reset) && !reset && proc == &first_updates);
    assert(halo_extensions_input_captured());
    first_close = TRUE;
    halo_extensions_editor_update(0.1f);
    assert(first_updates == 4 && second_updates == 1);
    first_close = FALSE;
    /* The lower-priority editor inherits ownership after release. */
    assert(halo_extensions_editor_director_camera(0, &proc, &reset) && !reset && proc == &second_updates);
    first_open = TRUE;
    halo_extensions_editor_update(0.1f);
    assert(first_updates == 4 && second_updates == 2);
    halo_extensions_editor_render();
    assert(first_renders == 1 && second_renders == 1);
    /* Lifecycle reset removes stale ownership; priority applies afresh. */
    halo_extensions_editor_initialize_for_new_map();
    assert(!halo_extensions_input_captured());
    first_open = second_open = TRUE;
    halo_extensions_editor_render();
    assert(first_renders == 2 && second_renders == 1);
    halo_extensions_editor_dispose_from_old_map();
    assert(!halo_extensions_input_captured());
}

static void test_camera_release(void)
{
    boolean reset = FALSE;
    void *proc = NULL;
    first_open = TRUE;
    first_close = TRUE;
    second_request = FALSE;
    halo_extensions_editor_update(0.1f);
    first_close = FALSE;
    assert(!halo_extensions_input_captured());
    assert(!halo_extensions_editor_director_camera(0, &proc, &reset) && reset && !proc);
    assert(!halo_extensions_editor_director_camera(0, &proc, &reset) && !reset);
    assert(!halo_extensions_editor_director_camera(1, &proc, &reset) && reset);
    assert(!halo_extensions_editor_director_camera(1, &proc, &reset) && !reset);
    /* Idle activation can still reach the second editor if the first declines. */
    second_request = TRUE;
    halo_extensions_editor_update(0.1f);
    assert(second_open);
    assert(halo_extensions_editor_director_camera(0, &proc, &reset) && !reset && proc == &second_updates);
    halo_extensions_editor_dispose_from_old_map();
}

int main(void)
{
    assert(halo_extensions_count() == 2);
    assert(halo_extensions_get(-1) == NULL && halo_extensions_get(2) == NULL);
    halo_extensions_objects_placed();
    assert(notifications == 12);
    assert(halo_extensions_player_is_computer_controlled(7));
    assert(!halo_extensions_player_is_computer_controlled(8));
    test_ruleset_ownership();
    test_editor_ownership();
    test_camera_release();
    assert(halo_extensions_world_edit_revision() == 3);
    return 0;
}
