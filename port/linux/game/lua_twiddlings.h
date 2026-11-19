/*
LUA_TWIDDLINGS.H

Twiddlings: Chimera Lua scripts' raw memory reads and writes, which name
Halo PC's addresses, translated to this game's memory (lua_twiddlings.c).
Nothing a script names is ever read or written as it is: an address is
resolved through the twiddlings table to what it is here, or refused.

SPDX-FileCopyrightText: 2026 The Chupathingy contributors
SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef LUA_TWIDDLINGS_H
#define LUA_TWIDDLINGS_H

#ifdef HALO_LUA

/* size bytes (1, 2, 4 or 8) at a Halo PC address into value, little
endian as Halo PC has them; FALSE when the address is not one Twiddlings
knows (value zeroed, logged once: "twiddlings: unknown read at ...") */
boolean twiddlings_read(unsigned int address, void *value, unsigned int size);
/* size bytes from value to a Halo PC address; FALSE when it is refused
(logged once: "twiddlings: write refused at ...") */
boolean twiddlings_write(unsigned int address, void const *value, unsigned int size);
/* a NUL-terminated string at a Halo PC address, at most size - 1
characters; "" and FALSE when unknown */
boolean twiddlings_read_string(unsigned int address, char *text, unsigned int size);
/* a string and its NUL written at a Halo PC address; FALSE when refused */
boolean twiddlings_write_string(unsigned int address, char const *text);

/* the Halo PC address of a tag's instance in the tag index (Chimera's
get_tag), by the tag's handle; 0 for none. Custom Edition maps' tags are
at Halo PC's addresses here, so this is the address itself */
unsigned int twiddlings_tag_instance_address(long tag_index);
/* the same, by group and path (the path's case ignored); 0 for none */
unsigned int twiddlings_tag_instance_address_by_name(unsigned long group_tag, char const *path);

/* the stand-in address a script is given for an object (get_object), by
its handle or its index alone: 0 for none (lua_twiddlings_objects.c) */
unsigned int twiddlings_object_address(long object_index);
/* the same for a player (get_player), by handle or index */
unsigned int twiddlings_player_address(long player_index);
/* the player whose place in the player list (Chimera's rcon id) is this,
as a handle; NONE for none */
long twiddlings_player_by_list_index(long list_index);
/* get_player(index)'s address: that player's, or with none there yet, the
slot's, which reads as that player once one is there (as the Chimera SPV3
was written for gave a script that asks at the menus) */
unsigned int twiddlings_player_slot_address(long list_index);

/* (lua_twiddlings.c's) an access at the objects', the players' or the
object table's addresses: -1 if the address is none of theirs, else whether
it was done (FALSE: refused, logged) */
int twiddlings_objects_access(boolean write, unsigned int address, void *value, unsigned int size);

/* haloce.exe's script functions an execute_script may name (Halo PC's
input preferences scripts use as storage), numbered from 0
(lua_scripting_hs_function): NULL and NONE past them */
struct hs_function_definition *twiddlings_hs_function(short index);
short twiddlings_hs_function_find(char const *name);

#endif

#endif
