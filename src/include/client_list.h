#ifndef __IRC_CLIENT_LIST_GUARD__
#define __IRC_CLIENT_LIST_GUARD__
#include "client.h"

/** @file
	@brief Stores connected clients
	
	This set of functions is responsible for handling every operation that is related to the list of connected clients that the server maintains.
	@author Filipe Goncalves
	@date November 2013
*/

/** Finds a client by nickname.
	@param nick The nickname to look for.
	@return `NULL` if no such client exists.
			Pointer to `struct irc_client` of the specified client otherwise.
*/
struct irc_client *client_list_find_by_nick(char *nick);

/** Adds a new client to the client's list.
	@param new_client Pointer to the new client. Cannot be `NULL`.
*/
void client_list_add(struct irc_client *new_client);

/** Deletes a client from the client's list
	@param client Pointer to the client that shall be deleted. Cannot be `NULL`.
*/
void client_list_delete(struct irc_client *client);

/** Broadcasts a message to every client stored in the list.
	@param msg The message to broadcast. This must be a valid pointer to a characters sequence.
	@param nick The message's author.
	@param msg_size How many characters are in the sequence pointed to by `msg`. Must be > 0
*/
void notify_all_clients(char *msg, char *nick, int msg_size);

#endif /* __IRC_CLIENT_LIST_GUARD__ */
