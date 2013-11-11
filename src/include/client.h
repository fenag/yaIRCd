#ifndef __IRC_CLIENT_GUARD__
#define __IRC_CLIENT_GUARD__

#include <ev.h>

/** @file
	@brief Functions that deal with irc clients

	These functions provide an abstraction layer to deal with irc clients. A client is defined to be anything connecting to the server that is not a server.
	Note that developers are not encouraged to directly manipulate the structure. That would break the abstraction layer.
	Every operation to be performed in an `irc_client` shall be invoked through a function declared in this header file.
	@author Filipe Goncalves
	@date November 2013
*/

/** The structure that describes an IRC client */
struct irc_client {
	struct ev_io ev_watcher; /**<libev watcher for this client's socket. This watcher will be responsible for calling the appropriate callback function when there is interesting data to read from the socket. */
	struct ev_loop *ev_loop; /**<libev loop for this client's thread. Each thread holds its own loop. */
	char *realname; /**<GECOS field */
	char *hostname; /**<reverse looked up hostname, or the IP address if no reverse is available */
	char *nick; /**<nickname */
	char *username; /**<ident field */
	char *server; /**<this client's server ip address. `NULL` if it's a local client */
	unsigned is_registered : 1; /**<bit field indicating if this client has registered his connection */
	int socket_fd; /**<the socket descriptor used to communicate with this client */
};

/** This structure serves as a wrapper to pass arguments to this client's thread initialization function. `pthread_create()` is capable of passing a generic pointer holding the arguments, thus, we encapsulate
	every argument to be passed to `new_client()` in this structure.
*/
struct irc_client_args_wrapper {
	int socket; /**<socket file descriptor for the new connection. Typically, this is the return value from `accept()` */
	char *ip_addr; /**<the new client's ip address */
};

/* Documented in client.c */		
void *new_client(void *args);

#endif /* __IRC_CLIENT_GUARD__ */
