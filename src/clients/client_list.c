#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "client_list.h"

/** @file
	@brief Client list operations implementation
	
	This file implements the available operations on the clients list. It is a wrapper for trie operations, and it is thread safe.
	
	@author Filipe Goncalves
	@date November 2013
	@todo Ensure thread safety in every function
	@todo Move MAX_FILES_OPEN to the configuration file (when we have one...)
	@todo Drop this crappy linked list / half hash representation. Choose a trie instead, where each node points to a client; this will allow for efficient lookup and wildcard matching in the future.
	@todo If no trie is to be used, implement the hash function.
	@todo Implement find_by_nick
	@todo Rename MAX_FILES_OPEN: even though the number of local clients is bounded by max files allowed for the server process, remote clients don't count.
*/

/** Size of the list */
#define MAX_FILES_OPEN 1024


/** A node on the client's list. */
struct client_lst {
	struct irc_client *client; /**<pointer to the client instance */
	struct client_lst *next; /**<pointer to the next client */
};

/** Hash table implementation. Each array position holds a list of clients for which every node N has the same hash value. */
static struct client_lst *clients[MAX_FILES_OPEN];

/** Computes the hash for a given characters sequence.
	@param str The string to hash
	@return The hash value - something >= 0 and < MAX_FILES_OPEN
*/
static int hash(char *str) {
	return 0;
}

/* Documented in header file client_list.h */
struct irc_client *client_list_find_by_nick(char *nick) {
	return NULL;
}

/* Documented in header file client_list.h */
void client_list_add(struct irc_client *new_client) {
	int pos;
	struct client_lst *new_element;
	
	new_element = malloc(sizeof(struct client_lst));
	new_element->client = new_client;
	pos = hash(new_client->nick);
	new_element->next = clients[pos];
	clients[pos] = new_element;
}

/* Documented in header file client_list.h */
void client_list_delete(struct irc_client *client) {
	struct client_lst *curr, *prev;
	int pos;
	
	pos = hash(client->nick);
	
	for (prev = NULL, curr = clients[pos]; curr != NULL && curr->client != client; prev = curr, curr = curr->next)
		; /* Intentionally left blank */
	if (curr == NULL) {
		/* Huh?! ... no such client */
		return;
	}
	if (prev != NULL) {
		prev->next = curr->next;
	}
	else {
		clients[pos] = curr->next;
	}
	free(curr);
}

/* Documented in header file client_list.h */
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
