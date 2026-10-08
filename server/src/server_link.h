/*
SERVER_LINK.H

Delta Control's link to halo.milenko.org (control_link.c; docs/delta.md,
Delta Control): optional, off until the server's owner links it (sv_link),
and never at all with HALO_DEDICATED_LINK=false. The server dials out (no
port to open); the site relays commands of the accounts its owner gave a
role, and the server's own checks decide. Plain types: the game's units
call it.
*/

#ifndef SERVER_LINK_H
#define SERVER_LINK_H

/* the link's thread started, if the server is linked (a credential kept in
the data folder) and the link is not turned off. Once, at start */
void server_link_begin(void);

/* sv_link: a code to enter on the site (in text, text_size bytes, with what
to do with it): 1, else 0 and why */
int server_link_start(const char *server_name, const char *version, char *text, int text_size);
/* sv_unlink: the site told, the credential deleted (the site told first,
when it can be; the credential goes either way): 1, else 0 and why */
int server_link_stop(char *text, int text_size);
/* sv_link_status: whether it is linked, to whom, when it last heard from
the site (JSON when json) */
void server_link_status(int json, char *text, int text_size);

/* (the main thread's, each frame) the site's commands waiting, one at a
time: 1 and the command, its id and the handle it came from, else 0; and
its answer, once run */
int server_link_next_command(char *line, int line_size, char *handle, int handle_size, unsigned int *id);
void server_link_finish_command(unsigned int id, int ok, const char *output);
/* (the main thread's, now and then) the server's state for the site: a JSON
object */
void server_link_set_status(const char *json);

#endif
