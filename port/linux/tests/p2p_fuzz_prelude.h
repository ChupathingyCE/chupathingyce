/*
P2P_FUZZ_PRELUDE.H

What the p2p fuzz targets (tools/test_host_fuzz.py) take in place of the
port's platform.h (which needs the Xbox SDK's headers), and of posix.h too
where the target is built with __HALO_LINUX_POSIX_H defined: the few
functions the units call, declared. The harnesses define them.
*/

#include <sys/socket.h>
#include <netinet/in.h>

void platform_log(const char *format, ...);

#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036

#ifdef __HALO_LINUX_POSIX_H

void posix_random_bytes(void *buffer, unsigned long size);
int posix_socket_last_error(void);
int posix_socket(int family, int type, int protocol);
int posix_socket_close(int socket);
int posix_socket_connect(int socket, const void *address, int address_length);
int posix_socket_send(int socket, const void *buffer, int length, int flags);
int posix_socket_recv(int socket, void *buffer, int length, int flags);
int posix_socket_set_nonblocking(int socket, int nonblocking);
#else
unsigned long GetTickCount(void);
void Sleep(unsigned long milliseconds);
#endif
