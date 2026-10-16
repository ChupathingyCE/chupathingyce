/*
DELTA_CHAT_WORDS.C

The blocked names list (server/blocked_names.txt) built into the game, for the
chat filter of every copy (delta_chat.h): its entries alone, in ROT13 as there.
tools/test_delta_peer.py checks the two are the same: change the list there,
then this.
*/

#include "delta_chat.h"

const char delta_chat_blocked_words[] =
	"anywhere avtt\n"
	"anywhere avtn\n"
	"anywhere avte\n"
	"anywhere avtre\n"
	"anywhere aytt\n"
	"anywhere avdd\n"
	"anywhere snttbg\n"
	"anywhere sntbg\n"
	"anywhere sntt\n"
	"anywhere genaal\n"
	"anywhere ergneq\n"
	"anywhere puvax\n"
	"anywhere jrgonpx\n"
	"anywhere ornare\n"
	"anywhere enturnq\n"
	"anywhere gbjryurnq\n"
	"anywhere uvgyre\n"
	"anywhere anmv\n"
	"anywhere xxx\n"
	"anywhere encvfg\n"
	"anywhere crqbcuvyr\n"
	"anywhere zbyrfg\n"
	"anywhere juvgrcbjre\n"
	"anywhere fvrturvy\n"
	"anywhere xvyywrj\n"
	"anywhere wrjxvyy\n"
	"anywhere tnfgurwrj\n"
	"whole avt\n"
	"whole avtf\n"
	"whole snt\n"
	"whole sntf\n"
	"whole fcvp\n"
	"whole fcvpf\n"
	"whole pbba\n"
	"whole pbbaf\n"
	"whole tbbx\n"
	"whole tbbxf\n"
	"whole wnc\n"
	"whole wncf\n"
	"whole encr\n"
	"whole crqb\n"
	"whole gneq\n"
	"allow avtrevn\n"
	"allow avtrevna\n"
	"allow avvtngn\n"
	"allow avtngn\n"
	"allow favttre\n"
	"allow gurencvfg\n"
	"allow ergneqnag\n"
	"allow anmve\n"
	"allow anmven\n";
