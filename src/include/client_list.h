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


/* Documented in client_list.c */
void client_list_init(void);
void client_list_destroy(void);
struct irc_client *client_list_find_by_nick(char *nick);
int client_list_add(struct irc_client *new_client, char *newnick);
void client_list_delete(struct irc_client *client);

#endif /* __IRC_CLIENT_LIST_GUARD__ */
