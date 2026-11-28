/*
OPENSAUCE_SURVEY.H

The log of an OpenSauce map (port/linux/game/opensauce_survey.c): the
survey of a map as it is checked, what its scripts name that the game
lacks, its resource misses and the loader's corrections. It runs for any
map with OpenSauce's header, and for every Custom Edition map in SPV3 mode
(spv3_mode.c), as it always did there. Its lines start "opensauce:", and
"spv3:" in SPV3 mode, so SPV3's reports read as before.

A console's build reads no OpenSauce map (opensauce_map.c): there the calls
are empty.
*/

#ifndef OPENSAUCE_SURVEY_H
#define OPENSAUCE_SURVEY_H

#if defined(HALO_CUSTOM_EDITION) && !defined(HALO_CONSOLE) && !defined(HALO_XBOX_CONSOLE)

#define OPENSAUCE_SURVEY

/* whether the log is on: in SPV3 mode, and while an OpenSauce map is
checked or loaded */
boolean opensauce_survey_active(void);

/* a line in debug.txt, "opensauce: ..." ("spv3: ..." in SPV3 mode), while
the log is on */
void opensauce_survey_log(char const *format, ...);

/* the survey of a map just read, before it is checked (ce_map_checks.c):
its header, OpenSauce's header, its tags' groups, its BSPs against the
window and its scripts' functions and globals the game lacks; once per map,
for a map with OpenSauce's header (any map in SPV3 mode). tags is the tag
data as read, at the tag cache's base. A map without the header outside
SPV3 mode is not logged, and ends the last one's survey */
void opensauce_survey_map(char const *map_name, void const *cache_header, void const *tags,
	unsigned long tag_data_size, unsigned long file_size);

/* whether a cache's header (its first 0x800 bytes) is one the survey logs:
OpenSauce's header in it, or SPV3 mode */
boolean opensauce_survey_wanted(void const *cache_header);

/* the map's checks done (ce_map_checks.c): refusal is why it was refused,
logged, or NULL */
void opensauce_survey_checked(char const *map_name, char const *refusal);

/* a script's function or global the game lacks, named as the scripts were
compiled (hs_compile.c), counted for the map's summary */
void opensauce_survey_note_missing_function(char const *name);
void opensauce_survey_note_missing_global(char const *name);
/* the map's scripts compiled: the counts logged and cleared */
void opensauce_survey_scripts_summary(long dropped_scripts, long scripts, long dropped_globals, long globals);

/* a resource (a bitmap's pixels or a sound's samples) a tag asked a resource
map for that it does not have */
void opensauce_survey_note_resource_miss(char const *kind, char const *tag_name);

/* a change the game made to what an OpenSauce map asks for (a camera point
narrowed, a grenade type passed over): counted and its first ones logged */
void opensauce_survey_note_correction(char const *what);

#else

static __inline boolean opensauce_survey_active(void) { return FALSE; }
static __inline void opensauce_survey_log(char const *format, ...) { (void)format; }
static __inline boolean opensauce_survey_wanted(void const *cache_header)
{
	(void)cache_header;
	return FALSE;
}
static __inline void opensauce_survey_map(char const *map_name, void const *cache_header, void const *tags,
	unsigned long tag_data_size, unsigned long file_size)
{
	(void)map_name;
	(void)cache_header;
	(void)tags;
	(void)tag_data_size;
	(void)file_size;
}
static __inline void opensauce_survey_checked(char const *map_name, char const *refusal)
{
	(void)map_name;
	(void)refusal;
}
static __inline void opensauce_survey_note_missing_function(char const *name) { (void)name; }
static __inline void opensauce_survey_note_missing_global(char const *name) { (void)name; }
static __inline void opensauce_survey_scripts_summary(long dropped_scripts, long scripts, long dropped_globals,
	long globals)
{
	(void)dropped_scripts;
	(void)scripts;
	(void)dropped_globals;
	(void)globals;
}
static __inline void opensauce_survey_note_resource_miss(char const *kind, char const *tag_name)
{
	(void)kind;
	(void)tag_name;
}
static __inline void opensauce_survey_note_correction(char const *what) { (void)what; }

#endif

#endif
