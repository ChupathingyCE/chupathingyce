/* port: a private, local bot match, using the ordinary map/variant/loadout
   pipeline. The overlay is available with the game-browser renderer. No
   debug test input, persistent config changes or network host is needed. */
#ifdef HALO_GAME_BROWSER
#include "bot_lobby.h"
#include "cseries/cseries_windows.h"
#include "bots.h"
#include "cache/cache_files.h"
#include "bitmaps/bitmap_group.h"
#include "cutscene/cinematics.h"
#include "tag_files/tag_groups.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/player_ui.h"
#include "interface/ui_widget.h"
#include "main/main.h"
#include "networking/network_game_globals.h"
#include "saved games/player_profile.h"
#include "saved games/saved_game_files.h"
#include "halo_ui_pointer.h"
#include "../../../port/linux/src/ui_overlay.h"
#include <stdio.h>

/* main.c's local-player count and map transition. */
extern short player_spawn_count;
void game_connection_set(short connection);
long config_integer(char const *name);
int config_boolean(char const *name);
char const *config_string(char const *name);

enum {
    ROWS = 8, ROW_Y = 92, ROW_HEIGHT = 25, SETTLE_MS = 400,
    REPEAT_MS = 250, LIST_X = 37, LIST_WIDTH = 566, LIST_HEIGHT = 261, DETAIL_Y = 338,
    VALUE_LEFT = 345, VALUE_RIGHT = 592, VALUE_CENTER = 468,
    COLOR_BACKGROUND_TOP = 0x0B1830FF, COLOR_BACKGROUND_BOTTOM = 0x03070FFF,
    COLOR_RULE = 0x2A62C8FF, COLOR_TITLE = 0x3D8BFFFF, COLOR_PANEL = 0x081530F0,
    COLOR_PANEL_EDGE = 0x2F6DD0FF, COLOR_HEAD = 0x7FB0FFFF,
    COLOR_ROW_SELECTED = 0x2052B0FF, COLOR_ROW_RULE = 0x16294AFF,
    COLOR_TEXT = 0xE6EEFCFF, COLOR_DIM = 0x8FA6C8FF, COLOR_LABEL = 0x4AA3FFFF
};
static char const *const maps[][2] = {
    { "bloodgulch", "Blood Gulch" }, { "beavercreek", "Battle Creek" },
    { "boardingaction", "Boarding Action" }, { "carousel", "Derelict" },
    { "chillout", "Chill Out" }, { "damnation", "Damnation" },
    { "hangemhigh", "Hang 'Em High" }, { "longest", "Longest" },
    { "prisoner", "Prisoner" }, { "putput", "Chiron TL-34" },
    { "ratrace", "Rat Race" }, { "sidewinder", "Sidewinder" }, { "wizard", "Wizard" }
};
/* mp_map_grafix's frames, in the same order as maps above. */
static short const map_frames[] = { 9, 0, 8, 7, 6, 2, 5, 12, 4, 11, 3, 1, 10 };
/* port: Both menu frontends select the same supported standard variants. */
static char const *const modes[] = { "FFA Slayer", "Team Slayer", "CTF (experimental)" };
static char const *const variants[] = { "slayer", "team_slayer", "ctf" };
typedef char bot_lobby_modes_check[NUMBEROF(modes) == NUMBEROF(variants) ? 1 : -1];
static char const *const skills[] = { "Recruit", "Marine", "ODST", "Spartan" };
static char const *const weapons[] = {
    "None", "Random", "Assault Rifle", "Magnum", "Shotgun", "Sniper Rifle",
    "Rocket Launcher", "Plasma Pistol", "Plasma Rifle", "Needler"
};
/* The tables above are indexed by the lobby's values: keep them parallel. */
typedef char bot_lobby_map_frames_check[NUMBEROF(map_frames) == NUMBEROF(maps) ? 1 : -1];
typedef char bot_lobby_weapons_check[NUMBEROF(weapons) == NUMBER_OF_LOADOUT_WEAPONS ? 1 : -1];
static char const *const labels[ROWS] = {
    "MAP", "GAME MODE", "FRIENDLY BOTS", "ENEMY BOTS", "BOT DIFFICULTY",
    "STARTING LOADOUT", "PRIMARY WEAPON", "SECONDARY WEAPON"
};
static struct {
    boolean active;
    short row;
    short values[ROWS];
    unsigned long opened;
    unsigned long direction_time[4];
    char status[96];
} lobby;

boolean bot_lobby_active(void) { return lobby.active; }

void bot_lobby_open(void)
{
    short index;
    char const *skill = config_string("bots.skill");
    csmemset(&lobby, 0, sizeof(lobby));
    lobby.active = TRUE;
    lobby.opened = system_milliseconds();
    lobby.values[1] = 1; /* Team Slayer */
    lobby.values[3] = (short)PIN(config_integer("bots.count"), 1, 31);
    lobby.values[4] = 1;
    lobby.values[6] = _loadout_weapon_assault_rifle;
    lobby.values[7] = _loadout_weapon_pistol;
    for (index = 0; index < NUMBEROF(skills); index++)
        if (skill && !csstrcasecmp(skill, skills[index])) lobby.values[4] = index;
    event_manager_flush();
}

static boolean row_mutable(short row)
{
    if (row < 0 || row >= ROWS) return FALSE;
    if (row == 2 && !lobby.values[1]) return FALSE;
    if ((row == 6 || row == 7) && !lobby.values[5]) return FALSE;
    return TRUE;
}

static void change(short direction)
{
    short maximum[ROWS] = { NUMBEROF(maps) - 1, NUMBEROF(modes) - 1, 31, 31, NUMBEROF(skills) - 1, 1,
        NUMBER_OF_LOADOUT_WEAPONS - 1, NUMBER_OF_LOADOUT_WEAPONS - 1 };
    short row = lobby.row;
    if (!row_mutable(row)) return;
    /* port: no ordinary lobby admission until real flag/scoring gates pass. */
    if (!config_boolean("bots.ctf")) maximum[1] = 1;
    if (row == 2 || row == 3)
    {
        if (row == 2 && !lobby.values[1]) return;
        maximum[row] = 31 - lobby.values[row == 2 ? 3 : 2];
    }
    /* Counts stop at the capacity limit; selector lists wrap both ways. */
    if (row == 2 || row == 3)
        lobby.values[row] = (short)PIN(lobby.values[row] + direction, 0, maximum[row]);
    else
        lobby.values[row] = (short)((lobby.values[row] + direction + maximum[row] + 1) %
            (maximum[row] + 1));
    if (row == 1 && !lobby.values[1])
    {
        lobby.values[3] += lobby.values[2];
        lobby.values[2] = 0;
    }
    lobby.status[0] = 0;
}

static void select_profile(void)
{
    struct player_profile profile;
    long index = player_ui_get_player1_last_used_profile_index();
    if (index == NONE || !TEST_FLAG(index, _saved_game_file_index_valid_bit))
    {
        long indices[100];
        word count = NUMBEROF(indices);
        player_profiles_enumerate_available_to_local_player_index(0, &count, indices, FALSE);
        index = count ? indices[0] : NONE;
    }
    if (index != NONE && TEST_FLAG(index, _saved_game_file_index_valid_bit) &&
        player_profile_get(index, &profile))
        player_ui_set_active_player_profile(0, index, &profile);
}

static boolean start_match(void)
{
    struct game_variant variant;
    struct game_variant_options options;
    char path[128], build[32];
    if (lobby.values[1] == 2 && !config_boolean("bots.ctf"))
    {
        snprintf(lobby.status, sizeof(lobby.status), "Experimental CTF is disabled.");
        return FALSE;
    }
    if (!lobby.values[2] && !lobby.values[3])
    {
        snprintf(lobby.status, sizeof(lobby.status), "Add at least one bot.");
        return FALSE;
    }
    snprintf(path, sizeof(path), "levels\\test\\%s\\%s",
        maps[lobby.values[0]][0], maps[lobby.values[0]][0]);
    if (!cache_files_map_present(path, 0) || !cache_files_map_plays_multiplayer(path, build))
    {
        snprintf(lobby.status, sizeof(lobby.status), "Map unavailable: check your Xbox maps.");
        return FALSE;
    }
    if (!bots_prepare_local_match(lobby.values[2], lobby.values[3], lobby.values[4]))
    {
        snprintf(lobby.status, sizeof(lobby.status), "Invalid bot match settings.");
        return FALSE;
    }
    game_engine_get_variant_by_name(&variant, variants[lobby.values[1]]);
    game_variant_options_default(&variant, &options);
    options.auto_team_balance = FALSE;
    if (lobby.values[5])
    {
        options.loadout = _loadout_custom;
        options.primary_weapon = (byte)lobby.values[6];
        options.secondary_weapon = (byte)lobby.values[7];
    }
    select_profile();
    dispose_global_network_game_client();
    dispose_global_network_game_server();
    network_game_accept_remote_connections(FALSE);
    player_ui_clear_multiplayer_variant();
    game_connection_set(_game_connection_local);
    game_set_game_variant(&variant);
    game_set_game_variant_options(&options);
    player_spawn_count = 1;
    player_ui_reset_single_player_local_player_controllers();
    player_ui_set_single_player_local_player_controller(0, 0);
    lobby.active = FALSE;
    ui_widgets_close_all();
    main_set_map_name(path);
    main_disallow_persistent_storage();
    /* main_defer_map_map_change cancels the flag; the shell's single-player
       transition queues it. Without this the widgets close over ui.map's ring. */
    main_menu_switch_to_single_player();
    return TRUE;
}

static void press(short button)
{
    if (system_milliseconds() - lobby.opened < SETTLE_MS) return;
    switch (button)
    {
    case _gamepad_analog_button_b: lobby.active = FALSE; break;
    case _gamepad_binary_button_dpad_up: lobby.row = (short)MAX(0, lobby.row - 1); break;
    case _gamepad_binary_button_dpad_down: lobby.row = (short)MIN(ROWS, lobby.row + 1); break;
    case _gamepad_binary_button_dpad_left: change(-1); break;
    case _gamepad_binary_button_dpad_right: change(1); break;
    case _gamepad_analog_button_a:
        if (lobby.row == ROWS) start_match(); else change(1);
        break;
    case _gamepad_binary_button_start: start_match(); break;
    default: break;
    }
}

static void button_event(struct event_record const *event)
{
    short button = event->data.button.index;
    unsigned long now = system_milliseconds();
    if (event->data.button.value == 1)
    {
        if (button >= _gamepad_binary_button_dpad_up && button <= _gamepad_binary_button_dpad_right)
            lobby.direction_time[button - _gamepad_binary_button_dpad_up] = now;
        press(button);
    }
    else if (event->data.button.value > 1 &&
        button >= _gamepad_binary_button_dpad_up && button <= _gamepad_binary_button_dpad_right)
    {
        short direction = button - _gamepad_binary_button_dpad_up;
        if (now - lobby.direction_time[direction] >= REPEAT_MS)
        {
            lobby.direction_time[direction] = now;
            press(button);
        }
    }
}

void bot_lobby_process(void)
{
    struct event_record event;
    short count;
    /* Bound a frame's input work even if a producer keeps posting. */
    for (count = 0; count < 64 && lobby.active && get_next_event(&event, NONE); count++)
    {
        if (event.type == 3) button_event(&event);
        else if (event.type == 1)
        {
            if (event.data.stick.y == SHORT_MAX) press(_gamepad_binary_button_dpad_up);
            if (event.data.stick.y == SHORT_MIN) press(_gamepad_binary_button_dpad_down);
            if (event.data.stick.x == SHORT_MIN) press(_gamepad_binary_button_dpad_left);
            if (event.data.stick.x == SHORT_MAX) press(_gamepad_binary_button_dpad_right);
        }
    }
    event_manager_flush();
}

void bot_lobby_pointer(struct halo_ui_pointer const *pointer)
{
    short row;
    if (pointer->right_clicks) press(_gamepad_analog_button_b);
    if (!lobby.active || !pointer->left_clicks) return;
    if (pointer->click_y >= 444)
    {
        if (pointer->click_x >= 37 && pointer->click_x <= 113)
            press(_gamepad_analog_button_b);
        else if (pointer->click_x >= 171 && pointer->click_x < 192)
            press(_gamepad_binary_button_dpad_left);
        else if (pointer->click_x >= 192 && pointer->click_x <= 288)
            press(_gamepad_binary_button_dpad_right);
        else if (pointer->click_x >= 335 && pointer->click_x <= 430)
            press(_gamepad_analog_button_a);
        else if (pointer->click_x >= 460 && pointer->click_x <= 603)
            press(_gamepad_binary_button_start);
        return;
    }
    if (pointer->click_x < LIST_X || pointer->click_x > LIST_X + LIST_WIDTH) return;
    row = (short)((pointer->click_y - ROW_Y) / ROW_HEIGHT);
    if (pointer->click_y < ROW_Y || row > ROWS) return;
    lobby.row = row;
    if (row == ROWS) press(_gamepad_binary_button_start);
    /* Labels select only; each half of the visible switch changes its value. */
    else if (pointer->click_x >= VALUE_LEFT)
        press(pointer->click_x < VALUE_CENTER ? _gamepad_binary_button_dpad_left :
            _gamepad_binary_button_dpad_right);
}

static char const *value_text(short row, char *text, size_t size)
{
    short value = lobby.values[row];
    switch (row)
    {
    case 0: return maps[value][1];
    case 1: return modes[value];
    case 4: return skills[value];
    case 5: return value ? "Custom" : "Map default";
    case 6: case 7: return weapons[value];
    default: snprintf(text, size, "%d", value); return text;
    }
}

/* port: XML widgets use the same bounded selectors and private launch path,
   without activating the overlay or consuming the widget system's events. */
void bot_lobby_xml_open(void)
{
    bot_lobby_open();
    lobby.active = FALSE;
}

void bot_lobby_xml_change(short row, short direction)
{
    if (row < 0 || row >= ROWS || (direction != -1 && direction != 1)) return;
    lobby.row = row;
    change(direction);
}

void bot_lobby_xml_text(short row, char *text, size_t size)
{
    char value[64];
    char const *shown;
    if (!text || !size) return;
    text[0] = 0;
    if (row < 0 || row >= ROWS) return;
    shown = row_mutable(row) ? value_text(row, value, sizeof(value)) :
        (row == 2 ? "Not used in FFA" : "Map default");
    snprintf(text, size, "%s:  %s", labels[row], shown);
}

char const *bot_lobby_xml_status(void) { return lobby.status; }
boolean bot_lobby_xml_start(void) { return start_match(); }

static void draw_rows(void)
{
    short row;
    char text[64];
    ui_overlay_rect(LIST_X, 72, LIST_WIDTH, LIST_HEIGHT, 6, COLOR_PANEL);
    ui_overlay_gradient(LIST_X, 72, LIST_WIDTH, 20, 6, 0x123266FF, 0x0C2347FF);
    ui_overlay_text(UI_FONT_BOLD, 8, 52, 78, UI_ALIGN_LEFT, COLOR_HEAD, "MATCH SETTINGS");
    ui_overlay_text(UI_FONT_BOLD, 8, VALUE_CENTER, 78, UI_ALIGN_CENTER, COLOR_HEAD, "VALUE");
    for (row = 0; row <= ROWS; row++)
    {
        float y = ROW_Y + row * ROW_HEIGHT;
        boolean mutable = row_mutable(row);
        unsigned int color = mutable || row == ROWS ? COLOR_TEXT : COLOR_DIM;
        if (row == lobby.row)
            ui_overlay_rect(LIST_X + 1, y, LIST_WIDTH - 2, ROW_HEIGHT, 0, COLOR_ROW_SELECTED);
        ui_overlay_rect(LIST_X + 1, y + ROW_HEIGHT, LIST_WIDTH - 2, 0.5f, 0, COLOR_ROW_RULE);
        ui_overlay_text(UI_FONT_BOLD, 10, 52, y + 7, UI_ALIGN_LEFT, color,
            row == ROWS ? "START BOT MATCH" : labels[row]);
        if (row == ROWS)
            ui_overlay_text(UI_FONT_BOLD, 9, VALUE_CENTER, y + 7, UI_ALIGN_CENTER, COLOR_LABEL, "ENTER TO PLAY");
        else
        {
            ui_overlay_outline(VALUE_LEFT, y + 3, VALUE_RIGHT - VALUE_LEFT, ROW_HEIGHT - 6,
                3, 0.75f, mutable ? COLOR_PANEL_EDGE : COLOR_ROW_RULE);
            if (mutable)
            {
                ui_overlay_text(UI_FONT_BOLD, 14, VALUE_LEFT + 10, y + 4, UI_ALIGN_CENTER, COLOR_LABEL, "\xE2\x80\xB9");
                ui_overlay_text(UI_FONT_BOLD, 14, VALUE_RIGHT - 10, y + 4, UI_ALIGN_CENTER, COLOR_LABEL, "\xE2\x80\xBA");
            }
            ui_overlay_text(UI_FONT_BOLD, 10, VALUE_CENTER, y + 7, UI_ALIGN_CENTER, color,
                !mutable ? (row == 2 ? "Not used in FFA" : "Map default") :
                    value_text(row, text, sizeof(text)));
        }
    }
    ui_overlay_outline(LIST_X, 72, LIST_WIDTH, LIST_HEIGHT, 6, 1, COLOR_PANEL_EDGE);
}

/* The same map artwork/cutout as Online Games, from the player's ui.map. */
static void draw_map(void)
{
    long tag = tag_loaded('bitm', "ui\\shell\\bitmaps\\mp_map_grafix");
    struct bitmap_data *bitmap = tag != NONE ?
        bitmap_group_get_bitmap_from_sequence(tag, 0, map_frames[lobby.values[0]]) : NULL;
    rectangle2d bounds = { DETAIL_Y + 9, 46, DETAIL_Y + 88, 142 };
    ui_overlay_cutout(46, DETAIL_Y + 9, 96, 79);
    draw_quad(&bounds, 0xFF0A1A33);
    if (bitmap)
    {
        rectangle2d art = { 0, 0, (short)MIN(116, bitmap->height), (short)MIN(140, bitmap->width) };
        draw_bitmap_in_rect(bitmap, &bounds, &art, NULL, 0xFFFFFFFF, NULL, FALSE);
    }
    ui_overlay_outline(45, DETAIL_Y + 8, 98, 81, 0, 1, COLOR_ROW_RULE);
}

static void draw_preview(void)
{
    char text[80];
    ui_overlay_rect(LIST_X, DETAIL_Y, LIST_WIDTH, 97, 6, COLOR_PANEL);
    ui_overlay_outline(LIST_X, DETAIL_Y, LIST_WIDTH, 97, 6, 1, COLOR_PANEL_EDGE);
    draw_map();
    ui_overlay_text(UI_FONT_BOLD, 13, 155, DETAIL_Y + 11, UI_ALIGN_LEFT, COLOR_TEXT, maps[lobby.values[0]][1]);
    snprintf(text, sizeof(text), "%s - %s", modes[lobby.values[1]], skills[lobby.values[4]]);
    ui_overlay_text(UI_FONT_REGULAR, 10, 155, DETAIL_Y + 32, UI_ALIGN_LEFT, COLOR_LABEL, text);
    snprintf(text, sizeof(text), "You + %d bots", lobby.values[2] + lobby.values[3]);
    ui_overlay_text(UI_FONT_REGULAR, 10, 155, DETAIL_Y + 49, UI_ALIGN_LEFT, COLOR_TEXT, text);
    ui_overlay_text(UI_FONT_REGULAR, 8.5f, 155, DETAIL_Y + 73, UI_ALIGN_LEFT, COLOR_DIM, "Private game on this machine only");
    ui_overlay_rect(430, DETAIL_Y + 10, 0.75f, 77, 0, COLOR_ROW_RULE);
    ui_overlay_text(UI_FONT_BOLD, 9, 444, DETAIL_Y + 12, UI_ALIGN_LEFT, COLOR_LABEL, "LOBBY PREVIEW");
    if (lobby.values[1])
    {
        snprintf(text, sizeof(text), "Your team: you + %d bots", lobby.values[2]);
        ui_overlay_text(UI_FONT_REGULAR, 9, 444, DETAIL_Y + 34, UI_ALIGN_LEFT, 0x6BB0FFFF, text);
        snprintf(text, sizeof(text), "Enemy team: %d bots", lobby.values[3]);
    }
    else
        snprintf(text, sizeof(text), "%d opponents", lobby.values[3]);
    ui_overlay_text(UI_FONT_REGULAR, 9, 444, DETAIL_Y + 51, UI_ALIGN_LEFT, 0xFF6B6BFF, text);
    ui_overlay_text(UI_FONT_REGULAR, 8.5f, 444, DETAIL_Y + 73, UI_ALIGN_LEFT, COLOR_DIM, "31 bots maximum");
}

void bot_lobby_render(void)
{
    char text[64];
    float margin = ((float)halo_screen_width() - 640.0f) / 2.0f + 2.0f;
    if (!lobby.active || !ui_overlay_available()) return;
    ui_overlay_gradient(-margin, 0, 640 + 2 * margin, 480, 0, COLOR_BACKGROUND_TOP, COLOR_BACKGROUND_BOTTOM);
    ui_overlay_gradient(-margin, 0, 640 + 2 * margin, 62, 0, 0x0A1A36FF, 0x050C1AFF);
    ui_overlay_rect(-margin, 61.5f, 640 + 2 * margin, 1, 0, COLOR_RULE);
    ui_overlay_text(UI_FONT_BOLD, 30, 37, 17, UI_ALIGN_LEFT, COLOR_TITLE, "BOT MATCH");
    snprintf(text, sizeof(text), "BOTS  %d    TEAMS  %d    PRIVATE", lobby.values[2] + lobby.values[3], lobby.values[1] ? 2 : 0);
    ui_overlay_text(UI_FONT_BOLD, 9, 603, 39, UI_ALIGN_RIGHT, COLOR_HEAD, text);
    draw_rows();
    ui_overlay_text(UI_FONT_REGULAR, 8.5f, 47, 320, UI_ALIGN_LEFT, lobby.status[0] ? 0xF08A4BFF : COLOR_DIM,
        lobby.status[0] ? lobby.status : "Arrow keys: select / change    Enter: apply selection    Lists wrap left / right");
    draw_preview();
    ui_overlay_rect(-margin, 444, 640 + 2 * margin, 0.75f, 0, COLOR_RULE);
    ui_overlay_button(UI_BUTTON_B, 15, 37, 455, 0xFFFFFFFF);
    ui_overlay_text(UI_FONT_BOLD, 10, 57, 456, UI_ALIGN_LEFT, COLOR_LABEL, "=BACK");
    ui_overlay_button(UI_BUTTON_DPAD_LEFT, 15, 171, 455, 0xFFFFFFFF);
    ui_overlay_button(UI_BUTTON_DPAD_RIGHT, 15, 192, 455, 0xFFFFFFFF);
    ui_overlay_text(UI_FONT_BOLD, 10, 213, 456, UI_ALIGN_LEFT, COLOR_LABEL, "=CHANGE");
    ui_overlay_button(UI_BUTTON_A, 15, 335, 455, 0xFFFFFFFF);
    ui_overlay_text(UI_FONT_BOLD, 10, 357, 456, UI_ALIGN_LEFT, COLOR_LABEL, "=SELECT");
    ui_overlay_button(UI_BUTTON_START, 15, 465, 455, 0xFFFFFFFF);
    ui_overlay_text(UI_FONT_BOLD, 10, 487, 456, UI_ALIGN_LEFT, COLOR_LABEL, "=START MATCH");
}
#endif
