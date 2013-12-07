#include <libconfig.h>
#include <stdlib.h>
#include <string.h>
#include <ev.h>
#include <stdio.h>
#include <protocol.h>
#include "serverinfo.h"

/** @file
   @brief Main server structures
   
   This file contains the main structs used to store essential information for the server to run properly, and
      implements the functions to read the file and store data.
   Every access to server information should be done through the use of functions defined in here.
   @note These structs might need to be upgraded if any change is made on the configuration file structure.
   @author Filipe Goncalves
   @author Fabio Ribeiro
   @date November 2013
 */

/** Configuration file path */
#define CONFIG_FILE "yaircd.conf"

/** How many memory to allocate initially to store MOTD line entries */
#define INITIAL_MOTD_LINES 64

/** Stores important information about a socket. */
struct socket_info {
	const char *ip; /**<IPv4 address where this socket will be listening. 0.0.0.0 means every IP. */
	int port; /**<Port number. Typically, greater than 1024, since we're not running as root (I hope!) */
	unsigned ssl : 1; /**<Bit-field indicating if it's an SSL socket. */
	int max_hangup_clients; /**<Max. hangup clients allowed to be on hold while the parent thread dispatches a new
	                           thread to deal with a freshly arrived connection */
};

/** Holds personal information about the server's administrator. */
struct admin_info {
	const char *name; /**<Name of the administrator. */
	const char *nick; /**<IRC nickname of the administrator. */
	const char *email; /**<Admin's email. */
};

/** Holds necessary information for the cloaking module */
struct cloaks_info {
	const char *net_prefix; /**<Network prefix. This string is concatenated to a hyphen and prepended to every
	                           cloaked host resulting from a reverse hostname. */
	const char *keys[3]; /**<Array of salt keys for the cloaking module. */
	size_t keys_length[3]; /**<Length of each of the salt keys. Since this never changes during execution, we
	                          compute it once when we parse the file, and store it here. */
};

/** Structure to store general information about the server read from the configuration file */
struct server_info {
	int id; /**<This server's numeric */
	const char *name; /**<Server's name */
	const char *description; /**<Description - shows up in a /WHOIS command */
	const char *net_name; /**<Network name */
	int socket_max_hangup_clients; /**<Max. hangup clients allowed to be on hold while the parent thread dispatches
	                                  a new thread to deal with a freshly arrived connection */
	int chanlimit; /**<How many channels a client is allowed to sit in simultaneously */
	struct admin_info admin; /**<Server administrator info. See the documentation for `struct admin_info`. */
	struct socket_info socket_standard; /**<Information about the standard (plaintext) socket. See the documentation
	                                       for `struct socket_info`. */
	struct socket_info socket_secure; /**<Information about the secure (SSL) socket. See the documentation for
	                                     `struct socket_info`. */
	struct cloaks_info cloaking; /**<Cloaked hosts information. See the documentation for `struct cloaks_info`. */
	const char *certificate_path; /**<File path for the certificate file used for secure connections. */
	const char *private_key_path; /**<File path for the server's private key. */
	ev_tstamp ping_freq; /**<If no activity is detected in a connection after `ping_freq` seconds, a PING is sent. */
	ev_tstamp timeout; /**<If no PONG reply arrives within `timeout` seconds, the session is terminated. */
	char **motd; /**<Dynamically allocated array holding MOTD entries for this server. This array is terminated with a NULL pointer. Each entry is a pointer to a null terminated
					 characters sequence with a MOTD entry without any newline character. */
};

/** Global server info structure holding every meta information about the IRCd. */
static struct server_info *info;

/** This function reads the MOTD file specified in the configuration file, and stores it in a convenient way to make it easy to access during the IRCd's lifetime.
	It will read chunks of `MAX_MOTD_LINE_LENGTH` characters from the MOTD file, and store each chunk in a `MOTD_ENTRY` container. As of this writing,
	the container is nothing more than a dynamically allocated array of characters that grows as needed.
	This function will allocate memory to hold every MOTD line, where each line is at most `MAX_MOTD_LINE_LENGTH` characters, without couting with newline or null terminator
	characters.
	Every MOTD line is processed, newline characters such as \\r and \\n are removed, and every sequence is null terminated.
	The end of the array is indicated by a position with a NULL pointer. 
	The rest of the code should use the macros defined in `serverinfo.h` to iterate through this dynamic array and access its values.
	@param cfg libconfig's configuration structure in use
	@return A `MOTD_ENTRY` object that shall be manipulated with the appropriate macros defined in `serverinfo.h`
*/
MOTD_ENTRY read_motd_file(config_t *cfg) {
	config_setting_t *setting;
	const char *motd_file_path;
	char line_buf[MAX_MOTD_LINE_LENGTH+1];
	FILE *motd_file;
	int line_size;
	MOTD_ENTRY motd;
	MOTD_ENTRY motd_new_ptr;
	int motd_scale;
	int motd_pos;
	
	motd = malloc(sizeof(*motd)*INITIAL_MOTD_LINES);
	motd_scale = 1;
	motd_pos = 0;
	
	if (motd == NULL) {
		return NULL;
	}
	setting = config_lookup(cfg, "files");
	/* Grab motd file path */
	config_setting_lookup_string(setting, "motd", &motd_file_path);
	
	if ((motd_file = fopen(motd_file_path, "r")) == NULL) {
		perror("Could not open MOTD file");
		free(motd);
		return NULL;
	}
	
	while (fgets(line_buf, sizeof(line_buf), motd_file) != NULL) {
		line_size = strlen(line_buf);
		/* Assert: line_size <= sizeof(line_buf)-1 */
		if (line_size > 0 && line_buf[line_size-1] == '\n') {
			line_size--;
		}
		if (line_size > 0 && line_buf[line_size-1] == '\r') {
			line_size--;
		}
		line_buf[line_size] = '\0';
		if (motd_pos+1 >= motd_scale*INITIAL_MOTD_LINES) {
			/* Oops! Need more memory! */
			motd_new_ptr = realloc((void *) motd, (motd_scale *= 2)*INITIAL_MOTD_LINES*sizeof(*motd));
			if (motd_new_ptr == NULL) {
				fprintf(stderr, "::serverinfo.c:read_motd_file(): Not enough memory to store MOTD.\n");
				free(motd[motd_pos-1]);
				motd[motd_pos-1] = NULL;
				fclose(motd_file);
				return motd;
			}
			motd = motd_new_ptr;
		}
		if ((motd[motd_pos] = malloc(line_size+1)) == NULL) {
			fprintf(stderr, "::serverinfo.c:read_motd_file(): Not enough memory to store MOTD line.\n");
			motd[motd_pos] = NULL;
			fclose(motd_file);
			return motd;
		}
		strcpy(motd[motd_pos], line_buf);
		motd_pos++;
	}
	/* assert: motd_pos < motd_scale*INITIAL_MOTD_LINES */
	motd[motd_pos] = NULL;
	
	if (ferror(motd_file)) {
		fprintf(stderr, "::serverinfo.c:read_motd_file(): Error on file.\n");
	}
	
	fclose(motd_file);
	return motd;
}

/**
   Using libconfig, this function creates and populates a `struct server_info` which is going to hold information about
      the chosen configuration for this server. If one changes CONFIG_FILE content, this is the only function, that one
      needs to adapt.
   This must be called exactly once by the parent thread before any other client connections are accepted.
   @warning If you need to mess around with this function, you better take a look at libconfig's documentation:
      http://www.hyperrealm.com/libconfig/libconfig_manual.html
   @return `1` on error; `0` otherwise
 */
int loadServerInfo(void)
{
	double ping_freq;
	double timeout;
	config_t cfg;
	config_setting_t *setting;
	config_init(&cfg);

	/* Read the configuration file, and check if it was successful*/
	if (config_read_file(&cfg, CONFIG_FILE) == CONFIG_FALSE) {
		perror("::serverinfo.c:loadServerInfo(): Server unable to read configuration file.");
		//debug
		//fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg), config_error_line(&cfg),
		// config_error_text(&cfg));
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

	/* Cloak block */
	setting = config_lookup(&cfg, "serverinfo.cloak");
	config_setting_lookup_string(setting, "net_prefix", &(info->cloaking.net_prefix));
	config_setting_lookup_string(setting, "key1", &(info->cloaking.keys[0]));
	config_setting_lookup_string(setting, "key2", &(info->cloaking.keys[1]));
	config_setting_lookup_string(setting, "key3", &(info->cloaking.keys[2]));
	info->cloaking.keys_length[0] = strlen(info->cloaking.keys[0]);
	info->cloaking.keys_length[1] = strlen(info->cloaking.keys[1]);
	info->cloaking.keys_length[2] = strlen(info->cloaking.keys[2]);
	
	/* Timeout block */
	setting = config_lookup(&cfg, "serverinfo.timeouts");
	config_setting_lookup_float(setting, "ping_freq", &ping_freq);
	config_setting_lookup_float(setting, "timeout", &timeout);
	info->ping_freq = ping_freq;
	info->timeout = timeout;
	
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
	
	/* Channel block */
	setting = config_lookup(&cfg, "channels");
	config_setting_lookup_int(setting, "chanlimit", &(info->chanlimit));
	
	/* Read and store MOTD file */
	info->motd = read_motd_file(&cfg);

	return 0;
}

/** Reads this server's name.
   @return Pointer to null terminated characters sequence with the server's name.
 */
const char *get_server_name(void)
{
	return info->name;
}

/** Reads this server's description.
   @return Pointer to null terminated characters sequence with the server's description.
 */
const char *get_server_desc(void)
{
	return info->description;
}

/** Reads the standard socket listening IP.
   @return Pointer to null terminated characters sequence with the listening IP.
 */
const char *get_std_socket_ip(void)
{
	return info->socket_standard.ip;
}

/** Reads the secure socket listening IP.
   @return Pointer to null terminated characters sequence with the listening IP.
 */
const char *get_ssl_socket_ip(void)
{
	return info->socket_secure.ip;
}

/** Reads the standard socket port number.
   @return Listening port number.
 */
int get_std_socket_port(void)
{
	return info->socket_standard.port;
}

/** Reads the secure socket port number.
   @return Listening port number.
 */
int get_ssl_socket_port(void)
{
	return info->socket_secure.port;
}

/** Reads the standard socket `max_hangup_clients` attribute.
   @return Max. hangup clients configured for this socket.
 */
int get_std_socket_hangup(void)
{
	return info->socket_standard.max_hangup_clients;
}

/** Reads the secure socket `max_hangup_clients` attribute.
   @return Max. hangup clients configured for this socket.
 */
int get_ssl_socket_hangup(void)
{
	return info->socket_secure.max_hangup_clients;
}

/** Reads the server's certificate file path.
   @return Pointer to null terminated characters sequence with the server's certificate file path.
 */
const char *get_cert_path(void)
{
	return info->certificate_path;
}

/** Reads the server's private key file path.
   @return Pointer to null terminated characters sequence with the server's private key file path.
 */
const char *get_priv_key_path(void)
{
	return info->private_key_path;
}

/** Reads the server's net prefix for cloaked hostnames
   @return Pointer to null terminated characters sequence with the server's net prefix.
 */
const char *get_cloak_net_prefix(void)
{
	return info->cloaking.net_prefix;
}

/** Reads the server's cloak key number `i`. Keys are numbered 1 to 3.
   @param i Which key to read.
   @return Pointer to null terminated characters sequence with the server's cloaking key `i`.
 */
const char *get_cloak_key(int i)
{
	return info->cloaking.keys[i - 1];
}

/** Reads the server's cloak key `i`'s length. This operation runs in `O(1)`, since this value never changesand was
   computed at initialization time. Keys are numbered 1 to 3.
   @param i Which key's length to return.
   @return Key `i`'s length.
 */
size_t get_cloak_key_length(int i)
{
	return info->cloaking.keys_length[i - 1];
}

/** Reads the chanlimit setting. A client cannot be in more than `chanlimit` channels simultaneously.
	@return How many channels, at most, a client can sit in
*/
int get_chanlimit(void) {
	return info->chanlimit;
}

/** Reads the ping frequency for this server.
	@return Ping frequency
*/
ev_tstamp get_ping_freq(void) {
	return info->ping_freq;
}

/** Reads the timeout value for this server.
	If no PONG reply is received within this amount of time, the client session is to be terminated.
	@return Timeout value
*/
ev_tstamp get_timeout(void) {
	return info->timeout;
}

MOTD_ENTRY get_motd(void) {
	return info->motd;
}
