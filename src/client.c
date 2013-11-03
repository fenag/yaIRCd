#include "client/client.h"
#include "client/client_list.h"
#include "protocol/limits.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

char n[] = "a"; /* TEMP - nick generator */

static void manage_client_messages(struct irc_client *client);
static struct irc_client *new_client_connection(char *ip_addr, int socket);
static void destroy_client(struct irc_client *client);
static void free_client(struct irc_client *client);

void *new_client(void *args) {
	struct irc_client_args_wrapper *arguments = (struct irc_client_args_wrapper *) args;
	struct irc_client *client;
	
	client = new_client_connection(arguments->ip_addr, arguments->socket);
	free(args);
	manage_client_messages(client);
	
	return NULL;
}

static void manage_client_messages(struct irc_client *client) {
	char msg_buf[MAX_MSG_SIZE];
	char temp[MAX_MSG_SIZE];
	int msg_size;
	
	sprintf(temp, "Hello. I am a basic IRC Server, take it easy on me!\n"
	"You are connecting from %s\n"
	"Your nickname is %s\n"
	"Your realname is %s"
	"Your username is %s\n"
	"You are on server %s\n"
	"Type in your message. Enjoy your chatting session!\n",
	client->hostname, client->nick, client->realname, client->username, client->server);
	
	write(client->socket_fd, temp, strlen(temp));
	
	while ((msg_size = read(client->socket_fd, msg_buf, sizeof(msg_buf))) > 0) {
		if (strcmp(msg_buf, ":quit\r\n") == 0)
			break;
		notify_all_clients(msg_buf, client->nick, msg_size);
	}
	notify_all_clients("[QUITTING]", client->nick, 10);
	destroy_client(client);
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

static void destroy_client(struct irc_client *client) {
	client_list_delete(client);
	free_client(client);
}

static void free_client(struct irc_client *client) {
	/*free(client->realname);*/ /* TODO - remove this comment when client->realname is not a constant string anymore */
	free(client->hostname);
	free(client->nick);
	/*free(client->username);*/
	/*ree(client->server);*/ /* TODO think about whether this is really necessary */
	close(client->socket_fd);
	free(client);
}
