#ifndef __YAIRCD_GENERIC_LIST_GUARD__
#define __YAIRCD_GENERIC_LIST_GUARD__

/** @file
	@brief Generic thread-safe words container.
	
	This file implements a generic thread-safe words container. It uses an underlying trie implementation, and a mutex to control concurrent accesses to this trie.
	Thus, the functions provided here are nothing more than the same functions offered by the trie implementation, except that everything is wrapped in a structure
	with accesses controlled by a mutex.
	
	Two core functions, `list_find_and_execute()`, and `list_find_and_execute_globalock()`, are provided to perform atomic arbitrary operations on the list items. Besides the global locking mechanism,
	each node added to a list is packed with a lock just for itself. This allows multiple threads to work on different list elements instead of having to wait for the global lock to become available even if their
	target node was not being used.
	
	`list_find_and_execute()` uses this approach to allow different threads to work on different nodes concurrently, but it shall only be called with functions that won't invoke any deletion or insertion operations. This is
	necessary because additions and insertions to the list must be controlled by the global locking mechanism, and these functions are not called with a global lock acquired.
	
	`list_find_and_execute_globalock()` removes this limitation at the cost of executing the whole operation while holding the global lock. Thus, it is safe for functions to invoke a delete operation on the node they're working
	on (and only on that node), and the function guarantees that no other threads are waiting to acquire the node's lock once it is deleted in the current thread. See this function's documentation to learn how this is achieved.
	
	It is a very interesting and recommendable exercise to go through the trie implementation. Interested readers are invited to look at trie.c.
	
	@author Filipe Goncalves
	@author Fabio Ribeiro
	@date November 2013
	@see trie.c
*/

/** Constant value used to indicate that it is desired to free a node's data when invoking `destroy_word_list()` */
#define LIST_FREE_NODE_DATA 1

/** Constant value used to indicate that it a node's data shall not be freed when invoking `destroy_word_list()` */
#define LIST_NO_FREE_NODE_DATA 0

/** Used by list_add() to indicate that a word is invalid */
#define LST_INVALID_WORD 1

/** Used by list_add() to indicate that there isn't enough memory for a new entry */
#define LST_NO_MEM 2

/** Used by list_add() to indicate that an entry already exists */
#define LST_ALREADY_EXISTS 3

/** Opaque type for a words list that shall be used by the rest of the code */
typedef struct yaircd_list *Word_list_ptr;

/* Documented in .c source file */
Word_list_ptr init_word_list(void (*free_function)(void *), int (*is_valid)(char), char (*pos_to_char)(int), int (*char_to_pos)(char), int charcount);
void destroy_word_list(Word_list_ptr list, int free_data);
void *list_find_word(Word_list_ptr list, char *word);
void *list_find_word_nolock(Word_list_ptr list, char *word);
void *list_find_and_execute(Word_list_ptr list, char *word, void *(*match_fun)(void *, void *), void *(*nomatch_fun)(void *), void *match_fargs, void *nomatch_fargs, int *success);
void *list_find_and_execute_globalock(Word_list_ptr list, char *word, void *(*match_fun)(void *, void *), void *(*nomatch_fun)(void *), void *match_fargs, void *nomatch_fargs, int *success);
int list_add(Word_list_ptr list, void *data, char *word);
int list_add_nolock(Word_list_ptr list, void *data, char *word);
void *list_delete(Word_list_ptr list, char *word);
void *list_delete_nolock(Word_list_ptr list, char *word);
void list_for_each(Word_list_ptr list, void (*f)(void *, void *), void *fargs);

#endif /* __YAIRCD_GENERIC_LIST_GUARD__ */
