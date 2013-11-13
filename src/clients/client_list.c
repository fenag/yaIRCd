#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include "trie.h"
#include "client_list.h"

/** @file
	@brief Client list operations implementation
	
	This file implements the available operations on the clients list. It is a wrapper for trie operations, and it is thread safe.
	
	Developers are adviced to read RFC Section 2.3.1 to learn about which characters are allowed in a nickname.
	Note that, according to RFC Section 2.2, due to IRC's scandinavian origin, the characters `{ } |` are considered to be the lower case equivalents of the characters `[ ] \\`, respectively.
	This is a critical issue when determining the equivalence of two nicknames.
	
	Every function in this file is thread safe, with the exception of `client_list_init()` and `client_list_destroy()`, which shall be called exactly once by the master process before any thread is created and
	after every thread is dead, respectively.
	
	@author Filipe Goncalves
	@date November 2013
	@todo Ensure thread safety in every function
	@todo Move MAX_LOCAL_CLIENTS to the configuration file (when we have one...), or determine it at compile time
	@todo Implement find_by_nick
*/

/** How many clients are allowed to be on this server */
#define MAX_LOCAL_CLIENTS 1024

/** Defines the size of the alphabet (letters in `[a-z]`). */
#define NICK_ALPHABET_SIZE 26
/** Defines how many special characters are allowed */
#define NICK_SPECIAL_CHARS_SIZE 9
/** Says how many edges a trie's node will have */
#define NICK_EDGES_NO NICK_ALPHABET_SIZE+NICK_SPECIAL_CHARS_SIZE

/** A trie to hold every client */
static struct trie_t *clients;

/** A mutex to handle concurrent access to `clients` */
pthread_mutex_t clients_mutex;

/** This function defines what characters are allowed inside a nickname. See RFC Section 2.3.1 to learn about this.
	@param s The character to check.
	@return `1` if `s` is allowed in a nickname; `0` otherwise.
*/
static int is_valid(char s) {
	return (((s) >= 'a' && (s) <= 'z') || ((s) >= 'A' && (s) <= 'Z') || (s) == '-' || (s) == '[' || (s) == ']' || (s) == '\\' || (s) == '`' || (s) == '^' || (s) == '{' || s == '}' || s =='|');
}

/** Translates from an ID of a special character (a character not in `[a-z]` back to its corresponding character.
	@param i ID
	@return The special character whose ID is `i`
*/
static inline char special_id_to_char(int i) {
	return ((i) == 0 ? '-' : (i) == 1 ? '{' : (i) == 2 ? '}' : (i) == 3 ? '|' : (i) == 4 ? '`' : (i) == 5 ? '^' : -1);
}

/** Converts a character ID back into its corresponding character.
	@param i ID
	@return The character whose ID is `i`
*/
static char pos_to_char(int i) {
	return ((char) (((i) < NICK_ALPHABET_SIZE) ? ('a'+(i)) : special_id_to_char((i) - NICK_ALPHABET_SIZE)));
}

/** Translates from a special character (a character not in `[a-z]` into its ID.
	@param s A special character
	@return `s`'s ID
*/
static inline int special_char_id(char s) {
	return ((s) == '-' ? 0 : s == '[' || s == '{' ? 1 : s == ']' || s == '}' ? 2 : s == '\\' || s == '|' ? 3 : s == '`' ? 4 : s == '^' ? 5 : -1);
}

/** Converts a character into its ID
	@param s The character
	@return `s`'s ID
*/
static int char_to_pos(char s) {
	return ((((s) >= 'a' && (s) <= 'z') || ((s) >= 'A' && (s) <= 'Z')) ? tolower((unsigned char) (s)) - 'a' : NICK_ALPHABET_SIZE + special_char_id(s));
}

/** Initializes clients list by creating an empty list. Initializes the necessary structures to control concurrent thread access.
	The trie will be passed pointers to the functions `is_valid()`, `pos_to_char()`, and `char_to_pos()`. This set of functions defines a valid nickname.
	@return `0` on success; `-1` on failure. `-1` indicates a resources allocation error.
	@warning This function must be called exactly once, by the parent thread, before any thread is created and tries to access the list of clients.
 */
int client_list_init(void) {
	if ((clients = init_trie(NULL, is_valid, pos_to_char, char_to_pos, NICK_EDGES_NO)) == NULL) {
		return -1;
	}
	if (pthread_mutex_init(&clients_mutex, NULL) != 0) {
		destroy_trie(clients, FLAG_NO_FREE_DATA);
		perror("::client_list.c:client_list_init(): Could not initialize mutex");
		return -1;
	}
	return 0;
}

/** Destroys a clients list after it is no longer needed. Frees `clients_mutex`.
	@warning This function must be called exactly once, by the master process, after every thread is dead and no more accesses to the list of clients will be performed.
 */
void client_list_destroy(void) {
	destroy_trie(clients, 0);
	if (pthread_mutex_destroy(&clients_mutex) != 0) {
		perror("::client_list.c:client_lit_destroy(): Could not destroy mutex");
	}
}

/** Finds a client by nickname.
	@param nick The nickname to look for; must be a null terminated characters sequence.
	@return `NULL` if no such client exists, or if `nick` contains invalid characters.
			Pointer to `struct irc_client` of the specified client otherwise.
*/
struct irc_client *client_list_find_by_nick(char *nick) {
	struct irc_client *ret;
	pthread_mutex_lock(&clients_mutex);
	ret = (struct irc_client *) find_word_trie(clients, nick);
	pthread_mutex_unlock(&clients_mutex);
	return ret;
}

/** Atomically adds a client to the clients list if there isn't already a client with the same nickname.
	This operation is thread safe and guaranteed to be free of race conditions. The search and add operations are executed atomically.
	@param client Pointer to the new client. Cannot be `NULL`.
	@param newnick Nickname for this client.
	@return <ul>
				<li>`0` on success</li>
				<li>`CLIENT_LST_INVALID_NICK` if this client's nickname contains invalid characters, in which case nothing was added to the list
				<li>`CLIENT_LST_NO_MEM` if there isn't enough memory to create a new client entry</li>
				<li>`CLIENT_LST_ALREADY_EXISTS` if there's a known client with this nickname</li>
			</ul>
	@warning This function does not update `client->nick` to `newnick`.
	@note `newnick` is assumed to be `client`'s nickname, no matter whatever is stored in `client->nick`. This is to ease the task of adding new clients which may contain invalid characters in their nickname, but
		   we haven't yet found out.
*/
int client_list_add(struct irc_client *client, char *newnick) {
	int ret;
	pthread_mutex_lock(&clients_mutex);
	if (client_list_find_by_nick(newnick) != NULL) {
		ret = CLIENT_LST_ALREADY_EXISTS;
	}
	else {
		ret = add_word_trie(clients, newnick, (void *) client);
	}
	pthread_mutex_unlock(&clients_mutex);
	return ret == TRIE_INVALID_WORD ? CLIENT_LST_INVALID_NICK : ret == TRIE_NO_MEM ? CLIENT_LST_NO_MEM : ret;	
}

/** Deletes a client from the clients list. If no such client exists, nothing happens.
	@param client Pointer to the client that shall be deleted. Cannot be `NULL`.
*/
void client_list_delete(struct irc_client *client) {
	pthread_mutex_lock(&clients_mutex);
	(void) delete_word_trie(clients, client->nick);
	pthread_mutex_unlock(&clients_mutex);
}
