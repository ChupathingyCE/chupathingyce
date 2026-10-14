"""Asset-free tests of production bot-lobby controls and local launch wiring."""
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def body(source, name):
    match = re.search(r"(?:static )?(?:boolean|void|char const \*|char)\s*" + name + r"\([^;{]*\)\s*\{", source)
    assert match, name
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def test_bot_lobby():
    source = (ROOT / "source/features/bots/bot_lobby.c").read_text()
    bots = (ROOT / "source/features/bots/bots.c").read_text()
    main_source = (ROOT / "source/main/main.c").read_text()
    globals_text = source[source.index("enum {"):source.index("boolean bot_lobby_active")]
    pending = bots[bots.index("static struct\n{\n\tboolean pending;"):bots.index("static short lobby_friendly_count")]
    functions = "\n".join(body(source, name) for name in
                          ("bot_lobby_open", "row_mutable", "change", "start_match", "press", "button_event", "bot_lobby_process", "bot_lobby_pointer", "value_text", "bot_lobby_xml_open", "bot_lobby_xml_change", "bot_lobby_xml_text", "bot_lobby_xml_status", "bot_lobby_xml_start", "draw_rows"))
    fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#define csmemset memset
#define csstrcasecmp strcasecmp
typedef int boolean;
typedef unsigned char byte;
#define TRUE 1
#define FALSE 0
#define BOTS_MAXIMUM 31
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define PIN(a,b,c) MIN(MAX(a,b),c)
enum { _loadout_weapon_assault_rifle=2, _loadout_weapon_pistol=3,
    NUMBER_OF_LOADOUT_WEAPONS=10, _loadout_custom=1, _game_connection_local=0 };
enum { _gamepad_analog_button_b, _gamepad_binary_button_dpad_up,
    _gamepad_binary_button_dpad_down, _gamepad_binary_button_dpad_left,
    _gamepad_binary_button_dpad_right, _gamepad_analog_button_a,
    _gamepad_binary_button_start };
static char const *bot_skills[4] = { "recruit", "marine", "odst", "spartan" };
#define NONE -1
static short lobby_friendly_count = NONE;
struct player_datum { short team_index; struct { char team_index; } network_player_data; };
static struct player_datum host={0}, added_player={0};
static int teams=1, host_present=1;
static int game_engine_has_teams(void) { return teams; }
static int local_player_get_player_index(int p) { assert(p==0); return 0; }
static struct player_datum *player_try_and_get(int p) {
    assert(p==0 || p==1); return p==1 ? &added_player : (host_present ? &host : NULL);
}
static char bots_choose_team(void) { return 1; }
static int bot_scenario_enabled(void) { return 0; }
static short bot_scenario_team(short bot_index) { (void)bot_index; return 0; }
static short game_connection(void) { return _game_connection_local; }
/* Local game-engine admission replaces the supplied network_player team. */
static void game_engine_player_added(long p) {
    assert(p==1); added_player.team_index=1; added_player.network_player_data.team_index=1;
}
struct halo_ui_pointer { int right_clicks, left_clicks, click_x, click_y; };
struct game_variant { int teams, ctf; };
struct game_variant_options { int auto_team_balance; byte loadout, primary_weapon, secondary_weapon; };
static int available=1, present=1, profile, client_disposed, server_disposed, remote=-1;
static int cleared, connection=-1, closed, transitioned;
static short player_spawn_count;
static struct game_variant launched;
static struct game_variant_options loadout;
/* Exercise the real main-loop transition flag, not just a called stub. */
static struct {
    int want_to_be_at_main_menu, allow_persistent_storage, defer_map_change, connection;
    char soloplayer_map_name[256];
} main_globals;
#define csstrncpy strncpy
static int game_in_editor(void) { return 0; }
static int game_in_progress(void) { return 1; }
static unsigned long now=1000;
static unsigned long system_milliseconds(void) { return now; }
#define SHORT_MAX 32767
#define SHORT_MIN (-32768)
struct event_record { short type; union { struct { short x,y; } stick;
    struct { byte index,value; } button; } data; };
static struct event_record queued;
static int has_event;
static int get_next_event(struct event_record *e, int controller) {
    assert(controller==NONE); if(!has_event) return 0; *e=queued; has_event=0; return 1;
}
static void event_manager_flush(void) { has_event=0; }
static int ctf_enabled;
static int config_boolean(char const *name) { assert(!strcmp(name,"bots.ctf")); return ctf_enabled; }
static long config_integer(char const *name) { assert(!strcmp(name,"bots.count")); return 0; }
static char const *config_string(char const *name) { assert(!strcmp(name,"bots.skill")); return "marine"; }
static void queue_button(short button, byte held) {
    queued.type=3; queued.data.button.index=(byte)button; queued.data.button.value=held; has_event=1;
}
static int cache_files_map_present(char const *p, unsigned long v) { (void)p; assert(v==0); return present; }
static int cache_files_map_plays_multiplayer(char const *p, char *b) { (void)p; (void)b; return available; }
static void game_engine_get_variant_by_name(struct game_variant *v, char const *n) {
    v->ctf=!strcmp(n,"ctf"); v->teams=v->ctf || !strcmp(n,"team_slayer");
}
static void game_variant_options_default(struct game_variant const *v, struct game_variant_options *o) {
    (void)v; memset(o,0,sizeof(*o)); o->auto_team_balance=1;
}
static void select_profile(void) { profile++; }
static void dispose_global_network_game_client(void) { client_disposed++; }
static void dispose_global_network_game_server(void) { server_disposed++; }
static void network_game_accept_remote_connections(int accept) { remote=accept; }
static void player_ui_clear_multiplayer_variant(void) { cleared++; }
static void game_connection_set(int c) { connection=c; main_globals.connection=c; }
static void game_set_game_variant(struct game_variant const *v) { launched=*v; }
static void game_set_game_variant_options(struct game_variant_options const *o) { loadout=*o; }
static void player_ui_reset_single_player_local_player_controllers(void) { }
static void player_ui_set_single_player_local_player_controller(int p,int c) { assert(p==0 && c==0); }
static void ui_widgets_close_all(void) { closed++; transitioned++; }
enum { UI_FONT_BOLD, UI_ALIGN_LEFT, UI_ALIGN_CENTER };
static int left_arrows, right_arrows, inactive_values;
static void ui_overlay_rect(float x,float y,float w,float h,float r,unsigned int c) {
    (void)x;(void)y;(void)w;(void)h;(void)r;(void)c;
}
static void ui_overlay_gradient(float x,float y,float w,float h,float r,unsigned int a,unsigned int b) {
    (void)x;(void)y;(void)w;(void)h;(void)r;(void)a;(void)b;
}
static void ui_overlay_outline(float x,float y,float w,float h,float r,float t,unsigned int c) {
    (void)x;(void)y;(void)w;(void)h;(void)r;(void)t;(void)c;
}
static float ui_overlay_text(int font,float size,float x,float y,int align,unsigned int color,char const *text) {
    (void)font;(void)size;(void)x;(void)y;(void)align;(void)color;
    if(!strcmp(text,"\xE2\x80\xB9")) left_arrows++;
    if(!strcmp(text,"\xE2\x80\xBA")) right_arrows++;
    if(!strcmp(text,"Map default") || !strcmp(text,"Not used in FFA")) inactive_values++;
    return 0;
}
/* MAIN_TRANSITION */
/* GLOBALS */
/* FUNCTIONS */
int main(void) {
    int i,j;
    char text[64];
    struct halo_ui_pointer pointer={0};
    assert(!strcmp(bot_skills[1],"marine") && !strcmp(labels[0],"MAP") && map_frames[0]==9);
    for(i=0;i<=31;i++) for(j=0;j<=31;j++) {
        assert(bots_prepare_local_match((short)i,(short)j,1)==(i+j<=31));
    }
    assert(!bots_prepare_local_match(-1,1,0));
    assert(!bots_prepare_local_match(1,32,0));
    assert(!bots_prepare_local_match(1,1,-1));
    assert(!bots_prepare_local_match(1,1,4));
    assert(bots_match_team(0)==1); /* ordinary configured bots still balance */
    lobby_friendly_count=3;
    for(i=0;i<7;i++) assert(bots_match_team((short)i)==(i<3 ? 0 : 1));
    host.team_index=1;
    for(i=0;i<7;i++) assert(bots_match_team((short)i)==(i<3 ? 1 : 0));
    teams=0; assert(bots_match_team(0)==1); teams=1;
    host_present=0; assert(bots_match_team(0)==0 && bots_match_team(3)==1);
    host_present=1; host.team_index=NONE; assert(bots_match_team(0)==0);
    host.team_index=0;
    for(i=0;i<7;i++) {
        bots_player_added(1,(short)i);
        assert(added_player.team_index==(i<3 ? 0 : 1));
        assert(added_player.network_player_data.team_index==added_player.team_index);
    }
    lobby_friendly_count=NONE; bots_player_added(1,0); assert(added_player.team_index==1);
    lobby_match.pending=FALSE;
    lobby.active=TRUE; lobby.values[1]=1; lobby.values[5]=0;
    draw_rows(); assert(left_arrows==6 && right_arrows==6 && inactive_values==3);
    left_arrows=right_arrows=inactive_values=0;
    lobby.values[1]=0; draw_rows(); assert(left_arrows==5 && right_arrows==5 && inactive_values==4);
    left_arrows=right_arrows=inactive_values=0;
    lobby.values[1]=1; lobby.values[5]=1; draw_rows(); assert(left_arrows==8 && right_arrows==8);
    lobby.active=TRUE; lobby.values[1]=1; lobby.values[2]=3; lobby.values[3]=28;
    lobby.row=2; change(1); assert(lobby.values[2]==3);
    lobby.row=3; change(1); assert(lobby.values[3]==28);
    lobby.row=1; change(-1);
    assert(lobby.values[1]==0 && lobby.values[2]==0 && lobby.values[3]==31);
    lobby.row=2; change(1); assert(lobby.values[2]==0);
    lobby.row=3; change(-1); assert(lobby.values[3]==30);
    lobby.row=0; change(-1); assert(lobby.values[0]==NUMBEROF(maps)-1);
    change(1); assert(lobby.values[0]==0);
    change(-1);
    lobby.row=4; lobby.values[4]=0; change(-1); assert(lobby.values[4]==3);
    change(1); assert(lobby.values[4]==0);
    lobby.row=6; lobby.values[5]=0; lobby.values[6]=2; change(1); assert(lobby.values[6]==2);
    lobby.values[5]=1; change(1); assert(lobby.values[6]==3);
    lobby.row=0;
    assert(!strcmp(value_text(0,text,sizeof(text)),"Wizard"));
    lobby.opened=now; lobby.row=ROWS; press(_gamepad_analog_button_a);
    assert(!transitioned); now+=500;
    lobby.row=0;
    queue_button(_gamepad_binary_button_dpad_down,1); bot_lobby_process(); assert(lobby.row==1);
    queue_button(_gamepad_binary_button_dpad_down,2); bot_lobby_process();
    assert(lobby.row==1 && "Held arrow must not skip through every row each frame");
    now+=250;
    queue_button(_gamepad_binary_button_dpad_down,2); bot_lobby_process(); assert(lobby.row==2);
    queue_button(_gamepad_binary_button_dpad_up,1); bot_lobby_process(); assert(lobby.row==1);
    /* Clicking a label selects it without changing its value; the visible
       selector's two halves change left and right. */
    pointer.left_clicks=1; pointer.click_y=ROW_Y+4*ROW_HEIGHT+8; pointer.click_x=80;
    lobby.values[4]=1; bot_lobby_pointer(&pointer); assert(lobby.row==4 && lobby.values[4]==1);
    pointer.click_x=VALUE_RIGHT-10; bot_lobby_pointer(&pointer); assert(lobby.values[4]==2);
    pointer.click_x=VALUE_LEFT+10; bot_lobby_pointer(&pointer); assert(lobby.values[4]==1);
    queue_button(_gamepad_analog_button_a,2); bot_lobby_process(); assert(lobby.values[4]==1);
    present=0; start_match(); assert(!transitioned && !lobby_match.pending && lobby.active);
    present=1; available=0; start_match(); assert(!transitioned && !lobby_match.pending && lobby.active);
    available=1; lobby.values[3]=0; start_match(); assert(!transitioned && !lobby_match.pending);
    lobby.values[0]=0; lobby.values[1]=1; lobby.values[2]=3; lobby.values[3]=4;
    lobby.values[4]=3; lobby.values[5]=1; lobby.values[6]=2; lobby.values[7]=3;
    pointer.left_clicks=1; pointer.click_x=500; pointer.click_y=ROW_Y+ROWS*ROW_HEIGHT+10;
    bot_lobby_pointer(&pointer);
    assert(transitioned==1 && !lobby.active);
    assert(lobby_match.pending && lobby_match.friendly_count==3 && lobby_match.enemy_count==4 && lobby_match.skill==3);
    assert(launched.teams && !loadout.auto_team_balance && loadout.loadout==1);
    assert(loadout.primary_weapon==2 && loadout.secondary_weapon==3);
    assert(!strcmp(main_globals.soloplayer_map_name,"levels\\test\\bloodgulch\\bloodgulch"));
    assert(main_globals.defer_map_change && "Start must queue a map change, not leave the UI ring running");
    assert(profile && client_disposed && server_disposed && cleared && closed);
    assert(connection==0 && remote==0 && main_globals.allow_persistent_storage==0 && player_spawn_count==1);
    lobby.active=TRUE; lobby.values[1]=0; lobby.values[2]=0; lobby.values[3]=2;
    lobby.values[5]=0; start_match(); assert(!launched.teams && loadout.loadout==0);
    lobby.active=TRUE; pointer.right_clicks=1; bot_lobby_pointer(&pointer); assert(!lobby.active);
    /* Native XML uses identical selectors without stealing widget input. */
    bot_lobby_xml_open(); assert(!lobby.active && lobby.values[1]==1 && lobby.values[3]==1);
    bot_lobby_xml_change(-1,1); bot_lobby_xml_change(8,1);
    bot_lobby_xml_change(0,2); assert(lobby.values[0]==0);
    bot_lobby_xml_change(0,-1); assert(lobby.values[0]==12);
    bot_lobby_xml_text(0,text,sizeof(text)); assert(strstr(text,"Wizard"));
    bot_lobby_xml_text(-1,text,sizeof(text)); assert(!text[0]);
    bot_lobby_xml_text(8,text,sizeof(text)); assert(!text[0]);
    bot_lobby_xml_text(0,text,1); assert(!text[0]);
    bot_lobby_xml_text(0,NULL,0);
    bot_lobby_xml_change(1,-1); bot_lobby_xml_change(2,1);
    assert(!lobby.values[2]); /* FFA cannot add friendly bots */
    bot_lobby_xml_change(3,-1);
    assert(!bot_lobby_xml_start() && !lobby.active);
    assert(!strcmp(bot_lobby_xml_status(),"Add at least one bot."));
    bot_lobby_xml_change(3,1);
    present=0; assert(!bot_lobby_xml_start());
    assert(strstr(bot_lobby_xml_status(),"Map unavailable"));
    present=1; available=1; assert(bot_lobby_xml_start());
    assert(!lobby.active && !launched.teams && lobby_match.enemy_count==1);
    bot_lobby_xml_open(); bot_lobby_xml_change(1,1); assert(lobby.values[1]==0); /* default: no CTF */
    lobby.values[1]=2; assert(!bot_lobby_xml_start()); assert(strstr(bot_lobby_xml_status(),"disabled"));
    ctf_enabled=1;
    bot_lobby_xml_open(); bot_lobby_xml_change(1,1); /* Team Slayer -> opt-in CTF */
    assert(lobby.values[1]==2);
    bot_lobby_xml_text(1,text,sizeof(text)); assert(strstr(text,"CTF (experimental)"));
    assert(bot_lobby_xml_start() && launched.ctf && launched.teams);
    bot_lobby_xml_change(1,1); assert(lobby.values[1]==0); /* CTF -> FFA */
    return 0;
}
'''
    fixture = fixture.replace("/* MAIN_TRANSITION */", "\n".join(body(main_source, name) for name in
                              ("main_set_map_name", "main_disallow_persistent_storage", "main_menu_switch_to_single_player")))
    fixture = fixture.replace("/* GLOBALS */", pending + globals_text)
    fixture = fixture.replace("/* FUNCTIONS */", body(bots, "bots_prepare_local_match") + body(bots, "bots_match_team") + body(bots, "bots_player_added") + functions)
    with tempfile.TemporaryDirectory(prefix="bot-lobby-test-") as directory:
        path = Path(directory) / "test.c"
        executable = Path(directory) / "test"
        path.write_text(fixture)
        subprocess.run([os.environ.get("CC", "clang"), "-std=gnu89", "-Wall", "-Wextra",
                        "-Werror", "-Wno-sign-compare", "-fsanitize=address,undefined",
                        str(path), "-o", str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == "__main__":
    test_bot_lobby()
