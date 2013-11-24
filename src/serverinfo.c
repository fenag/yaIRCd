#include <libconfig.h>
#include <stdlib.h>
#include "serverinfo.h"

/** @file
	@brief Main server structures
	
	This file contains the main structs used to store essential information for the server to run properly, and implements the functions to read the file and store data.
	
	Every access to server information should be done through the use of functions defined in here.
	
	@note These structs might need to be upgraded if any change is made on the configuration file structure.
	
	@author Fabio Ribeiro
	@date November 2013
*/

/** Configuration file path */
#define CONFIG_FILE "yaircd.conf"

/** Stores important information about a socket. */
struct socket_info {
	const char *ip; /**<IPv4 address where this socket will be listening. 0.0.0.0 means every IP. */
	int port; /**<Port number. Typically, greater than 1024, since we're not running as root (I hope!) */
	unsigned ssl : 1; /**<Bit-field indicating if it's an SSL socket. */
	int max_hangup_clients; /**<Max. hangup clients allowed to be on hold while the parent thread dispatches a new thread to deal with a freshly arrived connection */
};

/** Hold personal information about the server's administrator. */
struct admin_info {
	const char *name; /**<Name of the administrator. */
	const char *nick; /**<IRC nickname of the administrator. */
	const char *email; /**<Admin's email. */
};

/** Structure to store general information about the server read from the configuration file */
struct server_info {
	int id; /**<This server's numeric */
	const char *name; /**<Server's name */
	const char *description; /**<Description - shows up in a /WHOIS command */
	const char *net_name; /**<Network name */
	int socket_max_hangup_clients; /**<Max. hangup clients allowed to be on hold while the parent thread dispatches a new thread to deal with a freshly arrived connection */
	struct admin_info admin; /**<Server administrator info. See the documentation for `struct admin_info`. */
	struct socket_info socket_standard; /**<Information about the standard (plaintext) socket. See the documentation for `struct socket_info`. */
	struct socket_info socket_secure; /**<Information about the secure (SSL) socket. See the documentation for `struct socket_info`. */
	const char *certificate_path; /**<File path for the certificate file used for secure connections. */
	const char *private_key_path; /**<File path for the server's private key. */
	const char *motd_file_path; /**<MOTD file path */
};

static struct server_info *info;
static config_t cfg;

/**
 * Using libconfig, this function creates and populates a `struct server_info` which is going to hold information about the chosen
 * configuration for this server. If one changes CONFIG_FILE content, this is the only function, that one needs to adapt.
 * 
 * @warning If you need to mess around with this function, you better take a look at libconfig's documentation:
 * 			http://www.hyperrealm.com/libconfig/libconfig_manual.html
 * @return `1` on error; `0` otherwise
 */
int loadServerInfo(void) {
	
	config_setting_t *setting;
	config_init(&cfg);
	
	/* Read the configuration file, and check if it was successful*/
	if (config_read_file(&cfg, CONFIG_FILE) == CONFIG_FALSE) {
		perror("::serverinfo.c:loadServerInfo(): Server unable to read configuration file.");
		//debug
		//fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg), config_error_line(&cfg), config_error_text(&cfg));
		config_destroy(&cfg);
		return 1;
	}
	
	if ((info = malloc(sizeof(*info))) == NULL) {
		fprintf(stderr, "::serverinfo.c:loadServerInfo(): Could not allocate memory.\n");
		return 1;
	}
	
	/* Server info */
	setting = config_lookup(&cfg, "serverinfo");
	config_setting_lookup_int(setting, "serv_id", &(info->id));
	config_setting_lookup_string(setting, "serv_name", &(info->name));
	config_setting_lookup_string(setting, "serv_desc", &(info->description));
	config_setting_lookup_string(setting, "net_name", &(info->net_name));
	config_setting_lookup_string(setting, "certificate", &(info->certificate_path));
	config_setting_lookup_string(setting, "pkey", &(info->private_key_path));
	
	/* Admin info */
	setting = config_lookup(&cfg, "serverinfo.admin");
	config_setting_lookup_string(setting, "name", &(info->admin.name));
	config_setting_lookup_string(setting, "nick", &(info->admin.nick));
	config_setting_lookup_string(setting, "email", &(info->admin.email));
	
	/* Standard socket info */
	setting = config_lookup(&cfg, "listen.sockets.standard");
	
	config_setting_lookup_int(setting, "port", &(info->socket_standard.port));
	config_setting_lookup_int(setting, "max_hangup_clients", &(info->socket_standard.max_hangup_clients));
	config_setting_lookup_string(setting, "ip", &(info->socket_standard.ip));
	info->socket_standard.ssl = 0;
	
	/* Secure socket info */
	setting = config_lookup(&cfg, "listen.sockets.secure");
	config_setting_lookup_int(setting, "port", &(info->socket_secure.port));
	config_setting_lookup_int(setting, "max_hangup_clients", &(info->socket_secure.max_hangup_clients));
	config_setting_lookup_string(setting, "ip", &(info->socket_secure.ip));
	info->socket_secure.ssl = 1;
	
	/* Files */
	setting = config_lookup(&cfg, "files");
	config_setting_lookup_string(setting, "motd", &(info->motd_file_path));	
	return 0;
}

/**
 * This is where the mem allocated during the load of the configuration file is freed. You might have noticed
 * we didn't use any buffer while calling `config_setting_lookup_TYPE()`. This means that if you call this function
 * too soon, you might come across a mean, still beautiful, segmentation fault (just if you try to access the info
 * contained in the struct we have populated during the `loadServerInfo()` call).
 */
void freeServerInfo(void) {
	config_destroy(&cfg);
}

const char *get_std_socket_ip(void) {
	return info->socket_standard.ip;
}

const char *get_ssl_socket_ip(void) {
	return info->socket_secure.ip;
}

int get_std_socket_port(void) {
	return info->socket_standard.port;
}

int get_ssl_socket_port(void) {
	return info->socket_secure.port;
}

int get_std_socket_hangup(void) {
	return info->socket_standard.max_hangup_clients;
}

int get_ssl_socket_hangup(void) {
	return info->socket_secure.max_hangup_clients;
}

const char *get_cert_path(void) {
	return info->certificate_path;
}

const char *get_priv_key_path(void) {
	return info->private_key_path;
}
