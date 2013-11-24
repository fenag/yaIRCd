#ifndef __YAIRCD_SERVINFO_GUARD__
#define __YAIRCD_SERVINFO_GUARD__

/** @file
	@brief Main server structures
	
	The functions defined in this header file are to be used by the rest of the code to access server information when needed throughout the IRCd's lifetime.
	
	Since these are read-only functions, and everything is populated upon the IRCd's boot, this file does not emply any thread-safety mechanism.
	
	Every access to server information should be done through the use of functions declared in this header file.
	
	@author Fabio Ribeiro
	@date November 2013
*/

/* Documented in .c source file */
int loadServerInfo(void);
void freeServerInfo(void);
const char *get_std_socket_ip(void);
const char *get_ssl_socket_ip(void);
int get_std_socket_port(void);
int get_ssl_socket_port(void);
int get_std_socket_hangup(void);
int get_ssl_socket_hangup(void);
const char *get_cert_path(void);
const char *get_priv_key_path(void);
#endif /* __YAIRCD_SERVINFO_GUARD__ */
