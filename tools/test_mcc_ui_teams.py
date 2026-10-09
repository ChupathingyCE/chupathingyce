"""Exercise the real network settings handler and MCC's team-only ingress."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest
from harness import function

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def teams_tool(tmp_path_factory):
    compiler = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not compiler:
        pytest.skip("a C compiler is needed")
    own = (ROOT / "port/linux/game/mcc_ui_teams.c").read_text()
    native = (ROOT / "source/networking/network_server_message_handler.c").read_text()
    source = r'''
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
typedef int boolean;
typedef uint16_t word;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define NUMBEROF(a) (sizeof(a)/sizeof(*(a)))
#define VALID_INDEX(i,n) ((unsigned long)(i)<(unsigned long)(n))
#define MAXIMUM_NUMBER_OF_PLAYERS 4
#define NUMBER_OF_MULTIPLAYER_TEAMS 2
#define HALO_PORT_NETWORK_GAME_MESSAGE_VERSION 12
#define NETWORK_GAME_MESSAGE_VERSION HALO_PORT_NETWORK_GAME_MESSAGE_VERSION
#define _message_client_player_settings_request 16
#define _network_game_packet_class_client_pregame 3
struct network_player {uint16_t name[12];short color,icon;char machine_index,controller_index,team_index,player_list_index;};
struct network_game {struct {char name[128];} map;struct {struct {int teams;}universal_variant;}variant;struct network_player players[4];int machines[4];};
struct network_game_server {int state;struct network_game game;}server;
struct network_game_server_client_machine {int index;}machine;
struct player_datum {int quit_out_of_game,team_index,unit_index;struct network_player network_player_data;}players[4];
struct data_iterator {int i;};
struct {int auto_team_balance;}options;
static int player_data,mcc=1,bad_decode,legacy,send_count,kill_count,killed,decoded;
static struct network_player request;
static struct {int type;}scenario={1};
#define global_scenario (&scenario)
static boolean mcc_cache_tags_loaded(void){return mcc;}
static int network_game_server_get_state(struct network_game_server *s,void *unused){(void)unused;return s->state;}
static struct network_game *network_game_server_get_game(struct network_game_server *s){return &s->game;}
static boolean mcc_level_name(char const *s){return !strncmp(s,"mcc_maps\\",9);}
static boolean decode_network_game_message(void *out,void const *wire,short *size,short *type,short *version,int cls){
 (void)wire;(void)size;decoded++;if(*type!=16||*version!=12||cls!=3)exit(80);memcpy(out,&request,sizeof(request));return !bad_decode;
}
static void *network_game_server_get_client_machine(struct network_game_server *s,struct network_game_server_client_machine *m,long *index){(void)s;*index=m->index;return m->index==-1?NULL:m;}
static boolean network_player_is_valid(struct network_player *p){return p->machine_index>=0;}
static void data_iterator_new(struct data_iterator *it,int data){(void)data;it->i=0;}
static void *data_iterator_next(struct data_iterator *it){return it->i<4?&players[it->i++]:NULL;}
static void *game_variant_options_get(void);
#define game_variant_options_get() (&options)
static void unit_kill_no_statistics(long unit){kill_count++;killed=(int)unit;}
static void network_event(char const *fmt,...){(void)fmt;}
static boolean network_game_server_lobby_is_open(struct network_game_server *s){legacy++;return s->state==0;}
static void network_game_server_clean_player_name(struct network_game_server *s,struct network_player *p,long slot){(void)s;(void)p;(void)slot;}
static boolean network_game_update_player(struct network_game *g,struct network_player *p){g->players[p->player_list_index]=*p;return TRUE;}
static boolean network_game_server_send_game_data_pregame(struct network_game_server *s){(void)s;send_count++;return TRUE;}
''' + function(own, "mcc_ui_team_balance_allows") + '\n' + function(own, "mcc_ui_team_request") + '\n' + function(
        native, "network_game_server_handle_message_client_player_settings_request") + r'''
#define CHECK(c,n) do{if(!(c)){fprintf(stderr,"check%d failed\n",n);return n;}}while(0)
int main(int argc,char **argv){
    word wire[18]={16};int i;short size=sizeof(wire);struct network_game before;struct player_datum prior[4];
    char const *test=argc>1?argv[1]:"";
    ((unsigned char *)wire)[size-1]=16;
    strcpy(server.game.map.name,"mcc_maps\\dangercanyon");server.game.variant.universal_variant.teams=TRUE;server.state=1;machine.index=2;
    for(i=0;i<4;i++){server.game.players[i].machine_index=-1;players[i].quit_out_of_game=TRUE;players[i].unit_index=NONE;}
    request=(struct network_player){{'A',0},2,3,2,1,0,1};server.game.players[1]=request;
    players[1].quit_out_of_game=FALSE;players[1].team_index=0;players[1].unit_index=0x10001;players[1].network_player_data=request;
    request.team_index=1;
    if(!strcmp(test,"non-mcc"))mcc=0;
    if(!strcmp(test,"non-namespace"))strcpy(server.game.map.name,"custom_maps\\x");
    if(!strcmp(test,"pregame"))server.state=0;
    if(!strcmp(test,"postgame"))server.state=2;
    if(!strcmp(test,"no-teams"))server.game.variant.universal_variant.teams=FALSE;
    if(!strcmp(test,"campaign"))scenario.type=0;
    if(!strcmp(test,"unjoined"))machine.index=-1;
    if(!strcmp(test,"malformed"))bad_decode=1;
    if(!strcmp(test,"short"))size=1;
    if(!strcmp(test,"negative"))size=-1;
    if(!strcmp(test,"minimum"))size=-32768;
    if(!strcmp(test,"oversized"))size=128;
    if(!strcmp(test,"wrong-packet"))((unsigned char *)wire)[size-1]=15;
    if(!strcmp(test,"machine"))machine.index=3;
    if(!strcmp(test,"controller"))request.controller_index=2;
    if(!strcmp(test,"bad-team"))request.team_index=2;
    if(!strcmp(test,"bad-slot"))request.player_list_index=-1;
    if(!strcmp(test,"unknown-slot"))request.player_list_index=2;
    if(!strcmp(test,"name"))request.name[0]='B';
    if(!strcmp(test,"color"))request.color=4;
    if(!strcmp(test,"icon"))request.icon=4;
    if(!strcmp(test,"unknown-live"))players[1].network_player_data.controller_index=2;
    if(!strcmp(test,"already-team"))request.team_index=0;
    if(!strcmp(test,"dead"))players[1].unit_index=NONE;
    if(!strcmp(test,"balance")){options.auto_team_balance=TRUE;players[0].quit_out_of_game=FALSE;players[0].team_index=1;}
    before=server.game;memcpy(prior,players,sizeof(prior));
    CHECK(network_game_server_handle_message_client_player_settings_request(&server,&machine,wire,size),1);
    if(!strcmp(test,"valid")||!strcmp(test,"dead")){
        CHECK(!legacy&&server.game.players[1].team_index==1&&players[1].team_index==1&&players[1].network_player_data.team_index==1,2);
        CHECK(kill_count==(!strcmp(test,"valid")?1:0),3);if(kill_count)CHECK(killed==0x10001,4);return 0;
    }
    if(!strcmp(test,"pregame")){CHECK(legacy==1&&send_count==1&&!kill_count,5);return 0;}
    CHECK(!memcmp(&before,&server.game,sizeof(before))&&!memcmp(prior,players,sizeof(prior))&&!kill_count,6);
    if(!strcmp(test,"non-mcc")||!strcmp(test,"non-namespace")||!strcmp(test,"postgame"))CHECK(legacy==1,7);
    else CHECK(!legacy,8);
    return 0;
}
'''
    work = tmp_path_factory.mktemp("mcc-ui-teams")
    path = work / "teams.c"
    path.write_text(source)
    executable = work / ("teams.exe" if os.name == "nt" else "teams")
    command = [compiler, "-std=c99", str(path), "-o", str(executable)]
    if os.name == "nt":
        command[1:1] = ["--target=i686-pc-windows-msvc", "-fuse-ld=lld", "-D_CRT_SECURE_NO_WARNINGS"]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return executable


@pytest.mark.parametrize("case", ["valid", "dead", "non-mcc", "non-namespace", "pregame", "postgame", "no-teams",
    "campaign", "unjoined", "malformed", "short", "negative", "minimum", "oversized", "wrong-packet", "machine", "controller", "bad-team", "bad-slot", "unknown-slot", "name", "color", "icon",
    "unknown-live", "already-team", "balance"])
def test_mcc_ingame_team_request(teams_tool, case):
    result = subprocess.run([str(teams_tool), case], capture_output=True, text=True)
    assert result.returncode == 0, (case, result.returncode, result.stdout, result.stderr)
