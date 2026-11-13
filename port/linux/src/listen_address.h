/* The address a socket of the game binds when it asks for every interface
(INADDR_ANY): network.address when the setting names one (several copies
on one computer, each on its own loopback alias; xnet.c); this machine
alone (127.0.0.1) while the game hosted here takes no remote connection
(split screen: nobody else can join it, so its ports stay off the
network); else every interface, as asked. A socket that asked for one
address keeps it: the telnet console's loopback, a client's connected
socket. Chosen here, with no sockets, so that the choice is tested on its
own (port/linux/tests/listen_address_test.c). */
#ifndef LISTEN_ADDRESS_H
#define LISTEN_ADDRESS_H

enum listen_address_choice
{
	_listen_address_asked = 0,
	_listen_address_local_address,
	_listen_address_loopback
};

static inline int listen_address_choose(int asked_every_interface, int has_local_address, int hosting_local)
{
	if (!asked_every_interface)
		return _listen_address_asked;
	if (has_local_address)
		return _listen_address_local_address;
	return hosting_local ? _listen_address_loopback : _listen_address_asked;
}

#endif
