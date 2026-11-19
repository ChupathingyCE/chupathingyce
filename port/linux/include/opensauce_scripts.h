/*
OPENSAUCE_SCRIPTS.H

The script functions and globals OpenSauce's maps call that the game lacks
(port/linux/game/opensauce_scripts.c): OpenSauce's own and the Halo PC
ones its maps lean on. Only on a map with an OpenSauce header
(opensauce.h): elsewhere there are none, so no other map's scripts or the
console ever meet them.

Functions are numbered after Halo's and Stubbs mode's (hs.c); globals are
designated after the scenario's own (hs.c, hs_runtime.c), their values kept
in the scripts' globals like the scenario's.
*/

#ifndef OPENSAUCE_SCRIPTS_H
#define OPENSAUCE_SCRIPTS_H

struct hs_function_definition;

#if defined(HALO_CUSTOM_EDITION) && defined(HALO_MAP_LIST) && !defined(HALO_XBOX_CONSOLE)

#define OPENSAUCE_SCRIPTS

/* how many functions there are (none but on an OpenSauce map), one by its
index among them, a name's index (NONE) */
long opensauce_script_function_count(void);
struct hs_function_definition *opensauce_script_function_get(long index);
long opensauce_script_function_find(char const *name);

/* how many globals there are (none but on an OpenSauce map), a name's index
(NONE), and one's type, name and starting value */
short opensauce_script_global_count(void);
short opensauce_script_global_find(char const *name);
short opensauce_script_global_type(short index);
char const *opensauce_script_global_name(short index);
long opensauce_script_global_initial(short index);

/* a map unloaded: what the functions kept starts over */
void opensauce_scripts_map_unloaded(void);

#else

static __inline long opensauce_script_function_count(void) { return 0; }
static __inline struct hs_function_definition *opensauce_script_function_get(long index) { (void)index; return 0; }
static __inline long opensauce_script_function_find(char const *name) { (void)name; return -1; }
static __inline short opensauce_script_global_count(void) { return 0; }
static __inline short opensauce_script_global_find(char const *name) { (void)name; return -1; }
static __inline short opensauce_script_global_type(short index) { (void)index; return 0; }
static __inline char const *opensauce_script_global_name(short index) { (void)index; return ""; }
static __inline long opensauce_script_global_initial(short index) { (void)index; return 0; }
static __inline void opensauce_scripts_map_unloaded(void) { }

#endif

#endif
