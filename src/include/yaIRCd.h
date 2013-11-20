#ifndef __IRC_YAIRCD_GUARD__
#define __IRC_YAIRCD_GUARD__

#define IPv6_SOCK 0x1
#define SSL_SOCK 0x2
#define CONFIG_FILE "yaircd.conf"

/** @file
	@brief This header file contains the main structs used to store essencial information for the server to run properly. 
	
	@warning These structs might need to be upgraded if any change is made on the CONFIG_FILE
	@author Filipe Goncalves
	@author Fabio Ribeiro
	@date November 2013
*/

/** Store general information about the server */
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

/** Store information about a socket */
struct socket_info {
	const char * ip;
	int port;
	unsigned ssl : 1;
	int max_hangup_clients;
};

/** Store information about an admin */
struct admin_info {
	const char * name;
	const char * nick;
	const char * email;
};

#endif /* __IRC_YAIRCD_GUARD__ */
