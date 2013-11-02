#include "client/client_list.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#define MAX_FILES_OPEN 1024

struct client_lst {
	struct irc_client *client;
	struct client_lst *next;
};

static struct client_lst *clients[MAX_FILES_OPEN];

static int hash(char *str) {
	/* TODO implement hash function */
	return 0;
}

struct irc_client *client_list_find_by_nick(char *nick) {
	/* TODO implement this */
	return NULL;
}

void client_list_add(struct irc_client *new_client) {
	int pos;
	struct client_lst *new_element;
	
	new_element = malloc(sizeof(struct client_lst));
	new_element->client = new_client;
	pos = hash(new_client->nick);
	new_element->next = clients[pos];
	clients[pos] = new_element;
}

void notify_all_clients(char *msg, char *nick, int msg_size) {
	int i;
	struct client_lst *lst_iter;
	
	for (i = 0; i < sizeof(clients)/sizeof(clients[0]); i++) {
		for (lst_iter = clients[i]; lst_iter != NULL; lst_iter = lst_iter->next) {
			write(lst_iter->client->socket_fd, "<", 1);
			write(lst_iter->client->socket_fd, nick, strlen(nick));
			write(lst_iter->client->socket_fd, "> ", 2);
			write(lst_iter->client->socket_fd, msg, msg_size);
		}
	}
}