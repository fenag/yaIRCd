#include "client/client.h"
#include "client/client_list.h"
#include "protocol/limits.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ev.h>

char n[] = "a"; /* TEMP - nick generator */

static void manage_client_messages(EV_P_ ev_io *watcher, int revents);
static struct irc_client *new_client_connection(char *ip_addr, int socket);
static void destroy_client(struct irc_client *client);
static void free_client(struct irc_client *client);

void *new_client(void *args) {
	struct irc_client_args_wrapper *arguments = (struct irc_client_args_wrapper *) args;
	struct irc_client *client;
	
	client = new_client_connection(arguments->ip_addr, arguments->socket);
	free(args);
	
	if (client == NULL) {
		fprintf(stderr, "::client.c:new_client(): Could not allocate memory for new client\n");
	}
	
	return NULL;
}

static void manage_client_messages(EV_P_ ev_io *watcher, int revents) {
	char msg_buf[MAX_MSG_SIZE];
	int msg_size;
	struct irc_client *client;
	
    if (revents & EV_ERROR) {
        fprintf(stderr, "::client.c:manage_client_messages(): unexpected EV_ERROR on client event watcher\n");
        return;
    }
	if (!(revents & EV_READ)) {
		fprintf(stderr, "::client.c:manage_client_messages(): EV_READ not present, but there was no EV_ERROR, ignoring message\n");
		return;
	}
	
	client = (struct irc_client *) watcher;
	
	msg_size = read(client->socket_fd, msg_buf, sizeof(msg_buf));
	
	if (strcmp(msg_buf, ":quit\r\n") == 0) {
		notify_all_clients("[QUITTING]\n", client->nick, 11);
		destroy_client(client);
		return;
	}
	
	notify_all_clients(msg_buf, client->nick, msg_size);
}

static struct irc_client *new_client_connection(char *ip_addr, int socket) {

	struct irc_client *new_client;
	char temp[MAX_MSG_SIZE];
	
	if ((new_client = malloc(sizeof(struct irc_client))) == NULL)
		return NULL;
		
	new_client->realname = "Just a test";
	new_client->hostname = ip_addr;
	new_client->nick = malloc(2);
	strcpy(new_client->nick, n);
	n[0]++;
	new_client->username = "developer";
	new_client->server = "mantissa bits rule the world!";
	new_client->socket_fd = socket;
	
	new_client->ev_loop = ev_loop_new(0);
	ev_io_init(&new_client->ev_watcher, manage_client_messages, socket, EV_READ);
	ev_io_start(new_client->ev_loop, &new_client->ev_watcher);

	client_list_add(new_client);
	
	sprintf(temp, "Hello. I am a basic IRC Server, take it easy on me!\n"
	"You are connecting from %s\n"
	"Your nickname is %s\n"
	"Your realname is %s"
	"Your username is %s\n"
	"You are on server %s\n"
	"Type in your message. Enjoy your chatting session!\n",
	new_client->hostname, new_client->nick, new_client->realname, new_client->username, new_client->server);

	write(new_client->socket_fd, temp, strlen(temp));
	
	/* Wait for messages coming from client */
	ev_run(new_client->ev_loop, 0);
	
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
	
	ev_io_stop(client->ev_loop, &client->ev_watcher); /* Stop the callback mechanism */
	ev_break(client->ev_loop, EVBREAK_ONE); /* Stop iterating */
	
	free(client);
}
