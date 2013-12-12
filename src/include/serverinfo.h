#ifndef __YAIRCD_SERVINFO_GUARD__
#define __YAIRCD_SERVINFO_GUARD__
#include <stddef.h>
/** @file
	@brief Main server structures
	
	The functions defined in this header file are to be used by the rest of the code to access server information when needed throughout the IRCd's lifetime.
	
	Since these are read-only functions, and everything is populated upon the IRCd's boot, this file does not emply any thread-safety mechanism.
	
	Every access to server information should be done through the use of functions declared in this header file.
	
	@author Filipe Goncalves
	@author Fabio Ribeiro
	@date November 2013
*/

/** Server version */
#define YAIRCD_VERSION "yaIRCd v0.1"

/** A generic MOTD entry type used by the rest of the code */
#define MOTD_ENTRY char **

/** MOTD entries iterator. Assumes a MOTD exists, i.e., the code using this should explicitly test for `motd != NULL` before. */
#define motd_entry_for_each(motd, ptr) for (ptr = motd; *ptr != NULL; ptr++)

/** Knows how to access a MOTD's entry line */
#define motd_entry_line(m) (*(m))

/* Documented in .c source file */
int loadServerInfo(void);
const char *get_server_name(void);
const char *get_server_desc(void);
const char *get_std_socket_ip(void);
const char *get_ssl_socket_ip(void);
int get_std_socket_port(void);
int get_ssl_socket_port(void);
int get_std_socket_hangup(void);
int get_ssl_socket_hangup(void);
const char *get_cert_path(void);
const char *get_priv_key_path(void);
const char *get_cloak_net_prefix(void);
const char *get_cloak_key(int i);
size_t get_cloak_key_length(int i);
int get_chanlimit(void);
double get_ping_freq(void);
double get_timeout(void);
MOTD_ENTRY get_motd(void);
#endif /* __YAIRCD_SERVINFO_GUARD__ */
