#ifndef __IRC_YAIRCD_GUARD__
#define __IRC_YAIRCD_GUARD__

#define IPv6_SOCK 0x1
#define SSL_SOCK 0x2

struct server_info {
	int id;
	const char * name;
	const char * description;
	const char * net_name;
	int socket_max_hangup_clients;
	struct admin_info * admin;
	struct socket_info * socket_standard;
	struct socket_info * socket_secure;
	const char * certificate_path;
	const char * private_key_path;
};

struct socket_info {
	const char * ip;
	int port;
	unsigned ssl : 1;
	int max_hangup_clients;
};

struct admin_info {
	const char * name;
	const char * nick;
	const char * email;
};

#endif /* __IRC_YAIRCD_GUARD__ */
