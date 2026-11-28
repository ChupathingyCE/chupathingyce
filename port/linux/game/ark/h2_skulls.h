/*
H2_SKULLS.H

(port) Halo 2's skulls, Bungie's "ice cream flavors" (h2_skulls.c).
*/

#ifndef H2_SKULLS_H
#define H2_SKULLS_H

enum
{
	_h2_skull_envy = 0,
	_h2_skull_grunt_birthday_party,
	_h2_skull_assassins,
	_h2_skull_thunderstorm,
	_h2_skull_famine,
	_h2_skull_iwhbyd,
	_h2_skull_blind,
	_h2_skull_ghost,
	_h2_skull_black_eye,
	_h2_skull_catch,
	_h2_skull_sputnik,
	_h2_skull_iron,
	_h2_skull_mythic,
	_h2_skull_anger,
	_h2_skull_whuppopotamus,
	NUMBER_OF_H2_SKULLS
};

/* on, on a Halo 2 level: found there, or chosen on the Campaign screen */
boolean h2_skull_on(short skull);
/* (ice_cream_flavor_stock) found: on from now, a white flash and its sound */
void h2_skull_found(short skull);
/* a game mode's skulls (1 << _h2_skull_*), on any level while set; 0 none */
void h2_skulls_mode_set(unsigned long mask);
/* (ice_cream_flavor_available) */
boolean h2_skull_available(short skull);
/* each game tick of a Halo 2 level */
void h2_skulls_update(void);
/* the Campaign screen's row */
char const *h2_skull_name(short skull);
boolean h2_skull_chosen(short skull);
void h2_skull_choose(short skull, boolean on);
short h2_skulls_chosen_count(void);

#endif
