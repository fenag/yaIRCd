#ifndef __IRC_CLIENT_LIST_GUARD__
#define __IRC_CLIENT_LIST_GUARD__
#include "client.h"

/** @file
	@brief Stores connected clients
	
	This set of functions is responsible for handling every operation that is related to the list of connected clients that the server maintains.
	With the exception of `client_list_init()` and `client_list_destroy()`, every function is thread safe.
	
	@author Filipe Goncalves
	@date November 2013
	@see trie.h
*/

/** Return code for invalid nicks */
#define CLIENT_LST_INVALID_NICK 1

/** Return code for out of memory error */
#define CLIENT_LST_NO_MEM 2

/** Return code for attempting to add a client that already exists */
#define CLIENT_LST_ALREADY_EXISTS 3

/* Documented in client_list.c */
int client_list_init(void);
void client_list_destroy(void);
void *client_list_find_and_execute(char *nick, void *(*f)(struct irc_client *client, void *args), void *fargs, int *success);
int client_list_add(struct irc_client *new_client, char *newnick);
void client_list_delete(struct irc_client *client);

#endif /* __IRC_CLIENT_LIST_GUARD__ */
