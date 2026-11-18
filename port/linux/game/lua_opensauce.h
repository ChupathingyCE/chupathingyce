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

/* whether a script function or global (execute_script's first word) is
OpenSauce's, when the map is OpenSauce's: logged once as not here yet */
boolean lua_opensauce_script_word(char const *script, char const *word);

#endif

#endif
