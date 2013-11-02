#ifndef __IRC_CLIENT_GUARD__
#define __IRC_CLIENT_GUARD__

struct irc_client {
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
