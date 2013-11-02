#include "client/client.h"
#include "client/client_list.h"
#include "protocol/limits.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char n[] = "a"; /* TEMP - nick generator */

static void manage_client_messages(struct irc_client *client);
static struct irc_client *new_client_connection(char *ip_addr, int socket);

void *new_client(void *args) {
	struct irc_client_args_wrapper *arguments = (struct irc_client_args_wrapper *) args;
	struct irc_client *client;
	
	client = new_client_connection(arguments->ip_addr, arguments->socket);
	manage_client_messages(client);
	
	return NULL;
}

static void manage_client_messages(struct irc_client *client) {
	char msg_buf[MAX_MSG_SIZE];
	char temp[] = "Hello. I am a basic super primitive IRC server. Type your message here, and it will be echoed to every client.\n";
	int msg_size;
	
	write(client->socket_fd, temp, sizeof(temp));
	
	while ((msg_size = read(client->socket_fd, msg_buf, sizeof(msg_buf))) > 0) {
		notify_all_clients(msg_buf, client->nick, msg_size);
	}
	close(client->socket_fd);
	/* TODO add necessary frees() */
}

static struct irc_client *new_client_connection(char *ip_addr, int socket) {

	struct irc_client *new_client;
	
	new_client = malloc(sizeof(struct irc_client)); /* TODO check malloc's return value */
	new_client->realname = "Just a test";
	new_client->hostname = ip_addr;
	new_client->nick = malloc(2);
	strcpy(new_client->nick, n);
	n[0]++;
	new_client->username = "developer";
	new_client->server = "mantissa bits rule the world!";
	new_client->socket_fd = socket;

	client_list_add(new_client);
	
	return new_client;
}
