/*
TELNET_CONSOLE.H

header included in hcex build.
*/

#ifndef __TELNET_CONSOLE_H
#define __TELNET_CONSOLE_H
#pragma once

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes/TELNET_CONSOLE.C */

#ifdef HALO_CONSOLE_NO_TELNET
/* port: the consoles' builds have no script console (telnet_console.c is
not built; the Xbox plays on its own, with no 127.0.0.1 to reach it by) */
#define telnet_console_initialize() ((void)0)
#define telnet_console_dispose() ((void)0)
#define telnet_console_print(string) ((void)0)
#define telnet_console_process() ((void)0)
#else
void telnet_console_initialize(
	void);
void telnet_console_dispose(
	void);
void telnet_console_print(
	char *string);
void telnet_console_process(
	void);
#endif

/* ---------- globals */

/* ---------- public code */

#endif // __TELNET_CONSOLE_H
