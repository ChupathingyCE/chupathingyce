/* PTICK.H (Halo 2 mode)
	(port) What Halo 2 mode's units used from Project Ark's parallel tick
	(ptick.h): this engine ticks on one thread, so nothing speculates and no
	guard is ever taken. */
#ifndef H2_PTICK_COMPAT_H
#define H2_PTICK_COMPAT_H

enum { _ptick_abort_other = 0 };
#define halo_ptick_speculating 0
#define HALO_PTICK_SPECULATING() 0
#define halo_ptick_guard(reason, name) 0
#define HALO_PTICK_GUARDED(reason, name) 0
#define halo_ptick_in_tree(object_index) 0
#define halo_ptick_read_note(object_index) ((void)0)
#define HALO_PTICK_READ(index) ((void)0)

#endif
