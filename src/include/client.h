#ifndef __IRC_CLIENT_GUARD__
#define __IRC_CLIENT_GUARD__
#include <ev.h>
#include <openssl/ssl.h>
#include <netinet/in.h>
#include "write_msgs_queue.h"
#include "read_msgs.h"

/** @file
	@brief Functions that deal with irc clients

	These functions provide an abstraction layer to deal with irc clients. A client is defined to be anything connecting to the server that is not a server.
	Note that developers are not encouraged to directly manipulate the structure. That would break the abstraction layer.
	Every operation to be performed in an `irc_client` shall be invoked through a function declared in this header file.
	@author Filipe Goncalves
	@author Fabio Ribeiro
	@date November 2013
*/


/** The structure that describes an IRC client */
struct irc_client {
	struct ev_io io_watcher; /**<io watcher for this client's socket. This watcher will be responsible for calling the appropriate callback function when there is interesting data to read from the socket. */
	struct ev_async async_watcher; /**<async watcher used to wake up a client's thread when there is new data queued and waiting to be sent. */
	struct ev_loop *ev_loop; /**<libev loop for this client's thread. Each thread holds its own loop. */
	struct msg_queue write_queue; /**<Write queue that holds messages waiting to be sent. @see client_queue.h */
	char *realname; /**<GECOS field. */
	char *hostname; /**<reverse looked up hostname, or the IP address if no reverse is available. */
	char *public_host; /**<cloaked hostname for this client. This is the address shown to other regular users, so that a client's address is kept private. */
	char *nick; /**<nickname */
	char *username; /**<ident field */
	char *server; /**<this client's server ip address. `NULL` if it's a local client. */
	char **channels; /**<A dynamically allocated array of `char *` holding a list of the channels this client is in. Free positions hold a NULL pointer. */
	int channels_count; /**<How many channels he joined, i.e., how many positions in `channels` are taken (not NULL). */
	struct irc_message last_msg; /**<last IRC message received coming from this client. This structure will be filled as we read this client's socket, and when an entire message is finished reading, this structure
									 will contain the necessary information. */
	unsigned is_registered : 1; /**<bit field indicating if this client has registered his connection. */
	unsigned uses_ssl : 1; /**<bit field indicating if this client is using a secure connection. */
	unsigned host_reversed : 1; /**<bit field indicating if we were able to reverse lookup this client's IP address. If this field is not set, then `hostname` holds an IP address, otherwise, a hostname. */
	int socket_fd; /**<the socket descriptor used to communicate with this client. */
	SSL *ssl; /**<main SSL structure, created per establish connection. */
};

/** This structure serves as a wrapper to pass arguments to this client's thread initialization function. `pthread_create()` is capable of passing a generic pointer holding the arguments, thus, we encapsulate
	every argument to be passed to `new_client()` in this structure.
*/
struct irc_client_args_wrapper {
	int socket; /**<socket file descriptor for the new connection. Typically, this is the return value from `accept()` */
	/** A union holding the sockaddr structure for the new connection. Current versions only support IPv4, but this will make it easy to extend to IPv6 */
	union {
		struct sockaddr_in ipv4_address; /**<If this is a client on IPv4, we will store a `struct sockaddr_in` */
		struct sockaddr_in6 ipv6_address; /**<If this is a client on IPv6, we will store a `struct sockaddr_in6` */
	} address;
	socklen_t address_length; /**<Length of the sockaddr attribute in use */
	unsigned is_ipv6 : 1; /**<Bit-field indicating if this is an IPv6 connection. This field is used to remember what the union is holding. */
	SSL *ssl; /**<main SSL structure for secure connected clients */
};

/* Documented in client.c */		
void *new_client(void *args);
void terminate_session(struct irc_client *client, char *quit_msg);

#endif /* __IRC_CLIENT_GUARD__ */
