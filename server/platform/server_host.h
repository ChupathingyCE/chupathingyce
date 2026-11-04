/*
SERVER_HOST.H

The host system's calls the server's own units make (server_control.c,
control_tls.c, control_link.c, server_roles.c, gamespy_listing.c), by one
set of names on Linux and on Windows: sockets, the console's wake-up,
files written whole, random bytes. On Linux (and macOS, for the tests'
harnesses) each is the POSIX call. On Windows (tools/windows_build.py, the
Windows server) sockets are Winsock's and files the C runtime's, as the
Windows game's platform layer has them (port/windows/src).

Sockets are kept in an int, as on Linux: a Winsock SOCKET is a handle,
which Windows keeps far below 2^31; INVALID_SOCKET is -1 there.
*/

#ifndef __SERVER_HOST_H
#define __SERVER_HOST_H

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#include <process.h>
/* (port/windows/include/posix: ssize_t, the threads and the clocks) */
#include <unistd.h>

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

/* (files are binary already: win32_posix.c sets the C runtime's default) */
#define O_CLOEXEC _O_NOINHERIT
#define MSG_NOSIGNAL 0
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH
#define fsync _commit
#define poll(fds, count, timeout) WSAPoll(fds, count, timeout)

typedef unsigned short mode_t;
typedef ULONG nfds_t;

/* POSIX's sleep, in seconds */
static inline unsigned int sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}

/* POSIX's gmtime_r, as the C runtime has it */
static inline struct tm *gmtime_r(const time_t *when, struct tm *parts)
{
	return gmtime_s(parts, when) ? NULL : parts;
}

/* a file renamed over another: POSIX's rename, which Windows' C runtime
refuses when the new name is taken */
static inline int host_rename(const char *from, const char *to)
{
	if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return 0;
	errno = GetLastError() == ERROR_ACCESS_DENIED ? EACCES : GetLastError() == ERROR_FILE_NOT_FOUND ? ENOENT : EIO;
	return -1;
}
#define rename(from, to) host_rename(from, to)

/* (a file's access is its folder's on Windows, not a mode: nothing to set) */
static inline int host_restrict_file(int fd, mode_t mode)
{
	(void)fd;
	(void)mode;
	return 0;
}

static inline int host_restrict_path(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	return 0;
}

/* whether a file can be read by others than its owner: its access list,
which no mode bits say, is the folder's on Windows */
static inline int host_file_shared(const struct stat *information)
{
	(void)information;
	return 0;
}

/* Winsock, started once (counted by Windows: the game's own is kept) */
static inline void host_sockets_start(void)
{
	static LONG started;

	if (!InterlockedExchange(&started, 1))
	{
		WSADATA data;

		WSAStartup(MAKEWORD(2, 2), &data);
	}
}

/* the last socket call's error, as errno names it */
static inline int host_socket_errno(void)
{
	switch (WSAGetLastError())
	{
	case WSAEWOULDBLOCK:
		return EAGAIN;
	case WSAEINTR:
		return EINTR;
	case WSAEINPROGRESS:
		return EINPROGRESS;
	case WSAEADDRINUSE:
		return EADDRINUSE;
	case WSAEADDRNOTAVAIL:
		return EADDRNOTAVAIL;
	case WSAEACCES:
		return EACCES;
	case WSAECONNRESET:
		return ECONNRESET;
	case WSAENOTCONN:
		return ENOTCONN;
	case WSAEMFILE:
		return EMFILE;
	case WSAENOBUFS:
		return ENOBUFS;
	case WSAEAFNOSUPPORT:
		return EAFNOSUPPORT;
	default:
		return EIO;
	}
}

static inline int host_socket(int family, int type, int protocol)
{
	SOCKET socket_handle;

	host_sockets_start();
	socket_handle = WSASocketW(family, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
	return socket_handle == INVALID_SOCKET ? -1 : (int)socket_handle;
}

static inline int host_socket_close(int fd)
{
	return closesocket((SOCKET)fd) ? -1 : 0;
}

static inline int host_socket_nonblocking(int fd)
{
	u_long on = 1;

	return ioctlsocket((SOCKET)fd, FIONBIO, &on) ? -1 : 0;
}

/* (sockets are made uninheritable: host_socket, and accept's take that) */
static inline void host_socket_cloexec(int fd)
{
	SetHandleInformation((HANDLE)(ULONG_PTR)fd, HANDLE_FLAG_INHERIT, 0);
}

static inline int host_accept(int listener, struct sockaddr *address, socklen_t *length)
{
	SOCKET socket_handle = accept((SOCKET)listener, address, length);

	return socket_handle == INVALID_SOCKET ? -1 : (int)socket_handle;
}

/* a UDP socket's reply to a port that answered "unreachable" (an ICMP
message) is not an error on the next receive, as it is not on Linux */
static inline void host_udp_ignore_unreachable(int fd)
{
	BOOL off = FALSE;
	DWORD returned = 0;

	WSAIoctl((SOCKET)fd, _WSAIOW(IOC_VENDOR, 12), &off, sizeof(off), NULL, 0, &returned, NULL, NULL);
}

/* the control thread's wake-up: a pipe on Linux; here, a loopback UDP
socket that sends to itself, which WSAPoll can wait on with the others.
wake[0] is read, wake[1] written (the same socket) */
static inline int host_wake_open(int wake[2])
{
	struct sockaddr_in address;
	int length = sizeof(address);
	int fd = host_socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0)
		return -1;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind((SOCKET)fd, (struct sockaddr *)&address, sizeof(address)) ||
		getsockname((SOCKET)fd, (struct sockaddr *)&address, &length) ||
		connect((SOCKET)fd, (struct sockaddr *)&address, sizeof(address)) || host_socket_nonblocking(fd))
	{
		host_socket_close(fd);
		return -1;
	}
	host_udp_ignore_unreachable(fd);
	wake[0] = wake[1] = fd;
	return 0;
}

static inline void host_wake_signal(int fd)
{
	send((SOCKET)fd, "", 1, 0);
}

static inline void host_wake_drain(int fd)
{
	char drain[64];

	while (recv((SOCKET)fd, drain, sizeof(drain), 0) > 0)
		;
}

/* random bytes from the system: 1, else 0 */
static inline int host_random_bytes(void *buffer, size_t size)
{
	return BCryptGenRandom(NULL, (PUCHAR)buffer, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

#else /* POSIX */

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

/* (Linux's; the tests' harnesses build these units on macOS too, which has
neither) */
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static inline int host_restrict_file(int fd, mode_t mode)
{
	return fchmod(fd, mode);
}

static inline int host_restrict_path(const char *path, mode_t mode)
{
	return chmod(path, mode);
}

/* whether a file can be read by others than its owner (its mode) */
static inline int host_file_shared(const struct stat *information)
{
	return (information->st_mode & 077) != 0;
}

static inline void host_sockets_start(void)
{
}

static inline int host_socket_errno(void)
{
	return errno;
}

static inline int host_socket(int family, int type, int protocol)
{
	int fd = socket(family, type | SOCK_CLOEXEC, protocol);

	if (fd >= 0)
		fcntl(fd, F_SETFD, FD_CLOEXEC);
	return fd;
}

static inline int host_socket_close(int fd)
{
	return close(fd);
}

static inline int host_socket_nonblocking(int fd)
{
	return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

static inline void host_socket_cloexec(int fd)
{
	fcntl(fd, F_SETFD, FD_CLOEXEC);
}

static inline int host_accept(int listener, struct sockaddr *address, socklen_t *length)
{
	return accept(listener, address, length);
}

static inline void host_udp_ignore_unreachable(int fd)
{
	(void)fd;
}

static inline int host_wake_open(int wake[2])
{
	if (pipe(wake))
		return -1;
	fcntl(wake[0], F_SETFL, fcntl(wake[0], F_GETFL) | O_NONBLOCK);
	fcntl(wake[1], F_SETFL, fcntl(wake[1], F_GETFL) | O_NONBLOCK);
	fcntl(wake[0], F_SETFD, FD_CLOEXEC);
	fcntl(wake[1], F_SETFD, FD_CLOEXEC);
	return 0;
}

static inline void host_wake_signal(int fd)
{
	ssize_t written = write(fd, "", 1);

	(void)written;
}

static inline void host_wake_drain(int fd)
{
	char drain[64];

	while (read(fd, drain, sizeof(drain)) > 0)
		;
}

static inline int host_random_bytes(void *buffer, size_t size)
{
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	size_t done = 0;

	if (fd < 0)
		return 0;
	while (done < size)
	{
		ssize_t count = read(fd, (char *)buffer + done, size - done);

		if (count <= 0)
		{
			if (count < 0 && errno == EINTR)
				continue;
			close(fd);
			return 0;
		}
		done += (size_t)count;
	}
	close(fd);
	return 1;
}

#endif

#endif
