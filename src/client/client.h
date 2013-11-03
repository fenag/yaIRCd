#ifndef __IRC_CLIENT_GUARD__
#define __IRC_CLIENT_GUARD__

#include <ev.h>

struct irc_client {
	struct ev_io ev_watcher; /* libev watcher for this client's socket */
	struct ev_loop *ev_loop; /* libev loop for this client's thread */
	char *realname;
	char *hostname;
	char *nick;
	char *username;
	char *server;
	int socket_fd;
};

struct irc_client_args_wrapper {
	int socket;
	char *ip_addr;
};

void *new_client(void *args);

#endif /* __IRC_CLIENT_GUARD__ */
