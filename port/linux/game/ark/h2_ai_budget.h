#ifndef H2_AI_BUDGET_H
#define H2_AI_BUDGET_H

/* Halo 2's per-tick AI scheduling (port/linux/game/h2_ai_budget.c) */

/* the rules are on: a Halo 2 level and HALO_H2_AI_BUDGET != 0 (re-read at @set) */
boolean h2_ai_budget_active(void);
/* a new map */
void h2_ai_budget_reset(void);
/* the start of actors_update: begin (the second slot's threshold moves on,
0x1e2f50/0x1e3790), note each actor to be updated in its order, assign (the
perception slice to the next 5 actors, 0x1e2f50) */
void h2_ai_budget_tick_begin(void);
void h2_ai_budget_tick_note_actor(long actor_index);
void h2_ai_budget_tick_assign(void);
/* this actor has the perception slice this tick: its one prop status refresh runs */
boolean h2_ai_budget_perception_slice(long actor_index);
/* in actor_update, where the timeslice is given: the second slot, the
prop set re-pick (0x1e3790's second counter) */
boolean h2_ai_budget_prop_slot(long actor_index);
/* whether this actor took the second slot this tick */
boolean h2_ai_budget_prop_slot_given(long actor_index);

#endif
