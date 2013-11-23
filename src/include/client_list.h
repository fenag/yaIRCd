#ifndef __IRC_CLIENT_LIST_GUARD__
#define __IRC_CLIENT_LIST_GUARD__
#include "client.h"
#include "list.h"

/** @file
	@brief Stores connected clients
	
	This set of functions is responsible for handling every operation that is related to the list of connected clients that the server maintains.
	With the exception of `client_list_init()` and `client_list_destroy()`, every function is thread safe.
	
	@author Filipe Goncalves
	@date November 2013
	@see trie.h
*/

/** Defines the size of the alphabet (letters in `[a-z]`). */
#define NICK_ALPHABET_SIZE 26
/** Defines how many special characters are allowed */
#define NICK_SPECIAL_CHARS_SIZE 9
/** Defines the size of the numeric alphabet (digits `[0-9]`). */
#define NICK_DIGITS_COUNT 10
/** Total number of different characters */
#define NICK_EDGES_NO NICK_ALPHABET_SIZE+NICK_SPECIAL_CHARS_SIZE

/* Documented in client_list.c */
int client_list_init(void);
void client_list_destroy(void);
void *client_list_find_and_execute(char *nick, void *(*f)(void *, void *), void *fargs, int *success);
int client_list_add(struct irc_client *client, char *newnick);
void client_list_delete(struct irc_client *client);
int nick_is_valid(char s);
char nick_pos_to_char(int i);
int nick_char_to_pos(char s);

#endif /* __IRC_CLIENT_LIST_GUARD__ */
