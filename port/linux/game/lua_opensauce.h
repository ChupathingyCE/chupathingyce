/*
LUA_OPENSAUCE.H

What Lua scripts written for maps built on OpenSauce (SPV3's) see of
OpenSauce (lua_opensauce.c), kept apart from Chimera's API and from
Twiddlings' Halo PC: active only while the loaded map is one built for
OpenSauce, so other maps and their scripts never meet any of it.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef LUA_OPENSAUCE_H
#define LUA_OPENSAUCE_H

#ifdef HALO_LUA

/* whether the loaded map was built for OpenSauce (its cache header carries
OpenSauce's, as ce_map_checks.c reads it) */
boolean lua_opensauce_map(void);

/* Twiddlings' OpenSauce section: an access at a haloce.exe address that is
what it is because OpenSauce runs (the f0 to f5 script globals' storage):
-1 if the address is not one, else whether it was done. Always -1 when the
map is not OpenSauce's */
int twiddlings_opensauce_access(boolean write, unsigned int address, void *value, unsigned int size);

/* execute_script's expression whose first word the game's scripts don't
have: when the map is OpenSauce's and the word is one of OpenSauce's
post-processing globals, it is printed or set ("pp_motion_blur_enabled 1")
and TRUE; one of OpenSauce's functions not here yet is logged once and
TRUE; else FALSE */
boolean lua_opensauce_script_word(char const *script, char const *word, char const *expression);

/* OpenSauce's script functions here, numbered from 0
(lua_scripting_hs_function): NULL past them. Found by name only on an
OpenSauce map: NONE elsewhere */
struct hs_function_definition *lua_opensauce_hs_function(short index);
short lua_opensauce_hs_function_find(char const *name);

/* a map loaded: OpenSauce's post-processing state starts over */
void lua_opensauce_map_load(void);
/* a tick: the post processing's fades go on */
void lua_opensauce_tick(void);

#endif

#endif
