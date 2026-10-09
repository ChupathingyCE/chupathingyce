/* Runtime actions for MCC's own in-map widgets. Callback numbers are wire
 * values from the public UIEventHandlerReferenceFunction definition:
 * https://github.com/SnowyMouse/invader/blob/master/src/tag/hek/definition/ui_widget_definition.json
 * No Xbox/CE tag, callback table or settings implementation is rewritten. */
#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "interface/event_manager.h"
#include "interface/ui_widget.h"
#include "main/main.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "saved games/game_state.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "mcc_cache.h"
#include "mcc_maps.h"
#include "mcc_ui.h"
#include "mcc_ui_network.h"
#include "mcc_ui_teams.h"
#include "network_coop.h"
#include <string.h>

/* Only the public prefix shared by every widget is needed here. */
struct mcc_ui_widget_prefix {
    long definition;
    char const *name;
    short controller;
};
typedef char mcc_ui_controller_offset[offsetof(struct mcc_ui_widget_prefix, controller) == 8 ? 1 : -1];

char const *config_string(char const *name);
boolean pc_menu_profile_edit_begin(void);
boolean pc_menu_tag(long tag_index);
boolean ui_widget_port_open(struct widget_instance *widget, char const *name, boolean *deleted);
boolean ui_widget_port_dispatch_event(struct widget_instance *widget, short type, short controller, boolean *deleted);

static boolean mcc_ui_owns_widget(struct widget_instance *widget)
{
    struct mcc_ui_widget_prefix const *prefix = (void const *)widget;
    return widget && mcc_cache_tags_loaded() && prefix->definition != NONE &&
        mcc_cache_contains(tag_get('DeLa', prefix->definition), 0x60);
}

boolean mcc_ui_settings_needed(char const *map_name)
{
    return map_name && strcmp(map_name, "ui") && mcc_cache_tags_loaded() &&
        !strcmp(config_string("display.menus"), "pc");
}

static short mcc_ui_controller(struct widget_instance *widget, struct event_record *event)
{
    short controller = event ? event->controller_index :
        ((struct mcc_ui_widget_prefix const *)widget)->controller;
    return controller >= 0 && controller < 4 ? controller : 0;
}

static boolean mcc_ui_failure(short controller, wchar_t const *text)
{
    display_error_text_deferred(text, controller);
    return FALSE;
}

static boolean mcc_ui_host_required(short controller)
{
    if (game_connection() == _game_connection_network_client)
        return mcc_ui_failure(controller, L"Only the host can restart or revert this game.");
    return TRUE;
}

static boolean mcc_ui_restart(short controller)
{
    if (!mcc_ui_host_required(controller)) return FALSE;
    if (global_network_game_server_get()) {
        if (!mcc_ui_restart_network_game())
            return mcc_ui_failure(controller, L"The game could not be restarted.");
    } else main_reset_map();
    return TRUE;
}

boolean mcc_ui_new_game(struct widget_instance *widget)
{
    struct mcc_ui_widget_prefix const *prefix = (void const *)widget;
    char const *name;
    short controller;
    if (!mcc_ui_owns_widget(widget)) return FALSE;
    name = tag_get_name(prefix->definition);
    if (!name || strcmp(name, "ui\\shell\\multiplayer_game\\pause_game\\new_game_button")) return FALSE;
    controller = mcc_ui_controller(widget, NULL);
    /* End the round normally: connected players reach the carnage report
     * and the host can pick the next map in the native lobby. */
    if (global_network_game_server_get() && game_engine_running()) game_engine_end_game();
    else mcc_ui_failure(controller, L"Only the host can choose a new game.");
    return TRUE;
}

static boolean mcc_ui_choose_team(struct widget_instance *widget, short controller)
{
    struct network_game *game = network_game_get_game();
    struct mcc_ui_widget_prefix const *prefix = (void const *)widget;
    char const *name = tag_get_name(prefix->definition), *leaf;
    short team, machine = network_game_client_get_local_machine_index();
    long i;
    if (!game || !game->variant.universal_variant.teams)
        return mcc_ui_failure(controller, L"This game does not use teams.");
    leaf = name ? strrchr(name, '\\') : NULL;
    leaf = leaf ? leaf + 1 : name;
    if (leaf && !strcmp(leaf, "red_team_button")) team = 0;
    else if (leaf && !strcmp(leaf, "blue_team_button")) team = 1;
    else return mcc_ui_failure(controller, L"This team choice is not supported.");
    if (!mcc_ui_team_balance_allows(machine, controller, team))
        return mcc_ui_failure(controller, L"The host's automatic team balance prevents this team change.");
    for (i = 0; i < (long)NUMBEROF(game->players); i++) {
        struct network_player player = game->players[i];
        if (network_player_is_valid(&player) && player.machine_index == machine &&
            player.controller_index == controller) {
            player.team_index = (char)team;
            if (network_game_client_update_local_player_data(global_network_game_client_get(), &player))
                return TRUE;
            break;
        }
    }
    return mcc_ui_failure(controller, L"The team change could not be sent to the host.");
}

boolean mcc_ui_event_function(struct widget_instance *widget,
    struct event_record *event, word function, boolean *deleted, boolean *result)
{
    short controller;
    if (!mcc_ui_owns_widget(widget)) return FALSE;
    controller = mcc_ui_controller(widget, event);
    *result = TRUE;
    switch (function) {
    case 11: /* Confirm revert to checkpoint. */
        if (!mcc_ui_host_required(controller)) { *result = FALSE; break; }
        if (!game_state_port_saved_game_valid()) {
            *result = mcc_ui_failure(controller, L"There is no saved checkpoint to revert to.");
            break;
        }
        main_revert_map(); /* Existing engine host-revert path synchronizes co-op. */
        break;
    case 12: /* Confirm restart level. */
    case 163: /* PC multiplayer options: restart game. */
        *result = mcc_ui_restart(controller);
        break;
    case 13: /* Campaign quit: retain this MCC campaign's checkpoint. */
        if (network_coop_active()) {
            short i;
            for (i = 0; i < 4; i++) network_game_client_local_player_quit(i);
        } else {
            if (mcc_maps_level_campaign(main_get_map_name()) && game_state_port_saved_game_valid())
                game_state_save_to_persistent_storage();
            main_goto_main_menu();
        }
        break;
    case 108: /* Mouse accept: use the same A/confirmation route as a controller. */
    case 109:
    case 110:
    case 111:
    case 128:
        event_manager_post_button(controller,
            function == 108 ? 0 : function == 109 ? 1 : function == 110 ? 10 : function == 111 ? 11 : 2);
        break;
    case 137: /* Open trusted native settings, never the map's PC configuration widgets. */
        if (!pc_menu_tag(tag_loaded('DeLa', "pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\player_profile_edit_screen")) ||
            !pc_menu_profile_edit_begin()) {
            *result = mcc_ui_failure(controller, L"Settings could not be opened for this player.");
            break;
        }
        *result = ui_widget_port_open(widget,
            "pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\player_profile_edit_screen", deleted);
        break;
    case 155: /* PC pause/options creation has no engine state to initialize. */
    case 156:
        break;
    case 157:
        *result = mcc_ui_choose_team(widget, controller);
        break;
    case 164:
        *result = ui_widget_port_dispatch_event(widget_instance_get_topmost_parent(widget), 32, controller, deleted);
        break;
    case 179: /* Explicit save checkpoint from the campaign pause screen. */
        if (game_connection() == _game_connection_network_client) {
            *result = mcc_ui_failure(controller, L"Only the host can save a checkpoint.");
            break;
        }
        game_state_save();
        if (!game_state_port_saved_game_valid()) {
            *result = mcc_ui_failure(controller, L"The checkpoint could not be saved.");
            break;
        }
        if (!network_coop_active() && mcc_maps_level_campaign(main_get_map_name()))
            game_state_save_to_persistent_storage();
        break;
    default:
        /* Native-compatible IDs still pass through the original dispatcher
         * and its map-widget permissions. Unknown PC callbacks must fail so
         * their open/close flags cannot claim an operation succeeded. */
        if (function < 102) return FALSE;
        *result = mcc_ui_failure(controller, L"This map's menu action is not supported by the native game.");
        break;
    }
    return TRUE;
}
