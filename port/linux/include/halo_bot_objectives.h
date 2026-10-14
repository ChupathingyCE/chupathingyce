/* port: copied, host-local objective state; never serialized or retained as
   engine pointers. source/game/game_engine_ctf.c owns the flag rules. */
#ifndef HALO_BOT_OBJECTIVES_H
#define HALO_BOT_OBJECTIVES_H
#include "cseries/cseries.h"
#include "math/real_math.h"
struct game_variant;

enum { HALO_BOT_CTF_TEAMS = 2 };
enum halo_bot_flag_state
{
    _halo_bot_flag_missing = 0,
    _halo_bot_flag_home,
    _halo_bot_flag_dropped,
    _halo_bot_flag_carried
};
struct halo_bot_ctf_flag
{
    short state;
    long object_index;
    long carrier_player_index;
    real_point3d stand;
    /* Only uncarried positions: this API does not reveal hidden carriers. */
    real_point3d position;
};
struct halo_bot_ctf_snapshot
{
    boolean can_score;
    boolean flag_at_home_to_score;
    long scores[HALO_BOT_CTF_TEAMS]; /* engine-awarded captures, for live validation */
    /* Engine event counters wrap as bytes; 255 denotes an automatic return. */
    byte return_events[HALO_BOT_CTF_TEAMS];
    byte returners[HALO_BOT_CTF_TEAMS];
    struct halo_bot_ctf_flag flags[HALO_BOT_CTF_TEAMS];
};
/* Standard two-team/two-flag CTF only. No Assault or manual/single-flag rules. */
boolean game_engine_ctf_bot_variant_supported(struct game_variant const *variant);
/* FALSE leaves an empty snapshot. Datums and positions are checked each call. */
boolean game_engine_ctf_bot_snapshot(struct halo_bot_ctf_snapshot *snapshot);
#endif
