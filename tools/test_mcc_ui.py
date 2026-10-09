"""Exercise MCC callbacks through the actual native event-handler dispatcher.

Mouse accept is queued and then delivered to the same A/confirmation records
as controller input. Host commands and tag storage are narrow fakes; routing,
navigation flags, failure handling and map-widget permission checks are real.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest
from harness import function, structure

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="session")
def ui_tool(tmp_path_factory):
    compiler = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not compiler:
        pytest.skip("a C compiler is needed for MCC widget tests")
    ui = (ROOT / "port/linux/game/mcc_ui.c").read_text()
    native = (ROOT / "source/interface/ui_widget.c").read_text()
    events = (ROOT / "source/interface/ui_widget_event_handler_functions.c").read_text()
    tags = (ROOT / "port/linux/game/mcc_tags.c").read_text()
    names = ["mcc_ui_owns_widget", "mcc_ui_settings_needed", "mcc_ui_controller",
             "mcc_ui_failure", "mcc_ui_host_required", "mcc_ui_restart", "mcc_ui_new_game", "mcc_ui_choose_team",
             "mcc_ui_event_function"]
    source = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
typedef unsigned char boolean;
typedef unsigned short word;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define NUMBEROF(a) (sizeof(a)/sizeof(*(a)))
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
#define PC_MENU_FUNCTION_BASE 256
#define TEST_FLAG(value,bit) ((value)&(1u<<(bit)))
#define _error_silent 0
#define match_assert(file,line,condition) assert(condition)
#define match_vassert(file,line,condition,...) assert(condition)
enum {_game_connection_local,_game_connection_network_client,_game_connection_network_server};
enum {_event_handler_close_current_widget_bit,_event_handler_close_other_widget_bit,
_event_handler_close_all_widgets_bit,_event_handler_open_widget_bit,_event_handler_reload_self_bit,
_event_handler_reload_widget_bit,_event_handler_give_focus_to_widget_bit,_event_handler_run_function_bit,
_event_handler_replace_with_other_widget_bit,_event_handler_go_back_to_previous_widget_bit,
_event_handler_run_scenario_script_bit,_event_handler_look_for_conditional_widget_on_failure_bit};
enum {_conditional_widget_load_if_event_handler_function_fails_bit};
enum {_ui_audio_feedback_none,_ui_audio_feedback_cursor,_ui_audio_feedback_forward,_ui_audio_feedback_back};
struct widget_instance {long definition_tag_index;char const *name;short local_player_index;
short horizontal_offset,vertical_offset;struct widget_instance *parent,*next,*previous,*child,*focused_child;};
struct event_record {short type,controller_index;};
struct tag_reference {long index;};
struct tag_block {long count;void *address;};
struct ui_widget_definition {struct tag_block conditional_widgets;};
struct ui_widget_event_handler_reference {uint32_t flags;short event_type,function;
struct tag_reference widget_tag,sound_effect;char script[32];};
struct ui_widget_conditional_reference {struct tag_reference widget_tag;long flags;};
struct widget_stack_data {int unused;};
static struct {struct widget_instance *active_widgets[4];void *widget_stack[4];} widget_globals;
struct network_player {int valid;short machine_index,controller_index;char team_index;};
struct network_game {struct {struct {boolean teams;} universal_variant;} variant;struct network_player players[4];};
static struct network_game game;
static struct network_player sent_player;
static int mcc=1,connection,coop,saved=1,profile=1,have_settings=1,profile_begin,settings_open;
static int revert_count,reset_count,restart_count,save_count,persist_count,menu_count,quit_count,end_count,settings_collision;
static int posted_button=-1,posted_controller=-1,deleted_count,opened_count,legacy_count,port_count;
static int message_count,script_count,team_sent,denied_count,top_dispatch,quit_controller=-1,balance_allowed=1;
static boolean free_widget_on_delete,round_active=TRUE;
static struct widget_instance *released_widget;
static long opened_tag;
static wchar_t const *message;
static char const *widget_tag_name="ui\\shell\\multiplayer_game\\pause_game\\blue_team_button";
static char owned[0x60],unowned[0x60];
static boolean mcc_cache_tags_loaded(void){return mcc;}
static void *tag_get(long group,long index){(void)group;return index==100?owned:unowned;}
static char *tag_get_name(long index){(void)index;return (char *)widget_tag_name;}
static boolean mcc_cache_contains(void const *p,long size){return p==owned && size<=sizeof(owned);}
static char const *config_string(char const *name){(void)name;return "pc";}
static short game_connection(void){return connection;}
static void display_error_text_deferred(wchar_t const *text,short controller){assert(controller>=0&&controller<4);message_count++;message=text;}
static void *global_network_game_server_get(void){return connection==_game_connection_network_server?&game:NULL;}
static void *global_network_game_client_get(void){return connection?&game:NULL;}
static boolean mcc_ui_restart_network_game(void){restart_count++;return TRUE;}
static void main_reset_map(void){reset_count++;}
static void main_revert_map(void){revert_count++;}
static void main_goto_main_menu(void){menu_count++;}
static boolean game_engine_running(void){return TRUE;}
static boolean game_engine_can_score(void){return round_active;}
static void ui_widget_delete(struct widget_instance *widget);
static void game_engine_end_game(void){int i;end_count++;for(i=0;i<4;i++)if(widget_globals.active_widgets[i])ui_widget_delete(widget_globals.active_widgets[i]);}
static boolean network_coop_active(void){return coop;}
static boolean mcc_maps_level_campaign(char const *name){(void)name;return TRUE;}
static char *main_get_map_name(void){return "mcc_maps\\a10";}
static boolean game_state_port_saved_game_valid(void){return saved;}
static void game_state_save_to_persistent_storage(void){persist_count++;}
static void game_state_save(void){save_count++;saved=TRUE;}
static void network_game_client_local_player_quit(short controller){quit_count++;quit_controller=controller;}
static short local_player_count(void){return 1;}
static void player_ui_local_player_left_multiplayer_game(short controller){(void)controller;}
static struct network_game *network_game_get_game(void){return connection?&game:NULL;}
static short network_game_client_get_local_machine_index(void){return 2;}
static boolean network_player_is_valid(struct network_player const *player){return player->valid;}
static boolean network_game_client_update_local_player_data(void *client,struct network_player *player){assert(client);sent_player=*player;team_sent++;return TRUE;}
static boolean mcc_ui_team_balance_allows(short machine,short controller,short team){(void)machine;(void)controller;(void)team;return balance_allowed;}
static void event_manager_post_button(short controller,short button){posted_button=button;posted_controller=controller;}
static long tag_loaded(long group,char const *name){(void)group;(void)name;return have_settings?(settings_collision?100:200):NONE;}
static boolean pc_menu_profile_edit_begin(void){profile_begin++;return profile;}
static boolean ui_widget_port_open(struct widget_instance *widget,char const *name,boolean *deleted){(void)widget;assert(strstr(name,"pc\\main_menu\\settings_select"));settings_open++;*deleted=TRUE;return TRUE;}
static struct widget_instance *widget_instance_get_topmost_parent(struct widget_instance *widget){assert(widget!=released_widget);while(widget->parent)widget=widget->parent;return widget;}
static boolean ui_widget_port_dispatch_event(struct widget_instance *widget,short type,short controller,boolean *deleted){(void)widget;(void)deleted;assert(type==32&&controller==2);top_dispatch++;return TRUE;}
static void error(int priority,char const *text,...){(void)priority;(void)text;}
static void console_warning(char const *text,...){(void)text;}
static boolean pc_menu_tag(long index){return index==200;}
static boolean main_menu_is_active(void){return FALSE;}
static boolean pc_menu_event_function_invoke(struct widget_instance *widget,struct event_record *event,long function,boolean *deleted){(void)widget;(void)event;(void)function;(void)deleted;port_count++;return TRUE;}
static boolean legacy_function(struct widget_instance *widget,struct event_record *event,boolean *deleted){(void)widget;(void)event;(void)deleted;legacy_count++;return TRUE;}
static struct {boolean (*functions[102])(struct widget_instance *,struct event_record *,boolean *);char const *names[102];} event_handler_function_list;
static boolean hs_evaluate_by_name(char const *name){assert(!strcmp(name,"my_action"));script_count++;return TRUE;}
static void widget_instance_give_focus_by_tag(struct widget_instance *widget,long tag,short controller){(void)widget;(void)tag;(void)controller;}
static void widget_instance_reload_recursive(struct widget_instance *widget){(void)widget;}
static void ui_widget_reload_by_tag(long tag){(void)tag;}
static struct widget_instance *widget_instance_find_by_tag_index(long tag){(void)tag;return NULL;}
static void ui_widget_delete(struct widget_instance *widget){int i;assert(widget!=released_widget);deleted_count++;for(i=0;i<4;i++)if(widget_globals.active_widgets[i]==widget)widget_globals.active_widgets[i]=NULL;if(free_widget_on_delete){released_widget=widget;memset(widget,0xDD,sizeof(*widget));free(widget);}}
static boolean ui_widget_launch_widget(struct widget_instance *widget,long tag){assert(widget!=released_widget);opened_count++;opened_tag=tag;return TRUE;}
static struct widget_instance *ui_widget_load_by_name_or_tag(char const *name,long tag,struct widget_instance *widget,short controller,long a,long b,long c){(void)name;(void)tag;(void)widget;(void)controller;(void)a;(void)b;(void)c;return NULL;}
static void widget_instance_go_back_to_previous(struct widget_instance *widget){assert(widget!=released_widget);}
static void unspatialized_impulse_sound_new(long tag,float volume){(void)tag;(void)volume;}
static void pop_widget(void **stack,struct widget_stack_data *data){(void)data;*stack=NULL;}
static void ui_play_audio_feedback_sound(long sound){(void)sound;}
''' + structure(ui, "mcc_ui_widget_prefix") + '\n' + '\n'.join(function(ui, name) for name in names) + '\n' + \
        '\n'.join(function(events, name) for name in ["network_game_remove_local_player", "ui_widget_function_denied",
                                                      "ui_widget_event_handler_function_invoke"]) + '\n' + \
        function(native, "event_handler_dispatch") + r'''
struct mcc_runtime {unsigned char *tag_index;uint32_t used;struct {uint32_t tag_count;}report;};
static unsigned char metadata[0x400];
static void *mcc_runtime_pointer(struct mcc_runtime *r,uint32_t address,uint32_t bytes){(void)r;return address<=sizeof(metadata)&&bytes<=sizeof(metadata)-address?metadata+address:NULL;}
static int mcc_hud(struct mcc_runtime *r,uint32_t group,unsigned char *p){(void)r;(void)group;(void)p;return TRUE;}
''' + '\n'.join(function(tags, name) for name in ["mcc_word", "mcc_block", "mcc_tags_prepare"]) + r'''
#define CHECK(c,n) do{if(!(c)){fprintf(stderr,"check %d failed\n",n);return n;}}while(0)
static boolean fire(struct widget_instance *widget,short button,struct ui_widget_event_handler_reference *handler){
    boolean deleted=FALSE;struct event_record event={1,2};struct ui_widget_definition definition={0};
    if(button==handler->event_type)event_handler_dispatch(widget,&definition,&event,handler,&deleted);
    return deleted;
}
int main(int argc,char **argv){
    struct widget_instance widget={100,"button",2};int i;
    struct ui_widget_event_handler_reference mouse={128,28,108,{NONE},{NONE},{0}};
    struct ui_widget_event_handler_reference open={8,0,0,{101},{NONE},{0}};
    struct ui_widget_event_handler_reference action={132,0,11,{NONE},{NONE},{0}};
    for(i=0;i<102;i++)event_handler_function_list.functions[i]=legacy_function;
    event_handler_function_list.functions[72]=network_game_remove_local_player;
    widget_globals.active_widgets[2]=&widget;
    if(argc!=2)return 99;
    if(!strcmp(argv[1],"metadata")){
        unsigned char entries[32]={0},before[0x400];struct mcc_runtime r={entries,sizeof(metadata),{1}};
        uint32_t group='DeLa',address=0,count=2,events=0x100;
        memcpy(entries,&group,4);memcpy(entries+20,&address,4);
        memcpy(metadata+0x54,&count,4);memcpy(metadata+0x58,&events,4);
        memset(metadata+0x100,0xA5,0x90);memcpy(before,metadata,sizeof(before));
        CHECK(mcc_tags_prepare(&r),1);CHECK(!memcmp(before,metadata,sizeof(before)),2);
        events=0x3FF;memcpy(metadata+0x58,&events,4);CHECK(!mcc_tags_prepare(&r),3);return 0;
    }
    if(!strcmp(argv[1],"confirmation")){
        CHECK(!fire(&widget,28,&mouse)&&posted_button==0&&posted_controller==2,4);
        CHECK(!revert_count&&!opened_count&&!deleted_count,5);
        CHECK(fire(&widget,posted_button,&open)&&opened_count==1&&opened_tag==101,6);
        CHECK(!revert_count,7);
        CHECK(fire(&widget,0,&action)&&revert_count==1&&deleted_count==1,8);return 0;
    }
    if(!strcmp(argv[1],"client-denial")||!strcmp(argv[1],"no-checkpoint")){
        if(!strcmp(argv[1],"client-denial")){connection=1;coop=TRUE;}else saved=FALSE;
        CHECK(!fire(&widget,0,&action)&&message_count==1,9);
        CHECK(!revert_count&&!deleted_count&&!opened_count,10);
        action.function=12;connection=1;
        CHECK(!fire(&widget,0,&action)&&message_count==2&&!reset_count&&!restart_count,11);return 0;
    }
    if(!strcmp(argv[1],"restart")){
        action.function=12;CHECK(fire(&widget,0,&action)&&reset_count==1,12);
        connection=2;coop=TRUE;widget_globals.active_widgets[2]=&widget;
        CHECK(fire(&widget,0,&action)&&restart_count==1&&reset_count==1,13);
        action.function=163;coop=FALSE;widget_globals.active_widgets[2]=&widget;
        CHECK(fire(&widget,0,&action)&&restart_count==2,14);return 0;
    }
    if(!strcmp(argv[1],"quit")){
        action.function=13;action.flags=128;CHECK(!fire(&widget,0,&action)&&menu_count==1&&persist_count==1,15);
        coop=TRUE;connection=1;fire(&widget,0,&action);CHECK(quit_count==4&&persist_count==1,16);
        coop=FALSE;action.function=72;fire(&widget,0,&action);CHECK(quit_count==5&&quit_controller==2,17);return 0;
    }
    if(!strcmp(argv[1],"team")){
        connection=1;game.variant.universal_variant.teams=TRUE;game.players[0]=(struct network_player){1,1,2,0};
        game.players[1]=(struct network_player){1,2,2,1};action.function=157;
        CHECK(fire(&widget,0,&action)&&team_sent==1&&sent_player.team_index==1&&sent_player.machine_index==2,18);
        widget_tag_name="ui\\shell\\multiplayer_game\\pause_game\\red_team_button";
        fire(&widget,0,&action);CHECK(team_sent==2&&sent_player.team_index==0,19);
        game.variant.universal_variant.teams=FALSE;CHECK(!fire(&widget,0,&action)&&message_count==1,20);return 0;
    }
    if(!strcmp(argv[1],"settings")){
        action.function=137;action.flags=136;action.widget_tag.index=300;
        CHECK(fire(&widget,0,&action)&&settings_open==1&&profile_begin==1,21);
        CHECK(!opened_count,22);have_settings=FALSE;
        CHECK(!fire(&widget,0,&action)&&message_count==1&&settings_open==1,23);
        have_settings=TRUE;settings_collision=TRUE;
        CHECK(!fire(&widget,0,&action)&&profile_begin==1&&settings_open==1&&message_count==2,36);
        CHECK(mcc_ui_settings_needed("a10"),24);mcc=FALSE;CHECK(!mcc_ui_settings_needed("a10"),25);return 0;
    }
    if(!strcmp(argv[1],"save")){
        action.function=179;fire(&widget,0,&action);CHECK(save_count==1&&persist_count==1,26);
        coop=TRUE;connection=2;fire(&widget,0,&action);CHECK(save_count==2&&persist_count==1,27);
        connection=1;CHECK(!fire(&widget,0,&action)&&save_count==2&&message_count==1,28);return 0;
    }
    if(!strcmp(argv[1],"team-balance")){
        connection=1;game.variant.universal_variant.teams=TRUE;balance_allowed=FALSE;action.function=157;
        CHECK(!fire(&widget,0,&action)&&message_count==1&&!team_sent&&!deleted_count,41);return 0;
    }
    if(!strcmp(argv[1],"new-game")){
        struct widget_instance *heap_widget=malloc(sizeof(*heap_widget));
        CHECK(heap_widget!=NULL,42);*heap_widget=widget;
        widget_tag_name="ui\\shell\\multiplayer_game\\pause_game\\new_game_button";
        fire(&widget,28,&mouse);CHECK(posted_button==0,37);
        /* The actual engine synchronously frees the whole tree. Pending
         * close/go-back flags must not touch it again after MCC handles it. */
        open.flags|=(1u<<_event_handler_close_current_widget_bit)|(1u<<_event_handler_go_back_to_previous_widget_bit);
        connection=2;widget_globals.active_widgets[2]=heap_widget;free_widget_on_delete=TRUE;
        CHECK(fire(heap_widget,posted_button,&open)&&end_count==1&&!opened_count&&deleted_count==1,38);
        CHECK(!widget_globals.active_widgets[2]&&released_widget==heap_widget,43);
        free_widget_on_delete=FALSE;widget_globals.active_widgets[2]=&widget;
        connection=1;CHECK(!fire(&widget,0,&open)&&end_count==1&&message_count==1&&!opened_count&&deleted_count==1,39);
        connection=2;round_active=FALSE;
        CHECK(!fire(&widget,0,&open)&&end_count==1&&message_count==2&&deleted_count==1,44);
        open.flags=8;
        mcc=FALSE;fire(&widget,0,&open);CHECK(opened_count==1&&end_count==1,40);return 0;
    }
    if(!strcmp(argv[1],"isolation")){
        mcc=FALSE;action.flags=128;fire(&widget,0,&action);CHECK(legacy_count==1&&!revert_count,29);
        mcc=TRUE;widget.definition_tag_index=101;fire(&widget,0,&action);CHECK(legacy_count==2&&!revert_count,30);
        widget.definition_tag_index=200;action.function=256;fire(&widget,0,&action);CHECK(port_count==1,31);
        widget.definition_tag_index=100;fire(&widget,0,&action);CHECK(port_count==1,32);
        action.function=67;fire(&widget,0,&action);CHECK(legacy_count==2,33);return 0;
    }
    if(!strcmp(argv[1],"unsupported")){
        action.function=190;action.flags=140;action.widget_tag.index=300;
        CHECK(!fire(&widget,0,&action)&&message_count==1&&!opened_count&&!deleted_count,34);return 0;
    }
    if(!strcmp(argv[1],"script")){
        action.function=0;action.flags=1024;strcpy(action.script,"my_action");fire(&widget,0,&action);
        CHECK(script_count==1&&!revert_count&&!message_count,35);return 0;
    }
    return 98;
}
'''
    work = tmp_path_factory.mktemp("mcc-ui")
    path = work / "ui.c"
    path.write_text(source)
    executable = work / ("ui.exe" if os.name == "nt" else "ui")
    command = [compiler, "-std=c99", str(path), "-o", str(executable)]
    if os.name == "nt":
        command[1:1] = ["--target=i686-pc-windows-msvc", "-fuse-ld=lld", "-D_CRT_SECURE_NO_WARNINGS"]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return executable


@pytest.mark.parametrize("case", ["metadata", "confirmation", "client-denial", "no-checkpoint", "restart",
                                 "quit", "team", "team-balance", "settings", "save", "new-game", "isolation", "unsupported", "script"])
def test_mcc_widget_events(ui_tool, case):
    result = subprocess.run([str(ui_tool), case], capture_output=True, text=True)
    assert result.returncode == 0, (case, result.returncode, result.stdout, result.stderr)
